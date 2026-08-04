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
#include "UI/KitBrowserComponent.h"
#include "UI/KitHomePanel.h"
#include "UI/PadContextPopup.h"
#include "UI/TroubleshootingPanel.h"
#include "UI/ThemePlaygroundPanel.h"
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

    // Page: Kit - first-run setup and instrument-map presets
    std::unique_ptr<KitHomePanel> kitHomePanel;

    // Page: Browse - library kits and instrument-map presets
    std::unique_ptr<KitBrowserComponent> kitBrowserComponent;

    // Page: Zone - drum pad grid
    DrumPadGrid drumPadGrid;

    // Page: Map - MIDI note assignment table
    MidiMapTable midiMapTable;
    std::unique_ptr<CrosstalkCalibrationPopup> crosstalkPopup;
    std::unique_ptr<DismissOverlay> crosstalkPopupOverlay;
    void showCrosstalkPopup();
    void dismissCrosstalkPopup();

    // Page: Raw - existing instrument map editor
    juce::TextEditor instrumentMapEditor;
    juce::Label instrumentMapLabel;
    juce::TextButton applyButton { "Apply" };

    // Page: Settings
    std::unique_ptr<EngineSettingsPanel> settingsPanel;

    // Page: Fix
    std::unique_ptr<TroubleshootingPanel> troubleshootingPanel;

    // Page: UI theme playground
    std::unique_ptr<ThemePlaygroundPanel> themePlaygroundPanel;

    // Page: Instrument Config
    std::unique_ptr<InstrumentConfigPanel> instrumentConfigPanel;

    // Pad context popup (Zone page right-click)
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
    void setupKitPage();
    void setupBrowsePage();
    void setupDrumPadGridPage();
    void setupRawPage();
    void setupMapPage();
    void setupSettingsPage();
    void setupTroubleshootingPage();
    void setupThemePlaygroundPage();
    void setupInstrumentConfigPage();
    void reloadLibraryFromTroubleshooting();
    void setupSidebarKnobs();
    void applyInstrumentMapFromEditor();
    bool applyBrowserInstrumentMap(const juce::String& mapText, const juce::String& statusText);

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

    PageId activePageId = PageId::Kit;
    void setPage(PageId pageId);
    void showComponentsForPage(PageId pageId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
