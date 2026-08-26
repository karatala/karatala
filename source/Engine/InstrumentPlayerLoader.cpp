#include "InstrumentPlayer.h"
#include "../Config/PluginLogger.h"
#include "ArticulationNames.h"
#include "DrumKitEngine.h"
#include "MicRoutingBuilder.h"
#include "SampleLoader.h"
#include <cmath>

static constexpr float FLOATING_MAX_EXTRA_DISTANCE_DB = 3.0f;

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
    if (this->velocityLevels.empty()) {
        this->buildFloatingCandidateWindows();
        return;
    }

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
    this->buildFloatingCandidateWindows();
}

void InstrumentPlayer::ArticulationRuntime::buildFloatingCandidateWindows() {
    for (auto& window : this->floatingCandidateWindows) window = {};
    this->floatingSelectionAvailable = this->calibrated && this->velocityLevels.size() >= 2;
    if (!this->floatingSelectionAvailable) return;

    for (const auto& velocityLevel : this->velocityLevels) {
        if (velocityLevel.logicalHits.empty() || velocityLevel.logicalHits.size() > 2) {
            this->floatingSelectionAvailable = false;
            return;
        }
        for (const auto& logicalHit : velocityLevel.logicalHits) {
            if (!logicalHit.preTrimIntensityDb.has_value() || !std::isfinite(*logicalHit.preTrimIntensityDb)) {
                this->floatingSelectionAvailable = false;
                return;
            }
        }
    }

    for (int velocity = 0; velocity <= MIDI_MAX_VALUE; ++velocity) {
        const VelocityLevelIdx targetVelocityLevelIdx = this->velocityToLevel[static_cast<size_t>(velocity)];
        if (targetVelocityLevelIdx < 0 || static_cast<size_t>(targetVelocityLevelIdx) >= this->velocityLevels.size()) continue;

        std::array<FloatingHitCandidate, maxFloatingHitCandidates> collectedCandidates {};
        size_t collectedCount = 0;
        const size_t firstVelocityLevelIdx = targetVelocityLevelIdx > 0 ? static_cast<size_t>(targetVelocityLevelIdx - 1) : 0;
        const size_t lastVelocityLevelIdx = juce::jmin(this->velocityLevels.size() - 1, static_cast<size_t>(targetVelocityLevelIdx + 1));
        const float targetIntensityDb = this->selectionTargetDb[static_cast<size_t>(velocity)];
        for (size_t velocityLevelIdx = firstVelocityLevelIdx; velocityLevelIdx <= lastVelocityLevelIdx; ++velocityLevelIdx) {
            const auto& velocityLevel = this->velocityLevels[velocityLevelIdx];
            for (size_t logicalHitIdx = 0; logicalHitIdx < velocityLevel.logicalHits.size(); ++logicalHitIdx) {
                if (collectedCount >= collectedCandidates.size()) break;
                const float intensityDistanceDb = std::abs(*velocityLevel.logicalHits[logicalHitIdx].preTrimIntensityDb - targetIntensityDb);
                collectedCandidates[collectedCount++] = { static_cast<VelocityLevelIdx>(velocityLevelIdx), static_cast<uint8_t>(logicalHitIdx), intensityDistanceDb };
            }
        }
        if (collectedCount == 0) continue;

        std::stable_sort(collectedCandidates.begin(), collectedCandidates.begin() + collectedCount, [](const FloatingHitCandidate& first, const FloatingHitCandidate& second) {
            return first.intensityDistanceDb < second.intensityDistanceDb;
        });

        auto& window = this->floatingCandidateWindows[static_cast<size_t>(velocity)];
        const float maximumDistanceDb = collectedCandidates[0].intensityDistanceDb + FLOATING_MAX_EXTRA_DISTANCE_DB;
        float minimumIntensityDb = std::numeric_limits<float>::max();
        float maximumIntensityDb = std::numeric_limits<float>::lowest();
        for (size_t candidateIdx = 0; candidateIdx < collectedCount; ++candidateIdx) {
            const auto& candidate = collectedCandidates[candidateIdx];
            if (candidate.intensityDistanceDb > maximumDistanceDb) continue;
            window.candidates[window.count++] = candidate;
            const float intensityDb = *this->velocityLevels[static_cast<size_t>(candidate.velocityLevelIdx)].logicalHits[candidate.logicalHitIdx].preTrimIntensityDb;
            minimumIntensityDb = juce::jmin(minimumIntensityDb, intensityDb);
            maximumIntensityDb = juce::jmax(maximumIntensityDb, intensityDb);
        }
        window.intensityRangeDb = juce::jmax(1.0f, maximumIntensityDb - minimumIntensityDb);
    }
}

