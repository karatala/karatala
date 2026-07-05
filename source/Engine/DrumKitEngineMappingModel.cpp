#include "DrumKitEngineMappingModel.h"

namespace DrumKitEngineMappingModel {

struct ParsedSlotRef {
    juce::String slotId;
    std::vector<juce::String> articulations;
};
std::vector<juce::String> parseDelimitedNames(const juce::String& text) {
    std::vector<juce::String> result;
    juce::StringArray parts;
    parts.addTokens(text, ",", "");
    parts.removeEmptyStrings(true);
    for (const auto& part : parts) result.push_back(part.trim());
    return result;
}

ParsedSlotRef parseSlotRef(const juce::String& text) {
    ParsedSlotRef result;
    const auto trimmed = text.trim();
    const int slashIdx = trimmed.indexOfChar('/');
    if (slashIdx < 0) {
        result.slotId = trimmed;
        return result;
    }

    result.slotId = trimmed.substring(0, slashIdx).trim();
    result.articulations = parseDelimitedNames(trimmed.substring(slashIdx + 1));
    return result;
}


struct ParsedTargetRef {
    juce::String kitRef;
    juce::String instCode;
    std::vector<juce::String> articulations;
};

ParsedTargetRef parseTargetRef(const juce::String& text) {
    ParsedTargetRef result;
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
    result.articulations = parseDelimitedNames(remainder.substring(secondSlashIdx + 1));
    return result;
}

int findNextParamBoundary(const juce::String& text, int startIndex, const std::vector<juce::String>& knownKeys) {
    for (int idx = startIndex; idx < text.length(); ++idx) {
        const auto ch = text[idx];
        if (ch != ',' && ch != ';') continue;

        int probe = idx + 1;
        while (probe < text.length() && juce::CharacterFunctions::isWhitespace(text[probe]))
            ++probe;

        for (const auto& key : knownKeys)
            if (text.substring(probe).startsWith(key + "=")) return idx;
    }
    return -1;
}

ParseResult<DrumKitEngine::InstrumentMap> parseInstrumentMapText(const juce::String& text) {
    ParseResult<DrumKitEngine::InstrumentMap> result;
    const juce::StringArray lines = juce::StringArray::fromLines(text);
    const std::vector<juce::String> knownKeys { "tuning", "attack", "decay", "art", "vol", "flipLR", "flipPhase", "bleed" };

    auto parseInt = [](const juce::String& valueText, const juce::String& key, ParseResult<DrumKitEngine::InstrumentMap>& parseResult, int lineNumber, std::optional<int>& target) {
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
            result.addWarning("Line " + juce::String(idx + 1) + ": expected '<slot>:<target>'");
            continue;
        }

        const int secondColon = line.indexOf(firstColon + 1, ":");
        const auto slotText = line.substring(0, firstColon).trim();
        const auto targetText = secondColon >= 0 ? line.substring(firstColon + 1, secondColon).trim() : line.substring(firstColon + 1).trim();
        const auto paramsText = secondColon >= 0 ? line.substring(secondColon + 1).trim() : juce::String();

        if (slotText.isEmpty() || targetText.isEmpty()) {
            result.addWarning("Line " + juce::String(idx + 1) + ": empty slot or target");
            continue;
        }

        auto slotRef = parseSlotRef(slotText);
        auto targetRef = parseTargetRef(targetText);

        if (slotRef.slotId.isEmpty()) {
            result.addWarning("Line " + juce::String(idx + 1) + ": slot id is empty");
            continue;
        }
        if (targetRef.kitRef.isEmpty()) {
            result.addWarning("Line " + juce::String(idx + 1) + ": kit reference is empty");
            continue;
        }

        DrumKitEngine::InstrumentMapEntry entry;
        entry.slotId = slotRef.slotId;
        entry.slotArticulationFilter = slotRef.articulations;
        entry.kitRef = targetRef.kitRef;
        entry.instCode = targetRef.instCode;
        entry.articulationFilter = targetRef.articulations;
        bool sawDeprecatedArtParam = false;

        if (!paramsText.isEmpty()) {
            int start = 0;
            while (start < paramsText.length()) {
                while (start < paramsText.length() && (paramsText[start] == ',' || paramsText[start] == ';' || juce::CharacterFunctions::isWhitespace(paramsText[start])))
                    ++start;
                if (start >= paramsText.length())
                    break;

                const int equalsIdx = paramsText.indexOf(start, "=");
                if (equalsIdx < 0) {
                    result.addWarning("Line " + juce::String(idx + 1) + ": malformed param '" + paramsText.substring(start).trim() + "'");
                    break;
                }

                const auto key = paramsText.substring(start, equalsIdx).trim();
                const int boundaryIdx = findNextParamBoundary(paramsText, equalsIdx + 1, knownKeys);
                const auto value = (boundaryIdx < 0 ? paramsText.substring(equalsIdx + 1) : paramsText.substring(equalsIdx + 1, boundaryIdx)).trim();

                if (key == "tuning")
                    parseInt(value, key, result, idx + 1, entry.tuningCents);
                else if (key == "attack")
                    parseInt(value, key, result, idx + 1, entry.attackTimeMs);
                else if (key == "decay")
                    parseInt(value, key, result, idx + 1, entry.decayTimeMs);
                else if (key == "art") {
                    entry.articulationFilter = parseDelimitedNames(value);
                    if (!sawDeprecatedArtParam) {
                        result.addWarning("Line " + juce::String(idx + 1) + ": 'art=' is deprecated; use target path articulation '<kit>/<inst>/<sourceArt>'");
                        sawDeprecatedArtParam = true;
                    }
                } else if (key == "vol") {
                    auto trimmed = value.trim();
                    if (trimmed.isEmpty() || !trimmed.containsOnly("+-0123456789."))
                        result.addWarning("Line " + juce::String(idx + 1) + ": invalid value for 'vol': " + value);
                    else
                        entry.volumeDb = trimmed.getFloatValue();
                } else if (key == "flipLR") {
                    entry.flipLR = (value.trim() == "1" || value.trim().equalsIgnoreCase("true"));
                    entry.flipLRExplicit = true;
                } else if (key == "flipPhase") {
                    entry.flipPhase = (value.trim() == "1" || value.trim().equalsIgnoreCase("true"));
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
                                entry.micBleedLevels[micCode] = levelStr.getFloatValue();
                        }
                    }
                } else
                    result.addWarning("Line " + juce::String(idx + 1) + ": unknown param '" + key + "'");

                if (boundaryIdx < 0)
                    break;
                start = boundaryIdx + 1;
            }
        }

        if (entry.slotArticulationFilter.empty() && !entry.articulationFilter.empty())
            entry.slotArticulationFilter = entry.articulationFilter;
        else if (!entry.slotArticulationFilter.empty() && entry.articulationFilter.empty())
            entry.articulationFilter = entry.slotArticulationFilter;

        result.value.entries.push_back(std::move(entry));
    }

    if (result.value.entries.empty())
        return result.returnWithError("No valid instrument map entries found");

    return result;
}

}
