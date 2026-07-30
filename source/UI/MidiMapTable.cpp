#include "MidiMapTable.h"
#include "CompactModalDialog.h"
#include "UIHelpers.h"
#include "UITheme.h"
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

    auto setupHeader = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        UIHelpers::configureTableHeaderLabel(label);
        this->addAndMakeVisible(label);
    };
    setupHeader(this->headerInstrument1, "Instrument");
    setupHeader(this->headerArticulation1, "Articulation");
    setupHeader(this->headerMapping1, "MIDI Notes / Controller");
    setupHeader(this->headerInstrument2, "Instrument");
    setupHeader(this->headerArticulation2, "Articulation");
    setupHeader(this->headerMapping2, "MIDI Notes / Controller");
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
        row->midiAssignmentControl->setLayoutMetrics({ this->noteButtonWidth, this->learnButtonSize, this->midiControlGap, this->midiControlGap, 1 });
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
            UIHelpers::configureTableRowLabel(row->articulationLabel, UITheme::Colours::textAccent);

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
            UIHelpers::configureTableRowLabel(row->articulationLabel, UITheme::Colours::textAccent);

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

    CompactModalDialog::Options options;
    options.title = "Expression Curve Points";
    options.message = row.instrumentId + " / " + code;
    options.confirmText = "Apply";
    for (const auto& label : labels) options.fields.push_back({ label, label, juce::String(this->getEffectiveExpressionPointValue(*row.slotPtr, code, label)), 3, "0123456789" });
    const auto slotId = row.instrumentId;
    juce::Component::SafePointer<MidiMapTable> safeThis(this);
    CompactModalDialog::show(*this, std::move(options), [safeThis, slotId, code, labels](const CompactModalDialog& dialog) {
        if (safeThis == nullptr || safeThis->layoutPtr == nullptr) return juce::String();

        KitLayout::InstrumentSlot* targetSlot = nullptr;
        for (auto& slot : safeThis->layoutPtr->instrumentSlots) {
            if (slot.id == slotId) {
                targetSlot = &slot;
                break;
            }
        }
        if (targetSlot == nullptr) return juce::String();

        std::map<juce::String, int> parsedValues;
        std::map<juce::String, int> previousEffectiveValues;
        std::map<juce::String, bool> hadSlotOverrides;
        for (const auto& label : labels) {
            const auto parsed = parseExpressionPointEditorValue(dialog.getFieldText(label));
            if (!parsed.has_value()) return "'" + label + "' must be an integer from 0 to 127.";

            parsedValues[label] = *parsed;
            previousEffectiveValues[label] = safeThis->getEffectiveExpressionPointValue(*targetSlot, code, label);
            hadSlotOverrides[label] = KitLayout::getExpressionPoint(targetSlot->expressionPoints, code, label).has_value();
        }

        for (const auto& label : labels) {
            const int value = parsedValues[label];
            if (hadSlotOverrides[label] || value != previousEffectiveValues[label])
                targetSlot->expressionPoints[code][label] = static_cast<uint8_t>(value);
        }

        if (safeThis->onLayoutReapplyRequested)
            safeThis->onLayoutReapplyRequested();
        else if (safeThis->onMappingChanged)
            safeThis->onMappingChanged();
        return juce::String();
    });
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
    g.fillAll(UITheme::Colours::bgApp);

    // Draw toolbar background
    auto toolbarArea = this->getLocalBounds().removeFromTop(this->toolbarHeight);
    g.setColour(UITheme::Colours::bgPanel);
    g.fillRect(toolbarArea);
    g.setColour(UITheme::Colours::borderSubtle);
    g.drawHorizontalLine(toolbarArea.getBottom() - 1, static_cast<float>(toolbarArea.getX()), static_cast<float>(toolbarArea.getRight()));
}

