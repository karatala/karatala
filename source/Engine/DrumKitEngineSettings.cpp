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

static juce::String safePresetName(const juce::String& name) {
    return juce::File::createLegalFileName(name.trim()).replaceCharacters(":#\r\n\"", "_____").trim();
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
    const auto safeName = safePresetName(name);
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

DrumKitEngine::LibraryVelocityResponseInfo DrumKitEngine::getLibraryVelocityResponseInfo(const juce::String& qualifiedLibraryArticulationId) const {
    LibraryVelocityResponseInfo result;
    if (this->libraryManagerPtr == nullptr) return result;

    juce::StringArray idParts;
    idParts.addTokens(qualifiedLibraryArticulationId.trim(), "/", "");
    idParts.removeEmptyStrings();
    if (idParts.size() < 2) return result;

    const auto* libraryKit = this->libraryManagerPtr->findKitById(idParts[0]);
    const auto* libInst = libraryKit != nullptr ? libraryKit->findInstrumentById(idParts[1]) : nullptr;
    if (libInst == nullptr) return result;

    const KitLib::ArticulationSpec* libArt = nullptr;
    if (idParts.size() >= 3) libArt = libInst->findArticulationById(idParts[2]);
    else if (libInst->articulationSpecs.size() == 1) libArt = libInst->articulationSpecs.front().get();
    if (libArt == nullptr) return result;

    result.libraryArticulationFound = true;
    std::optional<float> hardestFittedDb;
    for (auto velocityLevelIt = libArt->velocityLevels.rbegin(); velocityLevelIt != libArt->velocityLevels.rend(); ++velocityLevelIt) {
        if (*velocityLevelIt != nullptr && (*velocityLevelIt)->fittedSourceDb.has_value()) {
            hardestFittedDb = (*velocityLevelIt)->fittedSourceDb;
            break;
        }
    }

    result.levels.reserve(libArt->velocityLevels.size());
    for (const auto& velocityLevel : libArt->velocityLevels) {
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

void DrumKitEngine::logMessage(const juce::String& message, DebugLevel debugLevel) {
    juce::ignoreUnused(message);
    if (this->maxDebugLevel >= debugLevel) DBG(DebugLevelToString(debugLevel) + ": " + message);
}

bool DrumKitEngine::setMicLayout(MicLayout layout) {
    this->activeMicLayoutPtr = std::make_unique<MicLayout>(layout);
    return this->applyMicLayout();
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

    auto articulationFallback = ArticulationSlotFallbackRules::parseYaml(root["articulationFallback"], config.instrumentCatalog);
    if (!articulationFallback.ok()) return result.returnWithError(articulationFallback.error());
    config.articulationSlotFallbackRules = std::move(articulationFallback.value);
    for (const auto& warning : articulationFallback.errors) result.addWarning(warning);

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

        const auto& sampleSelectionNode = engineNode["sample_selection"];
        if (sampleSelectionNode.isMap()) {
            const auto minimumVelocityLevels = sampleSelectionNode["minimumVelocityLevels"].asString();
            if (minimumVelocityLevels.isNotEmpty()) this->engineSettings.floatingSelectionMinimumVelocityLevels.store(juce::jlimit(2, 5, minimumVelocityLevels.getIntValue()));
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
    result += ArticulationSlotFallbackRules::makeDefault().toYaml();
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
    result += "#   sample_selection:\n";
    result += "#     minimumVelocityLevels: 2\n";
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
    yaml += this->globalConfig.articulationSlotFallbackRules.toYaml();
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
    yaml += "  sample_selection:\n";
    yaml += "    minimumVelocityLevels: " + juce::String(juce::jlimit(2, 5, settings.floatingSelectionMinimumVelocityLevels.load())) + "\n";
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
