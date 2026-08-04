#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/InstrumentCatalog.h"
#include "AvailableLibraryArticulation.h"
#include <set>

/**
 * @file LibraryAssignmentPicker.h
 * Selects a library articulation or a complete library instrument for an instrument-map assignment.
 * Used by PadContextPopup and InstrumentConfigPanel.
 */
class LibraryAssignmentPicker : public juce::Component,
                          private juce::Timer {
public:
    enum class SelectionMode {
        Articulation,
        Instrument,
    };

    struct LibraryInstrumentSelection {
        juce::String kitId;
        juce::String instrumentId;
    };

    explicit LibraryAssignmentPicker(const std::vector<AvailableLibraryArticulation>& availableLibraryArticulations, const InstrumentCatalog& catalog, SelectionMode mode = SelectionMode::Articulation);
    ~LibraryAssignmentPicker() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void setFilterType(KitLib::InstrumentType type);
    void setSelectedLibraryArticulation(int libArtIndex);
    void setTitle(const juce::String& title);
    void focusAssignmentList();

    /** Fires when a library articulation is confirmed. */
    std::function<void(int)> onLibraryArticulationSelected;

    /** Fires in instrument mode. The selected articulation is used only for preview. */
    std::function<void(const LibraryInstrumentSelection&)> onLibraryInstrumentSelected;

    /** Fires on preview click with velocity. */
    std::function<void(int, juce::uint8)> onLibraryArticulationPreview;

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

    const std::vector<AvailableLibraryArticulation>& availableLibraryArticulations;
    const InstrumentCatalog& catalog;
    SelectionMode selectionMode = SelectionMode::Articulation;
    KitLib::InstrumentType selectedKind = KitLib::InstrumentType::Unknown;
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
    juce::ListBox assignmentListBox;

    std::vector<int> visibleLibraryArticulationIndexes;
    int actionRow = -1;
    int actionRowBeforeMouseSelection = -1;
    bool mouseSelectionChanged = false;
    bool suppressPreviewOnSelection = false;
    std::vector<KitLib::InstrumentType> kindEntries;
    std::vector<juce::String> articulationEntries;
    std::vector<int> previewLibraryArticulationIndexes;
    std::set<juce::String> selectedArticulations;
    std::vector<juce::String> cachedLowerDisplayLabels;
    int selectedLibraryInstrumentArticulationIndex = -1;

    void setCategory(Category category);
    void rebuildKindEntries();
    void rebuildArticulationEntries();
    void rebuildFiltered();
    bool matchesCategory(KitLib::InstrumentType type) const;
    bool matchesCurrentFilters(const AvailableLibraryArticulation& libArt, const juce::String& lowerDisplayLabel, const juce::String& filterText) const;
    Category categoryForType(KitLib::InstrumentType type) const;
    static std::optional<InstrumentFamily> familyForCategory(Category category);
    static juce::String kindLabel(KitLib::InstrumentType type);
    static juce::String qualifiedLibraryInstrumentId(const AvailableLibraryArticulation& libArt);
    static juce::String libraryInstrumentDisplayLabel(const AvailableLibraryArticulation& libArt);
    void updateCategoryButtons();
    void confirmAssignment(int libArtIndex);
    void timerCallback() override;
    void queueRebuildFiltered();

    static constexpr int filterDebounceMs = 100;

    class AssignmentListModel : public juce::ListBoxModel {
    public:
        explicit AssignmentListModel(LibraryAssignmentPicker& popupOwner) : owner(popupOwner) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
        void selectedRowsChanged(int lastRowSelected) override;
        void returnKeyPressed(int lastRowSelected) override;
    private:
        LibraryAssignmentPicker& owner;
    };

    AssignmentListModel assignmentListModel { *this };

    class KindListModel : public juce::ListBoxModel {
    public:
        explicit KindListModel(LibraryAssignmentPicker& popupOwner) : owner(popupOwner) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
    private:
        LibraryAssignmentPicker& owner;
    };

    KindListModel kindListModel { *this };

    class ArticulationListModel : public juce::ListBoxModel {
    public:
        explicit ArticulationListModel(LibraryAssignmentPicker& popupOwner) : owner(popupOwner) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged(int lastRowSelected) override;
    private:
        LibraryAssignmentPicker& owner;
    };

    ArticulationListModel articulationListModel { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryAssignmentPicker)
};
