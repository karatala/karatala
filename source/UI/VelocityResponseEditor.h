#pragma once

#include "../Engine/DrumKitEngine.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class VelocityResponseEditor : public juce::Component, public juce::SettableTooltipClient {
public:
    struct LayerData {
        juce::String title;
        std::optional<juce::String> customDefinition;
        DrumKitEngine::LibraryVelocityResponseInfo libraryInfo;
    };

    explicit VelocityResponseEditor(DrumKitEngine& engine);

    void setLayerData(LayerData data);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void(const std::optional<juce::String>& definition, bool commit)> onResponseChanged;
    std::function<void(int velocity)> onPreview;
    std::function<void()> onDone;

private:
    static constexpr int globalCurveItemId = 1;
    static constexpr int presetItemBaseId = 100;
    static constexpr int hoverPreviewDelayMs = 120;
    static constexpr double clickPreviewDedupWindowMs = 400.0;

    struct Handle {
        int velocity = 0;
        float targetDb = 0.0f;
        bool recordedLevel = false;
    };

    DrumKitEngine& engine;
    LayerData layerData;
    VelocityResponse currentResponse = VelocityResponse::linear();
    juce::String currentDefinition { "linear" };
    bool custom = false;
    std::vector<Handle> handles;
    std::vector<DrumKitEngine::VelocityResponsePreset> presets;
    int selectedHandleIdx = -1;
    int hoverPreviewVelocity = -1;
    int lastPointerPreviewVelocity = -1;
    int clickPreviewVelocity = -1;
    int hoverPreviewGeneration = 0;
    double lastPointerPreviewTimeMs = 0.0;
    bool draggingHandle = false;
    bool dragStartedWithLayerCurve = false;
    bool dragChangedCurve = false;
    juce::String dragStartDefinition;
    float viewMinimumDb = -48.0f;
    float hoverGuideY = -1.0f;
    juce::Rectangle<float> plotBounds;

    juce::Label titleLabel;
    juce::Label descriptionLabel;
    juce::Label readoutLabel;
    juce::Label statusLabel;
    juce::ComboBox presetCombo;
    juce::TextButton saveAsButton { "Save as Preset..." };
    juce::TextButton doneButton;
    juce::TextEditor dbEditor;

    void loadDefinition(const juce::String& definition, bool notify, bool commit);
    void rebuildHandles();
    void updateViewMinimum();
    void updateControls();
    void updateReadout();
    void updatePresetSelection();
    void refreshPresets();
    void selectPreset();
    void setCustom(bool shouldUseCustom, bool notify);
    void previewVelocity(int velocity);
    void previewPointerVelocity(int velocity);
    void previewSelectedVelocity();
    void queueHoverPreview(int velocity);
    void clearHoverPreview(bool resetLastPreview);
    bool recentPointerPreviewMatches(int velocity) const;
    void notifyResponseChanged(bool commit);
    bool applySelectedDb(float targetDb, bool clampValue, bool commit);
    void commitDbEditor();
    void showSaveAsDialog();
    void showReplacePresetDialog(const juce::String& name);
    void savePreset(const juce::String& name, bool replaceExisting);
    void setStatus(const juce::String& text, bool error = false);
    int findNearestHandle(float xPosition) const;
    const DrumKitEngine::VelocityResponseLevelInfo* findLevel(int velocity) const;
    float velocityToX(int velocity) const;
    float dbToY(float targetDb) const;
    float yToDb(float yPosition) const;
    juce::String tooltipForVelocity(int velocity) const;
    static juce::String formatDb(float value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VelocityResponseEditor)
};
