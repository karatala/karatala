#include "InstrumentPlayer.h"
#include "ArticulationNameHelpers.h"
#include "DrumKitEngine.h"
#include "MicRoutingBuilder.h"
#include "SampleLoader.h"
#include <cmath>

const MicLayout* InstrumentPlayer::getMicLayout() const { return this->parent ? this->parent->getMicLayout() : nullptr; }
const MicRoutingPlan* InstrumentPlayer::getMicRoutingPlan() const { return this->parent ? this->parent->getMicRoutingPlan() : nullptr; }
SampleLoader* InstrumentPlayer::getSampleLoader() { return this->parent ? this->parent->getSampleLoader() : nullptr; }

void InstrumentPlayer::ArticulationRuntime::buildVelocityMapping(const VelocityResponse& velocityResponse) {
    this->velocityToLevel.fill(-1);
    this->velocityGain.fill(0.0f);
    this->selectionTargetDb.fill(-100.0f);
    this->calibrated = !this->velocityLevels.empty();
    for (const auto& velocityLevel : this->velocityLevels) {
        if (!velocityLevel.fittedSourceDb.has_value() || !velocityLevel.calibrationTrimDb.has_value()) {
            this->calibrated = false;
            break;
        }
    }
    if (this->velocityLevels.empty()) return;

    const VelocityResponse& effectiveVelocityResponse = this->parameters.velocityResponse.has_value() ? *this->parameters.velocityResponse : velocityResponse;
    for (int velocity = 0; velocity <= MIDI_MAX_VALUE; ++velocity) {
        VelocityLevelIdx chosenVelocityLevelIdx = static_cast<VelocityLevelIdx>(this->velocityLevels.size() - 1);
        for (size_t velocityLevelIdx = 0; velocityLevelIdx < this->velocityLevels.size(); ++velocityLevelIdx) {
            const auto& velocityLevel = this->velocityLevels[velocityLevelIdx];
            const int upperVelocity = velocityLevel.selectionUpperVelocity > 0 ? velocityLevel.selectionUpperVelocity : (velocityLevelIdx + 1 == this->velocityLevels.size() ? MIDI_MAX_VALUE : velocityLevel.velocity);
            if (velocity <= upperVelocity) {
                chosenVelocityLevelIdx = static_cast<VelocityLevelIdx>(velocityLevelIdx);
                break;
            }
        }
        this->velocityToLevel[static_cast<size_t>(velocity)] = chosenVelocityLevelIdx;
        if (this->calibrated) this->selectionTargetDb[static_cast<size_t>(velocity)] = *this->velocityLevels[static_cast<size_t>(chosenVelocityLevelIdx)].fittedSourceDb;
        this->velocityGain[static_cast<size_t>(velocity)] = effectiveVelocityResponse.getGain(static_cast<juce::uint8>(velocity));
    }
}

void InstrumentPlayer::rebuildVelocityResponseTables() {
    if (this->parent == nullptr) return;
    for (auto& articulation : this->runtimeArticulations) articulation.buildVelocityMapping(this->parent->getVelocityResponse());
}

bool InstrumentPlayer::refreshLibraryVelocityBoundaries(const KitLib::ArticulationSpec& libraryArticulation) {
    std::vector<uint8_t> upperBoundaries;
    upperBoundaries.reserve(libraryArticulation.velocityLevels.size());
    for (const auto& velocityLevelSpec : libraryArticulation.velocityLevels) upperBoundaries.push_back(velocityLevelSpec != nullptr ? velocityLevelSpec->selectionUpperVelocity : 0);
    return this->refreshLibraryVelocityBoundaries(libraryArticulation, upperBoundaries);
}

bool InstrumentPlayer::refreshLibraryVelocityBoundaries(const KitLib::ArticulationSpec& libraryArticulation, const std::vector<uint8_t>& upperBoundaries) {
    if (this->parent == nullptr) return false;
    if (upperBoundaries.size() != libraryArticulation.velocityLevels.size()) return false;
    bool changed = false;
    for (auto& runtimeArticulation : this->runtimeArticulations) {
        bool articulationChanged = false;
        for (auto& runtimeVelocityLevel : runtimeArticulation.velocityLevels) {
            for (size_t velocityLevelSpecIdx = 0; velocityLevelSpecIdx < libraryArticulation.velocityLevels.size(); ++velocityLevelSpecIdx) {
                if (runtimeVelocityLevel.velocityLevelSpec != libraryArticulation.velocityLevels[velocityLevelSpecIdx].get()) continue;
                runtimeVelocityLevel.selectionUpperVelocity = upperBoundaries[velocityLevelSpecIdx];
                articulationChanged = true;
                break;
            }
        }
        if (!articulationChanged) continue;
        runtimeArticulation.buildVelocityMapping(this->parent->getVelocityResponse());
        changed = true;
    }
    return changed;
}

bool InstrumentPlayer::refreshLibraryBalance(const KitLib::InstrumentSpec& libraryInstrument, const KitLib::ArticulationSpec* libraryArticulation) {
    bool changed = false;
    for (const auto& sound : this->sampledSoundPtrs) {
        if (sound == nullptr || sound->instrumentSpec != &libraryInstrument || sound->sampleSpec == nullptr || sound->sampleSpec->parent == nullptr || sound->sampleSpec->parent->parent == nullptr) continue;
        const auto* soundArticulation = sound->sampleSpec->parent->parent->parent;
        if (soundArticulation == nullptr || (libraryArticulation != nullptr && soundArticulation != libraryArticulation)) continue;
        const std::optional<int> effectiveBalance = this->instrumentSlot.balance.has_value()
            ? this->instrumentSlot.balance
            : (soundArticulation->balanceConflict && !soundArticulation->balanceOverride.has_value() ? std::optional<int> {} : soundArticulation->getEffectiveBalance());
        sound->placementBalance = effectiveBalance.value_or(0);
        const auto* layerParameters = this->getLayerParameters(static_cast<ArtRtIdx>(sound->runtimeArticulationIndex));
        InstrumentPlayer::updateSoundMicPlacementGains(*sound, layerParameters != nullptr ? layerParameters->pan : 0);
        changed = true;
    }
    return changed;
}

bool InstrumentPlayer::refreshSamplePlaybackOffset(const KitLib::SampleSpec& sampleSpec) {
    bool changed = false;
    for (const auto& sound : this->sampledSoundPtrs) {
        if (sound == nullptr || sound->sampleSpec != &sampleSpec) continue;
        sound->playbackOffsetMs = sampleSpec.playbackOffsetMs;
        changed = true;
    }
    return changed;
}

SampledSound::SourceRecipeStatus InstrumentPlayer::getSourceRecipeStatus(const KitLib::SampleSpec& sampleSpec) const {
    bool applied = false;
    for (const auto& sound : this->sampledSoundPtrs) {
        if (sound == nullptr || sound->sampleSpec != &sampleSpec) continue;
        const auto status = sound->sourceRecipeStatus.load(std::memory_order_acquire);
        if (status == SampledSound::SourceRecipeStatus::Failed) return status;
        if (status == SampledSound::SourceRecipeStatus::Pending) return status;
        if (status == SampledSound::SourceRecipeStatus::Applied) applied = true;
    }
    if (applied) return SampledSound::SourceRecipeStatus::Applied;
    return SampledSound::SourceRecipeStatus::NotLoaded;
}

