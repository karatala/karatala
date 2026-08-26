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

static juce::String relativeSamplePath(const KitLib::DrumKitSpec& kit, const juce::File& sampleFile) {
    return sampleFile.getRelativePathFrom(kit.meta.rootDir).replaceCharacter('\\', '/');
}

static int recipeStatusPriority(SampledSound::SourceRecipeStatus status) {
    switch (status) {
        case SampledSound::SourceRecipeStatus::Failed: return 3;
        case SampledSound::SourceRecipeStatus::Pending: return 2;
        case SampledSound::SourceRecipeStatus::Applied: return 1;
        case SampledSound::SourceRecipeStatus::NotLoaded: return 0;
    }
    return 0;
}

static std::vector<KitLib::SampleSpec*> findSamplesByRelativePath(KitLib::DrumKitSpec& kit, KitLib::ArticulationSpec& articulation, const juce::String& relativePath) {
    std::vector<KitLib::SampleSpec*> matches;
    const auto normalizedPath = relativePath.trim().replaceCharacter('\\', '/');
    for (const auto& velocityLevel : articulation.velocityLevels) {
        if (velocityLevel == nullptr) continue;
        for (const auto& logicalHit : velocityLevel->logicalHits) {
            if (logicalHit == nullptr) continue;
            for (const auto& sample : logicalHit->samples)
                if (sample != nullptr && relativeSamplePath(kit, sample->sampleFile) == normalizedPath) matches.push_back(sample.get());
        }
    }
    return matches;
}

static juce::String firstContentHash(const std::vector<KitLib::SampleSpec*>& samples) {
    for (const auto* sample : samples)
        if (sample != nullptr && sample->metadataContentHash.isNotEmpty()) return sample->metadataContentHash;
    return {};
}

