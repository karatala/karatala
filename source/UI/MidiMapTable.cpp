#include "MidiMapTable.h"
#include "UITheme.h"

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

        row->midiAssignmentControl = std::make_unique<MidiNoteAssignmentControl>();
        row->midiAssignmentControl->setLayoutMetrics({ noteButtonWidth, learnButtonSize, midiControlGap, midiControlGap, 2 });
        row->midiAssignmentControl->onNoteSelected = [this, rowIdx](int slotIdx, int note) {
            if (rowIdx < static_cast<int>(this->rows.size()))
                this->applyNoteToSlot(*this->rows[static_cast<size_t>(rowIdx)], slotIdx, note);
        };
        row->midiAssignmentControl->onLearnButtonClicked = [this, rowIdx](int slotIdx) {
            this->startMidiLearn(rowIdx, slotIdx);
        };
        this->contentComponent.addAndMakeVisible(*row->midiAssignmentControl);

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
            row->instrumentLabel.setFont(UITheme::Fonts::table().withHeight(11.0f));
            row->instrumentLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
            row->instrumentLabel.setJustificationType(juce::Justification::centredLeft);

            row->articulationLabel.setText(art.codeRaw, juce::dontSendNotification);
            row->articulationLabel.setFont(UITheme::Fonts::table().withHeight(11.0f));
            row->articulationLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
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
            row->instrumentLabel.setFont(UITheme::Fonts::table().withHeight(11.0f));
            row->instrumentLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
            row->instrumentLabel.setJustificationType(juce::Justification::centredLeft);

            row->articulationLabel.setText(row->articulationCode, juce::dontSendNotification);
            row->articulationLabel.setFont(UITheme::Fonts::table().withHeight(11.0f));
            row->articulationLabel.setColour(juce::Label::textColourId, UITheme::Colours::textAccentSoft);
            row->articulationLabel.setJustificationType(juce::Justification::centredLeft);

            int rowIdx = static_cast<int>(this->rows.size());
            setupRow(row, rowIdx);
            this->rows.push_back(std::move(row));
        }
    }
}

void MidiMapTable::syncNoteButtons(MapRow& row) {
    if (!row.notesPtr || !row.midiAssignmentControl) return;
    auto& notes = *row.notesPtr;
    const int note1 = !notes.empty()     ? static_cast<int>(notes[0]) : -1;
    const int note2 = notes.size() >= 2  ? static_cast<int>(notes[1]) : -1;
    row.midiAssignmentControl->setNotes(note1, note2);
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
    if (row.midiAssignmentControl)
        row.midiAssignmentControl->setLearningSlot(slotIdx);

    if (this->onMidiLearnStateChanged)
        this->onMidiLearnStateChanged(true);
}

void MidiMapTable::stopMidiLearn() {
    if (this->activeLearningRow >= 0 && this->activeLearningRow < static_cast<int>(this->rows.size())) {
        auto& row = *this->rows[static_cast<size_t>(this->activeLearningRow)];
        if (row.midiAssignmentControl)
            row.midiAssignmentControl->setLearningSlot(-1);
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
    g.fillAll(UITheme::Colours::bgEditor);
}

void MidiMapTable::resized() {
    auto area = getLocalBounds();

    this->viewport.setBounds(area);

    const int midiControlWidth = noteButtonWidth * 2 + learnButtonSize * 2 + midiControlGap * 3;
    // Single column width for one instrument group
    const int singleColWidth = instColWidth + artColWidth + midiControlWidth + 8;

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

            row.instrumentLabel.setBounds(xPos, yPos, instColWidth - midiControlGap, rowHeight);
            xPos += instColWidth;

            row.articulationLabel.setBounds(xPos, yPos, artColWidth - midiControlGap, rowHeight);
            xPos += artColWidth;

            if (row.midiAssignmentControl)
                row.midiAssignmentControl->setBounds(xPos, yPos, midiControlWidth, rowHeight);

            yPos += rowHeight;
        }
    };

    layoutColumn(0, col1Count, 4);
    if (twoColumns && col2Count > 0) {
        layoutColumn(splitRow, totalRows, singleColWidth + columnGap);
    }
}
