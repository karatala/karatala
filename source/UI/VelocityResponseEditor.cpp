#include "VelocityResponseEditor.h"
#include "UIHelpers.h"
#include "UITheme.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>

VelocityResponseEditor::VelocityResponseEditor(DrumKitEngine& drumKitEngine)
    : engine(drumKitEngine) {
    this->setWantsKeyboardFocus(true);

    this->titleLabel.setFont(UITheme::Fonts::sectionTitle());
    this->titleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->titleLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->titleLabel);

    this->descriptionLabel.setText("Controls how MIDI velocity changes this layer's output gain. It does not change sample selection.", juce::dontSendNotification);
    this->descriptionLabel.setFont(UITheme::Fonts::normal());
    this->descriptionLabel.setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
    this->descriptionLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->descriptionLabel);

    this->readoutLabel.setFont(UITheme::Fonts::normal());
    this->readoutLabel.setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
    this->readoutLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->readoutLabel);

    this->statusLabel.setFont(UITheme::Fonts::small());
    this->statusLabel.setColour(juce::Label::textColourId, UITheme::Colours::textMuted);
    this->statusLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->statusLabel);

    UIHelpers::configureComboBox(this->presetCombo);
    this->presetCombo.setTextWhenNothingSelected("Edited Layer Curve*");
    this->presetCombo.onChange = [this] { this->selectPreset(); };
    this->addAndMakeVisible(this->presetCombo);

    UIHelpers::configureControlButton(this->saveAsButton);
    this->addAndMakeVisible(this->saveAsButton);
    UIHelpers::configureDialogCloseButton(this->doneButton);
    this->addAndMakeVisible(this->doneButton);

    this->dbEditor.setMultiLine(false);
    this->dbEditor.setJustification(juce::Justification::centred);
    this->dbEditor.setInputRestrictions(0, "-0123456789.");
    this->dbEditor.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgControl);
    this->dbEditor.setColour(juce::TextEditor::textColourId, UITheme::Colours::textPrimary);
    UIHelpers::configureBorderlessTextEditor(this->dbEditor);
    this->dbEditor.onReturnKey = [this] {
        this->commitDbEditor();
        this->dbEditor.giveAwayKeyboardFocus();
    };
    this->dbEditor.onFocusLost = [this] { this->commitDbEditor(); };
    this->addAndMakeVisible(this->dbEditor);

    this->saveAsButton.onClick = [this] { this->showSaveAsDialog(); };
    this->doneButton.onClick = [this] {
        if (this->onDone) this->onDone();
    };
    this->setStatus({});
}

void VelocityResponseEditor::setLayerData(LayerData data) {
    this->layerData = std::move(data);
    std::sort(this->layerData.sourceInfo.levels.begin(), this->layerData.sourceInfo.levels.end(), [](const auto& leftLevel, const auto& rightLevel) {
        return leftLevel.velocity < rightLevel.velocity;
    });
    this->custom = this->layerData.customDefinition.has_value();
    this->titleLabel.setText(this->layerData.title, juce::dontSendNotification);
    const auto definition = this->layerData.customDefinition.value_or(this->engine.getVelocityResponse().getDefinition());
    this->loadDefinition(definition, false, false);
    this->refreshPresets();
    this->updateControls();
}

void VelocityResponseEditor::loadDefinition(const juce::String& definition, bool notify, bool commit) {
    auto parsed = VelocityResponse::fromDefinition(definition);
    if (!parsed.ok()) {
        this->setStatus(parsed.error(), true);
        return;
    }

    this->currentResponse = std::move(parsed.value);
    this->currentDefinition = this->currentResponse.getDefinition();
    this->rebuildHandles();
    this->updatePresetSelection();
    this->updateReadout();
    this->repaint();
    if (notify) this->notifyResponseChanged(commit);
}

