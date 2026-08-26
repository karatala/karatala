#pragma once

#include "../Engine/DrumKitEngine.h"
#include "KitPreviewGrid.h"
#include "AvailableLibraryArticulation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

class KitPage final : public juce::Component {
public:
    explicit KitPage(DrumKitEngine& engine);
    ~KitPage() override;

    void refresh(const juce::String& activeInstrumentMapText, const juce::String& activePresetReference);
    void setPreviewVelocity(float velocity);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

    std::function<bool(const juce::String&, const juce::String&)> onMapLoadRequested;
    std::function<void(const juce::String&)> onPresetReferenceChanged;
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
    };

    struct PresetEntry {
        juce::File file;
        juce::String reference;
        juce::String name;
        bool isUser = false;
    };

    static constexpr int currentSettingsEntryIndex = -1;

    class SectionListModel final : public juce::ListBoxModel {
    public:
        explicit SectionListModel(KitPage& pageOwner) : owner(pageOwner) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& graphics, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged(int lastRowSelected) override;

    private:
        KitPage& owner;
    };

    DrumKitEngine& engine;
    KitSection activeSection = KitSection::LibraryKits;
    juce::String currentMapText;
    juce::String selectedKitId;
    juce::String selectedPresetPath;
    juce::String activePresetReference;
    juce::String activePresetMapText;
    bool previewUsesActiveMap = false;
    std::vector<AvailableLibraryArticulation> availableLibraryArticulations;
    std::vector<KitEntry> kits;
    std::vector<PresetEntry> presets;
    std::vector<int> entryIndexes;

    juce::TextButton kitsSectionButton { "KITS" };
    juce::TextButton presetsSectionButton { "PRESETS" };
    juce::ListBox sectionList;
    SectionListModel sectionListModel { *this };
    juce::Label infoTitleLabel;
    juce::TextEditor detailsEditor;
    juce::Viewport previewViewport;
    KitPreviewGrid previewGrid;
    juce::TextButton primaryActionButton { "LOAD KIT" };
    juce::TextButton moreButton { "..." };

    void setActiveSection(KitSection section);
    void rebuildLibraryEntries();
    void rebuildPresetEntries(const juce::String& preferredSelectionPath = {});
    void rebuildListIndexes();
    void updateSelectedContent();
    void updateActionButtons();
    void updatePreviewGridSize();
    void performPrimaryAction();
    void showPresetActionsMenu();
    void loadSelectedKit();
    void loadSelectedPreset();
    void saveSelectedPreset();
    void savePresetAs();
    void deleteSelectedPreset();
    void reportStatus(const juce::String& message);

    const KitEntry* getSelectedKit() const;
    const PresetEntry* getSelectedPreset() const;
    bool isActivePreset(const PresetEntry& preset) const;
    bool isActivePresetDirty() const;
    bool shouldShowCurrentSettings() const;
    bool isCurrentSettingsSelected() const;
    void inferActivePreset();
    void setActivePreset(const PresetEntry& preset);
    void clearActivePreset();
    juce::File getUserPresetDirectory() const;
    std::vector<KitPreviewGrid::PreviewInstrument> buildKitPreviewInstruments(const KitEntry& kit) const;
    std::vector<KitPreviewGrid::PreviewInstrument> buildPresetPreviewInstruments(const juce::String& mapText) const;
    std::vector<KitPreviewGrid::PreviewTarget> resolveDirectPreviewTargets(const std::vector<KitPreviewGrid::PreviewTarget>& targets, const juce::String& articulationCode) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitPage)
};