void InstrumentPlayer::unloadInstrument() {
    this->loadProgress->reset();
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

    this->buildVelocityLevelLogicalHits(instSpec, notes, artSpec, artRt, artIdx, micRouting, articulationCalibrationAvailable);
    artRt.buildVelocityMapping(this->parent->getVelocityResponse());
    this->selectEarlyLoadLogicalHits(artRt);
    this->enqueueVelocityLevelSamples(*instSpec, *artSpec, artRt);
}

std::shared_ptr<SampledSound> InstrumentPlayer::createVelocityLevelSound(KitLib::InstrumentSpec* instSpec
    , const KitLayout::MidiNotes& notes
    , KitLib::ArticulationSpec* artSpec
    , KitLib::SampleSpec* sampleSpec
    , const VelocityLevelRuntime& velocityLevel
    , const ArticulationRuntime& artRt
    , ArtRtIdx artIdx
    , MicRoutingBuilder& micRouting
    , bool articulationCalibrationAvailable
) {
    const int fileChannelCount = sampleSpec->fileChannelCount > 0 ? sampleSpec->fileChannelCount : sampleSpec->micSpec.channels;
    const auto assignment = micRouting.resolve(sampleSpec->micSpec, fileChannelCount);
    if (assignment.globalStart < 0) return {};

    auto sound = std::make_shared<SampledSound>(instSpec, sampleSpec, sampleSpec->sampleFile, notes);
    const std::optional<int> effectiveBalance = this->instrumentSlot.balance.has_value()
        ? this->instrumentSlot.balance
        : (artSpec->balanceConflict && !artSpec->balanceOverride.has_value() ? std::optional<int> {} : artSpec->getEffectiveBalance());
    sound->decayEnvelope = sampleSpec->decayEnvelope;
    if (articulationCalibrationAvailable && sound->decayEnvelope.isValid()) sound->decayEnvelope.peakDb += velocityLevel.calibrationTrimDb.value_or(0.0f);
    if (sampleSpec->hasCsvMetadata && !sound->decayEnvelope.isValid()) {
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
    return sound;
}

void InstrumentPlayer::buildVelocityLevelLogicalHits(KitLib::InstrumentSpec* instSpec
    , const KitLayout::MidiNotes& notes
    , KitLib::ArticulationSpec* artSpec
    , ArticulationRuntime& artRt
    , ArtRtIdx artIdx
    , MicRoutingBuilder& micRouting
    , bool articulationCalibrationAvailable
) {
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
                auto sound = this->createVelocityLevelSound(instSpec, notes, artSpec, sampleSpec.get(), velocityLevel, artRt, artIdx, micRouting, articulationCalibrationAvailable);
                if (sound == nullptr) continue;
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
}

void InstrumentPlayer::selectEarlyLoadLogicalHits(ArticulationRuntime& artRt) {
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
}

void InstrumentPlayer::enqueueVelocityLevelSamples(const KitLib::InstrumentSpec& instSpec, const KitLib::ArticulationSpec& artSpec, ArticulationRuntime& artRt) {
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
                    const auto loadingProgress = this->loadProgress;
                    const uint64_t loadProgressRevision = loadingProgress->registerRequest();

                    loader->getQueue().enqueueCandidate(
                        loadSpec,
                        instSpec.meta.type,
                        artSpec.nameId,
                        velocityLevel.velocity,
                        logicalHit.roundRobinId,
                        logicalHit.earlyLoad,
                        [this, weakSound, readiness, recipeRevision, loadingProgress, loadProgressRevision, fixedGainDb = loadSpec.fixedGainDb](std::shared_ptr<juce::AudioBuffer<float>> buffer, double loadedSampleRate) {
                            loadingProgress->markFinished(loadProgressRevision);
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
