#include "MidiMapTable.h"

MidiMapTable::MidiMapTable() {
    this->viewport.setViewedComponent(&this->contentComponent, false);
    this->viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(this->viewport);
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

    auto setupRow = [this](std::unique_ptr<MapRow>& row, int rowIdx) {
        this->contentComponent.addAndMakeVisible(row->instrumentLabel);
        row->instrumentLabel.setVisible(row->showInstrumentLabel);

        this->contentComponent.addAndMakeVisible(row->articulationLabel);

        row->noteButton1.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
        row->noteButton2.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
        this->contentComponent.addAndMakeVisible(row->noteButton1);
        this->contentComponent.addAndMakeVisible(row->noteButton2);

        row->noteButton1.onClick = [this, rowIdx] { this->showMidiNotePicker(rowIdx, 0); };
        row->noteButton2.onClick = [this, rowIdx] { this->showMidiNotePicker(rowIdx, 1); };

        row->learnButton1.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
        row->learnButton2.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
        this->contentComponent.addAndMakeVisible(row->learnButton1);
        this->contentComponent.addAndMakeVisible(row->learnButton2);

        row->learnButton1.onClick = [this, rowIdx] { this->startMidiLearn(rowIdx, 0); };
        row->learnButton2.onClick = [this, rowIdx] { this->startMidiLearn(rowIdx, 1); };

        this->syncNoteButtons(*row);
    };

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

            row->articulationLabel.setText(art.codeRaw, juce::dontSendNotification);
            row->articulationLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
            row->articulationLabel.setJustificationType(juce::Justification::centredLeft);

            int rowIdx = static_cast<int>(this->rows.size());
            setupRow(row, rowIdx);
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

            row->articulationLabel.setText(row->articulationCode, juce::dontSendNotification);
            row->articulationLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
            row->articulationLabel.setColour(juce::Label::textColourId, juce::Colour(0xff88aacc));
            row->articulationLabel.setJustificationType(juce::Justification::centredLeft);

            int rowIdx = static_cast<int>(this->rows.size());
            setupRow(row, rowIdx);
            this->rows.push_back(std::move(row));
        }
    }
}

void MidiMapTable::syncNoteButtons(MapRow& row) {
    if (!row.notesPtr) return;
    auto& notes = *row.notesPtr;
    const int note1 = !notes.empty()     ? static_cast<int>(notes[0]) : -1;
    const int note2 = notes.size() >= 2  ? static_cast<int>(notes[1]) : -1;
    row.noteButton1.setButtonText(MidiNotePicker::noteToButtonText(note1));
    row.noteButton2.setButtonText(MidiNotePicker::noteToButtonText(note2));
}

void MidiMapTable::applyNoteToSlot(MapRow& row, int slotIdx, int note) {
    if (!row.notesPtr) return;
    auto& notes = *row.notesPtr;
    // Ensure the vector has at least (slotIdx+1) entries
    while (static_cast<int>(notes.size()) <= slotIdx)
        notes.push_back(0);
    if (note >= 0 && note <= 127)
        notes[static_cast<size_t>(slotIdx)] = static_cast<uint8_t>(note);
    else
        notes.erase(notes.begin() + slotIdx);
    this->syncNoteButtons(row);
    if (this->onMappingChanged)
        this->onMappingChanged();
}

void MidiMapTable::showMidiNotePicker(int rowIdx, int slotIdx) {
    if (rowIdx < 0 || rowIdx >= static_cast<int>(this->rows.size()))
        return;

    auto& row = *this->rows[static_cast<size_t>(rowIdx)];
    auto picker = std::make_unique<MidiNotePicker>();

    // Pre-select current note
    if (row.notesPtr && static_cast<int>(row.notesPtr->size()) > slotIdx)
        picker->setCurrentNote(static_cast<int>((*row.notesPtr)[static_cast<size_t>(slotIdx)]));

    picker->onNoteSelected = [this, rowIdx, slotIdx](int note) {
        if (rowIdx < static_cast<int>(this->rows.size()))
            this->applyNoteToSlot(*this->rows[static_cast<size_t>(rowIdx)], slotIdx, note);
        // Dismiss the CallOutBox
        if (auto* modal = juce::Component::getCurrentlyModalComponent())
            modal->exitModalState(0);
    };

    auto& btn = (slotIdx == 0) ? row.noteButton1 : row.noteButton2;
    this->activeMidiPicker = picker.get();
    juce::CallOutBox::launchAsynchronously(std::move(picker), btn.getScreenBounds(), nullptr);
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

    if (this->onMidiLearnStateChanged)
        this->onMidiLearnStateChanged(true);
}

void MidiMapTable::stopMidiLearn() {
    if (this->activeLearningRow >= 0 && this->activeLearningRow < static_cast<int>(this->rows.size())) {
        auto& row = *this->rows[static_cast<size_t>(this->activeLearningRow)];
        row.learnButton1.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
        row.learnButton2.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
        row.learnButton1.repaint();
        row.learnButton2.repaint();
    }

    bool wasLearning = (this->activeLearningRow >= 0);
    this->activeLearningRow = -1;
    this->activeLearningSlot = -1;

    if (wasLearning && this->onMidiLearnStateChanged)
        this->onMidiLearnStateChanged(false);
}

void MidiMapTable::midiLearnNoteReceived(int midiNote) {
    if (this->activeLearningRow < 0 || this->activeLearningRow >= static_cast<int>(this->rows.size()))
        return;

    auto& row = *this->rows[static_cast<size_t>(this->activeLearningRow)];
    this->applyNoteToSlot(row, this->activeLearningSlot, midiNote);
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
    const int noteButtonWidth = 84;
    const int learnBtnSize = juce::jmax(rowHeight - 4, 20); // minimum square
    const int gapSmall = 3;
    // Single column width for one instrument group
    const int singleColWidth = instColWidth + artColWidth + (noteButtonWidth + learnBtnSize + gapSmall) * 2 + gapSmall * 3 + 8;
    const int columnGap = 12;

    // Determine if we can fit 2 columns
    int availWidth = this->viewport.getWidth() - (this->viewport.isVerticalScrollBarShown() ? 14 : 0);
    bool twoColumns = availWidth >= singleColWidth * 2 + columnGap;

    // Split rows into columns by instrument groups
    int totalRows = static_cast<int>(this->rows.size());
    int halfRows = totalRows / 2;
    int splitRow = halfRows;

    if (twoColumns && totalRows > 2) {
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

            row.noteButton1.setBounds(xPos, yPos + 2, noteButtonWidth, rowHeight - 4);
            xPos += noteButtonWidth + gapSmall;

            row.learnButton1.setBounds(xPos, yPos + 2, learnBtnSize, learnBtnSize);
            xPos += learnBtnSize + gapSmall;

            row.noteButton2.setBounds(xPos, yPos + 2, noteButtonWidth, rowHeight - 4);
            xPos += noteButtonWidth + gapSmall;

            row.learnButton2.setBounds(xPos, yPos + 2, learnBtnSize, learnBtnSize);

            yPos += rowHeight;
        }
    };

    layoutColumn(0, col1Count, 4);
    if (twoColumns && col2Count > 0) {
        layoutColumn(splitRow, totalRows, singleColWidth + columnGap);
    }
}
