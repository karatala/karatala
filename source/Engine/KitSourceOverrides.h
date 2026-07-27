#pragma once

#include "../Config/Helpers.h"
#include <map>
#include <optional>
#include <vector>

namespace KitLib {

struct MicSourceOverride {
    std::optional<float> playbackOffsetMs;
};

struct SampleSourceOverride {
    juce::String contentHash;
    std::optional<float> manualGainDb;
    std::optional<float> playbackOffsetMs;
    std::map<juce::String, MicSourceOverride> micOverrides;
};

struct ArticulationSourceOverride {
    std::optional<int> balance;
    std::map<uint8_t, uint8_t> velocityUpperBounds;
};

struct InstrumentSourceOverride {
    std::optional<int> balance;
    std::map<juce::String, ArticulationSourceOverride> articulationOverrides;
};

class KitSourceOverrides {
public:
    static constexpr int FORMAT_VERSION = 1;
    static constexpr const char* FILE_NAME = "kit-overrides.yml";

    ParseResult<int> loadFromKitRoot(const juce::File& kitRoot);
    ParseResult<int> parseYaml(const juce::String& yaml);
    ParseResult<bool> saveToKitRoot(const juce::File& kitRoot) const;
    juce::String toYaml() const;

    const InstrumentSourceOverride* findInstrumentOverride(const juce::String& instrumentCodeRaw) const;
    const ArticulationSourceOverride* findArticulationOverride(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw) const;
    const SampleSourceOverride* findSampleOverride(const juce::String& relativePath) const;

    void setVelocityUpperBoundary(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw, uint8_t namedVelocity, std::optional<uint8_t> upperBoundary);
    void resetVelocityUpperBoundaries(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw);
    ParseResult<std::vector<uint8_t>> resolveVelocityUpperBoundaries(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw, const std::vector<uint8_t>& namedVelocities) const;

    bool isActive() const { return !this->instrumentOverrides.empty() || !this->sampleOverrides.empty(); }
    int getExplicitValueCount() const;

    std::map<juce::String, InstrumentSourceOverride> instrumentOverrides;
    std::map<juce::String, SampleSourceOverride> sampleOverrides;
};

} // namespace KitLib
