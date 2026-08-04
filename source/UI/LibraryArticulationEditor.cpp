#include "LibraryArticulationEditor.h"
#include "../Config/Parsing.h"
#include "../Engine/RoutingTypes.h"
#include "UIComponentStyle.h"
#include "UITheme.h"

#include <algorithm>
#include <cmath>

void LibraryArticulationEditor::WheelTextEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (!this->isEnabled() || event.mods.isAnyMouseButtonDown() || !this->onWheelStep) {
        juce::TextEditor::mouseWheelMove(event, wheel);
        return;
    }

    double wheelAmount = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? -static_cast<double>(wheel.deltaX) : static_cast<double>(wheel.deltaY);
    if (wheel.isReversed) wheelAmount = -wheelAmount;
    if (juce::approximatelyEqual(wheelAmount, 0.0)) return;
    this->onWheelStep(wheelAmount > 0.0, event.mods.isShiftDown());
}

void LibraryArticulationEditor::SampleMatrixLookAndFeel::drawTableHeaderColumn(juce::Graphics& graphics, juce::TableHeaderComponent& header, const juce::String& columnName, int columnId, int width, int height, bool isMouseOver, bool isMouseDown, int columnFlags) {
    juce::ignoreUnused(columnId, columnFlags);
    const auto highlightColour = header.findColour(juce::TableHeaderComponent::highlightColourId);
    if (isMouseDown) graphics.fillAll(highlightColour);
    else if (isMouseOver) graphics.fillAll(highlightColour.withMultipliedAlpha(0.625f));
    graphics.setColour(header.findColour(juce::TableHeaderComponent::textColourId));
    graphics.setFont(UITheme::Fonts::normal().boldened());
    graphics.drawFittedText(columnName, juce::Rectangle<int>(0, 0, width, height).reduced(2, 0), juce::Justification::centred, 1);
}

