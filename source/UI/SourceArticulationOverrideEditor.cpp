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

static juce::String optionalBalanceText(const std::optional<int>& value, const juce::String& emptyText) {
    return value.has_value() ? balanceValueToString(*value) : emptyText;
}

static juce::String floatText(float value) {
    auto text = juce::String(value, 3);
    while (text.containsChar('.') && text.endsWithChar('0')) text = text.dropLastCharacters(1);
    if (text.endsWithChar('.')) text = text.dropLastCharacters(1);
    return text;
}

} // namespace SourceArticulationOverrideEditorHelpers

SourceArticulationOverrideEditor::SourceArticulationOverrideEditor(DrumKitEngine& drumKitEngine)
    : engine(drumKitEngine) {
    this->setWantsKeyboardFocus(true);

    this->titleLabel.setFont(UITheme::Fonts::sectionTitle());
    this->titleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->titleLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->titleLabel);

    this->scopeLabel.setText("SOURCE KIT OVERRIDE - affects every map using this source", juce::dontSendNotification);
    this->scopeLabel.setFont(UITheme::Fonts::small());
    this->scopeLabel.setColour(juce::Label::textColourId, UITheme::Colours::accentWarning);
    this->scopeLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->scopeLabel);

    this->readoutLabel.setFont(UITheme::Fonts::normal());
    this->readoutLabel.setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
    this->readoutLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->readoutLabel);

    this->statusLabel.setFont(UITheme::Fonts::small());
    this->statusLabel.setColour(juce::Label::textColourId, UITheme::Colours::textMuted);
    this->statusLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->statusLabel);

    for (auto* label : { &this->instrumentBalanceLabel, &this->articulationBalanceLabel, &this->sampleInfoLabel, &this->manualGainLabel, &this->fileOffsetLabel, &this->micOffsetLabel }) {
        label->setFont(UITheme::Fonts::small());
        label->setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
        label->setJustificationType(juce::Justification::centredLeft);
        this->addAndMakeVisible(*label);
    }

    for (auto* button : { &this->resetInstrumentBalanceButton, &this->resetArticulationBalanceButton, &this->resetManualGainButton, &this->resetFileOffsetButton, &this->resetMicOffsetButton, &this->resetSelectedButton, &this->resetAllButton, &this->previewButton, &this->doneButton }) {
        UIHelpers::configureControlButton(*button);
        this->addAndMakeVisible(*button);
    }

    auto configureEditor = [this](juce::TextEditor& editor, int maximumLength, const juce::String& allowedCharacters) {
        editor.setMultiLine(false);
        editor.setJustification(juce::Justification::centred);
        editor.setInputRestrictions(maximumLength, allowedCharacters);
        editor.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgControl);
        editor.setColour(juce::TextEditor::textColourId, UITheme::Colours::textPrimary);
        UIHelpers::configureBorderlessTextEditor(editor);
        this->addAndMakeVisible(editor);
    };
    configureEditor(this->boundaryEditor, 3, "0123456789");
    configureEditor(this->instrumentBalanceEditor, 4, "LlCcRr0123456789");
    configureEditor(this->articulationBalanceEditor, 4, "LlCcRr0123456789");
    configureEditor(this->manualGainEditor, 12, "-+.0123456789");
    configureEditor(this->fileOffsetEditor, 12, "-+.0123456789");
    configureEditor(this->micOffsetEditor, 12, "-+.0123456789");

    this->boundaryEditor.onReturnKey = [this] {
        this->commitBoundaryEditor();
        this->boundaryEditor.giveAwayKeyboardFocus();
    };
    this->boundaryEditor.onFocusLost = [this] { this->commitBoundaryEditor(); };

    this->instrumentBalanceEditor.setTextToShowWhenEmpty("Use folder", UITheme::Colours::textMuted);
    this->articulationBalanceEditor.setTextToShowWhenEmpty("Use folder", UITheme::Colours::textMuted);
    this->manualGainEditor.setTextToShowWhenEmpty("Use filename", UITheme::Colours::textMuted);
    this->fileOffsetEditor.setTextToShowWhenEmpty("Use filename", UITheme::Colours::textMuted);
    this->micOffsetEditor.setTextToShowWhenEmpty("Use file", UITheme::Colours::textMuted);
    this->manualGainEditor.setTooltip("Whole-file source correction in dB. This updates preprocessing and calibration.");
    this->fileOffsetEditor.setTooltip("Negative milliseconds skip the start. Positive milliseconds delay the next note.");
    this->micOffsetEditor.setTooltip("Per-mic offset wins over the whole-file offset. Negative skips, positive delays.");
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

    UIHelpers::configureComboBox(this->sampleSelector);
    this->sampleSelector.setTooltip("Select a source file and mic row. Manual gain is shared by every mic in the same file.");
    this->sampleSelector.onChange = [this] {
        const int requestedSampleIdx = this->sampleSelector.getSelectedItemIndex();
        this->commitManualGain();
        this->commitFilePlaybackOffset();
        this->commitMicPlaybackOffset();
        this->selectedSampleIdx = requestedSampleIdx;
        this->sampleSelector.setSelectedItemIndex(requestedSampleIdx, juce::dontSendNotification);
        this->updateSampleControls();
    };
    this->addAndMakeVisible(this->sampleSelector);

    this->resetSelectedButton.onClick = [this] { this->resetSelectedBoundary(); };
    this->resetAllButton.onClick = [this] { this->resetAllBoundaries(); };
    this->resetInstrumentBalanceButton.onClick = [this] { this->instrumentBalanceEditor.setText({}, false); this->commitInstrumentBalance(); };
    this->resetArticulationBalanceButton.onClick = [this] { this->articulationBalanceEditor.setText({}, false); this->commitArticulationBalance(); };
    this->resetManualGainButton.onClick = [this] { this->manualGainEditor.setText({}, false); this->commitManualGain(); };
    this->resetFileOffsetButton.onClick = [this] { this->fileOffsetEditor.setText({}, false); this->commitFilePlaybackOffset(); };
    this->resetMicOffsetButton.onClick = [this] { this->micOffsetEditor.setText({}, false); this->commitMicPlaybackOffset(); };
    this->previewButton.onClick = [this] {
        if (!this->onPreview) return;
        if (this->selectedSampleIdx >= 0 && this->selectedSampleIdx < static_cast<int>(this->rangeInfo.samples.size())) {
            const auto& sample = this->rangeInfo.samples[static_cast<size_t>(this->selectedSampleIdx)];
            this->onPreview(sample.namedVelocity, sample.relativePath, sample.micCode);
            return;
        }
        if (!this->rangeInfo.levels.empty()) {
            const int previewLevelIdx = this->selectedDividerIdx >= 0 ? this->selectedDividerIdx : 0;
            this->onPreview(this->rangeInfo.levels[static_cast<size_t>(previewLevelIdx)].effectiveUpperBoundary, {}, {});
        }
    };
    this->doneButton.onClick = [this] {
        this->commitBoundaryEditor();
        this->commitInstrumentBalance();
        this->commitArticulationBalance();
        this->commitManualGain();
        this->commitFilePlaybackOffset();
        this->commitMicPlaybackOffset();
        if (this->onDone) this->onDone();
    };
}

