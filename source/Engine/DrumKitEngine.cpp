#include "DrumKitEngine.h"
#include "DrumKitEngineMappingModel.h"
#include "DrumKitEngineTextView.h"
#include "InstrumentSynth.h"
#include "SampleSelector.h"
#include "../Config/Helpers.h"
#include "../Config/YamlHelper.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace {
static bool containsId(const std::vector<uint32_t>& values, uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

static void addUniqueId(std::vector<uint32_t>& values, uint32_t value) {
    if (!containsId(values, value))
        values.push_back(value);
}

static void eraseId(std::vector<uint32_t>& values, uint32_t value) {
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
}

static std::vector<uint32_t> toArticulationIdList(const std::vector<juce::String>& names) {
    std::vector<uint32_t> result;
    result.reserve(names.size());
    for (const auto& name : names) {
        if (name.isNotEmpty())
            addUniqueId(result, StringsDictSingleton::getId(name.upToFirstOccurrenceOf("[", false, false).trim().toLowerCase()));
    }
    return result;
}

static std::vector<juce::String> effectiveArticulationFilter(const DrumKitEngine::InstrumentMapEntry& entry) {
    if (!entry.slotArticulationFilter.empty())
        return entry.slotArticulationFilter;
    return entry.articulationFilter;
}

static juce::String normalizeArticulationName(const juce::String& rawName) {
    return rawName.upToFirstOccurrenceOf("[", false, false).trim().toLowerCase();
}

static uint32_t resolveEntrySourceArtId(const DrumKitEngine::InstrumentMapEntry& entry, const juce::String& slotArtName) {
    if (slotArtName.isEmpty())
        return 0;

    const auto normalizedSlotArt = normalizeArticulationName(slotArtName);
    for (size_t idx = 0; idx < entry.slotArticulationFilter.size(); ++idx) {
        if (normalizeArticulationName(entry.slotArticulationFilter[idx]) != normalizedSlotArt)
            continue;
        if (idx < entry.articulationFilter.size() && entry.articulationFilter[idx].isNotEmpty())
            return StringsDictSingleton::getId(normalizeArticulationName(entry.articulationFilter[idx]));
        return StringsDictSingleton::getId(normalizeArticulationName(slotArtName));
    }

    if (!entry.articulationFilter.empty()) {
        for (const auto& sourceArtName : entry.articulationFilter) {
            if (sourceArtName.isNotEmpty())
                return StringsDictSingleton::getId(normalizeArticulationName(sourceArtName));
        }
    }

    return StringsDictSingleton::getId(normalizeArticulationName(slotArtName));
}

static bool stringArrayContainsIgnoreCase(const juce::StringArray& values, const juce::String& needle) {
    for (const auto& value : values) {
        if (value.compareIgnoreCase(needle) == 0)
            return true;
    }
    return false;
}

static int levenshteinDistance(const juce::String& left, const juce::String& right) {
    const int leftLength = left.length();
    const int rightLength = right.length();

    if (leftLength == 0)
        return rightLength;
    if (rightLength == 0)
        return leftLength;

    std::vector<int> previousRow(static_cast<size_t>(rightLength) + 1);
    std::vector<int> currentRow(static_cast<size_t>(rightLength) + 1);

    for (int column = 0; column <= rightLength; ++column)
        previousRow[static_cast<size_t>(column)] = column;

    for (int leftIdx = 1; leftIdx <= leftLength; ++leftIdx) {
        currentRow[0] = leftIdx;
        const auto leftChar = left[leftIdx - 1];

        for (int rightIdx = 1; rightIdx <= rightLength; ++rightIdx) {
            const int substitutionCost = (leftChar == right[rightIdx - 1]) ? 0 : 1;
            const int deletionCost = previousRow[static_cast<size_t>(rightIdx)] + 1;
            const int insertionCost = currentRow[static_cast<size_t>(rightIdx - 1)] + 1;
            const int substitutionTotal = previousRow[static_cast<size_t>(rightIdx - 1)] + substitutionCost;

            currentRow[static_cast<size_t>(rightIdx)] = std::min(std::min(deletionCost, insertionCost), substitutionTotal);
        }

        std::swap(previousRow, currentRow);
    }

    return previousRow[static_cast<size_t>(rightLength)];
}

static juce::StringArray collectInstrumentArticulationNames(const KitLib::InstrumentSpec& instrumentSpec) {
    juce::StringArray availableNames;
    for (const auto& articulationSpec : instrumentSpec.articulationSpecs) {
        if (articulationSpec == nullptr)
            continue;

        auto articulationName = articulationSpec->codeRaw.upToFirstOccurrenceOf("[", false, false).trim();
        if (articulationName.isEmpty())
            continue;

        if (!stringArrayContainsIgnoreCase(availableNames, articulationName))
            availableNames.add(articulationName);
    }

    availableNames.sort(true);
    return availableNames;
}

static int balanceSide(const std::optional<int>& balance) {
    if (!balance.has_value() || *balance == 0)
        return 0;
    return *balance < 0 ? -1 : 1;
}

static const KitLayout::InstrumentSlot* findLayoutSlot(const KitLayout& layout, const juce::String& slotId) {
    for (const auto& slot : layout.instrumentSlots)
        if (slot.id == slotId)
            return &slot;
    return nullptr;
}

static std::vector<const KitLib::ArticulationSpec*> collectOrientationArticulations(const KitLib::InstrumentSpec& instrumentSpec, const DrumKitEngine::InstrumentMapEntry& entry) {
    std::vector<const KitLib::ArticulationSpec*> result;

    if (entry.articulationFilter.empty()) {
        result.reserve(instrumentSpec.articulationSpecs.size());
        for (const auto& artSpec : instrumentSpec.articulationSpecs)
            if (artSpec != nullptr)
                result.push_back(artSpec.get());
        return result;
    }

    for (const auto& artName : entry.articulationFilter) {
        const auto normalizedArtName = normalizeArticulationName(artName);
        if (normalizedArtName.isEmpty())
            continue;

        const KitLib::ArticulationSpec* matched = nullptr;
        for (const auto& artSpec : instrumentSpec.articulationSpecs) {
            if (artSpec == nullptr)
                continue;
            if (normalizeArticulationName(artSpec->codeRaw) == normalizedArtName) {
                matched = artSpec.get();
                break;
            }
        }

        if (matched == nullptr)
            return {};

        result.push_back(matched);
    }

    return result;
}

static std::optional<bool> inferAutomaticFlipLR(const KitLayout::InstrumentSlot& slot, const KitLib::InstrumentSpec& instrumentSpec, const DrumKitEngine::InstrumentMapEntry& entry) {
    const int slotSide = balanceSide(slot.balance);
    if (slotSide == 0)
        return std::nullopt;

    const auto articulations = collectOrientationArticulations(instrumentSpec, entry);
    if (articulations.empty())
        return std::nullopt;

    std::optional<int> sourceSide;
    for (const auto* artSpec : articulations) {
        if (artSpec == nullptr || artSpec->balanceConflict)
            return std::nullopt;

        const int artSide = balanceSide(artSpec->balance);
        if (artSide == 0)
            return std::nullopt;

        if (!sourceSide.has_value())
            sourceSide = artSide;
        else if (*sourceSide != artSide)
            return std::nullopt;
    }

    if (!sourceSide.has_value())
        return std::nullopt;

    return *sourceSide != slotSide;
}

static void applyAutomaticOrientation(DrumKitEngine::InstrumentMap& instrumentMap, const KitLayout& layout, KitLib::LibraryManager& libraryManager) {
    for (auto& entry : instrumentMap.entries) {
        if (entry.flipLRExplicit || entry.slotId == "*")
            continue;

        const auto* slot = findLayoutSlot(layout, entry.slotId);
        if (slot == nullptr)
            continue;

        auto* kitSpec = libraryManager.findKit(entry.kitRef);
        if (kitSpec == nullptr || entry.instCode.isEmpty())
            continue;

        auto* instSpec = kitSpec->findInstrument(entry.instCode);
        if (instSpec == nullptr)
            continue;

        const auto inferredFlip = inferAutomaticFlipLR(*slot, *instSpec, entry);
        if (inferredFlip.has_value())
            entry.flipLR = *inferredFlip;
    }
}
static juce::String findClosestArticulationName(const juce::String& unknownName, const juce::StringArray& availableNames) {
    const auto normalizedUnknownName = normalizeArticulationName(unknownName);
    if (normalizedUnknownName.isEmpty() || availableNames.isEmpty())
        return {};

    juce::String bestMatch;
    int bestDistance = 1000000;

    for (const auto& availableName : availableNames) {
        const auto normalizedAvailableName = normalizeArticulationName(availableName);
        if (normalizedAvailableName.isEmpty())
            continue;

        const int distance = levenshteinDistance(normalizedUnknownName, normalizedAvailableName);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestMatch = availableName;
        }
    }

    if (bestDistance <= 2)
        return bestMatch;

    return {};
}

