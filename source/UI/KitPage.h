#pragma once

#include "../Engine/DrumKitEngine.h"
#include "KitPreviewGrid.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

class KitPage final : public juce::Component,
                      private juce::Timer {
public:
    explicit KitPage(DrumKitEngine& engine);
    ~KitPage() override;

    void refresh(const juce::String& activeInstrumentMapText);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

    std::function<bool(const juce::String&, const juce::String&)> onMapLoadRequested;
    std::function<void(const juce::String&)> onStatusMessage;

private:
    enum class KitSection {
        LibraryKits,
        Presets,
    };

    struct KitEntry {
        juce::String id;
        juce::String rawId;
        juce::String name;
        juce::String author;
        juce::String license;
        juce::String readmeMarkdown;
        juce::String searchText;
    };

    struct PresetEntry {
        juce::File file;
        juce::String name;
        bool isUser = false;
        juce::String searchText;
    };

    class SourceListModel final : public juce::ListBoxModel {
    public:
        explicit SourceListModel(KitPage& pageOwner) : owner(pageOwner) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& graphics, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged(int lastRowSelected) override;

    private:
        KitPage& owner;
    };

    static constexpr int filterDebounceMs = 100;

    DrumKitEngine& engine;
    KitSection activeSection = KitSection::LibraryKits;
    juce::String currentMapText;
    juce::String selectedKitId;
    juce::String selectedPresetPath;
    std::vector<SourceItem> sourceItems;
    std::vector<KitEntry> kits;
    std::vector<PresetEntry> presets;
    std::vector<int> filteredIndexes;

    juce::Label titleLabel;
    juce::TextButton kitsSectionButton { "LIBRARY KITS" };
    juce::TextButton presetsSectionButton { "PRESETS" };
    juce::TextEditor filterField;
    juce::ListBox sourceList;
    SourceListModel sourceListModel { *this };
    juce::Label infoTitleLabel;
    juce::Label infoAuthorLabel;
    juce::Label infoLicenseLabel;
    juce::TextEditor detailsEditor;
    juce::Label previewLabel;
    juce::Viewport previewViewport;
    KitPreviewGrid previewGrid;
    juce::TextButton loadButton { "LOAD KIT" };
    juce::TextButton saveButton { "SAVE" };
    juce::TextButton saveAsButton { "SAVE AS" };
    juce::TextButton deleteButton { "DELETE" };

    void setActiveSection(KitSection section);
    void rebuildLibraryEntries();
    void rebuildPresetEntries(const juce::String& preferredSelectionPath = {});
    void rebuildFilteredEntries();
    void updateSelectedContent();
    void updateActionButtons();
    void updatePreviewGridSize();
    void loadSelected();
    void loadSelectedKit();
    void loadSelectedPreset();
    void saveSelectedPreset();
    void savePresetAs();
    void deleteSelectedPreset();
    void reportStatus(const juce::String& message);
    void timerCallback() override;

    const KitEntry* getSelectedKit() const;
    const PresetEntry* getSelectedPreset() const;
    juce::File getUserPresetDirectory() const;
    std::vector<KitPreviewGrid::Item> buildKitPreviewItems(const KitEntry& kit) const;
    std::vector<KitPreviewGrid::Item> buildPresetPreviewItems(const PresetEntry& preset) const;
    static bool matchesFilter(const juce::String& searchText, const juce::String& filterText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitPage)
};