bool InstrumentPlayer::refreshLibraryCalibrationAndReload(const KitLib::ArticulationSpec& libraryArticulation) {
    if (this->parent == nullptr) return false;
    auto* loader = this->getSampleLoader();
    if (loader == nullptr) return false;

    bool changed = false;
    for (auto& runtimeArticulation : this->runtimeArticulations) {
        bool articulationChanged = false;
        for (auto& runtimeVelocityLevel : runtimeArticulation.velocityLevels) {
            const KitLib::VelocityLevelSpec* velocityLevelSpec = nullptr;
            for (const auto& candidateVelocityLevel : libraryArticulation.velocityLevels) {
                if (candidateVelocityLevel.get() == runtimeVelocityLevel.velocityLevelSpec) {
                    velocityLevelSpec = candidateVelocityLevel.get();
                    break;
                }
            }
            if (velocityLevelSpec == nullptr) continue;
            runtimeVelocityLevel.fittedSourceDb = velocityLevelSpec->fittedSourceDb;
            runtimeVelocityLevel.calibrationTrimDb = velocityLevelSpec->calibrationTrimDb;
            for (auto& logicalHit : runtimeVelocityLevel.logicalHits) {
                logicalHit.preTrimIntensityDb.reset();
                for (const auto& logicalHitSpec : velocityLevelSpec->logicalHits) {
                    if (logicalHitSpec->roundRobinId != logicalHit.roundRobinId) continue;
                    logicalHit.preTrimIntensityDb = logicalHitSpec->intensityDb;
                    break;
                }
            }
            articulationChanged = true;
        }
        if (articulationChanged) {
            runtimeArticulation.buildVelocityMapping(this->parent->getVelocityResponse());
            changed = true;
        }
    }

    std::unordered_set<SampledSound*> scheduledSounds;
    for (auto& runtimeArticulation : this->runtimeArticulations) {
        for (auto& runtimeVelocityLevel : runtimeArticulation.velocityLevels) {
            bool belongsToLibraryArticulation = false;
            for (const auto& velocityLevelSpec : libraryArticulation.velocityLevels) {
                if (velocityLevelSpec.get() == runtimeVelocityLevel.velocityLevelSpec) {
                    belongsToLibraryArticulation = true;
                    break;
                }
            }
            if (!belongsToLibraryArticulation) continue;
            const float calibrationTrimDb = runtimeArticulation.calibrated ? runtimeVelocityLevel.calibrationTrimDb.value_or(0.0f) : 0.0f;
            for (size_t sampleEntryIdx = 0; sampleEntryIdx < runtimeVelocityLevel.samples.size(); ++sampleEntryIdx) {
                auto& sampleEntry = runtimeVelocityLevel.samples[sampleEntryIdx];
                auto* sound = sampleEntry.sound;
                if (sound == nullptr || sound->sampleSpec == nullptr || !scheduledSounds.insert(sound).second) continue;
                const float requestedFixedGainDb = sound->sampleSpec->manualGainDb + calibrationTrimDb;
                sound->decayEnvelope = sound->sampleSpec->decayEnvelope;
                if (runtimeArticulation.calibrated && sound->decayEnvelope.isValid()) sound->decayEnvelope.peakDb += calibrationTrimDb;
                sound->requestedFixedGainDb = requestedFixedGainDb;
                const auto revision = sound->sourceRecipeRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
                const float correctionDb = requestedFixedGainDb - sound->canonicalFixedGainDb;
                sound->cachedSourceGainCorrection.store(juce::Decibels::decibelsToGain(correctionDb, -100.0f), std::memory_order_release);
                for (auto* voice : this->voices) {
                    if (!voice->isVoiceActive() || voice->getCurrentlyPlayingSound() != sound) continue;
                    voice->setSourceGainCorrectionDb(requestedFixedGainDb - voice->currentCanonicalFixedGainDb);
                }
                if (std::abs(correctionDb) < 0.0005f && sound->buffer != nullptr) {
                    sound->cachedSourceGainCorrection.store(1.0f, std::memory_order_release);
                    sound->sourceRecipeStatus.store(SampledSound::SourceRecipeStatus::Applied, std::memory_order_release);
                    continue;
                }

                sound->sourceRecipeStatus.store(SampledSound::SourceRecipeStatus::Pending, std::memory_order_release);
                SampleLoadSpec loadSpec;
                loadSpec.file = sound->sampleSpec->sampleFile;
                loadSpec.contentHash = sound->sampleSpec->metadataContentHash;
                loadSpec.metadataFileSizeBytes = sound->sampleSpec->metadataFileSizeBytes;
                loadSpec.fixedGainDb = requestedFixedGainDb;
                const auto weakSound = sampleEntry.weakSound;
                std::shared_ptr<LogicalHitReadiness> readiness;
                for (const auto& logicalHit : runtimeVelocityLevel.logicalHits) {
                    if (std::find(logicalHit.sampleEntryIndexes.begin(), logicalHit.sampleEntryIndexes.end(), sampleEntryIdx) != logicalHit.sampleEntryIndexes.end()) {
                        readiness = logicalHit.readiness;
                        break;
                    }
                }
                loader->requestAsyncLoad(loadSpec, [this, weakSound, readiness, revision, requestedFixedGainDb](std::shared_ptr<juce::AudioBuffer<float>> buffer, double loadedSampleRate) {
                    auto soundLocked = weakSound.lock();
                    if (soundLocked == nullptr || this->parent == nullptr) return;
                    const bool published = this->parent->publishSourceRecipeReload(soundLocked, revision, requestedFixedGainDb, std::move(buffer), loadedSampleRate);
                    if (readiness != nullptr && soundLocked->sourceRecipeRevision.load(std::memory_order_acquire) == revision && !soundLocked->readinessReported.exchange(true, std::memory_order_acq_rel)) readiness->markLoaded(published);
                });
            }
        }
    }
    return changed;
}

void InstrumentPlayer::storeRoutingContext(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationLibraryMap& articulationLibraryMap, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, bool strictArticulationResolution, const std::unordered_map<juce::String, juce::String>& micRouteOverrides) {
    this->lastRoutingContext.defaultInstrument = defaultInstSpec;
    this->lastRoutingContext.articulationLibraryMap = articulationLibraryMap;
    this->lastRoutingContext.allowedArticulationNameIds = allowedArticulationNameIds;
    this->lastRoutingContext.allowedArticulationFilters = allowedArticulationFilters;
    this->lastRoutingContext.artNameRemap = artNameRemap;
    this->lastRoutingContext.artExactRemap = artExactRemap;
    this->lastRoutingContext.artAliases = artAliases;
    this->lastRoutingContext.setMicRouteOverrides(micRouteOverrides);
    this->lastRoutingContext.strictArticulationResolution = strictArticulationResolution;
}

void InstrumentPlayer::resetRoutingTables() {
    this->noteToArticulation.fill(-1);
    this->rebuildHiHatNoteChokeValues();
    this->noteToControllers.clear();
    this->ccToArticulationMask.fill(false);
    this->lastCcArticulation.fill(-1);
    this->lastCcValue.fill(0);
    this->pendingCcValue.fill(0);
    this->pendingCcMidiChannel.fill(0);
    this->pendingCcTransition.fill(false);
    this->pendingCcArticulation.fill(-1);
    this->hihatPedalClosedState.fill(false);
    this->hihatImplicitPedalEligible.fill(false);
    this->lastCcProcessedSample.fill(0);
    this->hasProcessedCcTransition.fill(false);
    this->hihatPedalNote = -1;
    const auto pedalNameId = StringsDictSingleton::getId("pedal");
    for (const auto& articulation : this->instrumentSlot.articulations) {
        if (articulation.nameId == pedalNameId && !articulation.notes.empty()) {
            this->hihatPedalNote = articulation.notes.front();
            break;
        }
    }
    this->renderedSampleCount = 0;
    this->processedHiHatTransitions.store(0, std::memory_order_relaxed);
    this->coalescedHiHatTransitions.store(0, std::memory_order_relaxed);
    this->skippedHiHatTransitions.store(0, std::memory_order_relaxed);
    this->completedHiHatTransitions.store(0, std::memory_order_relaxed);
    for (auto& map : this->ccToArticulationTable) map.fill(0);
}

void InstrumentPlayer::rebuildArtLookupCache() {
    this->artIdByNameExprId.clear();
    for (int artIdx = 0; artIdx < static_cast<int>(this->runtimeArticulations.size()); ++artIdx) {
        const auto& art = this->runtimeArticulations[static_cast<size_t>(artIdx)];
        if (art.routeArticulationSlotNameId != 0 || art.routeArticulationSlotExpressionId != 0) continue;
        const uint64_t key = (static_cast<uint64_t>(art.nameId) << 32) | static_cast<uint64_t>(art.exprId);
        this->artIdByNameExprId.emplace(key, artIdx);
    }
    for (int artIdx = 0; artIdx < static_cast<int>(this->runtimeArticulations.size()); ++artIdx) {
        const auto& art = this->runtimeArticulations[static_cast<size_t>(artIdx)];
        const uint64_t key = (static_cast<uint64_t>(art.nameId) << 32) | static_cast<uint64_t>(art.exprId);
        this->artIdByNameExprId.emplace(key, artIdx);
    }
}

bool InstrumentPlayer::tryGetRuntimeArticulationIdx(const KitLib::ArticulationSpec* artSpec, bool createIfMissing, ArtRtIdx& artIdx) {
    if (artSpec == nullptr) return false;
    if (createIfMissing) {
        artIdx = this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId);
        return true;
    }
    artIdx = this->findArticulationIndex(artSpec->nameId, artSpec->exprId);
    return artIdx >= 0;
}

bool InstrumentPlayer::playbackEntryUsesArticulationSlotRuntimeKey(const ArticulationPlaybackEntry& playbackEntry) const {
    if (playbackEntry.isAliasEntry || playbackEntry.libraryArticulation == nullptr) return false;
    if (playbackEntry.isControllerSibling && !playbackEntry.usesExplicitMapLayer) return false;
    if (playbackEntry.usesExplicitMapLayer) return true;
    return playbackEntry.articulationSlotNameId != playbackEntry.libraryArticulation->nameId || playbackEntry.articulationSlotExpressionId != playbackEntry.libraryArticulation->exprId;
}

InstrumentPlayer::ArtRtIdx InstrumentPlayer::findRuntimeArticulationIndex(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId, StringsDictSingleton::Id articulationSlotNameId, StringsDictSingleton::Id articulationSlotExpressionId) const {
    for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx) {
        const auto& artRt = this->runtimeArticulations[static_cast<size_t>(artIdx)];
        if (artRt.nameId == nameId && artRt.exprId == exprId && artRt.routeArticulationSlotNameId == articulationSlotNameId && artRt.routeArticulationSlotExpressionId == articulationSlotExpressionId)
            return static_cast<ArtRtIdx>(artIdx);
    }
    return -1;
}

InstrumentPlayer::ArtRtIdx InstrumentPlayer::getOrAddRuntimeArticulationIdx(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId, StringsDictSingleton::Id articulationSlotNameId, StringsDictSingleton::Id articulationSlotExpressionId) {
    const auto existingArtIdx = this->findRuntimeArticulationIndex(nameId, exprId, articulationSlotNameId, articulationSlotExpressionId);
    if (existingArtIdx >= 0) return existingArtIdx;
    this->runtimeArticulations.emplace_back(nameId, exprId);
    auto& artRt = this->runtimeArticulations.back();
    artRt.routeArticulationSlotNameId = articulationSlotNameId;
    artRt.routeArticulationSlotExpressionId = articulationSlotExpressionId;
    return static_cast<ArtRtIdx>(this->runtimeArticulations.size() - 1);
}