SourceArticulationOverrideEditor::~SourceArticulationOverrideEditor() {
    this->stopTimer();
    this->restoreCommittedPreview();
}

void SourceArticulationOverrideEditor::setSource(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const juce::String& displayTitle) {
    this->restoreCommittedPreview();
    this->titleLabel.setText(displayTitle, juce::dontSendNotification);
    this->rangeInfo = this->engine.getSourceVelocityRangeInfo(kitId, instrumentId, articulationId);
    this->committedRangeInfo = this->rangeInfo;
    this->selectedDividerIdx = this->rangeInfo.levels.size() > 1 ? 0 : -1;
    this->draggingDivider = false;
    this->previewPending = false;
    this->rebuildSampleChoices();
    if (!this->rangeInfo.sourceFound) this->setStatus("Exact source articulation was not found", true);
    else if (!this->rangeInfo.writable) this->setStatus("Kit folder is read-only", true);
    else if (this->rangeInfo.levels.size() == 1) this->setStatus("Single layer: MIDI 1..127");
    else this->setStatus("Drag a divider, use arrow keys, or enter an upper boundary");
    this->updateControls();
}

bool SourceArticulationOverrideEditor::applySelectedBoundary(int upperBoundary, bool persist) {
    if (this->selectedDividerIdx < 0 || this->selectedDividerIdx + 1 >= static_cast<int>(this->rangeInfo.levels.size())) return false;
    const int minimumBoundary = this->getSelectedMinimum();
    const int maximumBoundary = this->getSelectedMaximum();
    if (upperBoundary < minimumBoundary || upperBoundary > maximumBoundary) {
        this->setStatus("Upper boundary must be " + juce::String(minimumBoundary) + ".." + juce::String(maximumBoundary), true);
        return false;
    }

    const int namedVelocity = this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)].namedVelocity;
    auto updateResult = this->engine.setSourceVelocityUpperBoundary(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, namedVelocity, upperBoundary, persist);
    if (!updateResult.ok()) {
        this->setStatus(updateResult.error(), true);
        if (persist) this->restoreCommittedPreview();
        return false;
    }

    this->rangeInfo = std::move(updateResult.value);
    if (persist) {
        this->committedRangeInfo = this->rangeInfo;
        this->previewPending = false;
        this->setStatus("Saved to kit-overrides.yml");
    } else {
        this->previewPending = true;
        this->setStatus("Live preview - release to save");
    }
    this->updateControls();
    return true;
}

