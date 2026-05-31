#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SourceItem.h"

/**
 * @file SourcePickerPopup.h
 * Standalone source (kit/instrument/articulation) picker popup.
 * Used by both PadContextPopup and InstrumentConfigPanel.
 */
class SourcePickerPopup : public juce::Component,
                          private juce::Timer {
public:
    explicit SourcePickerPopup(const std::vector<SourceItem>& items);
    ~SourcePickerPopup() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void setFilterType(KitLib::InstrumentSpec::Type type);

    /** Fires when an item is confirmed via the select button. */
    std::function<void(int)> onItemSelected;
    
    /** Fires on preview click with velocity. */
    std::function<void(int, juce::uint8)> onPreview;

private:
    static constexpr int selectButtonWidth = 26;
    static constexpr int previewButtonWidth = 60;

    const std::vector<SourceItem>& allItems;
    KitLib::InstrumentSpec::Type filterType = KitLib::InstrumentSpec::Type::Unknown;
    
    juce::TextEditor filterField;
    juce::ToggleButton relatedOnlyToggle { "related" };
    juce::ListBox listBox;

    struct FilteredEntry { 
        int originalIndex; 
    };
    std::vector<FilteredEntry> filteredEntries;
    std::vector<juce::String> cachedLowerDisplayText;

    void rebuildFiltered();
    void timerCallback() override;
    void queueRebuildFiltered();

    static constexpr int filterDebounceMs = 100;

    class ListModel : public juce::ListBoxModel {
    public:
        explicit ListModel(SourcePickerPopup& owner) : owner(owner) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
    private:
        SourcePickerPopup& owner;
    };
    
    ListModel listModel { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourcePickerPopup)
};
