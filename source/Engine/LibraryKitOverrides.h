#pragma once

#include "../Config/Helpers.h"
#include <map>
#include <optional>
#include <vector>

namespace KitLib {

struct MicOverride {
    std::optional<float> playbackOffsetMs;
};

struct SampleOverride {
    juce::String contentHash;
    std::optional<float> manualGainDb;
    std::optional<float> playbackOffsetMs;
    std::map<juce::String, MicOverride> micOverrides;
};

struct ArticulationOverride {
    std::optional<int> balance;
    std::map<uint8_t, uint8_t> velocityUpperBounds;
};

struct InstrumentOverride {
    std::optional<int> balance;
    std::map<juce::String, ArticulationOverride> articulationOverrides;
};

struct SampleOverrideMatch {
    const SampleOverride* value = nullptr;
    juce::String storedRelativePath;
    bool matchedByContentHash = false;
    bool ambiguousContentHash = false;
};

class LibraryKitOverrides {
public:
    static constexpr int FORMAT_VERSION = 1;
    static constexpr const char* FILE_NAME = "kit-overrides.yml";

    ParseResult<int> loadFromKitRoot(const juce::File& kitRoot);
    ParseResult<int> parseYaml(const juce::String& yaml);
    ParseResult<bool> saveToKitRoot(const juce::File& kitRoot);
    juce::String toYaml() const;

    const InstrumentOverride* findInstrumentOverride(const juce::String& instrumentCodeRaw) const;
    const ArticulationOverride* findArticulationOverride(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw) const;
    const SampleOverride* findSampleOverride(const juce::String& relativePath) const;
    SampleOverrideMatch findSampleOverride(const juce::String& relativePath, const juce::String& contentHash) const;
    const MicOverride* findMicOverride(const juce::String& relativePath, const juce::String& micCode) const;

    void setInstrumentBalance(const juce::String& instrumentCodeRaw, std::optional<int> balance);
    void setArticulationBalance(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw, std::optional<int> balance);
    void setVelocityUpperBoundary(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw, uint8_t namedVelocity, std::optional<uint8_t> upperBoundary);
    void resetVelocityUpperBoundaries(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw);
    void setSampleManualGain(const juce::String& relativePath, const juce::String& contentHash, std::optional<float> manualGainDb);
    void setSamplePlaybackOffset(const juce::String& relativePath, const juce::String& contentHash, std::optional<float> playbackOffsetMs);
    void setMicPlaybackOffset(const juce::String& relativePath, const juce::String& contentHash, const juce::String& micCode, std::optional<float> playbackOffsetMs);
    ParseResult<std::vector<uint8_t>> resolveVelocityUpperBoundaries(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw, const std::vector<uint8_t>& namedVelocities) const;

    bool isActive() const { return !this->instrumentOverrides.empty() || !this->sampleOverrides.empty(); }
    int getExplicitValueCount() const;

    std::map<juce::String, InstrumentOverride> instrumentOverrides;
    std::map<juce::String, SampleOverride> sampleOverrides;

private:
    bool loadedFileExisted = false;
    bool loadedFileValid = true;
    juce::String loadedFileContent;

    void removeEmptyInstrumentOverride(const juce::String& instrumentCodeRaw);
    void removeEmptySampleOverride(const juce::String& relativePath);
};

} // namespace KitLib
