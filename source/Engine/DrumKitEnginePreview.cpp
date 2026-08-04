#include "DrumKitEngine.h"
#include "ArticulationNames.h"
#include "InstrumentPlayer.h"
#include "SampleSelector.h"
#include "../Config/Helpers.h"
#include "../Config/PluginLogger.h"
#include "../Config/YamlHelper.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

static constexpr int previewVelocityBandSize = 8;

static int getPreviewMicRank(const KitLib::SampleSpec& sample) {
    if (!sample.isValid() || !sample.micSpec.isValid())
        return std::numeric_limits<int>::max();

    const auto& mic = sample.micSpec;
    const bool stereoCapable = mic.channels >= 2 || mic.format == KitLib::MicSpec::Format::Stereo || mic.format == KitLib::MicSpec::Format::MidSide;

    if (mic.type == KitLib::MicSpec::Type::Overhead) {
        if (mic.raw.equalsIgnoreCase("ons")) return 0;

        int rank = stereoCapable ? 10 : 30;
        if (mic.position == KitLib::MicSpec::Position::Normal) rank -= 2;
        else if (mic.position == KitLib::MicSpec::Position::Wide) rank += 2;
        return rank;
    }
    if (mic.type == KitLib::MicSpec::Type::Direct) {
        int rank = stereoCapable ? 90 : 100;
        if (mic.position == KitLib::MicSpec::Position::Top || mic.position == KitLib::MicSpec::Position::In) rank -= 4;
        else if (mic.position == KitLib::MicSpec::Position::Out) rank -= 2;
        else if (mic.position == KitLib::MicSpec::Position::Bottom) rank += 2;
        return rank;
    }
    if (mic.type == KitLib::MicSpec::Type::Room) {
        int rank = stereoCapable ? 200 : 220;
        if (mic.position == KitLib::MicSpec::Position::Close) rank -= 2;
        else if (mic.position == KitLib::MicSpec::Position::Far) rank += 2;
        return rank;
    }
    return 300;
}

static int getPreviewVelocityBucket(juce::uint8 velocity) {
    return juce::jlimit(0, 15, (static_cast<int>(velocity) - 1) / previewVelocityBandSize);
}

static juce::uint8 getPreviewVelocityForBucket(juce::uint8 velocity) { //FIXME: wtf is this naming? getPreviewVelocityForBucket, getPreviewVelocityBucket, ...
    const int bucket = getPreviewVelocityBucket(velocity);
    const int bucketVelocity = bucket * previewVelocityBandSize + previewVelocityBandSize / 2;
    return static_cast<juce::uint8>(juce::jlimit(1, 127, bucketVelocity));
}

static bool isBetterPreviewSample(const KitLib::SampleSpec& candidate, const KitLib::SampleSpec& current) {
    const int candidateRank = getPreviewMicRank(candidate);
    const int currentRank = getPreviewMicRank(current);
    if (candidateRank != currentRank)
        return candidateRank < currentRank;

    const int rawCompare = candidate.micSpec.raw.compareIgnoreCase(current.micSpec.raw);
    if (rawCompare != 0)
        return rawCompare < 0;

    if (candidate.fileChannelStart != current.fileChannelStart)
        return candidate.fileChannelStart < current.fileChannelStart;

    return candidate.sampleFileName.compareIgnoreCase(current.sampleFileName) < 0;
}

void DrumKitEngine::playPreviewNote(const juce::String& slotId, const juce::String& artName, juce::uint8 velocity) {
    auto* layout = this->activeKitLayoutPtr.get();
    if (layout == nullptr)
        return;

    // Find the MIDI note for this slot/articulation
    int midiNote = -1;
    for (const auto& slot : layout->instrumentSlots) {
        if (slot.id != slotId)
            continue;
        for (const auto& art : slot.articulations) {
            if (ArticulationNames::mapFilterMatchesArticulation(artName, art.codeRaw) || artName == "(default)") {
                if (!art.notes.empty()) {
                    midiNote = static_cast<int>(art.notes[0]);
                    break;
                }
            }
        }
        // Fallback: use first articulation's first note
        if (midiNote < 0 && !slot.articulations.empty() && !slot.articulations[0].notes.empty())
            midiNote = static_cast<int>(slot.articulations[0].notes[0]);
        break;
    }

    if (midiNote < 0)
        return;

    // Trigger all instrument players routed to this note (primary + shadow layers).
    auto noteIt = this->instrumentPlayersByNote.find(midiNote);
    if (noteIt != this->instrumentPlayersByNote.end()) {
        for (auto* instrumentPlayer : noteIt->second)
            instrumentPlayer->playNote(1, midiNote, velocity);
    }
}

