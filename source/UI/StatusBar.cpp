#include "StatusBar.h"

StatusBar::StatusBar() {
    this->statusLabel.setJustificationType(juce::Justification::centredLeft);
    this->statusLabel.setFont(juce::Font(11.0f));
    this->statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    addAndMakeVisible(this->statusLabel);

    this->infoLabel.setJustificationType(juce::Justification::centredRight);
    this->infoLabel.setFont(juce::Font(11.0f));
    this->infoLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(this->infoLabel);
}

void StatusBar::setStatusText(const juce::String& text) {
    this->statusLabel.setText(text, juce::dontSendNotification);
}

void StatusBar::setInfoText(const juce::String& text) {
    this->infoLabel.setText(text, juce::dontSendNotification);
}

void StatusBar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a1a));
    // Top border line
    g.setColour(juce::Colour(0xff333333));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void StatusBar::resized() {
    auto area = getLocalBounds().reduced(8, 0);
    this->statusLabel.setBounds(area.removeFromLeft(area.getWidth() / 2));
    this->infoLabel.setBounds(area);
}
