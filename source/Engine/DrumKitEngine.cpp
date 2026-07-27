#include "DrumKitEngine.h"
#include "DrumKitEngineMappingModel.h"
#include "DrumKitEngineTextView.h"
#include "ArticulationNameHelpers.h"
#include "DrumKitEngineMapHelpers.h"
#include "DrumKitEnginePreviewHelpers.h"
#include "InstrumentSynth.h"
#include "SampleSelector.h"
#include "../Config/Helpers.h"
#include "../Config/YamlHelper.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>
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

static juce::String joinNames(const std::vector<juce::String>& names) {
    juce::StringArray values;
    for (const auto& name : names)
        if (name.trim().isNotEmpty()) values.add(name.trim());
    return values.joinIntoString(",");
}

static juce::String makeEntrySortKey(const DrumKitEngine::InstrumentMapEntry& entry) {
    return entry.slotId.trim().toLowerCase() + "/" + joinNames(entry.slotArticulationFilter).toLowerCase();
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
    const auto reason = matchCount > 1 ? "Kit id is ambiguous" : "Kit not found";
    return juce::String(reason) + ": " + kitRef + " for slot '" + slotId + "'";
}

static juce::String describeInstrumentResolveFailure(const KitLib::DrumKitSpec& kitSpec, const juce::String& instCode, const juce::String& slotId) {
    const auto matchCount = countInstrumentIdMatches(kitSpec, instCode);
    const auto reason = matchCount > 1 ? "Instrument id is ambiguous" : "Instrument not found";
    return juce::String(reason) + ": " + kitSpec.meta.codeRaw + "/" + instCode + " for slot '" + slotId + "'";
}

static juce::String describeArticulationResolveFailure(const KitLib::InstrumentSpec& instrumentSpec, const juce::String& artCode, const juce::String& targetPath, const juce::String& slotId) {
    const auto matchCount = countArticulationIdMatches(instrumentSpec, artCode);
    const auto reason = matchCount > 1 ? "Source articulation id is ambiguous" : "Unknown source articulation";
    return juce::String(reason) + ": " + artCode + " for target '" + targetPath + "' in slot '" + slotId + "'";
}

static void normalizeArticulationFilters(DrumKitEngine::InstrumentMapEntry& entry, const KitLib::InstrumentSpec& instrumentSpec) {
    for (auto& artName : entry.articulationFilter) {
        auto* artSpec = instrumentSpec.findArticulationById(artName);
        if (artSpec != nullptr) artName = instrumentSpec.getPreferredArticulationId(*artSpec);
    }
}

static void normalizeInstrumentMapEntries(DrumKitEngine::InstrumentMap& instrumentMap, KitLib::LibraryManager& libraryManager) {
    for (auto& entry : instrumentMap.entries) {
        auto* kitSpec = libraryManager.findKitById(entry.kitRef);
        if (kitSpec == nullptr) continue;

        entry.kitRef = libraryManager.getPreferredKitId(*kitSpec);
        if (entry.instCode.isEmpty()) continue;

        auto* instSpec = kitSpec->findInstrumentById(entry.instCode);
        if (instSpec == nullptr) continue;

        entry.instCode = kitSpec->getPreferredInstrumentId(*instSpec);
        normalizeArticulationFilters(entry, *instSpec);
    }
}

} // namespace DrumKitEngineIdentityMapHelpers

namespace DrumKitEngineVelocityResponseHelpers {

static juce::String safePresetName(const juce::String& name) {
    return juce::File::createLegalFileName(name.trim()).replaceCharacters(":#\r\n\"", "_____").trim();
}

} // namespace DrumKitEngineVelocityResponseHelpers

namespace DrumKitEngineSourceOverrideHelpers {

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

static std::vector<KitLib::SampleVariantSpec*> findSamplesByRelativePath(KitLib::DrumKitSpec& kit, KitLib::ArticulationSpec& articulation, const juce::String& relativePath) {
    std::vector<KitLib::SampleVariantSpec*> matches;
    const auto normalizedPath = relativePath.trim().replaceCharacter('\\', '/');
    for (const auto& layer : articulation.velocityLayerSpecs) {
        if (layer == nullptr) continue;
        for (const auto& sample : layer->sampleVariantSpecs) {
            if (sample != nullptr && relativeSamplePath(kit, sample->sampleFile) == normalizedPath) matches.push_back(sample.get());
        }
    }
    return matches;
}

static juce::String firstContentHash(const std::vector<KitLib::SampleVariantSpec*>& samples) {
    for (const auto* sample : samples)
        if (sample != nullptr && sample->metadataContentHash.isNotEmpty()) return sample->metadataContentHash;
    return {};
}

} // namespace DrumKitEngineSourceOverrideHelpers

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

DrumKitEngine::VelocityResponseSourceInfo DrumKitEngine::getSourceVelocityResponseInfo(const juce::String& sourcePath) const {
    VelocityResponseSourceInfo result;
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

    result.sourceFound = true;
    std::optional<float> hardestFittedDb;
    for (auto layerIt = articulation->velocityLayerSpecs.rbegin(); layerIt != articulation->velocityLayerSpecs.rend(); ++layerIt) {
        if (*layerIt != nullptr && (*layerIt)->fittedSourceDb.has_value()) {
            hardestFittedDb = (*layerIt)->fittedSourceDb;
            break;
        }
    }

    result.levels.reserve(articulation->velocityLayerSpecs.size());
    for (const auto& layer : articulation->velocityLayerSpecs) {
        if (layer == nullptr) continue;
        VelocityResponseLevelInfo level;
        level.velocity = layer->velocity;
        if (hardestFittedDb.has_value() && layer->rawVelocityMedianDb.has_value()) level.rawMedianRelativeDb = *layer->rawVelocityMedianDb - *hardestFittedDb;
        if (hardestFittedDb.has_value() && layer->fittedSourceDb.has_value()) level.fittedRelativeDb = *layer->fittedSourceDb - *hardestFittedDb;
        level.calibrationTrimDb = layer->calibrationTrimDb;
        level.calibrationLimited = layer->calibrationLimited;
        result.levels.push_back(std::move(level));
    }

    return result;
}

DrumKitEngine::SourceVelocityRangeInfo DrumKitEngine::getSourceVelocityRangeInfo(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId) const {
    SourceVelocityRangeInfo result;
    if (this->libraryManagerPtr == nullptr) return result;
    const juce::ScopedLock sourceLock(this->sourceModelLock);
    const juce::ScopedLock graphLock(this->engineGraphLock);
    const auto* kit = this->libraryManagerPtr->findKitById(kitId);
    const auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    const auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result;

    result.sourceFound = true;
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
    const auto* articulationOverride = kit->sourceOverrides.findArticulationOverride(instrument->meta.codeRaw, articulation->codeRaw);
    result.levels.reserve(articulation->velocityLayerSpecs.size());
    for (size_t layerIdx = 0; layerIdx < articulation->velocityLayerSpecs.size(); ++layerIdx) {
        const auto& layer = articulation->velocityLayerSpecs[layerIdx];
        if (layer == nullptr) continue;
        SourceVelocityRangeLevelInfo level;
        level.namedVelocity = layer->velocity;
        level.filenameUpperBoundary = layerIdx + 1 == articulation->velocityLayerSpecs.size() ? MIDI_MAX_VALUE : layer->velocity;
        level.effectiveUpperBoundary = layer->selectionUpperVelocity;
        level.overridden = articulationOverride != nullptr && articulationOverride->velocityUpperBounds.find(layer->velocity) != articulationOverride->velocityUpperBounds.end();
        result.levels.push_back(level);

        for (const auto& sample : layer->sampleVariantSpecs) {
            if (sample == nullptr) continue;
            SourceSampleOverrideInfo sampleInfo;
            sampleInfo.namedVelocity = layer->velocity;
            sampleInfo.roundRobin = sample->letter == '\0' ? "-" : juce::String::charToString(sample->letter);
            sampleInfo.relativePath = DrumKitEngineSourceOverrideHelpers::relativeSamplePath(*kit, sample->sampleFile);
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
            const auto sampleOverrideMatch = kit->sourceOverrides.findSampleOverride(sampleInfo.relativePath, sampleInfo.contentHash);
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
            for (const auto& synth : this->instrumentSynthPtrs) {
                if (synth == nullptr) continue;
                const auto synthStatus = synth->getSourceRecipeStatus(*sample);
                if (DrumKitEngineSourceOverrideHelpers::recipeStatusPriority(synthStatus) > DrumKitEngineSourceOverrideHelpers::recipeStatusPriority(sampleInfo.recipeStatus))
                    sampleInfo.recipeStatus = synthStatus;
            }
            result.samples.push_back(std::move(sampleInfo));
        }
    }
    return result;
}

ParseResult<DrumKitEngine::SourceVelocityRangeInfo> DrumKitEngine::setSourceVelocityUpperBoundary(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId,
                                                                                                  int namedVelocity, std::optional<int> upperBoundary, bool persist) {
    ParseResult<SourceVelocityRangeInfo> result;
    if (namedVelocity < 1 || namedVelocity > MIDI_MAX_VALUE) return result.returnWithError("Named velocity must be 1..127");
    if (upperBoundary.has_value() && (*upperBoundary < 1 || *upperBoundary >= MIDI_MAX_VALUE)) return result.returnWithError("Upper boundary must be 1..126");
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");

    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result.returnWithError("Source articulation was not found");
    if (persist && !kit->meta.rootDir.hasWriteAccess()) return result.returnWithError("Kit folder is read-only");

    const auto layerIt = std::find_if(articulation->velocityLayerSpecs.begin(), articulation->velocityLayerSpecs.end(), [namedVelocity](const auto& layer) {
        return layer != nullptr && layer->velocity == namedVelocity;
    });
    if (layerIt == articulation->velocityLayerSpecs.end()) return result.returnWithError("Named velocity was not found in the source articulation");
    if (layerIt + 1 == articulation->velocityLayerSpecs.end()) return result.returnWithError("The last velocity layer always ends at 127");

    auto candidateOverrides = kit->sourceOverrides;
    std::optional<uint8_t> typedUpperBoundary;
    if (upperBoundary.has_value()) typedUpperBoundary = static_cast<uint8_t>(*upperBoundary);
    candidateOverrides.setVelocityUpperBoundary(instrument->meta.codeRaw, articulation->codeRaw, static_cast<uint8_t>(namedVelocity), typedUpperBoundary);

    std::vector<uint8_t> namedVelocities;
    namedVelocities.reserve(articulation->velocityLayerSpecs.size());
    for (const auto& layer : articulation->velocityLayerSpecs) namedVelocities.push_back(layer->velocity);
    const auto resolved = candidateOverrides.resolveVelocityUpperBoundaries(instrument->meta.codeRaw, articulation->codeRaw, namedVelocities);
    if (!resolved.ok()) return result.returnWithError(resolved.error());

    if (!persist) {
        {
            const juce::ScopedLock graphLock(this->engineGraphLock);
            for (auto& synth : this->instrumentSynthPtrs)
                if (synth != nullptr) synth->refreshSourceVelocityBoundaries(*articulation, resolved.value);
        }
        result.value = this->getSourceVelocityRangeInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
        const auto* candidateArticulationOverride = candidateOverrides.findArticulationOverride(instrument->meta.codeRaw, articulation->codeRaw);
        for (size_t layerIdx = 0; layerIdx < result.value.levels.size(); ++layerIdx) {
            auto& level = result.value.levels[layerIdx];
            level.effectiveUpperBoundary = resolved.value[layerIdx];
            level.overridden = candidateArticulationOverride != nullptr && candidateArticulationOverride->velocityUpperBounds.find(static_cast<uint8_t>(level.namedVelocity)) != candidateArticulationOverride->velocityUpperBounds.end();
        }
        return result;
    }

    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());

    this->invalidateSourcePreviewCache();
    {
        const juce::ScopedLock sourceLock(this->sourceModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        kit->sourceOverrides = std::move(candidateOverrides);
        for (size_t layerIdx = 0; layerIdx < articulation->velocityLayerSpecs.size(); ++layerIdx) articulation->velocityLayerSpecs[layerIdx]->selectionUpperVelocity = resolved.value[layerIdx];
        for (auto& synth : this->instrumentSynthPtrs)
            if (synth != nullptr) synth->refreshSourceVelocityBoundaries(*articulation);
    }

    result.value = this->getSourceVelocityRangeInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
    for (const auto& warning : resolved.errors) result.addWarning(warning);
    return result;
}

