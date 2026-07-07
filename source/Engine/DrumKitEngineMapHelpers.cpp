#include "DrumKitEngineMapHelpers.h"

#include "ArticulationNameHelpers.h"
#include "InstrumentSynth.h"

#include <algorithm>

namespace DrumKitEngineMapHelpers {
namespace Detail {

int balanceSide(const std::optional<int>& balance) {
    if (!balance.has_value() || *balance == 0) return 0;
    return *balance < 0 ? -1 : 1;
}

const KitLayout::InstrumentSlot* findLayoutSlot(const KitLayout& layout, const juce::String& slotId) {
    for (const auto& slot : layout.instrumentSlots)
        if (slot.id == slotId) return &slot;
    return nullptr;
}

std::vector<const KitLib::ArticulationSpec*> collectOrientationArticulations(
    const KitLib::InstrumentSpec& instrumentSpec,
    const DrumKitEngine::InstrumentMapEntry& entry) {
    std::vector<const KitLib::ArticulationSpec*> result;

    if (entry.articulationFilter.empty()) {
        result.reserve(instrumentSpec.articulationSpecs.size());
        for (const auto& artSpec : instrumentSpec.articulationSpecs)
            if (artSpec != nullptr) result.push_back(artSpec.get());
        return result;
    }
    for (const auto& artName : entry.articulationFilter) {
        const auto normalizedArtName = ArticulationNameHelpers::normalizeArticulationName(artName);
        if (normalizedArtName.isEmpty()) continue;

        const KitLib::ArticulationSpec* matched = nullptr;
        for (const auto& artSpec : instrumentSpec.articulationSpecs) {
            if (artSpec == nullptr) continue;

            if (instrumentSpec.findArticulationById(artName) == artSpec.get() || ArticulationNameHelpers::normalizeArticulationName(artSpec->codeRaw) == normalizedArtName) {
                matched = artSpec.get();
                break;
            }
        }
        if (matched == nullptr) return {};
        result.push_back(matched);
    }
    return result;
}

std::optional<bool> inferAutomaticFlipLR(
    const KitLayout::InstrumentSlot& slot,
    const KitLib::InstrumentSpec& instrumentSpec,
    const DrumKitEngine::InstrumentMapEntry& entry) {
    const int slotSide = balanceSide(slot.balance);
    if (slotSide == 0) return std::nullopt;

    const auto articulations = collectOrientationArticulations(instrumentSpec, entry);
    if (articulations.empty()) return std::nullopt;

    std::optional<int> sourceSide;
    for (const auto* artSpec : articulations) {
        if (artSpec == nullptr || artSpec->balanceConflict) return std::nullopt;

        const int artSide = balanceSide(artSpec->getEffectiveBalance());
        if (artSide == 0) return std::nullopt;

        if (!sourceSide.has_value()) sourceSide = artSide;
        else if (*sourceSide != artSide) return std::nullopt;
    }
    if (!sourceSide.has_value()) return std::nullopt;

    return *sourceSide != slotSide;
}

void copySynthOverrides(InstrumentSynth& target, const InstrumentSynth& source) {
    target.setTuningOffsetCents(source.getTuningOffsetCents());

    const auto attackOverride = source.getAttackOverrideMs();
    if (attackOverride.has_value()) target.setAttackOverrideMs(*attackOverride);

    const auto decayOverride = source.getDecayOverrideMs();
    if (decayOverride.has_value()) target.setDecayOverrideMs(*decayOverride);

    target.setVolumeDb(source.getVolumeDb());
    target.setPan(source.getPan());
    target.setFlipLR(source.getFlipLR());
    target.setFlipPhase(source.getFlipPhase());
    target.setMicBleedLevels(source.getMicBleedLevels());
}

void prepareSynthIfNeeded(InstrumentSynth& synth, double sampleRate, int samplesPerBlock) {
    if (sampleRate <= 0.0 || samplesPerBlock <= 0) return;
    synth.setCurrentPlaybackSampleRate(sampleRate);
    synth.prepareToPlay(sampleRate, samplesPerBlock);
}

juce::String joinValues(const std::vector<juce::String>& values) {
    juce::String out;
    for (size_t idx = 0; idx < values.size(); ++idx) {
        if (idx > 0) out += ",";
        out += values[idx].trim().toLowerCase();
    }
    return out;
}

juce::String joinBleedValues(const std::unordered_map<juce::String, float>& values) {
    std::vector<juce::String> keys;
    keys.reserve(values.size());
    for (const auto& [key, value] : values) {
        juce::ignoreUnused(value);
        keys.push_back(key);
    }

    std::sort(keys.begin(), keys.end(), [](const juce::String& left, const juce::String& right) {
        return left.compareIgnoreCase(right) < 0;
    });

    juce::String out;
    for (size_t idx = 0; idx < keys.size(); ++idx) {
        if (idx > 0) out += ",";
        const auto& key = keys[idx];
        out += key.trim().toLowerCase() + ":" + juce::String(values.at(key), 4);
    }
    return out;
}

} // namespace Detail

std::optional<bool> inferAutomaticFlipLR(const KitLayout::InstrumentSlot& slot, const KitLib::InstrumentSpec& instrumentSpec, const DrumKitEngine::InstrumentMapEntry& entry) {
    return Detail::inferAutomaticFlipLR(slot, instrumentSpec, entry);
}

void prepareSynthIfNeeded(InstrumentSynth& synth, double sampleRate, int samplesPerBlock) {
    Detail::prepareSynthIfNeeded(synth, sampleRate, samplesPerBlock);
}

void appendUniqueId(std::vector<uint32_t>& values, uint32_t value) {
    if (!vectorContains(values, value)) values.push_back(value);
}

std::vector<juce::String> effectiveArticulationFilter(const DrumKitEngine::InstrumentMapEntry& entry) {
    if (!entry.slotArticulationFilter.empty()) return entry.slotArticulationFilter;
    return entry.articulationFilter;
}

uint32_t resolveEntrySourceArtId(const DrumKitEngine::InstrumentMapEntry& entry, const juce::String& slotArtName) {
    if (slotArtName.isEmpty()) return 0;

    const auto normalizedSlotArt = ArticulationNameHelpers::normalizeArticulationName(slotArtName);
    for (size_t idx = 0; idx < entry.slotArticulationFilter.size(); ++idx) {
        if (ArticulationNameHelpers::normalizeArticulationName(entry.slotArticulationFilter[idx]) != normalizedSlotArt) continue;

        if (idx < entry.articulationFilter.size() && entry.articulationFilter[idx].isNotEmpty())
            return StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(entry.articulationFilter[idx]));
        return StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(slotArtName));
    }
    if (!entry.articulationFilter.empty()) {
        for (const auto& sourceArtName : entry.articulationFilter) {
            if (sourceArtName.isNotEmpty())
                return StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(sourceArtName));
        }
    }
    return StringsDictSingleton::getId(ArticulationNameHelpers::normalizeArticulationName(slotArtName));
}

