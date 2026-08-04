#include "LibraryArticulationEditor.h"
#include "../Config/Parsing.h"
#include "../Engine/RoutingTypes.h"
#include "UIHelpers.h"
#include "UITheme.h"

#include <algorithm>
#include <cmath>

namespace LibraryArticulationEditorDetail {
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
} // namespace LibraryArticulationEditorDetail

int LibraryArticulationEditor::SelectedSampleListModel::getNumRows() {
    return static_cast<int>(this->owner.selectedSampleIndices.size());
}

void LibraryArticulationEditor::SelectedSampleListModel::paintListBoxItem(int rowNumber, juce::Graphics& graphics, int width, int height, bool rowIsSelected) {
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

juce::String LibraryArticulationEditor::SelectedSampleListModel::getTooltipForRow(int rowNumber) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.selectedSampleIndices.size())) return {};
    const int sampleIdx = this->owner.selectedSampleIndices[static_cast<size_t>(rowNumber)];
    if (sampleIdx < 0 || sampleIdx >= static_cast<int>(this->owner.rangeInfo.samples.size())) return {};
    const auto& sample = this->owner.rangeInfo.samples[static_cast<size_t>(sampleIdx)];
    juce::StringArray details;
    details.add(sample.fileName);
    details.add("Mic: " + sample.micCode);
    if (sample.onsetMs.has_value()) details.add("Onset: " + LibraryArticulationEditorDetail::floatText(*sample.onsetMs) + " ms");
    if (sample.intensityDb.has_value()) details.add("Intensity: " + LibraryArticulationEditorDetail::floatText(*sample.intensityDb) + " dB");
    if (sample.truePeakDb.has_value()) details.add("Peak: " + LibraryArticulationEditorDetail::floatText(*sample.truePeakDb) + " dB");
    if (!sample.hasMetadata) details.add("Measurements unavailable");
    if (sample.overrideMatchedByContentHash) details.add("Override matched by content hash");
    if (sample.overrideContentHashAmbiguous) details.add("Editing disabled: duplicate content hash");
    details.add(LibraryArticulationEditorDetail::recipeStatusText(sample.recipeStatus));
    return details.joinIntoString("\n");
}

void LibraryArticulationEditor::setLibraryArticulation(const juce::String& libraryKitId, const juce::String& libraryInstrumentId, const juce::String& libraryArticulationId, const juce::String& displayTitle) {
    this->restoreCommittedPreview();
    this->clearPersistenceFailure();
    this->clearHoverPreview();
    this->titleLabel.setText(displayTitle, juce::dontSendNotification);
    this->rangeInfo = this->engine.getLibraryArticulationInfo(libraryKitId, libraryInstrumentId, libraryArticulationId);
    this->committedRangeInfo = this->rangeInfo;
    this->selectedDividerIdx = this->rangeInfo.levels.size() > 1 ? 0 : -1;
    this->draggingDivider = false;
    this->previewPending = false;
    this->manualGainDirty = false;
    this->fileOffsetDirty = false;
    this->micOffsetDirty = false;
    this->hasLastPreview = false;
    this->replaceInfo(this->rangeInfo, true);
    if (!this->rangeInfo.libraryArticulationFound) this->setStatus("Library articulation was not found.", true);
    else if (!this->rangeInfo.writable) this->setStatus("Preview is available, but this kit is read-only.", true);
    else if (this->rangeInfo.levels.size() == 1) this->setStatus("Single velocity level: MIDI 1-127. Click it to preview.");
    else this->setStatus("Hover to preview. Click cells, headers, and mics to select files.");
}

bool LibraryArticulationEditor::applySelectedBoundary(int upperBoundary, bool persist) {
    if (this->selectedDividerIdx < 0 || this->selectedDividerIdx + 1 >= static_cast<int>(this->rangeInfo.levels.size())) return false;
    const int minimumBoundary = this->getSelectedMinimum();
    const int maximumBoundary = this->getSelectedMaximum();
    if (upperBoundary < minimumBoundary || upperBoundary > maximumBoundary) {
        this->setStatus("Range boundary must be " + juce::String(minimumBoundary) + "-" + juce::String(maximumBoundary) + ".", true);
        return false;
    }

    const int namedVelocity = this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)].namedVelocity;
    auto updateResult = this->engine.setLibraryVelocityUpperBoundary(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, namedVelocity, upperBoundary, persist);
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