void VelocityResponseEditor::rebuildHandles() {
    int selectedVelocity = 127;
    if (this->selectedHandleIdx >= 0 && this->selectedHandleIdx < static_cast<int>(this->handles.size()))
        selectedVelocity = this->handles[static_cast<size_t>(this->selectedHandleIdx)].velocity;

    std::set<int> velocities { 1, 127 };
    for (const auto& point : this->currentResponse.getPoints()) velocities.insert(point.velocity);
    for (const auto& level : this->layerData.sourceInfo.levels) velocities.insert(level.velocity);

    this->handles.clear();
    this->handles.reserve(velocities.size());
    for (const int velocity : velocities) {
        Handle handle;
        handle.velocity = velocity;
        handle.targetDb = this->currentResponse.getTargetDb(static_cast<juce::uint8>(velocity));
        handle.recordedLevel = this->findLevel(velocity) != nullptr;
        this->handles.push_back(handle);
    }

    this->selectedHandleIdx = 0;
    int closestDistance = std::numeric_limits<int>::max();
    for (int handleIdx = 0; handleIdx < static_cast<int>(this->handles.size()); ++handleIdx) {
        const int distance = std::abs(this->handles[static_cast<size_t>(handleIdx)].velocity - selectedVelocity);
        if (distance < closestDistance) {
            closestDistance = distance;
            this->selectedHandleIdx = handleIdx;
        }
    }
    this->updateViewMinimum();
}

void VelocityResponseEditor::updateViewMinimum() {
    float minimumDb = -48.0f;
    for (const auto& handle : this->handles) minimumDb = juce::jmin(minimumDb, handle.targetDb);
    for (const auto& level : this->layerData.sourceInfo.levels) {
        if (level.fittedRelativeDb.has_value()) minimumDb = juce::jmin(minimumDb, *level.fittedRelativeDb);
    }
    this->viewMinimumDb = std::floor(minimumDb / 12.0f) * 12.0f;
    if (this->viewMinimumDb >= 0.0f) this->viewMinimumDb = -48.0f;
}

void VelocityResponseEditor::updateControls() {
    this->saveAsButton.setEnabled(this->custom);
    this->updatePresetSelection();
    this->updateReadout();
    this->resized();
    this->repaint();
}

void VelocityResponseEditor::updateReadout() {
    if (this->selectedHandleIdx < 0 || this->selectedHandleIdx >= static_cast<int>(this->handles.size())) {
        this->readoutLabel.setText("No velocity points", juce::dontSendNotification);
        this->readoutLabel.setTooltip({});
        this->dbEditor.setEnabled(false);
        this->dbEditor.setTooltip({});
        return;
    }

    const auto& handle = this->handles[static_cast<size_t>(this->selectedHandleIdx)];
    juce::String text = "MIDI velocity " + juce::String(handle.velocity) + " -> output gain " + VelocityResponseEditor::formatDb(this->currentResponse.getTargetDb(static_cast<juce::uint8>(handle.velocity)));
    if (handle.velocity == 127) text += " (fixed)";
    const auto tooltip = this->tooltipForVelocity(handle.velocity);
    this->readoutLabel.setText(text, juce::dontSendNotification);
    this->readoutLabel.setTooltip(tooltip);
    if (!this->dbEditor.hasKeyboardFocus(true)) this->dbEditor.setText(juce::String(handle.targetDb, 1), false);
    this->dbEditor.setEnabled(handle.velocity != 127);
    this->dbEditor.setTooltip(tooltip);
}

void VelocityResponseEditor::updatePresetSelection() {
    if (!this->custom) {
        this->presetCombo.setSelectedId(this->globalCurveItemId, juce::dontSendNotification);
        return;
    }

    int selectedId = 0;
    for (int presetIdx = 0; presetIdx < static_cast<int>(this->presets.size()); ++presetIdx) {
        if (this->presets[static_cast<size_t>(presetIdx)].definition == this->currentDefinition) {
            selectedId = this->presetItemBaseId + presetIdx;
            break;
        }
    }
    this->presetCombo.setSelectedId(selectedId, juce::dontSendNotification);
    if (selectedId == 0) this->presetCombo.setText("Edited Layer Curve*", juce::dontSendNotification);
}

void VelocityResponseEditor::refreshPresets() {
    juce::StringArray warnings;
    this->presets = this->engine.getVelocityResponsePresets(&warnings);
    this->presetCombo.clear(juce::dontSendNotification);
    juce::String globalLabel = "Global - Edited*";
    const auto globalDefinition = this->engine.getVelocityResponse().getDefinition();
    for (const auto& preset : this->presets) {
        if (preset.definition != globalDefinition) continue;
        globalLabel = "Global - " + preset.name;
        break;
    }
    this->presetCombo.addItem(globalLabel, this->globalCurveItemId);
    this->presetCombo.addSeparator();
    for (int presetIdx = 0; presetIdx < static_cast<int>(this->presets.size()); ++presetIdx) {
        const auto& preset = this->presets[static_cast<size_t>(presetIdx)];
        this->presetCombo.addItem(preset.name + (preset.isUser ? "" : " (Built-in)"), this->presetItemBaseId + presetIdx);
    }
    this->updatePresetSelection();
    if (!warnings.isEmpty()) this->setStatus(warnings.joinIntoString("; "), true);
}

