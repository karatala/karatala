#include "SourceArticulationOverrideEditor.h"
#include "../Engine/RoutingTypes.h"
#include "UIHelpers.h"
#include "UITheme.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace SourceArticulationOverrideEditorHelpers {

static std::optional<float> parseFiniteFloat(const juce::String& text) {
    const auto trimmed = text.trim();
    if (trimmed.isEmpty()) return std::nullopt;
    const auto utf8Text = trimmed.toRawUTF8();
    char* endPtr = nullptr;
    errno = 0;
    const float value = std::strtof(utf8Text, &endPtr);
    if (errno != 0 || endPtr == utf8Text || *endPtr != '\0' || !std::isfinite(value)) return std::nullopt;
    return value;
}

static juce::String optionalBalanceText(const std::optional<int>& value, const juce::String& emptyText = {}) {
    return value.has_value() ? balanceValueToString(*value) : emptyText;
}

static juce::String floatText(float value) {
    auto text = juce::String(value, 3);
    while (text.containsChar('.') && text.endsWithChar('0')) text = text.dropLastCharacters(1);
    if (text.endsWithChar('.')) text = text.dropLastCharacters(1);
    return text;
}

static juce::String recipeStatusText(SampledSound::SourceRecipeStatus status) {
    switch (status) {
        case SampledSound::SourceRecipeStatus::Pending: return "Updating audio";
        case SampledSound::SourceRecipeStatus::Failed: return "Live update failed";
        case SampledSound::SourceRecipeStatus::Applied: return "Applied";
        case SampledSound::SourceRecipeStatus::NotLoaded: return "Not loaded in this map";
    }
    return {};
}

static juce::String logicalHitHoverKey(int namedVelocity, const juce::String& roundRobin) {
    return "hit:" + juce::String(namedVelocity) + ":" + roundRobin;
}

static juce::String micHoverKey(int namedVelocity, const juce::String& roundRobin, const juce::String& micCode) {
    return "mic:" + juce::String(namedVelocity) + ":" + roundRobin + ":" + micCode.toLowerCase();
}

} // namespace SourceArticulationOverrideEditorHelpers

void SourceArticulationOverrideEditor::WheelTextEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (!this->isEnabled() || event.mods.isAnyMouseButtonDown() || !this->onWheelStep) {
        juce::TextEditor::mouseWheelMove(event, wheel);
        return;
    }

    double wheelAmount = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? -static_cast<double>(wheel.deltaX) : static_cast<double>(wheel.deltaY);
    if (wheel.isReversed) wheelAmount = -wheelAmount;
    if (juce::approximatelyEqual(wheelAmount, 0.0)) return;
    this->onWheelStep(wheelAmount > 0.0, event.mods.isShiftDown());
}

void SourceArticulationOverrideEditor::SampleMatrixLookAndFeel::drawTableHeaderColumn(juce::Graphics& graphics, juce::TableHeaderComponent& header, const juce::String& columnName, int columnId, int width, int height, bool isMouseOver, bool isMouseDown, int columnFlags) {
    juce::ignoreUnused(columnId, columnFlags);
    const auto highlightColour = header.findColour(juce::TableHeaderComponent::highlightColourId);
    if (isMouseDown) graphics.fillAll(highlightColour);
    else if (isMouseOver) graphics.fillAll(highlightColour.withMultipliedAlpha(0.625f));
    graphics.setColour(header.findColour(juce::TableHeaderComponent::textColourId));
    graphics.setFont(UITheme::Fonts::normal().boldened());
    graphics.drawFittedText(columnName, juce::Rectangle<int>(0, 0, width, height).reduced(2, 0), juce::Justification::centred, 1);
}

SourceArticulationOverrideEditor::SourceArticulationOverrideEditor(DrumKitEngine& drumKitEngine)
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

    for (auto* button : { &this->resetInstrumentBalanceButton, &this->resetArticulationBalanceButton, &this->resetManualGainButton, &this->resetFileOffsetButton, &this->resetMicOffsetButton, &this->evenRangesButton, &this->resetRangesButton, &this->retrySaveButton, &this->reloadSavedButton, &this->doneButton }) {
        UIHelpers::configureControlButton(*button);
        this->addAndMakeVisible(*button);
    }
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
        UIHelpers::configureBorderlessTextEditor(editor);
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

SourceArticulationOverrideEditor::~SourceArticulationOverrideEditor() {
    this->stopTimer();
    this->clearHoverPreview();
    this->restoreCommittedPreview();
    this->sampleMatrix.getHeader().setLookAndFeel(nullptr);
}

int SourceArticulationOverrideEditor::SampleMatrixModel::getNumRows() {
    return static_cast<int>(this->owner.roundRobins.size());
}

void SourceArticulationOverrideEditor::SampleMatrixModel::paintRowBackground(juce::Graphics& graphics, int rowNumber, int width, int height, bool rowIsSelected) {
    juce::ignoreUnused(rowIsSelected);
    graphics.fillAll(rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd);
    graphics.setColour(UITheme::Colours::borderSubtle);
    graphics.drawHorizontalLine(height - 1, 0.0f, static_cast<float>(width));
}

void SourceArticulationOverrideEditor::SampleMatrixModel::paintCell(juce::Graphics& graphics, int rowNumber, int columnId, int width, int height, bool rowIsSelected) {
    juce::ignoreUnused(rowIsSelected);
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.roundRobins.size())) return;
    graphics.setFont(UITheme::Fonts::normal());
    if (columnId == SourceArticulationOverrideEditor::roundRobinColumnId) {
        auto& header = this->owner.sampleMatrix.getHeader();
        graphics.setColour(header.findColour(juce::TableHeaderComponent::backgroundColourId));
        graphics.fillRect(0, 0, width, height);
        graphics.setColour(header.findColour(juce::TableHeaderComponent::outlineColourId));
        graphics.fillRect(width - 1, 0, 1, height);
        graphics.fillRect(0, height - 1, width, 1);
        this->owner.sampleMatrixLookAndFeel.drawTableHeaderColumn(graphics, header, this->owner.roundRobins[static_cast<size_t>(rowNumber)], columnId, width, height, false, false, juce::TableHeaderComponent::visible);
        return;
    }

    const int levelIdx = columnId - SourceArticulationOverrideEditor::velocityColumnIdBase;
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

void SourceArticulationOverrideEditor::SampleMatrixModel::cellClicked(int rowNumber, int columnId, const juce::MouseEvent& event) {
    juce::ignoreUnused(event);
    if (columnId == SourceArticulationOverrideEditor::roundRobinColumnId) {
        this->owner.invertLogicalHitSelection(std::nullopt, rowNumber);
        return;
    }
    const int levelIdx = columnId - SourceArticulationOverrideEditor::velocityColumnIdBase;
    if (levelIdx < 0) return;
    this->owner.toggleLogicalHitSelection(levelIdx, rowNumber);
}

juce::String SourceArticulationOverrideEditor::SampleMatrixModel::getCellTooltip(int rowNumber, int columnId) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.roundRobins.size())) return {};
    if (columnId == SourceArticulationOverrideEditor::roundRobinColumnId) return "RR " + this->owner.roundRobins[static_cast<size_t>(rowNumber)] + ": click to invert this row";
    const int levelIdx = columnId - SourceArticulationOverrideEditor::velocityColumnIdBase;
    if (levelIdx < 0 || levelIdx >= static_cast<int>(this->owner.rangeInfo.levels.size())) return {};
    if (!this->owner.logicalHitExists(levelIdx, rowNumber)) return {};
    const auto velocity = juce::String(this->owner.rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity).paddedLeft('0', 3);
    return velocity + this->owner.roundRobins[static_cast<size_t>(rowNumber)] + ": hover to preview all mics, click to select";
}

