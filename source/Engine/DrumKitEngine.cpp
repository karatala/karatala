#include "DrumKitEngine.h"
#include "DrumKitEngineMappingModel.h"
#include "DrumKitEngineTextView.h"
#include "ArticulationNameHelpers.h"
#include "DrumKitEngineMapHelpers.h"
#include "DrumKitEnginePreviewHelpers.h"
#include "InstrumentPlayer.h"
#include "SampleSelector.h"
#include "../Config/Helpers.h"
#include "../Config/YamlHelper.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace DrumKitEngineIdentityMapHelpers {

static juce::String identityKey(const juce::String& value) { return value.trim().toLowerCase(); }

static bool candidateMatches(const std::vector<juce::String>& candidates, const juce::String& id) {
    const auto key = identityKey(id);
    if (key.isEmpty()) return false;
    for (const auto& candidate : candidates)
        if (identityKey(candidate) == key) return true;
    return false;
}

static juce::String firstCandidateOrRaw(const std::vector<juce::String>& candidates, const juce::String& raw) {
    for (const auto& candidate : candidates)
        if (candidate.trim().isNotEmpty()) return candidate.trim();
    return raw.trim();
}

static juce::String makeMapLayerSortKey(const DrumKitEngine::InstrumentMapLayer& mapLayer) {
    const auto articulationSlotCode = mapLayer.slotArticulationFilter.empty() ? juce::String() : mapLayer.slotArticulationFilter.front().trim().toLowerCase();
    return mapLayer.slotId.trim().toLowerCase() + "/" + articulationSlotCode;
}

static int countKitIdMatches(const KitLib::LibraryManager& libraryManager, const juce::String& id) {
    int matchCount = 0;
    for (const auto& kitPtr : libraryManager.getKits()) {
        if (kitPtr == nullptr) continue;
        if (candidateMatches(KitLib::LibraryManager::getIdentityCandidates(kitPtr->meta), id)) ++matchCount;
    }
    return matchCount;
}

static int countInstrumentIdMatches(const KitLib::DrumKitSpec& kitSpec, const juce::String& id) {
    int matchCount = 0;
    for (const auto& [instrumentKey, instrumentPtr] : kitSpec.getInstrumentSpecsMap()) {
        juce::ignoreUnused(instrumentKey);
        if (instrumentPtr == nullptr) continue;
        if (candidateMatches(KitLib::InstrumentSpec::getIdentityCandidates(instrumentPtr->meta), id)) ++matchCount;
    }
    return matchCount;
}

static int countArticulationIdMatches(const KitLib::InstrumentSpec& instrumentSpec, const juce::String& id) {
    int matchCount = 0;
    for (const auto& articulationPtr : instrumentSpec.articulationSpecs) {
        if (articulationPtr == nullptr) continue;
        if (candidateMatches(articulationPtr->getIdentityCandidates(), id)) ++matchCount;
    }
    return matchCount;
}

static juce::String describeKitResolveFailure(const KitLib::LibraryManager& libraryManager, const juce::String& kitRef, const juce::String& slotId) {
    const auto matchCount = countKitIdMatches(libraryManager, kitRef);
    const auto reason = matchCount > 1 ? "Library kit ID is ambiguous" : "Library kit was not found";
    return juce::String(reason) + ": " + kitRef + " for slot '" + slotId + "'";
}

static juce::String describeInstrumentResolveFailure(const KitLib::DrumKitSpec& kitSpec, const juce::String& instCode, const juce::String& slotId) {
    const auto matchCount = countInstrumentIdMatches(kitSpec, instCode);
    const auto reason = matchCount > 1 ? "Library instrument ID is ambiguous" : "Library instrument was not found";
    return juce::String(reason) + ": " + kitSpec.meta.codeRaw + "/" + instCode + " for slot '" + slotId + "'";
}

static juce::String describeArticulationResolveFailure(const KitLib::InstrumentSpec& instrumentSpec, const juce::String& artCode, const juce::String& libraryInstrumentPath, const juce::String& slotId) {
    const auto matchCount = countArticulationIdMatches(instrumentSpec, artCode);
    const auto reason = matchCount > 1 ? "Library articulation ID is ambiguous" : "Unknown library articulation";
    return juce::String(reason) + ": " + artCode + " in library instrument '" + libraryInstrumentPath + "' for slot '" + slotId + "'";
}

static void normalizeLibraryArticulationFilters(DrumKitEngine::InstrumentMapLayer& mapLayer, const KitLib::InstrumentSpec& instrumentSpec) {
    for (auto& libraryArticulationName : mapLayer.articulationFilter) {
        auto* articulationSpec = instrumentSpec.findArticulationById(libraryArticulationName);
        if (articulationSpec != nullptr) libraryArticulationName = instrumentSpec.getPreferredArticulationId(*articulationSpec);
    }
}

static void normalizeInstrumentMapLayers(DrumKitEngine::InstrumentMap& instrumentMap, KitLib::LibraryManager& libraryManager) {
    for (auto& mapLayer : instrumentMap.layers) {
        auto* kitSpec = libraryManager.findKitById(mapLayer.kitRef);
        if (kitSpec == nullptr) continue;

        mapLayer.kitRef = libraryManager.getPreferredKitId(*kitSpec);
        if (mapLayer.instCode.isEmpty()) continue;

        auto* instSpec = kitSpec->findInstrumentById(mapLayer.instCode);
        if (instSpec == nullptr) continue;

        mapLayer.instCode = kitSpec->getPreferredInstrumentId(*instSpec);
        normalizeLibraryArticulationFilters(mapLayer, *instSpec);
    }
}

} // namespace DrumKitEngineIdentityMapHelpers

namespace DrumKitEngineVelocityResponseHelpers {

static juce::String safePresetName(const juce::String& name) {
    return juce::File::createLegalFileName(name.trim()).replaceCharacters(":#\r\n\"", "_____").trim();
}

} // namespace DrumKitEngineVelocityResponseHelpers

namespace DrumKitEngineLibraryOverrideDetail {

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

} // namespace DrumKitEngineLibraryOverrideDetail

MicLayout DrumKitEngine::resolveMicLayoutFromConfig() {
    const char* dataPathEnv = "EDRUMS_DATA_PATH";
    const auto defaultDataPath = DrumKitEngine().defaultDataPath;

    juce::File resolvedDataPath;
    auto envInfo = getEnvPath(dataPathEnv);
    if (envInfo.ok()) {
        resolvedDataPath = envInfo.value;
    } else {
        LOG(Notice, "" + envInfo.error());
        resolvedDataPath = defaultDataPath;
    }

    juce::String configuredMicLayout;
    const juce::File configFile = resolvedDataPath.getChildFile("config.yaml");
    if (configFile.existsAsFile()) {
        const auto root = YamlHelper::parse(configFile.loadFileAsString());
        if (!root.isNull()) {
            configuredMicLayout = root["micLayout"].asString().trim();
        } else {
            LOG(Warning, "Unable to parse config.yaml when resolving mic layout. Falling back to default.");
        }
    }

    if (configuredMicLayout.isNotEmpty() && configuredMicLayout != "default") {
        MicLayoutManager micLayoutManager;
        micLayoutManager.initDefault();
        auto scanResult = micLayoutManager.scanLayouts(resolvedDataPath.getChildFile("mic_layouts"));
        if (!scanResult.ok()) {
            LOG(Warning, "resolveMicLayoutFromConfig: " + scanResult.error());
        }

        if (const auto* resolved = micLayoutManager.getLayoutById(configuredMicLayout)) {
            return *resolved;
        }

        LOG(Warning, "resolveMicLayoutFromConfig: MicLayout not found: " + configuredMicLayout + ". Using default.");
    }

    return MicLayout::getDefault();
}


bool DrumKitEngine::useKitLayout(juce::String name) {
    if (name == "default") return this->setKitLayout(this->getKitLayoutManager()->getDefault());

    const KitLayout* ptr = this->getKitLayoutManager()->getLayoutById(name);
    if (ptr != nullptr) return this->setKitLayout(*ptr);

    auto userFile = this->getUserPresetsDir().getChildFile(name + ".yml");
    if (!userFile.existsAsFile())
        userFile = this->getUserPresetsDir().getChildFile(name + ".yaml");
    if (userFile.existsAsFile()) {
        auto parsed = LayoutTraits<KitLayout>::parseYaml(userFile.loadFileAsString());
        if (parsed.ok()) {
            parsed.value.id = name;
            parsed.value.rebuildIndices();
            return this->setKitLayout(std::move(parsed.value));
        }
    }
    LOG(Warning, "KitLayout not found: " + name);
    return false;
}

std::vector<DrumKitEngine::VelocityResponsePreset> DrumKitEngine::getVelocityResponsePresets(juce::StringArray* warnings) const {
    std::vector<VelocityResponsePreset> presets;
    presets.push_back({ "Linear", "linear", {}, false });
    presets.push_back({ "Logarithmic", "logarithmic", {}, false });
    presets.push_back({ "Exponential", "exponential", {}, false });

    const auto directory = this->getUserVelocityResponsePresetsDir();
    if (!directory.isDirectory()) return presets;

    auto files = directory.findChildFiles(juce::File::findFiles, false, "*.yml;*.yaml");
    std::sort(files.begin(), files.end(), [](const juce::File& leftFile, const juce::File& rightFile) {
        return leftFile.getFileName().compareIgnoreCase(rightFile.getFileName()) < 0;
    });

    for (const auto& file : files) {
        const auto root = YamlHelper::parse(file.loadFileAsString());
        const auto name = root["name"].asString().trim();
        const auto definition = root["velocityResponse"].asString().trim();
        auto parsedResponse = VelocityResponse::fromDefinition(definition);
        if (root.isNull() || name.isEmpty() || definition.isEmpty() || !parsedResponse.ok()) {
            if (warnings != nullptr) warnings->add("Invalid velocity-response preset: " + file.getFileName());
            continue;
        }

        VelocityResponsePreset preset;
        preset.name = name;
        preset.definition = parsedResponse.value.getDefinition();
        preset.file = file;
        preset.isUser = true;
        presets.push_back(std::move(preset));
    }

    return presets;
}

ParseResult<DrumKitEngine::VelocityResponsePreset> DrumKitEngine::saveVelocityResponsePreset(const juce::String& name, const juce::String& definition, bool replaceExisting) const {
    ParseResult<VelocityResponsePreset> result;
    const auto safeName = DrumKitEngineVelocityResponseHelpers::safePresetName(name);
    if (safeName.isEmpty() || safeName == "." || safeName == "..") return result.returnWithError("Preset name is empty");

    auto parsedResponse = VelocityResponse::fromDefinition(definition);
    if (!parsedResponse.ok()) return result.returnWithError(parsedResponse.error());

    const auto directory = this->getUserVelocityResponsePresetsDir();
    const auto createResult = directory.createDirectory();
    if (createResult.failed()) return result.returnWithError("Could not create velocity-response preset directory");

    auto file = directory.getChildFile(safeName + ".yaml");
    const auto alternateFile = directory.getChildFile(safeName + ".yml");
    if (!file.existsAsFile() && alternateFile.existsAsFile()) file = alternateFile;
    if (file.existsAsFile() && !replaceExisting) return result.returnWithError("Preset already exists");

    const auto yaml = "name: \"" + safeName + "\"\nvelocityResponse: \"" + parsedResponse.value.getDefinition() + "\"\n";
    if (!file.replaceWithText(yaml)) return result.returnWithError("Could not save velocity-response preset");

    result.value.name = safeName;
    result.value.definition = parsedResponse.value.getDefinition();
    result.value.file = file;
    result.value.isUser = true;
    return result;
}

DrumKitEngine::LibraryVelocityResponseInfo DrumKitEngine::getLibraryVelocityResponseInfo(const juce::String& sourcePath) const {
    LibraryVelocityResponseInfo result;
    if (this->libraryManagerPtr == nullptr) return result;

    juce::StringArray sourceParts;
    sourceParts.addTokens(sourcePath.trim(), "/", "");
    sourceParts.removeEmptyStrings();
    if (sourceParts.size() < 2) return result;

    const auto* kit = this->libraryManagerPtr->findKitById(sourceParts[0]);
    const auto* instrument = kit != nullptr ? kit->findInstrumentById(sourceParts[1]) : nullptr;
    if (instrument == nullptr) return result;

    const KitLib::ArticulationSpec* articulation = nullptr;
    if (sourceParts.size() >= 3) articulation = instrument->findArticulationById(sourceParts[2]);
    else if (instrument->articulationSpecs.size() == 1) articulation = instrument->articulationSpecs.front().get();
    if (articulation == nullptr) return result;

    result.libraryArticulationFound = true;
    std::optional<float> hardestFittedDb;
    for (auto velocityLevelIt = articulation->velocityLevels.rbegin(); velocityLevelIt != articulation->velocityLevels.rend(); ++velocityLevelIt) {
        if (*velocityLevelIt != nullptr && (*velocityLevelIt)->fittedSourceDb.has_value()) {
            hardestFittedDb = (*velocityLevelIt)->fittedSourceDb;
            break;
        }
    }

    result.levels.reserve(articulation->velocityLevels.size());
    for (const auto& velocityLevel : articulation->velocityLevels) {
        if (velocityLevel == nullptr) continue;
        VelocityResponseLevelInfo level;
        level.velocity = velocityLevel->velocity;
        if (hardestFittedDb.has_value() && velocityLevel->rawVelocityMedianDb.has_value()) level.rawMedianRelativeDb = *velocityLevel->rawVelocityMedianDb - *hardestFittedDb;
        if (hardestFittedDb.has_value() && velocityLevel->fittedSourceDb.has_value()) level.fittedRelativeDb = *velocityLevel->fittedSourceDb - *hardestFittedDb;
        level.calibrationTrimDb = velocityLevel->calibrationTrimDb;
        level.calibrationLimited = velocityLevel->calibrationLimited;
        result.levels.push_back(std::move(level));
    }

    return result;
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
                sampleInfo.relativePath = DrumKitEngineLibraryOverrideDetail::relativeSamplePath(*kit, sample->sampleFile);
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
                    if (DrumKitEngineLibraryOverrideDetail::recipeStatusPriority(recipeStatus) > DrumKitEngineLibraryOverrideDetail::recipeStatusPriority(sampleInfo.recipeStatus)) sampleInfo.recipeStatus = recipeStatus;
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

    this->invalidateSourcePreviewCache();
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

    this->invalidateSourcePreviewCache();
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

    this->invalidateSourcePreviewCache();
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

void DrumKitEngine::invalidateSourcePreviewCache() {
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
    this->invalidateSourcePreviewCache();

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
        auto matchingSamples = DrumKitEngineLibraryOverrideDetail::findSamplesByRelativePath(*kit, *articulation, normalizedPath);
        if (matchingSamples.empty()) return result.returnWithError("Sample file was not found in the library articulation: " + normalizedPath);
        const auto contentHash = DrumKitEngineLibraryOverrideDetail::firstContentHash(matchingSamples);
        const auto existingOverrideMatch = kit->libraryKitOverrides.findSampleOverride(normalizedPath, contentHash);
        if (existingOverrideMatch.ambiguousContentHash) return result.returnWithError("Sample content hash matches more than one saved override. Reload after resolving the duplicate entries.");
        const auto overridePath = existingOverrideMatch.value != nullptr ? existingOverrideMatch.storedRelativePath : normalizedPath;
        candidateOverrides.setSampleManualGain(overridePath, contentHash, manualGainDb);
        resolvedTargets.push_back({ overridePath, std::move(matchingSamples) });
    }
    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());
    this->invalidateSourcePreviewCache();

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
        auto matchingSamples = DrumKitEngineLibraryOverrideDetail::findSamplesByRelativePath(*kit, *articulation, normalizedPath);
        if (matchingSamples.empty()) return result.returnWithError("Sample file was not found in the library articulation: " + normalizedPath);
        if (target.micCode.has_value() && normalizedMicCode.isEmpty()) return result.returnWithError("Mic code is empty");
        if (target.micCode.has_value() && std::none_of(matchingSamples.begin(), matchingSamples.end(), [&normalizedMicCode](const auto* sample) {
            return sample != nullptr && sample->micSpec.raw.trim().toLowerCase() == normalizedMicCode;
        })) return result.returnWithError("Mic was not found in this sample file: " + normalizedMicCode);

        const auto contentHash = DrumKitEngineLibraryOverrideDetail::firstContentHash(matchingSamples);
        const auto existingOverrideMatch = kit->libraryKitOverrides.findSampleOverride(normalizedPath, contentHash);
        if (existingOverrideMatch.ambiguousContentHash) return result.returnWithError("Sample content hash matches more than one saved override. Reload after resolving the duplicate entries.");
        const auto overridePath = existingOverrideMatch.value != nullptr ? existingOverrideMatch.storedRelativePath : normalizedPath;
        if (target.micCode.has_value()) candidateOverrides.setMicPlaybackOffset(overridePath, contentHash, normalizedMicCode, playbackOffsetMs);
        else candidateOverrides.setSamplePlaybackOffset(overridePath, contentHash, playbackOffsetMs);
        resolvedTargets.push_back({ overridePath, std::move(matchingSamples) });
    }
    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());
    this->invalidateSourcePreviewCache();

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
    this->invalidateSourcePreviewCache();

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

