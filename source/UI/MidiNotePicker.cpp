#include "MidiNotePicker.h"

static const char* const NOTE_NAMES_PICKER[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
static const bool IS_BLACK_KEY_PICKER[12] = {
    false, true, false, true, false, false, true, false, true, false, true, false
};

// 6 hues cycling across octaves (red → orange → green → cyan → blue → purple)
static const float OCTAVE_HUES_PICKER[6] = {
    0.00f, 0.12f, 0.35f, 0.55f, 0.67f, 0.80f
};

// ─────────────────────────────────────────────────────────────────────────────

MidiNotePicker::MidiNotePicker() {
    setSize(TOTAL_COLS * CELL_W, ROWS * CELL_H);
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

int MidiNotePicker::noteAt(int col, int row) {
    if (row < SPACERS) return -1;  // spacer rows

    // Name columns: col 0..5 → same note range as number columns 6..11
    const int noteCol = (col < NAME_COLS) ? col : (col - NAME_COLS);
    const int noteRow = row - SPACERS;
    const int note    = 127 - noteCol * NOTES_PER_COL - noteRow;

    return (note >= 0 && note <= 127) ? note : -1;
}

juce::Colour MidiNotePicker::cellBg(int note, bool isBlackKey, bool isHighlighted) {
    if (note < 0)
        return juce::Colour(0xff111111);  // spacer / out-of-range

    const float hue = OCTAVE_HUES_PICKER[(note / 12) % 6];
    const float sat = isBlackKey ? 0.60f : 0.32f;
    const float bri = isBlackKey ? 0.14f : 0.24f;

    auto base = juce::Colour::fromHSV(hue, sat, bri, 1.0f);
    return isHighlighted ? base.brighter(1.5f) : base;
}

// ─── Paint ───────────────────────────────────────────────────────────────────

void MidiNotePicker::paint(juce::Graphics& g) {
    // Use a monospaced font for clean column alignment
    auto mono = juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
    g.setFont(mono);

    for (int col = 0; col < TOTAL_COLS; ++col) {
        const bool isNumCol = (col >= NAME_COLS);

        // Thin vertical separator between name block and number block
        if (col == NAME_COLS) {
            g.setColour(juce::Colour(0xff444444));
            g.fillRect(col * CELL_W, 0, 1, getHeight());
        }

        for (int row = 0; row < ROWS; ++row) {
            const int note = noteAt(col, row);
            const juce::Rectangle<int> cell(col * CELL_W, row * CELL_H, CELL_W, CELL_H);

            const bool bk          = (note >= 0) && IS_BLACK_KEY_PICKER[note % 12];
            const bool highlighted = (note >= 0) && (note == this->currentNote);

            // Background
            g.setColour(cellBg(note, bk, highlighted));
            g.fillRect(cell);

            // Thin horizontal line between cells
            g.setColour(juce::Colour(0xff333333));
            g.fillRect(cell.getX(), cell.getBottom() - 1, cell.getWidth(), 1);

            if (note < 0) continue;  // spacer — no text

            // Text colour: slightly dim for black keys
            g.setColour(juce::Colour(0xffffffff).withAlpha(bk ? 0.72f : 0.92f));

            if (isNumCol) {
                // Right-aligned note number
                g.drawText(juce::String(note),
                           cell.withTrimmedRight(3),
                           juce::Justification::centredRight);
            } else {
                // Left-aligned note name
                g.drawText(juce::String(NOTE_NAMES_PICKER[note % 12]),
                           cell.withTrimmedLeft(4),
                           juce::Justification::centredLeft);
            }
        }
    }

    // Outer border
    g.setColour(juce::Colour(0xff555555));
    g.drawRect(getLocalBounds(), 1);
}

// ─── Mouse ───────────────────────────────────────────────────────────────────

void MidiNotePicker::mouseUp(const juce::MouseEvent& e) {
    const int col = e.x / CELL_W;
    const int row = e.y / CELL_H;

    if (col < 0 || col >= TOTAL_COLS || row < 0 || row >= ROWS)
        return;

    const int note = noteAt(col, row);
    if (note < 0) return;  // spacer or out-of-range — ignore

    setCurrentNote(note);

    if (this->onNoteSelected)
        this->onNoteSelected(note);
}