DrumKitEngine::LibraryArticulationInfo DrumKitEngine::getLibraryArticulationInfo(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId) const {
    LibraryArticulationInfo result;
    if (this->libraryManagerPtr == nullptr) return result;
    const juce::ScopedLock libraryLock(this->libraryModelLock);
    const juce::ScopedLock graphLock(this->engineGraphLock);
    const auto* kit = this->libraryManagerPtr->findKitById(kitId);
    const auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    const auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result;

    result.libraryArticulationFound = true;
    result.writable = kit->meta.rootDir.hasWriteAccess();
    result.kitCodeRaw = kit->meta.codeRaw;
    result.instrumentCodeRaw = instrument->meta.codeRaw;
    result.articulationCodeRaw = articulation->codeRaw;
    result.instrumentBalance.filenameValue = instrument->filenameBalance;
    result.instrumentBalance.overrideValue = instrument->balanceOverride;
    result.instrumentBalance.effectiveValue = instrument->getEffectiveBalance();
    result.articulationBalance.filenameValue = articulation->filenameBalance;
    result.articulationBalance.overrideValue = articulation->balanceOverride;
    result.articulationBalance.effectiveValue = articulation->getEffectiveBalance();
    result.articulationBalance.filenameConflict = articulation->balanceConflict;
    const auto* articulationOverride = kit->libraryKitOverrides.findArticulationOverride(instrument->meta.codeRaw, articulation->codeRaw);
    result.levels.reserve(articulation->velocityLevels.size());
    for (size_t velocityLevelIdx = 0; velocityLevelIdx < articulation->velocityLevels.size(); ++velocityLevelIdx) {
        const auto& velocityLevel = articulation->velocityLevels[velocityLevelIdx];
        if (velocityLevel == nullptr) continue;
        LibraryVelocityLevelInfo level;
        level.namedVelocity = velocityLevel->velocity;
        level.filenameUpperBoundary = velocityLevelIdx + 1 == articulation->velocityLevels.size() ? MIDI_MAX_VALUE : velocityLevel->velocity;
        level.effectiveUpperBoundary = velocityLevel->selectionUpperVelocity;
        level.overridden = articulationOverride != nullptr && articulationOverride->velocityUpperBounds.find(velocityLevel->velocity) != articulationOverride->velocityUpperBounds.end();
        result.levels.push_back(level);

        for (const auto& logicalHit : velocityLevel->logicalHits) {
            if (logicalHit == nullptr) continue;
            const auto roundRobinLetter = KitLib::roundRobinLetterFromId(logicalHit->roundRobinId);
            for (const auto& sample : logicalHit->samples) {
                if (sample == nullptr) continue;
                LibrarySampleInfo sampleInfo;
                sampleInfo.namedVelocity = velocityLevel->velocity;
                sampleInfo.roundRobin = roundRobinLetter == '\0' ? "-" : juce::String::charToString(roundRobinLetter);
                sampleInfo.relativePath = relativeSamplePath(*kit, sample->sampleFile);
                sampleInfo.fileName = sample->sampleFileName;
                sampleInfo.micCode = sample->micSpec.raw.trim().toLowerCase();
                sampleInfo.contentHash = sample->metadataContentHash;
                sampleInfo.filenameManualGainDb = sample->filenameManualGainDb;
                sampleInfo.effectiveManualGainDb = sample->manualGainDb;
                sampleInfo.filenamePlaybackOffsetMs = sample->filenamePlaybackOffsetMs;
                sampleInfo.effectivePlaybackOffsetMs = sample->playbackOffsetMs;
                sampleInfo.hasMetadata = sample->hasCsvMetadata;
                sampleInfo.onsetMs = sample->onsetMs;
                sampleInfo.intensityDb = sample->intensityDb;
                sampleInfo.truePeakDb = sample->truePeakDb;
                const auto sampleOverrideMatch = kit->libraryKitOverrides.findSampleOverride(sampleInfo.relativePath, sampleInfo.contentHash);
                const auto* sampleOverride = sampleOverrideMatch.value;
                sampleInfo.overrideStoredRelativePath = sampleOverrideMatch.storedRelativePath;
                sampleInfo.overrideMatchedByContentHash = sampleOverrideMatch.matchedByContentHash;
                sampleInfo.overrideContentHashAmbiguous = sampleOverrideMatch.ambiguousContentHash;
                if (sampleOverride != nullptr) {
                    sampleInfo.manualGainDbOverride = sampleOverride->manualGainDb;
                    sampleInfo.filePlaybackOffsetMsOverride = sampleOverride->playbackOffsetMs;
                    const auto micOverrideIt = sampleOverride->micOverrides.find(sampleInfo.micCode);
                    if (micOverrideIt != sampleOverride->micOverrides.end()) sampleInfo.micPlaybackOffsetMsOverride = micOverrideIt->second.playbackOffsetMs;
                }
                for (const auto& instrumentPlayer : this->instrumentPlayerPtrs) {
                    if (instrumentPlayer == nullptr) continue;
                    const auto recipeStatus = instrumentPlayer->getSourceRecipeStatus(*sample);
                    if (recipeStatusPriority(recipeStatus) > recipeStatusPriority(sampleInfo.recipeStatus)) sampleInfo.recipeStatus = recipeStatus;
                }
                result.samples.push_back(std::move(sampleInfo));
            }
        }
    }
    return result;
}

