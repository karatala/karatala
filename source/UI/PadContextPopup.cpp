#include "PadContextPopup.h"

PadContextPopup::PadContextPopup(DrumKitEngine& engine, const KitLayout::InstrumentSlot& slot, int artIdx)
    : engine(engine)
    , slotId(slot.id) {
    // Resolve articulation info
    if (artIdx >= 0 && artIdx < static_cast<int>(slot.articulations.size())) {
        const auto& art = slot.articulations[static_cast<size_t>(artIdx)];
        this->artName = art.codeRaw.upToFirstOccurrenceOf("[", false, false);

        juce::StringArray noteStrings;
        for (auto note : art.notes)
            noteStrings.add(juce::String(static_cast<int>(note)));
        this->midiNotesText = noteStrings.joinIntoString(", ");
    } else {
        this->artName = "(default)";
    }

    // Slot label
    this->slotLabel.setText(slot.display.isNotEmpty() ? slot.display : slot.id, juce::dontSendNotification);
    this->slotLabel.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    this->slotLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(this->slotLabel);

    // Art label
    this->artLabel.setText("Art: " + this->artName, juce::dontSendNotification);
    this->artLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    this->artLabel.setColour(juce::Label::textColourId, juce::Colour(0xff99bbdd));
    addAndMakeVisible(this->artLabel);

    // MIDI notes label
    this->midiLabel.setText("MIDI: " + this->midiNotesText, juce::dontSendNotification);
    this->midiLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    this->midiLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(this->midiLabel);

    // Preview button
    this->previewButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a5a3a));
    this->previewButton.onClick = [this] {
        this->engine.playPreviewNote(this->slotId, this->artName, 100);
    };
    addAndMakeVisible(this->previewButton);

    // Close button
    this->closeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444444));
    this->closeButton.onClick = [this] {
        if (this->onDismiss)
            this->onDismiss();
    };
    addAndMakeVisible(this->closeButton);

    this->setSize(220, 130);
}

void PadContextPopup::paint(juce::Graphics& g) {
    g.setColour(juce::Colour(0xff222222));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
    g.setColour(juce::Colour(0xff555555));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void PadContextPopup::resized() {
    auto area = getLocalBounds().reduced(8);
    this->slotLabel.setBounds(area.removeFromTop(20));
    this->artLabel.setBounds(area.removeFromTop(18));
    this->midiLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(4);
    auto btnRow = area.removeFromTop(24);
    this->previewButton.setBounds(btnRow.removeFromLeft(100).reduced(0, 2));
    btnRow.removeFromLeft(4);
    this->closeButton.setBounds(btnRow.removeFromLeft(60).reduced(0, 2));
}
