#pragma once

#include "../Config/PluginLogger.h"
#include "ParameterPot.h"

namespace ParameterPotHelpers {

ParameterPot::LayoutOptions makeHorizontalPotLayout();
ParameterPot::LayoutOptions makeSettingsPotLayout(bool isInteger);
juce::String millisecondsText(double seconds);
double millisecondsValue(const juce::String& text);
juce::String percentageText(double value);
double percentageValue(const juce::String& text);
int debugLevelToChoiceId(DebugLevel level);
DebugLevel choiceIdToDebugLevel(int selectedId);

} // namespace ParameterPotHelpers
