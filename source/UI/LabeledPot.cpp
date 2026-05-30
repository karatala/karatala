#include "LabeledPot.h"
#include "UITheme.h"

LabeledPot::LabeledPot(const juce::String& name, double minVal, double maxVal, double interval, double initialValue, std::function<juce::String(double)> textFromValueFn) {
    this->titleLabel.setText(name, juce::dontSendNotification);
    this->titleLabel.setFont(UITheme::Fonts::small());
    this->titleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textSubtle);
    this->titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(this->titleLabel);

    this->pot.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    this->pot.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    this->pot.setRange(minVal, maxVal, interval);
    this->pot.setColour(juce::Slider::rotarySliderFillColourId, UITheme::Colours::accent);
    this->pot.setColour(juce::Slider::rotarySliderOutlineColourId, UITheme::Colours::borderMid);

    if (textFromValueFn)
        this->pot.textFromValueFunction = std::move(textFromValueFn);

    this->pot.setValue(initialValue, juce::dontSendNotification);
    addAndMakeVisible(this->pot);

    this->valueLabel.setFont(UITheme::Fonts::label());
    this->valueLabel.setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
    this->valueLabel.setJustificationType(juce::Justification::centred);
    this->valueLabel.setEditable(true);
    addAndMakeVisible(this->valueLabel);
    this->updateValueText();

    this->valueLabel.onTextChange = [this] {
        double newVal = this->valueLabel.getText().getDoubleValue();
        this->pot.setValue(newVal, juce::sendNotificationSync);
    };

    this->pot.onValueChange = [this] {
        this->updateValueText();
        if (this->onValueChange) this->onValueChange();
    };
    this->pot.onDragEnd = [this] {
        if (this->onDragEnd) this->onDragEnd();
    };
}

void LabeledPot::updateValueText() {
    this->valueLabel.setText(this->pot.getTextFromValue(this->pot.getValue()), juce::dontSendNotification);
}

void LabeledPot::resized() {
    auto area = getLocalBounds();
    const int titleH = 12;
    const int valueH = 14;
    this->titleLabel.setBounds(area.removeFromTop(titleH));
    this->valueLabel.setBounds(area.removeFromBottom(valueH));
    // Remaining center area for the knob (square, centered)
    const int knobSize = juce::jmin(area.getWidth(), area.getHeight());
    this->pot.setBounds(area.withSizeKeepingCentre(knobSize, knobSize));
}
