#include "MidiNotePicker.h"

static const char* const NOTE_NAMES_PICKER[] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};
static const bool IS_BLACK_KEY_PICKER[12] = {
    false, true, false, true, false, false, true, false, true, false, true, false
};



// ─────────────────────────────────────────────────────────────────────────────

MidiNotePicker::MidiNotePicker() {
    setSize(COL_PAIRS * PAIR_W, ROWS * CELL_H);
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

int MidiNotePicker::noteAt(int pairIdx, int row) {
    // Virtual top note of pair 0 is B9 = 131 (above MIDI range).
    // Each pair covers 24 descending semitones (2 octaves, B→C).
    const int note = 131 - pairIdx * 24 - row;
    return (note >= 0 && note <= 127) ? note : -1;
}

juce::Colour MidiNotePicker::cellBg(int note, bool isBlackKey, bool isHighlighted) {
    if (note < 0)
        return juce::Colour(0xffDDDDDD);

    auto base = isBlackKey ? juce::Colour(0xff222222) : juce::Colour(0xffE8E8E8);
    return isHighlighted ? (isBlackKey ? base.brighter(0.6f) : base.darker(0.25f)) : base;
}

// ─── Paint ───────────────────────────────────────────────────────────────────

void MidiNotePicker::paint(juce::Graphics& g) {
    auto mono = juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
    g.setFont(mono);

    for (int pairIdx = 0; pairIdx < COL_PAIRS; ++pairIdx) {
        const int pairX = pairIdx * PAIR_W;

        // Vertical separator between pairs
        if (pairIdx > 0) {
            g.setColour(juce::Colour(0xffAAAAAA));
            g.fillRect(pairX, 0, 1, getHeight());
        }

        for (int row = 0; row < ROWS; ++row) {
            const int note = noteAt(pairIdx, row);

            const juce::Rectangle<int> nameCell(pairX, row * CELL_H, NAME_W, CELL_H);
            const juce::Rectangle<int> numCell(pairX + NAME_W, row * CELL_H, NUM_W, CELL_H);

            const bool isBlack     = (note >= 0) && IS_BLACK_KEY_PICKER[note % 12];
            const bool highlighted = (note >= 0) && (note == this->currentNote);

            // Background — name half uses key colour, number half always white
            auto bg = cellBg(note, isBlack, highlighted);
            g.setColour(bg);
            g.fillRect(nameCell);
            g.setColour(note >= 0 ? juce::Colour(0xffF4F4F4) : juce::Colour(0xffDDDDDD));
            g.fillRect(numCell);

            // Horizontal separator
            g.setColour(juce::Colour(0xffAAAAAA));
            g.fillRect(pairX, nameCell.getBottom() - 1, PAIR_W, 1);

            if (note < 0) continue;

            // Note name (append octave on C notes)
            const int semitone = note % 12;
            juce::String name(NOTE_NAMES_PICKER[semitone]);
            if (semitone == 0)
                name += juce::String((note / 12) - 2);

            // Name text: white on black keys, dark on white keys
            g.setColour(isBlack ? juce::Colour(0xffEEEEEE) : juce::Colour(0xff222222));
            g.drawText(name, nameCell.withTrimmedLeft(4), juce::Justification::centredLeft);

            // Number text: always dark on white bg
            g.setColour(juce::Colour(0xff222222));
            g.drawText(juce::String(note), numCell.withTrimmedRight(3), juce::Justification::centredRight);
        }
    }

    // Outer border
    g.setColour(juce::Colour(0xff999999));
    g.drawRect(getLocalBounds(), 1);
}

// ─── Mouse ───────────────────────────────────────────────────────────────────

void MidiNotePicker::mouseUp(const juce::MouseEvent& e) {
    const int pairIdx = e.x / PAIR_W;
    const int row     = e.y / CELL_H;

    if (pairIdx < 0 || pairIdx >= COL_PAIRS || row < 0 || row >= ROWS)
        return;

    const int note = noteAt(pairIdx, row);
    if (note < 0) return;

    setCurrentNote(note);

    if (this->onNoteSelected)
        this->onNoteSelected(note);
}
