#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"
#include "CompactParameterControl.h"
#include "DismissOverlay.h"
#include "ScrollablePanel.h"
#include "SourceItem.h"
#include "SourcePickerPopup.h"
#include "UITheme.h"

class InstrumentConfigPanel : public ScrollablePanel,
                              private juce::Timer {
public:
    explicit InstrumentConfigPanel(DrumKitEngine& engine);
    ~InstrumentConfigPanel() override = default;

    void resized() override;
    void populate(const juce::String& instrumentMapText);
    void flushMapText();

    std::function<void(const juce::String&)> onStatusMessage;
    std::function<void(const juce::String&)> onMapTextChanged;

private:
    DrumKitEngine& engine;
    bool populated = false;

    std::vector<SourceItem> sourceItems;
    std::vector<DrumKitEngine::VelocityResponsePreset> velocityResponsePresets;
    std::vector<DrumKitEngine::InstrumentMapLayer> unprojectedActiveSlotMapLayers;
    void rebuildSourceItems();

    // ─── Flat table row (one per articulation/layer) ───
    struct TableRow {
        juce::String slotId;
        juce::String artName;
        KitLib::InstrumentSpec::Type slotType = KitLib::InstrumentSpec::Type::Unknown;
        juce::String sourcePathHint;
        int selectedSourceIndex = -1;
        std::optional<juce::String> velocityResponse;
        std::unordered_map<juce::String, float> micBleedLevels;
        std::unordered_map<juce::String, juce::String> micRouteOverrides;

        std::unique_ptr<juce::Label> slotLabel;
        std::unique_ptr<juce::Label> artLabel;
        std::unique_ptr<juce::TextButton> sourceButton;
        std::unique_ptr<juce::Label> velocityResponseStatus;
        std::unique_ptr<CompactParameterControl> tuningControl;
        std::unique_ptr<CompactParameterControl> attackControl;
        std::unique_ptr<CompactParameterControl> decayControl;
        std::unique_ptr<CompactParameterControl> gainControl;
        std::unique_ptr<CompactParameterControl> panControl;
        std::unique_ptr<juce::TextButton> flipLRButton;
        bool flipLRExplicit = false;
        std::unique_ptr<juce::TextButton> flipPhaseButton;
        std::unique_ptr<juce::TextButton> addButton;
        std::unique_ptr<juce::TextButton> removeButton;
    };

    juce::OwnedArray<TableRow> tableRows;

    juce::Label headerSlot, headerArticulation, headerSource, headerResponse, headerTuning, headerAttack, headerDecay, headerGain, headerPan, headerLR, headerPhase;

    // Popup management
    std::unique_ptr<DismissOverlay> popupOverlay;
    std::unique_ptr<SourcePickerPopup> activePopup;

    void showPickerPopup(TableRow* row);
    void dismissPopup();
    void layoutPopup();
    void layoutContent();
    void applyToEngine(const juce::String& changedSlotId = {});
    void applyRowParametersFast(TableRow* row);
    void timerCallback() override;
    void updateSourceButtonDisplay(TableRow* row);
    void updateVelocityResponseStatus(TableRow* row);
    int getMappedLayerIndexForRow(const TableRow* row) const;
    juce::String generateMapText() const;
    void addRow(const juce::String& slotId, const juce::String& artName, KitLib::InstrumentSpec::Type slotType,
                const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs,
                float volumeDb, int pan, bool flipLR, bool flipLRExplicit, bool flipPhase, int insertionIndex = -1,
                const std::optional<juce::String>& velocityResponse = std::nullopt,
                const std::unordered_map<juce::String, float>& micBleedLevels = {},
                const std::unordered_map<juce::String, juce::String>& micRouteOverrides = {});
    int findSourceIndex(const juce::String& fullPath) const;
    void updateRemoveButtonVisibility();
    void queueMapTextFlush();

    bool mapTextDirty = false;

    static constexpr int mapTextFlushDebounceMs = 300;

    static constexpr int headerHeight = UITheme::Spacing::rowCompact;
    static constexpr int rowHeight = UITheme::Spacing::rowNormal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentConfigPanel)
};
