#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UITheme.h"

namespace UIComponentStyle {

enum class ComboBoxStyle {
    Standard,
    LayoutSelector,
    MidiControllerList
};

void configureComboBox(juce::ComboBox& comboBox, ComboBoxStyle style = ComboBoxStyle::Standard);

void configureCaptionLabel(juce::Label& label, juce::Justification justification = juce::Justification::centred, juce::Colour colour = UITheme::Colours::textSecondary);

void configureValueLabel(juce::Label& label, juce::Justification justification = juce::Justification::centred, float fontHeight = 0.0f);

void configureSectionLabel(juce::Label& label);

void configureRotarySlider(juce::Slider& slider, juce::Slider::TextEntryBoxPosition textBoxPosition, int textBoxWidth, int textBoxHeight);

void configureControlButton(juce::TextButton& button, int connectedEdges = 0);

void configureDialogCloseButton(juce::TextButton& button);

void configureSettingsButton(juce::TextButton& button);

void configureCategoryButton(juce::TextButton& button);

void configureToggleButton(juce::TextButton& button, int connectedEdges = 0);

void configureTableHeaderLabel(juce::Label& label);

void configureTableRowLabel(juce::Label& label, juce::Colour colour = UITheme::Colours::textPrimary, juce::Justification justification = juce::Justification::centredLeft);

void configureMonoTextEditor(juce::TextEditor& editor, juce::Colour textColour = UITheme::Colours::textPrimary, bool multiline = true);

void configureBorderlessTextEditor(juce::TextEditor& editor);

} // namespace UIComponentStyle