LibraryArticulationEditor::LibraryArticulationEditor(DrumKitEngine& drumKitEngine)
    : engine(drumKitEngine),
      sampleMatrixModel(*this),
      selectedSampleListModel(*this),
      sampleMatrix("Sample overrides", &this->sampleMatrixModel),
      selectedSampleList("Selected samples", &this->selectedSampleListModel) {
    this->setWantsKeyboardFocus(true);

    this->titleLabel.setFont(UITheme::Fonts::sectionTitle());
    this->titleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->titleLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->titleLabel);

    this->scopeLabel.setText("Changes this instrument on every pad that uses it.", juce::dontSendNotification);
    this->scopeLabel.setFont(UITheme::Fonts::normal());
    this->scopeLabel.setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
    this->scopeLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->scopeLabel);

    for (auto* sectionLabel : { &this->balanceSectionLabel, &this->velocityRangesLabel, &this->sampleOverridesLabel, &this->selectedSamplesLabel }) {
        sectionLabel->setFont(UITheme::Fonts::normal().boldened());
        sectionLabel->setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
        sectionLabel->setJustificationType(juce::Justification::centredLeft);
        this->addAndMakeVisible(*sectionLabel);
    }
    this->balanceSectionLabel.setText("BALANCE", juce::dontSendNotification);
    this->velocityRangesLabel.setText("VELOCITY RANGES", juce::dontSendNotification);
    this->sampleOverridesLabel.setText("SAMPLE OVERRIDES", juce::dontSendNotification);

    for (auto* label : { &this->instrumentLabel, &this->articulationLabel, &this->micLabel, &this->statusLabel, &this->manualGainLabel, &this->fileOffsetLabel, &this->micOffsetLabel }) {
        label->setFont(UITheme::Fonts::normal());
        label->setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
        label->setJustificationType(juce::Justification::centredLeft);
        this->addAndMakeVisible(*label);
    }
    this->instrumentLabel.setText("Instrument", juce::dontSendNotification);
    this->articulationLabel.setText("Articulation", juce::dontSendNotification);
    this->micLabel.setText("MIC", juce::dontSendNotification);
    this->manualGainLabel.setText("File gain correction, dB", juce::dontSendNotification);
    this->fileOffsetLabel.setText("File start offset, ms", juce::dontSendNotification);
    this->micOffsetLabel.setText("Mic offset, ms", juce::dontSendNotification);
    this->statusLabel.setColour(juce::Label::textColourId, UITheme::Colours::textMuted);

    for (auto* nameLabel : { &this->instrumentNameLabel, &this->articulationNameLabel }) {
        nameLabel->setFont(UITheme::Fonts::normal());
        nameLabel->setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
        nameLabel->setColour(juce::Label::backgroundColourId, UITheme::Colours::bgControlAlt);
        nameLabel->setJustificationType(juce::Justification::centredLeft);
        this->addAndMakeVisible(*nameLabel);
    }

    for (auto* button : {
        &this->resetInstrumentBalanceButton, &this->resetArticulationBalanceButton, &this->resetManualGainButton, &this->resetFileOffsetButton,
        &this->resetMicOffsetButton, &this->evenRangesButton, &this->resetRangesButton, &this->retrySaveButton, &this->reloadSavedButton,
    }) {
        UIComponentStyle::configureControlButton(*button);
        this->addAndMakeVisible(*button);
    }
    UIComponentStyle::configureDialogCloseButton(this->doneButton);
    this->addAndMakeVisible(this->doneButton);
    for (auto* resetButton : { &this->resetInstrumentBalanceButton, &this->resetArticulationBalanceButton, &this->resetManualGainButton, &this->resetFileOffsetButton, &this->resetMicOffsetButton })
        resetButton->setButtonText(UITheme::Symbols::times);
    this->retrySaveButton.setVisible(false);
    this->reloadSavedButton.setVisible(false);
    this->retrySaveButton.setTooltip("Try the same save again.");
    this->reloadSavedButton.setTooltip("Discard editor values and reload kit-overrides.yml.");

    auto configureEditor = [this](juce::TextEditor& editor, int maximumLength, const juce::String& allowedCharacters) {
        editor.setMultiLine(false);
        editor.setJustification(juce::Justification::centred);
        editor.setInputRestrictions(maximumLength, allowedCharacters);
        editor.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
        editor.setBorder(juce::BorderSize<int>());
        editor.setIndents(0, 0);
        editor.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgControl);
        editor.setColour(juce::TextEditor::textColourId, UITheme::Colours::textPrimary);
        UIComponentStyle::configureBorderlessTextEditor(editor);
        this->addAndMakeVisible(editor);
    };
    configureEditor(this->instrumentBalanceEditor, 4, "LlCcRr0123456789");
    configureEditor(this->articulationBalanceEditor, 4, "LlCcRr0123456789");
    configureEditor(this->manualGainEditor, 12, "-+.0123456789");
    configureEditor(this->fileOffsetEditor, 12, "-+.0123456789");
    configureEditor(this->micOffsetEditor, 12, "-+.0123456789");

    this->manualGainEditor.setTooltip("Replaces the gain correction stored in each selected filename. Wheel: 0.5 dB. Shift-wheel: 0.1 dB.");
    this->fileOffsetEditor.setTooltip("Negative milliseconds skip the start. Positive milliseconds delay playback. Wheel: 10 ms. Shift-wheel: 1 ms.");
    this->micOffsetEditor.setTooltip("Overrides the file offset for each selected mic. Wheel: 10 ms. Shift-wheel: 1 ms.");
    this->instrumentBalanceEditor.onReturnKey = [this] { this->commitInstrumentBalance(); this->instrumentBalanceEditor.giveAwayKeyboardFocus(); };
    this->articulationBalanceEditor.onReturnKey = [this] { this->commitArticulationBalance(); this->articulationBalanceEditor.giveAwayKeyboardFocus(); };
    this->manualGainEditor.onReturnKey = [this] { this->commitManualGain(); this->manualGainEditor.giveAwayKeyboardFocus(); };
    this->fileOffsetEditor.onReturnKey = [this] { this->commitFilePlaybackOffset(); this->fileOffsetEditor.giveAwayKeyboardFocus(); };
    this->micOffsetEditor.onReturnKey = [this] { this->commitMicPlaybackOffset(); this->micOffsetEditor.giveAwayKeyboardFocus(); };
    this->instrumentBalanceEditor.onFocusLost = [this] { this->commitInstrumentBalance(); };
    this->articulationBalanceEditor.onFocusLost = [this] { this->commitArticulationBalance(); };
    this->manualGainEditor.onFocusLost = [this] { this->commitManualGain(); };
    this->fileOffsetEditor.onFocusLost = [this] { this->commitFilePlaybackOffset(); };
    this->micOffsetEditor.onFocusLost = [this] { this->commitMicPlaybackOffset(); };
    this->manualGainEditor.onTextChange = [this] { this->manualGainDirty = true; };
    this->fileOffsetEditor.onTextChange = [this] { this->fileOffsetDirty = true; };
    this->micOffsetEditor.onTextChange = [this] { this->micOffsetDirty = true; };
    this->instrumentBalanceEditor.onWheelStep = [this](bool increase, bool fine) {
        this->stepBalanceEditor(this->instrumentBalanceEditor, this->rangeInfo.instrumentBalance, increase, fine);
        this->commitInstrumentBalance();
    };
    this->articulationBalanceEditor.onWheelStep = [this](bool increase, bool fine) {
        this->stepBalanceEditor(this->articulationBalanceEditor, this->rangeInfo.articulationBalance, increase, fine);
        this->commitArticulationBalance();
    };
    this->manualGainEditor.onWheelStep = [this](bool increase, bool fine) { this->stepBatchEditor(this->manualGainEditor, BatchField::ManualGain, increase, fine); };
    this->fileOffsetEditor.onWheelStep = [this](bool increase, bool fine) { this->stepBatchEditor(this->fileOffsetEditor, BatchField::FileOffset, increase, fine); };
    this->micOffsetEditor.onWheelStep = [this](bool increase, bool fine) { this->stepBatchEditor(this->micOffsetEditor, BatchField::MicOffset, increase, fine); };

    this->sampleMatrix.setHeaderHeight(24);
    this->sampleMatrix.setRowHeight(24);
    this->sampleMatrix.setOutlineThickness(0);
    this->sampleMatrix.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    this->sampleMatrix.getHeader().setLookAndFeel(&this->sampleMatrixLookAndFeel);
    this->sampleMatrix.setTooltip("Click * to invert the table, an RR letter to invert its row, or a velocity heading to invert its column.");
    this->sampleMatrix.addMouseListener(this, true);
    this->addAndMakeVisible(this->sampleMatrix);

    this->selectedSampleList.setRowHeight(22);
    this->selectedSampleList.setOutlineThickness(0);
    this->selectedSampleList.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    this->addAndMakeVisible(this->selectedSampleList);

    this->evenRangesButton.setTooltip("Make all velocity ranges about the same size.");
    this->evenRangesButton.onClick = [this] { this->evenlyDistributeRanges(); };
    this->resetRangesButton.onClick = [this] { this->resetAllBoundaries(); };
    this->resetInstrumentBalanceButton.onClick = [this] { this->instrumentBalanceEditor.setText({}, false); this->commitInstrumentBalance(); };
    this->resetArticulationBalanceButton.onClick = [this] { this->articulationBalanceEditor.setText({}, false); this->commitArticulationBalance(); };
    this->resetManualGainButton.onClick = [this] {
        this->manualGainEditor.setText({}, false);
        this->manualGainDirty = true;
        this->commitManualGain();
    };
    this->resetFileOffsetButton.onClick = [this] {
        this->fileOffsetEditor.setText({}, false);
        this->fileOffsetDirty = true;
        this->commitFilePlaybackOffset();
    };
    this->resetMicOffsetButton.onClick = [this] {
        this->micOffsetEditor.setText({}, false);
        this->micOffsetDirty = true;
        this->commitMicPlaybackOffset();
    };
    this->retrySaveButton.onClick = [this] {
        const auto retryAction = this->retrySaveAction;
        if (retryAction) retryAction();
    };
    this->reloadSavedButton.onClick = [this] { this->reloadSavedOverrides(); };
    this->doneButton.onClick = [this] {
        this->commitInstrumentBalance();
        if (this->hasPersistenceFailure()) return;
        this->commitArticulationBalance();
        if (this->hasPersistenceFailure()) return;
        if (!this->commitPendingSampleEditors()) return;
        if (this->onDone) this->onDone();
    };
}