int SourceArticulationOverrideEditor::SelectedSampleListModel::getNumRows() {
    return static_cast<int>(this->owner.selectedSampleIndices.size());
}

void SourceArticulationOverrideEditor::SelectedSampleListModel::paintListBoxItem(int rowNumber, juce::Graphics& graphics, int width, int height, bool rowIsSelected) {
    juce::ignoreUnused(rowIsSelected);
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.selectedSampleIndices.size())) return;
    const int sampleIdx = this->owner.selectedSampleIndices[static_cast<size_t>(rowNumber)];
    if (sampleIdx < 0 || sampleIdx >= static_cast<int>(this->owner.rangeInfo.samples.size())) return;
    const auto& sample = this->owner.rangeInfo.samples[static_cast<size_t>(sampleIdx)];
    graphics.fillAll(rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd);
    graphics.setFont(UITheme::Fonts::normal());
    graphics.setColour(sample.recipeStatus == SampledSound::SourceRecipeStatus::Failed ? UITheme::Colours::accent : UITheme::Colours::textPrimary);
    graphics.drawText(sample.fileName, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
}

juce::String SourceArticulationOverrideEditor::SelectedSampleListModel::getTooltipForRow(int rowNumber) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.selectedSampleIndices.size())) return {};
    const int sampleIdx = this->owner.selectedSampleIndices[static_cast<size_t>(rowNumber)];
    if (sampleIdx < 0 || sampleIdx >= static_cast<int>(this->owner.rangeInfo.samples.size())) return {};
    const auto& sample = this->owner.rangeInfo.samples[static_cast<size_t>(sampleIdx)];
    juce::StringArray details;
    details.add(sample.fileName);
    details.add("Mic: " + sample.micCode);
    if (sample.onsetMs.has_value()) details.add("Onset: " + SourceArticulationOverrideEditorHelpers::floatText(*sample.onsetMs) + " ms");
    if (sample.intensityDb.has_value()) details.add("Intensity: " + SourceArticulationOverrideEditorHelpers::floatText(*sample.intensityDb) + " dB");
    if (sample.truePeakDb.has_value()) details.add("Peak: " + SourceArticulationOverrideEditorHelpers::floatText(*sample.truePeakDb) + " dB");
    if (!sample.hasMetadata) details.add("Measurements unavailable");
    if (sample.overrideMatchedByContentHash) details.add("Override matched by content hash");
    if (sample.overrideContentHashAmbiguous) details.add("Editing disabled: duplicate content hash");
    details.add(SourceArticulationOverrideEditorHelpers::recipeStatusText(sample.recipeStatus));
    return details.joinIntoString("\n");
}

void SourceArticulationOverrideEditor::setSource(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const juce::String& displayTitle) {
    this->restoreCommittedPreview();
    this->clearPersistenceFailure();
    this->clearHoverPreview();
    this->titleLabel.setText(displayTitle, juce::dontSendNotification);
    this->rangeInfo = this->engine.getSourceVelocityRangeInfo(kitId, instrumentId, articulationId);
    this->committedRangeInfo = this->rangeInfo;
    this->selectedDividerIdx = this->rangeInfo.levels.size() > 1 ? 0 : -1;
    this->draggingDivider = false;
    this->previewPending = false;
    this->manualGainDirty = false;
    this->fileOffsetDirty = false;
    this->micOffsetDirty = false;
    this->hasLastPreview = false;
    this->replaceInfo(this->rangeInfo, true);
    if (!this->rangeInfo.sourceFound) this->setStatus("Source articulation was not found.", true);
    else if (!this->rangeInfo.writable) this->setStatus("Preview is available, but this kit is read-only.", true);
    else if (this->rangeInfo.levels.size() == 1) this->setStatus("Single velocity layer: MIDI 1-127. Click it to preview.");
    else this->setStatus("Hover to preview. Click cells, headers, and mics to select files.");
}

bool SourceArticulationOverrideEditor::applySelectedBoundary(int upperBoundary, bool persist) {
    if (this->selectedDividerIdx < 0 || this->selectedDividerIdx + 1 >= static_cast<int>(this->rangeInfo.levels.size())) return false;
    const int minimumBoundary = this->getSelectedMinimum();
    const int maximumBoundary = this->getSelectedMaximum();
    if (upperBoundary < minimumBoundary || upperBoundary > maximumBoundary) {
        this->setStatus("Range boundary must be " + juce::String(minimumBoundary) + "-" + juce::String(maximumBoundary) + ".", true);
        return false;
    }

    const int namedVelocity = this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)].namedVelocity;
    auto updateResult = this->engine.setSourceVelocityUpperBoundary(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, namedVelocity, upperBoundary, persist);
    if (!updateResult.ok()) {
        if (persist) {
            const int dividerIdx = this->selectedDividerIdx;
            this->showPersistenceFailure(updateResult.error(), [this, dividerIdx, upperBoundary] {
                this->selectedDividerIdx = dividerIdx;
                this->applySelectedBoundary(upperBoundary, true);
            });
        } else {
            this->setStatus(updateResult.error(), true);
        }
        return false;
    }

    this->rangeInfo = std::move(updateResult.value);
    if (persist) {
        this->clearPersistenceFailure();
        this->committedRangeInfo = this->rangeInfo;
        this->previewPending = false;
        this->setStatus("Velocity ranges saved.");
    } else {
        this->previewPending = true;
        this->setStatus("Release the divider to save.");
    }
    this->updateControls();
    return true;
}

void SourceArticulationOverrideEditor::resetAllBoundaries() {
    if (!this->rangeInfo.writable || !this->rangeInfo.sourceFound) return;
    this->restoreCommittedPreview();
    auto resetResult = this->engine.resetSourceVelocityUpperBoundaries(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw);
    if (!resetResult.ok()) {
        this->showPersistenceFailure(resetResult.error(), [this] { this->resetAllBoundaries(); });
        return;
    }
    this->clearPersistenceFailure();
    this->replaceInfo(std::move(resetResult.value));
    this->setStatus("Velocity ranges reset to filename values.");
    this->replayLastPreview();
}

void SourceArticulationOverrideEditor::evenlyDistributeRanges() {
    if (!this->rangeInfo.writable || !this->rangeInfo.sourceFound || this->rangeInfo.levels.size() < 2) return;
    this->restoreCommittedPreview();

    const int levelCount = static_cast<int>(this->rangeInfo.levels.size());
    std::vector<DrumKitEngine::SourceVelocityUpperBoundaryTarget> targets;
    targets.reserve(this->rangeInfo.levels.size() - 1);
    for (int levelIdx = 0; levelIdx + 1 < levelCount; ++levelIdx) {
        const int upperBoundary = juce::roundToInt(127.0f * static_cast<float>(levelIdx + 1) / static_cast<float>(levelCount));
        targets.push_back({ this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity, upperBoundary });
    }

    auto updateResult = this->engine.setSourceVelocityUpperBoundariesBatch(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, targets);
    if (!updateResult.ok()) {
        this->showPersistenceFailure(updateResult.error(), [this] { this->evenlyDistributeRanges(); });
        return;
    }
    this->clearPersistenceFailure();
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("Velocity ranges distributed evenly.");
    this->replayLastPreview();
}