void LibraryArticulationEditor::resetAllBoundaries() {
    if (!this->rangeInfo.writable || !this->rangeInfo.libraryArticulationFound) return;
    this->restoreCommittedPreview();
    auto resetResult = this->engine.resetLibraryVelocityUpperBoundaries(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw);
    if (!resetResult.ok()) {
        this->showPersistenceFailure(resetResult.error(), [this] { this->resetAllBoundaries(); });
        return;
    }
    this->clearPersistenceFailure();
    this->replaceInfo(std::move(resetResult.value));
    this->setStatus("Velocity ranges reset to filename values.");
    this->replayLastPreview();
}

void LibraryArticulationEditor::evenlyDistributeRanges() {
    if (!this->rangeInfo.writable || !this->rangeInfo.libraryArticulationFound || this->rangeInfo.levels.size() < 2) return;
    this->restoreCommittedPreview();

    const int levelCount = static_cast<int>(this->rangeInfo.levels.size());
    std::vector<DrumKitEngine::VelocityUpperBoundaryTarget> targets;
    targets.reserve(this->rangeInfo.levels.size() - 1);
    for (int levelIdx = 0; levelIdx + 1 < levelCount; ++levelIdx) {
        const int upperBoundary = juce::roundToInt(127.0f * static_cast<float>(levelIdx + 1) / static_cast<float>(levelCount));
        targets.push_back({ this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity, upperBoundary });
    }

    auto updateResult = this->engine.setLibraryVelocityUpperBoundariesBatch(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, targets);
    if (!updateResult.ok()) {
        this->showPersistenceFailure(updateResult.error(), [this] { this->evenlyDistributeRanges(); });
        return;
    }
    this->clearPersistenceFailure();
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("Velocity ranges distributed evenly.");
    this->replayLastPreview();
}

void LibraryArticulationEditor::restoreCommittedPreview() {
    if (!this->previewPending || this->selectedDividerIdx < 0 || this->selectedDividerIdx >= static_cast<int>(this->committedRangeInfo.levels.size())) return;
    const auto& committedLevel = this->committedRangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)];
    std::optional<int> committedBoundary;
    if (committedLevel.overridden) committedBoundary = committedLevel.effectiveUpperBoundary;
    this->engine.setLibraryVelocityUpperBoundary(this->committedRangeInfo.kitCodeRaw, this->committedRangeInfo.instrumentCodeRaw, this->committedRangeInfo.articulationCodeRaw, committedLevel.namedVelocity, committedBoundary, false);
    this->rangeInfo = this->committedRangeInfo;
    this->previewPending = false;
    this->draggingDivider = false;
}

