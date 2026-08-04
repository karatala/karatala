#pragma once

#include "AvailableLibraryArticulation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

class KitPreviewGrid final : public juce::Component,
                             private juce::Timer {
public:
    struct ArticulationPad {
        juce::String groupId;
        juce::String groupTitle;
        AvailableLibraryArticulation libraryArticulation;
    };

    KitPreviewGrid() = default;
    ~KitPreviewGrid() override = default;

    void setArticulationPads(std::vector<ArticulationPad> newArticulationPads);
    int getPreferredHeight(int availableWidth);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    std::function<void(const AvailableLibraryArticulation&, juce::uint8)> onLibraryArticulationPreview;

private:
    struct ArticulationPadBounds {
        size_t articulationPadIndex = 0;
        juce::Rectangle<int> bounds;
    };

    static constexpr int groupGap = 8;
    static constexpr int groupHeaderHeight = 18;
    static constexpr int padGap = 4;
    static constexpr int padHeight = 26;
    static constexpr int preferredPadWidth = 104;

    std::vector<ArticulationPad> articulationPads;
    std::vector<ArticulationPadBounds> articulationPadBounds;
    int flashedArticulationPadIndex = -1;

    int layoutArticulationPads(int availableWidth, bool storeBounds);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitPreviewGrid)
};
