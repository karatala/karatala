#include "SourceArticulationOverrideEditor.h"
#include "UIHelpers.h"
#include "UITheme.h"

#include <algorithm>
#include <cmath>

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

    for (auto* button : { &this->resetSelectedButton, &this->resetAllButton, &this->previewButton, &this->doneButton }) {
        UIHelpers::configureControlButton(*button);
        this->addAndMakeVisible(*button);
    }

    this->boundaryEditor.setMultiLine(false);
    this->boundaryEditor.setJustification(juce::Justification::centred);
    this->boundaryEditor.setInputRestrictions(3, "0123456789");
    this->boundaryEditor.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgControl);
    this->boundaryEditor.setColour(juce::TextEditor::textColourId, UITheme::Colours::textPrimary);
    UIHelpers::configureBorderlessTextEditor(this->boundaryEditor);
    this->boundaryEditor.onReturnKey = [this] {
        this->commitBoundaryEditor();
        this->boundaryEditor.giveAwayKeyboardFocus();
    };
    this->boundaryEditor.onFocusLost = [this] { this->commitBoundaryEditor(); };
    this->addAndMakeVisible(this->boundaryEditor);

    this->resetSelectedButton.onClick = [this] { this->resetSelectedBoundary(); };
    this->resetAllButton.onClick = [this] { this->resetAllBoundaries(); };
    this->previewButton.onClick = [this] {
        if (this->rangeInfo.levels.empty()) return;
        const int previewLevelIdx = this->selectedDividerIdx >= 0 ? this->selectedDividerIdx : 0;
        if (this->onPreview) this->onPreview(this->rangeInfo.levels[static_cast<size_t>(previewLevelIdx)].effectiveUpperBoundary);
    };
    this->doneButton.onClick = [this] {
        if (this->onDone) this->onDone();
    };
}

SourceArticulationOverrideEditor::~SourceArticulationOverrideEditor() {
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
    this->previewButton.setEnabled(this->rangeInfo.sourceFound && !this->rangeInfo.levels.empty());
    this->updateReadout();
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
    area.removeFromTop(10);

    auto footer = area.removeFromBottom(UITheme::Sizes::standardControlHeight);
    this->previewButton.setBounds(footer.removeFromRight(64));
    footer.removeFromRight(6);
    this->resetAllButton.setBounds(footer.removeFromRight(92));
    footer.removeFromRight(4);
    this->resetSelectedButton.setBounds(footer.removeFromRight(102));
    this->statusLabel.setBounds(footer);
    area.removeFromBottom(6);

    auto readout = area.removeFromBottom(UITheme::Sizes::standardControlHeight);
    this->boundaryEditor.setBounds(readout.removeFromRight(52));
    readout.removeFromRight(6);
    this->readoutLabel.setBounds(readout);
    area.removeFromBottom(12);
    this->rangeBounds = area.toFloat().reduced(8.0f, 18.0f);
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
        if (!this->rangeInfo.levels.empty() && this->onPreview) {
            const int previewLevelIdx = this->selectedDividerIdx >= 0 ? this->selectedDividerIdx : 0;
            this->onPreview(this->rangeInfo.levels[static_cast<size_t>(previewLevelIdx)].effectiveUpperBoundary);
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