void SourceArticulationOverrideEditor::commitBoundaryEditor() {
    if (!this->boundaryEditor.isEnabled() || this->selectedDividerIdx < 0) return;
    const auto text = this->boundaryEditor.getText().trim();
    if (text.isEmpty()) {
        this->updateReadout();
        return;
    }
    const int upperBoundary = text.getIntValue();
    if (!this->previewPending && upperBoundary == this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)].effectiveUpperBoundary) return;
    this->applySelectedBoundary(upperBoundary, true);
}

void SourceArticulationOverrideEditor::resetSelectedBoundary() {
    if (!this->rangeInfo.writable || this->selectedDividerIdx < 0 || this->selectedDividerIdx >= static_cast<int>(this->rangeInfo.levels.size())) return;
    this->restoreCommittedPreview();
    const int namedVelocity = this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)].namedVelocity;
    auto resetResult = this->engine.setSourceVelocityUpperBoundary(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, namedVelocity, std::nullopt, true);
    if (!resetResult.ok()) {
        this->setStatus(resetResult.error(), true);
        return;
    }
    this->rangeInfo = std::move(resetResult.value);
    this->committedRangeInfo = this->rangeInfo;
    this->setStatus("Selected boundary reset to filename default");
    this->updateControls();
}

void SourceArticulationOverrideEditor::resetAllBoundaries() {
    if (!this->rangeInfo.writable || !this->rangeInfo.sourceFound) return;
    this->restoreCommittedPreview();
    auto resetResult = this->engine.resetSourceVelocityUpperBoundaries(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw);
    if (!resetResult.ok()) {
        this->setStatus(resetResult.error(), true);
        return;
    }
    this->rangeInfo = std::move(resetResult.value);
    this->committedRangeInfo = this->rangeInfo;
    this->setStatus("All articulation ranges reset to filename defaults");
    this->updateControls();
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
    const bool hasEditableDivider = this->rangeInfo.sourceFound && this->rangeInfo.writable && this->selectedDividerIdx >= 0 && this->selectedDividerIdx + 1 < static_cast<int>(this->rangeInfo.levels.size());
    this->boundaryEditor.setEnabled(hasEditableDivider);
    this->resetSelectedButton.setEnabled(hasEditableDivider && this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)].overridden);
    this->resetAllButton.setEnabled(this->rangeInfo.sourceFound && this->rangeInfo.writable && std::any_of(this->rangeInfo.levels.begin(), this->rangeInfo.levels.end(), [](const auto& level) {
        return level.overridden;
    }));
    this->previewButton.setEnabled(this->rangeInfo.sourceFound && (!this->rangeInfo.levels.empty() || !this->rangeInfo.samples.empty()));
    this->updateReadout();
    this->updateBalanceControls();
    this->updateSampleControls();
    const bool reloadPending = std::any_of(this->rangeInfo.samples.begin(), this->rangeInfo.samples.end(), [](const auto& sample) {
        return sample.recipeStatus == SampledSound::SourceRecipeStatus::Pending;
    });
    if (reloadPending && !this->isTimerRunning()) this->startTimer(300);
    else if (!reloadPending && this->isTimerRunning()) this->stopTimer();
    this->repaint();
}

void SourceArticulationOverrideEditor::updateReadout() {
    if (!this->rangeInfo.sourceFound) {
        this->readoutLabel.setText("Source range information unavailable", juce::dontSendNotification);
        this->boundaryEditor.setText(juce::String(), false);
        return;
    }
    if (this->rangeInfo.levels.size() == 1) {
        this->readoutLabel.setText("Named velocity " + juce::String(this->rangeInfo.levels.front().namedVelocity).paddedLeft('0', 3) + " | Effective range 1..127", juce::dontSendNotification);
        this->boundaryEditor.setText(juce::String(), false);
        return;
    }
    if (this->selectedDividerIdx < 0 || this->selectedDividerIdx + 1 >= static_cast<int>(this->rangeInfo.levels.size())) return;

    const auto& level = this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)];
    const int lowerBoundary = this->selectedDividerIdx == 0 ? 1 : this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx - 1)].effectiveUpperBoundary + 1;
    juce::String text = "Named velocity " + juce::String(level.namedVelocity).paddedLeft('0', 3);
    text += " | Effective range " + juce::String(lowerBoundary) + ".." + juce::String(level.effectiveUpperBoundary);
    text += " | Filename upper " + juce::String(level.filenameUpperBoundary);
    text += level.overridden ? " | Override " + juce::String(level.effectiveUpperBoundary) : " | Override: use filename";
    this->readoutLabel.setText(text, juce::dontSendNotification);
    if (!this->boundaryEditor.hasKeyboardFocus(true)) this->boundaryEditor.setText(juce::String(level.effectiveUpperBoundary), false);
    this->resetSelectedButton.setEnabled(this->rangeInfo.writable && level.overridden);
}

