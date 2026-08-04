#pragma once

#include "../Config/LogLevel.h"
#include "ParameterPot.h"
#include "UITheme.h"

namespace ParameterPotHelpers {

inline ParameterPot::LayoutOptions makeHorizontalPotLayout() {
    ParameterPot::LayoutOptions options;
    options.layoutMode = ParameterPot::LayoutMode::Horizontal;
    options.titleJustification = juce::Justification::centredLeft;
    options.valueJustification = juce::Justification::centredLeft;
    options.valueFontHeight = 13.0f;
    return options;
}

inline ParameterPot::LayoutOptions makeSettingsPotLayout(bool isInteger) {
    ParameterPot::LayoutOptions options;
    options.valueDisplay = ParameterPot::ValueDisplay::TextBoxBelow;
    options.textBoxWidth = isInteger ? UITheme::Sizes::settingsIntTextBoxW : UITheme::Sizes::settingsTextBoxW;
    options.textBoxHeight = UITheme::Sizes::settingsTextBoxH;
    return options;
}

inline juce::String millisecondsText(double seconds) { return juce::String(seconds * 1000.0, 0) + " ms"; }
inline double millisecondsValue(const juce::String& text) { return text.getDoubleValue() / 1000.0; }
inline juce::String percentageText(double value) { return juce::String(value * 100.0, 0) + "%"; }
inline double percentageValue(const juce::String& text) { return text.getDoubleValue() / 100.0; }
inline int debugLevelToChoiceId(DebugLevel level) { return static_cast<int>(level) + 2; }
inline DebugLevel choiceIdToDebugLevel(int selectedId) { return static_cast<DebugLevel>(juce::jlimit(-1, 5, selectedId - 2)); }

} // namespace ParameterPotHelpers