ParseResult<DrumKitEngine::LibraryArticulationInfo> DrumKitEngine::setLibraryVelocityUpperBoundary(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId,
                                                                                                  int namedVelocity, std::optional<int> upperBoundary, bool persist) {
    ParseResult<LibraryArticulationInfo> result;
    if (namedVelocity < 1 || namedVelocity > MIDI_MAX_VALUE) return result.returnWithError("Named velocity must be 1..127");
    if (upperBoundary.has_value() && (*upperBoundary < 1 || *upperBoundary >= MIDI_MAX_VALUE)) return result.returnWithError("Upper boundary must be 1..126");
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");

    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result.returnWithError("Library articulation was not found");
    if (persist && !kit->meta.rootDir.hasWriteAccess()) return result.returnWithError("Library kit folder is read-only");

    const auto velocityLevelIt = std::find_if(articulation->velocityLevels.begin(), articulation->velocityLevels.end(), [namedVelocity](const auto& velocityLevel) {
        return velocityLevel != nullptr && velocityLevel->velocity == namedVelocity;
    });
    if (velocityLevelIt == articulation->velocityLevels.end()) return result.returnWithError("Named velocity was not found in the library articulation");
    if (velocityLevelIt + 1 == articulation->velocityLevels.end()) return result.returnWithError("The last velocity level always ends at 127");

    auto candidateOverrides = kit->libraryKitOverrides;
    std::optional<uint8_t> typedUpperBoundary;
    if (upperBoundary.has_value()) typedUpperBoundary = static_cast<uint8_t>(*upperBoundary);
    candidateOverrides.setVelocityUpperBoundary(instrument->meta.codeRaw, articulation->codeRaw, static_cast<uint8_t>(namedVelocity), typedUpperBoundary);

    std::vector<uint8_t> namedVelocities;
    namedVelocities.reserve(articulation->velocityLevels.size());
    for (const auto& velocityLevel : articulation->velocityLevels) namedVelocities.push_back(velocityLevel->velocity);
    const auto resolved = candidateOverrides.resolveVelocityUpperBoundaries(instrument->meta.codeRaw, articulation->codeRaw, namedVelocities);
    if (!resolved.ok()) return result.returnWithError(resolved.error());

    if (!persist) {
        {
            const juce::ScopedLock graphLock(this->engineGraphLock);
            for (auto& instrumentPlayer : this->instrumentPlayerPtrs)
                if (instrumentPlayer != nullptr) instrumentPlayer->refreshLibraryVelocityBoundaries(*articulation, resolved.value);
        }
        result.value = this->getLibraryArticulationInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
        const auto* candidateArticulationOverride = candidateOverrides.findArticulationOverride(instrument->meta.codeRaw, articulation->codeRaw);
        for (size_t velocityLevelIdx = 0; velocityLevelIdx < result.value.levels.size(); ++velocityLevelIdx) {
            auto& level = result.value.levels[velocityLevelIdx];
            level.effectiveUpperBoundary = resolved.value[velocityLevelIdx];
            level.overridden = candidateArticulationOverride != nullptr && candidateArticulationOverride->velocityUpperBounds.find(static_cast<uint8_t>(level.namedVelocity)) != candidateArticulationOverride->velocityUpperBounds.end();
        }
        return result;
    }

    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());

    this->invalidateLibraryPreviewCache();
    {
        const juce::ScopedLock libraryLock(this->libraryModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        kit->libraryKitOverrides = std::move(candidateOverrides);
        for (size_t velocityLevelIdx = 0; velocityLevelIdx < articulation->velocityLevels.size(); ++velocityLevelIdx) articulation->velocityLevels[velocityLevelIdx]->selectionUpperVelocity = resolved.value[velocityLevelIdx];
        for (auto& instrumentPlayer : this->instrumentPlayerPtrs)
            if (instrumentPlayer != nullptr) instrumentPlayer->refreshLibraryVelocityBoundaries(*articulation);
    }

    result.value = this->getLibraryArticulationInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
    for (const auto& warning : resolved.errors) result.addWarning(warning);
    return result;
}

