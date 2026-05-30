#include "MidiNoteAssignmentControl.h"

MidiNoteAssignmentControl::MidiNoteAssignmentControl() {
    this->noteButton1.setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    this->noteButton2.setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    addAndMakeVisible(this->noteButton1);
    addAndMakeVisible(this->noteButton2);

    this->learnButton1.setColour(juce::TextButton::textColourOffId, UITheme::Colours::midiLearnIdle);
    this->learnButton2.setColour(juce::TextButton::textColourOffId, UITheme::Colours::midiLearnIdle);
    addAndMakeVisible(this->learnButton1);
    addAndMakeVisible(this->learnButton2);

    this->noteButton1.onClick = [this] { this->showMidiNotePicker(0); };
    this->noteButton2.onClick = [this] { this->showMidiNotePicker(1); };
    this->learnButton1.onClick = [this] {
        if (this->onLearnButtonClicked)
            this->onLearnButtonClicked(0);
    };
    this->learnButton2.onClick = [this] {
        if (this->onLearnButtonClicked)
            this->onLearnButtonClicked(1);
    };

    this->updateNoteButtons();
    this->updateLearnButtons();
}

void MidiNoteAssignmentControl::resized() {
    auto area = getLocalBounds();
    const int verticalInset = juce::jmax(0, this->layoutMetrics.verticalInset);
    const int noteButtonHeight = juce::jmax(0, area.getHeight() - verticalInset * 2);
    const int learnButtonHeight = juce::jmin(this->layoutMetrics.learnButtonSize, juce::jmax(0, area.getHeight() - verticalInset * 2));

    this->noteButton1.setBounds(area.removeFromLeft(this->layoutMetrics.noteButtonWidth).reduced(0, verticalInset));
    area.removeFromLeft(this->layoutMetrics.noteLearnGap);
    this->learnButton1.setBounds(area.removeFromLeft(this->layoutMetrics.learnButtonSize)
                                     .withSizeKeepingCentre(this->layoutMetrics.learnButtonSize, learnButtonHeight));

    area.removeFromLeft(this->layoutMetrics.pairGap);

    this->noteButton2.setBounds(area.removeFromLeft(this->layoutMetrics.noteButtonWidth).reduced(0, verticalInset));
    area.removeFromLeft(this->layoutMetrics.noteLearnGap);
    this->learnButton2.setBounds(area.removeFromLeft(this->layoutMetrics.learnButtonSize)
                                     .withSizeKeepingCentre(this->layoutMetrics.learnButtonSize, learnButtonHeight));

    juce::ignoreUnused(noteButtonHeight);
}

void MidiNoteAssignmentControl::setNotes(int firstNote, int secondNote) {
    this->noteValues[0] = firstNote;
    this->noteValues[1] = secondNote;
    this->updateNoteButtons();
}

void MidiNoteAssignmentControl::setLearningSlot(int slotIdx) {
    this->learningSlot = (slotIdx == 0 || slotIdx == 1) ? slotIdx : -1;
    this->updateLearnButtons();
}

void MidiNoteAssignmentControl::setLayoutMetrics(LayoutMetrics metrics) {
    this->layoutMetrics = metrics;
    this->resized();
}

int MidiNoteAssignmentControl::getPreferredWidth() const {
    return this->layoutMetrics.noteButtonWidth * 2
         + this->layoutMetrics.learnButtonSize * 2
         + this->layoutMetrics.noteLearnGap * 2
         + this->layoutMetrics.pairGap;
}

void MidiNoteAssignmentControl::updateNoteButtons() {
    this->noteButton1.setButtonText(MidiNotePicker::noteToButtonText(this->noteValues[0]));
    this->noteButton2.setButtonText(MidiNotePicker::noteToButtonText(this->noteValues[1]));
}

void MidiNoteAssignmentControl::updateLearnButtons() {
    this->learnButton1.setColour(juce::TextButton::textColourOffId,
                                 this->learningSlot == 0 ? UITheme::Colours::midiLearnActive : UITheme::Colours::midiLearnIdle);
    this->learnButton2.setColour(juce::TextButton::textColourOffId,
                                 this->learningSlot == 1 ? UITheme::Colours::midiLearnActive : UITheme::Colours::midiLearnIdle);
    this->learnButton1.repaint();
    this->learnButton2.repaint();
}

void MidiNoteAssignmentControl::showMidiNotePicker(int slotIdx) {
    auto picker = std::make_unique<MidiNotePicker>();
    picker->setCurrentNote(this->noteValues[static_cast<size_t>(slotIdx)]);

    picker->onNoteSelected = [this, slotIdx](int note) {
        this->noteValues[static_cast<size_t>(slotIdx)] = note;
        this->updateNoteButtons();

        if (this->onNoteSelected)
            this->onNoteSelected(slotIdx, note);

        if (auto* modal = juce::Component::getCurrentlyModalComponent())
            modal->exitModalState(0);
    };

    auto& button = (slotIdx == 0) ? this->noteButton1 : this->noteButton2;
    this->activeMidiPicker = picker.get();
    juce::CallOutBox::launchAsynchronously(std::move(picker), button.getScreenBounds(), nullptr);
}