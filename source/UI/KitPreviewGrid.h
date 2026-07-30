#pragma once

#include "SourceItem.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

class KitPreviewGrid final : public juce::Component,
                             private juce::Timer {
public:
    struct Item {
        juce::String groupId;
        juce::String groupTitle;
        SourceItem source;
    };

    KitPreviewGrid() = default;
    ~KitPreviewGrid() override = default;

    void setItems(std::vector<Item> newItems);
    int getPreferredHeight(int availableWidth);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    std::function<void(const SourceItem&, juce::uint8)> onPreview;

private:
    struct ItemBounds {
        size_t itemIndex = 0;
        juce::Rectangle<int> bounds;
    };

    static constexpr int groupGap = 8;
    static constexpr int groupHeaderHeight = 18;
    static constexpr int padGap = 4;
    static constexpr int padHeight = 26;
    static constexpr int preferredPadWidth = 104;

    std::vector<Item> items;
    std::vector<ItemBounds> itemBounds;
    int flashedItemIndex = -1;

    int layoutItems(int availableWidth, bool storeBounds);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitPreviewGrid)
};