ParseResult<DrumKitEngine::LibraryArticulationInfo> DrumKitEngine::setLibraryVelocityUpperBoundariesBatch(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const std::vector<VelocityUpperBoundaryTarget>& targets) {
    ParseResult<LibraryArticulationInfo> result;
    if (targets.empty()) return result.returnWithError("No velocity boundaries were selected");
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");

    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result.returnWithError("Library articulation was not found");
    if (!kit->meta.rootDir.hasWriteAccess()) return result.returnWithError("Library kit folder is read-only");

    auto candidateOverrides = kit->libraryKitOverrides;
    std::vector<int> updatedNamedVelocities;
    updatedNamedVelocities.reserve(targets.size());
    for (const auto& target : targets) {
        if (target.namedVelocity < 1 || target.namedVelocity > MIDI_MAX_VALUE) return result.returnWithError("Named velocity must be 1..127");
        if (target.upperBoundary < 1 || target.upperBoundary >= MIDI_MAX_VALUE) return result.returnWithError("Upper boundary must be 1..126");
        if (std::find(updatedNamedVelocities.begin(), updatedNamedVelocities.end(), target.namedVelocity) != updatedNamedVelocities.end())
            return result.returnWithError("Named velocity appears more than once");

        const auto velocityLevelIt = std::find_if(articulation->velocityLevels.begin(), articulation->velocityLevels.end(), [&target](const auto& velocityLevel) {
            return velocityLevel != nullptr && velocityLevel->velocity == target.namedVelocity;
        });
        if (velocityLevelIt == articulation->velocityLevels.end()) return result.returnWithError("Named velocity was not found in the library articulation");
        if (velocityLevelIt + 1 == articulation->velocityLevels.end()) return result.returnWithError("The last velocity level always ends at 127");

        updatedNamedVelocities.push_back(target.namedVelocity);
        candidateOverrides.setVelocityUpperBoundary(instrument->meta.codeRaw, articulation->codeRaw, static_cast<uint8_t>(target.namedVelocity), static_cast<uint8_t>(target.upperBoundary));
    }

    std::vector<uint8_t> namedVelocities;
    namedVelocities.reserve(articulation->velocityLevels.size());
    for (const auto& velocityLevel : articulation->velocityLevels) namedVelocities.push_back(velocityLevel->velocity);
    const auto resolved = candidateOverrides.resolveVelocityUpperBoundaries(instrument->meta.codeRaw, articulation->codeRaw, namedVelocities);
    if (!resolved.ok()) return result.returnWithError(resolved.error());

    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());

    this->invalidateLibraryPreviewCache();
    {
        const juce::ScopedLock libraryLock(this->libraryModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        kit->libraryKitOverrides = std::move(candidateOverrides);
        for (size_t velocityLevelIdx = 0; velocityLevelIdx < articulation->velocityLevels.size(); ++velocityLevelIdx) articulation->velocityLevels[velocityLevelIdx]->selectionUpperVelocity = resolved.value[velocityLevelIdx];
        for (auto& instrumentPlayer : this->instrumentPlayerPtrs)
            if (instrumentPlayer != nullptr) instrumentPlayer->refreshLibraryVelocityBoundaries(*articulation);
    }

    result.value = this->getLibraryArticulationInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
    for (const auto& warning : resolved.errors) result.addWarning(warning);
    return result;
}

ParseResult<DrumKitEngine::LibraryArticulationInfo> DrumKitEngine::resetLibraryVelocityUpperBoundaries(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId) {
    ParseResult<LibraryArticulationInfo> result;
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");

    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result.returnWithError("Library articulation was not found");
    if (!kit->meta.rootDir.hasWriteAccess()) return result.returnWithError("Library kit folder is read-only");

    auto candidateOverrides = kit->libraryKitOverrides;
    candidateOverrides.resetVelocityUpperBoundaries(instrument->meta.codeRaw, articulation->codeRaw);
    std::vector<uint8_t> namedVelocities;
    namedVelocities.reserve(articulation->velocityLevels.size());
    for (const auto& velocityLevel : articulation->velocityLevels) namedVelocities.push_back(velocityLevel->velocity);
    const auto resolved = candidateOverrides.resolveVelocityUpperBoundaries(instrument->meta.codeRaw, articulation->codeRaw, namedVelocities);
    if (!resolved.ok()) return result.returnWithError(resolved.error());

    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());

    this->invalidateLibraryPreviewCache();
    {
        const juce::ScopedLock libraryLock(this->libraryModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        kit->libraryKitOverrides = std::move(candidateOverrides);
        for (size_t velocityLevelIdx = 0; velocityLevelIdx < articulation->velocityLevels.size(); ++velocityLevelIdx) articulation->velocityLevels[velocityLevelIdx]->selectionUpperVelocity = resolved.value[velocityLevelIdx];
        for (auto& instrumentPlayer : this->instrumentPlayerPtrs)
            if (instrumentPlayer != nullptr) instrumentPlayer->refreshLibraryVelocityBoundaries(*articulation);
    }

    result.value = this->getLibraryArticulationInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
    return result;
}

