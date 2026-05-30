#pragma once

#include "BinaryData.h"
#include "PluginProcessor.h"
#include "UI/EngineSettingsPanel.h"
#include "UI/LookAndFeel.h"
#include "UI/Sidebar.h"
#include "UI/StatusBar.h"
#include "UI/DrumPadGrid.h"
#include "UI/DismissOverlay.h"
#include "UI/MidiMapTable.h"
#include "UI/InstrumentConfigPanel.h"
#include "UI/PadContextPopup.h"
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
    juce::TooltipWindow tooltipWindow { this, 500 };
    std::unique_ptr<melatonin::Inspector> inspector;

    // Layout components
    Sidebar sidebar;
    StatusBar statusBar;

    // Mode: Zen / Zoned — drum pad grid
    DrumPadGrid drumPadGrid;

    // Mode: Map — MIDI note assignment table
    MidiMapTable midiMapTable;

    // Mode: Raw — existing instrument map editor
    juce::TextEditor referencePanel;
    juce::Label referencePanelLabel;
    juce::TextEditor instrumentMapEditor;
    juce::Label instrumentMapLabel;
    juce::TextButton applyButton { "Apply instrument map" };

    // Mode: Settings
    std::unique_ptr<EngineSettingsPanel> settingsPanel;

    // Mode: Instrument Config
    std::unique_ptr<InstrumentConfigPanel> instrumentConfigPanel;

    // Pad context popup (Zone mode right-click)
    std::unique_ptr<PadContextPopup> padContextPopup;
    std::unique_ptr<DismissOverlay> padPopupOverlay;
    void dismissPadContextPopup();
    void showPadContextPopup(const KitLayout::InstrumentSlot* slot, int artIdx, juce::Point<int> screenPos);
    void flashPadForSlot(const juce::String& slotId, float velocity);
    void flashPadForMidiNote(int midiNote, float velocity);

    ViewMode activeMode = ViewMode::Zen;
    void setViewMode(ViewMode mode);
    void showComponentsForMode(ViewMode mode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
