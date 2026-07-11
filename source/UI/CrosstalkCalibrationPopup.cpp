#include "CrosstalkCalibrationPopup.h"
#include "UIHelpers.h"
#include "UITheme.h"

CrosstalkCalibrationPopup::CrosstalkCalibrationPopup(DrumKitEngine& drumKitEngine)
    : engine(drumKitEngine) {
    this->titleLabel.setText("Crosstalk Calibration", juce::dontSendNotification);
    this->titleLabel.setFont(UITheme::Fonts::sectionTitle().withHeight(16.0f));
    this->titleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->addAndMakeVisible(this->titleLabel);

    this->statusLabel.setFont(UITheme::Fonts::labelBold().withHeight(15.0f));
    this->statusLabel.setColour(juce::Label::textColourId, UITheme::Colours::textAccent);
    this->statusLabel.setJustificationType(juce::Justification::centredLeft);
    this->statusLabel.setMinimumHorizontalScale(0.75f);
    this->addAndMakeVisible(this->statusLabel);

    UIHelpers::configureControlButton(this->startSkipButton);
    UIHelpers::configureControlButton(this->stopButton);
    UIHelpers::configureControlButton(this->clearButton);
    UIHelpers::configureControlButton(this->closeButton);
    this->startSkipButton.onClick = [this] {
        if (this->calibrationRunning)
            this->skipTarget();
        else
            this->startCalibration();
    };
    this->stopButton.onClick = [this] { this->stopCalibration(); };
    this->clearButton.onClick = [this] { this->clearProfile(); };
    this->closeButton.onClick = [this] {
        this->cancelAndRestore();
        if (this->onDismiss) this->onDismiss();
    };
    this->addAndMakeVisible(this->startSkipButton);
    this->addAndMakeVisible(this->stopButton);
    this->addAndMakeVisible(this->clearButton);
    this->addAndMakeVisible(this->closeButton);

    UIHelpers::configureMonoTextEditor(this->profileEditor, UITheme::Colours::textSecondary, true);
    this->profileEditor.setReadOnly(true);
    this->profileEditor.setCaretVisible(false);
    this->addAndMakeVisible(this->profileEditor);

    this->buildTargets();
    this->updateProfileText();
    this->setStatus("Press Start, then strike each requested pad or zone hard.");
    this->stopButton.setEnabled(false);
}

CrosstalkCalibrationPopup::~CrosstalkCalibrationPopup() {
    this->cancelAndRestore();
}

void CrosstalkCalibrationPopup::buildTargets() {
    this->targets.clear();
    const auto* layout = this->engine.getKitLayout();
    if (layout == nullptr)
        return;

    std::array<bool, 128> addedNotes {};
    for (const auto& slot : layout->instrumentSlots) {
        for (const auto& articulation : slot.articulations) {
            for (const auto midiNote : articulation.notes) {
                if (addedNotes[midiNote])
                    continue;
                addedNotes[midiNote] = true;
                Target target;
                target.note = static_cast<int>(midiNote);
                target.name = (slot.display.isNotEmpty() ? slot.display : slot.id) + " / " + articulation.codeRaw + " (note " + juce::String(target.note) + ")";
                this->targets.push_back(std::move(target));
            }
        }
    }
}

void CrosstalkCalibrationPopup::startCalibration() {
    if (this->targets.empty()) {
        this->setStatus("No mapped notes are available to calibrate.");
        return;
    }

    this->engine.beginCrosstalkCalibration();
    this->calibrationRunning = true;
    this->targetIndex = 0;
    this->startSkipButton.setButtonText("Skip");
    this->stopButton.setEnabled(true);
    if (this->onCaptureStateChanged) this->onCaptureStateChanged(true);
    this->startTimer(15);
    this->advanceTarget();
    if (this->onStatusMessage) this->onStatusMessage("Crosstalk calibration started");
}

void CrosstalkCalibrationPopup::skipTarget() {
    if (!this->calibrationRunning)
        return;
    ++this->targetIndex;
    this->advanceTarget();
}

void CrosstalkCalibrationPopup::stopCalibration() {
    if (!this->calibrationRunning)
        return;

    this->calibrationRunning = false;
    this->captureActive = false;
    this->stopTimer();
    if (this->onCaptureStateChanged) this->onCaptureStateChanged(false);
    this->engine.finishCrosstalkCalibration();
    this->engine.saveSettings();
    this->startSkipButton.setButtonText("Start");
    this->stopButton.setEnabled(false);
    this->setStatus("Calibration stopped. Learned data has been saved.");
    this->updateProfileText();
    if (this->onStatusMessage) this->onStatusMessage("Crosstalk calibration saved: " + juce::String(this->engine.getCrosstalkProfileEntryCount()) + " learned pairs");
}

void CrosstalkCalibrationPopup::clearProfile() {
    if (this->calibrationRunning) {
        this->calibrationRunning = false;
        this->captureActive = false;
        this->stopTimer();
        if (this->onCaptureStateChanged) this->onCaptureStateChanged(false);
        this->engine.finishCrosstalkCalibration();
    }

    this->engine.clearCrosstalkProfile();
    this->engine.saveSettings();
    this->startSkipButton.setButtonText("Start");
    this->stopButton.setEnabled(false);
    this->setStatus("Crosstalk profile cleared.");
    this->updateProfileText();
    if (this->onStatusMessage) this->onStatusMessage("Crosstalk profile cleared");
}

