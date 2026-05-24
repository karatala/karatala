#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"
#include "UITypes.h"
#include "SourcePickerPopup.h"

class PadContextPopup : public juce::Component {
public:
    explicit PadContextPopup(DrumKitEngine& engine, const KitLayout::InstrumentSlot& slot, int artIdx, const juce::String& savedInstrumentMapText = "");
    ~PadContextPopup() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void flushMapText();

    std::function<void()> onDismiss;
    /** Fires whenever this popup changes the instrument map text. */
    std::function<void(const juce::String&)> onMapTextChanged;
    /** Fires when MIDI mapping changes (note combos edited). */
    std::function<void()> onMidiMappingChanged;

private:
    DrumKitEngine& engine;
    juce::String slotId;
    juce::String artName;
    juce::String savedInstrumentMapText;
    KitLib::InstrumentSpec::Type slotType = KitLib::InstrumentSpec::Type::Unknown;

    // ─── Header ───
    juce::Label slotLabel;
    juce::Label artLabel;

    // ─── MIDI map ───
    juce::Label midiHeaderLabel;
    juce::ComboBox noteCombo1;
    juce::ComboBox noteCombo2;
    juce::TextButton learnButton1 { juce::String::charToString(0x25CF) };
    juce::TextButton learnButton2 { juce::String::charToString(0x25CF) };
    KitLayout::MidiNotes* midiNotesPtr = nullptr;
    int activeLearningSlot = -1;

    static void populateNoteComboBox(juce::ComboBox& combo);
    static juce::String midiNoteToString(int note);
    void syncComboFromNotes();
    void applyComboToNotes();

    std::vector<SourceItem> sourceItems;
    void rebuildSourceItems();
    int findSourceIndex(const juce::String& searchPath) const;

    // ─── Instrument map layers ───
    struct LayerRow {
        int selectedSourceIndex = -1;
        std::unique_ptr<juce::TextButton> sourceButton;
        std::unique_ptr<juce::Slider> tuningSlider;
        std::unique_ptr<juce::Slider> attackSlider;
        std::unique_ptr<juce::Slider> decaySlider;
        std::unique_ptr<juce::TextButton> addButton;
        std::unique_ptr<juce::TextButton> removeButton;
    };
    juce::OwnedArray<LayerRow> layerRows;
    juce::Label layerHeaderLabel;

    void addLayerRow(const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs);
    int getLayerIndex(const LayerRow* layer) const;
    void applyLayerParametersFast(LayerRow* layer);
    void updateRemoveButtonVisibility();
    void applyToEngine();
    juce::String generateFullMapText() const;
    void populateLayers();
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
