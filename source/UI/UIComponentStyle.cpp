#include "UIComponentStyle.h"

namespace UIComponentStyle {

void configureComboBox(juce::ComboBox& comboBox, ComboBoxStyle style) {
    comboBox.getProperties().set("squareCorners", true);
    comboBox.getProperties().set("layoutSelector", style == ComboBoxStyle::LayoutSelector);
    comboBox.getProperties().set("midiControllerList", style == ComboBoxStyle::MidiControllerList);
    comboBox.setColour(juce::ComboBox::backgroundColourId, UITheme::Colours::bgControl);
    comboBox.setColour(juce::ComboBox::textColourId, UITheme::Colours::textPrimary);
    comboBox.setColour(juce::ComboBox::arrowColourId, UITheme::Colours::textSecondary);
    comboBox.setColour(juce::ComboBox::outlineColourId, UITheme::Colours::borderSubtle);
}

void configureCaptionLabel(juce::Label& label, juce::Justification justification, juce::Colour colour) {
    label.setFont(UITheme::Fonts::small());
    label.setJustificationType(justification);
    label.setColour(juce::Label::textColourId, colour);
}

void configureValueLabel(juce::Label& label, juce::Justification justification, float fontHeight) {
    auto font = UITheme::Fonts::label();
    if (fontHeight > 0.0f) font = font.withHeight(fontHeight);
    label.setFont(font);
    label.setJustificationType(justification);
    label.setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
}

void configureSectionLabel(juce::Label& label) {
    label.setFont(UITheme::Fonts::sectionTitle());
    label.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
}

void configureRotarySlider(juce::Slider& slider, juce::Slider::TextEntryBoxPosition textBoxPosition, int textBoxWidth, int textBoxHeight) {
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(textBoxPosition, false, textBoxWidth, textBoxHeight);
    slider.setColour(juce::Slider::rotarySliderFillColourId, UITheme::Colours::accent);
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, UITheme::Colours::borderMid);
    slider.setColour(juce::Slider::textBoxTextColourId, UITheme::Colours::textSecondary);
    slider.setColour(juce::Slider::textBoxOutlineColourId, UITheme::Colours::transparent);
}

void configureControlButton(juce::TextButton& button, int connectedEdges) {
    button.setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    button.setConnectedEdges(connectedEdges);
}

void configureDialogCloseButton(juce::TextButton& button) {
    configureControlButton(button);
    button.setButtonText(UITheme::Symbols::times);
    button.getProperties().set("dialogCloseButton", true);
    button.setTooltip("Close");
}

void configureSettingsButton(juce::TextButton& button) {
    configureControlButton(button);
    button.setButtonText(juce::String());
    button.getProperties().set("settingsButton", true);
}

void configureCategoryButton(juce::TextButton& button) {
    button.setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    button.setColour(juce::TextButton::textColourOffId, UITheme::Colours::textSecondary);
    button.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
}

void configureToggleButton(juce::TextButton& button, int connectedEdges) {
    button.setClickingTogglesState(true);
    button.setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    button.setColour(juce::TextButton::buttonOnColourId, UITheme::Colours::accentWarning);
    button.setColour(juce::TextButton::textColourOffId, UITheme::Colours::textMuted);
    button.setColour(juce::TextButton::textColourOnId, UITheme::Colours::textPrimary);
    button.setConnectedEdges(connectedEdges);
}

void configureTableHeaderLabel(juce::Label& label) {
    label.setFont(UITheme::Fonts::small().boldened());
    label.setColour(juce::Label::textColourId, UITheme::Colours::textMuted);
    label.setJustificationType(juce::Justification::centredLeft);
}

void configureTableRowLabel(juce::Label& label, juce::Colour colour, juce::Justification justification) {
    label.setFont(UITheme::Fonts::normal());
    label.setColour(juce::Label::textColourId, colour);
    label.setJustificationType(justification);
}

void configureMonoTextEditor(juce::TextEditor& editor, juce::Colour textColour, bool multiline) {
    editor.setMultiLine(multiline);
    editor.setScrollbarsShown(true);
    editor.setFont(UITheme::Fonts::mono());
    editor.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgEditor);
    editor.setColour(juce::TextEditor::textColourId, textColour);
}

void configureBorderlessTextEditor(juce::TextEditor& editor) {
    editor.setColour(juce::TextEditor::outlineColourId, UITheme::Colours::transparent);
    editor.setColour(juce::TextEditor::focusedOutlineColourId, UITheme::Colours::transparent);
    editor.setColour(juce::TextEditor::shadowColourId, UITheme::Colours::transparent);
}

} // namespace UIComponentStyle