ParseResult<DrumKitEngine::SourceVelocityRangeInfo> DrumKitEngine::resetSourceVelocityUpperBoundaries(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId) {
    ParseResult<SourceVelocityRangeInfo> result;
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");

    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result.returnWithError("Source articulation was not found");
    if (!kit->meta.rootDir.hasWriteAccess()) return result.returnWithError("Kit folder is read-only");

    auto candidateOverrides = kit->sourceOverrides;
    candidateOverrides.resetVelocityUpperBoundaries(instrument->meta.codeRaw, articulation->codeRaw);
    std::vector<uint8_t> namedVelocities;
    namedVelocities.reserve(articulation->velocityLayerSpecs.size());
    for (const auto& layer : articulation->velocityLayerSpecs) namedVelocities.push_back(layer->velocity);
    const auto resolved = candidateOverrides.resolveVelocityUpperBoundaries(instrument->meta.codeRaw, articulation->codeRaw, namedVelocities);
    if (!resolved.ok()) return result.returnWithError(resolved.error());

    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());

    this->invalidateSourcePreviewCache();
    {
        const juce::ScopedLock sourceLock(this->sourceModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        kit->sourceOverrides = std::move(candidateOverrides);
        for (size_t layerIdx = 0; layerIdx < articulation->velocityLayerSpecs.size(); ++layerIdx) articulation->velocityLayerSpecs[layerIdx]->selectionUpperVelocity = resolved.value[layerIdx];
        for (auto& synth : this->instrumentSynthPtrs)
            if (synth != nullptr) synth->refreshSourceVelocityBoundaries(*articulation);
    }

    result.value = this->getSourceVelocityRangeInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
    return result;
}

void DrumKitEngine::invalidateSourcePreviewCache() {
    this->previewTargetGeneration.fetch_add(1, std::memory_order_acq_rel);
    this->previewResolveThreadPool.removeAllJobs(true, 2000);
    const juce::ScopedLock resolutionLock(this->previewTargetResolutionLock);
    this->previewTargetCache.clear();
    this->pendingPreviewTargetKeys.clear();
}

ParseResult<DrumKitEngine::SourceVelocityRangeInfo> DrumKitEngine::setSourceInstrumentBalance(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, std::optional<int> balance) {
    return this->setSourceBalance(kitId, instrumentId, articulationId, balance, false);
}

ParseResult<DrumKitEngine::SourceVelocityRangeInfo> DrumKitEngine::setSourceArticulationBalance(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, std::optional<int> balance) {
    return this->setSourceBalance(kitId, instrumentId, articulationId, balance, true);
}

ParseResult<DrumKitEngine::SourceVelocityRangeInfo> DrumKitEngine::setSourceBalance(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, std::optional<int> balance, bool articulationScope) {
    ParseResult<SourceVelocityRangeInfo> result;
    if (balance.has_value() && (*balance < -127 || *balance > 127)) return result.returnWithError("Source balance must be L1..L127, C0, or R1..R127");
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");
    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result.returnWithError("Source articulation was not found");
    if (!kit->meta.rootDir.hasWriteAccess()) return result.returnWithError("Kit folder is read-only");

    auto candidateOverrides = kit->sourceOverrides;
    if (articulationScope) candidateOverrides.setArticulationBalance(instrument->meta.codeRaw, articulation->codeRaw, balance);
    else candidateOverrides.setInstrumentBalance(instrument->meta.codeRaw, balance);
    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());
    this->invalidateSourcePreviewCache();

    {
        const juce::ScopedLock sourceLock(this->sourceModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        kit->sourceOverrides = std::move(candidateOverrides);
        if (articulationScope) articulation->balanceOverride = balance;
        else instrument->balanceOverride = balance;
        for (const auto& synth : this->instrumentSynthPtrs) {
            if (synth == nullptr) continue;
            synth->refreshSourceBalance(*instrument, articulationScope ? articulation : nullptr);
        }
    }
    result.value = this->getSourceVelocityRangeInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
    return result;
}

ParseResult<DrumKitEngine::SourceVelocityRangeInfo> DrumKitEngine::setSourceSampleManualGain(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const juce::String& relativePath, std::optional<float> manualGainDb) {
    ParseResult<SourceVelocityRangeInfo> result;
    if (manualGainDb.has_value() && !std::isfinite(*manualGainDb)) return result.returnWithError("Manual gain must be a finite dB value");
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");
    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result.returnWithError("Source articulation was not found");
    if (!kit->meta.rootDir.hasWriteAccess()) return result.returnWithError("Kit folder is read-only");

    const auto matchingSamples = DrumKitEngineSourceOverrideHelpers::findSamplesByRelativePath(*kit, *articulation, relativePath);
    if (matchingSamples.empty()) return result.returnWithError("Source sample file was not found in this articulation");
    const auto contentHash = DrumKitEngineSourceOverrideHelpers::firstContentHash(matchingSamples);
    const auto existingOverrideMatch = kit->sourceOverrides.findSampleOverride(relativePath, contentHash);
    if (existingOverrideMatch.ambiguousContentHash) return result.returnWithError("Sample content hash matches more than one saved override. Reload after resolving the duplicate entries.");
    const auto overridePath = existingOverrideMatch.value != nullptr ? existingOverrideMatch.storedRelativePath : relativePath;
    auto candidateOverrides = kit->sourceOverrides;
    candidateOverrides.setSampleManualGain(overridePath, contentHash, manualGainDb);
    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());
    this->invalidateSourcePreviewCache();

    {
        const juce::ScopedLock sourceLock(this->sourceModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        kit->sourceOverrides = std::move(candidateOverrides);
        const auto* sampleOverride = kit->sourceOverrides.findSampleOverride(overridePath);
        const std::optional<float> effectiveOverride = sampleOverride != nullptr ? sampleOverride->manualGainDb : std::nullopt;
        for (auto* sample : matchingSamples) {
            const float previousGainDb = sample->manualGainDb;
            sample->manualGainDbOverride = effectiveOverride;
            sample->manualGainDb = sample->manualGainDbOverride.value_or(sample->filenameManualGainDb);
            const float gainDeltaDb = sample->manualGainDb - previousGainDb;
            sample->peakDb += gainDeltaDb;
            if (sample->decayEnvelope.isValid()) sample->decayEnvelope.peakDb += gainDeltaDb;
        }
        instrument->buildCalibrationProfiles();
        for (const auto& synth : this->instrumentSynthPtrs)
            if (synth != nullptr) synth->refreshSourceCalibrationAndReload(*articulation);
    }

    result.value = this->getSourceVelocityRangeInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
    return result;
}

ParseResult<DrumKitEngine::SourceVelocityRangeInfo> DrumKitEngine::setSourceSamplePlaybackOffset(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const juce::String& relativePath, const std::optional<juce::String>& micCode, std::optional<float> playbackOffsetMs) {
    ParseResult<SourceVelocityRangeInfo> result;
    if (playbackOffsetMs.has_value() && !std::isfinite(*playbackOffsetMs)) return result.returnWithError("Playback offset must be a finite millisecond value");
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");
    auto* kit = this->libraryManagerPtr->findKitById(kitId);
    auto* instrument = kit != nullptr ? kit->findInstrumentById(instrumentId) : nullptr;
    auto* articulation = instrument != nullptr ? instrument->findArticulationById(articulationId) : nullptr;
    if (kit == nullptr || instrument == nullptr || articulation == nullptr) return result.returnWithError("Source articulation was not found");
    if (!kit->meta.rootDir.hasWriteAccess()) return result.returnWithError("Kit folder is read-only");

    const auto matchingSamples = DrumKitEngineSourceOverrideHelpers::findSamplesByRelativePath(*kit, *articulation, relativePath);
    if (matchingSamples.empty()) return result.returnWithError("Source sample file was not found in this articulation");
    const auto normalizedMicCode = micCode.has_value() ? micCode->trim().toLowerCase() : juce::String();
    if (micCode.has_value() && normalizedMicCode.isEmpty()) return result.returnWithError("Source mic code is empty");
    if (micCode.has_value() && std::none_of(matchingSamples.begin(), matchingSamples.end(), [&normalizedMicCode](const auto* sample) {
        return sample != nullptr && sample->micSpec.raw.trim().toLowerCase() == normalizedMicCode;
    })) return result.returnWithError("Source mic was not found in this sample file");

    auto candidateOverrides = kit->sourceOverrides;
    const auto contentHash = DrumKitEngineSourceOverrideHelpers::firstContentHash(matchingSamples);
    const auto existingOverrideMatch = kit->sourceOverrides.findSampleOverride(relativePath, contentHash);
    if (existingOverrideMatch.ambiguousContentHash) return result.returnWithError("Sample content hash matches more than one saved override. Reload after resolving the duplicate entries.");
    const auto overridePath = existingOverrideMatch.value != nullptr ? existingOverrideMatch.storedRelativePath : relativePath;
    if (micCode.has_value()) candidateOverrides.setMicPlaybackOffset(overridePath, contentHash, normalizedMicCode, playbackOffsetMs);
    else candidateOverrides.setSamplePlaybackOffset(overridePath, contentHash, playbackOffsetMs);
    const auto saveResult = candidateOverrides.saveToKitRoot(kit->meta.rootDir);
    if (!saveResult.ok()) return result.returnWithError(saveResult.error());
    this->invalidateSourcePreviewCache();

    {
        const juce::ScopedLock sourceLock(this->sourceModelLock);
        const juce::ScopedLock graphLock(this->engineGraphLock);
        kit->sourceOverrides = std::move(candidateOverrides);
        const auto* sampleOverride = kit->sourceOverrides.findSampleOverride(overridePath);
        for (auto* sample : matchingSamples) {
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
            for (const auto& synth : this->instrumentSynthPtrs)
                if (synth != nullptr) synth->refreshSourcePlaybackOffset(*sample);
        }
    }

    result.value = this->getSourceVelocityRangeInfo(kit->meta.codeRaw, instrument->meta.codeRaw, articulation->codeRaw);
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
        if (sound->sampleVariantSpec != nullptr) {
            sourceChannelStart = sound->sampleVariantSpec->fileChannelStart;
            if (sound->sampleVariantSpec->fileChannelCount > 0) sourceChannelCount = sound->sampleVariantSpec->fileChannelCount;
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
        this->lastInstrumentMapStatus = "Mapped default kit and generated starter instrument map";//FIXME: refactor, ui relates shit out
    } else {
        this->suggestedInstrumentMapText.clear();
        this->lastAppliedSlotFingerprints.clear();
        this->lastAppliedSlotReloadFingerprints.clear();
        this->lastAppliedShadowFingerprints.clear();
        this->lastAppliedShadowReloadFingerprints.clear();
        this->lastAppliedInstrumentMapText.clear();
    }
}