void SourceArticulationOverrideEditor::updateBalanceControls() {
    const bool editable = this->rangeInfo.sourceFound && this->rangeInfo.writable;
    this->instrumentBalanceEditor.setEnabled(editable);
    this->articulationBalanceEditor.setEnabled(editable);
    const auto instrumentFilename = SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.instrumentBalance.filenameValue, "none");
    const auto instrumentEffective = SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.instrumentBalance.effectiveValue, "center");
    this->instrumentBalanceLabel.setText("Instrument source balance | Filename " + instrumentFilename + " | Effective " + instrumentEffective, juce::dontSendNotification);
    const auto articulationFilename = this->rangeInfo.articulationBalance.filenameConflict
        ? juce::String("conflict")
        : SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.articulationBalance.filenameValue, "inherit instrument");
    const auto articulationEffective = SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.articulationBalance.effectiveValue, "center");
    this->articulationBalanceLabel.setText("Articulation source balance | Filename " + articulationFilename + " | Effective " + articulationEffective, juce::dontSendNotification);
    if (!this->instrumentBalanceEditor.hasKeyboardFocus(true)) this->instrumentBalanceEditor.setText(SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.instrumentBalance.overrideValue, {}), false);
    if (!this->articulationBalanceEditor.hasKeyboardFocus(true)) this->articulationBalanceEditor.setText(SourceArticulationOverrideEditorHelpers::optionalBalanceText(this->rangeInfo.articulationBalance.overrideValue, {}), false);
    this->resetInstrumentBalanceButton.setEnabled(editable && this->rangeInfo.instrumentBalance.overrideValue.has_value());
    this->resetArticulationBalanceButton.setEnabled(editable && this->rangeInfo.articulationBalance.overrideValue.has_value());
}

void SourceArticulationOverrideEditor::rebuildSampleChoices(const juce::String& preferredPath, const juce::String& preferredMicCode) {
    this->sampleSelector.clear(juce::dontSendNotification);
    int preferredIdx = -1;
    for (int sampleIdx = 0; sampleIdx < static_cast<int>(this->rangeInfo.samples.size()); ++sampleIdx) {
        const auto& sample = this->rangeInfo.samples[static_cast<size_t>(sampleIdx)];
        const auto label = juce::String(sample.namedVelocity).paddedLeft('0', 3) + sample.roundRobin + " | " + sample.micCode + " | " + sample.fileName;
        this->sampleSelector.addItem(label, sampleIdx + 1);
        if (sample.relativePath == preferredPath && sample.micCode.compareIgnoreCase(preferredMicCode) == 0) preferredIdx = sampleIdx;
    }
    if (preferredIdx < 0 && !this->rangeInfo.samples.empty()) preferredIdx = 0;
    this->selectedSampleIdx = preferredIdx;
    this->sampleSelector.setSelectedItemIndex(preferredIdx, juce::dontSendNotification);
}