void DrumKitEngine::invalidateLibraryPreviewCache() {
    this->previewGeneration.fetch_add(1, std::memory_order_acq_rel);
    this->previewTargetGeneration.fetch_add(1, std::memory_order_acq_rel);
    this->previewResolveThreadPool.removeAllJobs(true, 2000);
    const juce::ScopedLock resolutionLock(this->previewTargetResolutionLock);
    this->previewTargetCache.clear();
    this->pendingPreviewTargetKeys.clear();
}

ParseResult<DrumKitEngine::LibraryArticulationInfo> DrumKitEngine::setLibraryInstrumentBalance(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, std::optional<int> balance) {
    return this->setLibraryPlacementBalance(kitId, instrumentId, articulationId, balance, false);
}

ParseResult<DrumKitEngine::LibraryArticulationInfo> DrumKitEngine::setLibraryArticulationBalance(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, std::optional<int> balance) {
    return this->setLibraryPlacementBalance(kitId, instrumentId, articulationId, balance, true);
}

ParseResult<DrumKitEngine::LibraryArticulationInfo> DrumKitEngine::setLibraryPlacementBalance(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, std::optional<int> balance, bool articulationScope) {
    ParseResult<LibraryArticulationInfo> result;
    if (balance.has_value() && (*balance < -127 || *balance > 127)) return result.returnWithError("Placement balance must be L1..L127, C0, or R1..R127");
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");
    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result.returnWithError("Library articulation was not found");
    if (!kit->meta.rootDir.hasWriteAccess()) return result.returnWithError("Library kit folder is read-only");

    auto candidateOverrides = kit->libraryKitOverrides;
    if (articulationScope) candidateOverrides.setArticulationBalance(instrument->meta.codeRaw, articulation->codeRaw, balance);
    else candidateOverrides.setInstrumentBalance(instrument->meta.codeRaw, balance);
    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());
    this->invalidateLibraryPreviewCache();

    {
        const juce::ScopedLock libraryLock(this->libraryModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        kit->libraryKitOverrides = std::move(candidateOverrides);
        if (articulationScope) articulation->balanceOverride = balance;
        else instrument->balanceOverride = balance;
        for (const auto& instrumentPlayer : this->instrumentPlayerPtrs) {
            if (instrumentPlayer == nullptr) continue;
            instrumentPlayer->refreshLibraryBalance(*instrument, articulationScope ? articulation : nullptr);
        }
    }
    result.value = this->getLibraryArticulationInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
    return result;
}

ParseResult<DrumKitEngine::LibraryArticulationInfo> DrumKitEngine::setSampleManualGain(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const juce::String& relativePath, std::optional<float> manualGainDb) {
    return this->setSampleManualGainBatch(kitId, instrumentId, articulationId, std::vector<juce::String> { relativePath }, manualGainDb);
}

