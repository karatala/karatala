#include "InstrumentMap.h"

#include "../VelocityResponse.h"
#include <algorithm>
#include <utility>

namespace InstrumentMapDetail {

static juce::String makeLayerSortKey(const InstrumentMap::Layer& mapLayer) {
    const auto articulationSlotCode = mapLayer.slotArticulationFilter.empty() ? juce::String() : mapLayer.slotArticulationFilter.front().trim().toLowerCase();
    return mapLayer.slotId.trim().toLowerCase() + "/" + articulationSlotCode;
}

} // namespace InstrumentMapDetail

juce::String InstrumentMap::identityKey(const juce::String& value) {
    return value.trim().toLowerCase();
}

bool InstrumentMap::candidateMatches(const std::vector<juce::String>& candidates, const juce::String& id) {
    const auto key = identityKey(id);
    if (key.isEmpty()) return false;
    for (const auto& candidate : candidates)
        if (identityKey(candidate) == key) return true;
    return false;
}

namespace InstrumentMapDetail {

struct ParsedSlotRef {
    juce::String slotId;
    std::vector<juce::String> articulationSlotCodes;
    bool hasExtraPathSegment = false;
};

static std::vector<juce::String> parseDelimitedNames(const juce::String& text) {
    std::vector<juce::String> result;
    juce::StringArray parts;
    parts.addTokens(text, ",", "");
    for (const auto& part : parts) {
        const auto name = part.trim();
        if (name.isNotEmpty()) result.push_back(name);
    }
    return result;
}

static ParsedSlotRef parseSlotRef(const juce::String& text) {
    ParsedSlotRef result;
    const auto trimmed = text.trim();
    const int slashIdx = trimmed.indexOfChar('/');
    if (slashIdx < 0) {
        result.slotId = trimmed;
        return result;
    }

    result.slotId = trimmed.substring(0, slashIdx).trim();
    const auto articulationSlotText = trimmed.substring(slashIdx + 1);
    result.hasExtraPathSegment = articulationSlotText.containsChar('/');
    result.articulationSlotCodes = parseDelimitedNames(articulationSlotText);
    return result;
}

struct ParsedLibraryRef {
    juce::String kitRef;
    juce::String instCode;
    std::vector<juce::String> libraryArticulationCodes;
    bool hasExtraPathSegment = false;
};

static ParsedLibraryRef parseLibraryRef(const juce::String& text) {
    ParsedLibraryRef result;
    const auto trimmed = text.trim();
    const int firstSlashIdx = trimmed.indexOfChar('/');
    if (firstSlashIdx < 0) {
        result.kitRef = trimmed;
        return result;
    }

    result.kitRef = trimmed.substring(0, firstSlashIdx).trim();
    const auto remainder = trimmed.substring(firstSlashIdx + 1).trim();
    const int secondSlashIdx = remainder.indexOfChar('/');
    if (secondSlashIdx < 0) {
        result.instCode = remainder;
        return result;
    }

    result.instCode = remainder.substring(0, secondSlashIdx).trim();
    const auto articulationText = remainder.substring(secondSlashIdx + 1).trim();
    result.hasExtraPathSegment = articulationText.containsChar('/');
    result.libraryArticulationCodes = parseDelimitedNames(articulationText);
    return result;
}

static int findNextParamBoundary(const juce::String& text, int startIndex) {
    for (int idx = startIndex; idx < text.length(); ++idx)
        if (text[idx] == ';') return idx;
    return -1;
}

} // namespace InstrumentMapDetail

/**
 * Parses instrument map text.
 *
 * Line format:
 *   slot/articulationSlot:libraryKit/libraryInstrument/libraryArticulation[:param=value;param=value,value;param=key:value,key:value]
 *
 * Examples:
 *   kick1/center:MyKit/kick-24/center
 *   ride1/bow:KitA/ride-20/bow:tuning=-2;attack=50;decay=300
 *   snare1/rim:MixedKit/snare-14/sidestick:vol=-3.5;bleed=oh:0.25,room:0.5;micRoute=oh:room
 *
 */
