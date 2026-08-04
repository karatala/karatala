#pragma once

#include "../Config/Helpers.h"
#include "../Config/YamlHelper.h"
#include "InstrumentCatalog.h"

#include <map>
#include <utility>
#include <vector>

class ArticulationSlotFallbackRules {
public:
    static ArticulationSlotFallbackRules makeDefault() {
        ArticulationSlotFallbackRules rules;
        rules.preferences[KitLib::InstrumentType::Kick] = { "center", "head" };
        rules.preferences[KitLib::InstrumentType::Snare] = { "center", "rim", "sidestick" };
        rules.preferences[KitLib::InstrumentType::Tom] = { "center", "rim" };
        rules.preferences[KitLib::InstrumentType::HiHat] = { "bow", "edge", "pedal" };
        rules.preferences[KitLib::InstrumentType::Crash] = { "edge", "bow", "bell" };
        rules.preferences[KitLib::InstrumentType::Splash] = { "edge", "bow", "bell" };
        rules.preferences[KitLib::InstrumentType::China] = { "edge", "bow", "bell" };
        rules.preferences[KitLib::InstrumentType::Ride] = { "bow", "edge", "bell" };
        return rules;
    }

    static ParseResult<ArticulationSlotFallbackRules> parseYaml(const YamlHelper::YamlNode& node, const InstrumentCatalog& instrumentCatalog, ArticulationSlotFallbackRules base = makeDefault()) {
        ParseResult<ArticulationSlotFallbackRules> result;
        if (node.isNull()) {
            result.value = std::move(base);
            return result;
        }
        if (!node.isMap()) return result.returnWithError("articulationFallback must be a map of instrument types to articulation-slot lists");

        for (const auto& typeName : node.keys()) {
            const auto instrumentType = instrumentCatalog.resolveKind(typeName);
            if (instrumentType == KitLib::InstrumentType::Unknown) {
                result.addWarning("articulationFallback: unknown instrument type '" + typeName + "'");
                continue;
            }

            const auto& preferenceNode = node[typeName];
            if (!preferenceNode.isSequence()) {
                result.addWarning("articulationFallback: '" + typeName + "' must be an articulation-slot list");
                continue;
            }

            std::vector<juce::String> typePreferences;
            for (const auto& item : preferenceNode.sequenceValue) {
                const auto articulationSlot = item.asString().trim();
                if (articulationSlot.isEmpty()) {
                    result.addWarning("articulationFallback: '" + typeName + "' contains an empty articulation slot");
                    continue;
                }
                if (!vectorContains(typePreferences, articulationSlot)) typePreferences.push_back(articulationSlot);
            }
            base.preferences[instrumentType] = std::move(typePreferences);
        }

        result.value = std::move(base);
        return result;
    }

    const std::vector<juce::String>& forType(KitLib::InstrumentType instrumentType) const {
        static const std::vector<juce::String> empty;
        const auto iterator = this->preferences.find(instrumentType);
        return iterator != this->preferences.end() ? iterator->second : empty;
    }

    juce::String toYaml() const {
        juce::String yaml = "articulationFallback:\n";
        for (const auto& [instrumentType, typePreferences] : this->preferences) {
            juce::StringArray values;
            for (const auto& preference : typePreferences) values.add(preference);
            yaml += "  " + KitLib::instrumentTypeToString(instrumentType) + ": [" + values.joinIntoString(", ") + "]\n";
        }
        return yaml;
    }

private:
    std::map<KitLib::InstrumentType, std::vector<juce::String>> preferences;
};