void VelocityResponseEditor::selectPreset() {
    const int selectedId = this->presetCombo.getSelectedId();
    if (selectedId == this->globalCurveItemId) {
        const bool changedResponse = this->custom || this->currentDefinition != this->engine.getVelocityResponse().getDefinition();
        this->setCustom(false, true);
        if (changedResponse) this->previewSelectedVelocity();
        return;
    }

    const int presetIdx = selectedId - this->presetItemBaseId;
    if (presetIdx < 0 || presetIdx >= static_cast<int>(this->presets.size())) return;
    this->custom = true;
    this->loadDefinition(this->presets[static_cast<size_t>(presetIdx)].definition, true, true);
    this->updateControls();
    this->setStatus("Loaded preset: " + this->presets[static_cast<size_t>(presetIdx)].name);
    this->previewSelectedVelocity();
}

void VelocityResponseEditor::setCustom(bool shouldUseCustom, bool notify) {
    const auto globalDefinition = this->engine.getVelocityResponse().getDefinition();
    if (this->custom == shouldUseCustom && (shouldUseCustom || this->currentDefinition == globalDefinition)) return;
    this->custom = shouldUseCustom;
    if (!this->custom) this->loadDefinition(globalDefinition, false, false);
    this->updateControls();
    if (notify) this->notifyResponseChanged(true);
    this->setStatus({});
}

void VelocityResponseEditor::previewSelectedVelocity() {
    if (this->selectedHandleIdx < 0 || this->selectedHandleIdx >= static_cast<int>(this->handles.size())) return;
    if (this->onPreview) this->onPreview(this->handles[static_cast<size_t>(this->selectedHandleIdx)].velocity);
}

void VelocityResponseEditor::notifyResponseChanged(bool commit) {
    if (this->onResponseChanged) this->onResponseChanged(this->custom ? std::optional<juce::String>(this->currentDefinition) : std::nullopt, commit);
}

bool VelocityResponseEditor::applySelectedDb(float targetDb, bool clampValue, bool commit) {
    if (!this->custom || this->selectedHandleIdx < 0 || this->selectedHandleIdx >= static_cast<int>(this->handles.size())) return false;
    auto& selected = this->handles[static_cast<size_t>(this->selectedHandleIdx)];
    if (selected.velocity == 127) return false;

    const float minimumDb = this->selectedHandleIdx > 0 ? this->handles[static_cast<size_t>(this->selectedHandleIdx - 1)].targetDb : -std::numeric_limits<float>::max();
    const float maximumDb = this->selectedHandleIdx + 1 < static_cast<int>(this->handles.size()) ? this->handles[static_cast<size_t>(this->selectedHandleIdx + 1)].targetDb : 0.0f;
    if (!clampValue && (targetDb < minimumDb || targetDb > maximumDb || targetDb > 0.0f)) {
        this->setStatus("dB must stay between the neighbouring curve points and at or below 0 dB", true);
        this->updateReadout();
        return false;
    }

    const float previousTargetDb = selected.targetDb;
    selected.targetDb = clampValue ? juce::jlimit(minimumDb, juce::jmin(0.0f, maximumDb), targetDb) : targetDb;
    if (juce::approximatelyEqual(selected.targetDb, previousTargetDb)) {
        selected.targetDb = previousTargetDb;
        this->updateReadout();
        return false;
    }
    std::vector<VelocityResponse::Point> points;
    points.reserve(this->handles.size());
    for (const auto& handle : this->handles) points.push_back({ handle.velocity, handle.targetDb });
    auto parsed = VelocityResponse::fromPoints(points);
    if (!parsed.ok()) {
        selected.targetDb = previousTargetDb;
        this->setStatus(parsed.error(), true);
        this->updateReadout();
        this->repaint();
        return false;
    }

    this->currentResponse = std::move(parsed.value);
    this->currentDefinition = this->currentResponse.getDefinition();
    this->updateViewMinimum();
    this->updatePresetSelection();
    this->updateReadout();
    this->repaint();
    this->setStatus({});
    this->notifyResponseChanged(commit);
    return true;
}