bool DrumKitEngine::useMicLayout(juce::String name) {
    const MicLayout* ptr = this->getMicLayoutManager()->getLayoutById(name);
    if (ptr != nullptr) return this->setMicLayout(*ptr);
    if (name != "default") LOG(Warning, "MicLayout not found: " + name);
    return false;
}

bool DrumKitEngine::setKitLayout(KitLayout layout, bool reapplyCurrentMap) {
    this->activeKitLayoutPtr = std::make_unique<KitLayout>(layout);
    const bool layoutApplied = this->applyKitLayout();
    if (layoutApplied && reapplyCurrentMap && this->lastAppliedInstrumentMapText.isNotEmpty())
        this->useInstrumentMap(this->lastAppliedInstrumentMapText);
    return layoutApplied;
}

bool DrumKitEngine::setGlobalKitLayoutId(const juce::String& layoutId) {
    const auto trimmed = layoutId.trim();
    if (trimmed.isEmpty()) return false;
    if (trimmed != "default" && this->kitLayoutManagerPtr->getLayoutById(trimmed) == nullptr) {
        const auto yml = this->getUserPresetsDir().getChildFile(trimmed + ".yml");
        const auto yaml = this->getUserPresetsDir().getChildFile(trimmed + ".yaml");
        if (!yml.existsAsFile() && !yaml.existsAsFile()) return false;
    }
    this->globalConfig.kitLayout = trimmed;
    this->saveSettings();
    return true;
}

bool DrumKitEngine::setGlobalMicLayoutId(const juce::String& layoutId) {
    const auto trimmed = layoutId.trim();
    if (trimmed.isEmpty()) return false;
    if (trimmed != "default" && this->micLayoutManagerPtr->getLayoutById(trimmed) == nullptr) return false;
    this->globalConfig.micLayout = trimmed;
    this->saveSettings();
    return true;
}

bool DrumKitEngine::setGlobalLibraryPath(const juce::File& libraryPath) {
    if (!libraryPath.isDirectory()) return false;
    this->globalConfig.libraryPath = libraryPath;
    this->saveSettings();
    return true;
}

void DrumKitEngine::setGlobalSampleLoaderCacheLimitMegabytes(uint64_t limitMegabytes) {
    this->globalConfig.sampleLoaderCacheLimitMegabytes = juce::jmax<uint64_t>(1, limitMegabytes);
    if (this->sampleLoaderPtr != nullptr)
        this->sampleLoaderPtr->setCacheLimitBytes(this->globalConfig.sampleLoaderCacheLimitMegabytes * 1024ull * 1024ull);
    this->saveSettings();
}

void DrumKitEngine::setGlobalLogLevel(DebugLevel level) {
    this->globalConfig.logLevel = level;
    GlobalLogger::instance().setLogLevel(level);
}

bool DrumKitEngine::setMicLayout(MicLayout layout) {
    this->activeMicLayoutPtr = std::make_unique<MicLayout>(layout);
    return this->applyMicLayout();
}

void DrumKitEngine::mapDefaultKit() {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    const auto& kits = this->getInstrumentManager()->getKits();
    const KitLib::DrumKitSpec* kit = kits.empty() ? nullptr : kits[0].get(); //MAYBE: later use global setting for default kit instead
    if (kit != nullptr) {
        this->mapKit(*kit);
        this->regenerateSuggestedInstrumentMapText();
        auto parsedSuggestedMap = DrumKitEngine::parseInstrumentMapText(this->suggestedInstrumentMapText);
        if (parsedSuggestedMap.ok() && this->libraryManagerPtr != nullptr && this->activeKitLayoutPtr != nullptr) {
            DrumKitEngineMapHelpers::applyAutomaticOrientation(parsedSuggestedMap.value, *this->activeKitLayoutPtr, *this->libraryManagerPtr);
            const auto normalizedSuggestedMapText = DrumKitEngine::serializeInstrumentMap(parsedSuggestedMap.value);
            this->lastAppliedSlotFingerprints = DrumKitEngineMapHelpers::buildResolvedSlotFingerprintMap(parsedSuggestedMap.value, *this->libraryManagerPtr);
            this->lastAppliedSlotReloadFingerprints = DrumKitEngineMapHelpers::buildResolvedSlotReloadFingerprintMap(parsedSuggestedMap.value, *this->libraryManagerPtr);
            this->lastAppliedShadowFingerprints.clear();
            this->lastAppliedShadowReloadFingerprints.clear();
            this->lastAppliedInstrumentMapText = normalizedSuggestedMapText;
        } else {
            this->lastAppliedSlotFingerprints.clear();
            this->lastAppliedSlotReloadFingerprints.clear();
            this->lastAppliedShadowFingerprints.clear();
            this->lastAppliedShadowReloadFingerprints.clear();
            this->lastAppliedInstrumentMapText.clear();
        }
        this->lastInstrumentMapStatus = "Mapped default library kit and generated starter instrument map";//FIXME: refactor, ui relates shit out
    } else {
        this->suggestedInstrumentMapText.clear();
        this->lastAppliedSlotFingerprints.clear();
        this->lastAppliedSlotReloadFingerprints.clear();
        this->lastAppliedShadowFingerprints.clear();
        this->lastAppliedShadowReloadFingerprints.clear();
        this->lastAppliedInstrumentMapText.clear();
    }
}

juce::String DrumKitEngine::buildInstrumentMapForKit(const KitLayout& layout, const KitLib::DrumKitSpec& libraryKit) const {
    InstrumentMap result;
    std::unordered_set<KitLib::InstrumentSpec*> usedLibraryInstruments;
    std::map<KitLib::InstrumentSpec::Type, int> nextIndexByType;

    for (const auto& slot : layout.instrumentSlots) {
        auto* matchedLibraryInstrument = DrumKitEngineMapHelpers::findMatchingLibraryInstrument(slot, libraryKit, usedLibraryInstruments, nextIndexByType);
        if (matchedLibraryInstrument == nullptr) continue;
        const auto preferredLibraryKitId = this->libraryManagerPtr != nullptr
            ? this->libraryManagerPtr->getPreferredKitId(libraryKit)
            : DrumKitEngineIdentityMapHelpers::firstCandidateOrRaw(KitLib::DrumKitSpec::getIdentityCandidates(libraryKit.meta), libraryKit.meta.codeRaw);
        auto mapLayers = DrumKitEngineMapHelpers::buildInstrumentMapLayers(slot, libraryKit, *matchedLibraryInstrument, preferredLibraryKitId);
        result.layers.insert(result.layers.end(), std::make_move_iterator(mapLayers.begin()), std::make_move_iterator(mapLayers.end()));
    }

    return serializeInstrumentMap(result);
}

ParseResult<juce::String> DrumKitEngine::buildInstrumentMapWithSlotInstrument(const juce::String& currentMapText, const juce::String& slotId, const juce::String& libraryKitId, const juce::String& libraryInstrumentId) const {
    ParseResult<juce::String> result;
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");
    if (this->activeKitLayoutPtr == nullptr) return result.returnWithError("Kit layout is not loaded");

    const auto slotIterator = std::find_if(this->activeKitLayoutPtr->instrumentSlots.begin(), this->activeKitLayoutPtr->instrumentSlots.end(), [&slotId](const auto& slot) {
        return slot.id.compareIgnoreCase(slotId) == 0;
    });
    if (slotIterator == this->activeKitLayoutPtr->instrumentSlots.end()) return result.returnWithError("Instrument slot was not found: " + slotId);

    auto* libraryKit = this->libraryManagerPtr->findKitById(libraryKitId);
    if (libraryKit == nullptr) return result.returnWithError("Library kit was not found: " + libraryKitId);
    auto* libraryInstrument = libraryKit->findInstrumentById(libraryInstrumentId);
    if (libraryInstrument == nullptr) return result.returnWithError("Library instrument was not found: " + libraryInstrumentId);

    InstrumentMap map;
    if (currentMapText.trim().isNotEmpty()) {
        auto parsedMap = DrumKitEngine::parseInstrumentMapText(currentMapText);
        if (!parsedMap.ok()) return result.returnWithError(parsedMap.error());
        map = std::move(parsedMap.value);
    }

    const auto preferredLibraryKitId = this->libraryManagerPtr->getPreferredKitId(*libraryKit);
    auto replacementMapLayers = DrumKitEngineMapHelpers::buildInstrumentMapLayers(*slotIterator, *libraryKit, *libraryInstrument, preferredLibraryKitId);
    if (replacementMapLayers.empty()) return result.returnWithError("Library instrument has no articulations: " + libraryInstrumentId);
    DrumKitEngineMapHelpers::replaceSlotMapLayers(map, slotIterator->id, std::move(replacementMapLayers));
    result.value = DrumKitEngine::serializeInstrumentMap(map);
    return result;
}

/** Writes instrument map text in normalized slot/layer order with semicolon-separated params. */
juce::String DrumKitEngine::serializeInstrumentMap(const InstrumentMap& instrumentMap) {
    juce::StringArray lines;

    std::vector<const InstrumentMapLayer*> sortedMapLayers;
    sortedMapLayers.reserve(instrumentMap.layers.size());
    for (const auto& mapLayer : instrumentMap.layers) sortedMapLayers.push_back(&mapLayer);

    std::stable_sort(sortedMapLayers.begin(), sortedMapLayers.end(), [](const auto* leftMapLayer, const auto* rightMapLayer) {
        return DrumKitEngineIdentityMapHelpers::makeMapLayerSortKey(*leftMapLayer) < DrumKitEngineIdentityMapHelpers::makeMapLayerSortKey(*rightMapLayer);
    });

    for (const auto* mapLayerPtr : sortedMapLayers) {
        const auto& mapLayer = *mapLayerPtr;
        if (mapLayer.slotId.trim().isEmpty() || mapLayer.kitRef.trim().isEmpty() || mapLayer.instCode.trim().isEmpty()) continue;
        if (mapLayer.articulationFilter.size() != 1 || mapLayer.articulationFilter.front().trim().isEmpty() || mapLayer.slotArticulationFilter.size() > 1) continue;

        juce::String slotRef = mapLayer.slotId.trim();
        if (!mapLayer.slotArticulationFilter.empty() && mapLayer.slotArticulationFilter.front().trim().isNotEmpty()) slotRef += "/" + mapLayer.slotArticulationFilter.front().trim();

        const auto libraryRef = mapLayer.kitRef.trim() + "/" + mapLayer.instCode.trim() + "/" + mapLayer.articulationFilter.front().trim();

        juce::StringArray params;
        if (mapLayer.tuningCents.has_value()) params.add("tuning=" + juce::String(*mapLayer.tuningCents));
        if (mapLayer.attackTimeMs.has_value()) params.add("attack=" + juce::String(*mapLayer.attackTimeMs));
        if (mapLayer.decayTimeMs.has_value()) params.add("decay=" + juce::String(*mapLayer.decayTimeMs));
        if (mapLayer.volumeDb.has_value()) params.add("vol=" + juce::String(*mapLayer.volumeDb, 4).trimCharactersAtEnd("0").trimCharactersAtEnd("."));
        if (mapLayer.pan.has_value()) params.add("pan=" + juce::String(*mapLayer.pan));
        if (mapLayer.velocityResponse.has_value()) params.add("velocityResponse=" + *mapLayer.velocityResponse);
        if (mapLayer.flipLR || mapLayer.flipLRExplicit) params.add("flipLR=" + juce::String(mapLayer.flipLR ? "1" : "0"));
        if (mapLayer.flipPhase) params.add("flipPhase=1");
        if (!mapLayer.micBleedLevels.empty()) {
            std::vector<juce::String> keys;
            keys.reserve(mapLayer.micBleedLevels.size());
            for (const auto& [key, value] : mapLayer.micBleedLevels) {
                juce::ignoreUnused(value);
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());
            juce::StringArray bleedValues;
            for (const auto& key : keys) bleedValues.add(key + ":" + juce::String(mapLayer.micBleedLevels.at(key), 4).trimCharactersAtEnd("0").trimCharactersAtEnd("."));
            params.add("bleed=" + bleedValues.joinIntoString(","));
        }
        if (!mapLayer.micRouteOverrides.empty()) {
            std::vector<juce::String> keys;
            keys.reserve(mapLayer.micRouteOverrides.size());
            for (const auto& [key, value] : mapLayer.micRouteOverrides) {
                juce::ignoreUnused(value);
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());
            juce::StringArray routeValues;
            for (const auto& key : keys) routeValues.add(key + ":" + mapLayer.micRouteOverrides.at(key));
            params.add("micRoute=" + routeValues.joinIntoString(","));
        }

        auto line = slotRef + ":" + libraryRef;
        if (!params.isEmpty()) line += ":" + params.joinIntoString(";");
        lines.add(line);
    }

    return lines.joinIntoString("\n");
}

void DrumKitEngine::regenerateSuggestedInstrumentMapText() { //TODO: refactor, move to ui related helper, or generate on demand in UI instead of storing in engine
    this->suggestedInstrumentMapText = this->normalizeInstrumentMapText(DrumKitEngineTextView::buildSuggestedInstrumentMapText(this->activeKitLayoutPtr.get(), this->instrumentPlayerById));
}

juce::String DrumKitEngine::normalizeInstrumentMapText(const juce::String& text) const {
    auto parsed = DrumKitEngine::parseInstrumentMapText(text);
    if (!parsed.ok()) return text;
    if (this->libraryManagerPtr != nullptr)
        DrumKitEngineIdentityMapHelpers::normalizeInstrumentMapLayers(parsed.value, *this->libraryManagerPtr);
    return DrumKitEngine::serializeInstrumentMap(parsed.value);
}

void DrumKitEngine::mapKit(const KitLib::DrumKitSpec& kit) { //FIXME: review, probably rewrite, simplify
    if (this->activeKitLayoutPtr == nullptr) {
        LOG(Warning, "no active kit layout to map to");
        return;
    }

    auto librarySnapshot = this->libraryManagerPtr;
    std::unordered_set<KitLib::InstrumentSpec*> usedInstSpecSet;
    std::map<KitLib::InstrumentSpec::Type, int> nextIndexByType;
    

    for (const auto& layoutInstrument : this->activeKitLayoutPtr->instrumentSlots) { //FIXME: rewrite loop thru instrumentPlayerById
        auto sit = this->instrumentPlayerById.find(layoutInstrument.id);
        if (sit == this->instrumentPlayerById.end()) {
            LOG(Warning, "layout instrument slot not present: " + layoutInstrument.id);
            continue;
        }
        InstrumentPlayer* slot = sit->second;

        auto* matched = DrumKitEngineMapHelpers::findMatchingLibraryInstrument(layoutInstrument, kit, usedInstSpecSet, nextIndexByType);

        if (matched == nullptr) {
            LOG(Warning, "no matching instrument found in kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");
            slot->unloadInstrument();
            continue;
        }

        LOG(Info, "Loading instrument '" + matched->meta.codeRaw + "' from kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");

        slot->loadInstrument(matched, librarySnapshot); //TODO: wrap in performance measuring block
    }

    if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
        for (const auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
            instrumentPlayerPtr->setCurrentPlaybackSampleRate(this->lastSampleRate);
            instrumentPlayerPtr->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
        }
    }

    // Start staged sample loading only after all instruments/articulations were enqueued
    if (this->sampleLoaderPtr != nullptr) {
        this->sampleLoaderPtr->getQueue().startProcessing();
    }
}

static juce::String hiHatTransitionCurveToYaml(HiHatTransitionCurve curve) {
    switch (curve) {
        case HiHatTransitionCurve::Linear: return "linear";
        case HiHatTransitionCurve::Smooth: return "smooth";
        case HiHatTransitionCurve::EqualPower: return "equal_power";
    }
    return "equal_power";
}

static juce::String hiHatFootSampleModeToYaml(HiHatFootSampleMode mode) {
    switch (mode) {
        case HiHatFootSampleMode::Off: return "off";
        case HiHatFootSampleMode::CloseOnly: return "close_only";
        case HiHatFootSampleMode::Always: return "always";
    }
    return "close_only";
}

static juce::String hiHatMissingDecayPolicyToYaml(HiHatMissingDecayPolicy policy) {
    switch (policy) {
        case HiHatMissingDecayPolicy::KeepTail: return "keep_tail";
        case HiHatMissingDecayPolicy::SimpleFade: return "simple_fade";
    }
    return "keep_tail";
}