void LibraryArticulationEditor::updateControls() {
    this->evenRangesButton.setEnabled(this->rangeInfo.libraryArticulationFound && this->rangeInfo.writable && this->rangeInfo.levels.size() > 1);
    this->resetRangesButton.setEnabled(this->rangeInfo.libraryArticulationFound && this->rangeInfo.writable && std::any_of(this->rangeInfo.levels.begin(), this->rangeInfo.levels.end(), [](const auto& level) {
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

void LibraryArticulationEditor::updateBalanceControls() {
    const bool editable = this->rangeInfo.libraryArticulationFound && this->rangeInfo.writable;
    this->instrumentNameLabel.setText(this->rangeInfo.instrumentCodeRaw, juce::dontSendNotification);
    this->articulationNameLabel.setText(this->rangeInfo.articulationCodeRaw, juce::dontSendNotification);
    this->instrumentBalanceEditor.setEnabled(editable);
    this->articulationBalanceEditor.setEnabled(editable);

    const auto instrumentEffective = LibraryArticulationEditorDetail::optionalBalanceText(this->rangeInfo.instrumentBalance.effectiveValue, "C0");
    const auto articulationEffective = LibraryArticulationEditorDetail::optionalBalanceText(this->rangeInfo.articulationBalance.effectiveValue, "C0");
    this->instrumentBalanceEditor.setTextToShowWhenEmpty(instrumentEffective, UITheme::Colours::textMuted);
    this->articulationBalanceEditor.setTextToShowWhenEmpty(articulationEffective, UITheme::Colours::textMuted);
    if (!this->instrumentBalanceEditor.hasKeyboardFocus(true)) this->instrumentBalanceEditor.setText(LibraryArticulationEditorDetail::optionalBalanceText(this->rangeInfo.instrumentBalance.overrideValue), false);
    if (!this->articulationBalanceEditor.hasKeyboardFocus(true)) this->articulationBalanceEditor.setText(LibraryArticulationEditorDetail::optionalBalanceText(this->rangeInfo.articulationBalance.overrideValue), false);
    this->resetInstrumentBalanceButton.setEnabled(editable && this->rangeInfo.instrumentBalance.overrideValue.has_value());
    this->resetArticulationBalanceButton.setEnabled(editable && this->rangeInfo.articulationBalance.overrideValue.has_value());

    const auto instrumentFilename = LibraryArticulationEditorDetail::optionalBalanceText(this->rangeInfo.instrumentBalance.filenameValue, "none");
    const auto articulationFilename = this->rangeInfo.articulationBalance.filenameConflict ? juce::String("conflict") : LibraryArticulationEditorDetail::optionalBalanceText(this->rangeInfo.articulationBalance.filenameValue, "inherit instrument");
    this->instrumentBalanceEditor.setTooltip("Folder: " + instrumentFilename + ". Current: " + instrumentEffective + ". Wheel: 4. Shift-wheel: 1.");
    this->articulationBalanceEditor.setTooltip("Folder: " + articulationFilename + ". Current: " + articulationEffective + ". Wheel: 4. Shift-wheel: 1.");
}

void LibraryArticulationEditor::rebuildSampleView(bool initializeSelection) {
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

void LibraryArticulationEditor::rebuildMatrixColumns() {
    auto& header = this->sampleMatrix.getHeader();
    header.removeAllColumns();
    header.addColumn("*", roundRobinColumnId, 30, 30, 30, juce::TableHeaderComponent::visible);
    for (int levelIdx = 0; levelIdx < static_cast<int>(this->rangeInfo.levels.size()); ++levelIdx) {
        const auto velocityText = juce::String(this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity).paddedLeft('0', 3);
        header.addColumn(velocityText, velocityColumnIdBase + levelIdx, 30, 30, 30, juce::TableHeaderComponent::visible);
    }
    this->sampleMatrix.updateContent();
}

void LibraryArticulationEditor::rebuildMicButtons() {
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

void LibraryArticulationEditor::updateSelectedSamples() {
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

void LibraryArticulationEditor::updateBatchEditors() {
    const bool editable = this->selectedSamplesAreEditable();
    auto updateEditor = [this, editable](juce::TextEditor& editor, juce::TextButton& resetButton, BatchField field, bool dirty, const juce::String& inheritedText) {
        editor.setEnabled(editable);
        const auto commonValue = this->getCommonBatchValue(field);
        if (!dirty && !editor.hasKeyboardFocus(true)) {
            editor.setText(commonValue.value.has_value() && !commonValue.mixed ? LibraryArticulationEditorDetail::floatText(*commonValue.value) : juce::String(), false);
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

void LibraryArticulationEditor::updateMicButtonStates() {
    for (int micIdx = 0; micIdx < static_cast<int>(this->micButtons.size()) && micIdx < static_cast<int>(this->micCodes.size()); ++micIdx) {
        const bool selected = this->isMicSelected(this->micCodes[static_cast<size_t>(micIdx)]);
        auto& button = *this->micButtons[static_cast<size_t>(micIdx)];
        button.setToggleState(selected, juce::dontSendNotification);
        button.setButtonText((selected ? juce::String::charToString(0x25cf) : juce::String::charToString(0x25cb)) + " " + this->micCodes[static_cast<size_t>(micIdx)]);
    }
}

void LibraryArticulationEditor::commitInstrumentBalance() {
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
    auto updateResult = this->engine.setLibraryInstrumentBalance(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, parsedBalance);
    if (!updateResult.ok()) {
        this->showPersistenceFailure(updateResult.error(), [this] { this->commitInstrumentBalance(); });
        return;
    }
    this->clearPersistenceFailure();
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("Instrument balance saved.");
    this->previewChangedValues();
}

void LibraryArticulationEditor::commitArticulationBalance() {
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
    auto updateResult = this->engine.setLibraryArticulationBalance(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, parsedBalance);
    if (!updateResult.ok()) {
        this->showPersistenceFailure(updateResult.error(), [this] { this->commitArticulationBalance(); });
        return;
    }
    this->clearPersistenceFailure();
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("Articulation balance saved.");
    this->previewChangedValues();
}

void LibraryArticulationEditor::commitManualGain() {
    if (!this->manualGainDirty || !this->manualGainEditor.isEnabled()) return;
    const auto text = this->manualGainEditor.getText().trim();
    const auto parsedValue = Parsing::parseFiniteFloat(text);
    if (text.isNotEmpty() && !parsedValue.has_value()) {
        this->setStatus("File gain correction must be a valid dB number.", true);
        return;
    }
    const auto relativePaths = this->getSelectedRelativePaths();
    if (relativePaths.empty()) return;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSampleManualGainBatch(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, relativePaths, parsedValue);
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

void LibraryArticulationEditor::commitFilePlaybackOffset() {
    if (!this->fileOffsetDirty || !this->fileOffsetEditor.isEnabled()) return;
    const auto text = this->fileOffsetEditor.getText().trim();
    const auto parsedValue = Parsing::parseFiniteFloat(text);
    if (text.isNotEmpty() && !parsedValue.has_value()) {
        this->setStatus("File start offset must be a valid millisecond number.", true);
        return;
    }
    const auto targets = this->getSelectedPlaybackTargets(false);
    if (targets.empty()) return;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSamplePlaybackOffsetBatch(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, targets, parsedValue);
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

void LibraryArticulationEditor::commitMicPlaybackOffset() {
    if (!this->micOffsetDirty || !this->micOffsetEditor.isEnabled()) return;
    const auto text = this->micOffsetEditor.getText().trim();
    const auto parsedValue = Parsing::parseFiniteFloat(text);
    if (text.isNotEmpty() && !parsedValue.has_value()) {
        this->setStatus("Mic offset must be a valid millisecond number.", true);
        return;
    }
    const auto targets = this->getSelectedPlaybackTargets(true);
    if (targets.empty()) return;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSamplePlaybackOffsetBatch(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, targets, parsedValue);
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

bool LibraryArticulationEditor::commitPendingSampleEditors() {
    this->commitManualGain();
    if (this->manualGainDirty || this->hasPersistenceFailure()) return false;
    this->commitFilePlaybackOffset();
    if (this->fileOffsetDirty || this->hasPersistenceFailure()) return false;
    this->commitMicPlaybackOffset();
    return !this->micOffsetDirty && !this->hasPersistenceFailure();
}

void LibraryArticulationEditor::showPersistenceFailure(const juce::String& errorText, std::function<void()> retryAction) {
    this->retrySaveAction = std::move(retryAction);
    this->retrySaveButton.setVisible(true);
    this->reloadSavedButton.setVisible(true);
    this->setStatus(errorText, true);
    this->resized();
}

void LibraryArticulationEditor::clearPersistenceFailure() {
    const bool wasVisible = this->retrySaveButton.isVisible() || this->reloadSavedButton.isVisible();
    this->retrySaveAction = {};
    this->retrySaveButton.setVisible(false);
    this->reloadSavedButton.setVisible(false);
    if (wasVisible) this->resized();
}

bool LibraryArticulationEditor::hasPersistenceFailure() const {
    return this->retrySaveButton.isVisible();
}

void LibraryArticulationEditor::reloadSavedOverrides() {
    if (!this->rangeInfo.libraryArticulationFound) return;
    this->restoreCommittedPreview();
    auto reloadResult = this->engine.reloadLibraryKitOverrides(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw);
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

void LibraryArticulationEditor::replaceInfo(DrumKitEngine::LibraryArticulationInfo updatedInfo, bool initializeSelection) {
    this->rangeInfo = std::move(updatedInfo);
    this->committedRangeInfo = this->rangeInfo;
    this->previewPending = false;
    this->rebuildSampleView(initializeSelection);
    this->updateControls();
}

void LibraryArticulationEditor::timerCallback() {
    if (!this->rangeInfo.libraryArticulationFound || this->previewPending || this->draggingDivider) return;
    auto refreshedInfo = this->engine.getLibraryArticulationInfo(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw);
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

void LibraryArticulationEditor::setStatus(const juce::String& text, bool error) {
    this->statusLabel.setText(text, juce::dontSendNotification);
    this->statusLabel.setColour(juce::Label::textColourId, error ? UITheme::Colours::accent : UITheme::Colours::textMuted);
}

bool LibraryArticulationEditor::logicalHitExists(const LogicalHitKey& hit) const {
    return std::any_of(this->rangeInfo.samples.begin(), this->rangeInfo.samples.end(), [&hit](const auto& sample) {
        return sample.namedVelocity == hit.namedVelocity && sample.roundRobin == hit.roundRobin;
    });
}

bool LibraryArticulationEditor::logicalHitExists(int levelIdx, int roundRobinIdx) const {
    if (levelIdx < 0 || levelIdx >= static_cast<int>(this->rangeInfo.levels.size()) || roundRobinIdx < 0 || roundRobinIdx >= static_cast<int>(this->roundRobins.size())) return false;
    return this->logicalHitExists({ this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity, this->roundRobins[static_cast<size_t>(roundRobinIdx)] });
}

bool LibraryArticulationEditor::isLogicalHitSelected(const LogicalHitKey& hit) const {
    return std::any_of(this->selectedHits.begin(), this->selectedHits.end(), [this, &hit](const auto& selectedHit) { return this->logicalHitsMatch(hit, selectedHit); });
}

bool LibraryArticulationEditor::logicalHitsMatch(const LogicalHitKey& first, const LogicalHitKey& second) const {
    return first.namedVelocity == second.namedVelocity && first.roundRobin == second.roundRobin;
}

const DrumKitEngine::LibrarySampleInfo* LibraryArticulationEditor::findSample(const LogicalHitKey& hit, const juce::String& micCode) const {
    const auto sampleIt = std::find_if(this->rangeInfo.samples.begin(), this->rangeInfo.samples.end(), [&hit, &micCode](const auto& sample) {
        return sample.namedVelocity == hit.namedVelocity && sample.roundRobin == hit.roundRobin && (micCode.isEmpty() || sample.micCode.compareIgnoreCase(micCode) == 0);
    });
    return sampleIt != this->rangeInfo.samples.end() ? &*sampleIt : nullptr;
}

std::vector<juce::String> LibraryArticulationEditor::getSelectedRelativePaths() const {
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

std::vector<DrumKitEngine::SamplePlaybackOffsetTarget> LibraryArticulationEditor::getSelectedPlaybackTargets(bool micScoped) const {
    std::vector<DrumKitEngine::SamplePlaybackOffsetTarget> result;
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

LibraryArticulationEditor::CommonBatchValue LibraryArticulationEditor::getCommonBatchValue(BatchField field) const {
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

bool LibraryArticulationEditor::selectedSamplesAreEditable() const {
    if (!this->rangeInfo.writable || this->selectedSampleIndices.empty()) return false;
    return std::none_of(this->selectedSampleIndices.begin(), this->selectedSampleIndices.end(), [this](int sampleIdx) {
        return sampleIdx < 0 || sampleIdx >= static_cast<int>(this->rangeInfo.samples.size()) || this->rangeInfo.samples[static_cast<size_t>(sampleIdx)].overrideContentHashAmbiguous;
    });
}


int LibraryArticulationEditor::findNearestDivider(float xPosition, float maximumDistance) const {
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

int LibraryArticulationEditor::boundaryForX(float xPosition) const {
    if (this->rangeBounds.getWidth() <= 0.0f) return 1;
    const float proportion = juce::jlimit(0.0f, 1.0f, (xPosition - this->rangeBounds.getX()) / this->rangeBounds.getWidth());
    return juce::jlimit(1, 126, juce::roundToInt(proportion * 127.0f));
}

float LibraryArticulationEditor::boundaryToX(int upperBoundary) const {
    return this->rangeBounds.getX() + this->rangeBounds.getWidth() * static_cast<float>(upperBoundary) / 127.0f;
}

int LibraryArticulationEditor::getSelectedMinimum() const {
    if (this->selectedDividerIdx <= 0) return 1;
    return this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx - 1)].effectiveUpperBoundary + 1;
}

int LibraryArticulationEditor::getSelectedMaximum() const {
    if (this->selectedDividerIdx < 0 || this->selectedDividerIdx + 1 >= static_cast<int>(this->rangeInfo.levels.size())) return 126;
    return this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx + 1)].effectiveUpperBoundary - 1;
}

int LibraryArticulationEditor::getLevelIndexForVelocity(int velocity) const {
    for (int levelIdx = 0; levelIdx < static_cast<int>(this->rangeInfo.levels.size()); ++levelIdx)
        if (velocity <= this->rangeInfo.levels[static_cast<size_t>(levelIdx)].effectiveUpperBoundary) return levelIdx;
    return this->rangeInfo.levels.empty() ? -1 : static_cast<int>(this->rangeInfo.levels.size()) - 1;
}

float LibraryArticulationEditor::getBatchWheelBaseValue(BatchField field) const {
    if (this->selectedSampleIndices.empty()) return 0.0f;
    const int sampleIdx = this->selectedSampleIndices.back();
    if (sampleIdx < 0 || sampleIdx >= static_cast<int>(this->rangeInfo.samples.size())) return 0.0f;
    const auto& sample = this->rangeInfo.samples[static_cast<size_t>(sampleIdx)];
    if (field == BatchField::ManualGain) return sample.effectiveManualGainDb;
    if (field == BatchField::FileOffset) return sample.filePlaybackOffsetMsOverride.value_or(sample.filenamePlaybackOffsetMs);
    return sample.effectivePlaybackOffsetMs;
}

void LibraryArticulationEditor::stepBalanceEditor(WheelTextEditor& editor, const DrumKitEngine::PlacementBalanceInfo& balanceInfo, bool increase, bool fine) {
    const auto editorValue = parseBalanceValue(editor.getText().trim());
    const int currentValue = editorValue.value_or(balanceInfo.effectiveValue.value_or(0));
    const int step = fine ? 1 : 4;
    editor.setText(balanceValueToString(juce::jlimit(-127, 127, currentValue + (increase ? step : -step))), false);
}

void LibraryArticulationEditor::stepBatchEditor(WheelTextEditor& editor, BatchField field, bool increase, bool fine) {
    const auto editorValue = Parsing::parseFiniteFloat(editor.getText());
    const float currentValue = editorValue.value_or(this->getBatchWheelBaseValue(field));
    const float coarseStep = field == BatchField::ManualGain ? 0.5f : 10.0f;
    const float fineStep = field == BatchField::ManualGain ? 0.1f : 1.0f;
    const float step = fine ? fineStep : coarseStep;
    editor.setText(LibraryArticulationEditorDetail::floatText(currentValue + (increase ? step : -step)), false);
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