void DrumKitEngine::playLayerPreview(const juce::String& slotId, const juce::String& artName, int layerIndex, juce::uint8 velocity) {
    if (layerIndex < 0) return;
    auto* layout = this->activeKitLayoutPtr.get();
    if (layout == nullptr) return;

    int midiNote = -1;
    for (const auto& slot : layout->instrumentSlots) {
        if (slot.id != slotId) continue;
        for (const auto& articulation : slot.articulations) {
            if (!ArticulationNames::mapFilterMatchesArticulation(artName, articulation.codeRaw) && artName != "(default)") continue;
            if (!articulation.notes.empty()) {
                midiNote = static_cast<int>(articulation.notes[0]);
                break;
            }
        }
        if (midiNote < 0 && !slot.articulations.empty() && !slot.articulations[0].notes.empty()) midiNote = static_cast<int>(slot.articulations[0].notes[0]);
        break;
    }
    if (midiNote < 0) return;

    const auto targetRoutes = this->getTargetRoutesForArticulationFilter(slotId, artName);
    int currentLayerIndex = 0;
    for (auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
        if (instrumentPlayerPtr == nullptr || instrumentPlayerPtr->getId() != slotId || !DrumKitEngine::playerMatchesTargetRoutes(*instrumentPlayerPtr, targetRoutes)) continue;
        if (currentLayerIndex == layerIndex) {
            instrumentPlayerPtr->playNote(1, midiNote, velocity);
            return;
        }
        ++currentLayerIndex;
    }
}

void DrumKitEngine::playPreviewArticulation(const juce::String& slotId, const juce::String& articulationCode, juce::uint8 velocity) {
    auto* layout = this->activeKitLayoutPtr.get();
    if (layout == nullptr) return;

    const KitLayout::InstrumentSlot::ArticulationSlot previewArticulation(articulationCode);
    const auto openExpressionId = StringsDictSingleton::getId("open");
    int midiNote = -1;
    for (const auto& slot : layout->instrumentSlots) {
        if (slot.id != slotId) continue;
        for (const auto& articulation : slot.articulations) {
            if (articulation.nameId != previewArticulation.nameId || articulation.notes.empty()) continue;
            if (midiNote < 0) midiNote = static_cast<int>(articulation.notes[0]);
            if (articulation.exprId == openExpressionId) {
                midiNote = static_cast<int>(articulation.notes[0]);
                break;
            }
        }
        break;
    }
    if (midiNote < 0) return;

    auto playersIterator = this->instrumentPlayersByNote.find(midiNote);
    if (playersIterator == this->instrumentPlayersByNote.end()) return;
    for (auto* instrumentPlayer : playersIterator->second)
        if (instrumentPlayer != nullptr && instrumentPlayer->getId() == slotId) instrumentPlayer->playPreviewArticulation(1, midiNote, articulationCode, velocity);
}

