#include "MidiMapTable.h"

static const char* NOTE_NAMES[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static void setComboFont(juce::ComboBox& combo, float size) {
    combo.setJustificationType(juce::Justification::centredLeft);
    if (auto* label = dynamic_cast<juce::Label*>(combo.getChildComponent(0)))
        label->setFont(juce::Font(juce::FontOptions(size)));
}

MidiMapTable::MidiMapTable() {
    this->viewport.setViewedComponent(&this->contentComponent, false);
    this->viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(this->viewport);
}

juce::String MidiMapTable::midiNoteToString(int note) {
    if (note < 0 || note > 127)
        return "-- / --";
    int octave = (note / 12) - 2;
    int noteIdx = note % 12;
    return juce::String(NOTE_NAMES[noteIdx]) + juce::String(octave) + " / " + juce::String(note);
}

void MidiMapTable::populateNoteComboBox(juce::ComboBox& combo) {
    combo.clear(juce::dontSendNotification);
    combo.addItem("-- / --", 1); // id=1 means unassigned
    for (int note = 0; note <= 127; ++note) {
        combo.addItem(midiNoteToString(note), note + 2); // id = note+2 (since 1 = unassigned)
    }
}

void MidiMapTable::populate(KitLayout* layout) {
    this->layoutPtr = layout;
    this->stopMidiLearn();
    this->buildRows();
    this->resized();
}

void MidiMapTable::buildRows() {
    this->rows.clear();
    this->contentComponent.removeAllChildren();

    if (!this->layoutPtr)
        return;

    for (auto& slot : this->layoutPtr->instrumentSlots) {
        bool firstRow = true;

        // Articulation rows
        for (auto& art : slot.articulations) {
            auto row = std::make_unique<MapRow>();
            row->instrumentId = slot.id;
            row->articulationCode = art.codeRaw;
            row->isController = false;
            row->notesPtr = &art.notes;
            row->showInstrumentLabel = firstRow;
            firstRow = false;

            row->instrumentLabel.setText(slot.display.isNotEmpty() ? slot.display : slot.id, juce::dontSendNotification);
            row->instrumentLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
            row->instrumentLabel.setJustificationType(juce::Justification::centredLeft);
            this->contentComponent.addAndMakeVisible(row->instrumentLabel);
            row->instrumentLabel.setVisible(row->showInstrumentLabel);

            row->articulationLabel.setText(art.codeRaw, juce::dontSendNotification);
            row->articulationLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
            row->articulationLabel.setJustificationType(juce::Justification::centredLeft);
            this->contentComponent.addAndMakeVisible(row->articulationLabel);

            populateNoteComboBox(row->noteCombo1);
            populateNoteComboBox(row->noteCombo2);
            this->contentComponent.addAndMakeVisible(row->noteCombo1);
            this->contentComponent.addAndMakeVisible(row->noteCombo2);
            setComboFont(row->noteCombo1, 10.0f);
            setComboFont(row->noteCombo2, 10.0f);

            int rowIdx = static_cast<int>(this->rows.size());
            row->noteCombo1.onChange = [this, rowIdx] {
                this->applyComboToNotes(*this->rows[static_cast<size_t>(rowIdx)]);
            };
            row->noteCombo2.onChange = [this, rowIdx] {
                this->applyComboToNotes(*this->rows[static_cast<size_t>(rowIdx)]);
            };

            row->learnButton1.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
            row->learnButton2.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
            this->contentComponent.addAndMakeVisible(row->learnButton1);
            this->contentComponent.addAndMakeVisible(row->learnButton2);

            row->learnButton1.onClick = [this, rowIdx] { this->startMidiLearn(rowIdx, 0); };
            row->learnButton2.onClick = [this, rowIdx] { this->startMidiLearn(rowIdx, 1); };

            this->syncComboFromNotes(*row);
            this->rows.push_back(std::move(row));
        }

        // Controller rows
        for (auto& cc : slot.controllers) {
            auto row = std::make_unique<MapRow>();
            row->instrumentId = slot.id;
            row->articulationCode = "CC: " + cc.codeRaw;
            row->isController = true;
            row->controllerNumber = cc.number;
            row->notesPtr = &cc.notes;
            row->showInstrumentLabel = firstRow;
            firstRow = false;

            row->instrumentLabel.setText(slot.display.isNotEmpty() ? slot.display : slot.id, juce::dontSendNotification);
            row->instrumentLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
            row->instrumentLabel.setJustificationType(juce::Justification::centredLeft);
            this->contentComponent.addAndMakeVisible(row->instrumentLabel);
            row->instrumentLabel.setVisible(row->showInstrumentLabel);

            row->articulationLabel.setText(row->articulationCode, juce::dontSendNotification);
            row->articulationLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
            row->articulationLabel.setColour(juce::Label::textColourId, juce::Colour(0xff88aacc));
            row->articulationLabel.setJustificationType(juce::Justification::centredLeft);
            this->contentComponent.addAndMakeVisible(row->articulationLabel);

            populateNoteComboBox(row->noteCombo1);
            populateNoteComboBox(row->noteCombo2);
            this->contentComponent.addAndMakeVisible(row->noteCombo1);
            this->contentComponent.addAndMakeVisible(row->noteCombo2);
            setComboFont(row->noteCombo1, 10.0f);
            setComboFont(row->noteCombo2, 10.0f);

            int rowIdx = static_cast<int>(this->rows.size());
            row->noteCombo1.onChange = [this, rowIdx] {
                this->applyComboToNotes(*this->rows[static_cast<size_t>(rowIdx)]);
            };
            row->noteCombo2.onChange = [this, rowIdx] {
                this->applyComboToNotes(*this->rows[static_cast<size_t>(rowIdx)]);
            };

            row->learnButton1.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
            row->learnButton2.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
            this->contentComponent.addAndMakeVisible(row->learnButton1);
            this->contentComponent.addAndMakeVisible(row->learnButton2);

            row->learnButton1.onClick = [this, rowIdx] { this->startMidiLearn(rowIdx, 0); };
            row->learnButton2.onClick = [this, rowIdx] { this->startMidiLearn(rowIdx, 1); };

            this->syncComboFromNotes(*row);
            this->rows.push_back(std::move(row));
        }
    }
}

void MidiMapTable::syncComboFromNotes(MapRow& row) {
    if (!row.notesPtr)
        return;
    auto& notes = *row.notesPtr;

    // First combo
    if (!notes.empty()) {
        row.noteCombo1.setSelectedId(static_cast<int>(notes[0]) + 2, juce::dontSendNotification);
    } else {
        row.noteCombo1.setSelectedId(1, juce::dontSendNotification); // unassigned
    }

    // Second combo
    if (notes.size() >= 2) {
        row.noteCombo2.setSelectedId(static_cast<int>(notes[1]) + 2, juce::dontSendNotification);
    } else {
        row.noteCombo2.setSelectedId(1, juce::dontSendNotification); // unassigned
    }
}

void MidiMapTable::applyComboToNotes(MapRow& row) {
    if (!row.notesPtr)
        return;

    auto& notes = *row.notesPtr;
    notes.clear();

    int id1 = row.noteCombo1.getSelectedId();
    int id2 = row.noteCombo2.getSelectedId();

    if (id1 > 1)
        notes.push_back(static_cast<uint8_t>(id1 - 2));
    if (id2 > 1)
        notes.push_back(static_cast<uint8_t>(id2 - 2));

    if (this->onMappingChanged)
        this->onMappingChanged();
}

void MidiMapTable::startMidiLearn(int rowIdx, int slotIdx) {
    // Toggle off if already learning this slot
    if (this->activeLearningRow == rowIdx && this->activeLearningSlot == slotIdx) {
        this->stopMidiLearn();
        return;
    }

    // Reset previous learn button
    this->stopMidiLearn();

    this->activeLearningRow = rowIdx;
    this->activeLearningSlot = slotIdx;

    // Highlight active learn button
    auto& row = *this->rows[static_cast<size_t>(rowIdx)];
    auto& btn = (slotIdx == 0) ? row.learnButton1 : row.learnButton2;
    btn.setColour(juce::TextButton::textColourOffId, juce::Colours::yellow);
    btn.repaint();
}

void MidiMapTable::stopMidiLearn() {
    if (this->activeLearningRow >= 0 && this->activeLearningRow < static_cast<int>(this->rows.size())) {
        auto& row = *this->rows[static_cast<size_t>(this->activeLearningRow)];
        row.learnButton1.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
        row.learnButton2.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
        row.learnButton1.repaint();
        row.learnButton2.repaint();
    }
    this->activeLearningRow = -1;
    this->activeLearningSlot = -1;
}

void MidiMapTable::midiLearnNoteReceived(int midiNote) {
    if (this->activeLearningRow < 0 || this->activeLearningRow >= static_cast<int>(this->rows.size()))
        return;

    auto& row = *this->rows[static_cast<size_t>(this->activeLearningRow)];
    auto& combo = (this->activeLearningSlot == 0) ? row.noteCombo1 : row.noteCombo2;
    combo.setSelectedId(midiNote + 2, juce::sendNotificationSync);

    this->stopMidiLearn();
}

void MidiMapTable::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1e1e1e));
}