void SourceArticulationOverrideEditor::updateSampleControls() {
    const bool hasSample = this->selectedSampleIdx >= 0 && this->selectedSampleIdx < static_cast<int>(this->rangeInfo.samples.size());
    const bool editable = hasSample && this->rangeInfo.writable && !this->rangeInfo.samples[static_cast<size_t>(this->selectedSampleIdx)].overrideContentHashAmbiguous;
    this->sampleSelector.setEnabled(this->rangeInfo.sourceFound && !this->rangeInfo.samples.empty());
    for (auto* editor : { &this->manualGainEditor, &this->fileOffsetEditor, &this->micOffsetEditor }) editor->setEnabled(editable);
    if (!hasSample) {
        this->sampleInfoLabel.setText("No source sample rows", juce::dontSendNotification);
        this->manualGainLabel.setText("Whole-file manual gain", juce::dontSendNotification);
        this->fileOffsetLabel.setText("Whole-file playback offset", juce::dontSendNotification);
        this->micOffsetLabel.setText("Per-mic playback offset", juce::dontSendNotification);
        this->manualGainEditor.setText({}, false);
        this->fileOffsetEditor.setText({}, false);
        this->micOffsetEditor.setText({}, false);
        for (auto* button : { &this->resetManualGainButton, &this->resetFileOffsetButton, &this->resetMicOffsetButton }) button->setEnabled(false);
        return;
    }

    const auto& sample = this->rangeInfo.samples[static_cast<size_t>(this->selectedSampleIdx)];
    juce::String sampleStatus;
    switch (sample.recipeStatus) {
        case SampledSound::SourceRecipeStatus::Pending: sampleStatus = "updating audio"; break;
        case SampledSound::SourceRecipeStatus::Failed: sampleStatus = "live update failed"; break;
        case SampledSound::SourceRecipeStatus::Applied: sampleStatus = "applied"; break;
        case SampledSound::SourceRecipeStatus::NotLoaded: sampleStatus = "not loaded in current map"; break;
    }
    juce::String metadataText = sample.hasMetadata ? "metadata ready" : "metadata unavailable";
    if (sample.onsetMs.has_value()) metadataText += ", onset " + SourceArticulationOverrideEditorHelpers::floatText(*sample.onsetMs) + " ms";
    if (sample.intensityDb.has_value()) metadataText += ", intensity " + SourceArticulationOverrideEditorHelpers::floatText(*sample.intensityDb) + " dB";
    if (sample.truePeakDb.has_value()) metadataText += ", true peak " + SourceArticulationOverrideEditorHelpers::floatText(*sample.truePeakDb) + " dB";
    if (sample.overrideMatchedByContentHash) metadataText += ", override recovered by content hash";
    if (sample.overrideContentHashAmbiguous) metadataText += ", ambiguous override hash - editing disabled";
    const auto sampleInfoText = "Named velocity " + juce::String(sample.namedVelocity).paddedLeft('0', 3) + sample.roundRobin + " | Mic " + sample.micCode + " | " + metadataText + " | " + sampleStatus;
    this->sampleInfoLabel.setText(sampleInfoText, juce::dontSendNotification);
    this->sampleInfoLabel.setTooltip(sampleInfoText);
    this->manualGainLabel.setText("Whole-file manual gain | Filename " + SourceArticulationOverrideEditorHelpers::floatText(sample.filenameManualGainDb) + " dB | Effective " + SourceArticulationOverrideEditorHelpers::floatText(sample.effectiveManualGainDb) + " dB", juce::dontSendNotification);
    this->fileOffsetLabel.setText("Whole-file playback offset | Filename " + SourceArticulationOverrideEditorHelpers::floatText(sample.filenamePlaybackOffsetMs) + " ms", juce::dontSendNotification);
    this->micOffsetLabel.setText("Per-mic playback offset | Effective " + SourceArticulationOverrideEditorHelpers::floatText(sample.effectivePlaybackOffsetMs) + " ms", juce::dontSendNotification);
    if (!this->manualGainEditor.hasKeyboardFocus(true)) this->manualGainEditor.setText(sample.manualGainDbOverride.has_value() ? SourceArticulationOverrideEditorHelpers::floatText(*sample.manualGainDbOverride) : juce::String(), false);
    if (!this->fileOffsetEditor.hasKeyboardFocus(true)) this->fileOffsetEditor.setText(sample.filePlaybackOffsetMsOverride.has_value() ? SourceArticulationOverrideEditorHelpers::floatText(*sample.filePlaybackOffsetMsOverride) : juce::String(), false);
    if (!this->micOffsetEditor.hasKeyboardFocus(true)) this->micOffsetEditor.setText(sample.micPlaybackOffsetMsOverride.has_value() ? SourceArticulationOverrideEditorHelpers::floatText(*sample.micPlaybackOffsetMsOverride) : juce::String(), false);
    this->resetManualGainButton.setEnabled(editable && sample.manualGainDbOverride.has_value());
    this->resetFileOffsetButton.setEnabled(editable && sample.filePlaybackOffsetMsOverride.has_value());
    this->resetMicOffsetButton.setEnabled(editable && sample.micPlaybackOffsetMsOverride.has_value());
}

void SourceArticulationOverrideEditor::commitInstrumentBalance() {
    if (!this->instrumentBalanceEditor.isEnabled()) return;
    const auto text = this->instrumentBalanceEditor.getText().trim();
    const auto parsedBalance = text.isEmpty() ? std::optional<int> {} : parseBalanceValue(text);
    if (text.isNotEmpty() && !parsedBalance.has_value()) {
        this->setStatus("Instrument balance must be L1..L127, C0, or R1..R127", true);
        this->updateBalanceControls();
        return;
    }
    if (parsedBalance == this->rangeInfo.instrumentBalance.overrideValue) return;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSourceInstrumentBalance(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, parsedBalance);
    if (!updateResult.ok()) {
        this->setStatus(updateResult.error(), true);
        this->updateBalanceControls();
        return;
    }
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("Instrument source balance saved. New notes use it now.");
}