void CrosstalkCalibrationPopup::advanceTarget() {
    if (this->targetIndex >= static_cast<int>(this->targets.size())) {
        this->stopCalibration();
        this->setStatus("Calibration complete. Learned data has been saved.");
        return;
    }

    this->primaryVelocity = 0;
    this->captureActive = false;
    this->capturedVelocities.fill(0);
    const auto& target = this->targets[static_cast<size_t>(this->targetIndex)];
    this->setStatus("Hit hard: " + target.name + "  [" + juce::String(this->targetIndex + 1) + "/" + juce::String(this->targets.size()) + "]");
}

void CrosstalkCalibrationPopup::handleMidiNote(int midiNote, int velocity) {
    if (!this->calibrationRunning || !juce::isPositiveAndBelow(midiNote, 128))
        return;

    const auto& target = this->targets[static_cast<size_t>(this->targetIndex)];
    if (!this->captureActive) {
        this->capturedVelocities[static_cast<size_t>(midiNote)] = juce::jmax(this->capturedVelocities[static_cast<size_t>(midiNote)], velocity);
        if (midiNote != target.note || velocity < 80)
            return;
        this->primaryVelocity = velocity;
        this->captureActive = true;
        this->captureDeadlineMs = juce::Time::getMillisecondCounter() + 120;
        return;
    }

    if (midiNote != target.note)
        this->capturedVelocities[static_cast<size_t>(midiNote)] = juce::jmax(this->capturedVelocities[static_cast<size_t>(midiNote)], velocity);
}

void CrosstalkCalibrationPopup::finishTargetCapture() {
    if (!this->calibrationRunning || this->primaryVelocity <= 0)
        return;

    const int primaryNote = this->targets[static_cast<size_t>(this->targetIndex)].note;
    for (int secondaryNote = 0; secondaryNote < 128; ++secondaryNote) {
        const int secondaryVelocity = this->capturedVelocities[static_cast<size_t>(secondaryNote)];
        if (secondaryNote == primaryNote || secondaryVelocity <= 0 || secondaryVelocity >= this->primaryVelocity)
            continue;
        const int ratio = juce::jlimit(1, 110, ((secondaryVelocity + 5) * 127 + this->primaryVelocity - 1) / this->primaryVelocity);
        this->engine.setCrosstalkThreshold(primaryNote, secondaryNote, ratio);
    }

    this->updateProfileText();
    ++this->targetIndex;
    this->advanceTarget();
}

void CrosstalkCalibrationPopup::updateProfileText() {
    this->profileEditor.setText(this->engine.buildCrosstalkProfileText(), false);
}

void CrosstalkCalibrationPopup::setStatus(const juce::String& text) {
    this->statusLabel.setText(text, juce::dontSendNotification);
}

void CrosstalkCalibrationPopup::timerCallback() {
    if (this->onMidiPoll) {
        int noteNumber = 0;
        int velocity = 0;
        while (this->onMidiPoll(noteNumber, velocity))
            this->handleMidiNote(noteNumber, velocity);
    }

    if (this->captureActive && static_cast<juce::int32>(juce::Time::getMillisecondCounter() - this->captureDeadlineMs) >= 0)
        this->finishTargetCapture();
}

void CrosstalkCalibrationPopup::cancelAndRestore() {
    if (!this->calibrationRunning)
        return;
    this->calibrationRunning = false;
    this->captureActive = false;
    this->stopTimer();
    if (this->onCaptureStateChanged) this->onCaptureStateChanged(false);
    this->engine.cancelCrosstalkCalibration();
}

void CrosstalkCalibrationPopup::paint(juce::Graphics& graphics) {
    graphics.setColour(UITheme::Colours::bgPanelRaised);
    graphics.fillRoundedRectangle(this->getLocalBounds().toFloat(), 6.0f);
    graphics.setColour(UITheme::Colours::borderStrong);
    graphics.drawRoundedRectangle(this->getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void CrosstalkCalibrationPopup::resized() {
    auto area = this->getLocalBounds().reduced(12);
    auto titleRow = area.removeFromTop(26);
    this->closeButton.setBounds(titleRow.removeFromRight(24));
    titleRow.removeFromRight(6);
    this->titleLabel.setBounds(titleRow);
    area.removeFromTop(8);
    this->statusLabel.setBounds(area.removeFromTop(46));
    area.removeFromTop(8);

    auto buttonRow = area.removeFromTop(32);
    this->startSkipButton.setBounds(buttonRow.removeFromLeft(110));
    buttonRow.removeFromLeft(8);
    this->stopButton.setBounds(buttonRow.removeFromLeft(90));
    buttonRow.removeFromLeft(8);
    this->clearButton.setBounds(buttonRow.removeFromLeft(90));
    area.removeFromTop(10);
    this->profileEditor.setBounds(area);
}

juce::Rectangle<int> CrosstalkCalibrationPopup::getPreferredBounds(const juce::Rectangle<int>& availableBounds) const {
    const int width = juce::jmin(680, availableBounds.getWidth());
    const int height = juce::jmin(480, availableBounds.getHeight());
    return juce::Rectangle<int>(width, height).withCentre(availableBounds.getCentre());
}
