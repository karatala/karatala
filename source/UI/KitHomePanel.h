#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"
#include "ScrollablePanel.h"

class KitHomePanel : public ScrollablePanel {
public:
    struct Commit {
        KitLayout layout;
        juce::String instrumentMapText;
        bool setGlobalKitLayout = false;
        juce::String globalMicLayoutId;
    };

    explicit KitHomePanel(DrumKitEngine& engine);
    ~KitHomePanel() override = default;

    void resized() override;
    void refresh(const juce::String& currentMapText);

    std::function<void(Commit)> onLoadRequested;
    std::function<void(const juce::String&)> onStatusMessage;

private:
    struct LayoutEntry {
        KitLayout layout;
        bool isUser = false;
    };

    struct SourceChoice {
        juce::String kitCode;
        juce::String instrumentCode;
        juce::String rawKitCode;
        juce::String rawInstrumentCode;
        juce::String display;
        KitLib::InstrumentSpec::Type type = KitLib::InstrumentSpec::Type::Unknown;
    };

    struct SlotRow {
        juce::String slotId;
        juce::Label slotLabel;
        juce::ComboBox typeCombo;
        juce::TextEditor balanceEditor;
        juce::ComboBox directMicOutsCombo;
        juce::ComboBox sourceCombo;
        std::vector<SourceChoice> choices;
    };

    struct PresetEntry {
        juce::File file;
        juce::String name;
        bool isUser = false;
    };

    DrumKitEngine& engine;
    juce::String draftMapText;
    std::vector<LayoutEntry> layouts;
    std::vector<const KitLib::DrumKitSpec*> kits;
    std::vector<MicLayout> micLayouts;
    std::vector<PresetEntry> presets;
    std::vector<std::unique_ptr<SlotRow>> slotRows;

    juce::Label titleLabel;
    juce::Label introLabel;
    juce::Label layoutLabel;
    juce::ComboBox layoutCombo;
    juce::ToggleButton globalLayoutToggle { "Use as default for new instances" };
    juce::Label kitLabel;
    juce::ComboBox kitCombo;

    juce::Label presetLabel;
    juce::ComboBox presetCombo;
    juce::TextButton presetLoadButton { "Load preset" };
    juce::TextButton presetSaveButton { "Save" };
    juce::TextButton presetDuplicateButton { "Duplicate" };
    juce::TextButton presetDeleteButton { "Delete" };

    juce::Label assignmentsLabel;
    juce::Label headerSlotLabel;
    juce::Label headerTypeLabel;
    juce::Label headerBalanceLabel;
    juce::Label headerDirectLabel;
    juce::Label headerSourceLabel;
    juce::Label micLabel;
    juce::ComboBox micCombo;
    juce::Label micWarningLabel;
    juce::TextButton loadButton { "Load Kit" };

    void rebuildLayoutEntries();
    void rebuildKitEntries();
    void rebuildMicEntries();
    void rebuildPresetEntries();
    void rebuildSlotRows();
    void generateDraftFromSelectedKit();
    void replaceDraftSlot(const SlotRow& row);
    void applySlotMetadata(SlotRow& row);
    void loadSelectedPreset();
    void savePreset(bool forceNewName);
    void deleteSelectedPreset();
    void updatePresetButtons();
    const LayoutEntry* getSelectedLayout() const;
    juce::File getUserPresetDirectory() const;
    void reportStatus(const juce::String& message);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitHomePanel)
};
