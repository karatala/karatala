#pragma once

#include "BinaryData.h"
#include "PluginProcessor.h"
#include "UI/EngineSettingsPanel.h"
#include "UI/LookAndFeel.h"
#include "UI/Sidebar.h"
#include "UI/StatusBar.h"
#include "UI/DrumPadGrid.h"
#include "melatonin_inspector/melatonin_inspector.h"

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor {
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    PluginProcessor& processorRef;
    ::LookAndFeel lookAndFeel;
    std::unique_ptr<melatonin::Inspector> inspector;

    // Layout components
    Sidebar sidebar;
    StatusBar statusBar;

    // Mode: Zen / Zoned — drum pad grid
    DrumPadGrid drumPadGrid;

    // Mode: Raw — existing instrument map editor
    juce::TextEditor referencePanel;
    juce::Label referencePanelLabel;
    juce::TextEditor instrumentMapEditor;
    juce::Label instrumentMapLabel;
    juce::TextButton applyButton { "Apply instrument map" };

    // Mode: Settings
    std::unique_ptr<EngineSettingsPanel> settingsPanel;

    ViewMode activeMode = ViewMode::Zen;
    void setViewMode(ViewMode mode);
    void showComponentsForMode(ViewMode mode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