LibraryArticulationEditor::~LibraryArticulationEditor() {
    this->stopTimer();
    this->clearHoverPreview();
    this->restoreCommittedPreview();
    this->sampleMatrix.getHeader().setLookAndFeel(nullptr);
}

int LibraryArticulationEditor::SampleMatrixModel::getNumRows() {
    return static_cast<int>(this->owner.roundRobins.size());
}

void LibraryArticulationEditor::SampleMatrixModel::paintRowBackground(juce::Graphics& graphics, int rowNumber, int width, int height, bool rowIsSelected) {
    juce::ignoreUnused(rowIsSelected);
    graphics.fillAll(rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd);
    graphics.setColour(UITheme::Colours::borderSubtle);
    graphics.drawHorizontalLine(height - 1, 0.0f, static_cast<float>(width));
}

void LibraryArticulationEditor::SampleMatrixModel::paintCell(juce::Graphics& graphics, int rowNumber, int columnId, int width, int height, bool rowIsSelected) {
    juce::ignoreUnused(rowIsSelected);
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.roundRobins.size())) return;
    graphics.setFont(UITheme::Fonts::normal());
    if (columnId == LibraryArticulationEditor::roundRobinColumnId) {
        auto& header = this->owner.sampleMatrix.getHeader();
        graphics.setColour(header.findColour(juce::TableHeaderComponent::backgroundColourId));
        graphics.fillRect(0, 0, width, height);
        graphics.setColour(header.findColour(juce::TableHeaderComponent::outlineColourId));
        graphics.fillRect(width - 1, 0, 1, height);
        graphics.fillRect(0, height - 1, width, 1);
        this->owner.sampleMatrixLookAndFeel.drawTableHeaderColumn(graphics, header, this->owner.roundRobins[static_cast<size_t>(rowNumber)], columnId, width, height, false, false, juce::TableHeaderComponent::visible);
        return;
    }

    const int levelIdx = columnId - LibraryArticulationEditor::velocityColumnIdBase;
    if (levelIdx < 0 || levelIdx >= static_cast<int>(this->owner.rangeInfo.levels.size())) return;
    LogicalHitKey hit;
    hit.namedVelocity = this->owner.rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity;
    hit.roundRobin = this->owner.roundRobins[static_cast<size_t>(rowNumber)];
    if (!this->owner.logicalHitExists(hit)) return;

    const bool active = this->owner.hasActiveHit && this->owner.logicalHitsMatch(hit, this->owner.activeHit);
    const bool selected = this->owner.isLogicalHitSelected(hit);
    if (active) {
        graphics.setColour(UITheme::Colours::selectionStrong.withAlpha(0.18f));
        graphics.fillRect(1, 1, width - 2, height - 2);
        graphics.setColour(UITheme::Colours::selectionStrong);
        graphics.drawRect(1, 1, width - 2, height - 2, 1);
    }
    const float circleSize = 10.0f;
    const auto circle = juce::Rectangle<float>((static_cast<float>(width) - circleSize) * 0.5f, (static_cast<float>(height) - circleSize) * 0.5f, circleSize, circleSize);
    graphics.setColour(selected ? UITheme::Colours::selectionStrong : UITheme::Colours::textMuted);
    if (selected) graphics.fillEllipse(circle);
    else graphics.drawEllipse(circle, 1.3f);
}