juce::String DrumKitEngine::buildInstrumentMapForKit(const KitLayout& layout, const KitLib::DrumKitSpec& kit) const {
    InstrumentMap result;
    std::unordered_set<KitLib::InstrumentSpec*> usedInstruments;
    std::map<KitLib::InstrumentSpec::Type, int> nextIndexByType;

    for (const auto& slot : layout.instrumentSlots) {
        auto* matched = DrumKitEngineMapHelpers::findMatchingKitInstrument(slot, kit, usedInstruments, nextIndexByType);
        if (matched == nullptr) continue;
        InstrumentMapEntry entry;
        entry.slotId = slot.id;
        entry.kitRef = this->libraryManagerPtr != nullptr
            ? this->libraryManagerPtr->getPreferredKitId(kit)
            : DrumKitEngineIdentityMapHelpers::firstCandidateOrRaw(KitLib::DrumKitSpec::getIdentityCandidates(kit.meta), kit.meta.codeRaw);
        entry.instCode = kit.getPreferredInstrumentId(*matched);
        if (const auto inferredFlip = DrumKitEngineMapHelpers::inferAutomaticFlipLR(slot, *matched, entry))
            entry.flipLR = *inferredFlip;
        result.entries.push_back(std::move(entry));
    }

    return serializeInstrumentMap(result);
}

/** Writes instrument map text in normalized slot/layer order with semicolon-separated params. */
juce::String DrumKitEngine::serializeInstrumentMap(const InstrumentMap& instrumentMap) {
    juce::StringArray lines;

    auto joinNames = [](const std::vector<juce::String>& names) {
        juce::StringArray values;
        for (const auto& name : names)
            if (name.trim().isNotEmpty()) values.add(name.trim());
        return values.joinIntoString(",");
    };

    std::vector<const InstrumentMapEntry*> sortedEntries;
    sortedEntries.reserve(instrumentMap.entries.size());
    for (const auto& entry : instrumentMap.entries)
        sortedEntries.push_back(&entry);

    std::stable_sort(sortedEntries.begin(), sortedEntries.end(), [](const auto* leftEntry, const auto* rightEntry) {
        return DrumKitEngineIdentityMapHelpers::makeEntrySortKey(*leftEntry) < DrumKitEngineIdentityMapHelpers::makeEntrySortKey(*rightEntry);
    });

    for (const auto* entryPtr : sortedEntries) {
        const auto& entry = *entryPtr;
        if (entry.slotId.trim().isEmpty() || entry.kitRef.trim().isEmpty()) continue;

        juce::String slotRef = entry.slotId.trim();
        const auto slotArts = joinNames(entry.slotArticulationFilter);
        if (slotArts.isNotEmpty())
            slotRef += "/" + slotArts;

        juce::String targetRef = entry.kitRef.trim();
        if (entry.instCode.trim().isNotEmpty())
            targetRef += "/" + entry.instCode.trim();
        const auto sourceArts = joinNames(entry.articulationFilter);
        if (sourceArts.isNotEmpty())
            targetRef += "/" + sourceArts;

        juce::StringArray params;
        if (entry.tuningCents.has_value()) params.add("tuning=" + juce::String(*entry.tuningCents));
        if (entry.attackTimeMs.has_value()) params.add("attack=" + juce::String(*entry.attackTimeMs));
        if (entry.decayTimeMs.has_value()) params.add("decay=" + juce::String(*entry.decayTimeMs));
        if (entry.volumeDb.has_value()) params.add("vol=" + juce::String(*entry.volumeDb, 4).trimCharactersAtEnd("0").trimCharactersAtEnd("."));
        if (entry.pan.has_value()) params.add("pan=" + juce::String(*entry.pan));
        if (entry.velocityResponse.has_value()) params.add("velocityResponse=" + *entry.velocityResponse);
        if (entry.flipLR || entry.flipLRExplicit) params.add("flipLR=" + juce::String(entry.flipLR ? "1" : "0"));
        if (entry.flipPhase) params.add("flipPhase=1");
        if (!entry.micBleedLevels.empty()) {
            std::vector<juce::String> keys;
            keys.reserve(entry.micBleedLevels.size());
            for (const auto& [key, value] : entry.micBleedLevels) {
                juce::ignoreUnused(value);
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());
            juce::StringArray bleedValues;
            for (const auto& key : keys)
                bleedValues.add(key + ":" + juce::String(entry.micBleedLevels.at(key), 4).trimCharactersAtEnd("0").trimCharactersAtEnd("."));
            params.add("bleed=" + bleedValues.joinIntoString(","));
        }
        if (!entry.micRouteOverrides.empty()) {
            std::vector<juce::String> keys;
            keys.reserve(entry.micRouteOverrides.size());
            for (const auto& [key, value] : entry.micRouteOverrides) {
                juce::ignoreUnused(value);
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());
            juce::StringArray routeValues;
            for (const auto& key : keys)
                routeValues.add(key + ":" + entry.micRouteOverrides.at(key));
            params.add("micRoute=" + routeValues.joinIntoString(","));
        }

        auto line = slotRef + ":" + targetRef;
        if (!params.isEmpty())
            line += ":" + params.joinIntoString(";");
        lines.add(line);
    }

    return lines.joinIntoString("\n");
}

void DrumKitEngine::regenerateSuggestedInstrumentMapText() { //TODO: refactor, move to ui related helper, or generate on demand in UI instead of storing in engine
    this->suggestedInstrumentMapText = this->normalizeInstrumentMapText(DrumKitEngineTextView::buildSuggestedInstrumentMapText(this->activeKitLayoutPtr.get(), this->instrumentSynthById));
}