void VelocityResponseEditor::commitDbEditor() {
    if (!this->dbEditor.isEnabled() || this->selectedHandleIdx < 0 || this->selectedHandleIdx >= static_cast<int>(this->handles.size())) return;
    const auto targetDb = VelocityResponseEditor::parseDb(this->dbEditor.getText());
    if (!targetDb.has_value()) {
        this->setStatus("Enter a valid dB number", true);
        this->updateReadout();
        return;
    }
    if (juce::approximatelyEqual(*targetDb, this->handles[static_cast<size_t>(this->selectedHandleIdx)].targetDb)) {
        this->updateReadout();
        return;
    }

    const bool hadLayerCurve = this->custom;
    if (!this->custom) {
        this->custom = true;
        this->updateControls();
    }
    if (this->applySelectedDb(*targetDb, false, true)) {
        this->previewSelectedVelocity();
        return;
    }
    if (!hadLayerCurve) {
        this->custom = false;
        this->updateControls();
    }
}

void VelocityResponseEditor::showSaveAsDialog() {
    if (!this->custom) return;
    auto* dialog = new juce::AlertWindow("Save Velocity-to-dB Curve Preset", "Enter a preset name:", juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor("name", "My Curve", "Name:");
    dialog->addButton("Save", 1);
    dialog->addButton("Cancel", 0);
    juce::Component::SafePointer<VelocityResponseEditor> safeThis(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, dialog](int result) {
        if (result != 1 || safeThis == nullptr) return;
        safeThis->savePreset(dialog->getTextEditorContents("name"), false);
    }), true);
}

void VelocityResponseEditor::savePreset(const juce::String& name, bool replaceExisting) {
    auto saved = this->engine.saveVelocityResponsePreset(name, this->currentDefinition, replaceExisting);
    if (saved.ok()) {
        this->refreshPresets();
        this->setStatus("Saved preset: " + saved.value.name);
        return;
    }
    if (!replaceExisting && saved.error().containsIgnoreCase("already exists")) {
        juce::Component::SafePointer<VelocityResponseEditor> safeThis(this);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Replace Velocity-to-dB Curve Preset")
                .withMessage("Replace the existing preset?")
                .withButton("Replace")
                .withButton("Cancel"),
            [safeThis, name](int result) {
                if (result == 1 && safeThis != nullptr) safeThis->savePreset(name, true);
            });
        return;
    }
    this->setStatus(saved.error(), true);
}

void VelocityResponseEditor::setStatus(const juce::String& text, bool error) {
    const auto displayText = text.isNotEmpty() ? text : juce::String("Drag a point vertically. Release to hear the result.");
    this->statusLabel.setText(displayText, juce::dontSendNotification);
    this->statusLabel.setColour(juce::Label::textColourId, error ? UITheme::Colours::accentWarning : UITheme::Colours::textMuted);
}

int VelocityResponseEditor::findNearestHandle(float xPosition) const {
    if (this->handles.empty()) return -1;
    int bestIdx = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (int handleIdx = 0; handleIdx < static_cast<int>(this->handles.size()); ++handleIdx) {
        const float distance = std::abs(this->velocityToX(this->handles[static_cast<size_t>(handleIdx)].velocity) - xPosition);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIdx = handleIdx;
        }
    }
    return bestIdx;
}

const DrumKitEngine::VelocityResponseLevelInfo* VelocityResponseEditor::findLevel(int velocity) const {
    for (const auto& level : this->layerData.sourceInfo.levels)
        if (level.velocity == velocity) return &level;
    return nullptr;
}

float VelocityResponseEditor::velocityToX(int velocity) const {
    return this->plotBounds.getX() + static_cast<float>(velocity - 1) * this->plotBounds.getWidth() / 126.0f;
}

float VelocityResponseEditor::dbToY(float targetDb) const {
    return juce::jmap(juce::jlimit(this->viewMinimumDb, 0.0f, targetDb), 0.0f, this->viewMinimumDb, this->plotBounds.getY(), this->plotBounds.getBottom());
}

