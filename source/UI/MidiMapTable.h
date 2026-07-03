#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/LayoutManager.h"
#include "MidiNoteAssignmentControl.h"
#include "ScrollablePanel.h"
#include "UITheme.h"

class MidiMapTable : public ScrollablePanel {
public:
    struct PresetEntry {
        juce::String id;
        juce::String name;
        bool isUser = false;
    };

    explicit MidiMapTable();
    ~MidiMapTable() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void populate(KitLayout* layout);
    void populatePresets(std::vector<PresetEntry> presets, const juce::String& currentId);

    /** Called when a MIDI note mapping changes. Owner should rebuild engine routing. */
    std::function<void()> onMappingChanged;
    /** Called when expression curve points change. Owner should reapply the current layout/map. */
    std::function<void()> onLayoutReapplyRequested;

    /** Called when user selects a preset from the dropdown. */
    std::function<void(const PresetEntry&)> onPresetSelected;
    /** Called when user clicks Save. */
    std::function<void()> onPresetSaveRequested;
    /** Called when user clicks Clone (from ... menu). */
    std::function<void()> onPresetCloneRequested;
    /** Called when user clicks Delete (from ... menu). */
    std::function<void()> onPresetDeleteRequested;
    /** Called when user clicks Open User Folder. */
    std::function<void()> onOpenUserFolderRequested;
    /** Called when user clicks Open Factory Folder. */
    std::function<void()> onOpenFactoryFolderRequested;

    /** Call from engine/processor when MIDI learn is active and a note is received. */
    void midiLearnNoteReceived(int midiNote);

    /** Callback fired when MIDI learn starts (true) or stops (false).
     *  Owner should forward incoming MIDI notes to midiLearnNoteReceived(). */
    std::function<void(bool)> onMidiLearnStateChanged;
    std::function<void()> onCrosstalkPopupRequested;

private:
    static constexpr int rowHeight = UITheme::Spacing::rowLarge;
    static constexpr int headerHeight = UITheme::Spacing::rowLarge + 2;
    static constexpr int toolbarHeight = 32;
    static constexpr int instColWidth = UITheme::Sizes::midiMapInstCol;
    static constexpr int artColWidth = UITheme::Sizes::midiMapArtCol;
    static constexpr int noteButtonWidth = 84;
    static constexpr int learnButtonSize = 22;
    static constexpr int midiControlGap = 3;
    static constexpr int columnGap = 12;
    static constexpr int layoutLabelWidth = 38;

    KitLayout* layoutPtr = nullptr;

    // Preset toolbar
    juce::Label layoutLabel;
    juce::ComboBox presetCombo;
    juce::TextButton saveButton { "Save" };
    juce::TextButton moreButton { "..." };
    juce::TextButton crosstalkButton { "Crosstalk..." };
    std::vector<PresetEntry> presetEntries;

    struct MapRow {
        juce::String instrumentId;
        juce::String articulationCode;
        bool isController = false;
        int controllerNumber = -1;

        // Pointers into the active layout for direct editing
        KitLayout::MidiNotes* notesPtr = nullptr;
        KitLayout::InstrumentSlot* slotPtr = nullptr;
        KitLayout::InstrumentSlot::ContinuousController* controllerPtr = nullptr;

        juce::Label instrumentLabel;
        juce::Label articulationLabel;
        std::unique_ptr<MidiNoteAssignmentControl> midiAssignmentControl;
        std::unique_ptr<juce::ComboBox> controllerCombo;
        std::unique_ptr<juce::TextButton> curveButton;
        bool showInstrumentLabel = false;
    };

    std::vector<std::unique_ptr<MapRow>> rows;
    // MIDI learn state
    int activeLearningRow = -1;
    int activeLearningSlot = -1; // 0 = first button, 1 = second button

    void buildRows();
    void syncNoteButtons(MapRow& row);
    void applyNoteToSlot(MapRow& row, int slotIdx, int note);
    void applyControllerNumber(MapRow& row, int controllerNumber);
    void showExpressionCurvePopup(MapRow& row);
    int getEffectiveExpressionPointValue(const KitLayout::InstrumentSlot& slot, const juce::String& codeRaw, const juce::String& label) const;
    void startMidiLearn(int rowIdx, int slotIdx);
    void stopMidiLearn();
    void showMoreMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMapTable)
};
