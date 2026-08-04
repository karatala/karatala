#pragma once

#include "../Config/Helpers.h"

namespace ArticulationNames {

inline juce::String baseArticulationName(const juce::String& codeRaw) { return codeRaw.upToFirstOccurrenceOf("[", false, false).trim(); }

inline juce::String normalizeArticulationName(const juce::String& rawName) { return baseArticulationName(rawName).toLowerCase(); }

inline bool articulationCodeMatches(const juce::String& left, const juce::String& right) { return left.trim().compareIgnoreCase(right.trim()) == 0; }

inline bool baseArticulationMatches(const juce::String& left, const juce::String& right) { return normalizeArticulationName(left) == normalizeArticulationName(right); }

inline bool isLegacyBaseArticulationFilter(const juce::String& artName) { return !artName.containsChar('['); }

inline bool mapFilterMatchesArticulation(const juce::String& filter, const juce::String& artName) {
    if (articulationCodeMatches(filter, artName)) return true;
    return isLegacyBaseArticulationFilter(filter) && baseArticulationMatches(filter, artName);
}

inline bool stringArrayContainsIgnoreCase(const juce::StringArray& values, const juce::String& needle) {
    for (const auto& value : values)
        if (value.compareIgnoreCase(needle) == 0) return true;
    return false;
}

inline int levenshteinDistance(const juce::String& left, const juce::String& right) {
    const int leftLength = left.length();
    const int rightLength = right.length();

    if (leftLength == 0) return rightLength;
    if (rightLength == 0) return leftLength;

    std::vector<int> previousRow(static_cast<size_t>(rightLength) + 1);
    std::vector<int> currentRow(static_cast<size_t>(rightLength) + 1);

    for (int column = 0; column <= rightLength; ++column)
        previousRow[static_cast<size_t>(column)] = column;

    for (int leftIdx = 1; leftIdx <= leftLength; ++leftIdx) {
        currentRow[0] = leftIdx;
        const auto leftChar = left[leftIdx - 1];

        for (int rightIdx = 1; rightIdx <= rightLength; ++rightIdx) {
            const int substitutionCost = leftChar == right[rightIdx - 1] ? 0 : 1;
            const int deletionCost = previousRow[static_cast<size_t>(rightIdx)] + 1;
            const int insertionCost = currentRow[static_cast<size_t>(rightIdx - 1)] + 1;
            const int substitutionTotal = previousRow[static_cast<size_t>(rightIdx - 1)] + substitutionCost;

            currentRow[static_cast<size_t>(rightIdx)] = std::min(std::min(deletionCost, insertionCost), substitutionTotal);
        }
        std::swap(previousRow, currentRow);
    }
    return previousRow[static_cast<size_t>(rightLength)];
}

inline juce::String findClosestArticulationName(const juce::String& unknownName, const juce::StringArray& availableNames) {
    const auto normalizedUnknownName = normalizeArticulationName(unknownName);
    if (normalizedUnknownName.isEmpty() || availableNames.isEmpty()) return {};

    juce::String bestMatch;
    int bestDistance = 1000000;

    for (const auto& availableName : availableNames) {
        const auto normalizedAvailableName = normalizeArticulationName(availableName);
        if (normalizedAvailableName.isEmpty()) continue;

        const int distance = levenshteinDistance(normalizedUnknownName, normalizedAvailableName);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestMatch = availableName;
        }
    }
    return bestDistance <= 2 ? bestMatch : juce::String();
}

inline std::vector<uint32_t> toArticulationIdList(const std::vector<juce::String>& names) {
    std::vector<uint32_t> result;
    result.reserve(names.size());

    for (const auto& name : names) {
        if (name.isEmpty()) continue;
        const auto articulationId = StringsDictSingleton::getId(normalizeArticulationName(name));
        if (!vectorContains(result, articulationId)) result.push_back(articulationId);
    }
    return result;
}

} // namespace ArticulationNames