static juce::String makeInstrumentMapEntryFingerprint(const DrumKitEngine::InstrumentMapEntry& entry) {
    auto joinValues = [](const std::vector<juce::String>& values) {
        juce::String out;
        for (size_t idx = 0; idx < values.size(); ++idx) {
            if (idx > 0) out += ",";
            out += values[idx].trim().toLowerCase();
        }
        return out;
    };

    auto joinBleedValues = [](const std::unordered_map<juce::String, float>& values) {
        std::vector<juce::String> keys;
        keys.reserve(values.size());
        for (const auto& [key, value] : values) {
            juce::ignoreUnused(value);
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end(), [](const juce::String& a, const juce::String& b) {
            return a.compareIgnoreCase(b) < 0;
        });

        juce::String out;
        for (size_t idx = 0; idx < keys.size(); ++idx) {
            if (idx > 0) out += ",";
            const auto& key = keys[idx];
            out += key.trim().toLowerCase() + ":" + juce::String(values.at(key), 4);
        }
        return out;
    };

    juce::String fp;
    fp += entry.slotId.trim().toLowerCase() + "|";
    fp += entry.kitRef.trim().toLowerCase() + "|";
    fp += entry.instCode.trim().toLowerCase() + "|";
    fp += joinValues(entry.slotArticulationFilter) + "|";
    fp += joinValues(entry.articulationFilter) + "|";
    fp += entry.tuningCents.has_value() ? juce::String(*entry.tuningCents) : "_";
    fp += "|";
    fp += entry.attackTimeMs.has_value() ? juce::String(*entry.attackTimeMs) : "_";
    fp += "|";
    fp += entry.decayTimeMs.has_value() ? juce::String(*entry.decayTimeMs) : "_";
    fp += "|";
    fp += entry.volumeDb.has_value() ? juce::String(*entry.volumeDb, 4) : "_";
    fp += "|";
    fp += entry.flipLR ? "1" : "0";
    fp += entry.flipLRExplicit ? "e" : "a";
    fp += "|";
    fp += entry.flipPhase ? "1" : "0";
    fp += "|";
    fp += joinBleedValues(entry.micBleedLevels);
    return fp;
}

static std::unordered_map<juce::String, juce::String> buildSlotFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap) {
    std::unordered_map<juce::String, juce::String> result;
    for (const auto& entry : instrumentMap.entries)
        result[entry.slotId] += makeInstrumentMapEntryFingerprint(entry) + "\n";
    return result;
}

static bool canReuseSlotWithoutReload(const KitLayout::InstrumentSlot& oldSlot, const KitLayout::InstrumentSlot& newSlot) {
    if (oldSlot.type != newSlot.type)
        return false;

    if (oldSlot.articulations.size() != newSlot.articulations.size())
        return false;

    for (size_t idx = 0; idx < oldSlot.articulations.size(); ++idx) {
        const auto& oldArt = oldSlot.articulations[idx];
        const auto& newArt = newSlot.articulations[idx];
        if (oldArt.nameId != newArt.nameId || oldArt.exprId != newArt.exprId)
            return false;
    }

    return true;
}

static void copySynthOverrides(InstrumentSynth& target, const InstrumentSynth& source) {
    target.setTuningOffsetCents(source.getTuningOffsetCents());

    const auto attackOverride = source.getAttackOverrideMs();
    if (attackOverride.has_value())
        target.setAttackOverrideMs(*attackOverride);

    const auto decayOverride = source.getDecayOverrideMs();
    if (decayOverride.has_value())
        target.setDecayOverrideMs(*decayOverride);

    target.setVolumeDb(source.getVolumeDb());
    target.setFlipLR(source.getFlipLR());
    target.setFlipPhase(source.getFlipPhase());
    target.setMicBleedLevels(source.getMicBleedLevels());
}

static void prepareSynthIfNeeded(InstrumentSynth& synth, double sampleRate, int samplesPerBlock) {
    if (sampleRate <= 0.0 || samplesPerBlock <= 0)
        return;

    synth.setCurrentPlaybackSampleRate(sampleRate);
    synth.prepareToPlay(sampleRate, samplesPerBlock);
}

static std::unique_ptr<InstrumentSynth> createReplacementSynth(DrumKitEngine* owner, const KitLayout::InstrumentSlot& layoutSlot, const InstrumentSynth& source, double sampleRate, int samplesPerBlock) {
    auto replacement = std::make_unique<InstrumentSynth>(owner, layoutSlot);
    replacement->setMicOutputMapping(source.getMicOutputMapping());
    replacement->setMicChannelCount(source.getMicChannelCount());

    const auto& routingContext = source.getLastRoutingContext();
    if (routingContext.isValid()) {
        replacement->loadInstrumentWithSources(routingContext.defaultInstrument, routingContext.artSources, routingContext.allowedArticulationNameIds, routingContext.artNameRemap, routingContext.artAliases);
    } else if (auto* loadedInstrument = source.getLoadedInstrumentSpec()) {
        replacement->loadInstrument(const_cast<KitLib::InstrumentSpec*>(loadedInstrument));
    }

    copySynthOverrides(*replacement, source);
    prepareSynthIfNeeded(*replacement, sampleRate, samplesPerBlock);
    return replacement;
}

static juce::String getSlotArticulationName(const KitLayout::InstrumentSlot::ArticulationTrigger& art) {
    auto artName = StringsDictSingleton::nameOf(art.nameId);
    if (artName.isEmpty())
        artName = art.codeRaw;
    return artName;
}

