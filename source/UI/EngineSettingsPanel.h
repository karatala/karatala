#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/EngineSettings.h"

class DrumKitEngine;

class EngineSettingsPanel : public juce::Component {
public:
    explicit EngineSettingsPanel(DrumKitEngine& engine);
    ~EngineSettingsPanel() override = default;

    void resized() override;
    void loadFromEngine();

private:
    DrumKitEngine& engine;

    struct SliderEntry {
        juce::Label label;
        juce::Slider slider;
        float* target = nullptr;
        int* intTarget = nullptr;
    };

    juce::Label chokeGroupLabel;
    juce::Label tailGroupLabel;
    juce::Label hihatGroupLabel;
    juce::Label uiGridGroupLabel;

    std::vector<std::unique_ptr<SliderEntry>> sliders;

    void addSlider(const juce::String& name, float* target, float minVal, float maxVal, float interval);
    void addIntSlider(const juce::String& name, int* target, int minVal, int maxVal);
    void onSettingChanged();

    /** Called when grid size changes — owner can listen to this. */
public:
    std::function<void()> onGridSizeChanged;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineSettingsPanel)
};
