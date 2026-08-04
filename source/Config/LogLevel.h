#pragma once

#include <juce_core/juce_core.h>

enum DebugLevel {
    Off = -1,
    Error = 0,
    Warning = 1,
    Notice = 2,
    Info = 3,
    Debug = 4,
    Verbose = 5
};

juce::String DebugLevelToString(DebugLevel level);
juce::String DebugLevelToYaml(DebugLevel level);
DebugLevel parseDebugLevelOrDefault(const juce::String& value, DebugLevel defaultLevel = DebugLevel::Off);
bool isValidDebugLevelString(const juce::String& value);
