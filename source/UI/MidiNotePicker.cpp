#include "MidiNotePicker.h"
#include "UITheme.h"

static const char* const NOTE_NAMES_PICKER[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
static const bool IS_BLACK_KEY_PICKER[12] = {
    false, true, false, true, false, false, true, false, true, false, true, false
};



// ─────────────────────────────────────────────────────────────────────────────

MidiNotePicker::MidiNotePicker() {
    setSize(MARGIN + COL_PAIRS * PAIR_W + (COL_PAIRS - 1) * GAP + MARGIN,
            ROWS * CELL_H);
}

void MidiNotePicker::setCurrentNote(int note) {
    this->currentNote = note;
    repaint();
}

juce::String MidiNotePicker::noteToButtonText(int note) {
    if (note < 0 || note > 127)
        return "--";
    const int octave = (note / 12) - 2;
    return juce::String(note) + " / "
         + juce::String(NOTE_NAMES_PICKER[note % 12])
         + " " + juce::String(octave);
}

// ─── Static helpers ──────────────────────────────────────────────────────────

int MidiNotePicker::pairX(int pairIdx) {
    return MARGIN + pairIdx * (PAIR_W + GAP);
}

int MidiNotePicker::noteAt(int pairIdx, int row) {
    // Virtual top note of pair 0 is B9 = 131 (above MIDI range).
    // Each pair covers 24 descending semitones (2 octaves, B→C).
    const int note = 131 - pairIdx * 24 - row;
    return (note >= 0 && note <= 127) ? note : -1;
}

juce::Colour MidiNotePicker::cellBg(int note, bool isBlackKey, bool isHighlighted) {
    if (note < 0)
        return UITheme::Colours::borderMid;

    if (isHighlighted)
        return UITheme::Colours::selectionStrong;

    return isBlackKey ? UITheme::Colours::keyBlack : UITheme::Colours::keyWhite;
}

// ─── Paint ───────────────────────────────────────────────────────────────────

void MidiNotePicker::paint(juce::Graphics& g) {
    // Dark grey background (visible as margins and gaps)
    g.fillAll(UITheme::Colours::borderMid);

    auto mono = UITheme::Fonts::mono().withHeight(10.5f);
    g.setFont(mono);

    for (int idx = 0; idx < COL_PAIRS; ++idx) {
        const int px = pairX(idx);

        for (int row = 0; row < ROWS; ++row) {
            const int note = noteAt(idx, row);

            const juce::Rectangle<int> nameCell(px, row * CELL_H, NAME_W, CELL_H);
            const juce::Rectangle<int> numCell(px + NAME_W, row * CELL_H, NUM_W, CELL_H);

            const bool isBlack     = (note >= 0) && IS_BLACK_KEY_PICKER[note % 12];
            const bool highlighted = (note >= 0) && (note == this->currentNote);

            // Background — name half uses key colour, number half always white
            auto bg = cellBg(note, isBlack, highlighted);
            g.setColour(bg);
            g.fillRect(nameCell);
            g.setColour(note >= 0 ? (highlighted ? UITheme::Colours::selectionStrong : UITheme::Colours::keyWhite)
                                  : UITheme::Colours::borderMid);
            g.fillRect(numCell);

            // Horizontal separator
            g.setColour(UITheme::Colours::borderMid);
            g.fillRect(px, nameCell.getBottom() - 1, PAIR_W, 1);

            if (note < 0) continue;

            // Note name (append octave on C notes)
            const int semitone = note % 12;
            juce::String name(NOTE_NAMES_PICKER[semitone]);
            if (semitone == 0)
                name += juce::String((note / 12) - 2);

            // Name text: white on black/highlighted keys, dark on white keys
            g.setColour((isBlack || highlighted) ? UITheme::Colours::keyBlackText : UITheme::Colours::keyWhiteText);
            g.drawText(name, nameCell.withTrimmedLeft(4), juce::Justification::centredLeft);

            // Number text: always dark
            g.setColour(UITheme::Colours::keyWhiteText);
            g.drawText(juce::String(note), numCell.withTrimmedRight(3), juce::Justification::centredRight);
        }
    }
}

// ─── Mouse ───────────────────────────────────────────────────────────────────

void MidiNotePicker::mouseUp(const juce::MouseEvent& e) {
    const int row = e.y / CELL_H;
    if (row < 0 || row >= ROWS)
        return;

    const int adjustedX = e.x - MARGIN;
    if (adjustedX < 0) return;

    const int stride = PAIR_W + GAP;
    const int idx    = adjustedX / stride;
    const int localX = adjustedX % stride;

    // Click landed in the gap or beyond the last pair
    if (idx < 0 || idx >= COL_PAIRS || localX >= PAIR_W)
        return;

    const int note = noteAt(idx, row);
    if (note < 0) return;

    // Toggle: clicking the already-selected note clears it
    const int newNote = (note == this->currentNote) ? -1 : note;
    setCurrentNote(newNote);

    if (this->onNoteSelected)
        this->onNoteSelected(newNote);
}
