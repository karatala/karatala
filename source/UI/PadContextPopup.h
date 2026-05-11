#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"

class PadContextPopup : public juce::Component {
public:
    explicit PadContextPopup(DrumKitEngine& engine, const KitLayout::InstrumentSlot& slot, int artIdx);
    ~PadContextPopup() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onDismiss;

private:
    DrumKitEngine& engine;
    juce::String slotId;
    juce::String artName;
    juce::String midiNotesText;

    juce::Label slotLabel;
    juce::Label artLabel;
    juce::Label midiLabel;
    juce::TextButton previewButton { "Preview" };
    juce::TextButton closeButton { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadContextPopup)
};
