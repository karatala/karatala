#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/EngineSettings.h"
#include "../Engine/LayoutManager.h"
#include "ParameterPot.h"
#include "ScrollablePanel.h"
#include "UITheme.h"
#include <limits>

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
        RealtimeSetting<float>* target = nullptr;
        RealtimeSetting<int>* intTarget = nullptr;
        double defaultValue = 0.0;
    };

    struct ChoiceEntry {
        juce::Label label;
        juce::ComboBox comboBox;
        juce::TextButton resetButton { "x" };
        std::function<int()> loadSelectedId;
        std::function<void(int)> storeSelectedId;
        int defaultSelectedId = 1;
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
    std::vector<std::unique_ptr<ChoiceEntry>> choices;

    void addPot(const juce::String& name,
                const juce::String& tooltip,
                RealtimeSetting<float>* target,
                float minVal,
                float maxVal,
                float interval,
                std::function<juce::String(double)> textFromValueFn = nullptr,
                std::function<double(const juce::String&)> valueFromTextFn = nullptr,
                double defaultValue = std::numeric_limits<double>::quiet_NaN());
    void addPot(const juce::String& name,
                const juce::String& tooltip,
                RealtimeSetting<int>* target,
                int minVal,
                int maxVal,
                int defaultValue = std::numeric_limits<int>::min());
    void addPotCommon(const juce::String& name,
                      const juce::String& tooltip,
                      RealtimeSetting<float>* target,
                      RealtimeSetting<int>* intTarget,
                      double minVal,
                      double maxVal,
                      double interval,
                      bool isInteger,
                      std::function<juce::String(double)> textFromValueFn = nullptr,
                      std::function<double(const juce::String&)> valueFromTextFn = nullptr,
                      double defaultValue = std::numeric_limits<double>::quiet_NaN());
    void addChoice(const juce::String& name,
                   const juce::String& tooltip,
                   const juce::StringArray& items,
                   int defaultSelectedId,
                   std::function<int()> loadSelectedId,
                   std::function<void(int)> storeSelectedId);
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