void SourceArticulationOverrideEditor::restoreCommittedPreview() {
    if (!this->previewPending || this->selectedDividerIdx < 0 || this->selectedDividerIdx >= static_cast<int>(this->committedRangeInfo.levels.size())) return;
    const auto& committedLevel = this->committedRangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)];
    std::optional<int> committedBoundary;
    if (committedLevel.overridden) committedBoundary = committedLevel.effectiveUpperBoundary;
    this->engine.setSourceVelocityUpperBoundary(this->committedRangeInfo.kitCodeRaw, this->committedRangeInfo.instrumentCodeRaw, this->committedRangeInfo.articulationCodeRaw, committedLevel.namedVelocity, committedBoundary, false);
    this->rangeInfo = this->committedRangeInfo;
    this->previewPending = false;
    this->draggingDivider = false;
}

void SourceArticulationOverrideEditor::updateControls() {
    this->evenRangesButton.setEnabled(this->rangeInfo.sourceFound && this->rangeInfo.writable && this->rangeInfo.levels.size() > 1);
    this->resetRangesButton.setEnabled(this->rangeInfo.sourceFound && this->rangeInfo.writable && std::any_of(this->rangeInfo.levels.begin(), this->rangeInfo.levels.end(), [](const auto& level) {
        return level.overridden;
    }));
    this->updateBalanceControls();
    this->updateSelectedSamples();
    const bool reloadPending = std::any_of(this->rangeInfo.samples.begin(), this->rangeInfo.samples.end(), [](const auto& sample) {
        return sample.recipeStatus == SampledSound::SourceRecipeStatus::Pending;
    });
    if (reloadPending && !this->isTimerRunning()) this->startTimer(300);
    else if (!reloadPending && this->isTimerRunning()) this->stopTimer();
    this->sampleMatrix.repaint();
    this->repaint();
}

void SourceArticulationOverrideEditor::updateBalanceControls() {
    const bool editable = this->rangeInfo.sourceFound && this->rangeInfo.writable;
    this->instrumentNameLabel.setText(this->rangeInfo.instrumentCodeRaw, juce::dontSendNotification);
    this->articulationNameLabel.setText(this->rangeInfo.articulationCodeRaw, juce::dontSendNotification);
    this->instrumentBalanceEditor.setEnabled(editable);
    this->articulationBalanceEditor.setEnabled(editable);

    const auto instrumentEffective = SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.instrumentBalance.effectiveValue, "C0");
    const auto articulationEffective = SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.articulationBalance.effectiveValue, "C0");
    this->instrumentBalanceEditor.setTextToShowWhenEmpty(instrumentEffective, UITheme::Colours::textMuted);
    this->articulationBalanceEditor.setTextToShowWhenEmpty(articulationEffective, UITheme::Colours::textMuted);
    if (!this->instrumentBalanceEditor.hasKeyboardFocus(true)) this->instrumentBalanceEditor.setText(SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.instrumentBalance.overrideValue), false);
    if (!this->articulationBalanceEditor.hasKeyboardFocus(true)) this->articulationBalanceEditor.setText(SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.articulationBalance.overrideValue), false);
    this->resetInstrumentBalanceButton.setEnabled(editable && this->rangeInfo.instrumentBalance.overrideValue.has_value());
    this->resetArticulationBalanceButton.setEnabled(editable && this->rangeInfo.articulationBalance.overrideValue.has_value());

    const auto instrumentFilename = SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.instrumentBalance.filenameValue, "none");
    const auto articulationFilename = this->rangeInfo.articulationBalance.filenameConflict ? juce::String("conflict") : SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.articulationBalance.filenameValue, "inherit instrument");
    this->instrumentBalanceEditor.setTooltip("Folder: " + instrumentFilename + ". Current: " + instrumentEffective + ". Wheel: 4. Shift-wheel: 1.");
    this->articulationBalanceEditor.setTooltip("Folder: " + articulationFilename + ". Current: " + articulationEffective + ". Wheel: 4. Shift-wheel: 1.");
}

void SourceArticulationOverrideEditor::rebuildSampleView(bool initializeSelection) {
    const auto previousMicCodes = this->micCodes;
    this->roundRobins.clear();
    this->micCodes.clear();
    for (const auto& sample : this->rangeInfo.samples) {
        if (std::none_of(this->roundRobins.begin(), this->roundRobins.end(), [&sample](const auto& roundRobin) { return roundRobin == sample.roundRobin; })) this->roundRobins.push_back(sample.roundRobin);
        if (std::none_of(this->micCodes.begin(), this->micCodes.end(), [&sample](const auto& micCode) { return micCode.compareIgnoreCase(sample.micCode) == 0; })) this->micCodes.push_back(sample.micCode);
    }
    std::sort(this->roundRobins.begin(), this->roundRobins.end(), [](const auto& first, const auto& second) { return first.compareNatural(second) < 0; });
    std::sort(this->micCodes.begin(), this->micCodes.end(), [](const auto& first, const auto& second) { return first.compareNatural(second) < 0; });

    if (initializeSelection) {
        this->selectedHits.clear();
        this->selectedMicCodes = this->micCodes;
        this->activeHit = {};
        this->hasActiveHit = false;
    } else {
        std::erase_if(this->selectedHits, [this](const auto& hit) { return !this->logicalHitExists(hit); });
        std::erase_if(this->selectedMicCodes, [this](const auto& selectedMicCode) {
            return std::none_of(this->micCodes.begin(), this->micCodes.end(), [&selectedMicCode](const auto& micCode) { return micCode.compareIgnoreCase(selectedMicCode) == 0; });
        });
        if (this->hasActiveHit && !this->logicalHitExists(this->activeHit)) this->hasActiveHit = false;
        if (!this->hasActiveHit && !this->selectedHits.empty()) {
            this->activeHit = this->selectedHits.front();
            this->hasActiveHit = true;
        }
    }

    this->rebuildMatrixColumns();
    if (this->micCodes != previousMicCodes) this->rebuildMicButtons();
    else this->updateMicButtonStates();
    this->updateSelectedSamples();
}

void SourceArticulationOverrideEditor::rebuildMatrixColumns() {
    auto& header = this->sampleMatrix.getHeader();
    header.removeAllColumns();
    header.addColumn("*", roundRobinColumnId, 30, 30, 30, juce::TableHeaderComponent::visible);
    for (int levelIdx = 0; levelIdx < static_cast<int>(this->rangeInfo.levels.size()); ++levelIdx) {
        const auto velocityText = juce::String(this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity).paddedLeft('0', 3);
        header.addColumn(velocityText, velocityColumnIdBase + levelIdx, 30, 30, 30, juce::TableHeaderComponent::visible);
    }
    this->sampleMatrix.updateContent();
}

void SourceArticulationOverrideEditor::rebuildMicButtons() {
    this->micButtons.clear();
    for (const auto& micCode : this->micCodes) {
        auto button = std::make_unique<juce::TextButton>();
        UIHelpers::configureToggleButton(*button);
        button->setToggleState(this->isMicSelected(micCode), juce::dontSendNotification);
        button->setButtonText((this->isMicSelected(micCode) ? juce::String::charToString(0x25cf) : juce::String::charToString(0x25cb)) + " " + micCode);
        button->setTooltip("Hover to hear only " + micCode + ". Click to include or exclude it from batch editing.");
        button->onClick = [this, micCode] { this->toggleMicSelection(micCode); };
        button->addMouseListener(this, false);
        this->addAndMakeVisible(*button);
        this->micButtons.push_back(std::move(button));
    }
    this->resized();
}

