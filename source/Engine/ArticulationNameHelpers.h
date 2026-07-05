#pragma once

#include <algorithm>

#include "../Config/Helpers.h"

namespace ArticulationNameHelpers {

juce::String baseArticulationName(const juce::String& codeRaw);

juce::String normalizeArticulationName(const juce::String& rawName);

bool articulationCodeMatches(const juce::String& left, const juce::String& right);

bool baseArticulationMatches(const juce::String& left, const juce::String& right);

bool isLegacyBaseArticulationFilter(const juce::String& artName);


bool mapFilterMatchesArticulation(const juce::String& filter, const juce::String& artName);

bool stringArrayContainsIgnoreCase(const juce::StringArray& values, const juce::String& needle);

int levenshteinDistance(const juce::String& left, const juce::String& right);

juce::String findClosestArticulationName(const juce::String& unknownName, const juce::StringArray& availableNames);

std::vector<uint32_t> toArticulationIdList(const std::vector<juce::String>& names);

} // namespace ArticulationNameHelpers