ParseResult<DrumKitEngine::Config> DrumKitEngine::parseConfigYaml(const juce::String yamlStr) {
    ParseResult<DrumKitEngine::Config> result;

    const auto root = YamlHelper::parse(yamlStr);
    if (root.isNull()) {
        return result.returnWithError("Failed to parse YAML: " + yamlStr);
    }

    DrumKitEngine::Config config;

    const auto kitsStr = root["libraryPath"].asString();
    if (kitsStr.isNotEmpty())
        config.libraryPath = resolvePathRelOrAbs(this->dataPath, kitsStr);

    const auto cacheLimit = root["sampleLoaderCacheLimitMegabytes"].asString();
    if (cacheLimit.isNotEmpty())
        config.sampleLoaderCacheLimitMegabytes = static_cast<uint64_t>(juce::uint64(cacheLimit.getLargeIntValue()));

    const auto kitLayout = root["kitLayout"].asString();
    if (kitLayout.isNotEmpty())
        config.kitLayout = kitLayout;

    const auto micLayout = root["micLayout"].asString();
    if (micLayout.isNotEmpty())
        config.micLayout = micLayout;

    const auto velocityResponseDefinition = root["velocityResponse"].asString();
    if (velocityResponseDefinition.isNotEmpty()) {
        auto parsedResponse = VelocityResponse::fromDefinition(velocityResponseDefinition);
        if (parsedResponse.ok()) config.velocityResponse = parsedResponse.value.getDefinition();
        else result.addWarning("Invalid velocityResponse: " + parsedResponse.error() + ". Using linear.");
    }

    const auto logLevel = root["logLevel"].asString();
    if (logLevel.isNotEmpty()) {
        if (isValidDebugLevelString(logLevel))
            config.logLevel = parseDebugLevelOrDefault(logLevel);
        else
            result.addWarning("Unknown logLevel '" + logLevel + "'; using off.");
    }

    auto catalog = InstrumentCatalog::parseYaml(root["instrumentCatalog"]);
    config.instrumentCatalog = catalog.value;
    for (const auto& warning : catalog.errors)
        result.addWarning(warning);

    // Parse engine settings
    const auto& engineNode = root["engine"];
    if (engineNode.isMap()) {
        auto parseFloat = [](const YamlHelper::YamlNode& node, const juce::String& key, RealtimeSetting<float>& target) {
            const auto val = node[key].asString();
            if (val.isNotEmpty()) target.store(val.getFloatValue());
        };
        auto parseInt = [](const YamlHelper::YamlNode& node, const juce::String& key, RealtimeSetting<int>& target) {
            const auto val = node[key].asString();
            if (val.isNotEmpty()) target.store(val.getIntValue());
        };

        const auto& chokeNode = engineNode["aftertouch_choke"];
        if (chokeNode.isMap()) {
            parseFloat(chokeNode, "maxDuckDepth", this->engineSettings.chokeMaxDuckDepth);
            parseFloat(chokeNode, "duckTimeSec", this->engineSettings.chokeDuckTimeSec);
            parseFloat(chokeNode, "releaseTimeSec", this->engineSettings.chokeReleaseTimeSec);
            parseFloat(chokeNode, "muteTailSec", this->engineSettings.chokeMuteTailSec);
            parseFloat(chokeNode, "releaseBoost", this->engineSettings.chokeReleaseBoost);
            parseFloat(chokeNode, "muteThresholdRatio", this->engineSettings.chokeMuteThresholdRatio);
            const auto minPressure = chokeNode["minPressure"].asString();
            if (minPressure.isNotEmpty())
                this->engineSettings.polyAftertouchMinPressure.store(juce::jlimit(1, 20, minPressure.getIntValue()));
        }

        const auto& crosstalkNode = engineNode["midi_crosstalk"];
        if (crosstalkNode.isMap()) {
            const auto windowMs = crosstalkNode["windowMs"].asString();
            if (windowMs.isNotEmpty())
                this->engineSettings.crosstalkWindowMs.store(juce::jlimit(5, 100, windowMs.getIntValue()));

            this->crosstalkProfile.clear();
            const auto& profileNode = crosstalkNode["profile"];
            if (profileNode.isSequence()) {
                for (size_t idx = 0; idx < profileNode.size(); ++idx) {
                    const auto& entry = profileNode[idx];
                    this->crosstalkProfile.setThreshold(entry["primary"].asInt(), entry["secondary"].asInt(), entry["ratio"].asInt());
                }
            }
        }

        const auto& tailNode = engineNode["tail_shortening"];
        if (tailNode.isMap()) {
            parseFloat(tailNode, "loadThreshold", this->engineSettings.tailShortenLoadThreshold);
            parseFloat(tailNode, "ageMultiplier", this->engineSettings.tailShortenAgeMultiplier);
            parseFloat(tailNode, "ampProtect", this->engineSettings.tailShortenAmpProtect);
            parseFloat(tailNode, "minFactor", this->engineSettings.tailShortenMinFactor);
            parseFloat(tailNode, "voiceMinClamp", this->engineSettings.voiceTailShortenMinClamp);
            parseFloat(tailNode, "cullThresholdDb", this->engineSettings.voiceCullThresholdDb);
        }

        const auto& hihatNode = engineNode["hihat"];
        if (hihatNode.isMap()) {
            parseFloat(hihatNode, "polyphonyCrossfadeTimeoutSec", this->engineSettings.polyphonyCrossfadeTimeoutSec);

            const auto hitChokeDbMargin = hihatNode["hitChokeDbMargin"].asString();
            const auto legacyHitChokeDbMargin = hihatNode["crossArtChokeDbMargin"].asString();
            if (hitChokeDbMargin.isNotEmpty())
                this->engineSettings.hihatHitChokeDbMargin.store(juce::jlimit(0.0f, 20.0f, hitChokeDbMargin.getFloatValue()));
            else if (legacyHitChokeDbMargin.isNotEmpty())
                this->engineSettings.hihatHitChokeDbMargin.store(juce::jlimit(0.0f, 20.0f, legacyHitChokeDbMargin.getFloatValue()));

            const auto& transitionNode = hihatNode["transition"];
            const auto legacyCrossfade = hihatNode["pedalCrossfadeSec"].asString();
            const auto legacyMinRemaining = hihatNode["pedalMinRemainingSec"].asString();

            if (transitionNode.isMap()) {
                auto parseClampedFloat = [&transitionNode](const juce::String& key,
                                                          RealtimeSetting<float>& target,
                                                          float minimum,
                                                          float maximum) {
                    const auto value = transitionNode[key].asString();
                    if (value.isNotEmpty())
                        target.store(juce::jlimit(minimum, maximum, value.getFloatValue()));
                };
                auto parseClampedInt = [&transitionNode](const juce::String& key,
                                                        RealtimeSetting<int>& target,
                                                        int minimum,
                                                        int maximum) {
                    const auto value = transitionNode[key].asString();
                    if (value.isNotEmpty())
                        target.store(juce::jlimit(minimum, maximum, value.getIntValue()));
                };

                parseClampedFloat("openCrossfadeSec", this->engineSettings.hihatOpenCrossfadeSec, 0.005f, 0.250f);
                parseClampedFloat("closeCrossfadeSec", this->engineSettings.hihatCloseCrossfadeSec, 0.002f, 0.150f);
                parseClampedFloat("controllerSmoothingSec", this->engineSettings.hihatControllerSmoothingSec, 0.0f, 0.100f);
                parseClampedInt("boundaryHysteresisCc", this->engineSettings.hihatBoundaryHysteresisCc, 0, 8);
                parseClampedFloat("tailMatchAmount", this->engineSettings.hihatTailMatchAmount, 0.0f, 1.0f);
                parseClampedFloat("tailMatchBiasDb", this->engineSettings.hihatTailMatchBiasDb, -12.0f, 6.0f);
                parseClampedFloat("attackGuardSec", this->engineSettings.hihatTransitionAttackGuardSec, 0.0f, 0.100f);
                parseClampedInt("maxActiveGroups", this->engineSettings.hihatTransitionMaxActiveGroups, 1, 4);
                parseClampedFloat("minRemainingSec", this->engineSettings.hihatTransitionMinRemainingSec, 0.010f, 0.500f);
                parseClampedFloat("silenceFloorDb", this->engineSettings.hihatTransitionSilenceFloorDb, -90.0f, -36.0f);

                const auto curve = transitionNode["curve"].asString().trim().toLowerCase();
                if (curve == "linear")
                    this->engineSettings.hihatTransitionCurve.store(HiHatTransitionCurve::Linear);
                else if (curve == "smooth")
                    this->engineSettings.hihatTransitionCurve.store(HiHatTransitionCurve::Smooth);
                else if (curve == "equal_power")
                    this->engineSettings.hihatTransitionCurve.store(HiHatTransitionCurve::EqualPower);
                else if (curve.isNotEmpty()) {
                    LOG(Warning, "Unknown hi-hat transition curve '" + curve + "'; using equal_power.");
                    this->engineSettings.hihatTransitionCurve.store(HiHatTransitionCurve::EqualPower);
                }

                const auto footSampleMode = transitionNode["footSampleMode"].asString().trim().toLowerCase();
                if (footSampleMode == "off")
                    this->engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
                else if (footSampleMode == "close_only")
                    this->engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);
                else if (footSampleMode == "always")
                    this->engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Always);
                else if (footSampleMode.isNotEmpty()) {
                    LOG(Warning, "Unknown hi-hat foot sample mode '" + footSampleMode + "'; using close_only.");
                    this->engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);
                }

                const auto missingDecayPolicy = transitionNode["missingDecayPolicy"].asString().trim().toLowerCase();
                if (missingDecayPolicy == "keep_tail")
                    this->engineSettings.hihatMissingDecayPolicy.store(HiHatMissingDecayPolicy::KeepTail);
                else if (missingDecayPolicy == "simple_fade")
                    this->engineSettings.hihatMissingDecayPolicy.store(HiHatMissingDecayPolicy::SimpleFade);
                else if (missingDecayPolicy.isNotEmpty()) {
                    LOG(Warning, "Unknown hi-hat missing decay policy '" + missingDecayPolicy + "'; using keep_tail.");
                    this->engineSettings.hihatMissingDecayPolicy.store(HiHatMissingDecayPolicy::KeepTail);
                }
            }

            if (legacyCrossfade.isNotEmpty()) {
                const float legacyCrossfadeSec = legacyCrossfade.getFloatValue();
                if (!transitionNode.isMap() || transitionNode["openCrossfadeSec"].asString().isEmpty())
                    this->engineSettings.hihatOpenCrossfadeSec.store(juce::jlimit(0.005f, 0.250f, legacyCrossfadeSec));
                if (!transitionNode.isMap() || transitionNode["closeCrossfadeSec"].asString().isEmpty())
                    this->engineSettings.hihatCloseCrossfadeSec.store(juce::jlimit(0.002f, 0.150f, juce::jmin(legacyCrossfadeSec, 0.030f)));
            }

            if (legacyMinRemaining.isNotEmpty()
                && (!transitionNode.isMap() || transitionNode["minRemainingSec"].asString().isEmpty())) {
                this->engineSettings.hihatTransitionMinRemainingSec.store(
                    juce::jlimit(0.010f, 0.500f, legacyMinRemaining.getFloatValue()));
            }
        }

        const auto& uiGridNode = engineNode["ui_grid"];
        if (uiGridNode.isMap()) {
            parseInt(uiGridNode, "padGridColumns", this->engineSettings.padGridColumns);
            parseInt(uiGridNode, "padGridRows", this->engineSettings.padGridRows);
            parseInt(uiGridNode, "padGap", this->engineSettings.padGap);
        }
    }

    result.value = config;
    return result;
}

juce::String DrumKitEngine::getDefaultYaml() {
    juce::String result = "";
    result += "kitLayout: default\n";
    result += "micLayout: default\n";
    result += "sampleLoaderCacheLimitMegabytes: 1024\n";
    result += "velocityResponse: linear\n";
    result += "logLevel: off\n";
    const juce::File libDir = this->dataPath.getChildFile("sample_library");
    if (libDir.exists())
        result += "libraryPath: " + libDir.getFullPathName() + "\n";
    result += "\n";
    result += "# engine:\n";
    result += "#   aftertouch_choke:\n";
    result += "#     maxDuckDepth: 0.7\n";
    result += "#     duckTimeSec: 0.15\n";
    result += "#     releaseTimeSec: 0.05\n";
    result += "#     muteTailSec: 0.04\n";
    result += "#     releaseBoost: 0.1\n";
    result += "#     muteThresholdRatio: 0.5\n";
    result += "#     minPressure: 10\n";
    result += "#   midi_crosstalk:\n";
    result += "#     windowMs: 40\n";
    result += "#     profile: []\n";
    result += "#   tail_shortening:\n";
    result += "#     loadThreshold: 0.5\n";
    result += "#     ageMultiplier: 0.02\n";
    result += "#     ampProtect: 0.6\n";
    result += "#     minFactor: 0.3\n";
    result += "#     voiceMinClamp: 0.1\n";
    result += "#     cullThresholdDb: 12.0\n";
    result += "#   hihat:\n";
    result += "#     polyphonyCrossfadeTimeoutSec: 0.3\n";
    result += "#     hitChokeDbMargin: 3.0\n";
    result += "#     transition:\n";
    result += "#       openCrossfadeSec: 0.06\n";
    result += "#       closeCrossfadeSec: 0.02\n";
    result += "#       controllerSmoothingSec: 0.015\n";
    result += "#       boundaryHysteresisCc: 2\n";
    result += "#       tailMatchAmount: 0.75\n";
    result += "#       tailMatchBiasDb: -3.0\n";
    result += "#       attackGuardSec: 0.025\n";
    result += "#       maxActiveGroups: 1\n";
    result += "#       curve: equal_power\n";
    result += "#       footSampleMode: close_only\n";
    result += "#       minRemainingSec: 0.05\n";
    result += "#       silenceFloorDb: -72.0\n";
    result += "#       missingDecayPolicy: keep_tail\n";
    result += "#   ui_grid:\n";
    result += "#     padGridColumns: 0  # auto\n";
    result += "#     padGridRows: 0     # auto\n";
    result += "#     padGap: 0\n";
    return result;
}

static juce::String floatToYaml(float value) {
    // Use enough precision but avoid trailing zeros
    return juce::String(value, 4).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
}

juce::String DrumKitEngine::serializeSettingsYaml() const {
    juce::String yaml;
    yaml += "kitLayout: " + this->globalConfig.kitLayout + "\n";
    yaml += "micLayout: " + this->globalConfig.micLayout + "\n";
    yaml += "sampleLoaderCacheLimitMegabytes: " + juce::String(this->globalConfig.sampleLoaderCacheLimitMegabytes) + "\n";
    yaml += "velocityResponse: \"" + this->globalConfig.velocityResponse + "\"\n";
    yaml += "logLevel: " + DebugLevelToYaml(this->globalConfig.logLevel) + "\n";
    if (this->globalConfig.libraryPath.exists())
        yaml += "libraryPath: " + this->globalConfig.libraryPath.getFullPathName() + "\n";

    const auto& settings = this->engineSettings;
    yaml += "\nengine:\n";
    yaml += "  aftertouch_choke:\n";
    yaml += "    maxDuckDepth: " + floatToYaml(settings.chokeMaxDuckDepth.load()) + "\n";
    yaml += "    duckTimeSec: " + floatToYaml(settings.chokeDuckTimeSec.load()) + "\n";
    yaml += "    releaseTimeSec: " + floatToYaml(settings.chokeReleaseTimeSec.load()) + "\n";
    yaml += "    muteTailSec: " + floatToYaml(settings.chokeMuteTailSec.load()) + "\n";
    yaml += "    releaseBoost: " + floatToYaml(settings.chokeReleaseBoost.load()) + "\n";
    yaml += "    muteThresholdRatio: " + floatToYaml(settings.chokeMuteThresholdRatio.load()) + "\n";
    yaml += "    minPressure: " + juce::String(settings.polyAftertouchMinPressure.load()) + "\n";
    yaml += "  midi_crosstalk:\n";
    yaml += "    windowMs: " + juce::String(settings.crosstalkWindowMs.load()) + "\n";
    yaml += "    profile:\n";
    for (int primaryNote = 0; primaryNote < 128; ++primaryNote) {
        for (int secondaryNote = 0; secondaryNote < 128; ++secondaryNote) {
            const int ratio = this->crosstalkProfile.getThreshold(primaryNote, secondaryNote);
            if (ratio > 0)
                yaml += "      - { primary: " + juce::String(primaryNote) + ", secondary: " + juce::String(secondaryNote) + ", ratio: " + juce::String(ratio) + " }\n";
        }
    }
    yaml += "  tail_shortening:\n";
    yaml += "    loadThreshold: " + floatToYaml(settings.tailShortenLoadThreshold.load()) + "\n";
    yaml += "    ageMultiplier: " + floatToYaml(settings.tailShortenAgeMultiplier.load()) + "\n";
    yaml += "    ampProtect: " + floatToYaml(settings.tailShortenAmpProtect.load()) + "\n";
    yaml += "    minFactor: " + floatToYaml(settings.tailShortenMinFactor.load()) + "\n";
    yaml += "    voiceMinClamp: " + floatToYaml(settings.voiceTailShortenMinClamp.load()) + "\n";
    yaml += "    cullThresholdDb: " + floatToYaml(settings.voiceCullThresholdDb.load()) + "\n";
    yaml += "  hihat:\n";
    yaml += "    polyphonyCrossfadeTimeoutSec: " + floatToYaml(settings.polyphonyCrossfadeTimeoutSec.load()) + "\n";
    yaml += "    hitChokeDbMargin: " + floatToYaml(settings.hihatHitChokeDbMargin.load()) + "\n";
    yaml += "    transition:\n";
    yaml += "      openCrossfadeSec: " + floatToYaml(settings.hihatOpenCrossfadeSec.load()) + "\n";
    yaml += "      closeCrossfadeSec: " + floatToYaml(settings.hihatCloseCrossfadeSec.load()) + "\n";
    yaml += "      controllerSmoothingSec: " + floatToYaml(settings.hihatControllerSmoothingSec.load()) + "\n";
    yaml += "      boundaryHysteresisCc: " + juce::String(settings.hihatBoundaryHysteresisCc.load()) + "\n";
    yaml += "      tailMatchAmount: " + floatToYaml(settings.hihatTailMatchAmount.load()) + "\n";
    yaml += "      tailMatchBiasDb: " + floatToYaml(settings.hihatTailMatchBiasDb.load()) + "\n";
    yaml += "      attackGuardSec: " + floatToYaml(settings.hihatTransitionAttackGuardSec.load()) + "\n";
    yaml += "      maxActiveGroups: " + juce::String(settings.hihatTransitionMaxActiveGroups.load()) + "\n";
    yaml += "      curve: " + hiHatTransitionCurveToYaml(settings.hihatTransitionCurve.load()) + "\n";
    yaml += "      footSampleMode: " + hiHatFootSampleModeToYaml(settings.hihatFootSampleMode.load()) + "\n";
    yaml += "      minRemainingSec: " + floatToYaml(settings.hihatTransitionMinRemainingSec.load()) + "\n";
    yaml += "      silenceFloorDb: " + floatToYaml(settings.hihatTransitionSilenceFloorDb.load()) + "\n";
    yaml += "      missingDecayPolicy: " + hiHatMissingDecayPolicyToYaml(settings.hihatMissingDecayPolicy.load()) + "\n";
    yaml += "  ui_grid:\n";
    yaml += "    padGridColumns: " + juce::String(settings.padGridColumns.load()) + "\n";
    yaml += "    padGridRows: " + juce::String(settings.padGridRows.load()) + "\n";
    yaml += "    padGap: " + juce::String(settings.padGap.load()) + "\n";
    return yaml;
}