void SourceArticulationOverrideEditor::updateSelectedSamples() {
    this->selectedSampleIndices.clear();
    for (int sampleIdx = 0; sampleIdx < static_cast<int>(this->rangeInfo.samples.size()); ++sampleIdx) {
        const auto& sample = this->rangeInfo.samples[static_cast<size_t>(sampleIdx)];
        const LogicalHitKey hit { sample.namedVelocity, sample.roundRobin };
        if (this->isLogicalHitSelected(hit) && this->isMicSelected(sample.micCode)) this->selectedSampleIndices.push_back(sampleIdx);
    }
    const int selectedCount = static_cast<int>(this->selectedSampleIndices.size());
    this->selectedSamplesLabel.setText("SELECTED " + juce::String(selectedCount) + (selectedCount == 1 ? " SAMPLE" : " SAMPLES"), juce::dontSendNotification);
    this->selectedSampleList.updateContent();
    this->selectedSampleList.repaint();
    this->updateBatchEditors();
    this->updateMicButtonStates();
    this->sampleMatrix.repaint();
}

void SourceArticulationOverrideEditor::updateBatchEditors() {
    const bool editable = this->selectedSamplesAreEditable();
    auto updateEditor = [this, editable](juce::TextEditor& editor, juce::TextButton& resetButton, BatchField field, bool dirty, const juce::String& inheritedText) {
        editor.setEnabled(editable);
        const auto commonValue = this->getCommonBatchValue(field);
        if (!dirty && !editor.hasKeyboardFocus(true)) {
            editor.setText(commonValue.value.has_value() && !commonValue.mixed ? SourceArticulationOverrideEditorHelpers::floatText(*commonValue.value) : juce::String(), false);
            editor.setTextToShowWhenEmpty(commonValue.mixed ? "Mixed" : inheritedText, UITheme::Colours::textMuted);
        }
        bool anyOverride = false;
        for (const int sampleIdx : this->selectedSampleIndices) {
            if (sampleIdx < 0 || sampleIdx >= static_cast<int>(this->rangeInfo.samples.size())) continue;
            const auto& sample = this->rangeInfo.samples[static_cast<size_t>(sampleIdx)];
            if ((field == BatchField::ManualGain && sample.manualGainDbOverride.has_value()) || (field == BatchField::FileOffset && sample.filePlaybackOffsetMsOverride.has_value()) || (field == BatchField::MicOffset && sample.micPlaybackOffsetMsOverride.has_value())) {
                anyOverride = true;
                break;
            }
        }
        resetButton.setEnabled(editable && anyOverride);
    };
    updateEditor(this->manualGainEditor, this->resetManualGainButton, BatchField::ManualGain, this->manualGainDirty, "Use filename");
    updateEditor(this->fileOffsetEditor, this->resetFileOffsetButton, BatchField::FileOffset, this->fileOffsetDirty, "Use filename");
    updateEditor(this->micOffsetEditor, this->resetMicOffsetButton, BatchField::MicOffset, this->micOffsetDirty, "Use file");
}

void SourceArticulationOverrideEditor::updateMicButtonStates() {
    for (int micIdx = 0; micIdx < static_cast<int>(this->micButtons.size()) && micIdx < static_cast<int>(this->micCodes.size()); ++micIdx) {
        const bool selected = this->isMicSelected(this->micCodes[static_cast<size_t>(micIdx)]);
        auto& button = *this->micButtons[static_cast<size_t>(micIdx)];
        button.setToggleState(selected, juce::dontSendNotification);
        button.setButtonText((selected ? juce::String::charToString(0x25cf) : juce::String::charToString(0x25cb)) + " " + this->micCodes[static_cast<size_t>(micIdx)]);
    }
}

void SourceArticulationOverrideEditor::commitInstrumentBalance() {
    if (!this->instrumentBalanceEditor.isEnabled()) return;
    const auto text = this->instrumentBalanceEditor.getText().trim();
    const auto parsedBalance = text.isEmpty() ? std::optional<int> {} : parseBalanceValue(text);
    if (text.isNotEmpty() && !parsedBalance.has_value()) {
        this->clearPersistenceFailure();
        this->setStatus("Instrument balance must be L1-L127, C0, or R1-R127.", true);
        this->updateBalanceControls();
        return;
    }
    if (parsedBalance == this->rangeInfo.instrumentBalance.overrideValue) return;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSourceInstrumentBalance(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, parsedBalance);
    if (!updateResult.ok()) {
        this->showPersistenceFailure(updateResult.error(), [this] { this->commitInstrumentBalance(); });
        return;
    }
    this->clearPersistenceFailure();
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("Instrument balance saved.");
    this->previewChangedValues();
}

void SourceArticulationOverrideEditor::commitArticulationBalance() {
    if (!this->articulationBalanceEditor.isEnabled()) return;
    const auto text = this->articulationBalanceEditor.getText().trim();
    const auto parsedBalance = text.isEmpty() ? std::optional<int> {} : parseBalanceValue(text);
    if (text.isNotEmpty() && !parsedBalance.has_value()) {
        this->clearPersistenceFailure();
        this->setStatus("Articulation balance must be L1-L127, C0, or R1-R127.", true);
        this->updateBalanceControls();
        return;
    }
    if (parsedBalance == this->rangeInfo.articulationBalance.overrideValue) return;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSourceArticulationBalance(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, parsedBalance);
    if (!updateResult.ok()) {
        this->showPersistenceFailure(updateResult.error(), [this] { this->commitArticulationBalance(); });
        return;
    }
    this->clearPersistenceFailure();
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("Articulation balance saved.");
    this->previewChangedValues();
}

void SourceArticulationOverrideEditor::commitManualGain() {
    if (!this->manualGainDirty || !this->manualGainEditor.isEnabled()) return;
    const auto text = this->manualGainEditor.getText().trim();
    const auto parsedValue = SourceArticulationOverrideEditorHelpers::parseFiniteFloat(text);
    if (text.isNotEmpty() && !parsedValue.has_value()) {
        this->setStatus("File gain correction must be a valid dB number.", true);
        return;
    }
    const auto relativePaths = this->getSelectedRelativePaths();
    if (relativePaths.empty()) return;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSourceSampleManualGainBatch(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, relativePaths, parsedValue);
    if (!updateResult.ok()) {
        this->showPersistenceFailure(updateResult.error(), [this] { this->commitManualGain(); });
        return;
    }
    this->clearPersistenceFailure();
    this->manualGainDirty = false;
    this->replaceInfo(std::move(updateResult.value));
    const bool pending = std::any_of(this->selectedSampleIndices.begin(), this->selectedSampleIndices.end(), [this](int sampleIdx) {
        return sampleIdx >= 0 && sampleIdx < static_cast<int>(this->rangeInfo.samples.size()) && this->rangeInfo.samples[static_cast<size_t>(sampleIdx)].recipeStatus == SampledSound::SourceRecipeStatus::Pending;
    });
    this->setStatus(pending ? "Gain saved. Updating selected audio..." : "Gain saved and applied.");
    this->previewChangedValues();
}