bool InstrumentPlayer::tryGetRuntimeArticulationIdx(const ArticulationPlaybackEntry& playbackEntry, bool createIfMissing, ArtRtIdx& artIdx) {
    const auto* artSpec = playbackEntry.libraryArticulation;
    if (artSpec == nullptr) return false;
    if (!this->playbackEntryUsesArticulationSlotRuntimeKey(playbackEntry)) return this->tryGetRuntimeArticulationIdx(artSpec, createIfMissing, artIdx);

    if (createIfMissing) {
        artIdx = this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId, playbackEntry.articulationSlotNameId, playbackEntry.articulationSlotExpressionId);
        return true;
    }
    artIdx = this->findRuntimeArticulationIndex(artSpec->nameId, artSpec->exprId, playbackEntry.articulationSlotNameId, playbackEntry.articulationSlotExpressionId);
    return artIdx >= 0;
}

void InstrumentPlayer::unloadInstrument() {
    this->runtimeArticulations.clear();
    this->resetRoutingTables();
    this->aftertouchChokeTransitionCommitted = {};
    // stop and clear voices before releasing sounds
    this->clearVoices();
    this->clearSounds();
    this->instrumentSpecstPtr = nullptr;
    this->librarySnapshotPtr.reset();
    this->lastRoutingContext = {};
    this->clearArticulationSlotRoutes();
    this->articulationPlaybackEntries.clear();
    this->clearParameterOverrides();
}

void InstrumentPlayer::loadInstrument(KitLib::InstrumentSpec* instSpec) {
    this->loadInstrument(instSpec, nullptr);
}

void InstrumentPlayer::loadInstrument(KitLib::InstrumentSpec* instSpec, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot) {
    if (instSpec == nullptr) return;
    this->unloadInstrument();

    this->librarySnapshotPtr = std::move(librarySnapshot);
    this->instrumentSpecstPtr = instSpec;
    this->type = instSpec->meta.type;

    this->processArticulationSlots(instSpec, {});
}

void InstrumentPlayer::loadInstrumentWithRouting(KitLib::InstrumentSpec* defaultInstrument, const ArticulationLibraryMap& articulationLibraryMap) {
    this->loadInstrumentWithRouting(defaultInstrument, articulationLibraryMap, {});
}

void InstrumentPlayer::loadInstrumentWithRouting(KitLib::InstrumentSpec* defaultInstrument, const ArticulationLibraryMap& articulationLibraryMap, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, bool strictArticulationResolution, const std::unordered_map<juce::String, juce::String>& micRouteOverrides) {
    this->loadInstrumentWithRouting(defaultInstrument, articulationLibraryMap, allowedArticulationNameIds, allowedArticulationFilters, artNameRemap, artExactRemap, artAliases, nullptr, strictArticulationResolution, micRouteOverrides);
}

void InstrumentPlayer::loadInstrumentWithRouting(KitLib::InstrumentSpec* defaultInstrument, const ArticulationLibraryMap& articulationLibraryMap, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot, bool strictArticulationResolution, const std::unordered_map<juce::String, juce::String>& micRouteOverrides) {
    if (defaultInstrument == nullptr) return;
    this->unloadInstrument();

    this->librarySnapshotPtr = std::move(librarySnapshot);
    this->instrumentSpecstPtr = defaultInstrument;
    this->type = defaultInstrument->meta.type;

    this->storeRoutingContext(defaultInstrument, articulationLibraryMap, allowedArticulationNameIds, allowedArticulationFilters, artNameRemap, artExactRemap, artAliases, strictArticulationResolution, micRouteOverrides);
    this->processArticulationSlots(defaultInstrument, articulationLibraryMap, allowedArticulationNameIds, allowedArticulationFilters, artNameRemap, artExactRemap, artAliases, strictArticulationResolution);
}

void InstrumentPlayer::loadInstrumentWithPlaybackEntries(KitLib::InstrumentSpec* defaultInstrument, const RoutingContext& routingContext, std::vector<ArticulationPlaybackEntry> playbackEntries, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot) {
    if (defaultInstrument == nullptr) return;
    this->unloadInstrument();

    this->librarySnapshotPtr = std::move(librarySnapshot);
    this->instrumentSpecstPtr = defaultInstrument;
    this->type = defaultInstrument->meta.type;
    this->lastRoutingContext = routingContext;
    this->lastRoutingContext.rebuildMicRouteOverrideIds();
    if (!this->lastRoutingContext.isValid()) this->lastRoutingContext.defaultInstrument = defaultInstrument;

    LOG(Debug, "loadInstrumentWithPlaybackEntries: available articulationSpecs for inst=" + defaultInstrument->meta.codeRaw + ":");
    for (const auto& artSpec : defaultInstrument->articulationSpecs) {
        this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId);
    }

    this->articulationPlaybackEntries = std::move(playbackEntries);
    this->applyArticulationPlaybackEntries(true, true);
    this->rebuildArtLookupCache();
}

void InstrumentPlayer::clearParameterOverrides() {
    this->setTuningOffsetCents(0);
    this->setAttackOverrideMs(-1);
    this->setDecayOverrideMs(-1);
    this->setVolumeDb(0.0f);
    this->setPan(0);
    this->setFlipLR(false);
    this->setFlipPhase(false);
    this->micBleedLevels.clear();
    this->micBleedLevelsById.clear();
    this->refreshSampleMicBleedGains();
}

void InstrumentPlayer::setTuningOffsetCents(int cents) {
    this->tuningOffsetCents = cents;
    for (auto& art : this->runtimeArticulations) art.parameters.tuningCents = cents;
}

void InstrumentPlayer::setAttackOverrideMs(int ms) {
    if (ms < 0) this->attackOverrideMs.reset();
    else this->attackOverrideMs = ms;
    for (auto& art : this->runtimeArticulations) art.parameters.attackMs = ms;
}

void InstrumentPlayer::setDecayOverrideMs(int ms) {
    if (ms < 0) this->decayOverrideMs.reset();
    else this->decayOverrideMs = ms;
    for (auto& art : this->runtimeArticulations) art.parameters.decayMs = ms;
}

void InstrumentPlayer::setVolumeDb(float db) {
    this->volumeDb = db;
    for (auto& art : this->runtimeArticulations) art.parameters.volumeDb = db;
}

void InstrumentPlayer::configureSoundMicPlacement(
    SampledSound& sound, const KitLib::MicSpec& micSpec, int sourceChannelCount, int destinationChannelCount,
    bool routingAllowsPan, std::optional<int> placementBalance, int layerPan) {
    sound.placementBalance = placementBalance.value_or(0);
    if (destinationChannelCount < 2) {
        sound.micPlacementMode = MicPlacementMode::PreserveChannels;
    } else if (micSpec.type == KitLib::MicSpec::Type::Room) {
        sound.micPlacementMode = sourceChannelCount == 1 ? MicPlacementMode::MonoCenter : MicPlacementMode::PreserveChannels;
    } else if (sourceChannelCount == 1) {
        sound.micPlacementMode = MicPlacementMode::MonoSourceAndLayerPan;
    } else if (micSpec.type == KitLib::MicSpec::Type::Overhead || routingAllowsPan) {
        sound.micPlacementMode = MicPlacementMode::StereoLayerPan;
    } else {
        sound.micPlacementMode = MicPlacementMode::PreserveChannels;
    }
    InstrumentPlayer::updateSoundMicPlacementGains(sound, layerPan);
}