void DrumKitEngine::saveSettings() {
    const juce::File configFile = this->dataPath.getChildFile("config.yaml");
    const auto yaml = this->serializeSettingsYaml();

    if (!configFile.replaceWithText(yaml)) {
        LOG(Warning, "Could not write config.yaml");
    }
}

bool DrumKitEngine::applyKitLayout() {
    if (this->activeKitLayoutPtr == nullptr) {
        LOG(Warning, "kit layout was not set");
        return false;
    }

    struct ExistingPlayerState {
        std::vector<InstrumentPlayer*> rawPlayers;
        std::unordered_map<juce::String, int> primaryIndexById;
        std::unordered_map<juce::String, std::vector<int>> shadowIndicesBySlotId;
    } existingState;

    {
        const juce::ScopedLock graphLock(this->engineGraphLock);
        existingState.rawPlayers.reserve(this->instrumentPlayerPtrs.size());
        for (int idx = 0; idx < static_cast<int>(this->instrumentPlayerPtrs.size()); ++idx) {
            auto* instrumentPlayer = this->instrumentPlayerPtrs[static_cast<size_t>(idx)].get();
            existingState.rawPlayers.push_back(instrumentPlayer);
            if (idx < this->primaryInstrumentPlayerCount)
                existingState.primaryIndexById[instrumentPlayer->getId()] = idx;
            else
                existingState.shadowIndicesBySlotId[instrumentPlayer->getId()].push_back(idx);
        }
    }

    struct PreparedShadow {
        int oldIndex = -1;
        std::unique_ptr<InstrumentPlayer> replacement;
    };

    struct PreparedSlot {
        KitLayout::InstrumentSlot layoutSlot;
        juce::String slotId;
        int oldPrimaryIndex = -1;
        std::unique_ptr<InstrumentPlayer> primaryReplacement;
        std::vector<PreparedShadow> shadows;
    };

    std::vector<PreparedSlot> preparedSlots;
    preparedSlots.reserve(this->activeKitLayoutPtr->instrumentSlots.size());

    for (const auto& layoutSlot : this->activeKitLayoutPtr->instrumentSlots) {
        PreparedSlot prepared;
        prepared.layoutSlot = layoutSlot;
        prepared.slotId = layoutSlot.id;

        auto primaryIt = existingState.primaryIndexById.find(layoutSlot.id);
        if (primaryIt != existingState.primaryIndexById.end()) {
            prepared.oldPrimaryIndex = primaryIt->second;
            auto* oldPrimary = existingState.rawPlayers[static_cast<size_t>(prepared.oldPrimaryIndex)];
            if (!DrumKitEngineMapHelpers::canReuseSlotWithoutReload(oldPrimary->getInstrumentSlot(), layoutSlot))
                prepared.primaryReplacement = DrumKitEngineMapHelpers::createReplacementPlayer(this, layoutSlot, *oldPrimary, this->lastSampleRate, this->lastSamplesPerBlock);

            auto shadowIt = existingState.shadowIndicesBySlotId.find(layoutSlot.id);
            if (shadowIt != existingState.shadowIndicesBySlotId.end()) {
                prepared.shadows.reserve(shadowIt->second.size());
                for (const int shadowIndex : shadowIt->second) {
                    PreparedShadow shadow;
                    shadow.oldIndex = shadowIndex;

                    auto* oldShadow = existingState.rawPlayers[static_cast<size_t>(shadowIndex)];
                    if (!DrumKitEngineMapHelpers::canReuseSlotWithoutReload(oldShadow->getInstrumentSlot(), layoutSlot))
                        shadow.replacement = DrumKitEngineMapHelpers::createReplacementPlayer(this, layoutSlot, *oldShadow, this->lastSampleRate, this->lastSamplesPerBlock);

                    prepared.shadows.push_back(std::move(shadow));
                }
            }
        } else {
            prepared.primaryReplacement = std::make_unique<InstrumentPlayer>(this, layoutSlot);
            DrumKitEngineMapHelpers::preparePlayerIfNeeded(*prepared.primaryReplacement, this->lastSampleRate, this->lastSamplesPerBlock);
        }

        preparedSlots.push_back(std::move(prepared));
    }

    std::vector<std::unique_ptr<InstrumentPlayer>> oldPlayers;
    {
        const juce::ScopedLock graphLock(this->engineGraphLock);
        auto previousPlayers = std::move(this->instrumentPlayerPtrs);

        this->instrumentPlayerPtrs.clear();
        this->instrumentPlayerById.clear();
        this->primaryInstrumentPlayerCount = 0;

        for (auto& prepared : preparedSlots) {
            if (prepared.oldPrimaryIndex >= 0 && !prepared.primaryReplacement) {
                this->instrumentPlayerPtrs.push_back(std::move(previousPlayers[static_cast<size_t>(prepared.oldPrimaryIndex)]));
            } else {
                this->instrumentPlayerPtrs.push_back(std::move(prepared.primaryReplacement));
            }

            auto* inserted = this->instrumentPlayerPtrs.back().get();
            this->instrumentPlayerById[prepared.slotId] = inserted;
        }

        this->primaryInstrumentPlayerCount = static_cast<int>(this->instrumentPlayerPtrs.size());
        this->clearShadowPlayers();

        for (auto& prepared : preparedSlots) {
            for (auto& shadow : prepared.shadows) {
                if (shadow.oldIndex >= 0 && !shadow.replacement) {
                    this->instrumentPlayerPtrs.push_back(std::move(previousPlayers[static_cast<size_t>(shadow.oldIndex)]));
                } else if (shadow.replacement) {
                    this->instrumentPlayerPtrs.push_back(std::move(shadow.replacement));
                }

                auto* shadowPlayer = this->instrumentPlayerPtrs.back().get();
                
                this->addShadowPlayerNoteRoutes(*shadowPlayer, prepared.layoutSlot);
            }
        }

        std::unordered_set<juce::String> activeSlotIds;
        activeSlotIds.reserve(preparedSlots.size());
        for (const auto& prepared : preparedSlots)
            activeSlotIds.insert(prepared.slotId);

        for (auto it = this->lastAppliedSlotFingerprints.begin(); it != this->lastAppliedSlotFingerprints.end();) {
            if (it->first != "*" && activeSlotIds.find(it->first) == activeSlotIds.end())
                it = this->lastAppliedSlotFingerprints.erase(it);
            else
                ++it;
        }

        for (auto reloadIt = this->lastAppliedSlotReloadFingerprints.begin(); reloadIt != this->lastAppliedSlotReloadFingerprints.end();) {
            if (reloadIt->first != "*" && activeSlotIds.find(reloadIt->first) == activeSlotIds.end())
                reloadIt = this->lastAppliedSlotReloadFingerprints.erase(reloadIt);
            else
                ++reloadIt;
        }

        if (this->activeMicLayoutPtr != nullptr)
            this->applyMicLayoutUnlocked();

        for (auto& instrumentPlayer : previousPlayers) {
            if (instrumentPlayer)
                oldPlayers.push_back(std::move(instrumentPlayer));
        }
    }

    LOG(Info, "Applied kit layout: " + this->activeKitLayoutPtr->id
        + " (primary slots: " + juce::String(this->primaryInstrumentPlayerCount)
        + ", recycled instrument players: " + juce::String(static_cast<int>(existingState.rawPlayers.size()) - static_cast<int>(oldPlayers.size()))
        + ", retired instrument players: " + juce::String(static_cast<int>(oldPlayers.size())) + ")");

    this->buildNoteNameCache();
    return true;
}

void DrumKitEngine::rebuildPrimaryInstrumentNoteRouting() {
    this->instrumentPlayersByNote.clear();
    this->trackedCCMask.fill(false);
    this->latestPAValues.fill(0);
    this->trackedPAMask.fill(false);

    if (this->activeKitLayoutPtr == nullptr)
        return;

    for (const auto& instrumentSlot : this->activeKitLayoutPtr->instrumentSlots) {
        auto it = this->instrumentPlayerById.find(instrumentSlot.id);
        if (it == this->instrumentPlayerById.end())
            continue;

        auto* instrumentPlayer = it->second;

        // Rebuild the instrument player's internal note/CC routing from current layout data.
        instrumentPlayer->rebuildRoutingFromLayout(instrumentSlot);

        for (const auto& cc : instrumentSlot.controllers) {
            if (cc.number >= 0 && cc.number < 128)
                this->trackedCCMask[static_cast<size_t>(cc.number)] = true;
        }

        for (const auto& art : instrumentSlot.articulations) {
            const bool routesArt = DrumKitEngine::playerRoutesArticulationSlot(*instrumentPlayer, art);
            for (const int note : art.notes) {
                if (note < 0 || note >= 128)
                    continue;
                this->trackedPAMask[static_cast<size_t>(note)] = true;
                if (routesArt) this->instrumentPlayersByNote[note].push_back(instrumentPlayer);
            }
        }
    }
}

void DrumKitEngine::clearShadowPlayers() {
    if (static_cast<int>(this->instrumentPlayerPtrs.size()) > this->primaryInstrumentPlayerCount)
        this->instrumentPlayerPtrs.erase(this->instrumentPlayerPtrs.begin() + this->primaryInstrumentPlayerCount, this->instrumentPlayerPtrs.end());

    this->lastAppliedShadowFingerprints.clear();
    this->lastAppliedShadowReloadFingerprints.clear();
    this->rebuildPrimaryInstrumentNoteRouting();
}

void DrumKitEngine::rebuildMidiNoteRoutingFromLayout() {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    this->rebuildPrimaryInstrumentNoteRouting();

    if (this->activeKitLayoutPtr != nullptr) {
        for (int idx = this->primaryInstrumentPlayerCount; idx < static_cast<int>(this->instrumentPlayerPtrs.size()); ++idx) {
            auto* shadowPlayer = this->instrumentPlayerPtrs[static_cast<size_t>(idx)].get();
            if (shadowPlayer == nullptr) continue;

            const KitLayout::InstrumentSlot* layoutSlot = nullptr;
            for (const auto& slot : this->activeKitLayoutPtr->instrumentSlots) {
                if (slot.id == shadowPlayer->getId()) {
                    layoutSlot = &slot;
                    break;
                }
            }
            if (layoutSlot == nullptr) continue;

            this->addShadowPlayerNoteRoutes(*shadowPlayer, *layoutSlot);
        }
    }

    this->buildNoteNameCache();
}

void DrumKitEngine::addShadowPlayerNoteRoutes(InstrumentPlayer& shadowPlayer, const KitLayout::InstrumentSlot& layoutSlot) {
    shadowPlayer.rebuildRoutingFromLayout(layoutSlot);
    for (const auto& art : layoutSlot.articulations) {
        if (!DrumKitEngine::playerRoutesArticulationSlot(shadowPlayer, art)) continue;
        for (const int note : art.notes) {
            if (note >= 0 && note < 128) this->instrumentPlayersByNote[note].push_back(&shadowPlayer);
        }
    }
}

void DrumKitEngine::buildNoteNameCache() {
    for (size_t noteIdx = 0; noteIdx < this->noteNameCache.size(); ++noteIdx) this->noteNameCache[noteIdx].clear();

    if (this->activeKitLayoutPtr == nullptr) return;

    auto& layout = *this->activeKitLayoutPtr;
    auto& dict = StringsDictSingleton::instance();

    for (const auto& inst : layout.instrumentSlots) {
        juce::String instName = inst.display.isNotEmpty() ? inst.display : inst.id;
        for (const auto& art : inst.articulations) {

            juce::String artName = dict.findName(art.nameId);
            if (artName.isEmpty()) artName = art.codeRaw;

            juce::String finalName = instName;
            if (artName.isNotEmpty()) finalName += " " + artName;
            
            for (uint8_t note : art.notes) {
                if (note < 0 || note > 127) continue;
                
                if (this->noteNameCache[note].isEmpty())
                    this->noteNameCache[note] = finalName + " (" + juce::String(note) + ")";
            }
        }
    }
}

bool DrumKitEngine::applyMicLayout() {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    return this->applyMicLayoutUnlocked();
}