juce::StringArray collectInstrumentArticulationNames(const KitLib::InstrumentSpec& instrumentSpec) {
    juce::StringArray availableNames;
    for (const auto& articulationSpec : instrumentSpec.articulationSpecs) {
        if (articulationSpec == nullptr) continue;

        const auto articulationName = ArticulationNameHelpers::baseArticulationName(articulationSpec->codeRaw);
        if (articulationName.isEmpty()) continue;

        if (!ArticulationNameHelpers::stringArrayContainsIgnoreCase(availableNames, articulationName))
            availableNames.add(articulationName);
    }
    availableNames.sort(true);
    return availableNames;
}

void applyAutomaticOrientation(DrumKitEngine::InstrumentMap& instrumentMap, const KitLayout& layout, KitLib::LibraryManager& libraryManager) {
    for (auto& entry : instrumentMap.entries) {
        if (entry.flipLRExplicit) continue;

        const auto* slot = Detail::findLayoutSlot(layout, entry.slotId);
        if (slot == nullptr) continue;

        auto* kitSpec = libraryManager.findKitById(entry.kitRef);
        if (kitSpec == nullptr || entry.instCode.isEmpty()) continue;

        auto* instSpec = kitSpec->findInstrumentById(entry.instCode);
        if (instSpec == nullptr) continue;

        const auto inferredFlip = Detail::inferAutomaticFlipLR(*slot, *instSpec, entry);
        if (inferredFlip.has_value()) entry.flipLR = *inferredFlip;
    }
}