void LibraryArticulationEditor::SampleMatrixModel::cellClicked(int rowNumber, int columnId, const juce::MouseEvent& event) {
    juce::ignoreUnused(event);
    if (columnId == LibraryArticulationEditor::roundRobinColumnId) {
        this->owner.invertLogicalHitSelection(std::nullopt, rowNumber);
        return;
    }
    const int levelIdx = columnId - LibraryArticulationEditor::velocityColumnIdBase;
    if (levelIdx < 0) return;
    this->owner.toggleLogicalHitSelection(levelIdx, rowNumber);
}

juce::String LibraryArticulationEditor::SampleMatrixModel::getCellTooltip(int rowNumber, int columnId) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.roundRobins.size())) return {};
    if (columnId == LibraryArticulationEditor::roundRobinColumnId) return "RR " + this->owner.roundRobins[static_cast<size_t>(rowNumber)] + ": click to invert this row";
    const int levelIdx = columnId - LibraryArticulationEditor::velocityColumnIdBase;
    if (levelIdx < 0 || levelIdx >= static_cast<int>(this->owner.rangeInfo.levels.size())) return {};
    if (!this->owner.logicalHitExists(levelIdx, rowNumber)) return {};
    const auto velocity = juce::String(this->owner.rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity).paddedLeft('0', 3);
    return velocity + this->owner.roundRobins[static_cast<size_t>(rowNumber)] + ": hover to preview all mics, click to select";
}