void SourceArticulationOverrideEditor::commitFilePlaybackOffset() {
    if (!this->fileOffsetDirty || !this->fileOffsetEditor.isEnabled()) return;
    const auto text = this->fileOffsetEditor.getText().trim();
    const auto parsedValue = SourceArticulationOverrideEditorHelpers::parseFiniteFloat(text);
    if (text.isNotEmpty() && !parsedValue.has_value()) {
        this->setStatus("File start offset must be a valid millisecond number.", true);
        return;
    }
    const auto targets = this->getSelectedPlaybackTargets(false);
    if (targets.empty()) return;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSourceSamplePlaybackOffsetBatch(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, targets, parsedValue);
    if (!updateResult.ok()) {
        this->showPersistenceFailure(updateResult.error(), [this] { this->commitFilePlaybackOffset(); });
        return;
    }
    this->clearPersistenceFailure();
    this->fileOffsetDirty = false;
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("File start offset saved.");
    this->previewChangedValues();
}

void SourceArticulationOverrideEditor::commitMicPlaybackOffset() {
    if (!this->micOffsetDirty || !this->micOffsetEditor.isEnabled()) return;
    const auto text = this->micOffsetEditor.getText().trim();
    const auto parsedValue = SourceArticulationOverrideEditorHelpers::parseFiniteFloat(text);
    if (text.isNotEmpty() && !parsedValue.has_value()) {
        this->setStatus("Mic offset must be a valid millisecond number.", true);
        return;
    }
    const auto targets = this->getSelectedPlaybackTargets(true);
    if (targets.empty()) return;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSourceSamplePlaybackOffsetBatch(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, targets, parsedValue);
    if (!updateResult.ok()) {
        this->showPersistenceFailure(updateResult.error(), [this] { this->commitMicPlaybackOffset(); });
        return;
    }
    this->clearPersistenceFailure();
    this->micOffsetDirty = false;
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("Mic offset saved.");
    this->previewChangedValues();
}

bool SourceArticulationOverrideEditor::commitPendingSampleEditors() {
    this->commitManualGain();
    if (this->manualGainDirty || this->hasPersistenceFailure()) return false;
    this->commitFilePlaybackOffset();
    if (this->fileOffsetDirty || this->hasPersistenceFailure()) return false;
    this->commitMicPlaybackOffset();
    return !this->micOffsetDirty && !this->hasPersistenceFailure();
}

void SourceArticulationOverrideEditor::showPersistenceFailure(const juce::String& errorText, std::function<void()> retryAction) {
    this->retrySaveAction = std::move(retryAction);
    this->retrySaveButton.setVisible(true);
    this->reloadSavedButton.setVisible(true);
    this->setStatus(errorText, true);
    this->resized();
}

void SourceArticulationOverrideEditor::clearPersistenceFailure() {
    const bool wasVisible = this->retrySaveButton.isVisible() || this->reloadSavedButton.isVisible();
    this->retrySaveAction = {};
    this->retrySaveButton.setVisible(false);
    this->reloadSavedButton.setVisible(false);
    if (wasVisible) this->resized();
}

bool SourceArticulationOverrideEditor::hasPersistenceFailure() const {
    return this->retrySaveButton.isVisible();
}

void SourceArticulationOverrideEditor::reloadSavedOverrides() {
    if (!this->rangeInfo.sourceFound) return;
    this->restoreCommittedPreview();
    auto reloadResult = this->engine.reloadSourceOverrides(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw);
    if (!reloadResult.ok()) {
        this->setStatus(reloadResult.error(), true);
        return;
    }
    this->manualGainDirty = false;
    this->fileOffsetDirty = false;
    this->micOffsetDirty = false;
    this->clearPersistenceFailure();
    this->replaceInfo(std::move(reloadResult.value));
    this->setStatus("Reloaded saved values.");
}

void SourceArticulationOverrideEditor::replaceInfo(DrumKitEngine::SourceVelocityRangeInfo updatedInfo, bool initializeSelection) {
    this->rangeInfo = std::move(updatedInfo);
    this->committedRangeInfo = this->rangeInfo;
    this->previewPending = false;
    this->rebuildSampleView(initializeSelection);
    this->updateControls();
}

void SourceArticulationOverrideEditor::timerCallback() {
    if (!this->rangeInfo.sourceFound || this->previewPending || this->draggingDivider) return;
    auto refreshedInfo = this->engine.getSourceVelocityRangeInfo(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw);
    const bool failed = std::any_of(refreshedInfo.samples.begin(), refreshedInfo.samples.end(), [](const auto& sample) {
        return sample.recipeStatus == SampledSound::SourceRecipeStatus::Failed;
    });
    const bool pending = std::any_of(refreshedInfo.samples.begin(), refreshedInfo.samples.end(), [](const auto& sample) {
        return sample.recipeStatus == SampledSound::SourceRecipeStatus::Pending;
    });
    this->replaceInfo(std::move(refreshedInfo));
    if (failed) this->setStatus("Saved, but a live audio update failed. Reload all kits to retry.", true);
    else if (!pending) this->setStatus("Selected gain changes are active.");
}

void SourceArticulationOverrideEditor::setStatus(const juce::String& text, bool error) {
    this->statusLabel.setText(text, juce::dontSendNotification);
    this->statusLabel.setColour(juce::Label::textColourId, error ? UITheme::Colours::accent : UITheme::Colours::textMuted);
}

bool SourceArticulationOverrideEditor::logicalHitExists(const LogicalHitKey& hit) const {
    return std::any_of(this->rangeInfo.samples.begin(), this->rangeInfo.samples.end(), [&hit](const auto& sample) {
        return sample.namedVelocity == hit.namedVelocity && sample.roundRobin == hit.roundRobin;
    });
}

bool SourceArticulationOverrideEditor::logicalHitExists(int levelIdx, int roundRobinIdx) const {
    if (levelIdx < 0 || levelIdx >= static_cast<int>(this->rangeInfo.levels.size()) || roundRobinIdx < 0 || roundRobinIdx >= static_cast<int>(this->roundRobins.size())) return false;
    return this->logicalHitExists({ this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity, this->roundRobins[static_cast<size_t>(roundRobinIdx)] });
}

bool SourceArticulationOverrideEditor::isLogicalHitSelected(const LogicalHitKey& hit) const {
    return std::any_of(this->selectedHits.begin(), this->selectedHits.end(), [this, &hit](const auto& selectedHit) { return this->logicalHitsMatch(hit, selectedHit); });
}

bool SourceArticulationOverrideEditor::logicalHitsMatch(const LogicalHitKey& first, const LogicalHitKey& second) const {
    return first.namedVelocity == second.namedVelocity && first.roundRobin == second.roundRobin;
}

void SourceArticulationOverrideEditor::toggleLogicalHitSelection(int levelIdx, int roundRobinIdx) {
    if (!this->logicalHitExists(levelIdx, roundRobinIdx) || !this->commitPendingSampleEditors()) return;
    LogicalHitKey hit { this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity, this->roundRobins[static_cast<size_t>(roundRobinIdx)] };
    const auto selectedIt = std::find_if(this->selectedHits.begin(), this->selectedHits.end(), [this, &hit](const auto& selectedHit) { return this->logicalHitsMatch(hit, selectedHit); });
    if (selectedIt == this->selectedHits.end()) this->selectedHits.push_back(hit);
    else this->selectedHits.erase(selectedIt);
    this->activeHit = hit;
    this->hasActiveHit = true;
    this->hoverPreviewKey = SourceArticulationOverrideEditorHelpers::logicalHitHoverKey(hit.namedVelocity, hit.roundRobin);
    ++this->hoverPreviewGeneration;
    this->updateSelectedSamples();
    this->previewLogicalHit(hit, hit.namedVelocity);
}