ParseResult<DrumKitEngine::LibraryArticulationInfo> DrumKitEngine::setSampleManualGainBatch(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const std::vector<juce::String>& relativePaths, std::optional<float> manualGainDb) {
    ParseResult<LibraryArticulationInfo> result;
    if (manualGainDb.has_value() && !std::isfinite(*manualGainDb)) return result.returnWithError("Manual gain must be a finite dB value");
    if (relativePaths.empty()) return result.returnWithError("No sample files were selected");
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");
    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result.returnWithError("Library articulation was not found");
    if (!kit->meta.rootDir.hasWriteAccess()) return result.returnWithError("Library kit folder is read-only");

    auto candidateOverrides = kit->libraryKitOverrides;
    struct ResolvedTarget {
        juce::String overridePath;
        std::vector<KitLib::SampleSpec*> matchingSamples;
    };
    std::vector<ResolvedTarget> resolvedTargets;
    juce::StringArray uniquePaths;
    for (const auto& relativePath : relativePaths) {
        const auto normalizedPath = relativePath.trim().replaceCharacter('\\', '/');
        if (uniquePaths.contains(normalizedPath)) continue;
        uniquePaths.add(normalizedPath);
        auto matchingSamples = findSamplesByRelativePath(*kit, *articulation, normalizedPath);
        if (matchingSamples.empty()) return result.returnWithError("Sample file was not found in the library articulation: " + normalizedPath);
        const auto contentHash = firstContentHash(matchingSamples);
        const auto existingOverrideMatch = kit->libraryKitOverrides.findSampleOverride(normalizedPath, contentHash);
        if (existingOverrideMatch.ambiguousContentHash) return result.returnWithError("Sample content hash matches more than one saved override. Reload after resolving the duplicate entries.");
        const auto overridePath = existingOverrideMatch.value != nullptr ? existingOverrideMatch.storedRelativePath : normalizedPath;
        candidateOverrides.setSampleManualGain(overridePath, contentHash, manualGainDb);
        resolvedTargets.push_back({ overridePath, std::move(matchingSamples) });
    }
    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());
    this->invalidateLibraryPreviewCache();

    {
        const juce::ScopedLock libraryLock(this->libraryModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        kit->libraryKitOverrides = std::move(candidateOverrides);
        for (const auto& resolvedTarget : resolvedTargets) {
            const auto* sampleOverride = kit->libraryKitOverrides.findSampleOverride(resolvedTarget.overridePath);
            const std::optional<float> effectiveOverride = sampleOverride != nullptr ? sampleOverride->manualGainDb : std::nullopt;
            for (auto* sample : resolvedTarget.matchingSamples) {
                const float previousGainDb = sample->manualGainDb;
                sample->manualGainDbOverride = effectiveOverride;
                sample->manualGainDb = sample->manualGainDbOverride.value_or(sample->filenameManualGainDb);
                const float gainDeltaDb = sample->manualGainDb - previousGainDb;
                sample->peakDb += gainDeltaDb;
                if (sample->decayEnvelope.isValid()) sample->decayEnvelope.peakDb += gainDeltaDb;
            }
        }
        instrument->buildCalibrationProfiles();
        for (const auto& instrumentPlayer : this->instrumentPlayerPtrs)
            if (instrumentPlayer != nullptr) instrumentPlayer->refreshLibraryCalibrationAndReload(*articulation);
    }

    result.value = this->getLibraryArticulationInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
    return result;
}

ParseResult<DrumKitEngine::LibraryArticulationInfo> DrumKitEngine::setSamplePlaybackOffset(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const juce::String& relativePath, const std::optional<juce::String>& micCode, std::optional<float> playbackOffsetMs) {
    return this->setSamplePlaybackOffsetBatch(kitId, instrumentId, articulationId, std::vector<SamplePlaybackOffsetTarget> { { relativePath, micCode } }, playbackOffsetMs);
}

