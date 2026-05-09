#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine/EngineSettings.h"

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
    };

    juce::Label chokeGroupLabel;
    juce::Label tailGroupLabel;
    juce::Label hihatGroupLabel;

    std::vector<std::unique_ptr<SliderEntry>> sliders;

    void addSlider(const juce::String& name, float* target, float minVal, float maxVal, float interval);
    void onSettingChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineSettingsPanel)
};
