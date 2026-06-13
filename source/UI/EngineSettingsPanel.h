#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/EngineSettings.h"
#include "../Engine/LayoutManager.h"
#include "ParameterPot.h"
#include "ScrollablePanel.h"
#include "UITheme.h"

class DrumKitEngine;

class EngineSettingsPanel : public ScrollablePanel,
                            private juce::Timer {
public:
    explicit EngineSettingsPanel(DrumKitEngine& engine);
    ~EngineSettingsPanel() override;

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
    juce::Label outputGroupLabel;
    juce::ComboBox micLayoutCombo;
    juce::TextButton saveMicLayoutButton { "Set for future instances" };
    juce::Label micLayoutWarningLabel;
    std::vector<MicLayout> micLayouts;

    std::vector<std::unique_ptr<PotEntry>> pots;

    void addPot(const juce::String& name, const juce::String& tooltip, float* target, float minVal, float maxVal, float interval);
    void addPot(const juce::String& name, const juce::String& tooltip, int* target, int minVal, int maxVal);
    void addPotCommon(const juce::String& name,
                      const juce::String& tooltip,
                      float* target,
                      int* intTarget,
                      double minVal,
                      double maxVal,
                      double interval,
                      bool isInteger);
    void onSettingChanged();
    void timerCallback() override;
    void queueSettingsSave();
    void flushPendingSettingsSave();
    void rebuildMicLayouts();

    static constexpr int settingsSaveDebounceMs = 350;

public:
    std::function<void()> onGridSizeChanged;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineSettingsPanel)
};