ParseResult<DrumKitEngine::LibraryArticulationInfo> DrumKitEngine::setSamplePlaybackOffsetBatch(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const std::vector<SamplePlaybackOffsetTarget>& targets, std::optional<float> playbackOffsetMs) {
    ParseResult<LibraryArticulationInfo> result;
    if (playbackOffsetMs.has_value() && !std::isfinite(*playbackOffsetMs)) return result.returnWithError("Playback offset must be a finite millisecond value");
    if (targets.empty()) return result.returnWithError("No sample files were selected");
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");
    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result.returnWithError("Library articulation was not found");
    if (!kit->meta.rootDir.hasWriteAccess()) return result.returnWithError("Library kit folder is read-only");

    auto candidateOverrides = kit->libraryKitOverrides;
    struct ResolvedTarget {
        juce::String overridePath;
        std::vector<KitLib::SampleSpec*> matchingSamples;
    };
    std::vector<ResolvedTarget> resolvedTargets;
    juce::StringArray uniqueTargets;
    for (const auto& target : targets) {
        const auto normalizedPath = target.relativePath.trim().replaceCharacter('\\', '/');
        const auto normalizedMicCode = target.micCode.has_value() ? target.micCode->trim().toLowerCase() : juce::String();
        const auto targetKey = normalizedPath + "\n" + normalizedMicCode;
        if (uniqueTargets.contains(targetKey)) continue;
        uniqueTargets.add(targetKey);
        auto matchingSamples = findSamplesByRelativePath(*kit, *articulation, normalizedPath);
        if (matchingSamples.empty()) return result.returnWithError("Sample file was not found in the library articulation: " + normalizedPath);
        if (target.micCode.has_value() && normalizedMicCode.isEmpty()) return result.returnWithError("Mic code is empty");
        if (target.micCode.has_value() && std::none_of(matchingSamples.begin(), matchingSamples.end(), [&normalizedMicCode](const auto* sample) {
            return sample != nullptr && sample->micSpec.raw.trim().toLowerCase() == normalizedMicCode;
        })) return result.returnWithError("Mic was not found in this sample file: " + normalizedMicCode);

        const auto contentHash = firstContentHash(matchingSamples);
        const auto existingOverrideMatch = kit->libraryKitOverrides.findSampleOverride(normalizedPath, contentHash);
        if (existingOverrideMatch.ambiguousContentHash) return result.returnWithError("Sample content hash matches more than one saved override. Reload after resolving the duplicate entries.");
        const auto overridePath = existingOverrideMatch.value != nullptr ? existingOverrideMatch.storedRelativePath : normalizedPath;
        if (target.micCode.has_value()) candidateOverrides.setMicPlaybackOffset(overridePath, contentHash, normalizedMicCode, playbackOffsetMs);
        else candidateOverrides.setSamplePlaybackOffset(overridePath, contentHash, playbackOffsetMs);
        resolvedTargets.push_back({ overridePath, std::move(matchingSamples) });
    }
    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());
    this->invalidateLibraryPreviewCache();

    {
        const juce::ScopedLock libraryLock(this->libraryModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        kit->libraryKitOverrides = std::move(candidateOverrides);
        for (const auto& resolvedTarget : resolvedTargets) {
            const auto* sampleOverride = kit->libraryKitOverrides.findSampleOverride(resolvedTarget.overridePath);
            for (auto* sample : resolvedTarget.matchingSamples) {
                const auto sampleMicCode = sample->micSpec.raw.trim().toLowerCase();
                if (sampleOverride != nullptr) {
                    const auto micOverrideIt = sampleOverride->micOverrides.find(sampleMicCode);
                    if (micOverrideIt != sampleOverride->micOverrides.end() && micOverrideIt->second.playbackOffsetMs.has_value())
                        sample->playbackOffsetMsOverride = micOverrideIt->second.playbackOffsetMs;
                    else
                        sample->playbackOffsetMsOverride = sampleOverride->playbackOffsetMs;
                } else {
                    sample->playbackOffsetMsOverride.reset();
                }
                sample->playbackOffsetMs = sample->playbackOffsetMsOverride.value_or(sample->filenamePlaybackOffsetMs);
                for (const auto& instrumentPlayer : this->instrumentPlayerPtrs)
                    if (instrumentPlayer != nullptr) instrumentPlayer->refreshSamplePlaybackOffset(*sample);
            }
        }
    }

    result.value = this->getLibraryArticulationInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
    return result;
}

