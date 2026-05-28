#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Custom MIDI note grid picker.
 *
 * Layout: 6 column pairs (note-name + MIDI-number), 24 rows.
 * Each pair covers two octaves descending B→C.  Notes use mixed
 * sharp/flat naming: C C# D Eb E F F# G Ab A Bb B.
 * C notes show the octave label (e.g. "C4", "C-2").
 *
 * Clicking any cell fires onNoteSelected(noteNumber).
 * Pass -1 to deselect.
 */
class MidiNotePicker : public juce::Component {
public:
    MidiNotePicker();

    /** Called when the user picks a note (0-127) or -1 for "no note". */
    std::function<void(int)> onNoteSelected;

    /** Highlight the already-selected note (pass -1 for none). */
    void setCurrentNote(int note);

    /** Format a note as "127 / G 8" for display on the caller's button. */
    static juce::String noteToButtonText(int note);

    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    static constexpr int ROWS      = 24;
    static constexpr int COL_PAIRS = 6;
    static constexpr int NAME_W    = 28;
    static constexpr int NUM_W     = 30;
    static constexpr int PAIR_W    = NAME_W + NUM_W;  // 58
    static constexpr int GAP       = 4;               // dark grey gap between pairs
    static constexpr int MARGIN    = 4;               // dark grey margin on edges
    static constexpr int CELL_H    = 13;

    int currentNote = -1;

    /** X offset for a given column-pair index. */
    static int pairX(int pairIdx);

    /** Returns the MIDI note for a given column-pair / row, or -1 for out-of-range. */
    static int noteAt(int pairIdx, int row);

    /** Background colour for a cell. */
    static juce::Colour cellBg(int note, bool isBlackKey, bool isHighlighted);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiNotePicker)
};