void SourceArticulationOverrideEditor::invertLogicalHitSelection(std::optional<int> levelIdx, std::optional<int> roundRobinIdx) {
    if (!this->commitPendingSampleEditors()) return;
    const int firstLevelIdx = levelIdx.value_or(0);
    const int lastLevelIdx = levelIdx.has_value() ? *levelIdx + 1 : static_cast<int>(this->rangeInfo.levels.size());
    const int firstRoundRobinIdx = roundRobinIdx.value_or(0);
    const int lastRoundRobinIdx = roundRobinIdx.has_value() ? *roundRobinIdx + 1 : static_cast<int>(this->roundRobins.size());
    if (firstLevelIdx < 0 || lastLevelIdx > static_cast<int>(this->rangeInfo.levels.size()) || firstRoundRobinIdx < 0 || lastRoundRobinIdx > static_cast<int>(this->roundRobins.size())) return;

    for (int currentLevelIdx = firstLevelIdx; currentLevelIdx < lastLevelIdx; ++currentLevelIdx) {
        for (int currentRoundRobinIdx = firstRoundRobinIdx; currentRoundRobinIdx < lastRoundRobinIdx; ++currentRoundRobinIdx) {
            if (!this->logicalHitExists(currentLevelIdx, currentRoundRobinIdx)) continue;
            LogicalHitKey hit { this->rangeInfo.levels[static_cast<size_t>(currentLevelIdx)].namedVelocity, this->roundRobins[static_cast<size_t>(currentRoundRobinIdx)] };
            const auto selectedIt = std::find_if(this->selectedHits.begin(), this->selectedHits.end(), [this, &hit](const auto& selectedHit) { return this->logicalHitsMatch(hit, selectedHit); });
            if (selectedIt == this->selectedHits.end()) this->selectedHits.push_back(hit);
            else this->selectedHits.erase(selectedIt);
        }
    }
    this->updateSelectedSamples();
}

void SourceArticulationOverrideEditor::toggleMicSelection(const juce::String& micCode) {
    if (!this->commitPendingSampleEditors()) {
        this->updateMicButtonStates();
        return;
    }
    const auto selectedIt = std::find_if(this->selectedMicCodes.begin(), this->selectedMicCodes.end(), [&micCode](const auto& selectedMicCode) { return selectedMicCode.compareIgnoreCase(micCode) == 0; });
    if (selectedIt == this->selectedMicCodes.end()) this->selectedMicCodes.push_back(micCode);
    else this->selectedMicCodes.erase(selectedIt);
    if (this->hasActiveHit) {
        this->hoverPreviewKey = SourceArticulationOverrideEditorHelpers::micHoverKey(this->activeHit.namedVelocity, this->activeHit.roundRobin, micCode);
        ++this->hoverPreviewGeneration;
    } else {
        this->clearHoverPreview();
    }
    this->updateSelectedSamples();
    if (this->hasActiveHit) this->previewMic(this->activeHit, micCode, this->activeHit.namedVelocity);
}

bool SourceArticulationOverrideEditor::isMicSelected(const juce::String& micCode) const {
    return std::any_of(this->selectedMicCodes.begin(), this->selectedMicCodes.end(), [&micCode](const auto& selectedMicCode) { return selectedMicCode.compareIgnoreCase(micCode) == 0; });
}

const DrumKitEngine::SourceSampleOverrideInfo* SourceArticulationOverrideEditor::findSample(const LogicalHitKey& hit, const juce::String& micCode) const {
    const auto sampleIt = std::find_if(this->rangeInfo.samples.begin(), this->rangeInfo.samples.end(), [&hit, &micCode](const auto& sample) {
        return sample.namedVelocity == hit.namedVelocity && sample.roundRobin == hit.roundRobin && (micCode.isEmpty() || sample.micCode.compareIgnoreCase(micCode) == 0);
    });
    return sampleIt != this->rangeInfo.samples.end() ? &*sampleIt : nullptr;
}

std::vector<juce::String> SourceArticulationOverrideEditor::getSelectedRelativePaths() const {
    juce::StringArray uniquePaths;
    for (const int sampleIdx : this->selectedSampleIndices) {
        if (sampleIdx < 0 || sampleIdx >= static_cast<int>(this->rangeInfo.samples.size())) continue;
        uniquePaths.addIfNotAlreadyThere(this->rangeInfo.samples[static_cast<size_t>(sampleIdx)].relativePath);
    }
    std::vector<juce::String> result;
    result.reserve(static_cast<size_t>(uniquePaths.size()));
    for (const auto& relativePath : uniquePaths) result.push_back(relativePath);
    return result;
}

std::vector<DrumKitEngine::SourceSamplePlaybackOffsetTarget> SourceArticulationOverrideEditor::getSelectedPlaybackTargets(bool micScoped) const {
    std::vector<DrumKitEngine::SourceSamplePlaybackOffsetTarget> result;
    juce::StringArray uniqueKeys;
    for (const int sampleIdx : this->selectedSampleIndices) {
        if (sampleIdx < 0 || sampleIdx >= static_cast<int>(this->rangeInfo.samples.size())) continue;
        const auto& sample = this->rangeInfo.samples[static_cast<size_t>(sampleIdx)];
        const auto key = sample.relativePath + "\n" + (micScoped ? sample.micCode : juce::String());
        if (uniqueKeys.contains(key)) continue;
        uniqueKeys.add(key);
        result.push_back({ sample.relativePath, micScoped ? std::optional<juce::String> { sample.micCode } : std::nullopt });
    }
    return result;
}

SourceArticulationOverrideEditor::CommonBatchValue SourceArticulationOverrideEditor::getCommonBatchValue(BatchField field) const {
    CommonBatchValue result;
    bool hasFirstValue = false;
    for (const int sampleIdx : this->selectedSampleIndices) {
        if (sampleIdx < 0 || sampleIdx >= static_cast<int>(this->rangeInfo.samples.size())) continue;
        const auto& sample = this->rangeInfo.samples[static_cast<size_t>(sampleIdx)];
        std::optional<float> sampleValue;
        if (field == BatchField::ManualGain) sampleValue = sample.manualGainDbOverride;
        else if (field == BatchField::FileOffset) sampleValue = sample.filePlaybackOffsetMsOverride;
        else sampleValue = sample.micPlaybackOffsetMsOverride;
        if (!hasFirstValue) {
            result.value = sampleValue;
            hasFirstValue = true;
        } else if (result.value != sampleValue) {
            result.value.reset();
            result.mixed = true;
            break;
        }
    }
    return result;
}

bool SourceArticulationOverrideEditor::selectedSamplesAreEditable() const {
    if (!this->rangeInfo.writable || this->selectedSampleIndices.empty()) return false;
    return std::none_of(this->selectedSampleIndices.begin(), this->selectedSampleIndices.end(), [this](int sampleIdx) {
        return sampleIdx < 0 || sampleIdx >= static_cast<int>(this->rangeInfo.samples.size()) || this->rangeInfo.samples[static_cast<size_t>(sampleIdx)].overrideContentHashAmbiguous;
    });
}