void InstrumentPlayer::updateSoundMicPlacementGains(SampledSound& sound, int layerPan) {
    int effectivePan = 0;
    if (sound.micPlacementMode == MicPlacementMode::MonoSourceAndLayerPan) effectivePan = juce::jlimit(-127, 127, sound.placementBalance + layerPan);
    else if (sound.micPlacementMode == MicPlacementMode::StereoLayerPan) effectivePan = juce::jlimit(-127, 127, layerPan);

    const float normalizedPan = static_cast<float>(effectivePan) / 127.0f;
    float leftGain = 1.0f;
    float rightGain = 1.0f;
    if (sound.micPlacementMode == MicPlacementMode::MonoCenter || sound.micPlacementMode == MicPlacementMode::MonoSourceAndLayerPan) {
        const float panAngle = (normalizedPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        leftGain = std::cos(panAngle);
        rightGain = std::sin(panAngle);
    } else if (sound.micPlacementMode == MicPlacementMode::StereoLayerPan) {
        leftGain = normalizedPan > 0.0f ? 1.0f - normalizedPan : 1.0f;
        rightGain = normalizedPan < 0.0f ? 1.0f + normalizedPan : 1.0f;
    }
    sound.cachedPlacementLeftGain.store(leftGain, std::memory_order_relaxed);
    sound.cachedPlacementRightGain.store(rightGain, std::memory_order_relaxed);
}

void InstrumentPlayer::refreshArticulationMicPlacementGains(ArticulationRuntime& art) {
    for (auto& velocityLevel : art.velocityLevels)
        for (auto& sample : velocityLevel.samples)
            if (sample.sound != nullptr) InstrumentPlayer::updateSoundMicPlacementGains(*sample.sound, art.parameters.pan);
}

void InstrumentPlayer::setPan(int newPan) {
    const int clampedPan = juce::jlimit(-127, 127, newPan);
    this->pan.store(clampedPan, std::memory_order_relaxed);
    for (auto& art : this->runtimeArticulations) {
        art.parameters.pan = clampedPan;
        this->refreshArticulationMicPlacementGains(art);
    }
}

void InstrumentPlayer::setFlipLR(bool shouldFlip) {
    this->flipLR = shouldFlip;
    for (auto& art : this->runtimeArticulations) art.parameters.flipLR = shouldFlip;
}

void InstrumentPlayer::setFlipPhase(bool shouldFlip) {
    this->flipPhase = shouldFlip;
    for (auto& art : this->runtimeArticulations) art.parameters.flipPhase = shouldFlip;
}

void InstrumentPlayer::setMicBleedLevels(std::unordered_map<juce::String, float> levels) {
    this->micBleedLevels.clear();
    this->micBleedLevelsById.clear();
    for (auto& [micCode, level] : levels) {
        const float clamped = juce::jlimit(0.0f, 1.0f, level);
        if (clamped < 0.999f) {
            this->micBleedLevels[micCode] = clamped;
            const auto micCodeId = StringsDictSingleton::getId(micCode);
            if (micCodeId != StringsDictSingleton::EMPTY) this->micBleedLevelsById[micCodeId] = clamped;
        }
    }
    this->refreshSampleMicBleedGains();
}

const InstrumentPlayer::LayerParameters* InstrumentPlayer::getLayerParameters(ArtRtIdx artIdx) const {
    if (artIdx < 0 || static_cast<size_t>(artIdx) >= this->runtimeArticulations.size()) return nullptr;
    return &this->runtimeArticulations[static_cast<size_t>(artIdx)].parameters;
}

void InstrumentPlayer::applyParameterUpdate(ArticulationRuntime& art, const LayerParameterUpdate& update) {
    if (update.tuningCents.has_value()) art.parameters.tuningCents = *update.tuningCents;
    if (update.attackMs.has_value()) art.parameters.attackMs = *update.attackMs;
    if (update.decayMs.has_value()) art.parameters.decayMs = *update.decayMs;
    if (update.volumeDb.has_value()) art.parameters.volumeDb = *update.volumeDb;
    if (update.pan.has_value()) {
        art.parameters.pan = juce::jlimit(-127, 127, *update.pan);
        this->refreshArticulationMicPlacementGains(art);
    }
    if (update.flipLR.has_value()) art.parameters.flipLR = *update.flipLR;
    if (update.flipPhase.has_value()) art.parameters.flipPhase = *update.flipPhase;
    if (update.updateVelocityResponse) {
        art.parameters.velocityResponse = update.velocityResponse;
        if (this->parent != nullptr) art.buildVelocityMapping(this->parent->getVelocityResponse());
    }
    if (update.micBleedLevel.has_value()) {
        const auto targetMicCodeId = StringsDictSingleton::getId(update.micBleedLevel->first.trim().toLowerCase());
        const float targetLevel = juce::jlimit(0.0f, 1.0f, update.micBleedLevel->second);
        for (const auto& velocityLevel : art.velocityLevels) {
            for (const auto& sample : velocityLevel.samples) {
                if (sample.sound == nullptr || sample.sound->sampleSpec == nullptr) continue;
                const auto micCodeId = StringsDictSingleton::getId(sample.sound->sampleSpec->micSpec.raw.trim().toLowerCase());
                if (micCodeId == targetMicCodeId) sample.sound->cachedMicBleedGain.store(targetLevel, std::memory_order_relaxed);
            }
        }
    }
    if (!update.micBleedLevels.has_value()) return;

    std::unordered_map<StringsDictSingleton::Id, float> levelsByMicId;
    for (const auto& [micCode, level] : *update.micBleedLevels) {
        const auto micCodeId = StringsDictSingleton::getId(micCode.trim().toLowerCase());
        if (micCodeId != StringsDictSingleton::EMPTY) levelsByMicId[micCodeId] = juce::jlimit(0.0f, 1.0f, level);
    }
    for (const auto& velocityLevel : art.velocityLevels) {
        for (const auto& sample : velocityLevel.samples) {
            if (sample.sound == nullptr || sample.sound->sampleSpec == nullptr) continue;
            const auto micCodeId = StringsDictSingleton::getId(sample.sound->sampleSpec->micSpec.raw.trim().toLowerCase());
            const auto levelIt = levelsByMicId.find(micCodeId);
            const float level = levelIt != levelsByMicId.end() ? levelIt->second : 1.0f;
            sample.sound->cachedMicBleedGain.store(level, std::memory_order_relaxed);
        }
    }
}

bool InstrumentPlayer::updateArticulationParameters(const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes, const LayerParameterUpdate& update) {
    std::array<bool, 128> matchedArticulations {};
    if (targetRoutes.empty()) {
        for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx)
            if (!this->runtimeArticulations[artIdx].velocityLevels.empty()) matchedArticulations[artIdx] = true;
    } else {
        for (const auto& playbackEntry : this->articulationPlaybackEntries) {
            bool targetMatches = false;
            for (const auto& targetRoute : targetRoutes) {
                if (playbackEntry.articulationSlotNameId == targetRoute.first && playbackEntry.articulationSlotExpressionId == targetRoute.second) {
                    targetMatches = true;
                    break;
                }
            }
            if (!targetMatches) continue;
            ArtRtIdx artIdx = -1;
            if (this->tryGetRuntimeArticulationIdx(playbackEntry, false, artIdx) && artIdx >= 0) matchedArticulations[static_cast<size_t>(artIdx)] = true;
            for (const auto note : playbackEntry.midiNotes) {
                if (note >= this->noteToArticulation.size()) continue;
                const auto mappedArtIdx = this->noteToArticulation[static_cast<size_t>(note)];
                if (mappedArtIdx >= 0) matchedArticulations[static_cast<size_t>(mappedArtIdx)] = true;
            }
        }
        for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx) {
            const auto& art = this->runtimeArticulations[artIdx];
            const auto articulationSlotNameId = art.routeArticulationSlotNameId != 0 ? art.routeArticulationSlotNameId : art.nameId;
            const auto articulationSlotExpressionId = art.routeArticulationSlotNameId != 0 || art.routeArticulationSlotExpressionId != 0 ? art.routeArticulationSlotExpressionId : art.exprId;
            for (const auto& targetRoute : targetRoutes) {
                if (articulationSlotNameId == targetRoute.first && articulationSlotExpressionId == targetRoute.second) matchedArticulations[artIdx] = true;
            }
        }
    }

    bool updated = false;
    for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx) {
        if (!matchedArticulations[artIdx]) continue;
        this->applyParameterUpdate(this->runtimeArticulations[artIdx], update);
        updated = true;
    }
    return updated;
}

float InstrumentPlayer::getMicBleedLevel(const juce::String& micCode) const {
    if (micCode.isEmpty()) return 1.0f;
    return this->getMicBleedLevel(StringsDictSingleton::getId(micCode));
}

float InstrumentPlayer::getMicBleedLevel(StringsDictSingleton::Id micCodeId) const {
    if (micCodeId == StringsDictSingleton::EMPTY) return 1.0f;

    const auto it = this->micBleedLevelsById.find(micCodeId);
    if (it == this->micBleedLevelsById.end()) return 1.0f;

    return juce::jlimit(0.0f, 1.0f, it->second);
}

void InstrumentPlayer::refreshSampleMicBleedGains() {
    for (const auto& soundPtr : this->sampledSoundPtrs) {
        if (!soundPtr || soundPtr->sampleSpec == nullptr) continue;
        soundPtr->cachedMicBleedGain.store(this->getMicBleedLevel(soundPtr->sampleSpec->micSpec.raw), std::memory_order_relaxed);
    }
}

KitLib::ArticulationSpec* InstrumentPlayer::resolveArticulationSpec(KitLib::InstrumentSpec* instSpec, const ArticulationSlot& articulationSlot, bool allowFallback) {
    if (instSpec == nullptr) return nullptr;

    for (const auto& artSpec : instSpec->articulationSpecs) {
        if (artSpec->nameId == articulationSlot.nameId && artSpec->exprId == articulationSlot.exprId) return artSpec.get();
    }
    if (!allowFallback || this->getEngine() == nullptr) return nullptr;

    const auto& fallbackIdMap = this->getEngine()->getArticulationFallbackIdMap();
    auto it = fallbackIdMap.find(articulationSlot.nameId);
    if (it == fallbackIdMap.end()) return nullptr;

    for (const auto& fallbackId : it->second) {
        for (const auto& artSpec : instSpec->articulationSpecs) {
            if (artSpec->nameId == fallbackId && artSpec->exprId == articulationSlot.exprId) return artSpec.get();
        }
    }
    return nullptr;
}

std::vector<uint8_t> InstrumentPlayer::getEffectiveExpressionValues(const KitLib::ArticulationSpec& artSpec, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) const {
    for (const auto& dim : artSpec.expressionDimensions) {
        if (dim != nullptr && dim->type == dimensionType && !dim->values.empty())
            return dim->values;
    }

    const auto code = KitLib::ArticulationSpec::ExpressionDimension::typeToString(dimensionType);
    if (!KitLayout::isSupportedExpressionPointCode(code))
        return {};
        
    const auto label = KitLayout::expressionPointLabelForArticulation(artSpec, dimensionType);
    if (label.isEmpty())
        return {};

    if (const auto value = KitLayout::getExpressionPoint(this->instrumentSlot.expressionPoints, code, label); value.has_value())
        return { *value };

    if (this->parent != nullptr) {
        if (const auto* layout = this->parent->getKitLayout()) {
            if (const auto value = KitLayout::getExpressionPoint(layout->expressionPoints, code, label); value.has_value())
                return { *value };
        }
    }

    const auto defaults = KitLayout::getDefaultExpressionPoints();
    if (const auto value = KitLayout::getExpressionPoint(defaults, code, label); value.has_value())
        return { *value };

    return {};
}

bool InstrumentPlayer::hasEffectiveExpressionValues(const KitLib::ArticulationSpec& artSpec, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) const {
    return !this->getEffectiveExpressionValues(artSpec, dimensionType).empty();
}