ParseResult<DrumKitEngine::LibraryArticulationInfo> DrumKitEngine::reloadLibraryKitOverrides(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId) {
    ParseResult<LibraryArticulationInfo> result;
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");

    juce::File kitRoot;
    {
        const juce::ScopedLock libraryLock(this->libraryModelLock);
        const auto* kit = this->libraryManagerPtr->findKitById(kitId);
        if (kit == nullptr) return result.returnWithError("Library kit was not found");
        kitRoot = kit->meta.rootDir;
    }

    KitLib::LibraryKitOverrides reloadedOverrides;
    auto loadResult = reloadedOverrides.loadFromKitRoot(kitRoot);
    if (!loadResult.ok()) return result.returnWithError(loadResult.error());
    this->invalidateLibraryPreviewCache();

    juce::String resolvedKitId;
    juce::String resolvedInstrumentId;
    juce::String resolvedArticulationId;
    {
        const juce::ScopedLock libraryLock(this->libraryModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        auto* kit = this->libraryManagerPtr->findKitById(kitId);
        auto* selectedInstrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
        auto* selectedArticulation = selectedInstrument != nullptr ? selectedInstrument->findArticulationById(articulationId) : nullptr;
        if (kit == nullptr || selectedInstrument == nullptr || selectedArticulation == nullptr) return result.returnWithError("Library articulation was not found");

        kit->libraryKitOverrides = std::move(reloadedOverrides);
        for (const auto& [instrumentKey, instrument] : kit->instrumentSpecsMap) {
            juce::ignoreUnused(instrumentKey);
            if (instrument != nullptr) instrument->applyLibraryKitOverrides();
        }
        for (const auto& instrumentPlayer : this->instrumentPlayerPtrs) {
            if (instrumentPlayer == nullptr) continue;
            for (const auto& [instrumentKey, instrument] : kit->instrumentSpecsMap) {
                juce::ignoreUnused(instrumentKey);
                if (instrument == nullptr) continue;
                instrumentPlayer->refreshLibraryBalance(*instrument);
                for (const auto& articulation : instrument->articulationSpecs) {
                    if (articulation == nullptr) continue;
                    instrumentPlayer->refreshLibraryVelocityBoundaries(*articulation);
                    instrumentPlayer->refreshLibraryCalibrationAndReload(*articulation);
                    for (const auto& velocityLevel : articulation->velocityLevels) {
                        if (velocityLevel == nullptr) continue;
                        for (const auto& logicalHit : velocityLevel->logicalHits) {
                            if (logicalHit == nullptr) continue;
                            for (const auto& sample : logicalHit->samples)
                                if (sample != nullptr) instrumentPlayer->refreshSamplePlaybackOffset(*sample);
                        }
                    }
                }
            }
        }
        resolvedKitId = kit->meta.codeRaw;
        resolvedInstrumentId = selectedInstrument->meta.codeRaw;
        resolvedArticulationId = selectedArticulation->codeRaw;
    }

    result.value = this->getLibraryArticulationInfo(resolvedKitId, resolvedInstrumentId, resolvedArticulationId);
    for (const auto& warning : loadResult.errors) result.addWarning(warning);
    return result;
}

bool DrumKitEngine::publishSourceRecipeReload(const std::shared_ptr<SampledSound>& sound, uint64_t revision, float fixedGainDb, std::shared_ptr<juce::AudioBuffer<float>> buffer, double sampleRate) {
    if (sound == nullptr) return false;
    const juce::ScopedLock graphLock(this->engineGraphLock);
    if (sound->sourceRecipeRevision.load(std::memory_order_acquire) != revision) return false;
    if (buffer == nullptr) {
        sound->sourceRecipeStatus.store(SampledSound::SourceRecipeStatus::Failed, std::memory_order_release);
        return false;
    }

    sound->buffer = std::move(buffer);
    sound->sampleRate = sampleRate;
    sound->sampleLengthSamples = sound->buffer->getNumSamples();
    sound->sampleLengthMs = sampleRate > 0.0 ? static_cast<float>((static_cast<double>(sound->sampleLengthSamples) / sampleRate) * 1000.0) : 0.0f;
    sound->transitionAudioValid = false;
    if (sound->buffer->getNumChannels() > 0 && sound->buffer->getNumSamples() > 0) {
        int sourceChannelStart = 0;
        int sourceChannelCount = sound->buffer->getNumChannels();
        if (sound->sampleSpec != nullptr) {
            sourceChannelStart = sound->sampleSpec->fileChannelStart;
            if (sound->sampleSpec->fileChannelCount > 0) sourceChannelCount = sound->sampleSpec->fileChannelCount;
        }
        const int sourceChannelEnd = juce::jmin(sound->buffer->getNumChannels(), sourceChannelStart + sourceChannelCount);
        for (int sourceChannel = juce::jmax(0, sourceChannelStart); sourceChannel < sourceChannelEnd; ++sourceChannel) {
            if (sound->buffer->getMagnitude(sourceChannel, 0, sound->buffer->getNumSamples()) <= 0.000001f) continue;
            sound->transitionAudioValid = true;
            break;
        }
    }
    sound->canonicalFixedGainDb = fixedGainDb;
    sound->requestedFixedGainDb = fixedGainDb;
    sound->cachedSourceGainCorrection.store(1.0f, std::memory_order_release);
    sound->loaded.store(true, std::memory_order_release);
    sound->sourceRecipeStatus.store(SampledSound::SourceRecipeStatus::Applied, std::memory_order_release);
    return true;
}