bool DrumKitEngine::playLibraryLogicalHitPreview(const juce::String& slotId, const juce::String& articulationSlot, const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, int namedVelocity, const juce::String& roundRobin, juce::uint8 playbackVelocity) {
    if (this->libraryManagerPtr == nullptr || this->activeKitLayoutPtr == nullptr) return false;
    const juce::ScopedLock libraryLock(this->libraryModelLock);
    const juce::ScopedLock graphLock(this->engineGraphLock);
    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* libraryArticulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (libraryArticulation == nullptr) return false;

    int midiNote = -1;
    for (const auto& slot : this->activeKitLayoutPtr->instrumentSlots) {
        if (slot.id != slotId) continue;
        for (const auto& articulation : slot.articulations) {
            if (!ArticulationNames::mapFilterMatchesArticulation(articulationSlot, articulation.codeRaw) || articulation.notes.empty()) continue;
            midiNote = static_cast<int>(articulation.notes.front());
            break;
        }
        if (midiNote < 0 && !slot.articulations.empty() && !slot.articulations.front().notes.empty()) midiNote = static_cast<int>(slot.articulations.front().notes.front());
        break;
    }
    if (midiNote < 0) return false;

    const auto playersIterator = this->instrumentPlayersByNote.find(midiNote);
    if (playersIterator == this->instrumentPlayersByNote.end()) return false;
    const auto roundRobinId = KitLib::roundRobinIdFromText(roundRobin);
    for (auto* instrumentPlayer : playersIterator->second) {
        if (instrumentPlayer == nullptr || instrumentPlayer->getId() != slotId) continue;
        if (instrumentPlayer->playLibraryLogicalHitPreview(*libraryArticulation, namedVelocity, roundRobinId, 1, midiNote, playbackVelocity)) return true;
    }
    return false;
}

juce::ThreadPoolJob::JobStatus DrumKitEngine::PreviewResolveJob::runJob() {
    this->owner.resolveAndLoadPreviewTarget(std::move(this->cacheKey), this->kitCodeRaw, this->instCode, this->artCode, this->relativePath, this->micCode, this->resolveVelocity, this->playbackVelocity, this->generation);
    return jobHasFinished;
}

std::string DrumKitEngine::makePreviewTargetCacheKey(const juce::String& kitCodeRaw, const juce::String& instCode, const juce::String& artCode, juce::uint8 velocity, const juce::String& relativePath, const juce::String& micCode) const {
    const auto key = kitCodeRaw.trim().toLowerCase() 
        + "|" + instCode.trim().toLowerCase()
        + "|" + artCode.trim().toLowerCase()
        + "|" + juce::String(getPreviewVelocityBucket(velocity))
        + "|" + relativePath.trim().replaceCharacter('\\', '/').toLowerCase()
        + "|" + micCode.trim().toLowerCase();
    return key.toStdString();
}

