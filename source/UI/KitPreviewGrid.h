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
    struct ArticulationGroupBounds {
        size_t firstArticulationPadIndex = 0;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> titleBounds;
    };

    struct ArticulationPadBounds {
        size_t articulationPadIndex = 0;
        juce::Rectangle<int> bounds;
    };

    static constexpr int cardPadding = 4;
    static constexpr int groupGap = 6;
    static constexpr int groupHeaderHeight = 18;
    static constexpr int minimumPadWidth = 72;
    static constexpr int padGap = 4;
    static constexpr int padHeight = 26;
    static constexpr int preferredGroupWidth = 180;

    std::vector<ArticulationPad> articulationPads;
    std::vector<ArticulationGroupBounds> articulationGroupBounds;
    std::vector<ArticulationPadBounds> articulationPadBounds;
    int flashedArticulationPadIndex = -1;

    int layoutArticulationPads(int availableWidth, bool storeBounds);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitPreviewGrid)
};