void MidiMapTable::resized() {
    auto area = this->getLocalBounds();

    // Toolbar strip at the top (non-scrolling)
    auto toolbarArea = area.removeFromTop(this->toolbarHeight);
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
    this->layoutLabel.setBounds(selectorArea.removeFromLeft(this->layoutLabelWidth));
    selectorArea.removeFromLeft(gap);
    this->presetCombo.setBounds(selectorArea);

    auto tableHeaderArea = area.removeFromTop(this->headerHeight);
    this->viewport.setBounds(area);

    const int midiControlWidth = this->noteButtonWidth * 2 + this->learnButtonSize * 2 + this->midiControlGap * 3;
    const int controllerCurveButtonWidth = 42;
    const int singleColWidth = this->instColWidth + this->artColWidth + midiControlWidth + 8;

    const int availableWidth = this->viewport.getWidth() - this->viewport.getScrollBarThickness();
    const bool twoColumns = availableWidth >= singleColWidth * 2 + this->columnGap;

    const int totalRows = static_cast<int>(this->rows.size());
    const int halfRows = totalRows / 2;
    int splitRow = halfRows;

    if (twoColumns && totalRows > 2) {
        for (int idx = halfRows; idx < totalRows; ++idx) {
            if (this->rows[static_cast<size_t>(idx)]->showInstrumentLabel) {
                splitRow = idx;
                break;
            }
        }
    }

    const int column1Count = twoColumns ? splitRow : totalRows;
    const int column2Count = twoColumns ? (totalRows - splitRow) : 0;
    const int maxColumnRows = juce::jmax(column1Count, column2Count);

    const int contentHeight = maxColumnRows * this->rowHeight + UITheme::Spacing::sm;
    int contentWidth = twoColumns ? (singleColWidth * 2 + this->columnGap) : singleColWidth;
    contentWidth = juce::jmax(contentWidth, availableWidth);

    this->contentComponent.setBounds(0, 0, contentWidth, contentHeight);

    auto layoutHeaderColumn = [this, &tableHeaderArea, midiControlWidth](juce::Label& instrumentHeader, juce::Label& articulationHeader, juce::Label& mappingHeader, int xOffset, bool visible) {
        instrumentHeader.setVisible(visible);
        articulationHeader.setVisible(visible);
        mappingHeader.setVisible(visible);
        if (!visible) return;

        const int headerY = tableHeaderArea.getY();
        instrumentHeader.setBounds(xOffset, headerY, this->instColWidth - this->midiControlGap, this->headerHeight);
        xOffset += this->instColWidth;
        articulationHeader.setBounds(xOffset, headerY, this->artColWidth - this->midiControlGap, this->headerHeight);
        xOffset += this->artColWidth;
        mappingHeader.setBounds(xOffset, headerY, midiControlWidth, this->headerHeight);
    };
    layoutHeaderColumn(this->headerInstrument1, this->headerArticulation1, this->headerMapping1, UITheme::Spacing::sm, true);
    layoutHeaderColumn(this->headerInstrument2, this->headerArticulation2, this->headerMapping2, singleColWidth + this->columnGap, column2Count > 0);

    auto layoutColumn = [this, midiControlWidth](int startIdx, int endIdx, int xOffset) {
        int yPos = 0;
        for (int idx = startIdx; idx < endIdx; ++idx) {
            auto& row = *this->rows[static_cast<size_t>(idx)];
            int xPos = xOffset;

            row.instrumentLabel.setBounds(xPos, yPos, this->instColWidth - this->midiControlGap, this->rowHeight);
            xPos += this->instColWidth;

            row.articulationLabel.setBounds(xPos, yPos, this->artColWidth - this->midiControlGap, this->rowHeight);
            xPos += this->artColWidth;

            if (row.midiAssignmentControl)
                row.midiAssignmentControl->setBounds(xPos, yPos, midiControlWidth, this->rowHeight);
            if (row.controllerCombo) {
                auto controlArea = juce::Rectangle<int>(xPos, yPos, midiControlWidth, this->rowHeight).reduced(0, 1);
                if (row.curveButton) {
                    row.curveButton->setBounds(controlArea.removeFromRight(controllerCurveButtonWidth));
                    controlArea.removeFromRight(this->midiControlGap);
                }
                row.controllerCombo->setBounds(controlArea);
            }

            yPos += this->rowHeight;
        }
    };

    layoutColumn(0, column1Count, UITheme::Spacing::sm);
    if (twoColumns && column2Count > 0) {
        layoutColumn(splitRow, totalRows, singleColWidth + this->columnGap);
    }
}
