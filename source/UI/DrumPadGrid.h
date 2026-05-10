#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "DrumPad.h"

class DrumPadGrid : public juce::Component {
public:
    explicit DrumPadGrid();
    ~DrumPadGrid() override = default;

    void resized() override;

    void populate(const KitLayout* layout);
    void setPadMode(PadMode mode);
    void setGridSize(int columns, int rows);
    void setClickVelocity(float velocity);

    std::function<void(const KitLayout::InstrumentSlot*, int articulationIdx, float velocity)> onPadClicked;

    void flashPad(const juce::String& slotId, float velocity);

private:
    std::vector<std::unique_ptr<DrumPad>> pads;
    PadMode padMode = PadMode::Zen;
    int gridColumns = 6;
    int gridRows = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPadGrid)
};
