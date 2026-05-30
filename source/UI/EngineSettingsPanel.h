#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/EngineSettings.h"
#include "ParameterPot.h"
#include "UITheme.h"

class DrumKitEngine;

class EngineSettingsPanel : public juce::Component {
public:
    explicit EngineSettingsPanel(DrumKitEngine& engine);
    ~EngineSettingsPanel() override = default;

    void resized() override;
    void loadFromEngine();

private:
    DrumKitEngine& engine;

    struct PotEntry {
        std::unique_ptr<ParameterPot> parameterPot;
        juce::TextButton resetButton { "x" };
        float* target = nullptr;
        int* intTarget = nullptr;
        double defaultValue = 0.0;
    };

    juce::Label chokeGroupLabel;
    juce::Label tailGroupLabel;
    juce::Label hihatGroupLabel;
    juce::Label uiGridGroupLabel;

    juce::Viewport viewport;
    juce::Component contentComponent;

    std::vector<std::unique_ptr<PotEntry>> pots;

    void addPot(const juce::String& name, const juce::String& tooltip, float* target, float minVal, float maxVal, float interval);
    void addIntPot(const juce::String& name, const juce::String& tooltip, int* target, int minVal, int maxVal);
    void onSettingChanged();

public:
    std::function<void()> onGridSizeChanged;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineSettingsPanel)
};
