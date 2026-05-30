#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/LayoutManager.h"
#include "MidiNoteAssignmentControl.h"
#include "UITheme.h"

class MidiMapTable : public juce::Component {
public:
    explicit MidiMapTable();
    ~MidiMapTable() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void populate(KitLayout* layout);

    /** Called when a MIDI note mapping changes. Owner should rebuild engine routing. */
    std::function<void()> onMappingChanged;

    /** Call from engine/processor when MIDI learn is active and a note is received. */
    void midiLearnNoteReceived(int midiNote);

    /** Callback fired when MIDI learn starts (true) or stops (false).
     *  Owner should forward incoming MIDI notes to midiLearnNoteReceived(). */
    std::function<void(bool)> onMidiLearnStateChanged;

private:
    static constexpr int rowHeight = UITheme::Spacing::rowLarge;
    static constexpr int headerHeight = UITheme::Spacing::rowLarge + 2;
    static constexpr int instColWidth = 90;
    static constexpr int artColWidth = 100;
    static constexpr int noteButtonWidth = 84;
    static constexpr int learnButtonSize = 22;
    static constexpr int midiControlGap = 3;
    static constexpr int columnGap = 12;

    KitLayout* layoutPtr = nullptr;

    struct MapRow {
        juce::String instrumentId;
        juce::String articulationCode;
        bool isController = false;
        int controllerNumber = -1;

        // Pointers into the layout's notes vector for direct editing
        KitLayout::MidiNotes* notesPtr = nullptr;

        juce::Label instrumentLabel;
        juce::Label articulationLabel;
        std::unique_ptr<MidiNoteAssignmentControl> midiAssignmentControl;
        bool showInstrumentLabel = false;
    };

    std::vector<std::unique_ptr<MapRow>> rows;
    juce::Viewport viewport;
    juce::Component contentComponent;

    // MIDI learn state
    int activeLearningRow = -1;
    int activeLearningSlot = -1; // 0 = first button, 1 = second button

    void buildRows();
    void syncNoteButtons(MapRow& row);
    void applyNoteToSlot(MapRow& row, int slotIdx, int note);
    void startMidiLearn(int rowIdx, int slotIdx);
    void stopMidiLearn();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMapTable)
};
