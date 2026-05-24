#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Custom MIDI note grid picker.
 *
 * Layout: 12 columns (6 note-name columns + 6 note-number columns).
 * Each column has SPACERS placeholder rows followed by NOTES_PER_COL
 * note rows.  The two column groups are displayed side by side so the
 * grid looks like a piano keyboard legend:
 *
 *   ┌──────────────────┬──────────────────┐
 *   │  name columns    │  number columns  │
 *   │  G  F# F  E  …   │  127 126 125 … │
 *   └──────────────────┴──────────────────┘
 *
 * Clicking any name or number cell fires onNoteSelected(noteNumber).
 * Pass -1 to deselect.  Clicking a spacer row does nothing.
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
    static constexpr int NOTES_PER_COL = 24;
    static constexpr int SPACERS       = 4;
    static constexpr int ROWS          = SPACERS + NOTES_PER_COL; // 28
    static constexpr int NAME_COLS     = 6;
    static constexpr int NUM_COLS      = 6;
    static constexpr int TOTAL_COLS    = NAME_COLS + NUM_COLS;    // 12
    static constexpr int CELL_W        = 32;
    static constexpr int CELL_H        = 13;

    int currentNote = -1;

    /** Returns the MIDI note for a given column/row, or -1 for spacers and out-of-range. */
    static int noteAt(int col, int row);

    /** Background colour for a cell. */
    static juce::Colour cellBg(int note, bool isBlackKey, bool isHighlighted);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiNotePicker)
};
