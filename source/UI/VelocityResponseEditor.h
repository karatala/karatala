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
        DrumKitEngine::VelocityResponseSourceInfo sourceInfo;
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
    bool draggingHandle = false;
    float viewMinimumDb = -48.0f;
    juce::Rectangle<float> plotBounds;

    juce::Label titleLabel;
    juce::Label readoutLabel;
    juce::Label statusLabel;
    juce::ComboBox presetCombo;
    juce::TextButton saveAsButton { "Save As" };
    juce::TextButton makeCustomButton { "Create Layer Curve" };
    juce::TextButton useGlobalButton { "Use Global" };
    juce::TextButton previewButton { "Preview" };
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
    void notifyResponseChanged(bool commit);
    bool applySelectedDb(float targetDb, bool clampValue, bool commit);
    void commitDbEditor();
    void showSaveAsDialog();
    void savePreset(const juce::String& name, bool replaceExisting);
    void setStatus(const juce::String& text, bool error = false);
    int findNearestHandle(float xPosition) const;
    const DrumKitEngine::VelocityResponseLevelInfo* findLevel(int velocity) const;
    float velocityToX(int velocity) const;
    float dbToY(float targetDb) const;
    float yToDb(float yPosition) const;
    juce::String tooltipForVelocity(int velocity) const;
    static juce::String formatDb(float value);
    static std::optional<float> parseDb(const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VelocityResponseEditor)
};
