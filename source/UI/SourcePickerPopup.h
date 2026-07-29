#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/InstrumentCatalog.h"
#include "SourceItem.h"
#include <set>

/**
 * @file SourcePickerPopup.h
 * Standalone source (kit/instrument/articulation) picker popup.
 * Used by both PadContextPopup and InstrumentConfigPanel.
 */
class SourcePickerPopup : public juce::Component,
                          private juce::Timer {
public:
    enum class SelectionMode {
        Articulation,
        WholeInstrument,
    };

    struct InstrumentSelection {
        juce::String kitId;
        juce::String instrumentId;
    };

    explicit SourcePickerPopup(const std::vector<SourceItem>& items, const InstrumentCatalog& catalog, SelectionMode mode = SelectionMode::Articulation);
    ~SourcePickerPopup() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void setFilterType(KitLib::InstrumentSpec::Type type);
    void setSelectedItem(int originalIndex);
    void setTitle(const juce::String& title);
    void focusItemList();

    /** Fires when an item is confirmed via the select button. */
    std::function<void(int)> onItemSelected;

    /** Fires in whole-instrument mode. The selected articulation is used only for preview. */
    std::function<void(const InstrumentSelection&)> onInstrumentSelected;
    
    /** Fires on preview click with velocity. */
    std::function<void(int, juce::uint8)> onPreview;

    std::function<void()> onDismiss;

private:
    static constexpr int selectButtonWidth = 22;
    static constexpr int previewButtonWidth = 80;
    static constexpr int sidebarWidth = 110;

    enum class Category {
        All,
        Drums,
        Cymbals,
        Other,
    };

    const std::vector<SourceItem>& allItems;
    const InstrumentCatalog& catalog;
    SelectionMode selectionMode = SelectionMode::Articulation;
    KitLib::InstrumentSpec::Type selectedKind = KitLib::InstrumentSpec::Type::Unknown;
    Category selectedCategory = Category::All;
    
    juce::Label titleLabel;
    juce::TextButton closeButton;
    juce::TextEditor filterField;
    juce::TextButton allCategoryButton { "all" };
    juce::TextButton drumsCategoryButton { "drums" };
    juce::TextButton cymbalsCategoryButton { "cymbals" };
    juce::TextButton otherCategoryButton { "other" };
    juce::Label kindListLabel;
    juce::ListBox kindListBox;
    juce::Label articulationListLabel;
    juce::ListBox articulationListBox;
    juce::ListBox listBox;

    struct FilteredEntry { 
        int originalIndex; 
    };
    std::vector<FilteredEntry> filteredEntries;
    int actionRow = -1;
    int actionRowBeforeMouseSelection = -1;
    bool mouseSelectionChanged = false;
    bool suppressSelectionPreview = false;
    std::vector<KitLib::InstrumentSpec::Type> kindEntries;
    std::vector<juce::String> articulationEntries;
    std::vector<int> articulationSourceIndexes;
    std::set<juce::String> selectedArticulations;
    std::vector<juce::String> cachedLowerDisplayText;
    int selectedWholeInstrumentItemIndex = -1;

    void setCategory(Category category);
    void rebuildKindEntries();
    void rebuildArticulationEntries();
    void rebuildFiltered();
    bool matchesCategory(KitLib::InstrumentSpec::Type type) const;
    bool matchesCurrentFilters(const SourceItem& item, const juce::String& lowerDisplayText, const juce::String& filterText) const;
    Category categoryForType(KitLib::InstrumentSpec::Type type) const;
    static std::optional<InstrumentFamily> familyForCategory(Category category);
    static juce::String kindLabel(KitLib::InstrumentSpec::Type type);
    static juce::String wholeInstrumentKey(const SourceItem& item);
    static juce::String wholeInstrumentDisplayText(const SourceItem& item);
    void updateCategoryButtons();
    void confirmItem(int originalIndex);
    void timerCallback() override;
    void queueRebuildFiltered();

    static constexpr int filterDebounceMs = 100;

    class ListModel : public juce::ListBoxModel {
    public:
        explicit ListModel(SourcePickerPopup& popupOwner) : owner(popupOwner) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
        void selectedRowsChanged(int lastRowSelected) override;
        void returnKeyPressed(int lastRowSelected) override;
    private:
        SourcePickerPopup& owner;
    };
    
    ListModel listModel { *this };

    class KindListModel : public juce::ListBoxModel {
    public:
        explicit KindListModel(SourcePickerPopup& popupOwner) : owner(popupOwner) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
    private:
        SourcePickerPopup& owner;
    };

    KindListModel kindListModel { *this };

    class ArticulationListModel : public juce::ListBoxModel {
    public:
        explicit ArticulationListModel(SourcePickerPopup& popupOwner) : owner(popupOwner) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged(int lastRowSelected) override;
    private:
        SourcePickerPopup& owner;
    };

    ArticulationListModel articulationListModel { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourcePickerPopup)
};