void SourceArticulationOverrideEditor::commitArticulationBalance() {
    if (!this->articulationBalanceEditor.isEnabled()) return;
    const auto text = this->articulationBalanceEditor.getText().trim();
    const auto parsedBalance = text.isEmpty() ? std::optional<int> {} : parseBalanceValue(text);
    if (text.isNotEmpty() && !parsedBalance.has_value()) {
        this->setStatus("Articulation balance must be L1..L127, C0, or R1..R127", true);
        this->updateBalanceControls();
        return;
    }
    if (parsedBalance == this->rangeInfo.articulationBalance.overrideValue) return;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSourceArticulationBalance(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, parsedBalance);
    if (!updateResult.ok()) {
        this->setStatus(updateResult.error(), true);
        this->updateBalanceControls();
        return;
    }
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("Articulation source balance saved. New notes use it now.");
}

void SourceArticulationOverrideEditor::commitManualGain() {
    if (!this->manualGainEditor.isEnabled() || this->selectedSampleIdx < 0 || this->selectedSampleIdx >= static_cast<int>(this->rangeInfo.samples.size())) return;
    const auto& selectedSample = this->rangeInfo.samples[static_cast<size_t>(this->selectedSampleIdx)];
    const auto text = this->manualGainEditor.getText().trim();
    const auto parsedValue = SourceArticulationOverrideEditorHelpers::parseFiniteFloat(text);
    if (text.isNotEmpty() && !parsedValue.has_value()) {
        this->setStatus("Manual gain must be a valid dB number", true);
        this->updateSampleControls();
        return;
    }
    if (parsedValue == selectedSample.manualGainDbOverride) return;
    const auto relativePath = selectedSample.relativePath;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSourceSampleManualGain(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, relativePath, parsedValue);
    if (!updateResult.ok()) {
        this->setStatus(updateResult.error(), true);
        this->updateSampleControls();
        return;
    }
    this->replaceInfo(std::move(updateResult.value));
    const bool pending = std::any_of(this->rangeInfo.samples.begin(), this->rangeInfo.samples.end(), [&relativePath](const auto& sample) {
        return sample.relativePath == relativePath && sample.recipeStatus == SampledSound::SourceRecipeStatus::Pending;
    });
    this->setStatus(pending ? "Manual gain saved. Updating affected sample recipes..." : "Manual gain saved and applied.");
}

void SourceArticulationOverrideEditor::commitFilePlaybackOffset() {
    if (!this->fileOffsetEditor.isEnabled() || this->selectedSampleIdx < 0 || this->selectedSampleIdx >= static_cast<int>(this->rangeInfo.samples.size())) return;
    const auto& selectedSample = this->rangeInfo.samples[static_cast<size_t>(this->selectedSampleIdx)];
    const auto text = this->fileOffsetEditor.getText().trim();
    const auto parsedValue = SourceArticulationOverrideEditorHelpers::parseFiniteFloat(text);
    if (text.isNotEmpty() && !parsedValue.has_value()) {
        this->setStatus("Playback offset must be a valid millisecond number", true);
        this->updateSampleControls();
        return;
    }
    if (parsedValue == selectedSample.filePlaybackOffsetMsOverride) return;
    const auto relativePath = selectedSample.relativePath;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSourceSamplePlaybackOffset(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, relativePath, std::nullopt, parsedValue);
    if (!updateResult.ok()) {
        this->setStatus(updateResult.error(), true);
        this->updateSampleControls();
        return;
    }
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("Whole-file playback offset saved. New notes use it now.");
}

void SourceArticulationOverrideEditor::commitMicPlaybackOffset() {
    if (!this->micOffsetEditor.isEnabled() || this->selectedSampleIdx < 0 || this->selectedSampleIdx >= static_cast<int>(this->rangeInfo.samples.size())) return;
    const auto& selectedSample = this->rangeInfo.samples[static_cast<size_t>(this->selectedSampleIdx)];
    const auto text = this->micOffsetEditor.getText().trim();
    const auto parsedValue = SourceArticulationOverrideEditorHelpers::parseFiniteFloat(text);
    if (text.isNotEmpty() && !parsedValue.has_value()) {
        this->setStatus("Mic playback offset must be a valid millisecond number", true);
        this->updateSampleControls();
        return;
    }
    if (parsedValue == selectedSample.micPlaybackOffsetMsOverride) return;
    const auto relativePath = selectedSample.relativePath;
    const auto micCode = selectedSample.micCode;
    this->restoreCommittedPreview();
    auto updateResult = this->engine.setSourceSamplePlaybackOffset(this->rangeInfo.kitCodeRaw, this->rangeInfo.instrumentCodeRaw, this->rangeInfo.articulationCodeRaw, relativePath, std::optional<juce::String> { micCode }, parsedValue);
    if (!updateResult.ok()) {
        this->setStatus(updateResult.error(), true);
        this->updateSampleControls();
        return;
    }
    this->replaceInfo(std::move(updateResult.value));
    this->setStatus("Per-mic playback offset saved. New notes use it now.");
}

