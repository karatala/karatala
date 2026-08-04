#include "InstrumentMapOperations.h"

#include "../ArticulationNames.h"

#include <algorithm>
#include <iterator>

namespace InstrumentMap {

static int balanceSide(const std::optional<int>& balance) {
    if (!balance.has_value() || *balance == 0) return 0;
    return *balance < 0 ? -1 : 1;
}

static const KitLayout::InstrumentSlot* findLayoutSlot(const KitLayout& layout, const juce::String& slotId) {
    for (const auto& slot : layout.instrumentSlots)
        if (slot.id == slotId) return &slot;
    return nullptr;
}

static std::vector<const KitLib::ArticulationSpec*> collectLibraryArticulationsForOrientation(const KitLib::InstrumentSpec& libraryInstrument, const InstrumentMap::Layer& mapLayer) {
    std::vector<const KitLib::ArticulationSpec*> result;

    if (mapLayer.articulationFilter.empty()) {
        result.reserve(libraryInstrument.articulationSpecs.size());
        for (const auto& artSpec : libraryInstrument.articulationSpecs)
            if (artSpec != nullptr) result.push_back(artSpec.get());
        return result;
    }
    for (const auto& artName : mapLayer.articulationFilter) {
        const auto normalizedArtName = ArticulationNames::normalizeArticulationName(artName);
        if (normalizedArtName.isEmpty()) continue;

        const KitLib::ArticulationSpec* matched = nullptr;
        for (const auto& artSpec : libraryInstrument.articulationSpecs) {
            if (artSpec == nullptr) continue;

            if (libraryInstrument.findArticulationById(artName) == artSpec.get() || ArticulationNames::normalizeArticulationName(artSpec->codeRaw) == normalizedArtName) {
                matched = artSpec.get();
                break;
            }
        }
        if (matched == nullptr) return {};
        result.push_back(matched);
    }
    return result;
}

static std::optional<bool> inferAutomaticFlipLR(const KitLayout::InstrumentSlot& slot, const KitLib::InstrumentSpec& libraryInstrument, const InstrumentMap::Layer& mapLayer) {
    const int slotSide = balanceSide(slot.balance);
    if (slotSide == 0) return std::nullopt;

    const auto articulations = collectLibraryArticulationsForOrientation(libraryInstrument, mapLayer);
    if (articulations.empty()) return std::nullopt;

    std::optional<int> placementSide;
    for (const auto* artSpec : articulations) {
        if (artSpec == nullptr || (artSpec->balanceConflict && !artSpec->balanceOverride.has_value())) return std::nullopt;

        const int artSide = balanceSide(artSpec->getEffectiveBalance());
        if (artSide == 0) return std::nullopt;

        if (!placementSide.has_value()) placementSide = artSide;
        else if (*placementSide != artSide) return std::nullopt;
    }
    if (!placementSide.has_value()) return std::nullopt;

    return *placementSide != slotSide;
}

static juce::String serializeStringList(const std::vector<juce::String>& values) {
    juce::String out;
    for (size_t idx = 0; idx < values.size(); ++idx) {
        if (idx > 0) out += ",";
        out += values[idx].trim().toLowerCase();
    }
    return out;
}

static juce::String serializeBleedLevels(const std::unordered_map<juce::String, float>& values) {
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

static juce::String serializeRouteOverrides(const std::unordered_map<juce::String, juce::String>& values) {
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
        out += key.trim().toLowerCase() + ":" + values.at(key).trim().toLowerCase();
    }
    return out;
}

static juce::String makeLayerReloadFingerprintInternal(const InstrumentMap::Layer& mapLayer, bool includeParameterLayerShape) {
    const bool changesPlayerShape = includeParameterLayerShape && (mapLayer.tuningCents.has_value() || mapLayer.attackTimeMs.has_value() || mapLayer.decayTimeMs.has_value() || mapLayer.volumeDb.has_value() || mapLayer.pan.has_value() || mapLayer.velocityResponse.has_value() || mapLayer.flipLR || mapLayer.flipPhase || !mapLayer.micBleedLevels.empty());
    juce::String fingerprint;
    fingerprint += mapLayer.slotId.trim().toLowerCase() + "|";
    fingerprint += mapLayer.kitRef.trim().toLowerCase() + "|";
    fingerprint += mapLayer.instCode.trim().toLowerCase() + "|";
    fingerprint += serializeStringList(mapLayer.slotArticulationFilter) + "|";
    fingerprint += serializeStringList(mapLayer.articulationFilter) + "|";
    fingerprint += changesPlayerShape ? "layered|" : "shared|";
    fingerprint += serializeRouteOverrides(mapLayer.micRouteOverrides);
    return fingerprint;
}

static const KitLayout::InstrumentSlot::ArticulationSlot* findCompatibleArticulationSlotByCode(const KitLayout::InstrumentSlot& slot, const KitLib::ArticulationSpec& libraryArticulation) {
    for (const auto& articulationSlot : slot.articulations)
        if (articulationSlot.nameId == libraryArticulation.nameId && articulationSlot.exprId == libraryArticulation.exprId) return &articulationSlot;
    return nullptr;
}

static const KitLayout::InstrumentSlot::ArticulationSlot* findFallbackArticulationSlot(const KitLayout::InstrumentSlot& slot, const std::vector<juce::String>& preferences) {
    for (const auto& preference : preferences)
        for (const auto& articulationSlot : slot.articulations)
            if (ArticulationNames::mapFilterMatchesArticulation(preference, articulationSlot.codeRaw)) return &articulationSlot;
    return nullptr;
}

void applyAutomaticOrientation(Map& instrumentMap, const KitLayout& layout, KitLib::LibraryManager& libraryManager) {
    for (auto& mapLayer : instrumentMap.layers) {
        if (mapLayer.flipLRExplicit) continue;

        const auto* slot = findLayoutSlot(layout, mapLayer.slotId);
        if (slot == nullptr) continue;

        auto* libraryKit = libraryManager.findKitById(mapLayer.kitRef);
        if (libraryKit == nullptr || mapLayer.instCode.isEmpty()) continue;

        auto* libraryInstrument = libraryKit->findInstrumentById(mapLayer.instCode);
        if (libraryInstrument == nullptr) continue;

        const auto inferredFlip = inferAutomaticFlipLR(*slot, *libraryInstrument, mapLayer);
        if (inferredFlip.has_value()) mapLayer.flipLR = *inferredFlip;
    }
}

juce::String makeLayerFingerprint(const InstrumentMap::Layer& mapLayer) {
    juce::String fingerprint;
    fingerprint += mapLayer.slotId.trim().toLowerCase() + "|";
    fingerprint += mapLayer.kitRef.trim().toLowerCase() + "|";
    fingerprint += mapLayer.instCode.trim().toLowerCase() + "|";
    fingerprint += serializeStringList(mapLayer.slotArticulationFilter) + "|";
    fingerprint += serializeStringList(mapLayer.articulationFilter) + "|";
    fingerprint += mapLayer.tuningCents.has_value() ? juce::String(*mapLayer.tuningCents) : "_";
    fingerprint += "|";
    fingerprint += mapLayer.attackTimeMs.has_value() ? juce::String(*mapLayer.attackTimeMs) : "_";
    fingerprint += "|";
    fingerprint += mapLayer.decayTimeMs.has_value() ? juce::String(*mapLayer.decayTimeMs) : "_";
    fingerprint += "|";
    fingerprint += mapLayer.volumeDb.has_value() ? juce::String(*mapLayer.volumeDb, 4) : "_";
    fingerprint += "|";
    fingerprint += mapLayer.pan.has_value() ? juce::String(*mapLayer.pan) : "_";
    fingerprint += "|";
    fingerprint += mapLayer.velocityResponse.has_value() ? *mapLayer.velocityResponse : "_";
    fingerprint += "|";
    fingerprint += mapLayer.flipLR ? "1" : "0";
    fingerprint += mapLayer.flipLRExplicit ? "e" : "a";
    fingerprint += "|";
    fingerprint += mapLayer.flipPhase ? "1" : "0";
    fingerprint += "|";
    fingerprint += serializeBleedLevels(mapLayer.micBleedLevels);
    fingerprint += "|";
    fingerprint += serializeRouteOverrides(mapLayer.micRouteOverrides);

    return fingerprint;
}

juce::String makeLayerReloadFingerprint(const InstrumentMap::Layer& mapLayer) {
    return makeLayerReloadFingerprintInternal(mapLayer, true);
}

static std::unordered_map<juce::String, juce::String> buildSlotFingerprintMap(const InstrumentMap::Map& instrumentMap) {
    std::unordered_map<juce::String, juce::String> result;
    for (const auto& mapLayer : instrumentMap.layers)
        result[mapLayer.slotId] += makeLayerFingerprint(mapLayer) + "\n";
    return result;
}

std::unordered_map<juce::String, juce::String> buildResolvedSlotFingerprintMap(const InstrumentMap::Map& instrumentMap, const KitLib::LibraryManager& libraryManager) {
    auto result = buildSlotFingerprintMap(instrumentMap);
    for (const auto& mapLayer : instrumentMap.layers) {
        const auto* libraryKit = libraryManager.findKitById(mapLayer.kitRef);
        if (libraryKit == nullptr || mapLayer.instCode.isEmpty()) continue;

        const auto* libraryInstrument = libraryKit->findInstrumentById(mapLayer.instCode);
        if (libraryInstrument == nullptr) continue;

        result[mapLayer.slotId] += "library\n" + libraryInstrument->getContentFingerprint() + "\n";
    }
    return result;
}

std::unordered_map<juce::String, juce::String> buildResolvedSlotReloadFingerprintMap(const InstrumentMap::Map& instrumentMap, const KitLib::LibraryManager& libraryManager) {
    std::unordered_map<juce::String, juce::String> result;
    std::unordered_set<juce::String> seenSlotIds;
    for (const auto& mapLayer : instrumentMap.layers) {
        const bool isPrimaryMapLayer = seenSlotIds.insert(mapLayer.slotId).second;
        result[mapLayer.slotId] += makeLayerReloadFingerprintInternal(mapLayer, !isPrimaryMapLayer) + "\n";

        const auto* libraryKit = libraryManager.findKitById(mapLayer.kitRef);
        if (libraryKit == nullptr || mapLayer.instCode.isEmpty()) continue;

        const auto* libraryInstrument = libraryKit->findInstrumentById(mapLayer.instCode);
        if (libraryInstrument == nullptr) continue;

        result[mapLayer.slotId] += "library\n" + libraryInstrument->getContentFingerprint() + "\n";
    }
    return result;
}

KitLib::InstrumentSpec* findMatchingLibraryInstrument(
    const KitLayout::InstrumentSlot& layoutInstrument,
    const KitLib::DrumKitSpec& libraryKit,
    std::unordered_set<KitLib::InstrumentSpec*>& usedLibraryInstruments,
    std::map<KitLib::InstrumentSpec::Type, int>& nextIndexByType) {
    KitLib::InstrumentSpec* matched = nullptr;

    if (layoutInstrument.type != KitLib::InstrumentSpec::Type::Unknown) {
        auto candidates = libraryKit.getInstrumentsByType(layoutInstrument.type);
        int requestedIdx = -1;
        int pos = static_cast<int>(layoutInstrument.id.length()) - 1;
        while (pos >= 0 && juce::CharacterFunctions::isDigit(layoutInstrument.id[pos]))
            --pos;

        const auto digits = layoutInstrument.id.substring(pos + 1);
        if (digits.isNotEmpty()) requestedIdx = digits.getIntValue() - 1;

        if (requestedIdx >= 0 && requestedIdx < static_cast<int>(candidates.size()) && usedLibraryInstruments.find(candidates[static_cast<size_t>(requestedIdx)]) == usedLibraryInstruments.end()) {
            matched = candidates[static_cast<size_t>(requestedIdx)];
        }
        if (matched == nullptr) {
            int& next = nextIndexByType[layoutInstrument.type];
            for (int idx = next; idx < static_cast<int>(candidates.size()); ++idx) {
                if (usedLibraryInstruments.find(candidates[static_cast<size_t>(idx)]) == usedLibraryInstruments.end()) {
                    matched = candidates[static_cast<size_t>(idx)];
                    next = idx + 1;
                    break;
                }
            }
        }
        if (matched == nullptr) {
            for (int idx = 0; idx < static_cast<int>(candidates.size()); ++idx) {
                if (usedLibraryInstruments.find(candidates[static_cast<size_t>(idx)]) == usedLibraryInstruments.end()) {
                    matched = candidates[static_cast<size_t>(idx)];
                    nextIndexByType[layoutInstrument.type] = idx + 1;
                    break;
                }
            }
        }
    }

    if (matched == nullptr) {
        const auto needle = layoutInstrument.id.trim().toLowerCase();
        for (const auto& [id, instrumentPtr] : libraryKit.getInstrumentSpecsMap()) {
            juce::ignoreUnused(id);
            auto* candidate = instrumentPtr.get();
            if (candidate == nullptr || usedLibraryInstruments.find(candidate) != usedLibraryInstruments.end()) continue;

            if (candidate->meta.codeRaw.trim().toLowerCase().contains(needle) || candidate->meta.name.trim().toLowerCase().contains(needle)) {
                matched = candidate;
                break;
            }
        }
    }
    if (matched != nullptr) usedLibraryInstruments.insert(matched);

    return matched;
}

ParseResult<std::vector<InstrumentMap::Layer>> buildLayers(const KitLayout::InstrumentSlot& slot, const KitLib::DrumKitSpec& libraryKit, const KitLib::InstrumentSpec& libraryInstrument, const juce::String& preferredLibraryKitId, const ArticulationSlotFallbackRules& fallbackRules) {
    ParseResult<std::vector<InstrumentMap::Layer>> result;
    result.value.reserve(libraryInstrument.articulationSpecs.size());
    const auto preferredLibraryInstrumentId = libraryKit.getPreferredInstrumentId(libraryInstrument);
    const auto fallbackType = slot.type != KitLib::InstrumentSpec::Type::Unknown ? slot.type : libraryInstrument.meta.type;
    const auto& fallbackPreferences = fallbackRules.forType(fallbackType);

    for (const auto& libraryArticulationPtr : libraryInstrument.articulationSpecs) {
        if (libraryArticulationPtr == nullptr) continue;

        const auto preferredLibraryArticulationId = libraryInstrument.getPreferredArticulationId(*libraryArticulationPtr).trim();
        if (preferredLibraryArticulationId.isEmpty()) continue;

        const auto* articulationSlot = findCompatibleArticulationSlotByCode(slot, *libraryArticulationPtr);
        if (articulationSlot == nullptr) {
            articulationSlot = findFallbackArticulationSlot(slot, fallbackPreferences);
            if (articulationSlot == nullptr) {
                result.addWarning("Library articulation '" + preferredLibraryArticulationId + "' has no compatible articulation slot in '" + slot.id + "' and no configured fallback slot is available; map layer skipped");
                continue;
            }
            result.addWarning("Library articulation '" + preferredLibraryArticulationId + "' has no compatible articulation slot in '" + slot.id + "'; mapped to fallback slot '" + articulationSlot->codeRaw + "' using the '" + KitLib::InstrumentSpec::typeToString(fallbackType) + "' rule");
        }

        InstrumentMap::Layer mapLayer;
        mapLayer.slotId = slot.id;
        mapLayer.slotArticulationFilter = { articulationSlot->codeRaw };
        mapLayer.kitRef = preferredLibraryKitId;
        mapLayer.instCode = preferredLibraryInstrumentId;
        mapLayer.articulationFilter = { preferredLibraryArticulationId };
        if (const auto inferredFlip = inferAutomaticFlipLR(slot, libraryInstrument, mapLayer)) mapLayer.flipLR = *inferredFlip;
        result.value.push_back(std::move(mapLayer));
    }

    return result;
}

void replaceSlotLayers(InstrumentMap::Map& instrumentMap, const juce::String& slotId, std::vector<InstrumentMap::Layer> replacements) {
    std::erase_if(instrumentMap.layers, [&slotId](const auto& mapLayer) {
        return mapLayer.slotId.compareIgnoreCase(slotId) == 0;
    });
    instrumentMap.layers.insert(instrumentMap.layers.end(), std::make_move_iterator(replacements.begin()), std::make_move_iterator(replacements.end()));
}

std::vector<LayerProjection> projectLayers(const InstrumentMap::Map& instrumentMap, const KitLayout& layout) {
    std::vector<LayerProjection> result;
    for (const auto& slot : layout.instrumentSlots) {
        std::vector<juce::String> articulationSlotCodes;
        if (slot.articulations.empty()) articulationSlotCodes.push_back("(default)");
        else {
            articulationSlotCodes.reserve(slot.articulations.size());
            for (const auto& articulationSlot : slot.articulations) articulationSlotCodes.push_back(articulationSlot.codeRaw);
        }

        for (const auto& articulationSlotCode : articulationSlotCodes) {
            int articulationSlotLayerIndex = 0;
            for (size_t mapLayerIndex = 0; mapLayerIndex < instrumentMap.layers.size(); ++mapLayerIndex) {
                const auto& mapLayer = instrumentMap.layers[mapLayerIndex];
                if (mapLayer.slotId.compareIgnoreCase(slot.id) != 0) continue;

                const auto& articulationSlotFilters = mapLayer.slotArticulationFilter;
                bool matchesArticulationSlot = articulationSlotFilters.empty();
                for (const auto& filter : articulationSlotFilters) {
                    if (ArticulationNames::mapFilterMatchesArticulation(filter, articulationSlotCode)) {
                        matchesArticulationSlot = true;
                        break;
                    }
                }
                if (!matchesArticulationSlot) continue;

                result.push_back({ mapLayerIndex, slot.id, articulationSlotCode, articulationSlotLayerIndex, slot.type });
                ++articulationSlotLayerIndex;
            }
        }
    }
    return result;
}

std::vector<LayerProjection> collectLinkedMapLayerTargets(const std::vector<LayerProjection>& projections, const juce::String& selectedSlotId, const juce::String& selectedArticulationSlotCode, int selectedArticulationSlotLayerIndex, bool linkArticulations, bool linkAllTomBleed) {
    std::vector<LayerProjection> result;
    std::unordered_set<std::string> targetKeys;
    for (const auto& projection : projections) {
        const bool isSelected = projection.slotId.compareIgnoreCase(selectedSlotId) == 0 && projection.articulationSlotCode.compareIgnoreCase(selectedArticulationSlotCode) == 0 && projection.articulationSlotLayerIndex == selectedArticulationSlotLayerIndex;
        const bool isLinkedArticulation = linkArticulations && projection.slotId.compareIgnoreCase(selectedSlotId) == 0 && projection.articulationSlotLayerIndex == selectedArticulationSlotLayerIndex;
        const bool isLinkedTomBleed = linkAllTomBleed && projection.slotType == KitLib::InstrumentSpec::Type::Tom;
        if (!isSelected && !isLinkedArticulation && !isLinkedTomBleed) continue;

        const auto key = (projection.slotId + "\n" + projection.articulationSlotCode + "\n" + juce::String(projection.articulationSlotLayerIndex)).toStdString();
        if (targetKeys.insert(key).second) result.push_back(projection);
    }
    return result;
}

juce::String libraryArticulationForSlot(const InstrumentMap::Layer& mapLayer, const juce::String& articulationSlotCode) {
    if (mapLayer.articulationFilter.empty()) return {};
    for (const auto& articulationSlotFilter : mapLayer.slotArticulationFilter)
        if (ArticulationNames::mapFilterMatchesArticulation(articulationSlotFilter, articulationSlotCode)) return mapLayer.articulationFilter.front();
    return {};
}

juce::String qualifiedLibraryArticulationIdForSlot(const InstrumentMap::Layer& mapLayer, const juce::String& articulationSlotCode) {
    juce::String qualifiedId = mapLayer.kitRef;
    if (mapLayer.instCode.isNotEmpty()) qualifiedId += "/" + mapLayer.instCode;
    const auto libArtId = libraryArticulationForSlot(mapLayer, articulationSlotCode);
    if (libArtId.isNotEmpty()) qualifiedId += "/" + libArtId;
    return qualifiedId;
}

bool usesLegacyArticulationSlotExpressionFilter(const std::vector<juce::String>& articulationSlotFilters, const juce::String& articulationSlotCode) {
    if (!articulationSlotCode.containsChar('[') || articulationSlotFilters.empty()) return false;
    for (const auto& filter : articulationSlotFilters)
        if (ArticulationNames::articulationCodeMatches(filter, articulationSlotCode)) return false;
    for (const auto& filter : articulationSlotFilters)
        if (ArticulationNames::mapFilterMatchesArticulation(filter, articulationSlotCode)) return true;
    return false;
}

juce::String resolveLegacyHihatLibraryArticulationId(const KitLib::DrumKitSpec& libraryKit, const juce::String& qualifiedLibraryArticulationId, const juce::String& articulationSlotCode) {
    const int expressionStartIdx = articulationSlotCode.indexOfChar('[');
    const int expressionEndIdx = articulationSlotCode.indexOfChar(expressionStartIdx + 1, ']');
    if (expressionStartIdx < 0 || expressionEndIdx <= expressionStartIdx + 1) return qualifiedLibraryArticulationId;

    juce::StringArray idParts;
    idParts.addTokens(qualifiedLibraryArticulationId.trim(), "/", "");
    idParts.removeEmptyStrings();
    if (idParts.size() < 3) return qualifiedLibraryArticulationId;

    const auto baseLibArtId = ArticulationNames::baseArticulationName(idParts[2]);
    const auto articulationSlotExpression = articulationSlotCode.substring(expressionStartIdx + 1, expressionEndIdx).trim();
    if (baseLibArtId.isEmpty() || articulationSlotExpression.isEmpty()) return qualifiedLibraryArticulationId;

    const auto* libInst = libraryKit.findInstrumentById(idParts[1]);
    if (libInst == nullptr) return qualifiedLibraryArticulationId;

    const auto* libArt = libInst->findArticulationById(baseLibArtId + "[" + articulationSlotExpression + "]");
    if (libArt == nullptr) return qualifiedLibraryArticulationId;

    return idParts[0] + "/" + libraryKit.getPreferredInstrumentId(*libInst) + "/" + libInst->getPreferredArticulationId(*libArt);
}

size_t isolateLayerForArticulationSlot(InstrumentMap::Map& instrumentMap, const KitLayout::InstrumentSlot& slot, size_t mapLayerIndex, const juce::String& articulationSlotCode) {
    if (mapLayerIndex >= instrumentMap.layers.size() || slot.articulations.size() < 2) return mapLayerIndex;
    const auto originalMapLayer = instrumentMap.layers[mapLayerIndex];

    std::vector<InstrumentMap::Layer> replacements;
    size_t selectedReplacementIndex = 0;
    const auto& articulationSlotFilters = originalMapLayer.slotArticulationFilter;
    for (const auto& articulationSlot : slot.articulations) {
        bool matchesArticulationSlot = articulationSlotFilters.empty();
        for (const auto& filter : articulationSlotFilters) {
            if (ArticulationNames::mapFilterMatchesArticulation(filter, articulationSlot.codeRaw)) {
                matchesArticulationSlot = true;
                break;
            }
        }
        if (!matchesArticulationSlot) continue;

        auto replacement = originalMapLayer;
        replacement.slotArticulationFilter = { articulationSlot.codeRaw };
        replacements.push_back(std::move(replacement));
        if (ArticulationNames::articulationCodeMatches(articulationSlot.codeRaw, articulationSlotCode)) selectedReplacementIndex = replacements.size() - 1;
    }
    if (replacements.size() < 2) return mapLayerIndex;

    instrumentMap.layers.erase(instrumentMap.layers.begin() + static_cast<std::ptrdiff_t>(mapLayerIndex));
    instrumentMap.layers.insert(instrumentMap.layers.begin() + static_cast<std::ptrdiff_t>(mapLayerIndex), replacements.begin(), replacements.end());
    return mapLayerIndex + selectedReplacementIndex;
}

} // namespace InstrumentMap