void InstrumentPlayer::refreshRuntimeExpressionValues(const KitLib::ArticulationSpec& artSpec, ArticulationRuntime& artRt) {
    auto maxExpressionValue = [this, &artSpec](KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) {
        const auto values = this->getEffectiveExpressionValues(artSpec, dimensionType);
        if (values.empty()) return -1;
        return static_cast<int>(*std::max_element(values.begin(), values.end()));
    };

    artRt.pedalOpenCloseValue = maxExpressionValue(KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose);
    artRt.zoneCenterRimValue = maxExpressionValue(KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneCenterRim);
    artRt.zoneBowEdgeValue = maxExpressionValue(KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneBowEdge);
}

std::vector<InstrumentPlayer::ResolvedArticulationSlotBinding> InstrumentPlayer::collectResolvedArticulationSlots(const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext) {
    std::vector<ResolvedArticulationSlotBinding> bindings;

    if (!routingContext.isValid()) return bindings;

    bindings.reserve(layoutSlot.articulations.size());

    for (const auto& articulationSlot : layoutSlot.articulations) {
        if (!routingContext.allowedArticulationFilters.empty()) {
            bool matchesFilter = false;
            for (const auto& artFilter : routingContext.allowedArticulationFilters) {
                if (ArticulationNameHelpers::mapFilterMatchesArticulation(artFilter, articulationSlot.codeRaw)) {
                    matchesFilter = true;
                    break;
                }
            }
            if (!matchesFilter) continue;
        } else if (!routingContext.allowedArticulationNameIds.empty() && !vectorContains(routingContext.allowedArticulationNameIds, articulationSlot.nameId)) continue;

        ResolvedArticulationSlotBinding binding(articulationSlot);
        binding.libraryInstrument = routingContext.defaultInstrument;

        auto libraryInstrumentIterator = routingContext.articulationLibraryMap.find(articulationSlot.nameId);
        if (libraryInstrumentIterator != routingContext.articulationLibraryMap.end() && libraryInstrumentIterator->second != nullptr) {
            binding.libraryInstrument = libraryInstrumentIterator->second;
            binding.strictArticulationResolution = true;
        }

        if (binding.libraryInstrument == nullptr) continue;

        const auto exactRemapIt = routingContext.artExactRemap.find(makeArticulationSlotRouteKey(articulationSlot.nameId, articulationSlot.exprId));
        if (exactRemapIt != routingContext.artExactRemap.end()) {
            binding.resolvedArticulationSlot.nameId = exactRemapIt->second.nameId;
            binding.resolvedArticulationSlot.exprId = exactRemapIt->second.exprId;
            binding.strictArticulationResolution = true;
        } else {
            auto remapIt = routingContext.artNameRemap.find(articulationSlot.nameId);
            if (remapIt != routingContext.artNameRemap.end()) {
                binding.resolvedArticulationSlot.nameId = remapIt->second;
                binding.strictArticulationResolution = true;
            }
        }
        if (routingContext.strictArticulationResolution) binding.strictArticulationResolution = true;

        binding.resolvedSpec = this->resolveArticulationSpec(binding.libraryInstrument, binding.resolvedArticulationSlot, !binding.strictArticulationResolution);
        if (binding.resolvedSpec == nullptr) {
            LOG(Warning, binding.libraryInstrument->parent->meta.name + "/" + binding.libraryInstrument->meta.codeRaw
                + " has no suitable articulations for '" + layoutSlot.id + "' '" + articulationSlot.codeRaw + "'");
            bindings.push_back(std::move(binding));
            continue;
        }

        // Controller-driven siblings: explicit library articulation dimensions or kit layout/default expression points.
        for (const auto& artSpec : binding.libraryInstrument->articulationSpecs) {
            if (artSpec == nullptr) continue;
            if (artSpec->nameId == binding.resolvedArticulationSlot.nameId && artSpec->exprId == binding.resolvedArticulationSlot.exprId) continue;

            bool hasMatchingController = false;
            for (const auto& controller : layoutSlot.controllers) {
                if (!containsSameValue(controller.notes, articulationSlot.notes)) continue;
                if (controller.codeIdx == KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose && artSpec->nameId != binding.resolvedArticulationSlot.nameId) {
                    continue;
                }

                if (this->hasEffectiveExpressionValues(*artSpec, controller.codeIdx)) {
                    hasMatchingController = true;
                    break;
                }
            }

            if (hasMatchingController) binding.controllerSiblingSpecs.push_back(artSpec.get());
        }
        bindings.push_back(std::move(binding));
    }
    return bindings;
}

void InstrumentPlayer::appendArticulationPlaybackEntries(std::vector<ArticulationPlaybackEntry>& playbackEntries, const std::vector<ResolvedArticulationSlotBinding>& bindings) {
    for (const auto& binding : bindings) {
        if (binding.libraryInstrument == nullptr || binding.resolvedSpec == nullptr) continue;

        ArticulationPlaybackEntry playbackEntry(binding.articulationSlot);
        playbackEntry.libraryInstrument = binding.libraryInstrument;
        playbackEntry.libraryArticulation = binding.resolvedSpec;
        playbackEntry.usesExplicitMapLayer = binding.strictArticulationResolution;
        playbackEntries.push_back(std::move(playbackEntry));

        for (auto* siblingSpec : binding.controllerSiblingSpecs) {
            if (siblingSpec == nullptr) continue;

            ArticulationPlaybackEntry siblingPlaybackEntry(binding.articulationSlot);
            siblingPlaybackEntry.libraryInstrument = binding.libraryInstrument;
            siblingPlaybackEntry.libraryArticulation = siblingSpec;
            siblingPlaybackEntry.usesExplicitMapLayer = binding.strictArticulationResolution;
            siblingPlaybackEntry.isControllerSibling = true;
            playbackEntries.push_back(std::move(siblingPlaybackEntry));
        }
    }
}

void InstrumentPlayer::applyArticulationPlaybackEntry(const ArticulationPlaybackEntry& playbackEntry, bool loadSamples, bool createIfMissing, CcDimensionsMap& foundCcDimensions, std::set<juce::String>& skippedMicCodes) {
    const auto& articulationSlot = playbackEntry.articulationSlot;
    if (playbackEntry.isAliasEntry) {
        auto findAliasLibraryArticulation = [this, &playbackEntry](StringsDictSingleton::Id exprId) {
            ArtRtIdx libraryArticulationIdx = -1;
            for (const auto& libraryPlaybackEntry : this->articulationPlaybackEntries) {
                if (libraryPlaybackEntry.isAliasEntry || libraryPlaybackEntry.isControllerSibling || libraryPlaybackEntry.libraryArticulation == nullptr) continue;
                if (libraryPlaybackEntry.libraryArticulation->nameId != playbackEntry.aliasTarget.nameId || libraryPlaybackEntry.libraryArticulation->exprId != exprId) continue;
                if (this->tryGetRuntimeArticulationIdx(libraryPlaybackEntry, false, libraryArticulationIdx)) return libraryArticulationIdx;
            }
            return this->findArticulationIndex(playbackEntry.aliasTarget.nameId, exprId);
        };

        ArtRtIdx libraryArticulationIdx = -1;
        if (playbackEntry.aliasTarget.exprId != 0) libraryArticulationIdx = findAliasLibraryArticulation(playbackEntry.aliasTarget.exprId);
        if (libraryArticulationIdx < 0) libraryArticulationIdx = findAliasLibraryArticulation(articulationSlot.exprId);
        if (libraryArticulationIdx < 0 && articulationSlot.exprId != 0) libraryArticulationIdx = findAliasLibraryArticulation(0);
        if (libraryArticulationIdx < 0) return;

        for (const auto note : playbackEntry.midiNotes) {
            if (note < 128) this->noteToArticulation[static_cast<size_t>(note)] = libraryArticulationIdx;
        }
        return;
    }

    auto* libraryInstrument = playbackEntry.libraryInstrument;
    auto* artSpec = playbackEntry.libraryArticulation;
    if (artSpec == nullptr || libraryInstrument == nullptr) return;

    ArtRtIdx artIdx = -1;
    if (!this->tryGetRuntimeArticulationIdx(playbackEntry, createIfMissing, artIdx)) {
        LOG(Warning, "Skipping routing rebuild for '" + articulationSlot.codeRaw + "' because runtime articulation '" + artSpec->codeRaw + "' is not loaded");
        return;
    }
    auto& artRt = this->runtimeArticulations[static_cast<size_t>(artIdx)];
    if (loadSamples) {
        artSpec->sortVelocityLevels();
        this->processArticulationSpec(libraryInstrument, articulationSlot, artSpec, artRt, artIdx, foundCcDimensions, skippedMicCodes);
    } else {
        this->refreshRuntimeExpressionValues(*artSpec, artRt);
        this->buildCcMappings(articulationSlot, artSpec, artIdx, foundCcDimensions);
    }
    if (!playbackEntry.isControllerSibling) {
        for (uint8_t note : playbackEntry.midiNotes) this->noteToArticulation[note] = artIdx;
    }
}