void SourceArticulationOverrideEditor::previewLogicalHit(const LogicalHitKey& hit, int playbackVelocity) {
    const auto* sample = this->findSample(hit);
    if (sample == nullptr || !this->onPreview) return;
    this->activeHit = hit;
    this->hasActiveHit = true;
    this->lastPreviewHit = hit;
    this->lastPreviewMicCode.clear();
    this->lastPreviewVelocity = juce::jlimit(1, 127, playbackVelocity);
    this->lastPreviewWasMicSolo = false;
    this->hasLastPreview = true;
    this->sampleMatrix.repaint();
    this->onPreview(this->lastPreviewVelocity, hit.namedVelocity, hit.roundRobin, sample->relativePath, {});
}

void SourceArticulationOverrideEditor::previewMic(const LogicalHitKey& hit, const juce::String& micCode, int playbackVelocity) {
    const auto* sample = this->findSample(hit, micCode);
    if (sample == nullptr || !this->onPreview) return;
    this->activeHit = hit;
    this->hasActiveHit = true;
    this->lastPreviewHit = hit;
    this->lastPreviewMicCode = sample->micCode;
    this->lastPreviewVelocity = juce::jlimit(1, 127, playbackVelocity);
    this->lastPreviewWasMicSolo = true;
    this->hasLastPreview = true;
    this->sampleMatrix.repaint();
    this->onPreview(this->lastPreviewVelocity, hit.namedVelocity, hit.roundRobin, sample->relativePath, sample->micCode);
}

void SourceArticulationOverrideEditor::previewVelocityAt(float xPosition) {
    if (this->rangeBounds.getWidth() <= 0.0f || this->rangeInfo.levels.empty()) return;
    const float proportion = juce::jlimit(0.0f, 1.0f, (xPosition - this->rangeBounds.getX()) / this->rangeBounds.getWidth());
    const int velocity = juce::jlimit(1, 127, 1 + juce::roundToInt(proportion * 126.0f));
    const int levelIdx = this->getLevelIndexForVelocity(velocity);
    if (levelIdx < 0) return;
    const int namedVelocity = this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity;
    LogicalHitKey hit { namedVelocity, this->hasActiveHit ? this->activeHit.roundRobin : juce::String() };
    if (!this->logicalHitExists(hit)) {
        const auto sampleIt = std::find_if(this->rangeInfo.samples.begin(), this->rangeInfo.samples.end(), [namedVelocity](const auto& sample) { return sample.namedVelocity == namedVelocity; });
        if (sampleIt == this->rangeInfo.samples.end()) return;
        hit.roundRobin = sampleIt->roundRobin;
    }
    this->previewLogicalHit(hit, velocity);
}

void SourceArticulationOverrideEditor::previewChangedValues() {
    if (!this->selectedSampleIndices.empty()) {
        const int sampleIdx = this->selectedSampleIndices.back();
        if (sampleIdx >= 0 && sampleIdx < static_cast<int>(this->rangeInfo.samples.size())) {
            const auto& sample = this->rangeInfo.samples[static_cast<size_t>(sampleIdx)];
            this->previewLogicalHit({ sample.namedVelocity, sample.roundRobin }, sample.namedVelocity);
            return;
        }
    }
    if (!this->selectedHits.empty()) {
        const auto& hit = this->selectedHits.back();
        this->previewLogicalHit(hit, hit.namedVelocity);
        return;
    }
    if (this->hasActiveHit) this->previewLogicalHit(this->activeHit, this->activeHit.namedVelocity);
    else if (this->hasLastPreview) this->previewLogicalHit(this->lastPreviewHit, this->lastPreviewVelocity);
}

void SourceArticulationOverrideEditor::replayLastPreview() {
    if (!this->hasLastPreview) return;
    if (this->lastPreviewWasMicSolo) this->previewMic(this->lastPreviewHit, this->lastPreviewMicCode, this->lastPreviewVelocity);
    else this->previewLogicalHit(this->lastPreviewHit, this->lastPreviewVelocity);
}

void SourceArticulationOverrideEditor::queueLogicalHitHover(const LogicalHitKey& hit) {
    const auto hoverKey = SourceArticulationOverrideEditorHelpers::logicalHitHoverKey(hit.namedVelocity, hit.roundRobin);
    if (this->hoverPreviewKey == hoverKey) return;
    this->hoverPreviewKey = hoverKey;
    const int previewGeneration = ++this->hoverPreviewGeneration;
    juce::Component::SafePointer<SourceArticulationOverrideEditor> safeEditor(this);
    juce::Timer::callAfterDelay(150, [safeEditor, previewGeneration, hit] {
        if (safeEditor == nullptr || safeEditor->hoverPreviewGeneration != previewGeneration) return;
        safeEditor->previewLogicalHit(hit, hit.namedVelocity);
    });
}

void SourceArticulationOverrideEditor::queueMicHover(const juce::String& micCode) {
    if (!this->hasActiveHit) return;
    const auto hit = this->activeHit;
    const auto hoverKey = SourceArticulationOverrideEditorHelpers::micHoverKey(hit.namedVelocity, hit.roundRobin, micCode);
    if (this->hoverPreviewKey == hoverKey) return;
    this->hoverPreviewKey = hoverKey;
    const int previewGeneration = ++this->hoverPreviewGeneration;
    juce::Component::SafePointer<SourceArticulationOverrideEditor> safeEditor(this);
    juce::Timer::callAfterDelay(150, [safeEditor, previewGeneration, hit, micCode] {
        if (safeEditor == nullptr || safeEditor->hoverPreviewGeneration != previewGeneration) return;
        safeEditor->previewMic(hit, micCode, hit.namedVelocity);
    });
}

void SourceArticulationOverrideEditor::clearHoverPreview() {
    this->hoverPreviewKey.clear();
    ++this->hoverPreviewGeneration;
}

void SourceArticulationOverrideEditor::updateHoverTarget(const juce::MouseEvent& event) {
    const auto micCode = this->getMicCodeAt(event);
    if (micCode.isNotEmpty()) {
        this->queueMicHover(micCode);
        return;
    }
    const auto matrixHit = this->getMatrixHitAt(event);
    if (matrixHit.has_value()) {
        this->queueLogicalHitHover(*matrixHit);
        return;
    }
    this->clearHoverPreview();
}

std::optional<SourceArticulationOverrideEditor::LogicalHitKey> SourceArticulationOverrideEditor::getMatrixHitAt(const juce::MouseEvent& event) const {
    if (event.eventComponent == nullptr) return std::nullopt;
    if (event.eventComponent != &this->sampleMatrix && !this->sampleMatrix.isParentOf(event.eventComponent)) return std::nullopt;
    const auto matrixPosition = this->sampleMatrix.getLocalPoint(event.eventComponent, event.getPosition());
    for (int rowIdx = 0; rowIdx < static_cast<int>(this->roundRobins.size()); ++rowIdx) {
        for (int levelIdx = 0; levelIdx < static_cast<int>(this->rangeInfo.levels.size()); ++levelIdx) {
            if (!this->sampleMatrix.getCellPosition(velocityColumnIdBase + levelIdx, rowIdx, true).contains(matrixPosition) || !this->logicalHitExists(levelIdx, rowIdx)) continue;
            return LogicalHitKey { this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity, this->roundRobins[static_cast<size_t>(rowIdx)] };
        }
    }
    return std::nullopt;
}