static KitLib::InstrumentSpec* findMatchingKitInstrument(
    const KitLayout::InstrumentSlot& layoutInstrument,
    const KitLib::DrumKitSpec& kit,
    std::unordered_set<KitLib::InstrumentSpec*>& usedInstruments,
    std::map<KitLib::InstrumentSpec::Type, int>& nextIndexByType) {
    KitLib::InstrumentSpec* matched = nullptr;

    if (layoutInstrument.type != KitLib::InstrumentSpec::Type::Unknown) {
        auto candidates = kit.getInstrumentsByType(layoutInstrument.type);
        int requestedIdx = -1;
        int pos = static_cast<int>(layoutInstrument.id.length()) - 1;
        while (pos >= 0 && juce::CharacterFunctions::isDigit(layoutInstrument.id[pos])) --pos;
        const auto digits = layoutInstrument.id.substring(pos + 1);
        if (digits.isNotEmpty()) requestedIdx = digits.getIntValue() - 1;

        if (requestedIdx >= 0 && requestedIdx < static_cast<int>(candidates.size()) 
          && usedInstruments.find(candidates[static_cast<size_t>(requestedIdx)]) == usedInstruments.end()
        ) {
            matched = candidates[static_cast<size_t>(requestedIdx)];
        }

        if (matched == nullptr) {
            int& next = nextIndexByType[layoutInstrument.type];
            for (int idx = next; idx < static_cast<int>(candidates.size()); ++idx) {
                if (usedInstruments.find(candidates[static_cast<size_t>(idx)]) == usedInstruments.end()) {
                    matched = candidates[static_cast<size_t>(idx)];
                    next = idx + 1;
                    break;
                }
            }
        }

        if (matched == nullptr) {
            for (int idx = 0; idx < static_cast<int>(candidates.size()); ++idx) {
                if (usedInstruments.find(candidates[static_cast<size_t>(idx)]) == usedInstruments.end()) {
                    matched = candidates[static_cast<size_t>(idx)];
                    nextIndexByType[layoutInstrument.type] = idx + 1;
                    break;
                }
            }
        }
    }

    if (matched == nullptr) {
        const auto needle = layoutInstrument.id.trim().toLowerCase();
        for (const auto& [id, instPtr] : kit.getInstrumentSpecsMap()) {
            juce::ignoreUnused(id);
            auto* candidate = instPtr.get();
            if (candidate == nullptr || usedInstruments.find(candidate) != usedInstruments.end()) continue;
            if (candidate->meta.codeRaw.trim().toLowerCase().contains(needle)
                || candidate->meta.name.trim().toLowerCase().contains(needle)) {
                matched = candidate;
                break;
            }
        }
    }

    if (matched != nullptr) usedInstruments.insert(matched);
    return matched;
}
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
        if (parsedSuggestedMap.ok()) {
            this->lastAppliedSlotFingerprints = buildSlotFingerprintMap(parsedSuggestedMap.value);
            this->lastAppliedInstrumentMapText = this->suggestedInstrumentMapText;
        } else {
            this->lastAppliedSlotFingerprints.clear();
            this->lastAppliedInstrumentMapText.clear();
        }
        this->lastInstrumentMapStatus = "Mapped default kit and generated starter instrument map";//FIXME: refactor, ui relates shit out
    } else {
        this->suggestedInstrumentMapText.clear();
        this->lastAppliedSlotFingerprints.clear();
        this->lastAppliedInstrumentMapText.clear();
    }
}

juce::String DrumKitEngine::buildInstrumentMapForKit(const KitLayout& layout, const KitLib::DrumKitSpec& kit) const {
    InstrumentMap result;
    std::unordered_set<KitLib::InstrumentSpec*> usedInstruments;
    std::map<KitLib::InstrumentSpec::Type, int> nextIndexByType;

    for (const auto& slot : layout.instrumentSlots) {
        auto* matched = findMatchingKitInstrument(slot, kit, usedInstruments, nextIndexByType);
        if (matched == nullptr) continue;
        InstrumentMapEntry entry;
        entry.slotId = slot.id;
        entry.kitRef = kit.meta.codeRaw;
        entry.instCode = matched->meta.codeRaw;
        if (const auto inferredFlip = inferAutomaticFlipLR(slot, *matched, entry))
            entry.flipLR = *inferredFlip;
        result.entries.push_back(std::move(entry));
    }

    return serializeInstrumentMap(result);
}

juce::String DrumKitEngine::serializeInstrumentMap(const InstrumentMap& instrumentMap) {
    juce::StringArray lines;

    auto joinNames = [](const std::vector<juce::String>& names) {
        juce::StringArray values;
        for (const auto& name : names)
            if (name.trim().isNotEmpty()) values.add(name.trim());
        return values.joinIntoString(",");
    };

    for (const auto& entry : instrumentMap.entries) {
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

        auto line = slotRef + ":" + targetRef;
        if (!params.isEmpty())
            line += ":" + params.joinIntoString(",");
        lines.add(line);
    }

    return lines.joinIntoString("\n");
}

void DrumKitEngine::regenerateSuggestedInstrumentMapText() { //TODO: refactor, move to ui related helper, or generate on demand in UI instead of storing in engine
    this->suggestedInstrumentMapText = DrumKitEngineTextView::buildSuggestedInstrumentMapText(this->activeKitLayoutPtr.get(), this->instrumentSynthById);
}

void DrumKitEngine::seedInstrumentMapFingerprints(const juce::String& mapText) {
    auto parsed = DrumKitEngine::parseInstrumentMapText(mapText);
    if (parsed.ok()) {
        this->lastAppliedSlotFingerprints = buildSlotFingerprintMap(parsed.value);
        this->lastAppliedInstrumentMapText = mapText;
    }
}

juce::String DrumKitEngine::buildReferenceText() const {
    return DrumKitEngineTextView::buildReferenceText(this->activeKitLayoutPtr.get(), this->getInstrumentManager());
}

void DrumKitEngine::mapKit(const KitLib::DrumKitSpec& kit) { //FIXME: review, probably rewrite, simplify
    if (this->activeKitLayoutPtr == nullptr) {
        LOG(Warning, "no active kit layout to map to");
        return;
    }

    std::unordered_set<KitLib::InstrumentSpec*> usedInstSpecSet;
    std::map<KitLib::InstrumentSpec::Type, int> nextIndexByType;
    

    for (const auto& layoutInstrument : this->activeKitLayoutPtr->instrumentSlots) { //FIXME: rewrite loop thru instrumentSynthById
        auto sit = this->instrumentSynthById.find(layoutInstrument.id);
        if (sit == this->instrumentSynthById.end()) {
            LOG(Warning, "layout instrument slot not present: " + layoutInstrument.id);
            continue;
        }
        InstrumentSynth* slot = sit->second;

        auto* matched = findMatchingKitInstrument(layoutInstrument, kit, usedInstSpecSet, nextIndexByType);

        if (matched == nullptr) {
            LOG(Warning, "no matching instrument found in kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");
            slot->unloadInstrument();
            continue;
        }

        LOG(Info, "Loading instrument '" + matched->meta.codeRaw + "' from kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");

        slot->loadInstrument(matched); //TODO: wrap in performance measuring block
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
            if (!canReuseSlotWithoutReload(oldPrimary->getInstrumentSlot(), layoutSlot))
                prepared.primaryReplacement = createReplacementSynth(this, layoutSlot, *oldPrimary, this->lastSampleRate, this->lastSamplesPerBlock);

            auto shadowIt = existingState.shadowIndicesBySlotId.find(layoutSlot.id);
            if (shadowIt != existingState.shadowIndicesBySlotId.end()) {
                prepared.shadows.reserve(shadowIt->second.size());
                for (const int shadowIndex : shadowIt->second) {
                    PreparedShadow shadow;
                    shadow.oldIndex = shadowIndex;

                    auto* oldShadow = existingState.rawSynths[static_cast<size_t>(shadowIndex)];
                    if (!canReuseSlotWithoutReload(oldShadow->getInstrumentSlot(), layoutSlot))
                        shadow.replacement = createReplacementSynth(this, layoutSlot, *oldShadow, this->lastSampleRate, this->lastSamplesPerBlock);

                    prepared.shadows.push_back(std::move(shadow));
                }
            }
        } else {
            prepared.primaryReplacement = std::make_unique<InstrumentSynth>(this, layoutSlot);
            prepareSynthIfNeeded(*prepared.primaryReplacement, this->lastSampleRate, this->lastSamplesPerBlock);
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
                shadowSynth->rebuildRoutingFromLayout(prepared.layoutSlot);

                for (const auto& art : prepared.layoutSlot.articulations) {
                    if (!shadowSynth->supportsArticulation(getSlotArticulationName(art)))
                        continue;

                    for (const int note : art.notes) {
                        if (note >= 0 && note < 128)
                            this->instrumentSynthsByNote[note].push_back(shadowSynth);
                    }
                }
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
                this->trackedCCMask[cc.number] = true;
        }

        for (const auto& art : instrumentSlot.articulations) {
            for (const int note : art.notes) {
                if (note < 0 || note >= 128)
                    continue;
                this->trackedPAMask[note] = true;
                if (synth->hasRouteForMidiNote(note))
                    this->instrumentSynthsByNote[note].push_back(synth);
            }
        }
    }
}

void DrumKitEngine::clearShadowSynths() {
    if (static_cast<int>(this->instrumentSynthPtrs.size()) > this->primaryInstrumentSynthCount)
        this->instrumentSynthPtrs.erase(this->instrumentSynthPtrs.begin() + this->primaryInstrumentSynthCount, this->instrumentSynthPtrs.end());

    this->rebuildPrimaryInstrumentNoteRouting();
}