void SourceArticulationOverrideEditor::replaceInfo(DrumKitEngine::SourceVelocityRangeInfo updatedInfo) {
    juce::String selectedPath;
    juce::String selectedMicCode;
    if (this->selectedSampleIdx >= 0 && this->selectedSampleIdx < static_cast<int>(this->rangeInfo.samples.size())) {
        selectedPath = this->rangeInfo.samples[static_cast<size_t>(this->selectedSampleIdx)].relativePath;
        selectedMicCode = this->rangeInfo.samples[static_cast<size_t>(this->selectedSampleIdx)].micCode;
    }
    this->rangeInfo = std::move(updatedInfo);
    this->committedRangeInfo = this->rangeInfo;
    this->previewPending = false;
    this->rebuildSampleChoices(selectedPath, selectedMicCode);
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
    if (failed) this->setStatus("Saved, but a live sample update failed. Reload All Kits to retry.", true);
    else if (!pending) this->setStatus("Manual gain is saved and active.");
}

void SourceArticulationOverrideEditor::setStatus(const juce::String& text, bool error) {
    this->statusLabel.setText(text, juce::dontSendNotification);
    this->statusLabel.setColour(juce::Label::textColourId, error ? UITheme::Colours::accent : UITheme::Colours::textMuted);
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
        graphics.drawText("Source velocity ranges unavailable", this->rangeBounds.toNearestInt(), juce::Justification::centred);
        return;
    }

    int previousUpperBoundary = 0;
    graphics.setFont(UITheme::Fonts::normal());
    for (int levelIdx = 0; levelIdx < static_cast<int>(this->rangeInfo.levels.size()); ++levelIdx) {
        const auto& level = this->rangeInfo.levels[static_cast<size_t>(levelIdx)];
        const float left = this->boundaryToX(previousUpperBoundary);
        const float right = this->boundaryToX(level.effectiveUpperBoundary);
        const auto block = juce::Rectangle<float>(left, this->rangeBounds.getY(), juce::jmax(1.0f, right - left), this->rangeBounds.getHeight());
        const auto fillColour = levelIdx % 2 == 0 ? UITheme::Colours::selectionStrong.withAlpha(0.22f) : UITheme::Colours::textAccentSoft.withAlpha(0.16f);
        graphics.setColour(fillColour);
        graphics.fillRect(block);
        graphics.setColour(levelIdx == this->selectedDividerIdx ? UITheme::Colours::textPrimary : UITheme::Colours::textSecondary);
        if (block.getWidth() >= 24.0f) graphics.drawText(juce::String(level.namedVelocity).paddedLeft('0', 3), block.toNearestInt().reduced(2), juce::Justification::centred);
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
        graphics.fillRoundedRectangle(dividerX - 3.0f, this->rangeBounds.getCentreY() - 11.0f, 6.0f, 22.0f, 2.0f);
    }

    graphics.setFont(UITheme::Fonts::small());
    graphics.setColour(UITheme::Colours::textMuted);
    graphics.drawText("1", juce::roundToInt(this->rangeBounds.getX()), juce::roundToInt(this->rangeBounds.getBottom()) + 3, 20, 14, juce::Justification::centredLeft);
    graphics.drawText("127", juce::roundToInt(this->rangeBounds.getRight()) - 24, juce::roundToInt(this->rangeBounds.getBottom()) + 3, 24, 14, juce::Justification::centredRight);
}

