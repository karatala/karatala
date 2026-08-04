#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/EngineSettings.h"
#include "../Engine/LayoutManager.h"
#include "CompactParameterControl.h"
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

    struct ControlEntry {
        std::unique_ptr<CompactParameterControl> parameterControl;
        RealtimeSetting<float>* target = nullptr;
        RealtimeSetting<int>* intTarget = nullptr;
        double defaultValue = 0.0;
    };

    struct ChoiceEntry {
        juce::Label label;
        juce::ComboBox comboBox;
        juce::TextButton resetButton { UITheme::Symbols::times };
        std::function<int()> loadSelectedId;
        std::function<void(int)> storeSelectedId;
        int defaultSelectedId = 1;
    };

    juce::Label generalGroupLabel;
    juce::Label libraryPathLabel;
    juce::TextEditor libraryPathEditor;
    juce::TextButton browseLibraryButton { "Browse..." };
    juce::Label globalVelocityResponseLabel;
    juce::ComboBox globalVelocityResponseCombo;
    juce::Label kitLayoutLabel;
    juce::ComboBox kitLayoutCombo;
    juce::Label micLayoutLabel;
    juce::ComboBox micLayoutCombo;
    juce::TextButton saveLayoutDefaultsButton { "Save layout defaults" };
    juce::Label layoutDefaultsWarningLabel;
    std::unique_ptr<CompactParameterControl> cacheLimitControl;
    std::unique_ptr<juce::FileChooser> libraryChooser;
    std::vector<juce::String> globalVelocityResponseDefinitions;
    std::vector<KitLayout> kitLayouts;
    std::vector<MicLayout> micLayouts;

    juce::Label chokeGroupLabel;
    juce::Label tailGroupLabel;
    juce::Label hihatGroupLabel;
    juce::Label uiGridGroupLabel;

    std::vector<std::unique_ptr<ControlEntry>> controls;
    std::vector<std::unique_ptr<ChoiceEntry>> choices;
    int hihatChoiceCount = 0;
    int uiChoiceStart = 0;

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
                      std::function<juce::String(double)> textFromValueFn = nullptr,
                      std::function<double(const juce::String&)> valueFromTextFn = nullptr,
                      double defaultValue = std::numeric_limits<double>::quiet_NaN());
    void addChoice(const juce::String& name,
                   const juce::String& tooltip,
                   const juce::StringArray& optionLabels,
                   int defaultSelectedId,
                   std::function<int()> loadSelectedId,
                   std::function<void(int)> storeSelectedId);
    void onSettingChanged();
    void timerCallback() override;
    void queueSettingsSave();
    void flushPendingSettingsSave();
    void rebuildGlobalVelocityResponseChoices();
    void applyGlobalVelocityResponseChoice();
    void rebuildLayoutChoices();
    void chooseLibraryPath();
    void saveLayoutDefaults();

    static constexpr int settingsSaveDebounceMs = 350;

public:
    std::function<void()> onGridSizeChanged;
    std::function<void()> onLibraryReloadRequested;
    std::function<bool(const juce::String&)> onGlobalVelocityResponseChanged;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineSettingsPanel)
};
