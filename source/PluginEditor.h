#pragma once

#include "BinaryData.h"
#include "PluginProcessor.h"
#include "UI/EngineSettingsPanel.h"
#include "UI/LookAndFeel.h"
#include "UI/Sidebar.h"
#include "UI/StatusBar.h"
#include "UI/DrumPadGrid.h"
#include "UI/DismissOverlay.h"
#include "UI/CrosstalkCalibrationPopup.h"
#include "UI/MidiMapTable.h"
#include "UI/InstrumentConfigPanel.h"
#include "UI/KitHomePanel.h"
#include "UI/PadContextPopup.h"
#include "UI/TroubleshootingPanel.h"
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

    // Mode: Kit - first-run setup and instrument-map presets
    std::unique_ptr<KitHomePanel> kitHomePanel;

    // Mode: Zen / Zone - drum pad grid
    DrumPadGrid drumPadGrid;

    // Mode: Map - MIDI note assignment table
    MidiMapTable midiMapTable;
    std::unique_ptr<CrosstalkCalibrationPopup> crosstalkPopup;
    std::unique_ptr<DismissOverlay> crosstalkPopupOverlay;
    void showCrosstalkPopup();
    void dismissCrosstalkPopup();

    // Mode: Raw - existing instrument map editor
    juce::TextEditor instrumentMapEditor;
    juce::Label instrumentMapLabel;
    juce::TextButton applyButton { "Apply instrument map" };

    // Mode: Settings
    std::unique_ptr<EngineSettingsPanel> settingsPanel;

    // Mode: Fix
    std::unique_ptr<TroubleshootingPanel> troubleshootingPanel;

    // Mode: Instrument Config
    std::unique_ptr<InstrumentConfigPanel> instrumentConfigPanel;

    // Pad context popup (Zone mode right-click)
    std::unique_ptr<PadContextPopup> padContextPopup;
    std::unique_ptr<DismissOverlay> padPopupOverlay;
    void dismissPadContextPopup();
    void showPadContextPopup(const KitLayout::InstrumentSlot* slot, int artIdx, juce::Point<int> screenPos);
    void flashPadForSlot(const juce::String& slotId, float velocity);
    void flashPadForMidiNote(int midiNote, float velocity);
    void markDrumPadGridDirty();
    void markMidiMapTableDirty();
    void markInstrumentConfigDirty();
    void refreshDrumPadGridIfNeeded(bool force = false);
    void refreshMidiMapTableIfNeeded(bool force = false);
    void refreshInstrumentConfigIfNeeded(bool force = false);
    void setupSidebarAndStatusBar();
    void setupKitMode();
    void setupDrumPadGridMode();
    void setupRawMode();
    void setupMapMode();
    void setupSettingsMode();
    void setupTroubleshootingMode();
    void setupInstrumentConfigMode();
    void reloadLibraryFromTroubleshooting();
    void fixMissingInstrumentsFromTroubleshooting();
    void setupSidebarKnobs();
    void applyInstrumentMapFromEditor();

        void refreshPresetList();
        void handlePresetSave();
        void handlePresetClone();
        void handlePresetDelete();
        void saveLayoutToUserPresets(const juce::String& fileName, const KitLayout& layout);
        juce::String makeUniqueCopyName(const juce::String& baseName);

    bool drumPadGridDirty = false;
    bool midiMapTableDirty = false;
    bool instrumentConfigDirty = true;
    juce::String instrumentConfigSnapshot;

    ViewMode activeMode = ViewMode::Kit;
    void setViewMode(ViewMode mode);
    void showComponentsForMode(ViewMode mode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
