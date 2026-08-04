#include "StatusBar.h"
#include "UIComponentStyle.h"
#include "UITheme.h"

StatusBar::StatusBar() {
    this->statusLabel.setJustificationType(juce::Justification::centredLeft);
    this->statusLabel.setFont(UITheme::Fonts::tableSmall());
    this->statusLabel.setColour(juce::Label::textColourId, UITheme::Colours::textSuccess);
    addAndMakeVisible(this->statusLabel);

    this->infoLabel.setJustificationType(juce::Justification::centredRight);
    this->infoLabel.setFont(UITheme::Fonts::tableSmall());
    this->infoLabel.setColour(juce::Label::textColourId, UITheme::Colours::textInfo);
    addAndMakeVisible(this->infoLabel);

    UIComponentStyle::configureToggleButton(this->uiButton);
    this->uiButton.setClickingTogglesState(false);
    this->uiButton.setTooltip("Open UI theme playground");
    this->uiButton.onClick = [this] {
        if (this->onUiRequested) this->onUiRequested();
    };
    this->addAndMakeVisible(this->uiButton);

#if JUCE_DEBUG
    UIComponentStyle::configureControlButton(this->inspectButton);
    this->inspectButton.setTooltip("Open component inspector");
    this->inspectButton.onClick = [this] {
        if (this->onInspectRequested) this->onInspectRequested();
    };
    this->addAndMakeVisible(this->inspectButton);
#endif

    UIComponentStyle::configureToggleButton(this->logsButton);
    this->logsButton.setClickingTogglesState(false);
    this->logsButton.setTooltip("Open logs and troubleshooting");
    this->logsButton.onClick = [this] {
        if (this->onLogsRequested) this->onLogsRequested();
    };
    this->addAndMakeVisible(this->logsButton);
}

void StatusBar::setStatusText(const juce::String& text) {
    this->statusLabel.setText(text, juce::dontSendNotification);
}

void StatusBar::setInfoText(const juce::String& text) {
    this->infoLabel.setText(text, juce::dontSendNotification);
}

void StatusBar::setUiActive(bool active) {
    this->uiButton.setToggleState(active, juce::dontSendNotification);
}

void StatusBar::setLogsActive(bool active) {
    this->logsButton.setToggleState(active, juce::dontSendNotification);
}

void StatusBar::paint(juce::Graphics& g) {
    g.fillAll(UITheme::Colours::bgPanel);
    // Top border line
    g.setColour(UITheme::Colours::borderSubtle);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void StatusBar::resized() {
    auto area = getLocalBounds().reduced(8, 0);
    this->logsButton.setBounds(area.removeFromRight(42).reduced(0, 2));
    area.removeFromRight(2);
#if JUCE_DEBUG
    this->inspectButton.setBounds(area.removeFromRight(22).reduced(1, 2));
    area.removeFromRight(2);
#endif
    this->uiButton.setBounds(area.removeFromRight(28).reduced(0, 2));
    area.removeFromRight(6);
    this->infoLabel.setBounds(area.removeFromRight(area.getWidth() / 2));
    this->statusLabel.setBounds(area);
}