void InstrumentPlayer::applyArticulationPlaybackEntries(bool loadSamples, bool createIfMissing) {
    CcDimensionsMap foundCcDimensions;
    std::set<juce::String> skippedMicCodes;

    for (const auto& playbackEntry : this->articulationPlaybackEntries) {
        if (!playbackEntry.isControllerSibling && !playbackEntry.isAliasEntry) {
            LOG(Info, "  Loading articulation: " + playbackEntry.articulationSlot.codeRaw + " (notes " + midiVectorToString(playbackEntry.midiNotes) + ")");
            if (playbackEntry.libraryArticulation != nullptr) {
                LOG(Info, "    resolved: layout='" + playbackEntry.articulationSlot.codeRaw + "' -> artSpec='" + playbackEntry.libraryArticulation->codeRaw
                    + "' (nameId=" + juce::String((int)playbackEntry.libraryArticulation->nameId) + " exprId=" + juce::String((int)playbackEntry.libraryArticulation->exprId) + ")");
            }
        }
        this->applyArticulationPlaybackEntry(playbackEntry, loadSamples, createIfMissing, foundCcDimensions, skippedMicCodes);
    }

    if (loadSamples) this->loadAftertouchChokeTargets(foundCcDimensions, skippedMicCodes);

    if (loadSamples && !skippedMicCodes.empty() && LOG_ENABLED(Notice)) {
        juce::String codes;
        for (const auto& c : skippedMicCodes) {
            if (codes.isNotEmpty()) codes += ", ";
            codes += "'" + c + "'";
        }
        LOG(Notice, "    Skipping samples for mic(s) " + codes + " - not present in active mic layout"); //FIXME: missing context, line alone on 'notice' level not informative!
    }
    this->processCcDimensions(foundCcDimensions);
}

void InstrumentPlayer::loadAftertouchChokeTargets(CcDimensionsMap& foundCcDimensions, std::set<juce::String>& skippedMicCodes) {
    const auto chokeNameId = StringsDictSingleton::getId("choke");
    std::set<KitLib::InstrumentSpec*> libraryInstruments;
    for (const auto& playbackEntry : this->articulationPlaybackEntries) {
        if (playbackEntry.isAliasEntry || playbackEntry.libraryInstrument == nullptr || playbackEntry.libraryArticulation == nullptr) continue;
        if (playbackEntry.libraryArticulation->nameId == chokeNameId) continue;
        libraryInstruments.insert(playbackEntry.libraryInstrument);
    }

    std::set<ArtRtIdx> usedTargetArticulations;
    for (auto* libraryInstrument : libraryInstruments) {
        KitLib::ArticulationSpec* chokeSpec = nullptr;
        for (const auto& artSpec : libraryInstrument->articulationSpecs) {
            if (artSpec != nullptr && artSpec->nameId == chokeNameId && artSpec->exprId == 0) {
                chokeSpec = artSpec.get();
                break;
            }
        }
        if (chokeSpec == nullptr) continue;

        ArtRtIdx chokeArtIdx = -1;
        for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx) {
            const auto candidateIdx = static_cast<ArtRtIdx>(artIdx);
            if (usedTargetArticulations.find(candidateIdx) != usedTargetArticulations.end()) continue;
            const auto& candidateArt = this->runtimeArticulations[artIdx];
            if (candidateArt.nameId != chokeSpec->nameId || candidateArt.exprId != chokeSpec->exprId) continue;

            bool candidateUsesLibraryInstrument = candidateArt.velocityLevels.empty();
            for (const auto& velocityLevel : candidateArt.velocityLevels) {
                for (const auto& sampleEntry : velocityLevel.samples) {
                    if (sampleEntry.sound != nullptr && sampleEntry.sound->instrumentSpec == libraryInstrument) candidateUsesLibraryInstrument = true;
                }
            }
            if (candidateUsesLibraryInstrument) {
                chokeArtIdx = candidateIdx;
                break;
            }
        }
        if (chokeArtIdx < 0) {
            this->runtimeArticulations.emplace_back(chokeSpec->nameId, chokeSpec->exprId);
            chokeArtIdx = static_cast<ArtRtIdx>(this->runtimeArticulations.size() - 1);
        }
        usedTargetArticulations.insert(chokeArtIdx);

        auto& chokeArt = this->runtimeArticulations[static_cast<size_t>(chokeArtIdx)];
        if (chokeArt.velocityLevels.empty()) {
            chokeSpec->sortVelocityLevels();
            ArticulationSlot hiddenChokeArticulation { chokeSpec->codeRaw };
            this->processArticulationSpec(libraryInstrument, hiddenChokeArticulation, chokeSpec, chokeArt, chokeArtIdx, foundCcDimensions, skippedMicCodes);
        }

        bool hasTargetSamples = false;
        for (auto& velocityLevel : chokeArt.velocityLevels) {
            for (auto& sampleEntry : velocityLevel.samples) {
                if (sampleEntry.sound == nullptr || sampleEntry.sound->instrumentSpec != libraryInstrument) continue;
                sampleEntry.sound->isAftertouchChokeTarget = true;
                hasTargetSamples = true;
            }
        }
        if (!hasTargetSamples) continue;

        for (const auto& sound : this->sampledSoundPtrs) {
            if (sound == nullptr || sound->instrumentSpec != libraryInstrument || sound->isAftertouchChokeTarget) continue;
            sound->aftertouchChokeArticulationIndex = chokeArtIdx;
        }
    }
}

void InstrumentPlayer::appendAliasPlaybackEntries(std::vector<ArticulationPlaybackEntry>& playbackEntries, const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext) {
    if (routingContext.artAliases.empty()) return;

    for (const auto& articulationSlot : layoutSlot.articulations) {
        const auto aliasIt = routingContext.artAliases.find(articulationSlot.nameId);
        if (aliasIt == routingContext.artAliases.end()) continue;

        bool hasNormalPlaybackEntry = false;
        for (const auto& playbackEntry : playbackEntries) {
            if (!playbackEntry.isAliasEntry && !playbackEntry.isControllerSibling && playbackEntry.articulationSlotNameId == articulationSlot.nameId && playbackEntry.articulationSlotExpressionId == articulationSlot.exprId) {
                hasNormalPlaybackEntry = true;
                break;
            }
        }
        if (hasNormalPlaybackEntry) continue;

        ArticulationPlaybackEntry aliasPlaybackEntry(articulationSlot);
        aliasPlaybackEntry.aliasTarget = aliasIt->second;
        aliasPlaybackEntry.isAliasEntry = true;
        playbackEntries.push_back(std::move(aliasPlaybackEntry));
    }
}

std::vector<InstrumentPlayer::ArticulationPlaybackEntry> InstrumentPlayer::buildArticulationPlaybackEntries(const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext) {
    const auto bindings = this->collectResolvedArticulationSlots(layoutSlot, routingContext);
    std::vector<ArticulationPlaybackEntry> playbackEntries;
    this->appendArticulationPlaybackEntries(playbackEntries, bindings);
    this->appendAliasPlaybackEntries(playbackEntries, layoutSlot, routingContext);
    return playbackEntries;
}

void InstrumentPlayer::applyResolvedRouting(const std::vector<ResolvedArticulationSlotBinding>& bindings, const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext, bool loadSamples, bool createIfMissing) {
    this->articulationPlaybackEntries.clear();
    this->appendArticulationPlaybackEntries(this->articulationPlaybackEntries, bindings);
    this->appendAliasPlaybackEntries(this->articulationPlaybackEntries, layoutSlot, routingContext);
    this->applyArticulationPlaybackEntries(loadSamples, createIfMissing);
}

void InstrumentPlayer::processArticulationSlots(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationLibraryMap& articulationLibraryMap, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, bool strictArticulationResolution) {
    if (defaultInstSpec == nullptr) return;

    this->storeRoutingContext(defaultInstSpec, articulationLibraryMap, allowedArticulationNameIds, allowedArticulationFilters, artNameRemap, artExactRemap, artAliases, strictArticulationResolution, this->lastRoutingContext.micRouteOverrides);

    LOG(Debug, "processArticulationSlots: available articulationSpecs for inst=" + defaultInstSpec->meta.codeRaw + ":");
    for (const auto& artSpec : defaultInstSpec->articulationSpecs) {
        this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId); // discoverArticulations

        if (LOG_ENABLED(Debug)) {
            juce::String dims;
            for (const auto& dim : artSpec->expressionDimensions)
                dims += "[" + dim->typeToString(dim->type) + ", " + midiVectorToString(dim->values) + "]";
            LOG(Debug, "  nameId=" + juce::String((int)artSpec->nameId) + " exprId=" + juce::String((int)artSpec->exprId) + " exprDims=" + dims + " artSpec=" + artSpec->codeRaw);
        }
    }
    const auto bindings = this->collectResolvedArticulationSlots(this->instrumentSlot, this->lastRoutingContext);
    this->applyResolvedRouting(bindings, this->instrumentSlot, this->lastRoutingContext, true, true);
    this->rebuildArtLookupCache();
}


void InstrumentPlayer::processArticulationSpec(KitLib::InstrumentSpec* instSpec
  , const ArticulationSlot& articulationSlot
  , KitLib::ArticulationSpec* artSpec
  , ArticulationRuntime& artRt
  , ArtRtIdx artIdx
  , CcDimensionsMap& foundCcDimensions
  , std::set<juce::String>& skippedMicCodes
) {
    MicRoutingBuilder micRouting(this);

    this->createVelocityLevels(instSpec, articulationSlot.notes, artSpec, artRt, artIdx, micRouting);
    micRouting.mergeSkippedInto(skippedMicCodes);

    this->refreshRuntimeExpressionValues(*artSpec, artRt);
    this->buildCcMappings(articulationSlot, artSpec, artIdx, foundCcDimensions);
}