juce::String DrumKitEngine::normalizeInstrumentMapText(const juce::String& text) const {
    auto parsed = DrumKitEngine::parseInstrumentMapText(text);
    if (!parsed.ok()) return text;
    if (this->libraryManagerPtr != nullptr)
        DrumKitEngineIdentityMapHelpers::normalizeInstrumentMapEntries(parsed.value, *this->libraryManagerPtr);
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
    

    for (const auto& layoutInstrument : this->activeKitLayoutPtr->instrumentSlots) { //FIXME: rewrite loop thru instrumentSynthById
        auto sit = this->instrumentSynthById.find(layoutInstrument.id);
        if (sit == this->instrumentSynthById.end()) {
            LOG(Warning, "layout instrument slot not present: " + layoutInstrument.id);
            continue;
        }
        InstrumentSynth* slot = sit->second;

        auto* matched = DrumKitEngineMapHelpers::findMatchingKitInstrument(layoutInstrument, kit, usedInstSpecSet, nextIndexByType);

        if (matched == nullptr) {
            LOG(Warning, "no matching instrument found in kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");
            slot->unloadInstrument();
            continue;
        }

        LOG(Info, "Loading instrument '" + matched->meta.codeRaw + "' from kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");

        slot->loadInstrument(matched, librarySnapshot); //TODO: wrap in performance measuring block
    }

    if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
        for (const auto& instrumentSynthPtr : this->instrumentSynthPtrs) {
            instrumentSynthPtr->setCurrentPlaybackSampleRate(this->lastSampleRate);
            instrumentSynthPtr->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
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

    struct ExistingSynthState {
        std::vector<InstrumentSynth*> rawSynths;
        std::unordered_map<juce::String, int> primaryIndexById;
        std::unordered_map<juce::String, std::vector<int>> shadowIndicesBySlotId;
    } existingState;

    {
        const juce::ScopedLock graphLock(this->engineGraphLock);
        existingState.rawSynths.reserve(this->instrumentSynthPtrs.size());
        for (int idx = 0; idx < static_cast<int>(this->instrumentSynthPtrs.size()); ++idx) {
            auto* synth = this->instrumentSynthPtrs[static_cast<size_t>(idx)].get();
            existingState.rawSynths.push_back(synth);
            if (idx < this->primaryInstrumentSynthCount)
                existingState.primaryIndexById[synth->getId()] = idx;
            else
                existingState.shadowIndicesBySlotId[synth->getId()].push_back(idx);
        }
    }

    struct PreparedShadow {
        int oldIndex = -1;
        std::unique_ptr<InstrumentSynth> replacement;
    };

    struct PreparedSlot {
        KitLayout::InstrumentSlot layoutSlot;
        juce::String slotId;
        int oldPrimaryIndex = -1;
        std::unique_ptr<InstrumentSynth> primaryReplacement;
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
            auto* oldPrimary = existingState.rawSynths[static_cast<size_t>(prepared.oldPrimaryIndex)];
            if (!DrumKitEngineMapHelpers::canReuseSlotWithoutReload(oldPrimary->getInstrumentSlot(), layoutSlot))
                prepared.primaryReplacement = DrumKitEngineMapHelpers::createReplacementSynth(this, layoutSlot, *oldPrimary, this->lastSampleRate, this->lastSamplesPerBlock);

            auto shadowIt = existingState.shadowIndicesBySlotId.find(layoutSlot.id);
            if (shadowIt != existingState.shadowIndicesBySlotId.end()) {
                prepared.shadows.reserve(shadowIt->second.size());
                for (const int shadowIndex : shadowIt->second) {
                    PreparedShadow shadow;
                    shadow.oldIndex = shadowIndex;

                    auto* oldShadow = existingState.rawSynths[static_cast<size_t>(shadowIndex)];
                    if (!DrumKitEngineMapHelpers::canReuseSlotWithoutReload(oldShadow->getInstrumentSlot(), layoutSlot))
                        shadow.replacement = DrumKitEngineMapHelpers::createReplacementSynth(this, layoutSlot, *oldShadow, this->lastSampleRate, this->lastSamplesPerBlock);

                    prepared.shadows.push_back(std::move(shadow));
                }
            }
        } else {
            prepared.primaryReplacement = std::make_unique<InstrumentSynth>(this, layoutSlot);
            DrumKitEngineMapHelpers::prepareSynthIfNeeded(*prepared.primaryReplacement, this->lastSampleRate, this->lastSamplesPerBlock);
        }

        preparedSlots.push_back(std::move(prepared));
    }

    std::vector<std::unique_ptr<InstrumentSynth>> oldSynths;
    {
        const juce::ScopedLock graphLock(this->engineGraphLock);
        auto previousSynths = std::move(this->instrumentSynthPtrs);

        this->instrumentSynthPtrs.clear();
        this->instrumentSynthById.clear();
        this->primaryInstrumentSynthCount = 0;

        for (auto& prepared : preparedSlots) {
            if (prepared.oldPrimaryIndex >= 0 && !prepared.primaryReplacement) {
                this->instrumentSynthPtrs.push_back(std::move(previousSynths[static_cast<size_t>(prepared.oldPrimaryIndex)]));
            } else {
                this->instrumentSynthPtrs.push_back(std::move(prepared.primaryReplacement));
            }

            auto* inserted = this->instrumentSynthPtrs.back().get();
            this->instrumentSynthById[prepared.slotId] = inserted;
        }

        this->primaryInstrumentSynthCount = static_cast<int>(this->instrumentSynthPtrs.size());
        this->clearShadowSynths();

        for (auto& prepared : preparedSlots) {
            for (auto& shadow : prepared.shadows) {
                if (shadow.oldIndex >= 0 && !shadow.replacement) {
                    this->instrumentSynthPtrs.push_back(std::move(previousSynths[static_cast<size_t>(shadow.oldIndex)]));
                } else if (shadow.replacement) {
                    this->instrumentSynthPtrs.push_back(std::move(shadow.replacement));
                }

                auto* shadowSynth = this->instrumentSynthPtrs.back().get();
                
                this->addShadowSynthNoteRoutes(*shadowSynth, prepared.layoutSlot);
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

        for (auto& synth : previousSynths) {
            if (synth)
                oldSynths.push_back(std::move(synth));
        }
    }

    LOG(Info, "Applied kit layout: " + this->activeKitLayoutPtr->id
        + " (primary slots: " + juce::String(this->primaryInstrumentSynthCount)
        + ", recycled synths: " + juce::String(static_cast<int>(existingState.rawSynths.size()) - static_cast<int>(oldSynths.size()))
        + ", retired synths: " + juce::String(static_cast<int>(oldSynths.size())) + ")");

    this->buildNoteNameCache();
    return true;
}

void DrumKitEngine::rebuildPrimaryInstrumentNoteRouting() {
    this->instrumentSynthsByNote.clear();
    this->trackedCCMask.fill(false);
    this->latestPAValues.fill(0);
    this->trackedPAMask.fill(false);

    if (this->activeKitLayoutPtr == nullptr)
        return;

    for (const auto& instrumentSlot : this->activeKitLayoutPtr->instrumentSlots) {
        auto it = this->instrumentSynthById.find(instrumentSlot.id);
        if (it == this->instrumentSynthById.end())
            continue;

        auto* synth = it->second;

        // Rebuild the synth's internal note/CC routing from current layout data.
        synth->rebuildRoutingFromLayout(instrumentSlot);

        for (const auto& cc : instrumentSlot.controllers) {
            if (cc.number >= 0 && cc.number < 128)
                this->trackedCCMask[static_cast<size_t>(cc.number)] = true;
        }

        for (const auto& art : instrumentSlot.articulations) {
            const bool routesArt = DrumKitEngine::synthRoutesLayoutArticulation(*synth, art);
            for (const int note : art.notes) {
                if (note < 0 || note >= 128)
                    continue;
                this->trackedPAMask[static_cast<size_t>(note)] = true;
                if (routesArt) this->instrumentSynthsByNote[note].push_back(synth);
            }
        }
    }
}

void DrumKitEngine::clearShadowSynths() {
    if (static_cast<int>(this->instrumentSynthPtrs.size()) > this->primaryInstrumentSynthCount)
        this->instrumentSynthPtrs.erase(this->instrumentSynthPtrs.begin() + this->primaryInstrumentSynthCount, this->instrumentSynthPtrs.end());

    this->lastAppliedShadowFingerprints.clear();
    this->lastAppliedShadowReloadFingerprints.clear();
    this->rebuildPrimaryInstrumentNoteRouting();
}

void DrumKitEngine::rebuildMidiNoteRoutingFromLayout() {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    this->rebuildPrimaryInstrumentNoteRouting();

    if (this->activeKitLayoutPtr != nullptr) {
        for (int idx = this->primaryInstrumentSynthCount; idx < static_cast<int>(this->instrumentSynthPtrs.size()); ++idx) {
            auto* shadowSynth = this->instrumentSynthPtrs[static_cast<size_t>(idx)].get();
            if (shadowSynth == nullptr) continue;

            const KitLayout::InstrumentSlot* layoutSlot = nullptr;
            for (const auto& slot : this->activeKitLayoutPtr->instrumentSlots) {
                if (slot.id == shadowSynth->getId()) {
                    layoutSlot = &slot;
                    break;
                }
            }
            if (layoutSlot == nullptr) continue;

            this->addShadowSynthNoteRoutes(*shadowSynth, *layoutSlot);
        }
    }

    this->buildNoteNameCache();
}

void DrumKitEngine::addShadowSynthNoteRoutes(InstrumentSynth& shadowSynth, const KitLayout::InstrumentSlot& layoutSlot) {
    shadowSynth.rebuildRoutingFromLayout(layoutSlot);
    for (const auto& art : layoutSlot.articulations) {
        if (!DrumKitEngine::synthRoutesLayoutArticulation(shadowSynth, art)) continue;
        for (const int note : art.notes) {
            if (note >= 0 && note < 128) this->instrumentSynthsByNote[note].push_back(&shadowSynth);
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

    if (this->instrumentSynthPtrs.size() == 0) {
        LOG(Warning, "unable to apply mic layout: no instrument slots was defined in kit layout");
        return false;
    }
    // mic layout is for a mapping of each SampleVariantSpec mic (MicSpec) of InstrumentSynth to coresponding output channel of the plugin
    // the map (sequence and other params defined in MicSpec) of channels is defined in micLayout.
    //  MicSpec to determine which of plugin and routing 
    


    // For each instrument slot, calculate how many mic channels it should have
    // for (size_t idx = 0; idx < this->instrumentSynthPtrs.size(); ++idx) {
    //     InstrumentSynth* slotPtr = this->instrumentSynthPtrs[idx].get();
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
    // allocations are populated by the pure builder when source direct mic requests are
    // provided; this layout-level snapshot is still the shared source for normal mic order.
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

    // Initialize empty mapping for each synth
    for (const auto& instPtr : this->instrumentSynthPtrs) {
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

        // Determine target instrument synths //FIXME: move to loadInstrument?
        std::vector<InstrumentSynth*> targets;
        const auto selector = parseMicInstrumentSelector(mic.instrument, this->globalConfig.instrumentCatalog);
        for (const auto& isp : this->instrumentSynthPtrs) {
            if (matchMicInstrumentSelector(selector, isp->getId(), isp->getType(), this->globalConfig.instrumentCatalog) != MicSelectorPriority::None)
                targets.push_back(isp.get());
        }

        if (targets.empty()) {
            LOG(Warning,  "Assigning mic " + mic.name + " (" + juce::String(mic.specs.channels) + "ch)" + " for instrument '" + mic.instrument + "' FAILED: no coresponding instrument slots found in your kit layout");
            outCursor += channelCount; // still reserve host channels
            continue;
        } else {
            juce::StringArray targetSlotIds;
            for (const auto* targetSynth : targets) targetSlotIds.addIfNotAlreadyThere(targetSynth->getId());
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

    // Apply mapped mic channel counts to synths (used for informational purposes)
    for (const auto& instPtr : this->instrumentSynthPtrs) {
        const auto& map = instPtr->getMicOutputMapping();
        int totalAssigned = static_cast<int>(map.size());
        if (totalAssigned <= 0) totalAssigned = 2;
        instPtr->setMicChannelCount(totalAssigned);
    }

    for (const auto& instPtr : this->instrumentSynthPtrs) {
        auto primaryIt = this->instrumentSynthById.find(instPtr->getId());
        if (primaryIt == this->instrumentSynthById.end())
            continue;

        auto* primary = primaryIt->second;
        if (primary == instPtr.get())
            continue;

        instPtr->setMicOutputMapping(primary->getMicOutputMapping());
        instPtr->setMicChannelCount(primary->getMicChannelCount());
    }

    // Detailed mapping log per instrument
    for (const auto& instPtr : this->instrumentSynthPtrs) {
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
    for (const auto& instrumentSynthPtr : this->instrumentSynthPtrs) {
        instrumentSynthPtr->setCurrentPlaybackSampleRate(sampleRate);
        instrumentSynthPtr->prepareToPlay(sampleRate, samplesPerBlock);
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
    for (const auto& instrumentSynthPtr : this->instrumentSynthPtrs) {
        instrumentSynthPtr->renderNextBlock(outputBuffer, midiMessages, outputBufferStartSample, outputBufferNumSamples);
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

    // Trigger on all synths routed to this note (primary + shadow layers)
    auto noteIt = this->instrumentSynthsByNote.find(midiNote);
    if (noteIt != this->instrumentSynthsByNote.end()) {
        for (auto* synth : noteIt->second)
            synth->playNote(1, midiNote, velocity);
    }
}

void DrumKitEngine::playPreviewArticulation(const juce::String& slotId, const juce::String& articulationCode, juce::uint8 velocity) {
    auto* layout = this->activeKitLayoutPtr.get();
    if (layout == nullptr) return;

    const KitLayout::InstrumentSlot::ArticulationTrigger previewArticulation(articulationCode);
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

    auto synthsIt = this->instrumentSynthsByNote.find(midiNote);
    if (synthsIt == this->instrumentSynthsByNote.end()) return;
    for (auto* synth : synthsIt->second)
        if (synth != nullptr && synth->getId() == slotId) synth->playPreviewArticulation(1, midiNote, articulationCode, velocity);
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
    const juce::ScopedLock sourceLock(this->sourceModelLock);
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

    if (artSpec == nullptr || artSpec->velocityLayerSpecs.empty())
        return std::nullopt;

    bool calibrated = !artSpec->velocityLayerSpecs.empty();
    for (const auto& layer : artSpec->velocityLayerSpecs) {
        if (!layer->fittedSourceDb.has_value() || !layer->calibrationTrimDb.has_value()) {
            calibrated = false;
            break;
        }
    }

    KitLib::VelocityLayerSpec* bestLayer = nullptr;
    KitLib::SampleVariantSpec* bestVariant = nullptr;
    const auto normalizedRelativePath = relativePath.trim().replaceCharacter('\\', '/');
    const auto normalizedMicCode = micCode.trim();
    if (normalizedRelativePath.isNotEmpty()) {
        for (auto& layer : artSpec->velocityLayerSpecs) {
            if (layer == nullptr) continue;
            for (auto& sampleVariant : layer->sampleVariantSpecs) {
                if (sampleVariant == nullptr || !sampleVariant->isValid()) continue;
                const auto candidateRelativePath = sampleVariant->sampleFile.getRelativePathFrom(kit->meta.rootDir).replaceCharacter('\\', '/');
                if (!candidateRelativePath.equalsIgnoreCase(normalizedRelativePath)) continue;
                if (normalizedMicCode.isNotEmpty() && !sampleVariant->micSpec.raw.equalsIgnoreCase(normalizedMicCode)) continue;
                bestLayer = layer.get();
                bestVariant = sampleVariant.get();
                break;
            }
            if (bestVariant != nullptr) break;
        }
    } else {
        bestLayer = artSpec->velocityLayerSpecs.back().get();
        for (auto& layer : artSpec->velocityLayerSpecs) {
            if (velocity <= layer->selectionUpperVelocity) {
                bestLayer = layer.get();
                break;
            }
        }

        if (bestLayer == nullptr || bestLayer->sampleVariantSpecs.empty()) return std::nullopt;

        for (auto& sampleVariant : bestLayer->sampleVariantSpecs) {
            if (sampleVariant == nullptr || !sampleVariant->isValid()) continue;
            if (bestVariant == nullptr || DrumKitEnginePreviewHelpers::isBetterPreviewSampleVariant(*sampleVariant, *bestVariant)) bestVariant = sampleVariant.get();
        }
    }

    if (bestLayer == nullptr || bestVariant == nullptr) return std::nullopt;

    PreviewTarget target;
    target.sampleFile = bestVariant->sampleFile;
    target.contentHash = bestVariant->metadataContentHash;
    target.metadataFileSizeBytes = bestVariant->metadataFileSizeBytes;
    target.micCode = bestVariant->micSpec.raw;
    target.fileChannelStart = juce::jmax(0, bestVariant->fileChannelStart);
    target.fileChannelCount = bestVariant->fileChannelCount > 0 ? bestVariant->fileChannelCount : bestVariant->micSpec.channels;
    target.fixedGainDb = bestVariant->manualGainDb + (calibrated ? *bestLayer->calibrationTrimDb : 0.0f);
    target.playbackOffsetMs = bestVariant->playbackOffsetMs;
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
    for (auto& synthPtr : this->instrumentSynthPtrs) {
        if (synthPtr == nullptr) continue;
        if (synthPtr->getId() != slotId) continue;
        if (!DrumKitEngine::synthMatchesTargetRoutes(*synthPtr, targetRoutes) || !synthPtr->routesSourceArticulation(*targetInstrumentSpec, targetArticulationSpec)) continue;

        if (currentOccurrence == sourceOccurrenceIndex) {
            return synthPtr->updateArticulationParameters(targetRoutes, update);
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
    for (auto& synthPtr : this->instrumentSynthPtrs) {
        if (synthPtr == nullptr) continue;
        if (synthPtr->getId() != slotId) continue;
        if (!DrumKitEngine::synthMatchesTargetRoutes(*synthPtr, targetRoutes)) continue;

        if (currentLayerIndex == layerIndex) {
            synthPtr->updateArticulationParameters(targetRoutes, update);
            return;
        }

        ++currentLayerIndex;
    }
}

bool DrumKitEngine::updateSlotMicRoute(const juce::String& slotId, const juce::String& artName, int layerIndex, const juce::String& sourceMicCode, const juce::String& outputId) {
    if (layerIndex < 0) {
        this->logMessage("Engine mic route FAILED: slot=" + slotId + " art=" + artName + " reason=invalid layer index", DebugLevel::Debug);
        return false;
    }

    const auto targetRoutes = this->getTargetRoutesForArticulationFilter(slotId, artName);
    const juce::ScopedLock graphLock(this->engineGraphLock);

    int currentLayerIndex = 0;
    for (auto& synthPtr : this->instrumentSynthPtrs) {
        if (synthPtr == nullptr || synthPtr->getId() != slotId || !DrumKitEngine::synthMatchesTargetRoutes(*synthPtr, targetRoutes)) continue;
        if (currentLayerIndex == layerIndex) {
            const bool updated = synthPtr->updateMicRoute(sourceMicCode, outputId);
            this->logMessage("Engine mic route " + juce::String(updated ? "SUCCESS" : "FAILED") + ": slot=" + slotId + " art=" + artName + " layer=" + juce::String(layerIndex + 1) + " source=" + sourceMicCode + " target=" + (outputId.isNotEmpty() ? outputId : juce::String("auto")) + (updated ? "" : " reason=no matching loaded sound or output route"), DebugLevel::Debug);
            return updated;
        }
        ++currentLayerIndex;
    }
    this->logMessage("Engine mic route FAILED: slot=" + slotId + " art=" + artName + " layer=" + juce::String(layerIndex + 1) + " reason=matching synth layer not found", DebugLevel::Debug);
    return false;
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

bool DrumKitEngine::synthMatchesTargetRoutes(const InstrumentSynth& synth, const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes) {
    if (targetRoutes.empty()) return true;
    if (!synth.hasDestinationRoutes()) return false;

    for (const auto& route : targetRoutes) {
        if (synth.routesDestinationArticulation(route.first, route.second)) return true;
    }
    return false;
}

bool DrumKitEngine::synthRoutesLayoutArticulation(const InstrumentSynth& synth, const KitLayout::InstrumentSlot::ArticulationTrigger& art) {
    return synth.routesDestinationArticulation(art.nameId, art.exprId);
}

void DrumKitEngine::applyLayerParameterUpdate(InstrumentSynth& synth, const LayerParameterUpdate& update) {
    if (update.tuningCents.has_value()) synth.setTuningOffsetCents(*update.tuningCents);
    if (update.attackMs.has_value()) synth.setAttackOverrideMs(*update.attackMs);
    if (update.decayMs.has_value()) synth.setDecayOverrideMs(*update.decayMs);
    if (update.volumeDb.has_value()) synth.setVolumeDb(*update.volumeDb);
    if (update.pan.has_value()) synth.setPan(*update.pan);
    if (update.flipLR.has_value()) synth.setFlipLR(*update.flipLR);
    if (update.flipPhase.has_value()) synth.setFlipPhase(*update.flipPhase);
    if (update.micBleedLevels.has_value()) synth.setMicBleedLevels(*update.micBleedLevels);
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
    // All parsing, kit/instrument resolution, and synth creation happens here.
    // The audio thread continues processing the OLD synth graph uninterrupted.

    auto parsed = DrumKitEngine::parseInstrumentMapText(text);
    for (const auto& warning : parsed.errors)
        LOG(parsed.ok() ? Warning : Error, warning);

    if (!parsed.ok()) {
        this->lastInstrumentMapStatus = parsed.error();
        return false;
    }

    DrumKitEngineIdentityMapHelpers::normalizeInstrumentMapEntries(parsed.value, *librarySnapshot);
    DrumKitEngineMapHelpers::applyAutomaticOrientation(parsed.value, *this->activeKitLayoutPtr, *librarySnapshot);
    const auto normalizedMapText = DrumKitEngine::serializeInstrumentMap(parsed.value);

    LOG(Info, "=== useInstrumentMap: " + juce::String(parsed.value.entries.size()) + " entries ===");
    for (size_t idx = 0; idx < parsed.value.entries.size(); ++idx) {
        const auto& entry = parsed.value.entries[idx];
        juce::String slotArtStr, srcArtStr;
        for (const auto& sa : entry.slotArticulationFilter) slotArtStr += (slotArtStr.isEmpty() ? "" : ",") + sa;
        for (const auto& sa : entry.articulationFilter) srcArtStr += (srcArtStr.isEmpty() ? "" : ",") + sa;
        juce::String routeStr;
        for (const auto& [sourceMicCode, outputId] : entry.micRouteOverrides)
            routeStr += (routeStr.isEmpty() ? "" : ",") + sourceMicCode + "->" + outputId;
        LOG(Info, "  entry[" + juce::String(idx) + "]: slot=" + entry.slotId
            + (slotArtStr.isNotEmpty() ? "/" + slotArtStr : "")
            + " -> kit=" + entry.kitRef + " inst=" + entry.instCode
            + (srcArtStr.isNotEmpty() ? " art=" + srcArtStr : "")
            + (entry.tuningCents.has_value() ? " tuning=" + juce::String(*entry.tuningCents) : "")
            + (entry.attackTimeMs.has_value() ? " attack=" + juce::String(*entry.attackTimeMs) : "")
            + (entry.decayTimeMs.has_value() ? " decay=" + juce::String(*entry.decayTimeMs) : "")
            + (routeStr.isNotEmpty() ? " micRoute=[" + routeStr + "]" : ""));
    }

    struct ShadowPlan {
        const InstrumentMapEntry* entry = nullptr;
        KitLib::InstrumentSpec* instSpec = nullptr;
        std::vector<juce::String> articulations;
        InstrumentSynth::ArticulationNameRemap artNameRemap;
        InstrumentSynth::ArticulationExactRemap artExactRemap;
    };

    // Build a name remap from an entry's slot/source art filters for cross-art mapping
    auto buildArtNameRemap = [](const InstrumentMapEntry& entry) -> InstrumentSynth::ArticulationNameRemap {
        InstrumentSynth::ArticulationNameRemap remap;
        const auto& slotArts = entry.slotArticulationFilter;
        const auto& srcArts = entry.articulationFilter;
        if (slotArts.empty() || srcArts.empty())
            return remap;
        for (size_t idx = 0; idx < slotArts.size() && idx < srcArts.size(); ++idx) {
            auto slotId = StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(slotArts[idx]));
            auto srcId = StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(srcArts[idx]));
            if (slotId != srcId) 
                remap[slotId] = srcId;
        }
        return remap;
    };

    auto makeArticulationRouteKey = [](uint32_t nameId, uint32_t exprId) {
        return (static_cast<uint64_t>(nameId) << 32) | static_cast<uint64_t>(exprId);
    };

    auto buildArtExactRemap = [&makeArticulationRouteKey](const InstrumentMapEntry& entry) -> InstrumentSynth::ArticulationExactRemap {
        InstrumentSynth::ArticulationExactRemap remap;
        const auto& slotArts = entry.slotArticulationFilter;
        const auto& srcArts = entry.articulationFilter;
        if (slotArts.empty() || srcArts.empty()) return remap;
        for (size_t idx = 0; idx < slotArts.size() && idx < srcArts.size(); ++idx) {
            if (slotArts[idx].isEmpty() || srcArts[idx].isEmpty()) continue;
            const KitLayout::InstrumentSlot::ArticulationTrigger slotArt(slotArts[idx]);
            const KitLayout::InstrumentSlot::ArticulationTrigger sourceArt(srcArts[idx]);
            if (slotArt.nameId == sourceArt.nameId && slotArt.exprId == sourceArt.exprId) continue;
            remap[makeArticulationRouteKey(slotArt.nameId, slotArt.exprId)] = { sourceArt.nameId, sourceArt.exprId };
        }
        return remap;
    };
    auto resolveVelocityResponseOverride = [](const InstrumentMapEntry& entry) -> std::optional<VelocityResponse> {
        if (!entry.velocityResponse.has_value()) return std::nullopt;
        auto parsedResponse = VelocityResponse::fromDefinition(*entry.velocityResponse);
        return parsedResponse.ok() ? std::optional<VelocityResponse>(std::move(parsedResponse.value)) : std::nullopt;
    };
    auto applyOverrides = [&resolveVelocityResponseOverride](InstrumentSynth& synth, const InstrumentMapEntry& entry) {
        synth.setTuningOffsetCents(entry.tuningCents.value_or(0));
        synth.setAttackOverrideMs(entry.attackTimeMs.value_or(-1));
        synth.setDecayOverrideMs(entry.decayTimeMs.value_or(-1));
        synth.setVolumeDb(entry.volumeDb.value_or(0.0f));
        synth.setPan(entry.pan.value_or(0));
        synth.setFlipLR(entry.flipLR);
        synth.setFlipPhase(entry.flipPhase);
        synth.setMicBleedLevels(entry.micBleedLevels);
        LayerParameterUpdate velocityResponseUpdate;
        velocityResponseUpdate.updateVelocityResponse = true;
        velocityResponseUpdate.velocityResponse = resolveVelocityResponseOverride(entry);
        synth.updateArticulationParameters({}, velocityResponseUpdate);
    };

    auto makeParameterUpdate = [&resolveVelocityResponseOverride](const InstrumentMapEntry& entry) {
        LayerParameterUpdate update;
        update.tuningCents = entry.tuningCents.value_or(0);
        update.attackMs = entry.attackTimeMs.value_or(-1);
        update.decayMs = entry.decayTimeMs.value_or(-1);
        update.volumeDb = entry.volumeDb.value_or(0.0f);
        update.pan = entry.pan.value_or(0);
        update.flipLR = entry.flipLR;
        update.flipPhase = entry.flipPhase;
        update.micBleedLevels = entry.micBleedLevels;
        update.updateVelocityResponse = true;
        update.velocityResponse = resolveVelocityResponseOverride(entry);
        return update;
    };

    auto buildTargetRoutes = [](const KitLayout::InstrumentSlot& slotLayout, const std::vector<juce::String>& articulationFilters) {
        std::vector<std::pair<uint32_t, uint32_t>> targetRoutes;
        if (articulationFilters.empty()) return targetRoutes;
        for (const auto& articulation : slotLayout.articulations) {
            for (const auto& filter : articulationFilters) {
                if (!ArticulationNameHelpers::mapFilterMatchesArticulation(filter, articulation.codeRaw)) continue;
                const auto route = std::make_pair(articulation.nameId, articulation.exprId);
                if (std::find(targetRoutes.begin(), targetRoutes.end(), route) == targetRoutes.end()) targetRoutes.push_back(route);
            }
        }
        return targetRoutes;
    };

    auto applyPrimaryOverrides = [&applyOverrides, &makeParameterUpdate](InstrumentSynth& synth, const InstrumentMapEntry& entry, const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes) {
        if (targetRoutes.empty()) applyOverrides(synth, entry);
        else synth.updateArticulationParameters(targetRoutes, makeParameterUpdate(entry));
    };

    auto makeResolvedEntryFingerprint = [](const InstrumentMapEntry& entry, const KitLib::InstrumentSpec& instSpec) {
        return DrumKitEngineMapHelpers::makeInstrumentMapEntryFingerprint(entry) + "\nsource\n" + instSpec.getContentFingerprint();
    };

    auto makeResolvedEntryReloadFingerprint = [](const InstrumentMapEntry& entry, const KitLib::InstrumentSpec& instSpec) {
        return DrumKitEngineMapHelpers::makeInstrumentMapEntryReloadFingerprint(entry) + "\nsource\n" + instSpec.getContentFingerprint();
    };

    std::vector<juce::String> slotOrder;
    std::unordered_map<juce::String, std::vector<const InstrumentMapEntry*>> entriesBySlotId;
    for (const auto& entry : parsed.value.entries) {
        if (entriesBySlotId.find(entry.slotId) == entriesBySlotId.end())
            slotOrder.push_back(entry.slotId);
        entriesBySlotId[entry.slotId].push_back(&entry);
    }

    std::unordered_map<juce::String, juce::String> nextSlotFingerprints = DrumKitEngineMapHelpers::buildResolvedSlotFingerprintMap(parsed.value, *librarySnapshot);
    std::unordered_map<juce::String, juce::String> nextSlotReloadFingerprints = DrumKitEngineMapHelpers::buildResolvedSlotReloadFingerprintMap(parsed.value, *librarySnapshot);
    struct ExistingShadow {
        InstrumentSynth* synth = nullptr;
        juce::String stateFingerprint;
        juce::String reloadFingerprint;
    };

    // Build index maps for fast slot lookup (safe to read on message thread)
    std::unordered_map<juce::String, int> primarySlotIndex;
    std::unordered_map<juce::String, std::vector<ExistingShadow>> existingShadowsBySlotId;
    for (int idx = 0; idx < static_cast<int>(this->instrumentSynthPtrs.size()); ++idx) {
        auto* synth = this->instrumentSynthPtrs[static_cast<size_t>(idx)].get();
        if (synth == nullptr) continue;
        if (idx < this->primaryInstrumentSynthCount) {
            primarySlotIndex[synth->getId()] = idx;
            continue;
        }

        ExistingShadow existingShadow;
        existingShadow.synth = synth;
        auto stateIt = this->lastAppliedShadowFingerprints.find(synth);
        if (stateIt != this->lastAppliedShadowFingerprints.end()) existingShadow.stateFingerprint = stateIt->second;
        auto reloadIt = this->lastAppliedShadowReloadFingerprints.find(synth);
        if (reloadIt != this->lastAppliedShadowReloadFingerprints.end()) existingShadow.reloadFingerprint = reloadIt->second;
        existingShadowsBySlotId[synth->getId()].push_back(std::move(existingShadow));
    }

    juce::StringArray statusLines;
    for (const auto& warning : parsed.errors)
        statusLines.add(warning);

    int applied = 0;

    // Per-slot preparation: replacement synths and shadow synths built outside the lock
    struct PreparedShadow {
        std::unique_ptr<InstrumentSynth> synth;
        juce::String stateFingerprint;
        juce::String reloadFingerprint;
        std::vector<std::pair<int, InstrumentSynth*>> noteRoutes;
    };
    struct PreparedShadowParameterUpdate {
        InstrumentSynth* synth = nullptr;
        const InstrumentMapEntry* entry = nullptr;
        juce::String stateFingerprint;
    };
    struct PreparedSlot {
        juce::String slotId;
        int primaryIndex = -1;
        std::unique_ptr<InstrumentSynth> primaryReplacement;
        const InstrumentMapEntry* primaryParameterEntry = nullptr;
        std::vector<std::pair<uint32_t, uint32_t>> primaryParameterRoutes;
        bool removeOldShadows = false;
        bool rebuildShadows = false;
        std::vector<PreparedShadow> shadows;
        std::vector<PreparedShadowParameterUpdate> shadowParameterUpdates;
    };
    std::vector<PreparedSlot> preparedSlots;

    for (const auto& slotId : slotOrder) {
        const auto& group = entriesBySlotId[slotId];
        if (group.empty()) continue;

        auto slotIt = this->instrumentSynthById.find(slotId);
        if (slotIt == this->instrumentSynthById.end()) {
            statusLines.add("Instrument slot not found: " + slotId);
            continue;
        }
        auto* primarySlot = slotIt->second;
        bool slotChanged = !primarySlot->isLoaded();
        const bool slotStateChanged = this->lastAppliedSlotFingerprints[slotId] != nextSlotFingerprints[slotId];
        const bool slotReloadChanged = this->lastAppliedSlotReloadFingerprints[slotId] != nextSlotReloadFingerprints[slotId];
        const bool slotMatchesHint = !hasChangedSlotHint || slotId.trim().toLowerCase() == changedSlotKey;
        if (hasChangedSlotHint && !slotMatchesHint && !slotChanged && !slotStateChanged && !slotReloadChanged) continue;

        struct ResolvedEntry {
            const InstrumentMapEntry* entry = nullptr;
            KitLib::DrumKitSpec* kitSpec = nullptr;
            KitLib::InstrumentSpec* instSpec = nullptr;
            std::vector<juce::String> effectiveFilter;
        };

        std::vector<ResolvedEntry> resolvedEntries;
        bool slotResolveFailed = false;
        for (const auto* entry : group) {
            auto* kitSpec = librarySnapshot->findKitById(entry->kitRef);
            if (kitSpec == nullptr) {
                statusLines.add(DrumKitEngineIdentityMapHelpers::describeKitResolveFailure(*librarySnapshot, entry->kitRef, entry->slotId));
                slotResolveFailed = true;
                continue;
            }

            if (entry->instCode.isEmpty()) {
                statusLines.add("Instrument code is required for slot '" + entry->slotId + "'");
                slotResolveFailed = true;
                continue;
            }

            auto* instSpec = kitSpec->findInstrumentById(entry->instCode);
            if (instSpec == nullptr) {
                statusLines.add(DrumKitEngineIdentityMapHelpers::describeInstrumentResolveFailure(*kitSpec, entry->instCode, entry->slotId));
                slotResolveFailed = true;
                continue;
            }

            if (!entry->articulationFilter.empty()) {
                const auto availableSourceArts = DrumKitEngineMapHelpers::collectInstrumentArticulationNames(*instSpec);
                juce::StringArray unknownSourceArts;
                juce::StringArray unknownSourceArtKeys;

                for (const auto& sourceArtName : entry->articulationFilter) {
                    const auto normalizedSourceArtName = ArticulationNameHelpers::normalizeArticulationName(sourceArtName);
                    if (normalizedSourceArtName.isEmpty()) continue;
                    const auto sourceArtMatchCount = DrumKitEngineIdentityMapHelpers::countArticulationIdMatches(*instSpec, sourceArtName);
                    if (sourceArtMatchCount == 1) continue;
                    if (sourceArtMatchCount == 0 && ArticulationNameHelpers::stringArrayContainsIgnoreCase(availableSourceArts, normalizedSourceArtName)) continue;
                    if (ArticulationNameHelpers::stringArrayContainsIgnoreCase(unknownSourceArtKeys, normalizedSourceArtName)) continue;
                    auto sourceArtToken = DrumKitEngineIdentityMapHelpers::describeArticulationResolveFailure(*instSpec, sourceArtName, kitSpec->meta.codeRaw + "/" + instSpec->meta.codeRaw, entry->slotId);
                    const auto suggestedSourceArt = ArticulationNameHelpers::findClosestArticulationName(normalizedSourceArtName, availableSourceArts);
                    if (suggestedSourceArt.isNotEmpty()) sourceArtToken += " (did you mean '" + suggestedSourceArt + "'?)";
                    unknownSourceArtKeys.add(normalizedSourceArtName);
                    unknownSourceArts.add(sourceArtToken);
                }

                if (!unknownSourceArts.isEmpty()) {
                    const auto unknownSourceArtsText = unknownSourceArts.joinIntoString(", ");
                    const auto availableSourceArtsText = availableSourceArts.isEmpty()
                        ? juce::String("none")
                        : availableSourceArts.joinIntoString(", ");

                    statusLines.add("Source articulation problem(s): " + unknownSourceArtsText + ". Available source articulations: " + availableSourceArtsText);
                    slotResolveFailed = true;
                    continue;
                }
            }

            auto filter = DrumKitEngineMapHelpers::effectiveArticulationFilter(*entry);
            juce::String filterStr;
            for (const auto& fa : filter) filterStr += (filterStr.isEmpty() ? "" : ",") + fa;
            LOG(Info, "  resolved: slot=" + entry->slotId + " -> " + kitSpec->meta.name + "/" + entry->instCode
                + (filterStr.isNotEmpty() ? " effectiveFilter=[" + filterStr + "]" : " effectiveFilter=[ALL]"));
            resolvedEntries.push_back({ entry, kitSpec, instSpec, std::move(filter) });
        }

        if (slotResolveFailed || resolvedEntries.empty()) {
            statusLines.add("Slot '" + slotId + "' was not applied; old route kept");
            nextSlotFingerprints[slotId] = this->lastAppliedSlotFingerprints[slotId];
            nextSlotReloadFingerprints[slotId] = this->lastAppliedSlotReloadFingerprints[slotId];
            continue;
        }

        slotChanged = slotChanged || slotReloadChanged;

        const auto& slotLayout = primarySlot->getInstrumentSlot();
        const auto& primary = resolvedEntries.front();
        InstrumentSynth::ArticulationSourceMap artSources;
        InstrumentSynth::ArticulationNameRemap primaryRemap = buildArtNameRemap(*primary.entry);
        InstrumentSynth::ArticulationExactRemap primaryExactRemap = buildArtExactRemap(*primary.entry);
        std::vector<ShadowPlan> shadowPlans;
        std::vector<uint32_t> claimedArticulations;
        std::vector<uint32_t> primaryAllowedArticulationIds;
        std::vector<juce::String> primaryAllowedArticulationFilters;
        InstrumentSynth::ArticulationAliasMap artAliases;
        auto appendPrimaryAllowedArticulationFilter = [&primaryAllowedArticulationFilters](const juce::String& artName) {
            const auto normalizedArtName = artName.trim();
            if (normalizedArtName.isEmpty()) return;
            for (const auto& existingArtName : primaryAllowedArticulationFilters)
                if (existingArtName.compareIgnoreCase(normalizedArtName) == 0) return;
            primaryAllowedArticulationFilters.push_back(normalizedArtName);
        };
        auto removePrimaryAllowedArticulationFilter = [&primaryAllowedArticulationFilters](uint32_t artId) {
            std::erase_if(primaryAllowedArticulationFilters, [artId](const juce::String& artName) {
                return StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(artName)) == artId;
            });
        };

        if (primary.effectiveFilter.empty()) {
            for (const auto& layoutArt : slotLayout.articulations)
                DrumKitEngineMapHelpers::appendUniqueId(primaryAllowedArticulationIds, layoutArt.nameId);
        } else {
            for (const auto& artName : primary.effectiveFilter) {
                if (artName.isNotEmpty()) {
                    DrumKitEngineMapHelpers::appendUniqueId(primaryAllowedArticulationIds, StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(artName)));
                    appendPrimaryAllowedArticulationFilter(artName);
                }
            }
        }

        if (LOG_ENABLED(Info)) {
            juce::String allowedStr;
            for (const auto& artId : primaryAllowedArticulationIds)
                allowedStr += (allowedStr.isEmpty() ? "" : ",") + StringsDictSingleton::nameOf(artId);
            LOG(Info, "  slot=" + slotId + " primaryAllowed(initial)=[" + allowedStr + "] slotChanged=" + juce::String(slotChanged ? "yes" : "no"));
        }

        for (const auto& artName : primary.effectiveFilter) {
            if (artName.isNotEmpty())
                DrumKitEngineMapHelpers::appendUniqueId(claimedArticulations, StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(artName)));
        }

        for (size_t idx = 1; idx < resolvedEntries.size(); ++idx) {
            const auto& resolved = resolvedEntries[idx];
            if (resolved.effectiveFilter.empty()) {
                shadowPlans.push_back({ resolved.entry, resolved.instSpec, {}, buildArtNameRemap(*resolved.entry), buildArtExactRemap(*resolved.entry) });
                continue;
            }

            std::vector<juce::String> layeredArts;
            const bool hasParameterOverrides = resolved.entry->tuningCents.has_value()
                                            || resolved.entry->attackTimeMs.has_value()
                                            || resolved.entry->decayTimeMs.has_value()
                                            || resolved.entry->volumeDb.has_value()
                                            || resolved.entry->pan.has_value()
                                            || resolved.entry->velocityResponse.has_value()
                                            || resolved.entry->flipLR
                                            || resolved.entry->flipPhase
                                            || !resolved.entry->micBleedLevels.empty()
                                            || !resolved.entry->micRouteOverrides.empty();
            for (const auto& artName : resolved.effectiveFilter) {
                if (artName.isEmpty())
                    continue;

                const auto artId = StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(artName));
                const bool alreadyClaimed = vectorContains(claimedArticulations, artId);
                if (hasParameterOverrides) {
                    layeredArts.push_back(artName);
                    if (!alreadyClaimed) {
                        DrumKitEngineMapHelpers::appendUniqueId(claimedArticulations, artId);
                        std::erase(primaryAllowedArticulationIds, artId);
                        removePrimaryAllowedArticulationFilter(artId);
                    }
                } else if (!alreadyClaimed) {
                    DrumKitEngineMapHelpers::appendUniqueId(claimedArticulations, artId);
                    artSources[artId] = resolved.instSpec;
                    DrumKitEngineMapHelpers::appendUniqueId(primaryAllowedArticulationIds, artId);
                    if (!primary.effectiveFilter.empty()) appendPrimaryAllowedArticulationFilter(artName);
                    // Propagate cross-art remap for this articulation to the primary synth
                    auto entryRemap = buildArtNameRemap(*resolved.entry);
                    auto remapIt = entryRemap.find(artId);
                    if (remapIt != entryRemap.end())
                        primaryRemap[artId] = remapIt->second;
                    auto entryExactRemap = buildArtExactRemap(*resolved.entry);
                    primaryExactRemap.insert(entryExactRemap.begin(), entryExactRemap.end());
                } else {
                    layeredArts.push_back(artName);
                }
            }

            if (!layeredArts.empty())
                shadowPlans.push_back({ resolved.entry, resolved.instSpec, layeredArts, buildArtNameRemap(*resolved.entry), buildArtExactRemap(*resolved.entry) });
        }

        uint32_t fallbackLayoutArtId = 0;
        InstrumentSynth::ArticulationTarget fallbackSourceArt;
        if (slotLayout.controllers.empty()) {
            for (const auto& primaryArtName : primary.effectiveFilter) {
                if (primaryArtName.isEmpty())
                    continue;
                fallbackLayoutArtId = StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(primaryArtName));
                fallbackSourceArt.nameId = DrumKitEngineMapHelpers::resolveEntrySourceArtId(*primary.entry, primaryArtName);
                if (!primary.entry->articulationFilter.empty()) {
                    if (primary.entry->slotArticulationFilter.empty()) {
                        const KitLayout::InstrumentSlot::ArticulationTrigger sourceArt(primary.entry->articulationFilter.front());
                        fallbackSourceArt.exprId = sourceArt.exprId;
                    } else {
                        for (size_t artIdx = 0; artIdx < primary.entry->slotArticulationFilter.size() && artIdx < primary.entry->articulationFilter.size(); ++artIdx) {
                            if (!ArticulationNameHelpers::mapFilterMatchesArticulation(primary.entry->slotArticulationFilter[artIdx], primaryArtName)) continue;
                            const KitLayout::InstrumentSlot::ArticulationTrigger sourceArt(primary.entry->articulationFilter[artIdx]);
                            fallbackSourceArt.exprId = sourceArt.exprId;
                            break;
                        }
                    }
                }
                break;
            }
        }

        // Simple non-controller slots inherit empty articulations from the first mapped art.
        // Controller-driven slots keep their existing primary fallback so CC tables remain intact.
        for (const auto& layoutArt : slotLayout.articulations) {
            if (vectorContains(claimedArticulations, layoutArt.nameId)) continue;
            if (fallbackSourceArt.nameId != 0) artAliases[layoutArt.nameId] = fallbackSourceArt;
            else DrumKitEngineMapHelpers::appendUniqueId(primaryAllowedArticulationIds, layoutArt.nameId);
        }

        if (LOG_ENABLED(Info)) {
            juce::String allowedStr;
            for (const auto& artId : primaryAllowedArticulationIds)
                allowedStr += (allowedStr.isEmpty() ? "" : ",") + StringsDictSingleton::nameOf(artId);
            LOG(Info, "  slot=" + slotId + " primaryAllowed(final)=[" + allowedStr + "]");
            if (!primaryRemap.empty()) {
                juce::String remapStr;
                for (const auto& [fromId, toId] : primaryRemap)
                    remapStr += (remapStr.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(fromId) + "->" + StringsDictSingleton::nameOf(toId);
                LOG(Info, "  primaryRemap: [" + remapStr + "]");
            }
            if (!artSources.empty()) {
                juce::String srcStr;
                for (const auto& [artId, instPtr] : artSources)
                    srcStr += (srcStr.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(artId) + "->" + instPtr->meta.codeRaw;
                LOG(Info, "  artSources: [" + srcStr + "]");
            }
        }

        PreparedSlot prep;
        prep.slotId = slotId;
        auto pIdxIt = primarySlotIndex.find(slotId);
        prep.primaryIndex = (pIdxIt != primarySlotIndex.end()) ? pIdxIt->second : -1;
        if (!slotChanged && slotStateChanged && prep.primaryIndex >= 0) {
            prep.primaryParameterEntry = primary.entry;
            prep.primaryParameterRoutes = buildTargetRoutes(slotLayout, primary.effectiveFilter);
        }

        const auto existingShadowIt = existingShadowsBySlotId.find(slotId);
        const bool slotHasExistingShadows = existingShadowIt != existingShadowsBySlotId.end() && !existingShadowIt->second.empty();
        const bool slotHasShadowPlans = !shadowPlans.empty();
        bool canReuseExistingShadows = slotHasExistingShadows && slotHasShadowPlans && !slotChanged && existingShadowIt->second.size() == shadowPlans.size();
        std::vector<bool> matchedExistingShadows(slotHasExistingShadows ? existingShadowIt->second.size() : 0, false);
        std::vector<PreparedShadowParameterUpdate> shadowParameterUpdates;

        if (canReuseExistingShadows) {
            for (const auto& shadowPlan : shadowPlans) {
                const auto reloadFingerprint = makeResolvedEntryReloadFingerprint(*shadowPlan.entry, *shadowPlan.instSpec);
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
                const auto stateFingerprint = makeResolvedEntryFingerprint(*shadowPlan.entry, *shadowPlan.instSpec);
                const auto& existingShadow = existingShadowIt->second[static_cast<size_t>(matchedShadowIndex)];
                if (existingShadow.stateFingerprint != stateFingerprint)
                    shadowParameterUpdates.push_back({ existingShadow.synth, shadowPlan.entry, stateFingerprint });
            }
        }

        prep.rebuildShadows = slotHasShadowPlans && (!canReuseExistingShadows || slotChanged);
        prep.removeOldShadows = slotHasExistingShadows && (prep.rebuildShadows || (!slotHasShadowPlans && (slotChanged || slotStateChanged)));
        if (canReuseExistingShadows) prep.shadowParameterUpdates = std::move(shadowParameterUpdates);

        // Create a replacement synth for the primary slot (expensive work done outside lock)
        if (slotChanged && prep.primaryIndex >= 0) {
            auto replacement = std::make_unique<InstrumentSynth>(this, slotLayout);
            replacement->setMicOutputMapping(primarySlot->getMicOutputMapping());
            replacement->setMicChannelCount(primarySlot->getMicChannelCount());
            const bool primaryStrictArticulationResolution = !primaryAllowedArticulationFilters.empty() || !primaryRemap.empty() || !primaryExactRemap.empty();
            InstrumentSynth::RoutingContext primaryRoutingContext;
            primaryRoutingContext.defaultInstrument = primary.instSpec;
            primaryRoutingContext.artSources = artSources;
            primaryRoutingContext.allowedArticulationNameIds = primaryAllowedArticulationIds;
            primaryRoutingContext.allowedArticulationFilters = primaryAllowedArticulationFilters;
            primaryRoutingContext.artNameRemap = primaryRemap;
            primaryRoutingContext.artExactRemap = primaryExactRemap;
            primaryRoutingContext.artAliases = artAliases;
            primaryRoutingContext.micRouteOverrides = primary.entry->micRouteOverrides;
            primaryRoutingContext.strictArticulationResolution = primaryStrictArticulationResolution;
            auto primaryRoutes = replacement->buildResolvedRouteList(slotLayout, primaryRoutingContext);
            replacement->loadInstrumentWithRouteList(primary.instSpec, primaryRoutingContext, std::move(primaryRoutes), librarySnapshot);
            applyPrimaryOverrides(*replacement, *primary.entry, buildTargetRoutes(slotLayout, primary.effectiveFilter));
            LOG(Info, "  PRIMARY loaded: slot=" + slotId + " inst=" + primary.instSpec->meta.codeRaw
                + " tuning=" + juce::String(primary.entry->tuningCents.value_or(0))
                + (primary.entry->attackTimeMs.has_value() ? " attack=" + juce::String(*primary.entry->attackTimeMs) : "")
                + (primary.entry->decayTimeMs.has_value() ? " decay=" + juce::String(*primary.entry->decayTimeMs) : ""));
            if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
                replacement->setCurrentPlaybackSampleRate(this->lastSampleRate);
                replacement->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
            }
            prep.primaryReplacement = std::move(replacement);
            ++applied;
        }

        // Create shadow synths (expensive work done outside lock)
        if (prep.rebuildShadows) {
            for (const auto& shadowPlan : shadowPlans) {
                auto shadow = std::make_unique<InstrumentSynth>(this, slotLayout);
                auto* shadowRaw = shadow.get();
                const auto shadowArtIds = ArticulationNameHelpers::toArticulationIdList(shadowPlan.articulations);
                shadowRaw->setMicOutputMapping(primarySlot->getMicOutputMapping());
                shadowRaw->setMicChannelCount(primarySlot->getMicChannelCount());
                const bool shadowStrictArticulationResolution = !shadowPlan.articulations.empty() || !shadowPlan.artNameRemap.empty() || !shadowPlan.artExactRemap.empty();
                InstrumentSynth::ArticulationAliasMap shadowAliases;
                if (fallbackLayoutArtId != 0 && vectorContains(shadowArtIds, fallbackLayoutArtId)) shadowAliases = artAliases;
                InstrumentSynth::RoutingContext shadowRoutingContext;
                shadowRoutingContext.defaultInstrument = shadowPlan.instSpec;
                shadowRoutingContext.allowedArticulationNameIds = shadowArtIds;
                shadowRoutingContext.allowedArticulationFilters = shadowPlan.articulations;
                shadowRoutingContext.artNameRemap = shadowPlan.artNameRemap;
                shadowRoutingContext.artExactRemap = shadowPlan.artExactRemap;
                shadowRoutingContext.artAliases = shadowAliases;
                shadowRoutingContext.micRouteOverrides = shadowPlan.entry->micRouteOverrides;
                shadowRoutingContext.strictArticulationResolution = shadowStrictArticulationResolution;
                auto shadowRoutes = shadowRaw->buildResolvedRouteList(slotLayout, shadowRoutingContext);
                shadowRaw->loadInstrumentWithRouteList(shadowPlan.instSpec, shadowRoutingContext, std::move(shadowRoutes), librarySnapshot);
                applyOverrides(*shadowRaw, *shadowPlan.entry);
                if (LOG_ENABLED(Info)) {
                    juce::String shadowArtsStr;
                    for (const auto& sa : shadowPlan.articulations) shadowArtsStr += (shadowArtsStr.isEmpty() ? "" : ",") + sa;
                    juce::String remapStr;
                    for (const auto& [fromId, toId] : shadowPlan.artNameRemap) remapStr += (remapStr.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(fromId) + "->" + StringsDictSingleton::nameOf(toId);
                    LOG(Info, "  SHADOW loaded: slot=" + slotId + " inst=" + shadowPlan.instSpec->meta.codeRaw
                        + " arts=[" + shadowArtsStr + "]"
                        + (remapStr.isNotEmpty() ? " remap=[" + remapStr + "]" : "")
                        + " tuning=" + juce::String(shadowPlan.entry->tuningCents.value_or(0)));
                }

                if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
                    shadowRaw->setCurrentPlaybackSampleRate(this->lastSampleRate);
                    shadowRaw->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
                }

                PreparedShadow ps;
                ps.stateFingerprint = makeResolvedEntryFingerprint(*shadowPlan.entry, *shadowPlan.instSpec);
                ps.reloadFingerprint = makeResolvedEntryReloadFingerprint(*shadowPlan.entry, *shadowPlan.instSpec);
                for (const auto& art : slotLayout.articulations) {
                    if (!shadowRaw->routesDestinationArticulation(art.nameId, art.exprId)) continue;
                    for (const int note : art.notes)
                        ps.noteRoutes.push_back({note, shadowRaw});
                }
                ps.synth = std::move(shadow);
                prep.shadows.push_back(std::move(ps));
                ++applied;
            }
        }

        preparedSlots.push_back(std::move(prep));
    }

    // ======================== PHASE 3: SWAP (brief lock) ========================
    // The lock is held only for fast pointer swaps and routing table updates.
    // Old synths are collected and destroyed AFTER the lock is released.
    std::vector<std::unique_ptr<InstrumentSynth>> oldSynths;
    {
        const juce::ScopedLock graphLock(this->engineGraphLock);

        std::unordered_set<juce::String> shadowSlotIdsToRemove;
        for (const auto& prep : preparedSlots) {
            if (prep.removeOldShadows) shadowSlotIdsToRemove.insert(prep.slotId);
        }

        std::unordered_set<InstrumentSynth*> removedShadowSynths;
        if (!shadowSlotIdsToRemove.empty()) {
            for (int idx = static_cast<int>(this->instrumentSynthPtrs.size()) - 1; idx >= this->primaryInstrumentSynthCount; --idx) {
                auto* shadowSynth = this->instrumentSynthPtrs[static_cast<size_t>(idx)].get();
                if (shadowSynth == nullptr || shadowSlotIdsToRemove.find(shadowSynth->getId()) == shadowSlotIdsToRemove.end()) continue;
                removedShadowSynths.insert(shadowSynth);
                this->lastAppliedShadowFingerprints.erase(shadowSynth);
                this->lastAppliedShadowReloadFingerprints.erase(shadowSynth);
                oldSynths.push_back(std::move(this->instrumentSynthPtrs[static_cast<size_t>(idx)]));
                this->instrumentSynthPtrs.erase(this->instrumentSynthPtrs.begin() + idx);
            }
        }

        // Swap in prepared primary replacements (pointer swap only)
        bool anyPrimarySwapped = false;
        for (auto& prep : preparedSlots) {
            if (prep.primaryReplacement && prep.primaryIndex >= 0
                && prep.primaryIndex < static_cast<int>(this->instrumentSynthPtrs.size())) {
                const auto primaryIndex = static_cast<size_t>(prep.primaryIndex);
                oldSynths.push_back(std::move(this->instrumentSynthPtrs[primaryIndex]));
                this->instrumentSynthPtrs[primaryIndex] = std::move(prep.primaryReplacement);
                this->instrumentSynthById[prep.slotId] = this->instrumentSynthPtrs[primaryIndex].get();
                anyPrimarySwapped = true;
            } else if (prep.primaryParameterEntry != nullptr && prep.primaryIndex >= 0 && prep.primaryIndex < static_cast<int>(this->instrumentSynthPtrs.size())) {
                applyPrimaryOverrides(*this->instrumentSynthPtrs[static_cast<size_t>(prep.primaryIndex)], *prep.primaryParameterEntry, prep.primaryParameterRoutes);
                ++applied;
            }

            for (const auto& shadowUpdate : prep.shadowParameterUpdates) {
                if (shadowUpdate.synth == nullptr || shadowUpdate.entry == nullptr) continue;
                applyOverrides(*shadowUpdate.synth, *shadowUpdate.entry);
                this->lastAppliedShadowFingerprints[shadowUpdate.synth] = shadowUpdate.stateFingerprint;
                ++applied;
            }
        }

        if (anyPrimarySwapped) {
            this->rebuildPrimaryInstrumentNoteRouting();
            if (this->activeKitLayoutPtr != nullptr) {
                for (int idx = this->primaryInstrumentSynthCount; idx < static_cast<int>(this->instrumentSynthPtrs.size()); ++idx) {
                    auto* shadowSynth = this->instrumentSynthPtrs[static_cast<size_t>(idx)].get();
                    if (shadowSynth == nullptr) continue;
                    const auto layoutIt = std::find_if(this->activeKitLayoutPtr->instrumentSlots.begin(), this->activeKitLayoutPtr->instrumentSlots.end(), [&shadowSynth](const KitLayout::InstrumentSlot& slot) { return slot.id == shadowSynth->getId(); });
                    if (layoutIt != this->activeKitLayoutPtr->instrumentSlots.end()) this->addShadowSynthNoteRoutes(*shadowSynth, *layoutIt);
                }
            }
        } else if (!removedShadowSynths.empty()) {
            for (auto& [note, synths] : this->instrumentSynthsByNote) {
                juce::ignoreUnused(note);
                std::erase_if(synths, [&removedShadowSynths](InstrumentSynth* synth) { return removedShadowSynths.find(synth) != removedShadowSynths.end(); });
            }
        }

        // Add prepared shadow synths and their note routes
        for (auto& prep : preparedSlots) {
            for (auto& ps : prep.shadows) {
                for (const auto& [note, synthPtr] : ps.noteRoutes)
                    this->instrumentSynthsByNote[note].push_back(synthPtr);
                auto* shadowSynth = ps.synth.get();
                this->lastAppliedShadowFingerprints[shadowSynth] = ps.stateFingerprint;
                this->lastAppliedShadowReloadFingerprints[shadowSynth] = ps.reloadFingerprint;
                this->instrumentSynthPtrs.push_back(std::move(ps.synth));
            }
        }

        this->lastAppliedSlotFingerprints = std::move(nextSlotFingerprints);
        this->lastAppliedSlotReloadFingerprints = std::move(nextSlotReloadFingerprints);
        this->lastAppliedInstrumentMapText = normalizedMapText;
    }
    // Old synths destroyed here outside the lock - audio thread cannot access them

    if (applied > 0 && this->sampleLoaderPtr != nullptr)
        this->sampleLoaderPtr->getQueue().startProcessing();

    if (applied <= 0 && statusLines.isEmpty())
        statusLines.add("Instrument map unchanged - no reload needed");

    if (statusLines.isEmpty())
        statusLines.add("Instrument map applied successfully");

    this->lastInstrumentMapStatus = statusLines.joinIntoString("\n");
    LOG(Info, "=== useInstrumentMap done: applied=" + juce::String(applied) + " oldSynths=" + juce::String(oldSynths.size())
        + " totalSynths=" + juce::String(this->instrumentSynthPtrs.size()) + " ===");
    return applied > 0;
}

ParseResult<DrumKitEngine::InstrumentMap> DrumKitEngine::parseInstrumentMapText(const juce::String& text) {
    return DrumKitEngineMappingModel::parseInstrumentMapText(text);
}
