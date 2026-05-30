#include "HorizontalKnobBrick.h"

HorizontalKnobBrick::HorizontalKnobBrick(const juce::String& name, double minVal, double maxVal, double interval, double initialValue, std::function<juce::String(double)> textFromValueFn) {
    this->titleLabel.setText(name, juce::dontSendNotification);
    this->titleLabel.setFont(juce::Font(juce::FontOptions(9.0f)));
    this->titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff777777));
    this->titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(this->titleLabel);

    this->pot.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    this->pot.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    this->pot.setRange(minVal, maxVal, interval);
    this->pot.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::red);
    this->pot.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff444444));

    if (textFromValueFn)
        this->pot.textFromValueFunction = std::move(textFromValueFn);

    this->pot.setValue(initialValue, juce::dontSendNotification);
    addAndMakeVisible(this->pot);

    this->valueLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    this->valueLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcccccc));
    this->valueLabel.setJustificationType(juce::Justification::centredLeft);
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

void HorizontalKnobBrick::updateValueText() {
    this->valueLabel.setText(this->pot.getTextFromValue(this->pot.getValue()), juce::dontSendNotification);
}

void HorizontalKnobBrick::resized() {
    auto area = getLocalBounds();
    const int potSize = juce::jmin(area.getHeight(), area.getWidth() * 4 / 5);
    auto potArea = area.removeFromRight(potSize);
    this->pot.setBounds(potArea);
    auto leftArea = area;
    this->titleLabel.setBounds(leftArea.removeFromTop(leftArea.getHeight() / 3));
    this->valueLabel.setBounds(leftArea);
}
