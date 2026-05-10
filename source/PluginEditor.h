#pragma once

#include "BinaryData.h"
#include "PluginProcessor.h"
#include "EngineSettingsPanel.h"
#include "UI/LookAndFeel.h"
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
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    PluginProcessor& processorRef;
    ::LookAndFeel lookAndFeel;
    std::unique_ptr<melatonin::Inspector> inspector;
    juce::TextButton inspectButton { "Inspect the UI" };
    juce::TextEditor referencePanel;
    juce::Label referencePanelLabel;
    juce::TextEditor instrumentMapEditor;
    juce::Label instrumentMapLabel;
    juce::TextButton applyButton { "Apply instrument map" };
    juce::Label statusLabel;
    juce::TextButton settingsTabButton { "Engine Settings" };
    std::unique_ptr<EngineSettingsPanel> settingsPanel;
    bool showingSettings = false;
    void toggleSettingsView();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
