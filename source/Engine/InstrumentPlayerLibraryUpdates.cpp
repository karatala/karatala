#include "InstrumentPlayer.h"
#include "../Config/PluginLogger.h"
#include "ArticulationNames.h"
#include "DrumKitEngine.h"
#include "MicRoutingBuilder.h"
#include "SampleLoader.h"
#include <cmath>

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
