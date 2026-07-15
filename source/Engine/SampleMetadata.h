#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <optional>
#include <unordered_map>
#include "../Config/Helpers.h"

namespace KitLib {

struct SampleMetadataRow {
    static constexpr int DECAY_POINT_COUNT = 10;

    juce::String relativePath;
    juce::String micCode;
    juce::String contentHash;
    juce::int64 fileSizeBytes = 0;
    std::optional<float> onsetMs;
    std::optional<float> intensityDb;
    std::optional<float> truePeakDb;
    std::optional<float> peakDb;
    std::optional<float> peakMs;
    std::optional<float> decayEndMs;
    std::optional<float> stereoBalance;
    std::array<std::optional<float>, DECAY_POINT_COUNT> decayPointsMs;
};

class SampleMetadataTable {
public:
    ParseResult<int> loadFromKitRoot(const juce::File& kitRoot);
    const SampleMetadataRow* find(const juce::String& relativePath, const juce::String& micCode) const;
    bool isActive() const { return this->active; }
    int size() const { return static_cast<int>(this->rowsByKey.size()); }

private:
    static juce::String makeKey(const juce::String& relativePath, const juce::String& micCode);

    bool active = false;
    std::unordered_map<juce::String, SampleMetadataRow> rowsByKey;
};

} // namespace KitLib
