#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"
#include "SourceItem.h"
#include "SourcePickerPopup.h"
#include "MidiNoteAssignmentControl.h"
#include "ParameterPot.h"

class PadContextPopup : public juce::Component {
public:
    explicit PadContextPopup(DrumKitEngine& engine, const KitLayout::InstrumentSlot& slot, int artIdx, const juce::String& savedInstrumentMapText = "");
    ~PadContextPopup() override { this->dismissSourcePicker(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void flushMapText();
    juce::Rectangle<int> getPreferredBounds(const juce::Rectangle<int>& availableBounds) const;
    void setPreviewVelocityProvider(std::function<int()> provider) { this->previewVelocityProvider = std::move(provider); }

    std::function<void()> onDismiss;
    /** Fires whenever this popup changes the instrument map text. */
    std::function<void(const juce::String&)> onMapTextChanged;
    /** Fires when MIDI mapping changes (note combos edited). */
    std::function<void()> onMidiMappingChanged;

    /** Called when the user receives a MIDI note while learn is active. */
    void midiLearnNoteReceived(int midiNote);

    /** Callback fired when MIDI learn starts (true) or stops (false). */
    std::function<void(bool)> onMidiLearnStateChanged;

private:
    DrumKitEngine& engine;
    juce::String slotId;
    juce::String artName;
    juce::String savedInstrumentMapText;
    std::function<int()> previewVelocityProvider;
    KitLib::InstrumentSpec::Type slotType = KitLib::InstrumentSpec::Type::Unknown;

    // ─── Header ───
    juce::Label slotLabel;

    // ─── MIDI map (inline in header) ───
    juce::Label     midiHeaderLabel;
    std::unique_ptr<MidiNoteAssignmentControl> midiAssignmentControl;
    juce::TextButton addLayerButton { "+ Layer" };
    KitLayout::MidiNotes* midiNotesPtr = nullptr;
    int activeLearningSlot = -1;

    void syncNoteButtons();
    void applyNoteToSlot(int slotIdx, int note);

    std::vector<SourceItem> sourceItems;
    void rebuildSourceItems();
    int findSourceIndex(const juce::String& searchPath) const;

    // ─── Instrument map layers ───
    struct LayerRow {
        juce::String sourcePathHint;
        int selectedSourceIndex = -1;
        std::unique_ptr<juce::TextButton> sourceButton;
        std::unique_ptr<juce::Label> sourceDetailLabel; // kit/inst/art multiline
        std::unique_ptr<ParameterPot> tuningPot;
        std::unique_ptr<ParameterPot> attackPot;
        std::unique_ptr<ParameterPot> decayPot;
        std::unique_ptr<ParameterPot> gainPot;
        std::unique_ptr<juce::TextButton> flipLRButton;
        std::unique_ptr<juce::TextButton> flipPhaseButton;
        // Per-mic bleed pots
        struct MicBleedControl {
            juce::String micCode;
            juce::String micName;
            std::unique_ptr<ParameterPot> bleedPot;
        };
        std::vector<std::unique_ptr<MicBleedControl>> micBleeds;
        std::unique_ptr<juce::TextButton> removeButton;
        // Cached bounds for painting group backgrounds
        juce::Rectangle<int> mainGroupBounds;
        juce::Rectangle<int> bleedGroupBounds;
    };
    juce::OwnedArray<LayerRow> layerRows;

    void addLayerRow(const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs,
                     float volumeDb, bool flipLR, bool flipPhase,
                     const std::unordered_map<juce::String, float>& micBleedLevels);
    juce::uint8 getPreviewVelocity() const;
    int getLayerIndex(const LayerRow* layer) const;
    void applyLayerParametersFast(LayerRow* layer);
    void updateRemoveButtonVisibility();
    void updateSourceDisplay(LayerRow* layer);
    void applyToEngine();
    void recalculateLayerIndexOffset();
    juce::String generateFullMapText() const;
    void populateLayers();
    std::vector<std::pair<juce::String, juce::String>> getRelevantMics() const;
    bool mapTextDirty = false;
    int layerIndexOffset = 0;

    juce::Component::SafePointer<juce::CallOutBox> activePickerCallout;

    // ─── Scrollable layer area ───
    struct LayerContentComponent : public juce::Component {
        juce::OwnedArray<LayerRow>* layerRowsPtr = nullptr;
        void paint(juce::Graphics& g) override;
    };
    LayerContentComponent layerContent;
    juce::Viewport layerViewport;

    void showSourcePicker(LayerRow* layer);
    void dismissSourcePicker();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadContextPopup)
};