juce::String makeInstrumentMapEntryFingerprint(const DrumKitEngine::InstrumentMapEntry& entry) {
    juce::String fingerprint;
    fingerprint += entry.slotId.trim().toLowerCase() + "|";
    fingerprint += entry.kitRef.trim().toLowerCase() + "|";
    fingerprint += entry.instCode.trim().toLowerCase() + "|";
    fingerprint += Detail::joinValues(entry.slotArticulationFilter) + "|";
    fingerprint += Detail::joinValues(entry.articulationFilter) + "|";
    fingerprint += entry.tuningCents.has_value() ? juce::String(*entry.tuningCents) : "_";
    fingerprint += "|";
    fingerprint += entry.attackTimeMs.has_value() ? juce::String(*entry.attackTimeMs) : "_";
    fingerprint += "|";
    fingerprint += entry.decayTimeMs.has_value() ? juce::String(*entry.decayTimeMs) : "_";
    fingerprint += "|";
    fingerprint += entry.volumeDb.has_value() ? juce::String(*entry.volumeDb, 4) : "_";
    fingerprint += "|";
    fingerprint += entry.pan.has_value() ? juce::String(*entry.pan) : "_";
    fingerprint += "|";
    fingerprint += entry.flipLR ? "1" : "0";
    fingerprint += entry.flipLRExplicit ? "e" : "a";
    fingerprint += "|";
    fingerprint += entry.flipPhase ? "1" : "0";
    fingerprint += "|";
    fingerprint += Detail::joinBleedValues(entry.micBleedLevels);
    
    return fingerprint;
}

juce::String makeInstrumentMapEntryReloadFingerprint(const DrumKitEngine::InstrumentMapEntry& entry) {
    juce::String fingerprint;
    fingerprint += entry.slotId.trim().toLowerCase() + "|";
    fingerprint += entry.kitRef.trim().toLowerCase() + "|";
    fingerprint += entry.instCode.trim().toLowerCase() + "|";
    fingerprint += Detail::joinValues(entry.slotArticulationFilter) + "|";
    fingerprint += Detail::joinValues(entry.articulationFilter);
    return fingerprint;
}

std::unordered_map<juce::String, juce::String> buildSlotFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap) {
    std::unordered_map<juce::String, juce::String> result;
    for (const auto& entry : instrumentMap.entries)
        result[entry.slotId] += makeInstrumentMapEntryFingerprint(entry) + "\n";
    return result;
}

std::unordered_map<juce::String, juce::String> buildResolvedSlotFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap, const KitLib::LibraryManager& libraryManager) {
    auto result = buildSlotFingerprintMap(instrumentMap);
    for (const auto& entry : instrumentMap.entries) {
        const auto* kitSpec = libraryManager.findKitById(entry.kitRef);
        if (kitSpec == nullptr || entry.instCode.isEmpty()) continue;

        const auto* instSpec = kitSpec->findInstrumentById(entry.instCode);
        if (instSpec == nullptr) continue;

        result[entry.slotId] += "source\n" + instSpec->getContentFingerprint() + "\n";
    }
    return result;
}