void SourceArticulationOverrideEditor::resized() {
    auto area = this->getLocalBounds().reduced(12);
    auto header = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->doneButton.setBounds(header.removeFromRight(54));
    header.removeFromRight(6);
    this->titleLabel.setBounds(header);
    area.removeFromTop(2);
    this->scopeLabel.setBounds(area.removeFromTop(16));
    area.removeFromTop(6);

    auto footer = area.removeFromBottom(UITheme::Sizes::standardControlHeight);
    this->previewButton.setBounds(footer.removeFromRight(64));
    footer.removeFromRight(6);
    this->resetAllButton.setBounds(footer.removeFromRight(92));
    footer.removeFromRight(4);
    this->resetSelectedButton.setBounds(footer.removeFromRight(102));
    this->statusLabel.setBounds(footer);
    area.removeFromBottom(6);

    auto layoutValueRow = [](juce::Rectangle<int> row, juce::Label& label, juce::TextEditor& editor, juce::TextButton& resetButton) {
        resetButton.setBounds(row.removeFromRight(48));
        row.removeFromRight(4);
        editor.setBounds(row.removeFromRight(82));
        row.removeFromRight(6);
        label.setBounds(row);
    };
    layoutValueRow(area.removeFromTop(UITheme::Sizes::standardControlHeight), this->instrumentBalanceLabel, this->instrumentBalanceEditor, this->resetInstrumentBalanceButton);
    area.removeFromTop(3);
    layoutValueRow(area.removeFromTop(UITheme::Sizes::standardControlHeight), this->articulationBalanceLabel, this->articulationBalanceEditor, this->resetArticulationBalanceButton);
    area.removeFromTop(7);

    const int rangeAreaHeight = juce::jlimit(72, 112, area.getHeight() / 3);
    const auto rangeArea = area.removeFromTop(rangeAreaHeight);
    this->rangeBounds = rangeArea.toFloat().reduced(8.0f, 18.0f);
    area.removeFromTop(3);
    auto readout = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->boundaryEditor.setBounds(readout.removeFromRight(52));
    readout.removeFromRight(6);
    this->readoutLabel.setBounds(readout);
    area.removeFromTop(7);

    this->sampleSelector.setBounds(area.removeFromTop(UITheme::Sizes::standardControlHeight));
    area.removeFromTop(2);
    this->sampleInfoLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(4);
    layoutValueRow(area.removeFromTop(UITheme::Sizes::standardControlHeight), this->manualGainLabel, this->manualGainEditor, this->resetManualGainButton);
    area.removeFromTop(3);
    layoutValueRow(area.removeFromTop(UITheme::Sizes::standardControlHeight), this->fileOffsetLabel, this->fileOffsetEditor, this->resetFileOffsetButton);
    area.removeFromTop(3);
    layoutValueRow(area.removeFromTop(UITheme::Sizes::standardControlHeight), this->micOffsetLabel, this->micOffsetEditor, this->resetMicOffsetButton);
}

void SourceArticulationOverrideEditor::mouseDown(const juce::MouseEvent& event) {
    if (!this->rangeInfo.writable || !this->rangeBounds.expanded(10.0f, 4.0f).contains(event.position)) return;
    const int dividerIdx = this->findNearestDivider(event.position.x, 12.0f);
    if (dividerIdx < 0) return;
    this->selectedDividerIdx = dividerIdx;
    this->draggingDivider = true;
    this->grabKeyboardFocus();
    this->updateControls();
}

void SourceArticulationOverrideEditor::mouseDrag(const juce::MouseEvent& event) {
    if (!this->draggingDivider || this->selectedDividerIdx < 0) return;
    const int upperBoundary = juce::jlimit(this->getSelectedMinimum(), this->getSelectedMaximum(), this->boundaryForX(event.position.x));
    const auto& selectedLevel = this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)];
    if (upperBoundary != selectedLevel.effectiveUpperBoundary) this->applySelectedBoundary(upperBoundary, false);
}

void SourceArticulationOverrideEditor::mouseUp(const juce::MouseEvent&) {
    if (!this->draggingDivider) return;
    this->draggingDivider = false;
    if (this->previewPending && this->selectedDividerIdx >= 0) {
        const int upperBoundary = this->rangeInfo.levels[static_cast<size_t>(this->selectedDividerIdx)].effectiveUpperBoundary;
        this->applySelectedBoundary(upperBoundary, true);
    }
}

void SourceArticulationOverrideEditor::mouseMove(const juce::MouseEvent& event) {
    const int dividerIdx = this->findNearestDivider(event.position.x, 12.0f);
    if (this->rangeInfo.writable && dividerIdx >= 0 && this->rangeBounds.expanded(10.0f, 4.0f).contains(event.position)) {
        this->setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        const auto& level = this->rangeInfo.levels[static_cast<size_t>(dividerIdx)];
        this->setTooltip("Named velocity " + juce::String(level.namedVelocity).paddedLeft('0', 3) + " upper boundary: " + juce::String(level.effectiveUpperBoundary));
    } else {
        this->setMouseCursor(juce::MouseCursor::NormalCursor);
        this->setTooltip({});
    }
}

void SourceArticulationOverrideEditor::mouseExit(const juce::MouseEvent&) {
    this->setMouseCursor(juce::MouseCursor::NormalCursor);
    this->setTooltip({});
}

bool SourceArticulationOverrideEditor::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::returnKey) {
        if (this->onPreview && this->selectedSampleIdx >= 0 && this->selectedSampleIdx < static_cast<int>(this->rangeInfo.samples.size())) {
            const auto& sample = this->rangeInfo.samples[static_cast<size_t>(this->selectedSampleIdx)];
            this->onPreview(sample.namedVelocity, sample.relativePath, sample.micCode);
        } else if (!this->rangeInfo.levels.empty() && this->onPreview) {
            const int previewLevelIdx = this->selectedDividerIdx >= 0 ? this->selectedDividerIdx : 0;
            this->onPreview(this->rangeInfo.levels[static_cast<size_t>(previewLevelIdx)].effectiveUpperBoundary, {}, {});
        }
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
    return nextBoundary == currentBoundary || this->applySelectedBoundary(nextBoundary, true);
}
