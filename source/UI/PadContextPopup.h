#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"
#include "UITypes.h"
#include "SourcePickerPopup.h"
#include "MidiNotePicker.h"
#include "LabeledPot.h"

class PadContextPopup : public juce::Component {
public:
    explicit PadContextPopup(DrumKitEngine& engine, const KitLayout::InstrumentSlot& slot, int artIdx, const juce::String& savedInstrumentMapText = "");
    ~PadContextPopup() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void flushMapText();
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
    juce::Label artLabel;

    // ─── MIDI map ───
    juce::Label     midiHeaderLabel;
    juce::TextButton noteButton1;
    juce::TextButton noteButton2;
    juce::TextButton learnButton1 { juce::String::charToString(0x25CF) };
    juce::TextButton learnButton2 { juce::String::charToString(0x25CF) };
    KitLayout::MidiNotes* midiNotesPtr = nullptr;
    int activeLearningSlot = -1;
    // Active CallOutBox content tracked weakly so we can dismiss on MIDI learn
    juce::Component::SafePointer<MidiNotePicker> activeMidiPicker;

    void syncNoteButtons();
    void applyNoteToSlot(int slotIdx, int note);
    void showMidiNotePicker(int slotIdx);

    std::vector<SourceItem> sourceItems;
    void rebuildSourceItems();
    int findSourceIndex(const juce::String& searchPath) const;

    // ─── Instrument map layers ───
    struct LayerRow {
        int selectedSourceIndex = -1;
        std::unique_ptr<juce::TextButton> sourceButton;
        std::unique_ptr<LabeledPot> tuningPot;
        std::unique_ptr<LabeledPot> attackPot;
        std::unique_ptr<LabeledPot> decayPot;
        std::unique_ptr<LabeledPot> volumePot;
        std::unique_ptr<juce::TextButton> flipLRButton;
        std::unique_ptr<juce::TextButton> flipPhaseButton;
        // Per-mic bleed pots: one per relevant mic
        struct MicBleedControl {
            juce::String micCode;
            juce::String micName;
            std::unique_ptr<juce::Slider> bleedPot;
            std::unique_ptr<juce::Label> bleedLabel;
        };
        std::vector<std::unique_ptr<MicBleedControl>> micBleeds;
        std::unique_ptr<juce::TextButton> addButton;
        std::unique_ptr<juce::TextButton> removeButton;
    };
    juce::OwnedArray<LayerRow> layerRows;
    juce::Label layerHeaderLabel;

    void addLayerRow(const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs,
                     float volumeDb, bool flipLR, bool flipPhase,
                     const std::unordered_map<juce::String, float>& micBleedLevels);
    juce::uint8 getPreviewVelocity() const;
    int getLayerIndex(const LayerRow* layer) const;
    void applyLayerParametersFast(LayerRow* layer);
    void updateRemoveButtonVisibility();
    void applyToEngine();
    juce::String generateFullMapText() const;
    void populateLayers();
    std::vector<std::pair<juce::String, juce::String>> getRelevantMics() const;
    bool mapTextDirty = false;

    std::unique_ptr<SourcePickerPopup> activePickerPopup;
    class PickerOverlay : public juce::Component {
    public:
        std::function<void()> onClick;
        void mouseDown(const juce::MouseEvent&) override { if (this->onClick) this->onClick(); }
    };
    std::unique_ptr<PickerOverlay> pickerOverlay;

    void showSourcePicker(LayerRow* layer);
    void dismissSourcePicker();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadContextPopup)
};