std::optional<DrumKitEngine::PreviewTarget> DrumKitEngine::resolvePreviewTarget(const juce::String& kitCodeRaw, const juce::String& instCode, const juce::String& artCode, juce::uint8 velocity, const juce::String& relativePath, const juce::String& micCode) {
    const juce::ScopedLock libraryLock(this->libraryModelLock);
    auto* libMgr = this->libraryManagerPtr.get();
    if (libMgr == nullptr)
        return std::nullopt;

    auto* kit = libMgr->findKitById(kitCodeRaw);
    if (kit == nullptr)
        return std::nullopt;

    auto* inst = kit->findInstrumentById(instCode);
    if (inst == nullptr)
        return std::nullopt;

    if (inst->articulationSpecs.empty())
        inst->scanInstrumentSamples();

    KitLib::ArticulationSpec* artSpec = nullptr;
    if (artCode.isNotEmpty())
        artSpec = inst->findArticulationById(artCode);

    if (artSpec == nullptr && artCode.isNotEmpty()) {
        const auto requestedBase = artCode.upToFirstOccurrenceOf("[", false, false);
        if (requestedBase.isNotEmpty()) {
            for (const auto& candidate : inst->articulationSpecs) {
                if (candidate != nullptr && candidate->codeRaw.upToFirstOccurrenceOf("[", false, false) == requestedBase) {
                    artSpec = candidate.get();
                    break;
                }
            }
        }
    }

    if (artSpec == nullptr && !inst->articulationSpecs.empty())
        artSpec = inst->articulationSpecs[0].get();

    if (artSpec == nullptr || artSpec->velocityLevels.empty())
        return std::nullopt;

    bool calibrated = !artSpec->velocityLevels.empty();
    for (const auto& velocityLevel : artSpec->velocityLevels) {
        if (!velocityLevel->fittedSourceDb.has_value() || !velocityLevel->calibrationTrimDb.has_value()) {
            calibrated = false;
            break;
        }
    }

    KitLib::VelocityLevelSpec* bestVelocityLevel = nullptr;
    KitLib::SampleSpec* bestSample = nullptr;
    const auto normalizedRelativePath = relativePath.trim().replaceCharacter('\\', '/');
    const auto normalizedMicCode = micCode.trim();
    if (normalizedRelativePath.isNotEmpty()) {
        for (auto& velocityLevel : artSpec->velocityLevels) {
            if (velocityLevel == nullptr) continue;
            for (auto& logicalHit : velocityLevel->logicalHits) {
                if (logicalHit == nullptr) continue;
                for (auto& sample : logicalHit->samples) {
                    if (sample == nullptr || !sample->isValid()) continue;
                    const auto candidateRelativePath = sample->sampleFile.getRelativePathFrom(kit->meta.rootDir).replaceCharacter('\\', '/');
                    if (!candidateRelativePath.equalsIgnoreCase(normalizedRelativePath)) continue;
                    if (normalizedMicCode.isNotEmpty() && !sample->micSpec.raw.equalsIgnoreCase(normalizedMicCode)) continue;
                    bestVelocityLevel = velocityLevel.get();
                    bestSample = sample.get();
                    break;
                }
                if (bestSample != nullptr) break;
            }
            if (bestSample != nullptr) break;
        }
    } else {
        bestVelocityLevel = artSpec->velocityLevels.back().get();
        for (auto& velocityLevel : artSpec->velocityLevels) {
            if (velocity <= velocityLevel->selectionUpperVelocity) {
                bestVelocityLevel = velocityLevel.get();
                break;
            }
        }

        if (bestVelocityLevel == nullptr) return std::nullopt;
        for (auto& logicalHit : bestVelocityLevel->logicalHits) {
            if (logicalHit == nullptr) continue;
            for (auto& sample : logicalHit->samples) {
                if (sample == nullptr || !sample->isValid()) continue;
                if (bestSample == nullptr || isBetterPreviewSample(*sample, *bestSample)) bestSample = sample.get();
            }
        }
    }

    if (bestVelocityLevel == nullptr || bestSample == nullptr) return std::nullopt;

    PreviewTarget target;
    target.sampleFile = bestSample->sampleFile;
    target.contentHash = bestSample->metadataContentHash;
    target.metadataFileSizeBytes = bestSample->metadataFileSizeBytes;
    target.micCode = bestSample->micSpec.raw;
    target.fileChannelStart = juce::jmax(0, bestSample->fileChannelStart);
    target.fileChannelCount = bestSample->fileChannelCount > 0 ? bestSample->fileChannelCount : bestSample->micSpec.channels;
    target.fixedGainDb = bestSample->manualGainDb + (calibrated ? *bestVelocityLevel->calibrationTrimDb : 0.0f);
    target.playbackOffsetMs = bestSample->playbackOffsetMs;
    return target;
}