ParseResult<InstrumentMap::Map> InstrumentMap::parseText(const juce::String& text) {
    ParseResult<InstrumentMap::Map> result;
    const juce::StringArray lines = juce::StringArray::fromLines(text);
    auto parseInt = [](const juce::String& valueText, const juce::String& key, ParseResult<InstrumentMap::Map>& parseResult, int lineNumber, std::optional<int>& target) {
        auto trimmed = valueText.trim();
        if (trimmed.isEmpty() || !trimmed.containsOnly("+-0123456789")) {
            parseResult.addWarning("Line " + juce::String(lineNumber) + ": invalid value for '" + key + "': " + valueText);
            return;
        }
        target = trimmed.getIntValue();
    };

    for (int idx = 0; idx < lines.size(); ++idx) {
        auto line = lines[idx].trim();
        if (line.isEmpty() || line.startsWithChar('#'))
            continue;

        const int firstColon = line.indexOfChar(':');
        if (firstColon <= 0) {
            result.addWarning("Line " + juce::String(idx + 1) + ": expected '<instrument slot>:<library kit>/<library instrument>/<library articulation>'");
            continue;
        }

        const int secondColon = line.indexOf(firstColon + 1, ":");
        const auto slotText = line.substring(0, firstColon).trim();
        const auto targetText = secondColon >= 0 ? line.substring(firstColon + 1, secondColon).trim() : line.substring(firstColon + 1).trim();
        const auto paramsText = secondColon >= 0 ? line.substring(secondColon + 1).trim() : juce::String();

        if (slotText.isEmpty() || targetText.isEmpty()) {
            result.addWarning("Line " + juce::String(idx + 1) + ": instrument slot or library path is empty");
            continue;
        }

        auto slotRef = InstrumentMapDetail::parseSlotRef(slotText);
        auto libraryRef = InstrumentMapDetail::parseLibraryRef(targetText);

        if (slotRef.slotId.isEmpty()) {
            result.addWarning("Line " + juce::String(idx + 1) + ": instrument slot id is empty");
            continue;
        }
        if (slotRef.hasExtraPathSegment || slotRef.articulationSlotCodes.size() != 1) {
            result.addWarning("Line " + juce::String(idx + 1) + ": one articulation slot is required; expected 'instrumentSlot/articulationSlot'");
            continue;
        }
        if (libraryRef.kitRef.isEmpty()) {
            result.addWarning("Line " + juce::String(idx + 1) + ": library kit is empty; expected 'kit/instrument/articulation'");
            continue;
        }
        if (libraryRef.instCode.isEmpty()) {
            result.addWarning("Line " + juce::String(idx + 1) + ": library instrument is empty; expected 'kit/instrument/articulation'");
            continue;
        }
        if (libraryRef.hasExtraPathSegment || libraryRef.libraryArticulationCodes.size() != 1) {
            result.addWarning("Line " + juce::String(idx + 1) + ": one library articulation is required; expected 'kit/instrument/articulation'");
            continue;
        }

        InstrumentMap::Layer mapLayer;
        mapLayer.slotId = slotRef.slotId;
        mapLayer.slotArticulationFilter = slotRef.articulationSlotCodes;
        mapLayer.kitRef = libraryRef.kitRef;
        mapLayer.instCode = libraryRef.instCode;
        mapLayer.articulationFilter = libraryRef.libraryArticulationCodes;

        if (!paramsText.isEmpty()) {
            int start = 0;
            while (start < paramsText.length()) {
                while (start < paramsText.length() && (paramsText[start] == ';' || juce::CharacterFunctions::isWhitespace(paramsText[start])))
                    ++start;
                if (start >= paramsText.length())
                    break;

                const int equalsIdx = paramsText.indexOf(start, "=");
                if (equalsIdx < 0) {
                    result.addWarning("Line " + juce::String(idx + 1) + ": malformed param '" + paramsText.substring(start).trim() + "'");
                    break;
                }

                const auto key = paramsText.substring(start, equalsIdx).trim();
                const int boundaryIdx = InstrumentMapDetail::findNextParamBoundary(paramsText, equalsIdx + 1);
                const auto value = (boundaryIdx < 0 ? paramsText.substring(equalsIdx + 1) : paramsText.substring(equalsIdx + 1, boundaryIdx)).trim();

                if (key == "tuning")
                    parseInt(value, key, result, idx + 1, mapLayer.tuningCents);
                else if (key == "attack")
                    parseInt(value, key, result, idx + 1, mapLayer.attackTimeMs);
                else if (key == "decay")
                    parseInt(value, key, result, idx + 1, mapLayer.decayTimeMs);
                else if (key == "vol") {
                    auto trimmed = value.trim();
                    if (trimmed.isEmpty() || !trimmed.containsOnly("+-0123456789."))
                        result.addWarning("Line " + juce::String(idx + 1) + ": invalid value for 'vol': " + value);
                    else
                        mapLayer.volumeDb = trimmed.getFloatValue();
                } else if (key == "pan") {
                    parseInt(value, key, result, idx + 1, mapLayer.pan);
                } else if (key == "velocityResponse") {
                    auto parsedResponse = VelocityResponse::fromDefinition(value);
                    if (!parsedResponse.ok()) result.addWarning("Line " + juce::String(idx + 1) + ": invalid value for 'velocityResponse': " + parsedResponse.error());
                    else mapLayer.velocityResponse = parsedResponse.value.getDefinition();
                } else if (key == "flipLR") {
                    mapLayer.flipLR = (value.trim() == "1" || value.trim().equalsIgnoreCase("true"));
                    mapLayer.flipLRExplicit = true;
                } else if (key == "flipPhase") {
                    mapLayer.flipPhase = (value.trim() == "1" || value.trim().equalsIgnoreCase("true"));
                } else if (key == "bleed") {
                    // Format: bleed=micCode:level,micCode:level,...
                    juce::StringArray pairs;
                    pairs.addTokens(value, ",", "");
                    for (const auto& pair : pairs) {
                        auto colonPos = pair.indexOf(":");
                        if (colonPos > 0) {
                            auto micCode = pair.substring(0, colonPos).trim();
                            auto levelStr = pair.substring(colonPos + 1).trim();
                            if (micCode.isNotEmpty() && levelStr.containsOnly("0123456789."))
                                mapLayer.micBleedLevels[micCode] = levelStr.getFloatValue();
                        }
                    }
                } else if (key == "micRoute") {
                    // Format: micRoute=micCode:outputId,micCode:outputId,...
                    juce::StringArray pairs;
                    pairs.addTokens(value, ",", "");
                    for (const auto& pair : pairs) {
                        auto colonPos = pair.indexOf(":");
                        if (colonPos > 0) {
                            auto micCode = pair.substring(0, colonPos).trim();
                            auto outputId = pair.substring(colonPos + 1).trim();
                            if (micCode.isNotEmpty() && outputId.isNotEmpty())
                                mapLayer.micRouteOverrides[micCode] = outputId;
                        }
                    }
                } else
                    result.addWarning("Line " + juce::String(idx + 1) + ": unknown param '" + key + "'");

                if (boundaryIdx < 0)
                    break;
                start = boundaryIdx + 1;
            }
        }

        result.value.layers.push_back(std::move(mapLayer));
    }

    if (result.value.layers.empty())
        return result.returnWithError("No valid instrument map layers found");

    return result;
}