void MidiMapTable::resized() {
    auto area = getLocalBounds();

    this->viewport.setBounds(area);

    const int instColWidth = 90;
    const int artColWidth = 100;
    const int comboWidth = 84;
    const int learnBtnWidth = 20;
    const int gapSmall = 3;
    // Single column width for one instrument group
    const int singleColWidth = instColWidth + artColWidth + (comboWidth + learnBtnWidth + gapSmall) * 2 + gapSmall * 3 + 8;
    const int columnGap = 12;

    // Determine if we can fit 2 columns
    int availWidth = this->viewport.getWidth() - (this->viewport.isVerticalScrollBarShown() ? 14 : 0);
    bool twoColumns = availWidth >= singleColWidth * 2 + columnGap;

    // Split rows into columns by instrument groups
    // Find split point: half the rows, but on an instrument boundary
    int totalRows = static_cast<int>(this->rows.size());
    int halfRows = totalRows / 2;
    int splitRow = halfRows;

    if (twoColumns && totalRows > 2) {
        // Find nearest instrument boundary at or after halfRows
        for (int idx = halfRows; idx < totalRows; ++idx) {
            if (this->rows[static_cast<size_t>(idx)]->showInstrumentLabel) {
                splitRow = idx;
                break;
            }
        }
    }

    int col1Count = twoColumns ? splitRow : totalRows;
    int col2Count = twoColumns ? (totalRows - splitRow) : 0;
    int maxColRows = juce::jmax(col1Count, col2Count);

    int contentHeight = headerHeight + maxColRows * rowHeight + 8;
    int contentWidth = twoColumns ? (singleColWidth * 2 + columnGap) : singleColWidth;
    contentWidth = juce::jmax(contentWidth, availWidth);

    this->contentComponent.setBounds(0, 0, contentWidth, contentHeight);

    auto layoutColumn = [&](int startIdx, int endIdx, int xOffset) {
        int yPos = headerHeight;
        for (int idx = startIdx; idx < endIdx; ++idx) {
            auto& row = *this->rows[static_cast<size_t>(idx)];
            int xPos = xOffset;

            row.instrumentLabel.setBounds(xPos, yPos, instColWidth - gapSmall, rowHeight);
            xPos += instColWidth;

            row.articulationLabel.setBounds(xPos, yPos, artColWidth - gapSmall, rowHeight);
            xPos += artColWidth;

            row.noteCombo1.setBounds(xPos, yPos + 2, comboWidth, rowHeight - 4);
            xPos += comboWidth + gapSmall;

            row.learnButton1.setBounds(xPos, yPos + 2, learnBtnWidth, rowHeight - 4);
            xPos += learnBtnWidth + gapSmall;

            row.noteCombo2.setBounds(xPos, yPos + 2, comboWidth, rowHeight - 4);
            xPos += comboWidth + gapSmall;

            row.learnButton2.setBounds(xPos, yPos + 2, learnBtnWidth, rowHeight - 4);

            yPos += rowHeight;
        }
    };

    layoutColumn(0, col1Count, 4);
    if (twoColumns && col2Count > 0) {
        layoutColumn(splitRow, totalRows, singleColWidth + columnGap);
    }
}
