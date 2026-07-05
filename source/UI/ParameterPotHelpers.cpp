#include "ParameterPotHelpers.h"

#include "UITheme.h"

namespace ParameterPotHelpers {

ParameterPot::LayoutOptions makeHorizontalPotLayout() {
    ParameterPot::LayoutOptions options;
    options.layoutMode = ParameterPot::LayoutMode::Horizontal;
    options.titleJustification = juce::Justification::centredLeft;
    options.valueJustification = juce::Justification::centredLeft;
    options.valueFontHeight = 13.0f;
    return options;
}

ParameterPot::LayoutOptions makeSettingsPotLayout(bool isInteger) {
    ParameterPot::LayoutOptions options;
    options.valueDisplay = ParameterPot::ValueDisplay::TextBoxBelow;
    options.textBoxWidth = isInteger ? UITheme::Sizes::settingsIntTextBoxW : UITheme::Sizes::settingsTextBoxW;
    options.textBoxHeight = UITheme::Sizes::settingsTextBoxH;
    return options;
}

juce::String millisecondsText(double seconds) { return juce::String(seconds * 1000.0, 0) + " ms"; }
double millisecondsValue(const juce::String& text) { return text.getDoubleValue() / 1000.0; }
juce::String percentageText(double value) { return juce::String(value * 100.0, 0) + "%"; }
double percentageValue(const juce::String& text) { return text.getDoubleValue() / 100.0; }
int debugLevelToChoiceId(DebugLevel level) { return static_cast<int>(level) + 2; }
DebugLevel choiceIdToDebugLevel(int selectedId) { return static_cast<DebugLevel>(juce::jlimit(-1, 5, selectedId - 2)); }
} // namespace ParameterPotHelpers