bool DrumKitEngine::applyMicLayoutUnlocked() {
    if (this->activeMicLayoutPtr == nullptr) {
        LOG(Warning, "mic layout was not set");
        return false;
    }

    if (this->instrumentPlayerPtrs.size() == 0) {
        LOG(Warning, "unable to apply mic layout: no instrument slots was defined in kit layout");
        return false;
    }
    // mic layout is for a mapping of each SampleSpec mic (MicSpec) of InstrumentPlayer to coresponding output channel of the plugin
    // the map (sequence and other params defined in MicSpec) of channels is defined in micLayout.
    //  MicSpec to determine which of plugin and routing 
    


    // For each instrument slot, calculate how many mic channels it should have
    // for (size_t idx = 0; idx < this->instrumentPlayerPtrs.size(); ++idx) {
    //     InstrumentPlayer* slotPtr = this->instrumentPlayerPtrs[idx].get();
    //     int totalChannels = 0;
    //     const auto slotId = slotPtr->getId();
    //     for (const MicLayout::Mic& mic : this->activeMicLayoutPtr->mics) {
    //         // match mic entries that list this slot id in their instrument field
    //         if (mic.instrument == slotId) {
    //             // parse mic code; try to infer channel count
    //             auto prRes = KitLib::MicSpec::parseMicCode(mic.code);
    //             if (prRes.ok()) {
    //                 totalChannels += prRes.value.channels;
    //             } else {
    //                 totalChannels += 1; //FIXME: fallback mono? review, maybe take something from file
    //             }
    //         }
    //     }

    //     if (totalChannels <= 0)
    //         totalChannels = 2; //FIXME: default to stereo? review, maybe take something from file

    //     slotPtr->setMicChannelCount(totalChannels);
    // }

    // Build an immutable routing-plan snapshot for this kit/mic layout. Stage 3 direct
    // allocations are populated by the pure builder when library direct mic requests are
    // provided; this layout-level snapshot still defines normal mic order.
    std::vector<MicRoutingPlan::SlotRequest> planSlotRequests;
    if (this->activeKitLayoutPtr != nullptr) {
        planSlotRequests.reserve(this->activeKitLayoutPtr->instrumentSlots.size());
        for (const auto& slot : this->activeKitLayoutPtr->instrumentSlots) {
            MicRoutingPlan::SlotRequest request;
            request.slotId = slot.id;
            request.slotType = slot.type;
            request.directPolicy = slot.directMicOuts.value_or(
                this->globalConfig.instrumentCatalog.defaultDirectMicOutsFor(slot.type).value_or(DirectMicOutPolicy::Shared));
            planSlotRequests.push_back(std::move(request));
        }
    }
    this->activeMicRoutingPlanPtr = std::make_unique<MicRoutingPlan>(
        MicRoutingPlan::build(*this->activeMicLayoutPtr, planSlotRequests, this->globalConfig.instrumentCatalog));

    // Build per-instrument mapping using expected host channel count (defined by plugin outputs)
    // All output buses are mono; stereo mics occupy 2 consecutive mono channels
    const int expectedHostChannels = TOTAL_HOST_OUTPUT_CHANNELS;

    // Initialize an empty mapping for each instrument player.
    for (const auto& instPtr : this->instrumentPlayerPtrs) {
        instPtr->setMicOutputMapping({});
    }

    int outCursor = 0;
    for (const auto& mic : this->activeMicLayoutPtr->mics) {
        int channelCount = mic.specs.channels;
        if (channelCount <= 0) channelCount = 1;

        if (outCursor + channelCount > expectedHostChannels) {
            LOG(Warning, "Not enough host output channels to assign mic '" + mic.name + "' with " + juce::String(channelCount) + " channels. Skipping remaining mics.");
            break;
        }

        // Determine target instrument players. //FIXME: move to loadInstrument?
        std::vector<InstrumentPlayer*> targets;
        const auto selector = parseMicInstrumentSelector(mic.instrument, this->globalConfig.instrumentCatalog);
        for (const auto& isp : this->instrumentPlayerPtrs) {
            if (matchMicInstrumentSelector(selector, isp->getId(), isp->getType(), this->globalConfig.instrumentCatalog) != MicSelectorPriority::None)
                targets.push_back(isp.get());
        }

        if (targets.empty()) {
            LOG(Warning,  "Assigning mic " + mic.name + " (" + juce::String(mic.specs.channels) + "ch)" + " for instrument '" + mic.instrument + "' FAILED: no coresponding instrument slots found in your kit layout");
            outCursor += channelCount; // still reserve host channels
            continue;
        } else {
            juce::StringArray targetSlotIds;
            for (const auto* targetPlayer : targets) targetSlotIds.addIfNotAlreadyThere(targetPlayer->getId());
            LOG(Info, "Mic output available: name='" + mic.name + "' route=" + mic.id + " channels=" + juce::String(channelCount) + " slots=[" + targetSlotIds.joinIntoString(",") + "]");
        }

        // Host channel indices for this mic - directly maps to mono bus indices
        std::vector<int> hostIdx;
        for (int i = 0; i < channelCount; ++i) hostIdx.push_back(outCursor + i);

        // Append host indices to each target instrument's mapping
        for (auto* t : targets) {
            auto existing = t->getMicOutputMapping();
            std::vector<int> merged = existing;
            merged.insert(merged.end(), hostIdx.begin(), hostIdx.end());
            t->setMicOutputMapping(merged);
        }

        outCursor += channelCount; // advance by actual mic channel count
    }

    // Apply mapped mic channel counts to instrument players (used for informational purposes).
    for (const auto& instPtr : this->instrumentPlayerPtrs) {
        const auto& map = instPtr->getMicOutputMapping();
        int totalAssigned = static_cast<int>(map.size());
        if (totalAssigned <= 0) totalAssigned = 2;
        instPtr->setMicChannelCount(totalAssigned);
    }

    for (const auto& instPtr : this->instrumentPlayerPtrs) {
        auto primaryIt = this->instrumentPlayerById.find(instPtr->getId());
        if (primaryIt == this->instrumentPlayerById.end())
            continue;

        auto* primary = primaryIt->second;
        if (primary == instPtr.get())
            continue;

        instPtr->setMicOutputMapping(primary->getMicOutputMapping());
        instPtr->setMicChannelCount(primary->getMicChannelCount());
    }

    // Detailed mapping log per instrument
    for (const auto& instPtr : this->instrumentPlayerPtrs) {
        juce::String mapStr = "";
        const auto& map = instPtr->getMicOutputMapping();
        for (size_t i = 0; i < map.size(); ++i) {
            if (i) mapStr += ",";
            mapStr += juce::String(map[i] + 1); // convert to 1-based for user-friendly display
        }
        if (mapStr.isEmpty()) mapStr = "(none)";
        LOG(Debug, "Mic mapping " + mapStr + " (" + juce::String(instPtr->getMicChannelCount()) + " ch) used for instrument '" + instPtr->getId() + "'");
    }

    LOG(Info, "Applied mic layout: " + this->activeMicLayoutPtr->id + " (assigned host channels: " + juce::String(outCursor) + ")");
    return true;
}