void LibraryArticulationEditor::paint(juce::Graphics& graphics) {
    graphics.setColour(UITheme::Colours::bgPanelRaised);
    graphics.fillRoundedRectangle(this->getLocalBounds().toFloat(), 6.0f);
    graphics.setColour(UITheme::Colours::borderMid);
    graphics.drawRoundedRectangle(this->getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
    if (this->rangeBounds.isEmpty()) return;

    graphics.setColour(UITheme::Colours::bgEditor);
    graphics.fillRoundedRectangle(this->rangeBounds, 4.0f);
    if (!this->rangeInfo.libraryArticulationFound || this->rangeInfo.levels.empty()) {
        graphics.setColour(UITheme::Colours::textMuted);
        graphics.setFont(UITheme::Fonts::normal());
        graphics.drawText("Velocity ranges unavailable", this->rangeBounds.toNearestInt(), juce::Justification::centred);
        return;
    }

    int previousUpperBoundary = 0;
    for (int levelIdx = 0; levelIdx < static_cast<int>(this->rangeInfo.levels.size()); ++levelIdx) {
        const auto& level = this->rangeInfo.levels[static_cast<size_t>(levelIdx)];
        const float left = this->boundaryToX(previousUpperBoundary);
        const float right = this->boundaryToX(level.effectiveUpperBoundary);
        const auto block = juce::Rectangle<float>(left, this->rangeBounds.getY(), juce::jmax(1.0f, right - left), this->rangeBounds.getHeight());
        graphics.setColour(levelIdx % 2 == 0 ? UITheme::Colours::selectionStrong.withAlpha(0.22f) : UITheme::Colours::textAccentSoft.withAlpha(0.16f));
        graphics.fillRect(block);
        graphics.setColour(UITheme::Colours::textPrimary);
        auto textBounds = block.toNearestInt().reduced(2);
        const int namedVelocityHeight = juce::jmin(21, textBounds.getHeight());
        if (block.getWidth() >= 30.0f) {
            graphics.setFont(UITheme::Fonts::normal());
            graphics.drawText(juce::String(level.namedVelocity).paddedLeft('0', 3), textBounds.removeFromTop(namedVelocityHeight), juce::Justification::centred);
            const int lowerBoundary = previousUpperBoundary + 1;
            graphics.setFont(UITheme::Fonts::small());
            graphics.setColour(UITheme::Colours::textSecondary);
            graphics.drawText(juce::String(lowerBoundary) + "-" + juce::String(level.effectiveUpperBoundary), textBounds, juce::Justification::centred);
        }
        if (level.overridden) {
            graphics.setColour(UITheme::Colours::selectionStrong);
            graphics.fillEllipse(block.getRight() - 7.0f, block.getY() + 4.0f, 4.0f, 4.0f);
        }
        previousUpperBoundary = level.effectiveUpperBoundary;
    }

    for (int dividerIdx = 0; dividerIdx + 1 < static_cast<int>(this->rangeInfo.levels.size()); ++dividerIdx) {
        const float dividerX = this->boundaryToX(this->rangeInfo.levels[static_cast<size_t>(dividerIdx)].effectiveUpperBoundary);
        graphics.setColour(dividerIdx == this->selectedDividerIdx ? UITheme::Colours::selectionStrong : UITheme::Colours::borderStrong);
        graphics.drawVerticalLine(juce::roundToInt(dividerX), this->rangeBounds.getY() - 3.0f, this->rangeBounds.getBottom() + 3.0f);
        graphics.fillRoundedRectangle(dividerX - 3.0f, this->rangeBounds.getCentreY() - 9.0f, 6.0f, 18.0f, 2.0f);
    }
}

void LibraryArticulationEditor::resized() {
    auto area = this->getLocalBounds().reduced(12);
    auto header = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->doneButton.setBounds(header.removeFromRight(UITheme::Sizes::standardControlHeight));
    header.removeFromRight(6);
    this->titleLabel.setBounds(header);
    area.removeFromTop(2);
    this->scopeLabel.setBounds(area.removeFromTop(UITheme::Sizes::standardControlHeight));
    area.removeFromTop(5);

    auto footer = area.removeFromBottom(UITheme::Sizes::standardControlHeight);
    if (this->reloadSavedButton.isVisible()) {
        this->reloadSavedButton.setBounds(footer.removeFromRight(94));
        footer.removeFromRight(4);
        this->retrySaveButton.setBounds(footer.removeFromRight(76));
        footer.removeFromRight(6);
    } else {
        this->reloadSavedButton.setBounds({});
        this->retrySaveButton.setBounds({});
    }
    this->statusLabel.setBounds(footer);
    area.removeFromBottom(6);

    auto batchArea = area.removeFromBottom(44);
    const int batchGap = 8;
    const int batchColumnWidth = juce::jmax(1, (batchArea.getWidth() - batchGap * 2) / 3);
    auto layoutBatchField = [](juce::Rectangle<int> column, juce::Label& label, juce::TextEditor& editor, juce::TextButton& resetButton) {
        label.setBounds(column.removeFromTop(UITheme::Sizes::standardControlHeight));
        auto controlRow = column.removeFromTop(UITheme::Sizes::standardControlHeight);
        resetButton.setBounds(controlRow.removeFromRight(22));
        controlRow.removeFromRight(3);
        editor.setBounds(controlRow);
    };
    auto manualGainColumn = batchArea.removeFromLeft(batchColumnWidth);
    batchArea.removeFromLeft(batchGap);
    auto fileOffsetColumn = batchArea.removeFromLeft(batchColumnWidth);
    batchArea.removeFromLeft(batchGap);
    layoutBatchField(manualGainColumn, this->manualGainLabel, this->manualGainEditor, this->resetManualGainButton);
    layoutBatchField(fileOffsetColumn, this->fileOffsetLabel, this->fileOffsetEditor, this->resetFileOffsetButton);
    layoutBatchField(batchArea, this->micOffsetLabel, this->micOffsetEditor, this->resetMicOffsetButton);
    area.removeFromBottom(7);

    auto balanceRow = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->balanceSectionLabel.setBounds(balanceRow.removeFromLeft(64));
    const int balanceGap = 12;
    const int balanceFixedWidth = 72 + 52 + 22 + 6;
    const int nameWidth = juce::jmax(60, (balanceRow.getWidth() - balanceGap - balanceFixedWidth * 2) / 2);
    auto layoutBalance = [nameWidth](juce::Rectangle<int>& row, juce::Label& fieldLabel, juce::Label& nameLabel, juce::TextEditor& editor, juce::TextButton& resetButton) {
        fieldLabel.setBounds(row.removeFromLeft(72));
        nameLabel.setBounds(row.removeFromLeft(nameWidth));
        row.removeFromLeft(4);
        editor.setBounds(row.removeFromLeft(52));
        row.removeFromLeft(2);
        resetButton.setBounds(row.removeFromLeft(22));
    };
    layoutBalance(balanceRow, this->instrumentLabel, this->instrumentNameLabel, this->instrumentBalanceEditor, this->resetInstrumentBalanceButton);
    balanceRow.removeFromLeft(balanceGap);
    layoutBalance(balanceRow, this->articulationLabel, this->articulationNameLabel, this->articulationBalanceEditor, this->resetArticulationBalanceButton);
    area.removeFromTop(7);

    auto rangeHeader = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->resetRangesButton.setBounds(rangeHeader.removeFromRight(58));
    rangeHeader.removeFromRight(6);
    this->evenRangesButton.setBounds(rangeHeader.removeFromRight(52));
    rangeHeader.removeFromRight(6);
    this->velocityRangesLabel.setBounds(rangeHeader);
    area.removeFromTop(3);
    this->rangeBounds = area.removeFromTop(42).toFloat();
    area.removeFromTop(7);

    const int columnGap = 10;
    const int leftWidth = juce::jmax(1, (area.getWidth() - columnGap) / 2);
    auto sampleHeaders = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    auto leftHeader = sampleHeaders.removeFromLeft(leftWidth);
    sampleHeaders.removeFromLeft(columnGap);
    this->sampleOverridesLabel.setBounds(leftHeader);
    this->selectedSamplesLabel.setBounds(sampleHeaders);

    auto leftColumn = area.removeFromLeft(leftWidth);
    area.removeFromLeft(columnGap);
    auto micRow = leftColumn.removeFromBottom(UITheme::Sizes::standardControlHeight);
    leftColumn.removeFromBottom(4);
    this->sampleMatrix.setBounds(leftColumn);
    this->selectedSampleList.setBounds(area);
    this->micLabel.setBounds(micRow.removeFromLeft(34));
    if (!this->micButtons.empty()) {
        const int micGap = 3;
        const int totalGaps = micGap * (static_cast<int>(this->micButtons.size()) - 1);
        const int micButtonWidth = juce::jmax(34, (micRow.getWidth() - totalGaps) / static_cast<int>(this->micButtons.size()));
        for (auto& micButton : this->micButtons) {
            micButton->setBounds(micRow.removeFromLeft(juce::jmin(micButtonWidth, micRow.getWidth())));
            if (!micRow.isEmpty()) micRow.removeFromLeft(juce::jmin(micGap, micRow.getWidth()));
        }
    }
}

void LibraryArticulationEditor::mouseDown(const juce::MouseEvent& event) {
    auto& matrixHeader = this->sampleMatrix.getHeader();
    if (event.eventComponent == &matrixHeader || matrixHeader.isParentOf(event.eventComponent)) {
        const auto headerPosition = matrixHeader.getLocalPoint(event.eventComponent, event.getPosition());
        const int columnId = matrixHeader.getColumnIdAtX(headerPosition.x);
        if (columnId == roundRobinColumnId) this->invertLogicalHitSelection(std::nullopt);
        else if (columnId >= velocityColumnIdBase) this->invertLogicalHitSelection(columnId - velocityColumnIdBase);
        return;
    }

    const auto localPosition = event.getEventRelativeTo(this).position;
    if (!this->rangeBounds.expanded(10.0f, 4.0f).contains(localPosition)) return;
    const int dividerIdx = this->findNearestDivider(localPosition.x, 12.0f);
    if (this->rangeInfo.writable && dividerIdx >= 0) {
        this->selectedDividerIdx = dividerIdx;
        this->draggingDivider = true;
        this->clearHoverPreview();
        this->grabKeyboardFocus();
        this->updateControls();
        return;
    }
    if (this->rangeBounds.contains(localPosition)) this->previewVelocityAt(localPosition.x);
}

void LibraryArticulationEditor::mouseDrag(const juce::MouseEvent& event) {
    if (!this->draggingDivider || this->selectedDividerIdx < 0) return;
    const auto localPosition = event.getEventRelativeTo(this).position;
    const int upperBoundary = juce::jlimit(this->getSelectedMinimum(), this->getSelectedMaximum(), this->boundaryForX(localPosition.x));
    const auto& selectedLevel = this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)];
    if (upperBoundary != selectedLevel.effectiveUpperBoundary) this->applySelectedBoundary(upperBoundary, false);
}