void DrumKitEngine::requestPreviewPlayback(const PreviewTarget& target, juce::uint8 velocity) {
    auto* loader = this->sampleLoaderPtr.get();

    if (loader == nullptr || !target.sampleFile.existsAsFile())
        return;

    const float gain = this->velocityResponse.getGain(velocity);

    SampleLoadSpec loadSpec;
    loadSpec.file = target.sampleFile;
    loadSpec.contentHash = target.contentHash;
    loadSpec.metadataFileSizeBytes = target.metadataFileSizeBytes;
    loadSpec.fixedGainDb = target.fixedGainDb;

    const int fileChannelStart = juce::jmax(0, target.fileChannelStart);
    const int fileChannelCount = target.fileChannelCount;
    const float playbackOffsetMs = target.playbackOffsetMs;

    loader->requestAsyncLoad(loadSpec, [this, gain, fileChannelStart, fileChannelCount, playbackOffsetMs](std::shared_ptr<juce::AudioBuffer<float>> buffer, double sampleRate) {
        if (buffer == nullptr || buffer->getNumChannels() <= 0 || buffer->getNumSamples() <= 0) return;

        const int sourceChannels = buffer->getNumChannels();
        const int sourceStart = juce::jlimit(0, sourceChannels - 1, fileChannelStart);
        const int availableChannels = sourceChannels - sourceStart;
        const int selectedChannels = juce::jlimit(1, availableChannels, fileChannelCount > 0 ? fileChannelCount : availableChannels);
        const int offsetSamples = juce::jmax(0, juce::roundToInt(std::abs(static_cast<double>(playbackOffsetMs)) * sampleRate / 1000.0));
        const int sourceReadStart = playbackOffsetMs < 0.0f ? juce::jmin(offsetSamples, buffer->getNumSamples()) : 0;
        const int destinationStart = playbackOffsetMs > 0.0f ? offsetSamples : 0;
        const int samplesToCopy = buffer->getNumSamples() - sourceReadStart;
        if (samplesToCopy <= 0) return;

        auto selectedBuffer = std::make_shared<juce::AudioBuffer<float>>(2, destinationStart + samplesToCopy);
        selectedBuffer->clear();
        selectedBuffer->copyFrom(0, destinationStart, *buffer, sourceStart, sourceReadStart, samplesToCopy);
        if (selectedChannels >= 2)
            selectedBuffer->copyFrom(1, destinationStart, *buffer, sourceStart + 1, sourceReadStart, samplesToCopy);
        else
            selectedBuffer->copyFrom(1, destinationStart, *buffer, sourceStart, sourceReadStart, samplesToCopy);

        const juce::SpinLock::ScopedLockType scopedLock(this->previewLock);
        this->previewBuffer = std::move(selectedBuffer);
        this->previewSampleRate = sampleRate;
        this->previewGain = gain;
        this->previewReadPos.store(0);
    });
}

void DrumKitEngine::resolveAndLoadPreviewTarget(std::string cacheKey, const juce::String& kitCodeRaw, const juce::String& instCode, const juce::String& artCode, const juce::String& relativePath, const juce::String& micCode, juce::uint8 resolveVelocity, juce::uint8 playbackVelocity, uint64_t generation) {
    auto target = this->resolvePreviewTarget(kitCodeRaw, instCode, artCode, resolveVelocity, relativePath, micCode);

    {
        const juce::ScopedLock resolutionLock(this->previewTargetResolutionLock);
        if (this->previewTargetGeneration.load(std::memory_order_acquire) != generation) return;
        this->pendingPreviewTargetKeys.erase(cacheKey);
        if (target.has_value())
            this->previewTargetCache[cacheKey] = *target;
    }

    if (target.has_value())
        this->requestPreviewPlayback(*target, playbackVelocity);
}


void DrumKitEngine::previewLibraryArticulation(const juce::String& kitCodeRaw, const juce::String& instCode, const juce::String& artCode, juce::uint8 velocity, const juce::String& relativePath, const juce::String& micCode) {
    const auto cacheKey = this->makePreviewTargetCacheKey(kitCodeRaw, instCode, artCode, velocity, relativePath, micCode);
    uint64_t generation = 0;
    std::optional<PreviewTarget> cachedTarget;

    {
        const juce::ScopedLock resolutionLock(this->previewTargetResolutionLock);
        generation = this->previewTargetGeneration.load(std::memory_order_acquire);
        auto it = this->previewTargetCache.find(cacheKey);
        if (it != this->previewTargetCache.end())
            cachedTarget = it->second;
        else if (!this->pendingPreviewTargetKeys.insert(cacheKey).second)
            return;
    }

    if (cachedTarget.has_value()) {
        this->requestPreviewPlayback(*cachedTarget, velocity);
        return;
    }

    const auto resolveVelocity = getPreviewVelocityForBucket(velocity);
    this->previewResolveThreadPool.addJob(new PreviewResolveJob(*this, cacheKey, kitCodeRaw, instCode, artCode, relativePath, micCode, resolveVelocity, velocity, generation), true);
}
