#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/InstrumentPlayer.h"
#include "DrumPad.h"

class DrumPadGrid : public juce::Component, private juce::Timer {
public:
    explicit DrumPadGrid();
    ~DrumPadGrid() override = default;

    void resized() override;

    void populate(const KitLayout* layout);
    void setGridSize(int columns, int rows);
    void setPadGap(int gap);
    void setClickVelocity(float velocity);
    void setLoadProgressSources(std::vector<InstrumentLoadProgressSource> sources);

    std::function<void(const KitLayout::InstrumentSlot*, int articulationIdx, const juce::String& previewArticulationCode, bool useDirectPreview, float velocity)> onPadClicked;
    std::function<void(const KitLayout::InstrumentSlot*, int articulationIdx, juce::Point<int> screenPos)> onPadRightClicked;

    void flashPad(const juce::String& slotId, float velocity);

private:
    std::vector<std::unique_ptr<DrumPad>> pads;
    std::vector<juce::String> padSlotIds;
    int gridColumns = 0;
    int gridRows = 0;
    int effectiveGridColumns = 1;
    int effectiveGridRows = 1;
    int padGap = 0;
    std::vector<InstrumentLoadProgressSource> loadProgressSources;

    void resolveGridSize(int instrumentCount);
    void updateLoadProgress();
    void timerCallback() override;

    static constexpr int loadProgressTimerHz = 10;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPadGrid)
};
