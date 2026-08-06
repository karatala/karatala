#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/LayoutManager.h"
#include "MidiNotePicker.h"
#include "UITheme.h"

class MidiNoteAssignmentControl : public juce::Component {
public:
    struct LayoutMetrics {
        int noteButtonWidth = 60;
        int learnButtonSize = UITheme::Sizes::standardControlHeight;
        int noteLearnGap = 0;
        int pairGap = 6;
        int verticalInset = 0;
    };

    MidiNoteAssignmentControl();
    ~MidiNoteAssignmentControl() override = default;

    static void applyNoteToSlot(KitLayout::MidiNotes& notes, int slotIdx, int note);

    void resized() override;

    void setNotes(int firstNote, int secondNote);
    void setLearningSlot(int slotIdx);
    int getLearningSlot() const { return this->learningSlot; }
    void setLayoutMetrics(LayoutMetrics metrics);
    int getPreferredWidth() const;

    std::function<void(int slotIdx, int note)> onNoteSelected;
    std::function<void(int slotIdx)> onLearnButtonClicked;

private:
    void updateNoteButtons();
    void updateLearnButtons();
    void showMidiNotePicker(int slotIdx);

    LayoutMetrics layoutMetrics;
    std::array<int, 2> noteValues { { -1, -1 } };
    int learningSlot = -1;

    juce::TextButton noteButton1 { "--" };
    juce::TextButton noteButton2 { "--" };
    juce::TextButton learnButton1 { UITheme::Symbols::filledCircle };
    juce::TextButton learnButton2 { UITheme::Symbols::filledCircle };
    juce::Component::SafePointer<MidiNotePicker> activeMidiPicker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiNoteAssignmentControl)
};