void LibraryArticulationEditor::mouseUp(const juce::MouseEvent& event) {
    if (!this->draggingDivider) return;
    this->draggingDivider = false;
    const auto localPosition = event.getEventRelativeTo(this).position;
    if (this->previewPending && this->selectedDividerIdx >= 0) {
        const int upperBoundary = this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)].effectiveUpperBoundary;
        if (this->applySelectedBoundary(upperBoundary, true)) this->previewVelocityAt(localPosition.x);
    } else {
        this->previewVelocityAt(localPosition.x);
    }
}

void LibraryArticulationEditor::mouseEnter(const juce::MouseEvent& event) {
    this->updateHoverTarget(event);
}

void LibraryArticulationEditor::mouseMove(const juce::MouseEvent& event) {
    this->updateHoverTarget(event);
    const auto localPosition = event.getEventRelativeTo(this).position;
    const int dividerIdx = this->findNearestDivider(localPosition.x, 12.0f);
    if (this->rangeInfo.writable && dividerIdx >= 0 && this->rangeBounds.expanded(10.0f, 4.0f).contains(localPosition)) {
        this->setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        const auto& level = this->rangeInfo.levels[static_cast<size_t>(dividerIdx)];
        this->setTooltip("Drag the boundary after " + juce::String(level.namedVelocity).paddedLeft('0', 3) + ".");
    } else if (this->rangeBounds.contains(localPosition) && !this->rangeInfo.levels.empty()) {
        this->setMouseCursor(juce::MouseCursor::NormalCursor);
        const float proportion = juce::jlimit(0.0f, 1.0f, (localPosition.x - this->rangeBounds.getX()) / this->rangeBounds.getWidth());
        const int velocity = juce::jlimit(1, 127, 1 + juce::roundToInt(proportion * 126.0f));
        const int levelIdx = this->getLevelIndexForVelocity(velocity);
        const auto& level = this->rangeInfo.levels[static_cast<size_t>(levelIdx)];
        const int lowerBoundary = levelIdx == 0 ? 1 : this->rangeInfo.levels[static_cast<size_t>(levelIdx - 1)].effectiveUpperBoundary + 1;
        this->setTooltip(juce::String(level.namedVelocity).paddedLeft('0', 3) + ": current range " + juce::String(lowerBoundary) + "-" + juce::String(level.effectiveUpperBoundary) + ", filename max " + juce::String(level.filenameUpperBoundary) + ". Click to preview.");
    } else {
        this->setMouseCursor(juce::MouseCursor::NormalCursor);
        this->setTooltip({});
    }
}