void DrumKitEngine::prepareToPlay(double sampleRate, int samplesPerBlock) {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    this->lastSampleRate = sampleRate;
    this->lastSamplesPerBlock = samplesPerBlock;
    this->crosstalkFilter.prepare(sampleRate, juce::jmax(1024, samplesPerBlock * 8));
    for (const auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
        instrumentPlayerPtr->setCurrentPlaybackSampleRate(sampleRate);
        instrumentPlayerPtr->prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void DrumKitEngine::processBlock(juce::AudioBuffer<float>& outputBuffer, juce::MidiBuffer& midiMessages) { //TODO: review
    const juce::ScopedTryLock graphLock(this->engineGraphLock);
    if (!graphLock.isLocked()) {
        outputBuffer.clear();
        return;
    }

    const auto outputBufferStartSample = 0;
    const auto outputBufferNumSamples = outputBuffer.getNumSamples();
    
    this->saveLatestControllerValues(midiMessages);
    this->crosstalkFilter.process(midiMessages, this->crosstalkProfile, this->engineSettings.crosstalkWindowMs.load(), outputBufferNumSamples);

    // Each instrument's SampledSound now stores *global* host channel indices in
    // micBufferChannelStart, so voices can render directly into outputBuffer.
    // The per-instrument micBuffer intermediate and local-to-global copy are no longer needed.
    for (const auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
        instrumentPlayerPtr->renderNextBlock(outputBuffer, midiMessages, outputBufferStartSample, outputBufferNumSamples);
    }

    // Mix one-shot preview playback into the first two output channels
    int readPos = this->previewReadPos.load();
    if (readPos >= 0) {
        const juce::SpinLock::ScopedLockType previewScopedLock(this->previewLock);
        auto buf = this->previewBuffer;
        if (buf != nullptr && readPos < buf->getNumSamples()) {
            int samplesToMix = juce::jmin(outputBufferNumSamples, buf->getNumSamples() - readPos);
            int srcChannels = buf->getNumChannels();
            int dstChannels = outputBuffer.getNumChannels();
            for (int ch = 0; ch < juce::jmin(srcChannels, dstChannels, 2); ++ch) {
                outputBuffer.addFrom(ch, 0, *buf, ch, readPos, samplesToMix, this->previewGain);
            }
            readPos += samplesToMix;
            if (readPos >= buf->getNumSamples())
                this->previewReadPos.store(-1);
            else
                this->previewReadPos.store(readPos);
        } else {
            this->previewReadPos.store(-1);
        }
    }
}

void DrumKitEngine::clearCrosstalkProfile() {
    this->crosstalkProfile.clear();
}

void DrumKitEngine::beginCrosstalkCalibration() {
    this->crosstalkProfileBeforeCalibration = this->crosstalkProfile;
    this->crosstalkProfile.clear();
}

void DrumKitEngine::cancelCrosstalkCalibration() {
    if (this->crosstalkProfileBeforeCalibration.has_value())
        this->crosstalkProfile = *this->crosstalkProfileBeforeCalibration;
    this->crosstalkProfileBeforeCalibration.reset();
}

void DrumKitEngine::finishCrosstalkCalibration() {
    this->crosstalkProfileBeforeCalibration.reset();
}

void DrumKitEngine::setCrosstalkThreshold(int primaryNote, int secondaryNote, int velocityRatio) {
    this->crosstalkProfile.setThreshold(primaryNote, secondaryNote, velocityRatio);
}

juce::String DrumKitEngine::buildCrosstalkProfileText() const {
    juce::String text;
    for (int primaryNote = 0; primaryNote < 128; ++primaryNote) {
        for (int secondaryNote = 0; secondaryNote < 128; ++secondaryNote) {
            const int ratio = this->crosstalkProfile.getThreshold(primaryNote, secondaryNote);
            if (ratio <= 0)
                continue;
            const auto primaryName = juce::MidiMessage::getMidiNoteName(primaryNote, true, true, 3);
            const auto secondaryName = juce::MidiMessage::getMidiNoteName(secondaryNote, true, true, 3);
            const int percentage = static_cast<int>(std::round(static_cast<double>(ratio) * 100.0 / 127.0));
            text += juce::String(primaryNote) + " " + primaryName + " -> " + juce::String(secondaryNote) + " " + secondaryName + ": suppress <= " + juce::String(percentage) + "% of primary velocity\n";
        }
    }
    return text.isNotEmpty() ? text : "No learned crosstalk pairs.";
}

std::optional<juce::String> DrumKitEngine::getNameForMidiNoteNumber(int note, int /* midiChannel */) {
    if (note < 0 || note > 127) return {};

    const auto& name = this->noteNameCache[static_cast<size_t>(note)];
    if (name.isNotEmpty()) return name;
    return {};
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
            if (ArticulationNameHelpers::mapFilterMatchesArticulation(artName, art.codeRaw) || artName == "(default)") {
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
            if (!ArticulationNameHelpers::mapFilterMatchesArticulation(artName, articulation.codeRaw) && artName != "(default)") continue;
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
            if (!ArticulationNameHelpers::mapFilterMatchesArticulation(articulationSlot, articulation.codeRaw) || articulation.notes.empty()) continue;
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
    this->owner.resolveAndLoadPreviewTarget(std::move(this->cacheKey),
                                            this->kitCodeRaw,
                                            this->instCode,
                                            this->artCode,
                                            this->relativePath,
                                            this->micCode,
                                            this->resolveVelocity,
                                            this->playbackVelocity,
                                            this->generation);
    return jobHasFinished;
}

std::string DrumKitEngine::makePreviewTargetCacheKey(const juce::String& kitCodeRaw,
                                                     const juce::String& instCode,
                                                     const juce::String& artCode,
                                                     juce::uint8 velocity,
                                                     const juce::String& relativePath,
                                                     const juce::String& micCode) const {
    const auto key = kitCodeRaw.trim().toLowerCase()
                   + "|" + instCode.trim().toLowerCase()
                   + "|" + artCode.trim().toLowerCase()
                   + "|" + juce::String(DrumKitEnginePreviewHelpers::getPreviewVelocityBucket(velocity))
                   + "|" + relativePath.trim().replaceCharacter('\\', '/').toLowerCase()
                   + "|" + micCode.trim().toLowerCase();
    return key.toStdString();
}

std::optional<DrumKitEngine::PreviewTarget> DrumKitEngine::resolvePreviewTarget(const juce::String& kitCodeRaw,
                                                                                 const juce::String& instCode,
                                                                                 const juce::String& artCode,
                                                                                 juce::uint8 velocity,
                                                                                 const juce::String& relativePath,
                                                                                 const juce::String& micCode) {
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
                if (bestSample == nullptr || DrumKitEnginePreviewHelpers::isBetterPreviewSample(*sample, *bestSample)) bestSample = sample.get();
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


void DrumKitEngine::previewSourceItem(const juce::String& kitCodeRaw, const juce::String& instCode, const juce::String& artCode, juce::uint8 velocity, const juce::String& relativePath, const juce::String& micCode) {
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

    const auto resolveVelocity = DrumKitEnginePreviewHelpers::getPreviewVelocityForBucket(velocity);
    this->previewResolveThreadPool.addJob(new PreviewResolveJob(*this, cacheKey, kitCodeRaw, instCode, artCode, relativePath, micCode, resolveVelocity, velocity, generation), true);
}


void DrumKitEngine::saveLatestControllerValues(const juce::MidiBuffer& midiMessages) {
    for (const auto& midiMess : midiMessages) {
        const juce::MidiMessage msg = midiMess.getMessage();
        if (msg.isController()) {
            const auto ccNum = msg.getControllerNumber();
            if (ccNum >= 0 && ccNum <= 127 && this->trackedCCMask[static_cast<size_t>(ccNum)])
                this->latestCCValues[static_cast<size_t>(ccNum)] = msg.getControllerValue();
        } else if (msg.isAftertouch()) {
            const auto note = msg.getNoteNumber();
            if (note >= 0 && note <= 127 && this->trackedPAMask[static_cast<size_t>(note)])
                this->latestPAValues[static_cast<size_t>(note)] = msg.getAfterTouchValue();
        }
    }
}

void DrumKitEngine::updateSlotParameters(const juce::String& slotId, const juce::String& artName, int layerIndex, std::optional<int> tuningCents, std::optional<int> attackMs, std::optional<int> decayMs) {
    LayerParameterUpdate update;
    update.tuningCents = tuningCents;
    update.attackMs = attackMs;
    update.decayMs = decayMs;
    this->updateSlotParameters(slotId, artName, layerIndex, update);
}

bool DrumKitEngine::updateSlotSourceParameters(const juce::String& slotId, const juce::String& artName, const juce::String& sourcePath, int sourceOccurrenceIndex, const LayerParameterUpdate& update) {
    if (sourceOccurrenceIndex < 0) return false;

    juce::StringArray sourceParts;
    sourceParts.addTokens(sourcePath.trim(), "/", "");
    sourceParts.removeEmptyStrings();
    if (sourceParts.size() < 2) return false;

    const auto targetKit = sourceParts[0].trim();
    const auto targetInst = sourceParts[1].trim();
    const auto targetArt = sourceParts.size() >= 3 ? sourceParts[2].trim() : juce::String();

    const auto targetRoutes = this->getTargetRoutesForArticulationFilter(slotId, artName);
    auto librarySnapshot = this->libraryManagerPtr;
    if (librarySnapshot == nullptr) return false;
    auto* targetKitSpec = librarySnapshot->findKitById(targetKit);
    if (targetKitSpec == nullptr) return false;
    auto* targetInstrumentSpec = targetKitSpec->findInstrumentById(targetInst);
    if (targetInstrumentSpec == nullptr) return false;
    const auto* targetArticulationSpec = targetArt.isNotEmpty() ? targetInstrumentSpec->findArticulationById(targetArt) : nullptr;
    if (targetArt.isNotEmpty() && targetArticulationSpec == nullptr) return false;

    const juce::ScopedLock graphLock(this->engineGraphLock);

    int currentOccurrence = 0;
    for (auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
        if (instrumentPlayerPtr == nullptr) continue;
        if (instrumentPlayerPtr->getId() != slotId) continue;
        if (!DrumKitEngine::playerMatchesTargetRoutes(*instrumentPlayerPtr, targetRoutes) || !instrumentPlayerPtr->routesLibraryArticulation(*targetInstrumentSpec, targetArticulationSpec)) continue;

        if (currentOccurrence == sourceOccurrenceIndex) {
            return instrumentPlayerPtr->updateArticulationParameters(targetRoutes, update);
        }

        ++currentOccurrence;
    }

    return false;
}
void DrumKitEngine::updateSlotParameters(const juce::String& slotId, const juce::String& artName, int layerIndex, const LayerParameterUpdate& update) {
    if (layerIndex < 0)
        return;

    const auto targetRoutes = this->getTargetRoutesForArticulationFilter(slotId, artName);

    const juce::ScopedLock graphLock(this->engineGraphLock);

    int currentLayerIndex = 0;
    for (auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
        if (instrumentPlayerPtr == nullptr) continue;
        if (instrumentPlayerPtr->getId() != slotId) continue;
        if (!DrumKitEngine::playerMatchesTargetRoutes(*instrumentPlayerPtr, targetRoutes)) continue;

        if (currentLayerIndex == layerIndex) {
            instrumentPlayerPtr->updateArticulationParameters(targetRoutes, update);
            return;
        }

        ++currentLayerIndex;
    }
}

bool DrumKitEngine::updateSlotParametersBatch(const std::vector<LayerParameterTarget>& targets) {
    struct PreparedTarget {
        const LayerParameterTarget* target = nullptr;
        std::vector<std::pair<uint32_t, uint32_t>> routes;
    };

    std::vector<PreparedTarget> preparedTargets;
    preparedTargets.reserve(targets.size());
    for (const auto& target : targets) {
        if (target.layerIndex < 0 || target.slotId.trim().isEmpty()) continue;
        preparedTargets.push_back({ &target, this->getTargetRoutesForArticulationFilter(target.slotId, target.articulationName) });
    }
    if (preparedTargets.empty()) return false;

    const juce::ScopedLock graphLock(this->engineGraphLock);
    bool updated = false;
    for (const auto& preparedTarget : preparedTargets) {
        int currentLayerIndex = 0;
        for (auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
            if (instrumentPlayerPtr == nullptr || instrumentPlayerPtr->getId() != preparedTarget.target->slotId || !DrumKitEngine::playerMatchesTargetRoutes(*instrumentPlayerPtr, preparedTarget.routes)) continue;
            if (currentLayerIndex == preparedTarget.target->layerIndex) {
                updated = instrumentPlayerPtr->updateArticulationParameters(preparedTarget.routes, preparedTarget.target->update) || updated;
                break;
            }
            ++currentLayerIndex;
        }
    }
    return updated;
}

bool DrumKitEngine::updateSlotMicRoutesBatch(const std::vector<MicRouteTarget>& targets) {
    struct PreparedTarget {
        const MicRouteTarget* target = nullptr;
        std::vector<std::pair<uint32_t, uint32_t>> routes;
    };

    std::vector<PreparedTarget> preparedTargets;
    preparedTargets.reserve(targets.size());
    for (const auto& target : targets) {
        if (target.layerIndex < 0 || target.slotId.trim().isEmpty() || target.micCode.trim().isEmpty()) continue;
        preparedTargets.push_back({ &target, this->getTargetRoutesForArticulationFilter(target.slotId, target.articulationName) });
    }
    if (preparedTargets.empty()) return false;

    const juce::ScopedLock graphLock(this->engineGraphLock);
    bool updated = false;
    for (const auto& preparedTarget : preparedTargets) {
        int currentLayerIndex = 0;
        for (auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
            if (instrumentPlayerPtr == nullptr || instrumentPlayerPtr->getId() != preparedTarget.target->slotId || !DrumKitEngine::playerMatchesTargetRoutes(*instrumentPlayerPtr, preparedTarget.routes)) continue;
            if (currentLayerIndex == preparedTarget.target->layerIndex) {
                updated = instrumentPlayerPtr->updateMicRoute(preparedTarget.target->micCode, preparedTarget.target->outputId) || updated;
                break;
            }
            ++currentLayerIndex;
        }
    }
    return updated;
}

std::vector<std::pair<uint32_t, uint32_t>> DrumKitEngine::getTargetRoutesForArticulationFilter(const juce::String& slotId, const juce::String& artName) const {
    std::vector<std::pair<uint32_t, uint32_t>> targetRoutes;
    const bool hasArtFilter = artName.trim().isNotEmpty() && artName.trim() != "(default)";
    if (!hasArtFilter || this->activeKitLayoutPtr == nullptr) return targetRoutes;

    for (const auto& slot : this->activeKitLayoutPtr->instrumentSlots) {
        if (slot.id != slotId) continue;
        for (const auto& art : slot.articulations) {
            if (!ArticulationNameHelpers::mapFilterMatchesArticulation(artName, art.codeRaw)) continue;
            const auto route = std::make_pair(art.nameId, art.exprId);
            if (std::find(targetRoutes.begin(), targetRoutes.end(), route) == targetRoutes.end()) targetRoutes.push_back(route);
        }
        break;
    }
    return targetRoutes;
}

bool DrumKitEngine::playerMatchesTargetRoutes(const InstrumentPlayer& instrumentPlayer, const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes) {
    if (targetRoutes.empty()) return true;
    if (!instrumentPlayer.hasArticulationSlotRoutes()) return false;

    for (const auto& route : targetRoutes) {
        if (instrumentPlayer.routesArticulationSlot(route.first, route.second)) return true;
    }
    return false;
}

bool DrumKitEngine::playerRoutesArticulationSlot(const InstrumentPlayer& instrumentPlayer, const KitLayout::InstrumentSlot::ArticulationSlot& art) {
    return instrumentPlayer.routesArticulationSlot(art.nameId, art.exprId);
}

void DrumKitEngine::applyLayerParameterUpdate(InstrumentPlayer& instrumentPlayer, const LayerParameterUpdate& update) {
    if (update.tuningCents.has_value()) instrumentPlayer.setTuningOffsetCents(*update.tuningCents);
    if (update.attackMs.has_value()) instrumentPlayer.setAttackOverrideMs(*update.attackMs);
    if (update.decayMs.has_value()) instrumentPlayer.setDecayOverrideMs(*update.decayMs);
    if (update.volumeDb.has_value()) instrumentPlayer.setVolumeDb(*update.volumeDb);
    if (update.pan.has_value()) instrumentPlayer.setPan(*update.pan);
    if (update.flipLR.has_value()) instrumentPlayer.setFlipLR(*update.flipLR);
    if (update.flipPhase.has_value()) instrumentPlayer.setFlipPhase(*update.flipPhase);
    if (update.micBleedLevels.has_value()) instrumentPlayer.setMicBleedLevels(*update.micBleedLevels);
}

bool DrumKitEngine::useInstrumentMap(juce::String text) {
    return this->useInstrumentMapInternal(std::move(text), {});
}

bool DrumKitEngine::applyInstrumentMapSlotChange(const juce::String& fullMapText, const juce::String& changedSlotId) {
    return this->useInstrumentMapInternal(fullMapText, changedSlotId);
}

bool DrumKitEngine::useInstrumentMapInternal(juce::String text, const juce::String& changedSlotIdHint) {
    // ======================== PHASE 1: VALIDATION (no lock) ========================
    // Safe to read libraryManagerPtr/activeKitLayoutPtr without lock - only mutated on
    // the message thread, which is our current thread.
    this->lastInstrumentMapStatus.clear();

    if (text.trim().isEmpty()) {
        this->lastInstrumentMapStatus = "Instrument map is empty";
        LOG(Warning, this->lastInstrumentMapStatus);
        return false;
    }

    auto librarySnapshot = this->libraryManagerPtr;
    if (librarySnapshot == nullptr) {
        this->lastInstrumentMapStatus = "LibraryManager is not initialized";
        LOG(Error, this->lastInstrumentMapStatus);
        return false;
    }

    if (this->activeKitLayoutPtr == nullptr) {
        this->lastInstrumentMapStatus = "Cannot apply instrument map without an active kit layout";
        LOG(Error, this->lastInstrumentMapStatus);
        return false;
    }

    const auto changedSlotKey = changedSlotIdHint.trim().toLowerCase();
    const bool hasChangedSlotHint = changedSlotKey.isNotEmpty();

    // ======================== PHASE 2: PARSE & RESOLVE (no lock) ========================
    // All parsing, kit/instrument resolution, and instrument player creation happens here.
    // The audio thread continues to process the old instrument player graph.

    auto parsed = DrumKitEngine::parseInstrumentMapText(text);
    for (const auto& warning : parsed.errors)
        LOG(parsed.ok() ? Warning : Error, warning);

    if (!parsed.ok()) {
        this->lastInstrumentMapStatus = parsed.error();
        return false;
    }

    DrumKitEngineIdentityMapHelpers::normalizeInstrumentMapLayers(parsed.value, *librarySnapshot);
    DrumKitEngineMapHelpers::applyAutomaticOrientation(parsed.value, *this->activeKitLayoutPtr, *librarySnapshot);
    const auto normalizedMapText = DrumKitEngine::serializeInstrumentMap(parsed.value);

    LOG(Info, "=== useInstrumentMap: " + juce::String(parsed.value.layers.size()) + " map layers ===");
    for (size_t idx = 0; idx < parsed.value.layers.size(); ++idx) {
        const auto& mapLayer = parsed.value.layers[idx];
        juce::String articulationSlotText, libraryArticulationText;
        for (const auto& articulationSlotName : mapLayer.slotArticulationFilter) articulationSlotText += (articulationSlotText.isEmpty() ? "" : ",") + articulationSlotName;
        for (const auto& libraryArticulationName : mapLayer.articulationFilter) libraryArticulationText += (libraryArticulationText.isEmpty() ? "" : ",") + libraryArticulationName;
        juce::String routeStr;
        for (const auto& [micCode, outputId] : mapLayer.micRouteOverrides)
            routeStr += (routeStr.isEmpty() ? "" : ",") + micCode + "->" + outputId;
        LOG(Info, "  mapLayer[" + juce::String(idx) + "]: slot=" + mapLayer.slotId
            + (articulationSlotText.isNotEmpty() ? "/" + articulationSlotText : "")
            + " -> kit=" + mapLayer.kitRef + " inst=" + mapLayer.instCode
            + (libraryArticulationText.isNotEmpty() ? " art=" + libraryArticulationText : "")
            + (mapLayer.tuningCents.has_value() ? " tuning=" + juce::String(*mapLayer.tuningCents) : "")
            + (mapLayer.attackTimeMs.has_value() ? " attack=" + juce::String(*mapLayer.attackTimeMs) : "")
            + (mapLayer.decayTimeMs.has_value() ? " decay=" + juce::String(*mapLayer.decayTimeMs) : "")
            + (routeStr.isNotEmpty() ? " micRoute=[" + routeStr + "]" : ""));
    }

    struct ShadowPlan {
        const InstrumentMapLayer* mapLayer = nullptr;
        KitLib::InstrumentSpec* instSpec = nullptr;
        std::vector<juce::String> articulationSlots;
        InstrumentPlayer::ArticulationNameRemap artNameRemap;
        InstrumentPlayer::ArticulationExactRemap artExactRemap;
    };

    // Build a name remap from the articulation slot and library articulation filters.
    auto buildArtNameRemap = [](const InstrumentMapLayer& mapLayer) -> InstrumentPlayer::ArticulationNameRemap {
        InstrumentPlayer::ArticulationNameRemap remap;
        const auto& articulationSlots = mapLayer.slotArticulationFilter;
        const auto& libraryArticulations = mapLayer.articulationFilter;
        if (articulationSlots.empty() || libraryArticulations.empty())
            return remap;
        for (size_t idx = 0; idx < articulationSlots.size() && idx < libraryArticulations.size(); ++idx) {
            auto articulationSlotId = StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(articulationSlots[idx]));
            auto libraryArticulationId = StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(libraryArticulations[idx]));
            if (articulationSlotId != libraryArticulationId)
                remap[articulationSlotId] = libraryArticulationId;
        }
        return remap;
    };

    auto makeArticulationRouteKey = [](uint32_t nameId, uint32_t exprId) {
        return (static_cast<uint64_t>(nameId) << 32) | static_cast<uint64_t>(exprId);
    };

    auto buildArtExactRemap = [&makeArticulationRouteKey](const InstrumentMapLayer& mapLayer) -> InstrumentPlayer::ArticulationExactRemap {
        InstrumentPlayer::ArticulationExactRemap remap;
        const auto& articulationSlots = mapLayer.slotArticulationFilter;
        const auto& libraryArticulations = mapLayer.articulationFilter;
        if (articulationSlots.empty() || libraryArticulations.empty()) return remap;
        for (size_t idx = 0; idx < articulationSlots.size() && idx < libraryArticulations.size(); ++idx) {
            if (articulationSlots[idx].isEmpty() || libraryArticulations[idx].isEmpty()) continue;
            const KitLayout::InstrumentSlot::ArticulationSlot articulationSlot(articulationSlots[idx]);
            const KitLayout::InstrumentSlot::ArticulationSlot libraryArticulation(libraryArticulations[idx]);
            if (articulationSlot.nameId == libraryArticulation.nameId && articulationSlot.exprId == libraryArticulation.exprId) continue;
            remap[makeArticulationRouteKey(articulationSlot.nameId, articulationSlot.exprId)] = { libraryArticulation.nameId, libraryArticulation.exprId };
        }
        return remap;
    };
    auto resolveVelocityResponseOverride = [](const InstrumentMapLayer& mapLayer) -> std::optional<VelocityResponse> {
        if (!mapLayer.velocityResponse.has_value()) return std::nullopt;
        auto parsedResponse = VelocityResponse::fromDefinition(*mapLayer.velocityResponse);
        return parsedResponse.ok() ? std::optional<VelocityResponse>(std::move(parsedResponse.value)) : std::nullopt;
    };
    auto applyOverrides = [&resolveVelocityResponseOverride](InstrumentPlayer& instrumentPlayer, const InstrumentMapLayer& mapLayer) {
        instrumentPlayer.setTuningOffsetCents(mapLayer.tuningCents.value_or(0));
        instrumentPlayer.setAttackOverrideMs(mapLayer.attackTimeMs.value_or(-1));
        instrumentPlayer.setDecayOverrideMs(mapLayer.decayTimeMs.value_or(-1));
        instrumentPlayer.setVolumeDb(mapLayer.volumeDb.value_or(0.0f));
        instrumentPlayer.setPan(mapLayer.pan.value_or(0));
        instrumentPlayer.setFlipLR(mapLayer.flipLR);
        instrumentPlayer.setFlipPhase(mapLayer.flipPhase);
        instrumentPlayer.setMicBleedLevels(mapLayer.micBleedLevels);
        LayerParameterUpdate velocityResponseUpdate;
        velocityResponseUpdate.updateVelocityResponse = true;
        velocityResponseUpdate.velocityResponse = resolveVelocityResponseOverride(mapLayer);
        instrumentPlayer.updateArticulationParameters({}, velocityResponseUpdate);
    };

    auto makeParameterUpdate = [&resolveVelocityResponseOverride](const InstrumentMapLayer& mapLayer) {
        LayerParameterUpdate update;
        update.tuningCents = mapLayer.tuningCents.value_or(0);
        update.attackMs = mapLayer.attackTimeMs.value_or(-1);
        update.decayMs = mapLayer.decayTimeMs.value_or(-1);
        update.volumeDb = mapLayer.volumeDb.value_or(0.0f);
        update.pan = mapLayer.pan.value_or(0);
        update.flipLR = mapLayer.flipLR;
        update.flipPhase = mapLayer.flipPhase;
        update.micBleedLevels = mapLayer.micBleedLevels;
        update.updateVelocityResponse = true;
        update.velocityResponse = resolveVelocityResponseOverride(mapLayer);
        return update;
    };

    auto buildTargetRoutes = [](const KitLayout::InstrumentSlot& slotLayout, const std::vector<juce::String>& articulationSlotFilters) {
        std::vector<std::pair<uint32_t, uint32_t>> targetRoutes;
        if (articulationSlotFilters.empty()) return targetRoutes;
        for (const auto& articulationSlot : slotLayout.articulations) {
            for (const auto& articulationSlotFilter : articulationSlotFilters) {
                if (!ArticulationNameHelpers::mapFilterMatchesArticulation(articulationSlotFilter, articulationSlot.codeRaw)) continue;
                const auto route = std::make_pair(articulationSlot.nameId, articulationSlot.exprId);
                if (std::find(targetRoutes.begin(), targetRoutes.end(), route) == targetRoutes.end()) targetRoutes.push_back(route);
            }
        }
        return targetRoutes;
    };

    auto applyPrimaryOverrides = [&applyOverrides, &makeParameterUpdate](InstrumentPlayer& instrumentPlayer, const InstrumentMapLayer& mapLayer, const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes) {
        if (targetRoutes.empty()) applyOverrides(instrumentPlayer, mapLayer);
        else instrumentPlayer.updateArticulationParameters(targetRoutes, makeParameterUpdate(mapLayer));
    };

    auto makeResolvedMapLayerFingerprint = [](const InstrumentMapLayer& mapLayer, const KitLib::InstrumentSpec& instSpec) {
        return DrumKitEngineMapHelpers::makeInstrumentMapLayerFingerprint(mapLayer) + "\nsource\n" + instSpec.getContentFingerprint();
    };

    auto makeResolvedMapLayerReloadFingerprint = [](const InstrumentMapLayer& mapLayer, const KitLib::InstrumentSpec& instSpec) {
        return DrumKitEngineMapHelpers::makeInstrumentMapLayerReloadFingerprint(mapLayer) + "\nsource\n" + instSpec.getContentFingerprint();
    };

    std::vector<juce::String> slotOrder;
    std::unordered_map<juce::String, std::vector<const InstrumentMapLayer*>> mapLayersBySlotId;
    for (const auto& mapLayer : parsed.value.layers) {
        if (mapLayersBySlotId.find(mapLayer.slotId) == mapLayersBySlotId.end()) slotOrder.push_back(mapLayer.slotId);
        mapLayersBySlotId[mapLayer.slotId].push_back(&mapLayer);
    }

    std::unordered_map<juce::String, juce::String> nextSlotFingerprints = DrumKitEngineMapHelpers::buildResolvedSlotFingerprintMap(parsed.value, *librarySnapshot);
    std::unordered_map<juce::String, juce::String> nextSlotReloadFingerprints = DrumKitEngineMapHelpers::buildResolvedSlotReloadFingerprintMap(parsed.value, *librarySnapshot);
    struct ExistingShadow {
        InstrumentPlayer* instrumentPlayer = nullptr;
        juce::String stateFingerprint;
        juce::String reloadFingerprint;
    };

    // Build index maps for fast slot lookup (safe to read on message thread)
    std::unordered_map<juce::String, int> primarySlotIndex;
    std::unordered_map<juce::String, std::vector<ExistingShadow>> existingShadowsBySlotId;
    for (int idx = 0; idx < static_cast<int>(this->instrumentPlayerPtrs.size()); ++idx) {
        auto* instrumentPlayer = this->instrumentPlayerPtrs[static_cast<size_t>(idx)].get();
        if (instrumentPlayer == nullptr) continue;
        if (idx < this->primaryInstrumentPlayerCount) {
            primarySlotIndex[instrumentPlayer->getId()] = idx;
            continue;
        }

        ExistingShadow existingShadow;
        existingShadow.instrumentPlayer = instrumentPlayer;
        auto stateIt = this->lastAppliedShadowFingerprints.find(instrumentPlayer);
        if (stateIt != this->lastAppliedShadowFingerprints.end()) existingShadow.stateFingerprint = stateIt->second;
        auto reloadIt = this->lastAppliedShadowReloadFingerprints.find(instrumentPlayer);
        if (reloadIt != this->lastAppliedShadowReloadFingerprints.end()) existingShadow.reloadFingerprint = reloadIt->second;
        existingShadowsBySlotId[instrumentPlayer->getId()].push_back(std::move(existingShadow));
    }

    juce::StringArray statusLines;
    for (const auto& warning : parsed.errors)
        statusLines.add(warning);

    int applied = 0;

    // Prepare replacement and shadow instrument players for each slot outside the lock.
    struct PreparedShadow {
        std::unique_ptr<InstrumentPlayer> instrumentPlayer;
        juce::String stateFingerprint;
        juce::String reloadFingerprint;
        std::vector<std::pair<int, InstrumentPlayer*>> noteRoutes;
    };
    struct PreparedShadowParameterUpdate {
        InstrumentPlayer* instrumentPlayer = nullptr;
        const InstrumentMapLayer* mapLayer = nullptr;
        juce::String stateFingerprint;
    };
    struct PreparedSlot {
        juce::String slotId;
        int primaryIndex = -1;
        std::unique_ptr<InstrumentPlayer> primaryReplacement;
        const InstrumentMapLayer* primaryParameterMapLayer = nullptr;
        std::vector<std::pair<uint32_t, uint32_t>> primaryParameterRoutes;
        bool removeOldShadows = false;
        bool rebuildShadows = false;
        std::vector<PreparedShadow> shadows;
        std::vector<PreparedShadowParameterUpdate> shadowParameterUpdates;
    };
    std::vector<PreparedSlot> preparedSlots;

    for (const auto& slotId : slotOrder) {
        const auto& slotMapLayers = mapLayersBySlotId[slotId];
        if (slotMapLayers.empty()) continue;

        auto slotIt = this->instrumentPlayerById.find(slotId);
        if (slotIt == this->instrumentPlayerById.end()) {
            statusLines.add("Instrument slot not found: " + slotId);
            continue;
        }
        auto* primarySlot = slotIt->second;
        bool slotChanged = !primarySlot->isLoaded();
        const bool slotStateChanged = this->lastAppliedSlotFingerprints[slotId] != nextSlotFingerprints[slotId];
        const bool slotReloadChanged = this->lastAppliedSlotReloadFingerprints[slotId] != nextSlotReloadFingerprints[slotId];
        const bool slotMatchesHint = !hasChangedSlotHint || slotId.trim().toLowerCase() == changedSlotKey;
        if (hasChangedSlotHint && !slotMatchesHint && !slotChanged && !slotStateChanged && !slotReloadChanged) continue;

        struct ResolvedMapLayer {
            const InstrumentMapLayer* mapLayer = nullptr;
            KitLib::DrumKitSpec* kitSpec = nullptr;
            KitLib::InstrumentSpec* instSpec = nullptr;
            std::vector<juce::String> effectiveArticulationSlotFilters;
        };

        std::vector<ResolvedMapLayer> resolvedMapLayers;
        bool slotResolveFailed = false;
        for (const auto* mapLayer : slotMapLayers) {
            auto* kitSpec = librarySnapshot->findKitById(mapLayer->kitRef);
            if (kitSpec == nullptr) {
                statusLines.add(DrumKitEngineIdentityMapHelpers::describeKitResolveFailure(*librarySnapshot, mapLayer->kitRef, mapLayer->slotId));
                slotResolveFailed = true;
                continue;
            }

            if (mapLayer->instCode.isEmpty()) {
                statusLines.add("Library instrument ID is required for slot '" + mapLayer->slotId + "'");
                slotResolveFailed = true;
                continue;
            }

            auto* instSpec = kitSpec->findInstrumentById(mapLayer->instCode);
            if (instSpec == nullptr) {
                statusLines.add(DrumKitEngineIdentityMapHelpers::describeInstrumentResolveFailure(*kitSpec, mapLayer->instCode, mapLayer->slotId));
                slotResolveFailed = true;
                continue;
            }

            if (!mapLayer->articulationFilter.empty()) {
                const auto availableLibraryArticulations = DrumKitEngineMapHelpers::collectLibraryArticulationNames(*instSpec);
                juce::StringArray unknownLibraryArticulations;
                juce::StringArray unknownLibraryArticulationKeys;

                for (const auto& libraryArticulationName : mapLayer->articulationFilter) {
                    const auto normalizedLibraryArticulationName = ArticulationNameHelpers::normalizeArticulationName(libraryArticulationName);
                    if (normalizedLibraryArticulationName.isEmpty()) continue;
                    const auto libraryArticulationMatchCount = DrumKitEngineIdentityMapHelpers::countArticulationIdMatches(*instSpec, libraryArticulationName);
                    if (libraryArticulationMatchCount == 1) continue;
                    if (libraryArticulationMatchCount == 0 && ArticulationNameHelpers::stringArrayContainsIgnoreCase(availableLibraryArticulations, normalizedLibraryArticulationName)) continue;
                    if (ArticulationNameHelpers::stringArrayContainsIgnoreCase(unknownLibraryArticulationKeys, normalizedLibraryArticulationName)) continue;
                    auto libraryArticulationToken = DrumKitEngineIdentityMapHelpers::describeArticulationResolveFailure(*instSpec, libraryArticulationName, kitSpec->meta.codeRaw + "/" + instSpec->meta.codeRaw, mapLayer->slotId);
                    const auto suggestedLibraryArticulation = ArticulationNameHelpers::findClosestArticulationName(normalizedLibraryArticulationName, availableLibraryArticulations);
                    if (suggestedLibraryArticulation.isNotEmpty()) libraryArticulationToken += " (did you mean '" + suggestedLibraryArticulation + "'?)";
                    unknownLibraryArticulationKeys.add(normalizedLibraryArticulationName);
                    unknownLibraryArticulations.add(libraryArticulationToken);
                }

                if (!unknownLibraryArticulations.isEmpty()) {
                    const auto unknownLibraryArticulationsText = unknownLibraryArticulations.joinIntoString(", ");
                    const auto availableLibraryArticulationsText = availableLibraryArticulations.isEmpty()
                        ? juce::String("none")
                        : availableLibraryArticulations.joinIntoString(", ");

                    statusLines.add("Library articulation errors: " + unknownLibraryArticulationsText + ". Available library articulations: " + availableLibraryArticulationsText);
                    slotResolveFailed = true;
                    continue;
                }
            }

            auto articulationSlotFilters = DrumKitEngineMapHelpers::effectiveArticulationSlotFilter(*mapLayer);
            juce::String articulationSlotFilterText;
            for (const auto& articulationSlotFilter : articulationSlotFilters) articulationSlotFilterText += (articulationSlotFilterText.isEmpty() ? "" : ",") + articulationSlotFilter;
            LOG(Info, "  resolved: slot=" + mapLayer->slotId + " -> " + kitSpec->meta.name + "/" + mapLayer->instCode
                + (articulationSlotFilterText.isNotEmpty() ? " articulationSlots=[" + articulationSlotFilterText + "]" : " articulationSlots=[ALL]"));
            resolvedMapLayers.push_back({ mapLayer, kitSpec, instSpec, std::move(articulationSlotFilters) });
        }

        if (slotResolveFailed || resolvedMapLayers.empty()) {
            statusLines.add("Slot '" + slotId + "' was not applied; old route kept");
            nextSlotFingerprints[slotId] = this->lastAppliedSlotFingerprints[slotId];
            nextSlotReloadFingerprints[slotId] = this->lastAppliedSlotReloadFingerprints[slotId];
            continue;
        }

        slotChanged = slotChanged || slotReloadChanged;

        const auto& slotLayout = primarySlot->getInstrumentSlot();
        const auto& primary = resolvedMapLayers.front();
        InstrumentPlayer::ArticulationLibraryMap articulationLibraryMap;
        InstrumentPlayer::ArticulationNameRemap primaryRemap = buildArtNameRemap(*primary.mapLayer);
        InstrumentPlayer::ArticulationExactRemap primaryExactRemap = buildArtExactRemap(*primary.mapLayer);
        std::vector<ShadowPlan> shadowPlans;
        std::vector<uint32_t> claimedArticulationSlotNameIds;
        std::vector<uint32_t> primaryAllowedArticulationSlotNameIds;
        std::vector<juce::String> primaryAllowedArticulationSlotFilters;
        InstrumentPlayer::ArticulationAliasMap artAliases;
        auto appendPrimaryAllowedArticulationSlotFilter = [&primaryAllowedArticulationSlotFilters](const juce::String& articulationSlotName) {
            const auto normalizedArticulationSlotName = articulationSlotName.trim();
            if (normalizedArticulationSlotName.isEmpty()) return;
            for (const auto& existingArticulationSlotName : primaryAllowedArticulationSlotFilters)
                if (existingArticulationSlotName.compareIgnoreCase(normalizedArticulationSlotName) == 0) return;
            primaryAllowedArticulationSlotFilters.push_back(normalizedArticulationSlotName);
        };
        auto removePrimaryAllowedArticulationSlotFilter = [&primaryAllowedArticulationSlotFilters](uint32_t articulationSlotNameId) {
            std::erase_if(primaryAllowedArticulationSlotFilters, [articulationSlotNameId](const juce::String& articulationSlotName) {
                return StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(articulationSlotName)) == articulationSlotNameId;
            });
        };

        if (primary.effectiveArticulationSlotFilters.empty()) {
            for (const auto& articulationSlot : slotLayout.articulations)
                DrumKitEngineMapHelpers::appendUniqueId(primaryAllowedArticulationSlotNameIds, articulationSlot.nameId);
        } else {
            for (const auto& articulationSlotName : primary.effectiveArticulationSlotFilters) {
                if (articulationSlotName.isNotEmpty()) {
                    DrumKitEngineMapHelpers::appendUniqueId(primaryAllowedArticulationSlotNameIds, StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(articulationSlotName)));
                    appendPrimaryAllowedArticulationSlotFilter(articulationSlotName);
                }
            }
        }

        if (LOG_ENABLED(Info)) {
            juce::String allowedStr;
            for (const auto& articulationSlotNameId : primaryAllowedArticulationSlotNameIds)
                allowedStr += (allowedStr.isEmpty() ? "" : ",") + StringsDictSingleton::nameOf(articulationSlotNameId);
            LOG(Info, "  slot=" + slotId + " primaryAllowed(initial)=[" + allowedStr + "] slotChanged=" + juce::String(slotChanged ? "yes" : "no"));
        }

        for (const auto& articulationSlotName : primary.effectiveArticulationSlotFilters) {
            if (articulationSlotName.isNotEmpty())
                DrumKitEngineMapHelpers::appendUniqueId(claimedArticulationSlotNameIds, StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(articulationSlotName)));
        }

        for (size_t idx = 1; idx < resolvedMapLayers.size(); ++idx) {
            const auto& resolved = resolvedMapLayers[idx];
            if (resolved.effectiveArticulationSlotFilters.empty()) {
                shadowPlans.push_back({ resolved.mapLayer, resolved.instSpec, {}, buildArtNameRemap(*resolved.mapLayer), buildArtExactRemap(*resolved.mapLayer) });
                continue;
            }

            std::vector<juce::String> layeredArticulationSlots;
            const bool hasParameterOverrides = resolved.mapLayer->tuningCents.has_value()
                                            || resolved.mapLayer->attackTimeMs.has_value()
                                            || resolved.mapLayer->decayTimeMs.has_value()
                                            || resolved.mapLayer->volumeDb.has_value()
                                            || resolved.mapLayer->pan.has_value()
                                            || resolved.mapLayer->velocityResponse.has_value()
                                            || resolved.mapLayer->flipLR
                                            || resolved.mapLayer->flipPhase
                                            || !resolved.mapLayer->micBleedLevels.empty()
                                            || !resolved.mapLayer->micRouteOverrides.empty();
            for (const auto& articulationSlotName : resolved.effectiveArticulationSlotFilters) {
                if (articulationSlotName.isEmpty()) continue;

                const auto articulationSlotNameId = StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(articulationSlotName));
                const bool alreadyClaimed = vectorContains(claimedArticulationSlotNameIds, articulationSlotNameId);
                if (hasParameterOverrides) {
                    layeredArticulationSlots.push_back(articulationSlotName);
                    if (!alreadyClaimed) {
                        DrumKitEngineMapHelpers::appendUniqueId(claimedArticulationSlotNameIds, articulationSlotNameId);
                        std::erase(primaryAllowedArticulationSlotNameIds, articulationSlotNameId);
                        removePrimaryAllowedArticulationSlotFilter(articulationSlotNameId);
                    }
                } else if (!alreadyClaimed) {
                    DrumKitEngineMapHelpers::appendUniqueId(claimedArticulationSlotNameIds, articulationSlotNameId);
                    articulationLibraryMap[articulationSlotNameId] = resolved.instSpec;
                    DrumKitEngineMapHelpers::appendUniqueId(primaryAllowedArticulationSlotNameIds, articulationSlotNameId);
                    if (!primary.effectiveArticulationSlotFilters.empty()) appendPrimaryAllowedArticulationSlotFilter(articulationSlotName);
                    // Propagate the cross-articulation remap to the primary instrument player.
                    auto mapLayerRemap = buildArtNameRemap(*resolved.mapLayer);
                    auto remapIt = mapLayerRemap.find(articulationSlotNameId);
                    if (remapIt != mapLayerRemap.end()) primaryRemap[articulationSlotNameId] = remapIt->second;
                    auto mapLayerExactRemap = buildArtExactRemap(*resolved.mapLayer);
                    primaryExactRemap.insert(mapLayerExactRemap.begin(), mapLayerExactRemap.end());
                } else {
                    layeredArticulationSlots.push_back(articulationSlotName);
                }
            }

            if (!layeredArticulationSlots.empty())
                shadowPlans.push_back({ resolved.mapLayer, resolved.instSpec, layeredArticulationSlots, buildArtNameRemap(*resolved.mapLayer), buildArtExactRemap(*resolved.mapLayer) });
        }

        uint32_t fallbackArticulationSlotNameId = 0;
        InstrumentPlayer::ArticulationTarget fallbackLibraryArticulation;
        if (slotLayout.controllers.empty()) {
            for (const auto& primaryArticulationSlotName : primary.effectiveArticulationSlotFilters) {
                if (primaryArticulationSlotName.isEmpty()) continue;
                fallbackArticulationSlotNameId = StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(primaryArticulationSlotName));
                fallbackLibraryArticulation.nameId = DrumKitEngineMapHelpers::resolveMapLayerLibraryArticulationId(*primary.mapLayer, primaryArticulationSlotName);
                if (!primary.mapLayer->articulationFilter.empty()) {
                    if (primary.mapLayer->slotArticulationFilter.empty()) {
                        const KitLayout::InstrumentSlot::ArticulationSlot libraryArticulation(primary.mapLayer->articulationFilter.front());
                        fallbackLibraryArticulation.exprId = libraryArticulation.exprId;
                    } else {
                        for (size_t artIdx = 0; artIdx < primary.mapLayer->slotArticulationFilter.size() && artIdx < primary.mapLayer->articulationFilter.size(); ++artIdx) {
                            if (!ArticulationNameHelpers::mapFilterMatchesArticulation(primary.mapLayer->slotArticulationFilter[artIdx], primaryArticulationSlotName)) continue;
                            const KitLayout::InstrumentSlot::ArticulationSlot libraryArticulation(primary.mapLayer->articulationFilter[artIdx]);
                            fallbackLibraryArticulation.exprId = libraryArticulation.exprId;
                            break;
                        }
                    }
                }
                break;
            }
        }

        // Simple non-controller slots inherit empty articulations from the first mapped art.
        // Controller-driven slots keep their existing primary fallback so CC tables remain intact.
        for (const auto& articulationSlot : slotLayout.articulations) {
            if (vectorContains(claimedArticulationSlotNameIds, articulationSlot.nameId)) continue;
            if (fallbackLibraryArticulation.nameId != 0) artAliases[articulationSlot.nameId] = fallbackLibraryArticulation;
            else DrumKitEngineMapHelpers::appendUniqueId(primaryAllowedArticulationSlotNameIds, articulationSlot.nameId);
        }

        if (LOG_ENABLED(Info)) {
            juce::String allowedStr;
            for (const auto& articulationSlotNameId : primaryAllowedArticulationSlotNameIds)
                allowedStr += (allowedStr.isEmpty() ? "" : ",") + StringsDictSingleton::nameOf(articulationSlotNameId);
            LOG(Info, "  slot=" + slotId + " primaryAllowed(final)=[" + allowedStr + "]");
            if (!primaryRemap.empty()) {
                juce::String remapStr;
                for (const auto& [fromId, toId] : primaryRemap)
                    remapStr += (remapStr.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(fromId) + "->" + StringsDictSingleton::nameOf(toId);
                LOG(Info, "  primaryRemap: [" + remapStr + "]");
            }
            if (!articulationLibraryMap.empty()) {
                juce::String libraryMapText;
                for (const auto& [artId, instPtr] : articulationLibraryMap)
                    libraryMapText += (libraryMapText.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(artId) + "->" + instPtr->meta.codeRaw;
                LOG(Info, "  articulationLibraryMap: [" + libraryMapText + "]");
            }
        }

        PreparedSlot prep;
        prep.slotId = slotId;
        auto pIdxIt = primarySlotIndex.find(slotId);
        prep.primaryIndex = (pIdxIt != primarySlotIndex.end()) ? pIdxIt->second : -1;
        if (!slotChanged && slotStateChanged && prep.primaryIndex >= 0) {
            prep.primaryParameterMapLayer = primary.mapLayer;
            prep.primaryParameterRoutes = buildTargetRoutes(slotLayout, primary.effectiveArticulationSlotFilters);
        }

        const auto existingShadowIt = existingShadowsBySlotId.find(slotId);
        const bool slotHasExistingShadows = existingShadowIt != existingShadowsBySlotId.end() && !existingShadowIt->second.empty();
        const bool slotHasShadowPlans = !shadowPlans.empty();
        bool canReuseExistingShadows = slotHasExistingShadows && slotHasShadowPlans && !slotChanged && existingShadowIt->second.size() == shadowPlans.size();
        std::vector<bool> matchedExistingShadows(slotHasExistingShadows ? existingShadowIt->second.size() : 0, false);
        std::vector<PreparedShadowParameterUpdate> shadowParameterUpdates;

        if (canReuseExistingShadows) {
            for (const auto& shadowPlan : shadowPlans) {
                const auto reloadFingerprint = makeResolvedMapLayerReloadFingerprint(*shadowPlan.mapLayer, *shadowPlan.instSpec);
                int matchedShadowIndex = -1;
                for (int idx = 0; idx < static_cast<int>(existingShadowIt->second.size()); ++idx) {
                    if (matchedExistingShadows[static_cast<size_t>(idx)] || existingShadowIt->second[static_cast<size_t>(idx)].reloadFingerprint != reloadFingerprint) continue;
                    matchedShadowIndex = idx;
                    break;
                }

                if (matchedShadowIndex < 0) {
                    canReuseExistingShadows = false;
                    break;
                }

                matchedExistingShadows[static_cast<size_t>(matchedShadowIndex)] = true;
                const auto stateFingerprint = makeResolvedMapLayerFingerprint(*shadowPlan.mapLayer, *shadowPlan.instSpec);
                const auto& existingShadow = existingShadowIt->second[static_cast<size_t>(matchedShadowIndex)];
                if (existingShadow.stateFingerprint != stateFingerprint)
                    shadowParameterUpdates.push_back({ existingShadow.instrumentPlayer, shadowPlan.mapLayer, stateFingerprint });
            }
        }

        prep.rebuildShadows = slotHasShadowPlans && (!canReuseExistingShadows || slotChanged);
        prep.removeOldShadows = slotHasExistingShadows && (prep.rebuildShadows || (!slotHasShadowPlans && (slotChanged || slotStateChanged)));
        if (canReuseExistingShadows) prep.shadowParameterUpdates = std::move(shadowParameterUpdates);

        // Create a replacement instrument player for the primary slot outside the lock.
        if (slotChanged && prep.primaryIndex >= 0) {
            auto replacement = std::make_unique<InstrumentPlayer>(this, slotLayout);
            replacement->setMicOutputMapping(primarySlot->getMicOutputMapping());
            replacement->setMicChannelCount(primarySlot->getMicChannelCount());
            const bool primaryStrictArticulationResolution = !primaryAllowedArticulationSlotFilters.empty() || !primaryRemap.empty() || !primaryExactRemap.empty();
            InstrumentPlayer::RoutingContext primaryRoutingContext;
            primaryRoutingContext.defaultInstrument = primary.instSpec;
            primaryRoutingContext.articulationLibraryMap = articulationLibraryMap;
            primaryRoutingContext.allowedArticulationNameIds = primaryAllowedArticulationSlotNameIds;
            primaryRoutingContext.allowedArticulationFilters = primaryAllowedArticulationSlotFilters;
            primaryRoutingContext.artNameRemap = primaryRemap;
            primaryRoutingContext.artExactRemap = primaryExactRemap;
            primaryRoutingContext.artAliases = artAliases;
            primaryRoutingContext.micRouteOverrides = primary.mapLayer->micRouteOverrides;
            primaryRoutingContext.strictArticulationResolution = primaryStrictArticulationResolution;
            auto primaryPlaybackEntries = replacement->buildArticulationPlaybackEntries(slotLayout, primaryRoutingContext);
            replacement->loadInstrumentWithPlaybackEntries(primary.instSpec, primaryRoutingContext, std::move(primaryPlaybackEntries), librarySnapshot);
            applyPrimaryOverrides(*replacement, *primary.mapLayer, buildTargetRoutes(slotLayout, primary.effectiveArticulationSlotFilters));
            LOG(Info, "  PRIMARY loaded: slot=" + slotId + " inst=" + primary.instSpec->meta.codeRaw
                + " tuning=" + juce::String(primary.mapLayer->tuningCents.value_or(0))
                + (primary.mapLayer->attackTimeMs.has_value() ? " attack=" + juce::String(*primary.mapLayer->attackTimeMs) : "")
                + (primary.mapLayer->decayTimeMs.has_value() ? " decay=" + juce::String(*primary.mapLayer->decayTimeMs) : ""));
            if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
                replacement->setCurrentPlaybackSampleRate(this->lastSampleRate);
                replacement->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
            }
            prep.primaryReplacement = std::move(replacement);
            ++applied;
        }

        // Create shadow instrument players outside the lock.
        if (prep.rebuildShadows) {
            for (const auto& shadowPlan : shadowPlans) {
                auto shadow = std::make_unique<InstrumentPlayer>(this, slotLayout);
                auto* shadowRaw = shadow.get();
                const auto shadowArticulationSlotNameIds = ArticulationNameHelpers::toArticulationIdList(shadowPlan.articulationSlots);
                shadowRaw->setMicOutputMapping(primarySlot->getMicOutputMapping());
                shadowRaw->setMicChannelCount(primarySlot->getMicChannelCount());
                const bool shadowStrictArticulationResolution = !shadowPlan.articulationSlots.empty() || !shadowPlan.artNameRemap.empty() || !shadowPlan.artExactRemap.empty();
                InstrumentPlayer::ArticulationAliasMap shadowAliases;
                if (fallbackArticulationSlotNameId != 0 && vectorContains(shadowArticulationSlotNameIds, fallbackArticulationSlotNameId)) shadowAliases = artAliases;
                InstrumentPlayer::RoutingContext shadowRoutingContext;
                shadowRoutingContext.defaultInstrument = shadowPlan.instSpec;
                shadowRoutingContext.allowedArticulationNameIds = shadowArticulationSlotNameIds;
                shadowRoutingContext.allowedArticulationFilters = shadowPlan.articulationSlots;
                shadowRoutingContext.artNameRemap = shadowPlan.artNameRemap;
                shadowRoutingContext.artExactRemap = shadowPlan.artExactRemap;
                shadowRoutingContext.artAliases = shadowAliases;
                shadowRoutingContext.micRouteOverrides = shadowPlan.mapLayer->micRouteOverrides;
                shadowRoutingContext.strictArticulationResolution = shadowStrictArticulationResolution;
                auto shadowPlaybackEntries = shadowRaw->buildArticulationPlaybackEntries(slotLayout, shadowRoutingContext);
                shadowRaw->loadInstrumentWithPlaybackEntries(shadowPlan.instSpec, shadowRoutingContext, std::move(shadowPlaybackEntries), librarySnapshot);
                applyOverrides(*shadowRaw, *shadowPlan.mapLayer);
                if (LOG_ENABLED(Info)) {
                    juce::String shadowArtsStr;
                    for (const auto& articulationSlotName : shadowPlan.articulationSlots) shadowArtsStr += (shadowArtsStr.isEmpty() ? "" : ",") + articulationSlotName;
                    juce::String remapStr;
                    for (const auto& [fromId, toId] : shadowPlan.artNameRemap) remapStr += (remapStr.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(fromId) + "->" + StringsDictSingleton::nameOf(toId);
                    LOG(Info, "  SHADOW loaded: slot=" + slotId + " inst=" + shadowPlan.instSpec->meta.codeRaw
                        + " arts=[" + shadowArtsStr + "]"
                        + (remapStr.isNotEmpty() ? " remap=[" + remapStr + "]" : "")
                        + " tuning=" + juce::String(shadowPlan.mapLayer->tuningCents.value_or(0)));
                }

                if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
                    shadowRaw->setCurrentPlaybackSampleRate(this->lastSampleRate);
                    shadowRaw->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
                }

                PreparedShadow ps;
                ps.stateFingerprint = makeResolvedMapLayerFingerprint(*shadowPlan.mapLayer, *shadowPlan.instSpec);
                ps.reloadFingerprint = makeResolvedMapLayerReloadFingerprint(*shadowPlan.mapLayer, *shadowPlan.instSpec);
                for (const auto& articulationSlot : slotLayout.articulations) {
                    if (!shadowRaw->routesArticulationSlot(articulationSlot.nameId, articulationSlot.exprId)) continue;
                    for (const int note : articulationSlot.notes)
                        ps.noteRoutes.push_back({note, shadowRaw});
                }
                ps.instrumentPlayer = std::move(shadow);
                prep.shadows.push_back(std::move(ps));
                ++applied;
            }
        }

        preparedSlots.push_back(std::move(prep));
    }

    // ======================== PHASE 3: SWAP (brief lock) ========================
    // The lock is held only for fast pointer swaps and routing table updates.
    // Old instrument players are collected and destroyed after the lock is released.
    std::vector<std::unique_ptr<InstrumentPlayer>> oldPlayers;
    {
        const juce::ScopedLock graphLock(this->engineGraphLock);

        std::unordered_set<juce::String> shadowSlotIdsToRemove;
        for (const auto& prep : preparedSlots) {
            if (prep.removeOldShadows) shadowSlotIdsToRemove.insert(prep.slotId);
        }

        std::unordered_set<InstrumentPlayer*> removedShadowPlayers;
        if (!shadowSlotIdsToRemove.empty()) {
            for (int idx = static_cast<int>(this->instrumentPlayerPtrs.size()) - 1; idx >= this->primaryInstrumentPlayerCount; --idx) {
                auto* shadowPlayer = this->instrumentPlayerPtrs[static_cast<size_t>(idx)].get();
                if (shadowPlayer == nullptr || shadowSlotIdsToRemove.find(shadowPlayer->getId()) == shadowSlotIdsToRemove.end()) continue;
                removedShadowPlayers.insert(shadowPlayer);
                this->lastAppliedShadowFingerprints.erase(shadowPlayer);
                this->lastAppliedShadowReloadFingerprints.erase(shadowPlayer);
                oldPlayers.push_back(std::move(this->instrumentPlayerPtrs[static_cast<size_t>(idx)]));
                this->instrumentPlayerPtrs.erase(this->instrumentPlayerPtrs.begin() + idx);
            }
        }

        // Swap in prepared primary replacements (pointer swap only)
        bool anyPrimarySwapped = false;
        for (auto& prep : preparedSlots) {
            if (prep.primaryReplacement && prep.primaryIndex >= 0
                && prep.primaryIndex < static_cast<int>(this->instrumentPlayerPtrs.size())) {
                const auto primaryIndex = static_cast<size_t>(prep.primaryIndex);
                oldPlayers.push_back(std::move(this->instrumentPlayerPtrs[primaryIndex]));
                this->instrumentPlayerPtrs[primaryIndex] = std::move(prep.primaryReplacement);
                this->instrumentPlayerById[prep.slotId] = this->instrumentPlayerPtrs[primaryIndex].get();
                anyPrimarySwapped = true;
            } else if (prep.primaryParameterMapLayer != nullptr && prep.primaryIndex >= 0 && prep.primaryIndex < static_cast<int>(this->instrumentPlayerPtrs.size())) {
                applyPrimaryOverrides(*this->instrumentPlayerPtrs[static_cast<size_t>(prep.primaryIndex)], *prep.primaryParameterMapLayer, prep.primaryParameterRoutes);
                ++applied;
            }

            for (const auto& shadowUpdate : prep.shadowParameterUpdates) {
                if (shadowUpdate.instrumentPlayer == nullptr || shadowUpdate.mapLayer == nullptr) continue;
                applyOverrides(*shadowUpdate.instrumentPlayer, *shadowUpdate.mapLayer);
                this->lastAppliedShadowFingerprints[shadowUpdate.instrumentPlayer] = shadowUpdate.stateFingerprint;
                ++applied;
            }
        }

        if (anyPrimarySwapped) {
            this->rebuildPrimaryInstrumentNoteRouting();
            if (this->activeKitLayoutPtr != nullptr) {
                for (int idx = this->primaryInstrumentPlayerCount; idx < static_cast<int>(this->instrumentPlayerPtrs.size()); ++idx) {
                    auto* shadowPlayer = this->instrumentPlayerPtrs[static_cast<size_t>(idx)].get();
                    if (shadowPlayer == nullptr) continue;
                    const auto layoutIt = std::find_if(this->activeKitLayoutPtr->instrumentSlots.begin(), this->activeKitLayoutPtr->instrumentSlots.end(), [&shadowPlayer](const KitLayout::InstrumentSlot& slot) { return slot.id == shadowPlayer->getId(); });
                    if (layoutIt != this->activeKitLayoutPtr->instrumentSlots.end()) this->addShadowPlayerNoteRoutes(*shadowPlayer, *layoutIt);
                }
            }
        } else if (!removedShadowPlayers.empty()) {
            for (auto& [note, instrumentPlayers] : this->instrumentPlayersByNote) {
                juce::ignoreUnused(note);
                std::erase_if(instrumentPlayers, [&removedShadowPlayers](InstrumentPlayer* instrumentPlayer) { return removedShadowPlayers.find(instrumentPlayer) != removedShadowPlayers.end(); });
            }
        }

        // Add prepared shadow instrument players and their note routes.
        for (auto& prep : preparedSlots) {
            for (auto& ps : prep.shadows) {
                for (const auto& [note, instrumentPlayerPtr] : ps.noteRoutes)
                    this->instrumentPlayersByNote[note].push_back(instrumentPlayerPtr);
                auto* shadowPlayer = ps.instrumentPlayer.get();
                this->lastAppliedShadowFingerprints[shadowPlayer] = ps.stateFingerprint;
                this->lastAppliedShadowReloadFingerprints[shadowPlayer] = ps.reloadFingerprint;
                this->instrumentPlayerPtrs.push_back(std::move(ps.instrumentPlayer));
            }
        }

        this->lastAppliedSlotFingerprints = std::move(nextSlotFingerprints);
        this->lastAppliedSlotReloadFingerprints = std::move(nextSlotReloadFingerprints);
        this->lastAppliedInstrumentMapText = normalizedMapText;
    }
    // Old instrument players are destroyed here outside the lock. The audio thread cannot access them.

    if (applied > 0 && this->sampleLoaderPtr != nullptr)
        this->sampleLoaderPtr->getQueue().startProcessing();

    if (applied <= 0 && statusLines.isEmpty())
        statusLines.add("Instrument map unchanged - no reload needed");

    if (statusLines.isEmpty())
        statusLines.add("Instrument map applied successfully");

    this->lastInstrumentMapStatus = statusLines.joinIntoString("\n");
    LOG(Info, "=== useInstrumentMap done: applied=" + juce::String(applied) + " oldPlayers=" + juce::String(oldPlayers.size())
        + " totalPlayers=" + juce::String(this->instrumentPlayerPtrs.size()) + " ===");
    return applied > 0;
}

ParseResult<DrumKitEngine::InstrumentMap> DrumKitEngine::parseInstrumentMapText(const juce::String& text) {
    return DrumKitEngineMappingModel::parseInstrumentMapText(text);
}
