#pragma once

#include "../Engine/DrumKitEngine.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

class SourceArticulationOverrideEditor : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit SourceArticulationOverrideEditor(DrumKitEngine& engine);
    ~SourceArticulationOverrideEditor() override;

    void setSource(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const juce::String& displayTitle);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void(int velocity)> onPreview;
    std::function<void()> onDone;

private:
    DrumKitEngine& engine;
    DrumKitEngine::SourceVelocityRangeInfo rangeInfo;
    DrumKitEngine::SourceVelocityRangeInfo committedRangeInfo;
    int selectedDividerIdx = -1;
    bool draggingDivider = false;
    bool previewPending = false;
    juce::Rectangle<float> rangeBounds;

    juce::Label titleLabel;
    juce::Label scopeLabel;
    juce::Label readoutLabel;
    juce::Label statusLabel;
    juce::TextEditor boundaryEditor;
    juce::TextButton resetSelectedButton { "Reset selected" };
    juce::TextButton resetAllButton { "Reset ranges" };
    juce::TextButton previewButton { "Preview" };
    juce::TextButton doneButton { "Done" };

    bool applySelectedBoundary(int upperBoundary, bool persist);
    void commitBoundaryEditor();
    void resetSelectedBoundary();
    void resetAllBoundaries();
    void restoreCommittedPreview();
    void updateControls();
    void updateReadout();
    void setStatus(const juce::String& text, bool error = false);
    int findNearestDivider(float xPosition, float maximumDistance) const;
    int boundaryForX(float xPosition) const;
    float boundaryToX(int upperBoundary) const;
    int getSelectedMinimum() const;
    int getSelectedMaximum() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceArticulationOverrideEditor)
};
