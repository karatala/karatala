#pragma once

#include "../../Config/Parsing.h"

#include <juce_core/juce_core.h>
#include <optional>
#include <unordered_map>
#include <vector>

namespace InstrumentMap {

struct Layer {
    juce::String slotId;
    std::vector<juce::String> slotArticulationFilter;
    juce::String kitRef;
    juce::String instCode;
    std::vector<juce::String> articulationFilter;
    std::optional<int> tuningCents;
    std::optional<int> attackTimeMs;
    std::optional<int> decayTimeMs;
    std::optional<float> volumeDb;
    std::optional<int> pan;
    std::optional<juce::String> velocityResponse;
    bool flipLR = false;
    bool flipLRExplicit = false;
    bool flipPhase = false;
    std::unordered_map<juce::String, float> micBleedLevels;
    std::unordered_map<juce::String, juce::String> micRouteOverrides;
};

struct Map {
    std::vector<Layer> layers;
};

ParseResult<Map> parseText(const juce::String& text);
juce::String serialize(const Map& instrumentMap);

} // namespace InstrumentMap
