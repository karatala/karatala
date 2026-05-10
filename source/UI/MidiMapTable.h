#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/LayoutManager.h"

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

private:
    static constexpr int rowHeight = 26;
    static constexpr int headerHeight = 28;

    static juce::String midiNoteToString(int note);
    static void populateNoteComboBox(juce::ComboBox& combo);

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
        juce::ComboBox noteCombo1;
        juce::ComboBox noteCombo2;
        juce::TextButton learnButton1 { juce::String::charToString(0x25CF) }; // ●
        juce::TextButton learnButton2 { juce::String::charToString(0x25CF) };
        bool showInstrumentLabel = false;
    };

    std::vector<std::unique_ptr<MapRow>> rows;
    juce::Viewport viewport;
    juce::Component contentComponent;

    // MIDI learn state
    int activeLearningRow = -1;
    int activeLearningSlot = -1; // 0 = first combo, 1 = second combo

    void buildRows();
    void syncComboFromNotes(MapRow& row);
    void applyComboToNotes(MapRow& row);
    void startMidiLearn(int rowIdx, int slotIdx);
    void stopMidiLearn();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMapTable)
};