juce::String InstrumentMap::serialize(const Map& instrumentMap) {
    juce::StringArray lines;

    std::vector<const Layer*> sortedMapLayers;
    sortedMapLayers.reserve(instrumentMap.layers.size());
    for (const auto& mapLayer : instrumentMap.layers) sortedMapLayers.push_back(&mapLayer);

    std::stable_sort(sortedMapLayers.begin(), sortedMapLayers.end(), [](const auto* leftMapLayer, const auto* rightMapLayer) {
        return InstrumentMapDetail::makeLayerSortKey(*leftMapLayer) < InstrumentMapDetail::makeLayerSortKey(*rightMapLayer);
    });

    for (const auto* mapLayerPtr : sortedMapLayers) {
        const auto& mapLayer = *mapLayerPtr;
        if (mapLayer.slotId.trim().isEmpty() || mapLayer.kitRef.trim().isEmpty() || mapLayer.instCode.trim().isEmpty()) continue;
        if (mapLayer.articulationFilter.size() != 1 || mapLayer.articulationFilter.front().trim().isEmpty() || mapLayer.slotArticulationFilter.size() != 1 || mapLayer.slotArticulationFilter.front().trim().isEmpty()) continue;

        const auto slotRef = mapLayer.slotId.trim() + "/" + mapLayer.slotArticulationFilter.front().trim();

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