juce::String SourceArticulationOverrideEditor::getMicCodeAt(const juce::MouseEvent& event) const {
    if (event.eventComponent == nullptr) return {};
    for (int micIdx = 0; micIdx < static_cast<int>(this->micButtons.size()) && micIdx < static_cast<int>(this->micCodes.size()); ++micIdx) {
        const auto* button = this->micButtons[static_cast<size_t>(micIdx)].get();
        if (event.eventComponent == button || button->isParentOf(event.eventComponent)) return this->micCodes[static_cast<size_t>(micIdx)];
    }
    return {};
}

int SourceArticulationOverrideEditor::findNearestDivider(float xPosition, float maximumDistance) const {
    int nearestDividerIdx = -1;
    float nearestDistance = maximumDistance;
    for (int dividerIdx = 0; dividerIdx + 1 < static_cast<int>(this->rangeInfo.levels.size()); ++dividerIdx) {
        const float distance = std::abs(xPosition - this->boundaryToX(this->rangeInfo.levels[static_cast<size_t>(dividerIdx)].effectiveUpperBoundary));
        if (distance <= nearestDistance) {
            nearestDistance = distance;
            nearestDividerIdx = dividerIdx;
        }
    }
    return nearestDividerIdx;
}

int SourceArticulationOverrideEditor::boundaryForX(float xPosition) const {
    if (this->rangeBounds.getWidth() <= 0.0f) return 1;
    const float proportion = juce::jlimit(0.0f, 1.0f, (xPosition - this->rangeBounds.getX()) / this->rangeBounds.getWidth());
    return juce::jlimit(1, 126, juce::roundToInt(proportion * 127.0f));
}

float SourceArticulationOverrideEditor::boundaryToX(int upperBoundary) const {
    return this->rangeBounds.getX() + this->rangeBounds.getWidth() * static_cast<float>(upperBoundary) / 127.0f;
}

int SourceArticulationOverrideEditor::getSelectedMinimum() const {
    if (this->selectedDividerIdx <= 0) return 1;
    return this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx - 1)].effectiveUpperBoundary + 1;
}

int SourceArticulationOverrideEditor::getSelectedMaximum() const {
    if (this->selectedDividerIdx < 0 || this->selectedDividerIdx + 1 >= static_cast<int>(this->rangeInfo.levels.size())) return 126;
    return this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx + 1)].effectiveUpperBoundary - 1;
}

int SourceArticulationOverrideEditor::getLevelIndexForVelocity(int velocity) const {
    for (int levelIdx = 0; levelIdx < static_cast<int>(this->rangeInfo.levels.size()); ++levelIdx)
        if (velocity <= this->rangeInfo.levels[static_cast<size_t>(levelIdx)].effectiveUpperBoundary) return levelIdx;
    return this->rangeInfo.levels.empty() ? -1 : static_cast<int>(this->rangeInfo.levels.size()) - 1;
}

float SourceArticulationOverrideEditor::getBatchWheelBaseValue(BatchField field) const {
    if (this->selectedSampleIndices.empty()) return 0.0f;
    const int sampleIdx = this->selectedSampleIndices.back();
    if (sampleIdx < 0 || sampleIdx >= static_cast<int>(this->rangeInfo.samples.size())) return 0.0f;
    const auto& sample = this->rangeInfo.samples[static_cast<size_t>(sampleIdx)];
    if (field == BatchField::ManualGain) return sample.effectiveManualGainDb;
    if (field == BatchField::FileOffset) return sample.filePlaybackOffsetMsOverride.value_or(sample.filenamePlaybackOffsetMs);
    return sample.effectivePlaybackOffsetMs;
}

void SourceArticulationOverrideEditor::stepBalanceEditor(WheelTextEditor& editor, const DrumKitEngine::SourceBalanceInfo& balanceInfo, bool increase, bool fine) {
    const auto editorValue = parseBalanceValue(editor.getText().trim());
    const int currentValue = editorValue.value_or(balanceInfo.effectiveValue.value_or(0));
    const int step = fine ? 1 : 4;
    editor.setText(balanceValueToString(juce::jlimit(-127, 127, currentValue + (increase ? step : -step))), false);
}

void SourceArticulationOverrideEditor::stepBatchEditor(WheelTextEditor& editor, BatchField field, bool increase, bool fine) {
    const auto editorValue = SourceArticulationOverrideEditorHelpers::parseFiniteFloat(editor.getText().trim());
    const float currentValue = editorValue.value_or(this->getBatchWheelBaseValue(field));
    const float coarseStep = field == BatchField::ManualGain ? 0.5f : 10.0f;
    const float fineStep = field == BatchField::ManualGain ? 0.1f : 1.0f;
    const float step = fine ? fineStep : coarseStep;
    editor.setText(SourceArticulationOverrideEditorHelpers::floatText(currentValue + (increase ? step : -step)), false);
    if (field == BatchField::ManualGain) {
        this->manualGainDirty = true;
        this->commitManualGain();
    } else if (field == BatchField::FileOffset) {
        this->fileOffsetDirty = true;
        this->commitFilePlaybackOffset();
    } else {
        this->micOffsetDirty = true;
        this->commitMicPlaybackOffset();
    }
}

void SourceArticulationOverrideEditor::paint(juce::Graphics& graphics) {
    graphics.setColour(UITheme::Colours::bgPanelRaised);
    graphics.fillRoundedRectangle(this->getLocalBounds().toFloat(), 6.0f);
    graphics.setColour(UITheme::Colours::borderMid);
    graphics.drawRoundedRectangle(this->getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
    if (this->rangeBounds.isEmpty()) return;

    graphics.setColour(UITheme::Colours::bgEditor);
    graphics.fillRoundedRectangle(this->rangeBounds, 4.0f);
    if (!this->rangeInfo.sourceFound || this->rangeInfo.levels.empty()) {
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

void SourceArticulationOverrideEditor::resized() {
    auto area = this->getLocalBounds().reduced(12);
    auto header = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->doneButton.setBounds(header.removeFromRight(56));
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

void SourceArticulationOverrideEditor::mouseDown(const juce::MouseEvent& event) {
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

void SourceArticulationOverrideEditor::mouseDrag(const juce::MouseEvent& event) {
    if (!this->draggingDivider || this->selectedDividerIdx < 0) return;
    const auto localPosition = event.getEventRelativeTo(this).position;
    const int upperBoundary = juce::jlimit(this->getSelectedMinimum(), this->getSelectedMaximum(), this->boundaryForX(localPosition.x));
    const auto& selectedLevel = this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)];
    if (upperBoundary != selectedLevel.effectiveUpperBoundary) this->applySelectedBoundary(upperBoundary, false);
}

void SourceArticulationOverrideEditor::mouseUp(const juce::MouseEvent& event) {
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

void SourceArticulationOverrideEditor::mouseEnter(const juce::MouseEvent& event) {
    this->updateHoverTarget(event);
}

void SourceArticulationOverrideEditor::mouseMove(const juce::MouseEvent& event) {
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

void SourceArticulationOverrideEditor::mouseExit(const juce::MouseEvent& event) {
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

bool SourceArticulationOverrideEditor::keyPressed(const juce::KeyPress& key) {
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