std::unordered_map<juce::String, juce::String> buildResolvedSlotReloadFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap, const KitLib::LibraryManager& libraryManager) {
    std::unordered_map<juce::String, juce::String> result;
    for (const auto& entry : instrumentMap.entries) {
        result[entry.slotId] += makeInstrumentMapEntryReloadFingerprint(entry) + "\n";

        const auto* kitSpec = libraryManager.findKitById(entry.kitRef);
        if (kitSpec == nullptr || entry.instCode.isEmpty()) continue;

        const auto* instSpec = kitSpec->findInstrumentById(entry.instCode);
        if (instSpec == nullptr) continue;

        result[entry.slotId] += "source\n" + instSpec->getContentFingerprint() + "\n";
    }
    return result;
}

bool canReuseSlotWithoutReload(const KitLayout::InstrumentSlot& oldSlot, const KitLayout::InstrumentSlot& newSlot) {
    if (oldSlot.type != newSlot.type) return false;

    if (oldSlot.articulations.size() != newSlot.articulations.size()) return false;

    for (size_t idx = 0; idx < oldSlot.articulations.size(); ++idx) {
        const auto& oldArt = oldSlot.articulations[idx];
        const auto& newArt = newSlot.articulations[idx];
        if (oldArt.nameId != newArt.nameId || oldArt.exprId != newArt.exprId) return false;
    }
    return true;
}

std::unique_ptr<InstrumentSynth> createReplacementSynth(
    DrumKitEngine* owner,
    const KitLayout::InstrumentSlot& layoutSlot,
    const InstrumentSynth& source,
    double sampleRate,
    int samplesPerBlock) {
    auto replacement = std::make_unique<InstrumentSynth>(owner, layoutSlot);
    replacement->setMicOutputMapping(source.getMicOutputMapping());
    replacement->setMicChannelCount(source.getMicChannelCount());

    const auto librarySnapshot = source.getLibrarySnapshot();
    const auto& routingContext = source.getLastRoutingContext();
    if (routingContext.isValid()) {
        replacement->loadInstrumentWithSources(routingContext.defaultInstrument, routingContext.artSources, routingContext.allowedArticulationNameIds, routingContext.artNameRemap, routingContext.artAliases, librarySnapshot);
    } else if (auto* loadedInstrument = source.getLoadedInstrumentSpec()) {
        replacement->loadInstrument(const_cast<KitLib::InstrumentSpec*>(loadedInstrument), librarySnapshot);
    }

    Detail::copySynthOverrides(*replacement, source);
    Detail::prepareSynthIfNeeded(*replacement, sampleRate, samplesPerBlock);
    return replacement;
}

juce::String getSlotArticulationName(const KitLayout::InstrumentSlot::ArticulationTrigger& art) {
    auto artName = StringsDictSingleton::nameOf(art.nameId);
    if (artName.isEmpty()) artName = art.codeRaw;

    return artName;
}

KitLib::InstrumentSpec* findMatchingKitInstrument(
    const KitLayout::InstrumentSlot& layoutInstrument,
    const KitLib::DrumKitSpec& kit,
    std::unordered_set<KitLib::InstrumentSpec*>& usedInstruments,
    std::map<KitLib::InstrumentSpec::Type, int>& nextIndexByType) {
    KitLib::InstrumentSpec* matched = nullptr;

    if (layoutInstrument.type != KitLib::InstrumentSpec::Type::Unknown) {
        auto candidates = kit.getInstrumentsByType(layoutInstrument.type);
        int requestedIdx = -1;
        int pos = static_cast<int>(layoutInstrument.id.length()) - 1;
        while (pos >= 0 && juce::CharacterFunctions::isDigit(layoutInstrument.id[pos]))
            --pos;

        const auto digits = layoutInstrument.id.substring(pos + 1);
        if (digits.isNotEmpty()) requestedIdx = digits.getIntValue() - 1;

        if (requestedIdx >= 0
            && requestedIdx < static_cast<int>(candidates.size())
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
                || candidate->meta.name.trim().toLowerCase().contains(needle)
            ) {
                matched = candidate;
                break;
            }
        }
    }
    if (matched != nullptr) usedInstruments.insert(matched);

    return matched;
}

} // namespace DrumKitEngineMapHelpers
