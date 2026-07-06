#include "Sidebar.h"

#include "UIHelpers.h"

Sidebar::Sidebar() {
    this->modeButtons.add(new ModeButton(ViewMode::Kit, "Kit"));
    this->modeButtons.add(new ModeButton(ViewMode::Instrument, "Inst"));
    this->modeButtons.add(new ModeButton(ViewMode::Zen, "Zen"));
    this->modeButtons.add(new ModeButton(ViewMode::Zone, "Zone"));
    this->modeButtons.add(new ModeButton(ViewMode::Map, "Map"));
    this->modeButtons.add(new ModeButton(ViewMode::Raw, "Raw"));
    this->modeButtons.add(new ModeButton(ViewMode::Settings, "Conf"));
    this->modeButtons.add(new ModeButton(ViewMode::Fix, "Fix"));

    for (auto* btn : this->modeButtons) {
        btn->onClick = [this, btn] {
            this->setActiveMode(btn->mode);
            if (this->onModeChanged)
                this->onModeChanged(btn->mode);
        };
        addAndMakeVisible(btn);
    }

    // HiHat openness knob
    UIHelpers::configureValueLabel(this->hihatLabel);
    addAndMakeVisible(this->hihatLabel);

    UIHelpers::configureRotarySlider(this->hihatKnob, juce::Slider::NoTextBox, 0, 0);
    this->hihatKnob.setRange(0.0, 127.0, 1.0);
    this->hihatKnob.setInteraction(0.0, 1.0);
    this->hihatKnob.setValue(0.0, juce::dontSendNotification);
    this->hihatKnob.onValueChange = [this] { this->updateKnobLabels(); };
    addAndMakeVisible(this->hihatKnob);

    // Velocity knob
    UIHelpers::configureValueLabel(this->velocityLabel);
    addAndMakeVisible(this->velocityLabel);

    UIHelpers::configureRotarySlider(this->velocityKnob, juce::Slider::NoTextBox, 0, 0);
    this->velocityKnob.setRange(1.0, 127.0, 1.0);
    this->velocityKnob.setInteraction(100.0, 1.0);
    this->velocityKnob.setValue(100.0, juce::dontSendNotification);
    this->velocityKnob.onValueChange = [this] { this->updateKnobLabels(); };
    addAndMakeVisible(this->velocityKnob);

    // Round-robin lock knob
    UIHelpers::configureValueLabel(this->rrLockLabel);
    addAndMakeVisible(this->rrLockLabel);

    UIHelpers::configureRotarySlider(this->rrLockKnob, juce::Slider::NoTextBox, 0, 0);
    this->rrLockKnob.setRange(0.0, 8.0, 1.0);
    this->rrLockKnob.setInteraction(0.0, 1.0);
    this->rrLockKnob.setValue(0.0, juce::dontSendNotification);
    this->rrLockKnob.onValueChange = [this] { this->updateKnobLabels(); };
    addAndMakeVisible(this->rrLockKnob);

#if JUCE_DEBUG
    addAndMakeVisible(this->inspectButton);
#endif

    this->updateKnobLabels();

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
    g.fillAll(UITheme::Colours::bgPanel);
    // Right edge separator
    g.setColour(UITheme::Colours::borderSubtle);
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
}

void Sidebar::resized() {
    auto area = getLocalBounds().reduced(4, 6);

    // Mode buttons at top
    for (auto* btn : this->modeButtons) {
        btn->setBounds(area.removeFromTop(buttonSize).reduced(2, 1));
        area.removeFromTop(2);
    }

    // Knobs from bottom up: RR, VL, HH (so top-down reads HH, VL, RR)
    auto bottomArea = area;

    // RR lock (bottom)
    auto rrArea = bottomArea.removeFromBottom(knobSize + 14);
    this->rrLockLabel.setBounds(rrArea.removeFromTop(14));
    this->rrLockKnob.setBounds(rrArea.withSizeKeepingCentre(knobSize, knobSize));

    bottomArea.removeFromBottom(2);

    // Velocity
    auto velArea = bottomArea.removeFromBottom(knobSize + 14);
    this->velocityLabel.setBounds(velArea.removeFromTop(14));
    this->velocityKnob.setBounds(velArea.withSizeKeepingCentre(knobSize, knobSize));

    bottomArea.removeFromBottom(2);

    // HiHat (top of knob group)
    auto hhArea = bottomArea.removeFromBottom(knobSize + 14);
    this->hihatLabel.setBounds(hhArea.removeFromTop(14));
    this->hihatKnob.setBounds(hhArea.withSizeKeepingCentre(knobSize, knobSize));

#if JUCE_DEBUG
    bottomArea.removeFromBottom(4);
    auto inspectArea = bottomArea.removeFromBottom(24);
    this->inspectButton.setBounds(inspectArea.reduced(4, 0));
#endif
}

void Sidebar::updateKnobLabels() {
    int hhVal = static_cast<int>(this->hihatKnob.getValue());
    this->hihatLabel.setText("HH " + juce::String(hhVal), juce::dontSendNotification);

    int vlVal = static_cast<int>(this->velocityKnob.getValue());
    this->velocityLabel.setText("VL " + juce::String(vlVal), juce::dontSendNotification);

    int rrVal = static_cast<int>(this->rrLockKnob.getValue());
    this->rrLockLabel.setText("RR " + (rrVal == 0 ? juce::String("*") : juce::String(rrVal)), juce::dontSendNotification);
}