void InstrumentPlayer::createVelocityLevels(KitLib::InstrumentSpec* instSpec
    , const KitLayout::MidiNotes& notes
    , KitLib::ArticulationSpec* artSpec
    , ArticulationRuntime& artRt
    , ArtRtIdx artIdx
    , MicRoutingBuilder& micRouting
) {
    bool articulationCalibrationAvailable = !artSpec->velocityLevels.empty();
    for (const auto& velocitySpec : artSpec->velocityLevels) {
        if (!velocitySpec->fittedSourceDb.has_value() || !velocitySpec->calibrationTrimDb.has_value()) {
            articulationCalibrationAvailable = false;
            break;
        }
    }

    for (const auto& velocityLevelSpec : artSpec->velocityLevels) {
        VelocityLevelRuntime velocityLevel;
        velocityLevel.velocity = velocityLevelSpec->velocity;
        velocityLevel.selectionUpperVelocity = velocityLevelSpec->selectionUpperVelocity;
        velocityLevel.velocityLevelSpec = velocityLevelSpec.get();
        velocityLevel.fittedSourceDb = velocityLevelSpec->fittedSourceDb;
        velocityLevel.calibrationTrimDb = velocityLevelSpec->calibrationTrimDb;
        size_t sampleCount = 0;
        for (const auto& logicalHitSpec : velocityLevelSpec->logicalHits) sampleCount += logicalHitSpec->samples.size();
        velocityLevel.samples.reserve(sampleCount);
        velocityLevel.logicalHits.reserve(velocityLevelSpec->logicalHits.size());

        for (const auto& logicalHitSpec : velocityLevelSpec->logicalHits) {
            LogicalHitRuntime logicalHit;
            logicalHit.roundRobinId = logicalHitSpec->roundRobinId;
            logicalHit.preTrimIntensityDb = logicalHitSpec->intensityDb;

            for (const auto& sampleSpec : logicalHitSpec->samples) {
                const int fileChannelCount = sampleSpec->fileChannelCount > 0 ? sampleSpec->fileChannelCount : sampleSpec->micSpec.channels;
                const auto assignment = micRouting.resolve(sampleSpec->micSpec, fileChannelCount);
                if (assignment.globalStart < 0) continue;

                auto sound = std::make_shared<SampledSound>(instSpec, sampleSpec.get(), sampleSpec->sampleFile, notes);
                const std::optional<int> effectiveBalance = this->instrumentSlot.balance.has_value()
                    ? this->instrumentSlot.balance
                    : (artSpec->balanceConflict && !artSpec->balanceOverride.has_value() ? std::optional<int> {} : artSpec->getEffectiveBalance());
                sound->decayEnvelope = sampleSpec->decayEnvelope;
                if (articulationCalibrationAvailable && sound->decayEnvelope.isValid()) sound->decayEnvelope.peakDb += velocityLevel.calibrationTrimDb.value_or(0.0f);
                if ((sampleSpec->hasCsvMetadata || !sampleSpec->decayCode.isEmpty()) && !sound->decayEnvelope.isValid()) {
                    LOG(Warning, "Invalid decay metadata for sample: " + sampleSpec->sampleFile.getFileName() + ". Pedal articulation replacement will be skipped for this sample.");
                }
                sound->micBufferChannelStart = assignment.globalStart;
                sound->micBufferChannelCount = assignment.channels;
                sound->routedMicCode = assignment.micCode;
                sound->routedMicCodeId = StringsDictSingleton::getId(assignment.micCode);
                sound->cachedMicBleedGain.store(this->getMicBleedLevel(sampleSpec->micSpec.raw), std::memory_order_relaxed);
                sound->playbackOffsetMs = sampleSpec->playbackOffsetMs;
                sound->canonicalFixedGainDb = sampleSpec->manualGainDb + (articulationCalibrationAvailable ? velocityLevel.calibrationTrimDb.value_or(0.0f) : 0.0f);
                sound->requestedFixedGainDb = sound->canonicalFixedGainDb;
                sound->runtimeArticulationIndex = artIdx;
                InstrumentPlayer::configureSoundMicPlacement(*sound, sampleSpec->micSpec, fileChannelCount, assignment.channels, assignment.applyBalance, effectiveBalance, artRt.parameters.pan);

                this->addSound(sound);
                SampleEntry sampleEntry;
                sampleEntry.sound = sound.get();
                sampleEntry.roundRobinId = logicalHitSpec->roundRobinId;
                sampleEntry.weakSound = sound;
                logicalHit.sampleEntryIndexes.push_back(velocityLevel.samples.size());
                velocityLevel.samples.push_back(std::move(sampleEntry));
            }

            if (logicalHit.sampleEntryIndexes.empty()) continue;
            logicalHit.readiness->requiredEntryCount = static_cast<int>(logicalHit.sampleEntryIndexes.size());
            velocityLevel.logicalHits.push_back(std::move(logicalHit));
        }
        artRt.velocityLevels.push_back(std::move(velocityLevel));
    }

    artRt.buildVelocityMapping(this->parent->getVelocityResponse());

    static constexpr std::array<float, 5> CALIBRATED_EARLY_TARGETS_DB { -24.0f, -12.0f, -6.0f, -3.0f, 0.0f };
    static constexpr std::array<int, 5> UNCALIBRATED_EARLY_VELOCITIES { 10, 35, 65, 95, 120 };
    for (size_t targetIdx = 0; targetIdx < CALIBRATED_EARLY_TARGETS_DB.size(); ++targetIdx) {
        size_t bestVelocityLevelIdx = 0;
        size_t bestLogicalHitIdx = 0;
        bool foundLogicalHit = false;
        for (int selectionPass = 0; selectionPass < 2 && !foundLogicalHit; ++selectionPass) {
            float bestDistance = std::numeric_limits<float>::max();
            for (size_t velocityLevelIdx = 0; velocityLevelIdx < artRt.velocityLevels.size(); ++velocityLevelIdx) {
                auto& velocityLevel = artRt.velocityLevels[velocityLevelIdx];
                for (size_t logicalHitOffset = 0; logicalHitOffset < velocityLevel.logicalHits.size(); ++logicalHitOffset) {
                    const size_t logicalHitIdx = (targetIdx + logicalHitOffset) % velocityLevel.logicalHits.size();
                    auto& logicalHit = velocityLevel.logicalHits[logicalHitIdx];
                    if (selectionPass == 0 && logicalHit.earlyLoad) continue;
                    const float representativeValue = artRt.calibrated
                        ? logicalHit.preTrimIntensityDb.value_or(*velocityLevel.fittedSourceDb)
                        : static_cast<float>(velocityLevel.velocity);
                    const float targetValue = artRt.calibrated
                        ? *artRt.velocityLevels.back().fittedSourceDb + CALIBRATED_EARLY_TARGETS_DB[targetIdx]
                        : static_cast<float>(UNCALIBRATED_EARLY_VELOCITIES[targetIdx]);
                    const float distance = std::abs(representativeValue - targetValue);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestVelocityLevelIdx = velocityLevelIdx;
                        bestLogicalHitIdx = logicalHitIdx;
                        foundLogicalHit = true;
                    }
                }
            }
        }
        if (foundLogicalHit) artRt.velocityLevels[bestVelocityLevelIdx].logicalHits[bestLogicalHitIdx].earlyLoad = true;
    }

    SampleLoader* loader = this->getSampleLoader();
    if (loader == nullptr) return;
    for (int loadPass = 0; loadPass < 2; ++loadPass) {
        const bool loadEarlyHits = loadPass == 0;
        for (auto& velocityLevel : artRt.velocityLevels) {
            const float calibrationTrimDb = artRt.calibrated ? velocityLevel.calibrationTrimDb.value_or(0.0f) : 0.0f;
            for (auto& logicalHit : velocityLevel.logicalHits) {
                if (logicalHit.earlyLoad != loadEarlyHits) continue;
                for (const auto sampleEntryIdx : logicalHit.sampleEntryIndexes) {
                    auto* sound = velocityLevel.samples[sampleEntryIdx].sound;
                    if (sound == nullptr || sound->sampleSpec == nullptr) continue;
                    auto* sampleSpec = sound->sampleSpec;
                    const auto weakSound = velocityLevel.samples[sampleEntryIdx].weakSound;
                    const auto recipeRevision = sound->sourceRecipeRevision.load(std::memory_order_acquire);

                    SampleLoadSpec loadSpec;
                    loadSpec.file = sampleSpec->sampleFile;
                    loadSpec.contentHash = sampleSpec->metadataContentHash;
                    loadSpec.metadataFileSizeBytes = sampleSpec->metadataFileSizeBytes;
                    loadSpec.fixedGainDb = sampleSpec->manualGainDb + calibrationTrimDb;
                    const auto readiness = logicalHit.readiness;

                    loader->getQueue().enqueueCandidate(
                        loadSpec,
                        instSpec->meta.type,
                        artSpec->nameId,
                        velocityLevel.velocity,
                        logicalHit.roundRobinId,
                        logicalHit.earlyLoad,
                        [this, weakSound, readiness, recipeRevision, fixedGainDb = loadSpec.fixedGainDb](std::shared_ptr<juce::AudioBuffer<float>> buffer, double loadedSampleRate) {
                            auto soundLocked = weakSound.lock();
                            if (soundLocked == nullptr || this->parent == nullptr) return;
                            const bool published = this->parent->publishSourceRecipeReload(soundLocked, recipeRevision, fixedGainDb, std::move(buffer), loadedSampleRate);
                            if (soundLocked->sourceRecipeRevision.load(std::memory_order_acquire) == recipeRevision && !soundLocked->readinessReported.exchange(true, std::memory_order_acq_rel)) readiness->markLoaded(published);
                        }
                    );
                }
            }
        }
    }
}


