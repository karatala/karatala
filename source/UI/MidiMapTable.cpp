#include "MidiMapTable.h"
#include "UITheme.h"
#include "UIHelpers.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <optional>

namespace {

std::optional<int> parseExpressionPointEditorValue(const juce::String& text) {
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || !trimmed.containsOnly("0123456789"))
        return std::nullopt;

    const auto value = trimmed.getLargeIntValue();
    if (value < 0 || value > 127)
        return std::nullopt;

    return static_cast<int>(value);
}

} // namespace

MidiMapTable::MidiMapTable() {
    this->layoutLabel.setText("Layout", juce::dontSendNotification);
    UIHelpers::configureCaptionLabel(this->layoutLabel, juce::Justification::centredRight, UITheme::Colours::textSecondary);
    this->addAndMakeVisible(this->layoutLabel);

    this->addAndMakeVisible(this->presetCombo);
    UIHelpers::configureComboBox(this->presetCombo, UIHelpers::ComboBoxStyle::LayoutSelector);
    this->presetCombo.setTextWhenNothingSelected("(no presets)");
    this->presetCombo.onChange = [this] {
        int selectedId = this->presetCombo.getSelectedId();
        if (selectedId > 0 && selectedId <= static_cast<int>(this->presetEntries.size())) {
            if (this->onPresetSelected)
                this->onPresetSelected(this->presetEntries[static_cast<size_t>(selectedId - 1)]);
        }
    };

    UIHelpers::configureControlButton(this->saveButton);
    this->saveButton.onClick = [this] {
        if (this->onPresetSaveRequested) this->onPresetSaveRequested();
    };
    this->addAndMakeVisible(this->saveButton);

    UIHelpers::configureControlButton(this->moreButton);
    this->moreButton.onClick = [this] { this->showMoreMenu(); };
    this->addAndMakeVisible(this->moreButton);

    UIHelpers::configureControlButton(this->crosstalkButton);
    this->crosstalkButton.onClick = [this] {
        if (this->onCrosstalkPopupRequested) this->onCrosstalkPopupRequested();
    };
    this->addAndMakeVisible(this->crosstalkButton);
}

void MidiMapTable::populate(KitLayout* layout) {
    this->layoutPtr = layout;
    this->stopMidiLearn();
    this->buildRows();
    this->resized();
}

void MidiMapTable::populatePresets(std::vector<PresetEntry> presets, const juce::String& currentId) {
    this->presetEntries = std::move(presets);
    this->presetCombo.clear(juce::dontSendNotification);

    int selectedId = 0;
    for (size_t i = 0; i < this->presetEntries.size(); ++i) {
        const auto& entry = this->presetEntries[i];
        juce::String prefix = entry.isUser ? juce::String::fromUTF8("\xf0\x9f\x91\xa4 ") : juce::String::fromUTF8("\xf0\x9f\x93\xa6 ");
        juce::String label = prefix + (entry.name.isNotEmpty() ? entry.name : entry.id);
        this->presetCombo.addItem(label, static_cast<int>(i + 1));
        if (entry.id == currentId)
            selectedId = static_cast<int>(i + 1);
    }

    this->presetCombo.setSelectedId(selectedId, juce::dontSendNotification);
    this->presetCombo.onChange = [this] {
        int sid = this->presetCombo.getSelectedId();
        if (sid > 0 && sid <= static_cast<int>(this->presetEntries.size())) {
            if (this->onPresetSelected) this->onPresetSelected(this->presetEntries[static_cast<size_t>(sid - 1)]);
        }
    };
}

void MidiMapTable::showMoreMenu() {
    juce::PopupMenu menu;
    menu.setLookAndFeel(&this->getLookAndFeel());
    menu.addItem(1, "Clone to user presets");
    menu.addSeparator();
    menu.addItem(2, "Open user presets folder");
    menu.addItem(3, "Open factory presets folder");
    menu.addSeparator();

    // Enable Delete only for user presets
    int selectedId = this->presetCombo.getSelectedId();
    bool isUserPreset = false;
    if (selectedId > 0 && selectedId <= static_cast<int>(this->presetEntries.size()))
        isUserPreset = this->presetEntries[static_cast<size_t>(selectedId - 1)].isUser;
    menu.addItem(4, "Delete preset", isUserPreset);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this->moreButton),
        [this](int result) {
            if (result == 1 && this->onPresetCloneRequested) this->onPresetCloneRequested();
            else if (result == 2 && this->onOpenUserFolderRequested) this->onOpenUserFolderRequested();
            else if (result == 3 && this->onOpenFactoryFolderRequested) this->onOpenFactoryFolderRequested();
            else if (result == 4 && this->onPresetDeleteRequested) this->onPresetDeleteRequested();
        });
}