void LibraryArticulationEditor::mouseExit(const juce::MouseEvent& event) {
    if (event.eventComponent != nullptr) {
        const auto localPosition = this->getLocalPoint(event.eventComponent, event.getPosition());
        bool insidePreviewTarget = this->sampleMatrix.getBounds().contains(localPosition);
        for (const auto& micButton : this->micButtons)
            if (micButton->getBounds().contains(localPosition)) insidePreviewTarget = true;
        if (!insidePreviewTarget) this->clearHoverPreview();
    } else {
        this->clearHoverPreview();
    }
    this->setMouseCursor(juce::MouseCursor::NormalCursor);
    this->setTooltip({});
}

bool LibraryArticulationEditor::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::returnKey) {
        if (this->hasLastPreview) this->replayLastPreview();
        else if (this->hasActiveHit) this->previewLogicalHit(this->activeHit, this->activeHit.namedVelocity);
        return true;
    }
    if (key == juce::KeyPress::escapeKey) {
        if (this->onDone) this->onDone();
        return true;
    }
    if (!this->rangeInfo.writable || this->selectedDividerIdx < 0 || this->selectedDividerIdx + 1 >= static_cast<int>(this->rangeInfo.levels.size())) return false;
    const bool moveLeft = key.getKeyCode() == juce::KeyPress::leftKey;
    const bool moveRight = key.getKeyCode() == juce::KeyPress::rightKey;
    if (!moveLeft && !moveRight) return false;
    const int step = key.getModifiers().isShiftDown() ? 5 : 1;
    const int direction = moveLeft ? -1 : 1;
    const int currentBoundary = this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)].effectiveUpperBoundary;
    const int nextBoundary = juce::jlimit(this->getSelectedMinimum(), this->getSelectedMaximum(), currentBoundary + direction * step);
    if (nextBoundary == currentBoundary) return true;
    if (!this->applySelectedBoundary(nextBoundary, true)) return false;
    this->previewVelocityAt(this->boundaryToX(nextBoundary));
    return true;
}