void InstrumentPlayer::buildCcMappings(const ArticulationSlot& articulationSlot, KitLib::ArticulationSpec* artSpec, ArtRtIdx artIdx, CcDimensionsMap& foundCcDimensions) {

    { // Debug: report articulation and its expression dimensions
        if (LOG_ENABLED(Debug)) {
            auto code = artSpec->codeRaw;
            LOG(Debug, "buildCcMappings: processing artSpec=" + code + " nameId=" + juce::String((int)artSpec->nameId) + " exprId=" + juce::String((int)artSpec->exprId) + " articulationSlot=" + articulationSlot.codeRaw);
            if (artSpec->expressionDimensions.empty()) {
                LOG(Debug, "buildCcMappings: artSpec has no expressionDimensions");
            } else {
                juce::String dims;
                for (const auto& dim : artSpec->expressionDimensions)
                    dims += "[" + dim->typeToString(dim->type) + ", " + midiVectorToString(dim->values) + "]";
                LOG(Debug, "buildCcMappings: expressionDimensions=" + dims);
            }
        }
    }

    for (const auto& cc : this->instrumentSlot.controllers) {
        for (uint8_t note : cc.notes) {
            const auto ccNum = static_cast<uint8_t>(cc.number);
            auto& vec = this->noteToControllers[note];
            if (!vectorContains(vec, ccNum))
                vec.emplace_back(ccNum);
        }

        // only controllers with notes that are linked to this articulation
        if (containsSameValue(cc.notes, articulationSlot.notes)) {
            LOG(Debug, "buildCcMappings: controller " + juce::String((int)cc.number) + " {num=" + juce::String((int)cc.number) + ",codeIdx=" + juce::String((int)cc.codeIdx) + ", notes=" + midiVectorToString(cc.notes) + "} applies to articulation " + articulationSlot.codeRaw + " (articulationSlot.notes=" + midiVectorToString(articulationSlot.notes) + ")");
            const auto values = this->getEffectiveExpressionValues(*artSpec, cc.codeIdx);
            for (const auto& val : values) {
                const auto ccNum = static_cast<int8_t>(cc.number);
                const auto pair = std::make_pair(static_cast<int8_t>(val), artIdx);
                auto& vec = foundCcDimensions[ccNum];
                // avoid inserting identical (value,artIdx) duplicates
                if (!vectorContains(vec, pair)) {
                    vec.emplace_back(pair);
                    LOG(Debug, "buildCcMappings: cc=" + juce::String((int)ccNum) + " val=" + juce::String((int)val) + " (artIdx=" + juce::String(artIdx) + ") articulationSlot=" + articulationSlot.codeRaw);
                // } else {
                //     LOG(Warning, "buildCcMappings: skipping duplicate mapping cc=" + juce::String((int)ccNum) + " val=" + juce::String((int)val) + " (artIdx=" + juce::String(artIdx) + ") articulationSlot=" + articulationSlot.codeRaw);
                // }
                }
            }
        }
    }
}

void InstrumentPlayer::processCcDimensions(CcDimensionsMap& foundCcDimensions) {
    for (auto&[ccNum, valArtPairs] : foundCcDimensions) {
        LOG(Debug, "processCcDimensions: CC " + juce::String((int)ccNum) + " has " + juce::String((int)valArtPairs.size()) + " mappings");
        std::sort(valArtPairs.begin(), valArtPairs.end(), [](auto& a, auto& b){
            return a.first < b.first;
        });

        std::array<uint8_t, 128> map;
        const auto count = valArtPairs.size();

        for (size_t valueIdx = 0; valueIdx <= static_cast<size_t>(MIDI_MAX_VALUE); ++valueIdx) {
            int8_t chosen = valArtPairs[count - 1].second; // default to last
            for (size_t pairIdx = 0; pairIdx < count; ++pairIdx) {
                if (static_cast<int>(valueIdx) <= static_cast<int>(valArtPairs[pairIdx].first)) {
                    chosen = valArtPairs[pairIdx].second;
                    break;
                }
            }
            map[valueIdx] = static_cast<uint8_t>(chosen);
        }
        this->ccToArticulationMask[static_cast<size_t>(ccNum)] = true;
        this->ccToArticulationTable[static_cast<size_t>(ccNum)] = map;
        
        if (LOG_ENABLED(Debug)) { // log condensed mapping: thresholds -> articulation index
            juce::String summary;
            for (size_t i = 0; i < valArtPairs.size(); ++i) {
                if (i) summary += ",";
                summary += "(" + juce::String((int)valArtPairs[i].first) + ":" + this->getArtName(valArtPairs[i].second) + " (artIdx=" + juce::String(valArtPairs[i].second) + "))";
            }
            LOG(Debug, "processCcDimensions: CC " + juce::String((int)ccNum) + " thresholds: " + summary);
        }
    }
}

void InstrumentPlayer::rebuildRoutingFromLayout(const KitLayout::InstrumentSlot& layoutSlot) {
    this->instrumentSlot = layoutSlot;
    this->resetRoutingTables();

    LOG(Info, "Rebuilding routing from layout for slot '" + layoutSlot.id + "' with " + juce::String((int)layoutSlot.articulations.size()) + " articulations");

    RoutingContext routingContext = this->lastRoutingContext;
    if (!routingContext.isValid())
        routingContext.defaultInstrument = this->instrumentSpecstPtr;

    const auto bindings = this->collectResolvedArticulationSlots(layoutSlot, routingContext);
    this->applyResolvedRouting(bindings, layoutSlot, routingContext, false, false);
}

bool InstrumentPlayer::updateMicRoute(const juce::String& micCode, const juce::String& outputId) {
    const auto normalizedMicCode = micCode.trim();
    if (normalizedMicCode.isEmpty()) return false;

    const MicLayout::Mic* matchingLayoutMic = nullptr;
    int bestMicPriority = -1;
    if (const auto* micLayout = this->getMicLayout()) {
        for (const auto& mic : micLayout->mics) {
            if (mic.code.compareIgnoreCase(normalizedMicCode) != 0) continue;
            const auto selector = parseMicInstrumentSelector(mic.instrument, this->getEngine()->getInstrumentCatalog());
            const int priority = static_cast<int>(matchMicInstrumentSelector(selector, this->getId(), this->getType(), this->getEngine()->getInstrumentCatalog()));
            if (priority > bestMicPriority) {
                matchingLayoutMic = &mic;
                bestMicPriority = priority;
            }
        }
    }

    std::vector<SampledSound*> matchingSounds;
    std::vector<juce::String> matchingMicCodes;
    for (const auto& sound : this->sampledSoundPtrs) {
        if (sound == nullptr || sound->sampleSpec == nullptr) continue;
        const auto& micSpec = sound->sampleSpec->micSpec;
        const bool codeMatches = micSpec.raw.compareIgnoreCase(normalizedMicCode) == 0;
        const bool specsMatch = matchingLayoutMic != nullptr && micSpec.type == matchingLayoutMic->specs.type && micSpec.position == matchingLayoutMic->specs.position && micSpec.format == matchingLayoutMic->specs.format;
        if (!codeMatches && !specsMatch) continue;
        matchingSounds.push_back(sound.get());
        bool codeAlreadyAdded = false;
        for (const auto& matchingCode : matchingMicCodes) {
            if (matchingCode.compareIgnoreCase(micSpec.raw) == 0) {
                codeAlreadyAdded = true;
                break;
            }
        }
        if (!codeAlreadyAdded) matchingMicCodes.push_back(micSpec.raw);
    }
    if (matchingSounds.empty()) return false;

    const auto oldOverrides = this->lastRoutingContext.micRouteOverrides;
    auto newOverrides = oldOverrides;
    std::erase_if(newOverrides, [&normalizedMicCode, &matchingMicCodes](const auto& route) {
        if (route.first.compareIgnoreCase(normalizedMicCode) == 0) return true;
        for (const auto& matchingCode : matchingMicCodes)
            if (route.first.compareIgnoreCase(matchingCode) == 0) return true;
        return false;
    });
    const auto normalizedOutputId = outputId.trim();
    if (normalizedOutputId.isNotEmpty()) {
        for (const auto& matchingCode : matchingMicCodes)
            newOverrides[matchingCode] = normalizedOutputId;
    }
    this->lastRoutingContext.setMicRouteOverrides(newOverrides);

    if (normalizedOutputId == "-") {
        for (auto* sound : matchingSounds) {
            sound->micBufferChannelStart = -1;
            sound->micBufferChannelCount = sound->sampleSpec->fileChannelCount > 0 ? sound->sampleSpec->fileChannelCount : sound->sampleSpec->micSpec.channels;
            sound->routedMicCode = sound->sampleSpec->micSpec.raw;
            sound->routedMicCodeId = StringsDictSingleton::getId(sound->routedMicCode);
        }
        return true;
    }

    MicRoutingBuilder micRouting(this);
    const auto* firstMatchingSound = matchingSounds.front();
    const int fileChannelCount = firstMatchingSound->sampleSpec->fileChannelCount > 0 ? firstMatchingSound->sampleSpec->fileChannelCount : firstMatchingSound->sampleSpec->micSpec.channels;
    const auto assignment = micRouting.resolve(firstMatchingSound->sampleSpec->micSpec, fileChannelCount);
    if (assignment.globalStart < 0) {
        this->lastRoutingContext.setMicRouteOverrides(oldOverrides);
        return false;
    }

    for (auto* sound : matchingSounds) {
        sound->micBufferChannelStart = assignment.globalStart;
        sound->micBufferChannelCount = assignment.channels;
        sound->routedMicCode = assignment.micCode;
        sound->routedMicCodeId = StringsDictSingleton::getId(assignment.micCode);
        const int sourceChannelCount = sound->sampleSpec->fileChannelCount > 0 ? sound->sampleSpec->fileChannelCount : sound->sampleSpec->micSpec.channels;
        const auto* layerParameters = this->getLayerParameters(static_cast<ArtRtIdx>(sound->runtimeArticulationIndex));
        const int layerPan = layerParameters != nullptr ? layerParameters->pan : 0;
        InstrumentPlayer::configureSoundMicPlacement(*sound, sound->sampleSpec->micSpec, sourceChannelCount, assignment.channels, assignment.applyBalance, sound->placementBalance, layerPan);
    }
    return true;
}