void DrumKitEngine::rebuildMidiNoteRoutingFromLayout() {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    this->rebuildPrimaryInstrumentNoteRouting();
    this->buildNoteNameCache();
}

void DrumKitEngine::buildNoteNameCache() {
    for (int i = 0; i < 128; ++i) this->noteNameCache[i].clear();

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
            LOG(Debug, "Assigning mic " + mic.name + " (" + juce::String(mic.specs.channels) + "ch)" + " for instrument '" + mic.instrument + "'");
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

    const auto& name = this->noteNameCache[note];
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
            auto artCode = art.codeRaw.upToFirstOccurrenceOf("[", false, false);
            if (artCode == artName || artName == "(default)") {
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

namespace {

constexpr int previewVelocityBandSize = 8;

int getPreviewVelocityBucket(juce::uint8 velocity) {
    return juce::jlimit(0, 15, (static_cast<int>(velocity) - 1) / previewVelocityBandSize);
}

juce::uint8 getPreviewVelocityForBucket(juce::uint8 velocity) {
    const int bucket = getPreviewVelocityBucket(velocity);
    const int bucketVelocity = bucket * previewVelocityBandSize + previewVelocityBandSize / 2;
    return static_cast<juce::uint8>(juce::jlimit(1, 127, bucketVelocity));
}

int getPreviewMicRank(const KitLib::SampleVariantSpec& variant) {
    if (!variant.isValid() || !variant.micSpec.isValid())
        return std::numeric_limits<int>::max();

    const auto& mic = variant.micSpec;
    const bool stereoCapable = mic.channels >= 2
        || mic.format == KitLib::MicSpec::Format::Stereo
        || mic.format == KitLib::MicSpec::Format::MidSide
    ;
    if (mic.type == KitLib::MicSpec::Type::Overhead) {
        if (mic.raw.equalsIgnoreCase("ons")) return 0;

        int rank = stereoCapable ? 10 : 30;
        if (mic.position == KitLib::MicSpec::Position::Normal)
            rank -= 2;
        else if (mic.position == KitLib::MicSpec::Position::Wide)
            rank += 2;
        return rank;
    }

    if (mic.type == KitLib::MicSpec::Type::Direct) {
        int rank = stereoCapable ? 90 : 100;
        if (mic.position == KitLib::MicSpec::Position::Top || mic.position == KitLib::MicSpec::Position::In)
            rank -= 4;
        else if (mic.position == KitLib::MicSpec::Position::Out)
            rank -= 2;
        else if (mic.position == KitLib::MicSpec::Position::Bottom)
            rank += 2;
        return rank;
    }
    if (mic.type == KitLib::MicSpec::Type::Room) {
        int rank = stereoCapable ? 200 : 220;
        if (mic.position == KitLib::MicSpec::Position::Close)
            rank -= 2;
        else if (mic.position == KitLib::MicSpec::Position::Far)
            rank += 2;
        return rank;
    }
    return 300;
}

bool isBetterPreviewSampleVariant(const KitLib::SampleVariantSpec& candidate, const KitLib::SampleVariantSpec& current) {
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

} // namespace

juce::ThreadPoolJob::JobStatus DrumKitEngine::PreviewResolveJob::runJob() {
    this->owner.resolveAndLoadPreviewTarget(std::move(this->cacheKey),
                                            this->kitCodeRaw,
                                            this->instCode,
                                            this->artCode,
                                            this->resolveVelocity,
                                            this->playbackVelocity);
    return jobHasFinished;
}

std::string DrumKitEngine::makePreviewTargetCacheKey(const juce::String& kitCodeRaw,
                                                     const juce::String& instCode,
                                                     const juce::String& artCode,
                                                     juce::uint8 velocity) const {
    const auto key = kitCodeRaw.trim().toLowerCase()
                   + "|" + instCode.trim().toLowerCase()
                   + "|" + artCode.trim().toLowerCase()
                   + "|" + juce::String(getPreviewVelocityBucket(velocity));
    return key.toStdString();
}

std::optional<DrumKitEngine::PreviewTarget> DrumKitEngine::resolvePreviewTarget(const juce::String& kitCodeRaw,
                                                                                 const juce::String& instCode,
                                                                                 const juce::String& artCode,
                                                                                 juce::uint8 velocity) {
    auto* libMgr = this->libraryManagerPtr.get();
    if (libMgr == nullptr)
        return std::nullopt;

    auto* kit = libMgr->findKit(kitCodeRaw);
    if (kit == nullptr)
        return std::nullopt;

    auto* inst = kit->findInstrument(instCode);
    if (inst == nullptr)
        return std::nullopt;

    if (inst->articulationSpecs.empty())
        inst->scanInstrumentSamples();

    KitLib::ArticulationSpec* artSpec = nullptr;
    if (artCode.isNotEmpty())
        artSpec = inst->findArticulation(artCode);

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

    KitLib::VelocityLayerSpec* bestLayer = nullptr;
    int bestDist = std::numeric_limits<int>::max();
    for (auto& layer : artSpec->velocityLayerSpecs) {
        const int dist = std::abs(static_cast<int>(layer->velocity) - static_cast<int>(velocity));
        if (dist < bestDist) {
            bestDist = dist;
            bestLayer = layer.get();
        }
    }

    if (bestLayer == nullptr || bestLayer->sampleVariantSpecs.empty())
        return std::nullopt;

    KitLib::SampleVariantSpec* bestVariant = nullptr;
    for (auto& sampleVariant : bestLayer->sampleVariantSpecs) {
        if (sampleVariant == nullptr || !sampleVariant->isValid()) continue;

        if (bestVariant == nullptr || isBetterPreviewSampleVariant(*sampleVariant, *bestVariant))
            bestVariant = sampleVariant.get();
    }

    if (bestVariant == nullptr) return std::nullopt;

    PreviewTarget target;
    target.sampleFile = bestVariant->sampleFile;
    target.micCode = bestVariant->micSpec.raw;
    target.fileChannelStart = juce::jmax(0, bestVariant->fileChannelStart);
    target.fileChannelCount = bestVariant->fileChannelCount > 0 ? bestVariant->fileChannelCount : bestVariant->micSpec.channels;
    target.volumeTrimDb = bestVariant->volumeTrimDb;
    return target;
}

void DrumKitEngine::requestPreviewPlayback(const PreviewTarget& target, juce::uint8 velocity) {
    auto* loader = this->sampleLoaderPtr.get();

    if (loader == nullptr || !target.sampleFile.existsAsFile())
        return;

    const float gain = juce::Decibels::decibelsToGain(target.volumeTrimDb)
                     * (static_cast<float>(velocity) / 127.0f);

    SampleLoadSpec loadSpec;
    loadSpec.file = target.sampleFile;
    loadSpec.volumeTrimDb = 0.0f;
    loadSpec.preprocessTag = "preview";

    const int fileChannelStart = juce::jmax(0, target.fileChannelStart);
    const int fileChannelCount = target.fileChannelCount;

    loader->requestAsyncLoad(loadSpec, [this, gain, fileChannelStart, fileChannelCount](std::shared_ptr<juce::AudioBuffer<float>> buffer, double sampleRate) {
        if (buffer == nullptr || buffer->getNumChannels() <= 0 || buffer->getNumSamples() <= 0) return;

        const int sourceChannels = buffer->getNumChannels();
        const int sourceStart = juce::jlimit(0, sourceChannels - 1, fileChannelStart);
        const int availableChannels = sourceChannels - sourceStart;
        const int selectedChannels = juce::jlimit(1, availableChannels, fileChannelCount > 0 ? fileChannelCount : availableChannels);

        auto selectedBuffer = std::make_shared<juce::AudioBuffer<float>>(2, buffer->getNumSamples());
        selectedBuffer->copyFrom(0, 0, *buffer, sourceStart, 0, buffer->getNumSamples());
        if (selectedChannels >= 2)
            selectedBuffer->copyFrom(1, 0, *buffer, sourceStart + 1, 0, buffer->getNumSamples());
        else
            selectedBuffer->copyFrom(1, 0, *buffer, sourceStart, 0, buffer->getNumSamples());

        const juce::SpinLock::ScopedLockType scopedLock(this->previewLock);
        this->previewBuffer = std::move(selectedBuffer);
        this->previewSampleRate = sampleRate;
        this->previewGain = gain;
        this->previewReadPos.store(0);
    });
}

void DrumKitEngine::resolveAndLoadPreviewTarget(std::string cacheKey,
                                                const juce::String& kitCodeRaw,
                                                const juce::String& instCode,
                                                const juce::String& artCode,
                                                juce::uint8 resolveVelocity,
                                                juce::uint8 playbackVelocity) {
    auto target = this->resolvePreviewTarget(kitCodeRaw, instCode, artCode, resolveVelocity);

    {
        const juce::ScopedLock resolutionLock(this->previewTargetResolutionLock);
        this->pendingPreviewTargetKeys.erase(cacheKey);
        if (target.has_value())
            this->previewTargetCache[cacheKey] = *target;
    }

    if (target.has_value())
        this->requestPreviewPlayback(*target, playbackVelocity);
}


void DrumKitEngine::previewSourceItem(const juce::String& kitCodeRaw, const juce::String& instCode,
                                      const juce::String& artCode, juce::uint8 velocity) {
    const auto cacheKey = this->makePreviewTargetCacheKey(kitCodeRaw, instCode, artCode, velocity);
    std::optional<PreviewTarget> cachedTarget;

    {
        const juce::ScopedLock resolutionLock(this->previewTargetResolutionLock);
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
    this->previewResolveThreadPool.addJob(new PreviewResolveJob(*this,
                                                                cacheKey,
                                                                kitCodeRaw,
                                                                instCode,
                                                                artCode,
                                                                resolveVelocity,
                                                                velocity),
                                          true);
}


void DrumKitEngine::saveLatestControllerValues(const juce::MidiBuffer& midiMessages) {
    for (const auto& midiMess : midiMessages) {
        const juce::MidiMessage msg = midiMess.getMessage();
        if (msg.isController()) {
            const auto ccNum = msg.getControllerNumber();
            if (ccNum >= 0 && ccNum <= 127 && this->trackedCCMask[ccNum])
                this->latestCCValues[ccNum] = msg.getControllerValue();
        } else if (msg.isAftertouch()) {
            const auto note = msg.getNoteNumber();
            if (note >= 0 && note <= 127 && this->trackedPAMask[note])
                this->latestPAValues[note] = msg.getAfterTouchValue();
        }
    }
}

void DrumKitEngine::updateSlotParameters(const juce::String& slotId,
                                         const juce::String& artName,
                                         int layerIndex,
                                         std::optional<int> tuningCents,
                                         std::optional<int> attackMs,
                                         std::optional<int> decayMs) {
    LayerParameterUpdate update;
    update.tuningCents = tuningCents;
    update.attackMs = attackMs;
    update.decayMs = decayMs;
    this->updateSlotParameters(slotId, artName, layerIndex, update);
}

bool DrumKitEngine::updateSlotSourceParameters(const juce::String& slotId,
                                               const juce::String& artName,
                                               const juce::String& sourcePath,
                                               int sourceOccurrenceIndex,
                                               const LayerParameterUpdate& update) {
    if (sourceOccurrenceIndex < 0)
        return false;

    juce::StringArray sourceParts;
    sourceParts.addTokens(sourcePath.trim(), "/", "");
    sourceParts.removeEmptyStrings();
    if (sourceParts.size() < 2)
        return false;

    const auto targetKit = sourceParts[0].trim();
    const auto targetInst = sourceParts[1].trim();
    const auto targetArt = sourceParts.size() >= 3 ? sourceParts[2].trim() : juce::String();

    std::vector<int> targetNotes;
    const bool hasArtFilter = artName.trim().isNotEmpty() && artName.trim() != "(default)";
    if (hasArtFilter && this->activeKitLayoutPtr != nullptr) {
        const auto normalizedArtName = normalizeArticulationName(artName);
        for (const auto& slot : this->activeKitLayoutPtr->instrumentSlots) {
            if (slot.id != slotId)
                continue;
            for (const auto& art : slot.articulations) {
                if (normalizeArticulationName(art.codeRaw) != normalizedArtName)
                    continue;
                for (const auto note : art.notes) {
                    if (note >= 0 && note < 128)
                        targetNotes.push_back(note);
                }
                break;
            }
            break;
        }
    }

    auto synthMatchesArt = [&targetNotes](const InstrumentSynth& synth) {
        if (targetNotes.empty())
            return true;
        for (const auto note : targetNotes) {
            if (synth.hasRouteForMidiNote(note))
                return true;
        }
        return false;
    };

    auto synthMatchesSource = [&targetKit, &targetInst, &targetArt](const InstrumentSynth& synth) {
        const auto* inst = synth.getLoadedInstrumentSpec();
        if (inst == nullptr || inst->parent == nullptr)
            return false;
        if (inst->parent->meta.codeRaw.compareIgnoreCase(targetKit) != 0)
            return false;
        if (inst->meta.codeRaw.compareIgnoreCase(targetInst) != 0)
            return false;
        return targetArt.isEmpty() || synth.supportsArticulation(targetArt);
    };

    const juce::ScopedLock graphLock(this->engineGraphLock);

    int currentOccurrence = 0;
    for (auto& synthPtr : this->instrumentSynthPtrs) {
        if (synthPtr == nullptr)
            continue;
        if (synthPtr->getId() != slotId)
            continue;
        if (!synthMatchesArt(*synthPtr) || !synthMatchesSource(*synthPtr))
            continue;

        if (currentOccurrence == sourceOccurrenceIndex) {
            if (update.tuningCents.has_value()) synthPtr->setTuningOffsetCents(*update.tuningCents);
            if (update.attackMs.has_value()) synthPtr->setAttackOverrideMs(*update.attackMs);
            if (update.decayMs.has_value()) synthPtr->setDecayOverrideMs(*update.decayMs);
            if (update.volumeDb.has_value()) synthPtr->setVolumeDb(*update.volumeDb);
            if (update.flipLR.has_value()) synthPtr->setFlipLR(*update.flipLR);
            if (update.flipPhase.has_value()) synthPtr->setFlipPhase(*update.flipPhase);
            if (update.micBleedLevels.has_value()) synthPtr->setMicBleedLevels(*update.micBleedLevels);
            return true;
        }

        ++currentOccurrence;
    }

    return false;
}
void DrumKitEngine::updateSlotParameters(const juce::String& slotId, const juce::String& artName, int layerIndex, const LayerParameterUpdate& update) {
    if (layerIndex < 0)
        return;

    std::vector<int> targetNotes;
    const bool hasArtFilter = artName.trim().isNotEmpty() && artName.trim() != "(default)";
    if (hasArtFilter && this->activeKitLayoutPtr != nullptr) {
        const auto normalizedArtName = normalizeArticulationName(artName);
        for (const auto& slot : this->activeKitLayoutPtr->instrumentSlots) {
            if (slot.id != slotId)
                continue;
            for (const auto& art : slot.articulations) {
                if (normalizeArticulationName(art.codeRaw) != normalizedArtName)
                    continue;
                for (const auto note : art.notes) {
                    if (note >= 0 && note < 128)
                        targetNotes.push_back(note);
                }
                break;
            }
            break;
        }
    }

    auto synthMatchesArt = [&targetNotes](const InstrumentSynth& synth) {
        if (targetNotes.empty())
            return true;
        for (const auto note : targetNotes) {
            if (synth.hasRouteForMidiNote(note))
                return true;
        }
        return false;
    };

    const juce::ScopedLock graphLock(this->engineGraphLock);

    int currentLayerIndex = 0;
    for (auto& synthPtr : this->instrumentSynthPtrs) {
        if (synthPtr == nullptr)
            continue;
        if (synthPtr->getId() != slotId)
            continue;
        if (!synthMatchesArt(*synthPtr))
            continue;

        if (currentLayerIndex == layerIndex) {
            if (update.tuningCents.has_value()) synthPtr->setTuningOffsetCents(*update.tuningCents);
            if (update.attackMs.has_value()) synthPtr->setAttackOverrideMs(*update.attackMs);
            if (update.decayMs.has_value()) synthPtr->setDecayOverrideMs(*update.decayMs);
            if (update.volumeDb.has_value()) synthPtr->setVolumeDb(*update.volumeDb);
            if (update.flipLR.has_value()) synthPtr->setFlipLR(*update.flipLR);
            if (update.flipPhase.has_value()) synthPtr->setFlipPhase(*update.flipPhase);
            if (update.micBleedLevels.has_value()) synthPtr->setMicBleedLevels(*update.micBleedLevels);
            return;
        }

        ++currentLayerIndex;
    }
}

bool DrumKitEngine::useInstrumentMap(juce::String text) {
    // ======================== PHASE 1: VALIDATION (no lock) ========================
    // Safe to read libraryManagerPtr/activeKitLayoutPtr without lock - only mutated on
    // the message thread, which is our current thread.
    this->lastInstrumentMapStatus.clear();

    if (text.trim().isEmpty()) {
        this->lastInstrumentMapStatus = "Instrument map is empty";
        LOG(Warning, this->lastInstrumentMapStatus);
        return false;
    }

    if (this->libraryManagerPtr == nullptr) {
        this->lastInstrumentMapStatus = "LibraryManager is not initialized";
        LOG(Error, this->lastInstrumentMapStatus);
        return false;
    }

    if (this->activeKitLayoutPtr == nullptr) {
        this->lastInstrumentMapStatus = "Cannot apply instrument map without an active kit layout";
        LOG(Error, this->lastInstrumentMapStatus);
        return false;
    }

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

    applyAutomaticOrientation(parsed.value, *this->activeKitLayoutPtr, *this->libraryManagerPtr);

    LOG(Notice, "=== useInstrumentMap: " + juce::String(parsed.value.entries.size()) + " entries ===");
    for (size_t idx = 0; idx < parsed.value.entries.size(); ++idx) {
        const auto& entry = parsed.value.entries[idx];
        juce::String slotArtStr, srcArtStr;
        for (const auto& sa : entry.slotArticulationFilter) slotArtStr += (slotArtStr.isEmpty() ? "" : ",") + sa;
        for (const auto& sa : entry.articulationFilter) srcArtStr += (srcArtStr.isEmpty() ? "" : ",") + sa;
        LOG(Notice, "  entry[" + juce::String(idx) + "]: slot=" + entry.slotId
            + (slotArtStr.isNotEmpty() ? "/" + slotArtStr : "")
            + " -> kit=" + entry.kitRef + " inst=" + entry.instCode
            + (srcArtStr.isNotEmpty() ? " art=" + srcArtStr : "")
            + (entry.tuningCents.has_value() ? " tuning=" + juce::String(*entry.tuningCents) : "")
            + (entry.attackTimeMs.has_value() ? " attack=" + juce::String(*entry.attackTimeMs) : "")
            + (entry.decayTimeMs.has_value() ? " decay=" + juce::String(*entry.decayTimeMs) : ""));
    }

    struct ShadowPlan {
        const InstrumentMapEntry* entry = nullptr;
        KitLib::InstrumentSpec* instSpec = nullptr;
        std::vector<juce::String> articulations;
        InstrumentSynth::ArticulationNameRemap artNameRemap;
    };

    // Build a name remap from an entry's slot/source art filters for cross-art mapping
    auto buildArtNameRemap = [](const InstrumentMapEntry& entry) -> InstrumentSynth::ArticulationNameRemap {
        InstrumentSynth::ArticulationNameRemap remap;
        const auto& slotArts = entry.slotArticulationFilter;
        const auto& srcArts = entry.articulationFilter;
        if (slotArts.empty() || srcArts.empty())
            return remap;
        for (size_t idx = 0; idx < slotArts.size() && idx < srcArts.size(); ++idx) {
            auto slotId = StringsDictSingleton::getId(normalizeArticulationName(slotArts[idx]));
            auto srcId = StringsDictSingleton::getId(normalizeArticulationName(srcArts[idx]));
            if (slotId != srcId) 
                remap[slotId] = srcId;
        }
        return remap;
    };

    auto applyOverrides = [](InstrumentSynth& synth, const InstrumentMapEntry& entry) {
        synth.setTuningOffsetCents(entry.tuningCents.value_or(0));
        if (entry.attackTimeMs.has_value())
            synth.setAttackOverrideMs(*entry.attackTimeMs);
        if (entry.decayTimeMs.has_value())
            synth.setDecayOverrideMs(*entry.decayTimeMs);
        synth.setVolumeDb(entry.volumeDb.value_or(0.0f));
        synth.setFlipLR(entry.flipLR);
        synth.setFlipPhase(entry.flipPhase);
        synth.setMicBleedLevels(entry.micBleedLevels);
    };

    std::vector<juce::String> slotOrder;
    std::unordered_map<juce::String, std::vector<const InstrumentMapEntry*>> entriesBySlotId;
    for (const auto& entry : parsed.value.entries) {
        if (entriesBySlotId.find(entry.slotId) == entriesBySlotId.end())
            slotOrder.push_back(entry.slotId);
        entriesBySlotId[entry.slotId].push_back(&entry);
    }

    std::unordered_map<juce::String, juce::String> nextSlotFingerprints = buildSlotFingerprintMap(parsed.value);
    bool anyGroupHasMultipleEntries = false;
    for (const auto& slotId : slotOrder) {
        const auto& group = entriesBySlotId[slotId];
        if (group.size() > 1)
            anyGroupHasMultipleEntries = true;
    }

    const bool currentlyHasShadowSynths = static_cast<int>(this->instrumentSynthPtrs.size()) > this->primaryInstrumentSynthCount;
    const bool rebuildAllShadows = currentlyHasShadowSynths || anyGroupHasMultipleEntries;

    // Build index map for fast primary slot lookup (safe to read on message thread)
    std::unordered_map<juce::String, int> primarySlotIndex;
    for (int idx = 0; idx < this->primaryInstrumentSynthCount; ++idx)
        primarySlotIndex[this->instrumentSynthPtrs[idx]->getId()] = idx;

    juce::StringArray statusLines;
    for (const auto& warning : parsed.errors)
        statusLines.add(warning);

    int applied = 0;
    bool forceApplyAllSlots = false;

    // Wildcard handling: detect if a full kit reload is needed
    KitLib::DrumKitSpec* wildcardKitSpec = nullptr;
    bool wildcardChanged = false;
    bool hasUnloadedPrimarySynth = false;
    for (int idx = 0; idx < this->primaryInstrumentSynthCount; ++idx) {
        if (!this->instrumentSynthPtrs[static_cast<size_t>(idx)]->isLoaded()) {
            hasUnloadedPrimarySynth = true;
            break;
        }
    }

    // Per-slot preparation: replacement synths and shadow synths built outside the lock
    struct PreparedShadow {
        std::unique_ptr<InstrumentSynth> synth;
        std::vector<std::pair<int, InstrumentSynth*>> noteRoutes;
    };
    struct PreparedSlot {
        juce::String slotId;
        int primaryIndex = -1;
        std::unique_ptr<InstrumentSynth> primaryReplacement;
        std::vector<PreparedShadow> shadows;
    };
    std::vector<PreparedSlot> preparedSlots;

    for (const auto& slotId : slotOrder) {
        const auto& group = entriesBySlotId[slotId];
        if (group.empty())
            continue;

        if (slotId == "*") {
            const auto& wildcardEntry = *group.front();
            wildcardKitSpec = this->libraryManagerPtr->findKit(wildcardEntry.kitRef);
            if (wildcardKitSpec == nullptr) {
                statusLines.add("Kit not found for wildcard entry: " + wildcardEntry.kitRef);
                continue;
            }
            wildcardChanged = hasUnloadedPrimarySynth
                           || this->lastAppliedSlotFingerprints[slotId] != nextSlotFingerprints[slotId];
            if (wildcardChanged) {
                forceApplyAllSlots = true;
                ++applied;
            }
            continue;
        }

        auto slotIt = this->instrumentSynthById.find(slotId);
        if (slotIt == this->instrumentSynthById.end()) {
            statusLines.add("Instrument slot not found: " + slotId);
            continue;
        }
        auto* primarySlot = slotIt->second;
        const bool slotChanged = forceApplyAllSlots
                              || !primarySlot->isLoaded()
                              || (this->lastAppliedSlotFingerprints[slotId] != nextSlotFingerprints[slotId]);

        struct ResolvedEntry {
            const InstrumentMapEntry* entry = nullptr;
            KitLib::DrumKitSpec* kitSpec = nullptr;
            KitLib::InstrumentSpec* instSpec = nullptr;
            std::vector<juce::String> effectiveFilter;
        };

        std::vector<ResolvedEntry> resolvedEntries;
        for (const auto* entry : group) {
            auto* kitSpec = this->libraryManagerPtr->findKit(entry->kitRef);
            if (kitSpec == nullptr) {
                statusLines.add("Kit not found: " + entry->kitRef + " for slot '" + entry->slotId + "'");
                continue;
            }

            if (entry->instCode.isEmpty()) {
                statusLines.add("Instrument code is required for slot '" + entry->slotId + "'");
                continue;
            }

            auto* instSpec = kitSpec->findInstrument(entry->instCode);
            if (instSpec == nullptr) {
                statusLines.add("Instrument not found: " + kitSpec->meta.name + "/" + entry->instCode + " for slot '" + entry->slotId + "'");
                continue;
            }

            if (!entry->articulationFilter.empty()) {
                const auto availableSourceArts = collectInstrumentArticulationNames(*instSpec);
                juce::StringArray unknownSourceArts;
                juce::StringArray unknownSourceArtKeys;

                for (const auto& sourceArtName : entry->articulationFilter) {
                    const auto normalizedSourceArtName = normalizeArticulationName(sourceArtName);
                    if (normalizedSourceArtName.isEmpty())
                        continue;

                    if (stringArrayContainsIgnoreCase(availableSourceArts, normalizedSourceArtName))
                        continue;

                    if (stringArrayContainsIgnoreCase(unknownSourceArtKeys, normalizedSourceArtName))
                        continue;

                    auto sourceArtToken = normalizedSourceArtName;
                    const auto suggestedSourceArt = findClosestArticulationName(normalizedSourceArtName, availableSourceArts);
                    if (suggestedSourceArt.isNotEmpty())
                        sourceArtToken += " (did you mean '" + suggestedSourceArt + "'?)";
                    unknownSourceArtKeys.add(normalizedSourceArtName);
                    unknownSourceArts.add(sourceArtToken);
                }

                if (!unknownSourceArts.isEmpty()) {
                    const auto unknownSourceArtsText = unknownSourceArts.joinIntoString(", ");
                    const auto availableSourceArtsText = availableSourceArts.isEmpty()
                        ? juce::String("none")
                        : availableSourceArts.joinIntoString(", ");

                    statusLines.add("Unknown source articulation(s) [" + unknownSourceArtsText
                        + "] for target '" + kitSpec->meta.codeRaw + "/" + instSpec->meta.codeRaw
                        + "' in slot '" + entry->slotId + "'. Available source articulations: "
                        + availableSourceArtsText);
                }
            }

            auto filter = effectiveArticulationFilter(*entry);
            juce::String filterStr;
            for (const auto& fa : filter) filterStr += (filterStr.isEmpty() ? "" : ",") + fa;
            LOG(Notice, "  resolved: slot=" + entry->slotId + " -> " + kitSpec->meta.name + "/" + entry->instCode
                + (filterStr.isNotEmpty() ? " effectiveFilter=[" + filterStr + "]" : " effectiveFilter=[ALL]"));
            resolvedEntries.push_back({ entry, kitSpec, instSpec, std::move(filter) });
        }

        if (resolvedEntries.empty())
            continue;

        const auto& slotLayout = primarySlot->getInstrumentSlot();
        const auto& primary = resolvedEntries.front();
        InstrumentSynth::ArticulationSourceMap artSources;
        InstrumentSynth::ArticulationNameRemap primaryRemap = buildArtNameRemap(*primary.entry);
        std::vector<ShadowPlan> shadowPlans;
        std::vector<uint32_t> claimedArticulations;
        std::vector<uint32_t> primaryAllowedArticulationIds;
        InstrumentSynth::ArticulationAliasMap artAliases;

        if (primary.effectiveFilter.empty()) {
            for (const auto& layoutArt : slotLayout.articulations)
                addUniqueId(primaryAllowedArticulationIds, layoutArt.nameId);
        } else {
            for (const auto& artName : primary.effectiveFilter) {
                if (artName.isNotEmpty())
                    addUniqueId(primaryAllowedArticulationIds, StringsDictSingleton::getId(normalizeArticulationName(artName)));
            }
        }

        if (LOG_ENABLED(Notice)) {
            juce::String allowedStr;
            for (const auto& artId : primaryAllowedArticulationIds)
                allowedStr += (allowedStr.isEmpty() ? "" : ",") + StringsDictSingleton::nameOf(artId);
            LOG(Notice, "  slot=" + slotId + " primaryAllowed(initial)=[" + allowedStr + "] slotChanged=" + juce::String(slotChanged ? "yes" : "no"));
        }

        for (const auto& artName : primary.effectiveFilter) {
            if (artName.isNotEmpty())
                addUniqueId(claimedArticulations, StringsDictSingleton::getId(normalizeArticulationName(artName)));
        }

        for (size_t idx = 1; idx < resolvedEntries.size(); ++idx) {
            const auto& resolved = resolvedEntries[idx];
            if (resolved.effectiveFilter.empty()) {
                shadowPlans.push_back({ resolved.entry, resolved.instSpec, {}, buildArtNameRemap(*resolved.entry) });
                continue;
            }

            std::vector<juce::String> layeredArts;
            const bool hasParameterOverrides = resolved.entry->tuningCents.has_value()
                                            || resolved.entry->attackTimeMs.has_value()
                                            || resolved.entry->decayTimeMs.has_value()
                                            || resolved.entry->volumeDb.has_value()
                                            || resolved.entry->flipLR
                                            || resolved.entry->flipPhase
                                            || !resolved.entry->micBleedLevels.empty();
            for (const auto& artName : resolved.effectiveFilter) {
                if (artName.isEmpty())
                    continue;

                const auto artId = StringsDictSingleton::getId(normalizeArticulationName(artName));
                const bool alreadyClaimed = containsId(claimedArticulations, artId);
                if (hasParameterOverrides) {
                    layeredArts.push_back(artName);
                    if (!alreadyClaimed) {
                        addUniqueId(claimedArticulations, artId);
                        eraseId(primaryAllowedArticulationIds, artId);
                    }
                } else if (!alreadyClaimed) {
                    addUniqueId(claimedArticulations, artId);
                    artSources[artId] = resolved.instSpec;
                    addUniqueId(primaryAllowedArticulationIds, artId);
                    // Propagate cross-art remap for this articulation to the primary synth
                    auto entryRemap = buildArtNameRemap(*resolved.entry);
                    auto remapIt = entryRemap.find(artId);
                    if (remapIt != entryRemap.end())
                        primaryRemap[artId] = remapIt->second;
                } else {
                    layeredArts.push_back(artName);
                }
            }

            if (!layeredArts.empty())
                shadowPlans.push_back({ resolved.entry, resolved.instSpec, layeredArts, buildArtNameRemap(*resolved.entry) });
        }

        uint32_t fallbackLayoutArtId = 0;
        uint32_t fallbackSourceArtId = 0;
        if (slotLayout.controllers.empty()) {
            for (const auto& primaryArtName : primary.effectiveFilter) {
                if (primaryArtName.isEmpty())
                    continue;
                fallbackLayoutArtId = StringsDictSingleton::getId(normalizeArticulationName(primaryArtName));
                fallbackSourceArtId = resolveEntrySourceArtId(*primary.entry, primaryArtName);
                break;
            }
        }

        // Simple non-controller slots inherit empty articulations from the first mapped art.
        // Controller-driven slots keep their existing primary fallback so CC tables remain intact.
        for (const auto& layoutArt : slotLayout.articulations) {
            if (containsId(claimedArticulations, layoutArt.nameId))
                continue;
            if (fallbackSourceArtId != 0)
                artAliases[layoutArt.nameId] = fallbackSourceArtId;
            else
                addUniqueId(primaryAllowedArticulationIds, layoutArt.nameId);
        }

        if (LOG_ENABLED(Notice)) {
            juce::String allowedStr;
            for (const auto& artId : primaryAllowedArticulationIds)
                allowedStr += (allowedStr.isEmpty() ? "" : ",") + StringsDictSingleton::nameOf(artId);
            LOG(Notice, "  slot=" + slotId + " primaryAllowed(final)=[" + allowedStr + "]");
            if (!primaryRemap.empty()) {
                juce::String remapStr;
                for (const auto& [fromId, toId] : primaryRemap)
                    remapStr += (remapStr.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(fromId) + "->" + StringsDictSingleton::nameOf(toId);
                LOG(Notice, "  primaryRemap: [" + remapStr + "]");
            }
            if (!artSources.empty()) {
                juce::String srcStr;
                for (const auto& [artId, instPtr] : artSources)
                    srcStr += (srcStr.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(artId) + "->" + instPtr->meta.codeRaw;
                LOG(Notice, "  artSources: [" + srcStr + "]");
            }
        }

        PreparedSlot prep;
        prep.slotId = slotId;
        auto pIdxIt = primarySlotIndex.find(slotId);
        prep.primaryIndex = (pIdxIt != primarySlotIndex.end()) ? pIdxIt->second : -1;

        // Create a replacement synth for the primary slot (expensive work done outside lock)
        if (slotChanged && prep.primaryIndex >= 0) {
            auto replacement = std::make_unique<InstrumentSynth>(this, slotLayout);
            replacement->setMicOutputMapping(primarySlot->getMicOutputMapping());
            replacement->setMicChannelCount(primarySlot->getMicChannelCount());
            replacement->loadInstrumentWithSources(primary.instSpec, artSources, primaryAllowedArticulationIds, primaryRemap, artAliases);
            applyOverrides(*replacement, *primary.entry);
            LOG(Notice, "  PRIMARY loaded: slot=" + slotId + " inst=" + primary.instSpec->meta.codeRaw
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
        if (rebuildAllShadows) {
            for (const auto& shadowPlan : shadowPlans) {
                auto shadow = std::make_unique<InstrumentSynth>(this, slotLayout);
                auto* shadowRaw = shadow.get();
                const auto shadowArtIds = toArticulationIdList(shadowPlan.articulations);
                shadowRaw->setMicOutputMapping(primarySlot->getMicOutputMapping());
                shadowRaw->setMicChannelCount(primarySlot->getMicChannelCount());
                shadowRaw->loadInstrumentWithSources(shadowPlan.instSpec, {}, shadowArtIds, shadowPlan.artNameRemap, artAliases);
                applyOverrides(*shadowRaw, *shadowPlan.entry);
                if (LOG_ENABLED(Notice)) {
                    juce::String shadowArtsStr;
                    for (const auto& sa : shadowPlan.articulations) shadowArtsStr += (shadowArtsStr.isEmpty() ? "" : ",") + sa;
                    juce::String remapStr;
                    for (const auto& [fromId, toId] : shadowPlan.artNameRemap) remapStr += (remapStr.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(fromId) + "->" + StringsDictSingleton::nameOf(toId);
                    LOG(Notice, "  SHADOW loaded: slot=" + slotId + " inst=" + shadowPlan.instSpec->meta.codeRaw
                        + " arts=[" + shadowArtsStr + "]"
                        + (remapStr.isNotEmpty() ? " remap=[" + remapStr + "]" : "")
                        + " tuning=" + juce::String(shadowPlan.entry->tuningCents.value_or(0)));
                }

                if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
                    shadowRaw->setCurrentPlaybackSampleRate(this->lastSampleRate);
                    shadowRaw->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
                }

                PreparedShadow ps;
                for (const auto& art : slotLayout.articulations) {
                    if (!shadowArtIds.empty()) {
                        const auto aliasIt = artAliases.find(art.nameId);
                        const bool routesExplicitArt = containsId(shadowArtIds, art.nameId);
                        const bool routesAliasedArt = aliasIt != artAliases.end() && fallbackLayoutArtId != 0 && containsId(shadowArtIds, fallbackLayoutArtId);
                        if (!routesExplicitArt && !routesAliasedArt)
                            continue;
                    }
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

        // Handle wildcard: full kit reload (rare, lock hold acceptable)
        if (wildcardChanged && wildcardKitSpec != nullptr)
            this->mapKit(*wildcardKitSpec);

        // Remove old shadow synths
        if (rebuildAllShadows) {
            for (int idx = static_cast<int>(this->instrumentSynthPtrs.size()) - 1;
                 idx >= this->primaryInstrumentSynthCount; --idx) {
                oldSynths.push_back(std::move(this->instrumentSynthPtrs[idx]));
            }
            this->instrumentSynthPtrs.erase(
                this->instrumentSynthPtrs.begin() + this->primaryInstrumentSynthCount,
                this->instrumentSynthPtrs.end());
        }

        // Swap in prepared primary replacements (pointer swap only)
        bool anyPrimarySwapped = false;
        for (auto& prep : preparedSlots) {
            if (prep.primaryReplacement && prep.primaryIndex >= 0
                && prep.primaryIndex < static_cast<int>(this->instrumentSynthPtrs.size())) {
                oldSynths.push_back(std::move(this->instrumentSynthPtrs[prep.primaryIndex]));
                this->instrumentSynthPtrs[prep.primaryIndex] = std::move(prep.primaryReplacement);
                this->instrumentSynthById[prep.slotId] = this->instrumentSynthPtrs[prep.primaryIndex].get();
                anyPrimarySwapped = true;
            }
        }

        // Rebuild primary note routing if any primary was replaced or shadows were removed
        if (rebuildAllShadows || anyPrimarySwapped)
            this->rebuildPrimaryInstrumentNoteRouting();

        // Add prepared shadow synths and their note routes
        for (auto& prep : preparedSlots) {
            for (auto& ps : prep.shadows) {
                for (const auto& [note, synthPtr] : ps.noteRoutes)
                    this->instrumentSynthsByNote[note].push_back(synthPtr);
                this->instrumentSynthPtrs.push_back(std::move(ps.synth));
            }
        }

        this->lastAppliedSlotFingerprints = std::move(nextSlotFingerprints);
        this->lastAppliedInstrumentMapText = text;
    }
    // Old synths destroyed here outside the lock - audio thread cannot access them

    if (this->sampleLoaderPtr != nullptr)
        this->sampleLoaderPtr->getQueue().startProcessing();

    if (applied <= 0 && statusLines.isEmpty())
        statusLines.add("Instrument map unchanged - no reload needed");

    if (statusLines.isEmpty())
        statusLines.add("Instrument map applied successfully");

    this->lastInstrumentMapStatus = statusLines.joinIntoString("\n");
    LOG(Notice, "=== useInstrumentMap done: applied=" + juce::String(applied) + " oldSynths=" + juce::String(oldSynths.size())
        + " totalSynths=" + juce::String(this->instrumentSynthPtrs.size()) + " ===");
    return applied > 0;
}

ParseResult<DrumKitEngine::InstrumentMap> DrumKitEngine::parseInstrumentMapText(const juce::String& text) {
    return DrumKitEngineMappingModel::parseInstrumentMapText(text);
}