float VelocityResponseEditor::yToDb(float yPosition) const {
    return juce::jmap(juce::jlimit(this->plotBounds.getY(), this->plotBounds.getBottom(), yPosition), this->plotBounds.getY(), this->plotBounds.getBottom(), 0.0f, this->viewMinimumDb);
}

juce::String VelocityResponseEditor::tooltipForVelocity(int velocity) const {
    juce::String text = "MIDI velocity " + juce::String(velocity) + "\nOutput gain: " + VelocityResponseEditor::formatDb(this->currentResponse.getTargetDb(static_cast<juce::uint8>(velocity)));
    const auto* level = this->findLevel(velocity);
    if (level == nullptr) return text + "\nNo recorded sample at this exact velocity";
    text += level->fittedRelativeDb.has_value() ? "\nRecorded sample reference: " + VelocityResponseEditor::formatDb(*level->fittedRelativeDb) : "\nRecorded sample reference: unavailable";
    text += level->rawMedianRelativeDb.has_value() ? "\nRaw measured sample level: " + VelocityResponseEditor::formatDb(*level->rawMedianRelativeDb) : "\nRaw measured sample level: unavailable";
    text += level->calibrationTrimDb.has_value() ? "\nCalibration correction: " + VelocityResponseEditor::formatDb(*level->calibrationTrimDb) : "\nCalibration correction: unavailable";
    if (level->calibrationLimited) text += "\nCalibration correction is limited";
    return text;
}

juce::String VelocityResponseEditor::formatDb(float value) {
    return juce::String(value, 1) + " dB";
}

std::optional<float> VelocityResponseEditor::parseDb(const juce::String& text) {
    const auto trimmed = text.trim();
    if (trimmed.isEmpty()) return std::nullopt;
    const auto utf8Text = trimmed.toRawUTF8();
    char* endPtr = nullptr;
    errno = 0;
    const float value = std::strtof(utf8Text, &endPtr);
    if (errno != 0 || endPtr == utf8Text || *endPtr != '\0' || !std::isfinite(value)) return std::nullopt;
    return value;
}

