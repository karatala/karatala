#include "StatusBar.h"
#include "UITheme.h"

StatusBar::StatusBar() {
    this->statusLabel.setJustificationType(juce::Justification::centredLeft);
    this->statusLabel.setFont(UITheme::Fonts::table().withHeight(11.0f));
    this->statusLabel.setColour(juce::Label::textColourId, UITheme::Colours::textSuccess);
    addAndMakeVisible(this->statusLabel);

    this->infoLabel.setJustificationType(juce::Justification::centredRight);
    this->infoLabel.setFont(UITheme::Fonts::table().withHeight(11.0f));
    this->infoLabel.setColour(juce::Label::textColourId, UITheme::Colours::textInfo);
    addAndMakeVisible(this->infoLabel);
}

void StatusBar::setStatusText(const juce::String& text) {
    this->statusLabel.setText(text, juce::dontSendNotification);
}

void StatusBar::setInfoText(const juce::String& text) {
    this->infoLabel.setText(text, juce::dontSendNotification);
}

void StatusBar::paint(juce::Graphics& g) {
    g.fillAll(UITheme::Colours::bgPanel);
    // Top border line
    g.setColour(UITheme::Colours::borderSubtle);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void StatusBar::resized() {
    auto area = getLocalBounds().reduced(8, 0);
    this->statusLabel.setBounds(area.removeFromLeft(area.getWidth() / 2));
    this->infoLabel.setBounds(area);
}
