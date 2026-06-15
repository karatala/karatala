#pragma once

#include "../Engine/DrumKitEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

class CrosstalkCalibrationPopup : public juce::Component,
                                  private juce::Timer {
public:
    explicit CrosstalkCalibrationPopup(DrumKitEngine& engine);
    ~CrosstalkCalibrationPopup() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    juce::Rectangle<int> getPreferredBounds(const juce::Rectangle<int>& availableBounds) const;
    void cancelAndRestore();

    std::function<void(bool)> onCaptureStateChanged;
    std::function<bool(int&, int&)> onMidiPoll;
    std::function<void(const juce::String&)> onStatusMessage;
    std::function<void()> onDismiss;

private:
    struct Target {
        int note = -1;
        juce::String name;
    };

    DrumKitEngine& engine;
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton startSkipButton { "Start" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton clearButton { "Clear" };
    juce::TextButton closeButton { "x" };
    juce::TextEditor profileEditor;
    std::vector<Target> targets;
    std::array<int, 128> capturedVelocities {};
    int targetIndex = -1;
    int primaryVelocity = 0;
    bool captureActive = false;
    bool calibrationRunning = false;
    juce::uint32 captureDeadlineMs = 0;

    void buildTargets();
    void startCalibration();
    void skipTarget();
    void stopCalibration();
    void clearProfile();
    void advanceTarget();
    void handleMidiNote(int midiNote, int velocity);
    void finishTargetCapture();
    void updateProfileText();
    void setStatus(const juce::String& text);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrosstalkCalibrationPopup)
};
