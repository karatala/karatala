#include "Sidebar.h"

Sidebar::Sidebar() {
    this->modeButtons.add(new ModeButton(ViewMode::Zen, "Zen"));
    this->modeButtons.add(new ModeButton(ViewMode::Zoned, "Zone"));
    this->modeButtons.add(new ModeButton(ViewMode::Map, "Map"));
    this->modeButtons.add(new ModeButton(ViewMode::Raw, "Raw"));
    this->modeButtons.add(new ModeButton(ViewMode::Settings, juce::String::charToString(0x2699))); // gear

    for (auto* btn : this->modeButtons) {
        btn->onClick = [this, btn] {
            this->setActiveMode(btn->mode);
            if (this->onModeChanged)
                this->onModeChanged(btn->mode);
        };
        addAndMakeVisible(btn);
    }

    // Velocity knob
    this->velocityLabel.setText("Vel", juce::dontSendNotification);
    this->velocityLabel.setFont(juce::Font(10.0f));
    this->velocityLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(this->velocityLabel);

    this->velocityKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    this->velocityKnob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    this->velocityKnob.setRange(1.0, 127.0, 1.0);
    this->velocityKnob.setValue(100.0, juce::dontSendNotification);
    addAndMakeVisible(this->velocityKnob);

    // HiHat openness knob
    this->hihatLabel.setText("HH", juce::dontSendNotification);
    this->hihatLabel.setFont(juce::Font(10.0f));
    this->hihatLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(this->hihatLabel);

    this->hihatKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    this->hihatKnob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    this->hihatKnob.setRange(0.0, 127.0, 1.0);
    this->hihatKnob.setValue(0.0, juce::dontSendNotification);
    addAndMakeVisible(this->hihatKnob);

    // Round-robin lock knob
    this->rrLockLabel.setText("RR", juce::dontSendNotification);
    this->rrLockLabel.setFont(juce::Font(10.0f));
    this->rrLockLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(this->rrLockLabel);

    this->rrLockKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    this->rrLockKnob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    this->rrLockKnob.setRange(0.0, 8.0, 1.0);
    this->rrLockKnob.setValue(0.0, juce::dontSendNotification);
    addAndMakeVisible(this->rrLockKnob);

    addAndMakeVisible(this->inspectButton);

    this->updateButtonStates();
}

void Sidebar::setActiveMode(ViewMode mode) {
    this->activeMode = mode;
    this->updateButtonStates();
}

void Sidebar::updateButtonStates() {
    for (auto* btn : this->modeButtons) {
        btn->setToggleState(btn->mode == this->activeMode, juce::dontSendNotification);
    }
}

void Sidebar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a1a));
    // Right edge separator
    g.setColour(juce::Colour(0xff333333));
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
}

void Sidebar::resized() {
    auto area = getLocalBounds().reduced(4, 6);

    // Mode buttons at top
    for (auto* btn : this->modeButtons) {
        btn->setBounds(area.removeFromTop(buttonSize).reduced(2, 1));
        area.removeFromTop(2);
    }

    // Knobs from bottom up
    auto bottomArea = area;
    auto inspectArea = bottomArea.removeFromBottom(24);
    this->inspectButton.setBounds(inspectArea.reduced(4, 0));

    bottomArea.removeFromBottom(4);

    // RR lock
    auto rrArea = bottomArea.removeFromBottom(knobSize + 14);
    this->rrLockLabel.setBounds(rrArea.removeFromTop(14));
    this->rrLockKnob.setBounds(rrArea.withSizeKeepingCentre(knobSize, knobSize));

    bottomArea.removeFromBottom(2);

    // HiHat
    auto hhArea = bottomArea.removeFromBottom(knobSize + 14);
    this->hihatLabel.setBounds(hhArea.removeFromTop(14));
    this->hihatKnob.setBounds(hhArea.withSizeKeepingCentre(knobSize, knobSize));

    bottomArea.removeFromBottom(2);

    // Velocity
    auto velArea = bottomArea.removeFromBottom(knobSize + 14);
    this->velocityLabel.setBounds(velArea.removeFromTop(14));
    this->velocityKnob.setBounds(velArea.withSizeKeepingCentre(knobSize, knobSize));
}
