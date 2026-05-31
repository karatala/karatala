#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"
#include "DismissOverlay.h"
#include "SourceItem.h"
#include "SourcePickerPopup.h"
#include "UITheme.h"

class InstrumentConfigPanel : public juce::Component,
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
    void rebuildSourceItems();

    // ─── Flat table row (one per articulation/layer) ───
    struct TableRow {
        juce::String slotId;
        juce::String artName;
        KitLib::InstrumentSpec::Type slotType = KitLib::InstrumentSpec::Type::Unknown;
        juce::String sourcePathHint;
        int selectedSourceIndex = -1;

        std::unique_ptr<juce::Label> slotLabel;
        std::unique_ptr<juce::Label> artLabel;
        std::unique_ptr<juce::TextButton> sourceButton;
        std::unique_ptr<juce::Slider> tuningSlider;
        std::unique_ptr<juce::Slider> attackSlider;
        std::unique_ptr<juce::Slider> decaySlider;
        std::unique_ptr<juce::TextButton> addButton;
        std::unique_ptr<juce::TextButton> removeButton;
    };

    juce::OwnedArray<TableRow> tableRows;

    juce::Label headerSlot, headerInstrument, headerTuning, headerAttack, headerDecay;

    juce::Component contentComponent;
    juce::Viewport viewport;

    // Popup management
    std::unique_ptr<DismissOverlay> popupOverlay;
    std::unique_ptr<SourcePickerPopup> activePopup;

    void showPickerPopup(TableRow* row);
    void dismissPopup();
    void layoutContent();
    void applyToEngine();
    void applyRowParametersFast(TableRow* row);
    void timerCallback() override;
    void updateSourceButtonDisplay(TableRow* row);
    int getMappedLayerIndexForRow(const TableRow* row) const;
    juce::String generateMapText() const;
    void addRow(const juce::String& slotId, const juce::String& artName, KitLib::InstrumentSpec::Type slotType,
                const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs,
                int insertionIndex = -1);
    int findSourceIndex(const juce::String& fullPath) const;
    void updateRemoveButtonVisibility();
    void queueMapTextFlush();

    bool mapTextDirty = false;

    static constexpr int mapTextFlushDebounceMs = 300;

    static constexpr int rowHeight = UITheme::Spacing::rowNormal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentConfigPanel)
};