void MidiMapTable::buildRows() {
    this->rows.clear();
    this->contentComponent.removeAllChildren();

    if (!this->layoutPtr) return;

    auto setupCommonRow = [this](std::unique_ptr<MapRow>& row) {
        this->contentComponent.addAndMakeVisible(row->instrumentLabel);
        row->instrumentLabel.setVisible(row->showInstrumentLabel);
        this->contentComponent.addAndMakeVisible(row->articulationLabel);
    };

    auto setupNoteRow = [this, &setupCommonRow](std::unique_ptr<MapRow>& row, int rowIdx) {
        setupCommonRow(row);
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

    auto setupControllerRow = [this, &setupCommonRow](std::unique_ptr<MapRow>& row, int rowIdx) {
        setupCommonRow(row);
        row->controllerCombo = std::make_unique<juce::ComboBox>();
        UIHelpers::configureComboBox(*row->controllerCombo, UIHelpers::ComboBoxStyle::MidiControllerList);
        for (int controllerNumber = 0; controllerNumber <= 127; ++controllerNumber)
            row->controllerCombo->addItem(UIHelpers::getMidiControllerItemText(controllerNumber), controllerNumber + 1);

        row->controllerCombo->setSelectedId(row->controllerNumber + 1, juce::dontSendNotification);
        row->controllerCombo->onChange = [this, rowIdx] {
            if (rowIdx < static_cast<int>(this->rows.size())) {
                auto& changedRow = *this->rows[static_cast<size_t>(rowIdx)];
                this->applyControllerNumber(changedRow, changedRow.controllerCombo->getSelectedId() - 1);
            }
        };
        this->contentComponent.addAndMakeVisible(*row->controllerCombo);

        if (row->controllerPtr != nullptr && KitLayout::isSupportedExpressionPointCode(row->controllerPtr->codeRaw)) {
            row->curveButton = std::make_unique<juce::TextButton>("curve");
            UIHelpers::configureControlButton(*row->curveButton);
            row->curveButton->onClick = [this, rowIdx] {
                if (rowIdx < static_cast<int>(this->rows.size()))
                    this->showExpressionCurvePopup(*this->rows[static_cast<size_t>(rowIdx)]);
            };
            this->contentComponent.addAndMakeVisible(*row->curveButton);
        }
    };

    for (auto& slot : this->layoutPtr->instrumentSlots) {
        bool firstRow = true;

        // Articulation rows
        for (auto& art : slot.articulations) {
            auto row = std::make_unique<MapRow>();
            row->instrumentId = slot.id;
            row->articulationCode = art.codeRaw;
            row->isController = false;
            row->slotPtr = &slot;
            row->notesPtr = &art.notes;
            row->showInstrumentLabel = firstRow;
            firstRow = false;

            row->instrumentLabel.setText(slot.display.isNotEmpty() ? slot.display : slot.id, juce::dontSendNotification);
            UIHelpers::configureTableRowLabel(row->instrumentLabel, UITheme::Colours::textPrimary);

            row->articulationLabel.setText(art.codeRaw, juce::dontSendNotification);
            UIHelpers::configureTableRowLabel(row->articulationLabel, UITheme::Colours::textPrimary);

            int rowIdx = static_cast<int>(this->rows.size());
            setupNoteRow(row, rowIdx);
            this->rows.push_back(std::move(row));
        }

        // Controller rows
        for (auto& cc : slot.controllers) {
            auto row = std::make_unique<MapRow>();
            row->instrumentId = slot.id;
            row->articulationCode = "CC: " + cc.codeRaw;
            row->isController = true;
            row->controllerNumber = cc.number;
            row->slotPtr = &slot;
            row->controllerPtr = &cc;
            row->showInstrumentLabel = firstRow;
            firstRow = false;

            row->instrumentLabel.setText(slot.display.isNotEmpty() ? slot.display : slot.id, juce::dontSendNotification);
            UIHelpers::configureTableRowLabel(row->instrumentLabel, UITheme::Colours::textPrimary);

            row->articulationLabel.setText(row->articulationCode, juce::dontSendNotification);
            UIHelpers::configureTableRowLabel(row->articulationLabel, UITheme::Colours::textAccentSoft);

            int rowIdx = static_cast<int>(this->rows.size());
            setupControllerRow(row, rowIdx);
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
    UIHelpers::applyMidiNoteToSlot(*row.notesPtr, slotIdx, note);
    this->syncNoteButtons(row);
    if (this->onMappingChanged) this->onMappingChanged();
}

void MidiMapTable::applyControllerNumber(MapRow& row, int controllerNumber) {
    if (!row.controllerPtr || !juce::isPositiveAndBelow(controllerNumber, 128)) return;

    row.controllerPtr->number = controllerNumber;
    row.controllerNumber = controllerNumber;
    if (this->onMappingChanged) this->onMappingChanged();
}

int MidiMapTable::getEffectiveExpressionPointValue(const KitLayout::InstrumentSlot& slot, const juce::String& codeRaw, const juce::String& label) const {
    if (const auto value = KitLayout::getExpressionPoint(slot.expressionPoints, codeRaw, label); value.has_value())
        return static_cast<int>(*value);

    if (this->layoutPtr != nullptr) {
        if (const auto value = KitLayout::getExpressionPoint(this->layoutPtr->expressionPoints, codeRaw, label); value.has_value())
            return static_cast<int>(*value);
    }

    const auto defaults = KitLayout::getDefaultExpressionPoints();
    if (const auto value = KitLayout::getExpressionPoint(defaults, codeRaw, label); value.has_value())
        return static_cast<int>(*value);

    return 0;
}

void MidiMapTable::showExpressionCurvePopup(MapRow& row) {
    if (this->layoutPtr == nullptr || row.slotPtr == nullptr || row.controllerPtr == nullptr)
        return;

    const auto code = row.controllerPtr->codeRaw.trim().toLowerCase();
    const auto labels = KitLayout::getExpressionPointLabels(code);
    if (labels.empty())
        return;

    auto* dlg = new juce::AlertWindow("Expression curve points", row.instrumentId + " / " + code, juce::MessageBoxIconType::NoIcon);
    for (const auto& label : labels)
        dlg->addTextEditor(label, juce::String(this->getEffectiveExpressionPointValue(*row.slotPtr, code, label)), label + ":");
    dlg->addButton("Apply", 1);
    dlg->addButton("Cancel", 0);

    const auto slotId = row.instrumentId;
    dlg->enterModalState(true, juce::ModalCallbackFunction::create([this, dlg, slotId, code, labels](int result) {
        if (result != 1 || this->layoutPtr == nullptr)
            return;

        KitLayout::InstrumentSlot* targetSlot = nullptr;
        for (auto& slot : this->layoutPtr->instrumentSlots) {
            if (slot.id == slotId) {
                targetSlot = &slot;
                break;
            }
        }
        if (targetSlot == nullptr)
            return;

        std::map<juce::String, int> parsedValues;
        std::map<juce::String, int> previousEffectiveValues;
        std::map<juce::String, bool> hadSlotOverrides;
        for (const auto& label : labels) {
            const auto editorText = dlg->getTextEditorContents(label);
            const auto parsed = parseExpressionPointEditorValue(editorText);
            if (!parsed.has_value()) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Invalid expression curve point",
                    "'" + label + "' must be an integer from 0 to 127.");
                return;
            }

            parsedValues[label] = *parsed;
            previousEffectiveValues[label] = this->getEffectiveExpressionPointValue(*targetSlot, code, label);
            hadSlotOverrides[label] = KitLayout::getExpressionPoint(targetSlot->expressionPoints, code, label).has_value();
        }

        for (const auto& label : labels) {
            const int value = parsedValues[label];
            if (hadSlotOverrides[label] || value != previousEffectiveValues[label])
                targetSlot->expressionPoints[code][label] = static_cast<uint8_t>(value);
        }

        if (this->onLayoutReapplyRequested)
            this->onLayoutReapplyRequested();
        else if (this->onMappingChanged)
            this->onMappingChanged();
    }), true);
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
    if (row.midiAssignmentControl) row.midiAssignmentControl->setLearningSlot(slotIdx);
    if (this->onMidiLearnStateChanged) this->onMidiLearnStateChanged(true);
}

void MidiMapTable::stopMidiLearn() {
    if (this->activeLearningRow >= 0 && this->activeLearningRow < static_cast<int>(this->rows.size())) {
        auto& row = *this->rows[static_cast<size_t>(this->activeLearningRow)];
        if (row.midiAssignmentControl) row.midiAssignmentControl->setLearningSlot(-1);
    }

    bool wasLearning = (this->activeLearningRow >= 0);
    this->activeLearningRow = -1;
    this->activeLearningSlot = -1;

    if (wasLearning && this->onMidiLearnStateChanged)
        this->onMidiLearnStateChanged(false);
}

void MidiMapTable::midiLearnNoteReceived(int midiNote) {
    if (this->activeLearningRow < 0 || this->activeLearningRow >= static_cast<int>(this->rows.size())) return;

    auto& row = *this->rows[static_cast<size_t>(this->activeLearningRow)];
    this->applyNoteToSlot(row, this->activeLearningSlot, midiNote);
    this->stopMidiLearn();
}

void MidiMapTable::paint(juce::Graphics& g) {
    g.fillAll(UITheme::Colours::bgEditor);

    // Draw toolbar background
    auto toolbarArea = getLocalBounds().removeFromTop(toolbarHeight);
    g.setColour(UITheme::Colours::bgPanel);
    g.fillRect(toolbarArea);
    g.setColour(UITheme::Colours::borderSubtle);
    g.drawHorizontalLine(toolbarArea.getBottom() - 1, static_cast<float>(toolbarArea.getX()), static_cast<float>(toolbarArea.getRight()));
}

void MidiMapTable::resized() {
    auto area = getLocalBounds();

    // Toolbar strip at the top (non-scrolling)
    auto toolbarArea = area.removeFromTop(toolbarHeight);
    toolbarArea.reduce(UITheme::Spacing::sm, UITheme::Spacing::xs);
    const int moreWidth = 28;
    const int saveWidth = 44;
    const int crosstalkWidth = 88;
    const int gap = UITheme::Spacing::xs;
    this->moreButton.setBounds(toolbarArea.removeFromRight(moreWidth));
    toolbarArea.removeFromRight(gap);
    this->saveButton.setBounds(toolbarArea.removeFromRight(saveWidth));
    toolbarArea.removeFromRight(gap);
    this->crosstalkButton.setBounds(toolbarArea.removeFromRight(crosstalkWidth));
    toolbarArea.removeFromRight(gap);
    const int selectorWidth = toolbarArea.getWidth() / 2;
    auto selectorArea = toolbarArea.removeFromRight(selectorWidth);
    this->layoutLabel.setBounds(selectorArea.removeFromLeft(layoutLabelWidth));
    selectorArea.removeFromLeft(gap);
    this->presetCombo.setBounds(selectorArea);

    // Scrollable table below toolbar
    this->viewport.setBounds(area);

    const int midiControlWidth = noteButtonWidth * 2 + learnButtonSize * 2 + midiControlGap * 3;
    const int controllerCurveButtonWidth = 42;
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
            if (row.controllerCombo) {
                auto controlArea = juce::Rectangle<int>(xPos, yPos, midiControlWidth, rowHeight).reduced(0, 2);
                if (row.curveButton) {
                    row.curveButton->setBounds(controlArea.removeFromRight(controllerCurveButtonWidth));
                    controlArea.removeFromRight(midiControlGap);
                }
                row.controllerCombo->setBounds(controlArea);
            }

            yPos += rowHeight;
        }
    };

    layoutColumn(0, col1Count, 4);
    if (twoColumns && col2Count > 0) {
        layoutColumn(splitRow, totalRows, singleColWidth + columnGap);
    }
}