void VelocityResponseEditor::paint(juce::Graphics& graphics) {
    graphics.setColour(UITheme::Colours::bgPanelRaised);
    graphics.fillRoundedRectangle(this->getLocalBounds().toFloat(), 6.0f);
    graphics.setColour(UITheme::Colours::borderMid);
    graphics.drawRoundedRectangle(this->getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
    if (this->plotBounds.isEmpty()) return;

    graphics.setColour(UITheme::Colours::bgEditor);
    graphics.fillRoundedRectangle(this->plotBounds, 4.0f);

    int previousVelocity = 0;
    for (int levelIdx = 0; levelIdx < static_cast<int>(this->layerData.sourceInfo.levels.size()); ++levelIdx) {
        const auto& level = this->layerData.sourceInfo.levels[static_cast<size_t>(levelIdx)];
        const int regionStartVelocity = previousVelocity + 1;
        const int regionEndVelocity = levelIdx + 1 == static_cast<int>(this->layerData.sourceInfo.levels.size()) ? 127 : level.velocity;
        const float regionLeft = this->velocityToX(regionStartVelocity);
        const float regionRight = this->velocityToX(regionEndVelocity);
        graphics.setColour(UITheme::Colours::selectionStrong.withAlpha(levelIdx % 2 == 0 ? 0.035f : 0.065f));
        graphics.fillRect(juce::Rectangle<float>(regionLeft, this->plotBounds.getY(), juce::jmax(1.0f, regionRight - regionLeft), this->plotBounds.getHeight()));
        previousVelocity = level.velocity;
    }

    const int gridLineCount = 4;
    graphics.setFont(UITheme::Fonts::small());
    for (int gridIdx = 0; gridIdx <= gridLineCount; ++gridIdx) {
        const float proportion = static_cast<float>(gridIdx) / static_cast<float>(gridLineCount);
        const float targetDb = proportion * this->viewMinimumDb;
        const float yPosition = this->dbToY(targetDb);
        graphics.setColour(UITheme::Colours::borderSubtle);
        graphics.drawHorizontalLine(juce::roundToInt(yPosition), this->plotBounds.getX(), this->plotBounds.getRight());
        graphics.setColour(UITheme::Colours::textMuted);
        graphics.drawText(juce::String(targetDb, 0), 4, juce::roundToInt(yPosition) - 7, juce::roundToInt(this->plotBounds.getX()) - 8, 14, juce::Justification::centredRight);
    }

    for (const auto& level : this->layerData.sourceInfo.levels) {
        const float xPosition = this->velocityToX(level.velocity);
        graphics.setColour(UITheme::Colours::textAccentSoft.withAlpha(0.32f));
        graphics.drawVerticalLine(juce::roundToInt(xPosition), this->plotBounds.getY(), this->plotBounds.getBottom());
        if (level.fittedRelativeDb.has_value()) {
            const float markerY = this->dbToY(*level.fittedRelativeDb);
            juce::Path marker;
            marker.addTriangle(xPosition, markerY - 4.0f, xPosition + 4.0f, markerY, xPosition, markerY + 4.0f);
            marker.addTriangle(xPosition, markerY - 4.0f, xPosition - 4.0f, markerY, xPosition, markerY + 4.0f);
            graphics.setColour(level.calibrationLimited ? UITheme::Colours::accentWarning : UITheme::Colours::textAccent);
            graphics.fillPath(marker);
        }
    }

    juce::Path curve;
    for (int velocity = 1; velocity <= 127; ++velocity) {
        const float xPosition = this->velocityToX(velocity);
        const float yPosition = this->dbToY(this->currentResponse.getTargetDb(static_cast<juce::uint8>(velocity)));
        if (velocity == 1) curve.startNewSubPath(xPosition, yPosition);
        else curve.lineTo(xPosition, yPosition);
    }
    graphics.setColour(UITheme::Colours::selectionStrong);
    graphics.strokePath(curve, juce::PathStrokeType(2.0f));

    float lastLabelRight = -1000.0f;
    for (int handleIdx = 0; handleIdx < static_cast<int>(this->handles.size()); ++handleIdx) {
        const auto& handle = this->handles[static_cast<size_t>(handleIdx)];
        const float xPosition = this->velocityToX(handle.velocity);
        const float yPosition = this->dbToY(handle.targetDb);
        graphics.setColour((handle.recordedLevel ? UITheme::Colours::textAccentSoft : UITheme::Colours::borderStrong).withAlpha(0.65f));
        graphics.drawLine(xPosition, yPosition, xPosition, this->plotBounds.getBottom(), handleIdx == this->selectedHandleIdx ? 2.0f : 1.0f);
        graphics.setColour(handleIdx == this->selectedHandleIdx ? UITheme::Colours::selectionStrong : handle.recordedLevel ? UITheme::Colours::textAccent : UITheme::Colours::textSecondary);
        graphics.fillEllipse(xPosition - 4.0f, yPosition - 4.0f, 8.0f, 8.0f);

        const bool drawVelocityLabel = handleIdx == this->selectedHandleIdx || xPosition - lastLabelRight >= 18.0f || handleIdx + 1 == static_cast<int>(this->handles.size());
        if (drawVelocityLabel) {
            graphics.setColour(handleIdx == this->selectedHandleIdx ? UITheme::Colours::textPrimary : UITheme::Colours::textMuted);
            graphics.drawText(juce::String(handle.velocity), juce::roundToInt(xPosition) - 14, juce::roundToInt(this->plotBounds.getBottom()) + 3, 28, 14, juce::Justification::centred);
            lastLabelRight = xPosition + 14.0f;
        }
    }

    if (this->layerData.sourceInfo.levels.empty()) {
        graphics.setColour(UITheme::Colours::textMuted);
        graphics.setFont(UITheme::Fonts::normal());
        graphics.drawText(this->layerData.sourceInfo.sourceFound ? "No recorded velocity levels" : "Source velocity information unavailable", this->plotBounds.toNearestInt(), juce::Justification::centred);
    }
}

void VelocityResponseEditor::resized() {
    auto area = this->getLocalBounds().reduced(12);
    auto header = area.removeFromTop(36);
    auto closeButtonArea = header.removeFromRight(UITheme::Sizes::standardControlHeight);
    this->doneButton.setBounds(closeButtonArea.removeFromTop(UITheme::Sizes::standardControlHeight));
    header.removeFromRight(6);
    this->titleLabel.setBounds(header);
    area.removeFromTop(4);
    this->descriptionLabel.setBounds(area.removeFromTop(UITheme::Sizes::standardControlHeight));
    area.removeFromTop(6);

    auto toolbar = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->presetCombo.setBounds(toolbar.removeFromLeft(220));
    toolbar.removeFromLeft(4);
    this->saveAsButton.setBounds(toolbar.removeFromLeft(128));
    area.removeFromTop(6);

    auto readout = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->dbEditor.setBounds(readout.removeFromRight(58));
    readout.removeFromRight(6);
    this->readoutLabel.setBounds(readout);
    area.removeFromTop(2);
    this->statusLabel.setBounds(area.removeFromTop(16));
    area.removeFromTop(4);

    this->plotBounds = area.toFloat().withTrimmedLeft(38.0f).withTrimmedRight(8.0f).withTrimmedBottom(20.0f).reduced(0.0f, 4.0f);
}

void VelocityResponseEditor::mouseDown(const juce::MouseEvent& event) {
    if (!this->plotBounds.contains(event.position)) return;
    this->grabKeyboardFocus();
    this->selectedHandleIdx = this->findNearestHandle(event.position.x);
    this->updateReadout();
    this->repaint();
    if (this->selectedHandleIdx < 0 || this->handles[static_cast<size_t>(this->selectedHandleIdx)].velocity == 127) return;
    this->draggingHandle = true;
    this->dragStartedWithLayerCurve = this->custom;
    this->dragChangedCurve = false;
    this->dragStartDefinition = this->currentDefinition;
}

void VelocityResponseEditor::mouseDrag(const juce::MouseEvent& event) {
    if (!this->draggingHandle) return;
    if (!this->custom) {
        this->custom = true;
        this->updateControls();
    }
    const auto previousDefinition = this->currentDefinition;
    if (this->applySelectedDb(this->yToDb(event.position.y), true, false) && this->currentDefinition != previousDefinition) this->dragChangedCurve = true;
}

void VelocityResponseEditor::mouseUp(const juce::MouseEvent&) {
    if (!this->draggingHandle) return;
    this->draggingHandle = false;
    if (!this->dragChangedCurve || this->currentDefinition == this->dragStartDefinition) {
        if (!this->dragStartedWithLayerCurve && this->custom) this->setCustom(false, this->dragChangedCurve);
        return;
    }
    this->notifyResponseChanged(true);
    this->previewSelectedVelocity();
}

void VelocityResponseEditor::mouseMove(const juce::MouseEvent& event) {
    if (!this->plotBounds.contains(event.position)) {
        this->setTooltip({});
        return;
    }
    const int handleIdx = this->findNearestHandle(event.position.x);
    if (handleIdx >= 0) this->setTooltip(this->tooltipForVelocity(this->handles[static_cast<size_t>(handleIdx)].velocity));
}

void VelocityResponseEditor::mouseExit(const juce::MouseEvent&) {
    this->setTooltip({});
}

bool VelocityResponseEditor::keyPressed(const juce::KeyPress& key) {
    if (this->handles.empty()) return false;
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey) {
        const int direction = key == juce::KeyPress::leftKey ? -1 : 1;
        this->selectedHandleIdx = juce::jlimit(0, static_cast<int>(this->handles.size()) - 1, this->selectedHandleIdx + direction);
        this->updateReadout();
        this->repaint();
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
        if (this->selectedHandleIdx < 0 || this->handles[static_cast<size_t>(this->selectedHandleIdx)].velocity == 127) return true;
        const float step = key.getModifiers().isShiftDown() ? 1.0f : 0.1f;
        const float direction = key == juce::KeyPress::upKey ? 1.0f : -1.0f;
        const bool hadLayerCurve = this->custom;
        if (!this->custom) {
            this->custom = true;
            this->updateControls();
        }
        if (this->applySelectedDb(this->handles[static_cast<size_t>(this->selectedHandleIdx)].targetDb + direction * step, true, true)) {
            this->previewSelectedVelocity();
            return true;
        }
        if (!hadLayerCurve) {
            this->custom = false;
            this->updateControls();
        }
        return true;
    }
    if (key == juce::KeyPress::escapeKey) {
        if (this->onDone) this->onDone();
        return true;
    }
    return false;
}
