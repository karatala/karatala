#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"

class PadContextPopup : public juce::Component {
public:
    explicit PadContextPopup(DrumKitEngine& engine, const KitLayout::InstrumentSlot& slot, int artIdx);
    ~PadContextPopup() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onDismiss;
    /** Fires whenever this popup changes the instrument map text. */
    std::function<void(const juce::String&)> onMapTextChanged;
    /** Fires when MIDI mapping changes (note combos edited). */
    std::function<void()> onMidiMappingChanged;

private:
    DrumKitEngine& engine;
    juce::String slotId;
    juce::String artName;
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

    // ─── SourceItem (same as InstrumentConfigPanel) ───
    struct SourceItem {
        juce::String kitCodeRaw;
        juce::String kitDisplayName;
        juce::String instCode;
        juce::String artCode;
        KitLib::InstrumentSpec::Type instType = KitLib::InstrumentSpec::Type::Unknown;

        juce::String fullPath() const {
            juce::String result = this->kitCodeRaw;
            if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
            if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
            return result;
        }
        juce::String displayText() const {
            juce::String result = this->kitDisplayName;
            if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
            if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
            return result;
        }
    };
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
    void updateRemoveButtonVisibility();
    void applyToEngine();
    juce::String generateFullMapText() const;
    void populateLayers();

    // ─── Source picker popup (inline, reuses same pattern) ───
    class SourcePickerPopup : public juce::Component {
    public:
        explicit SourcePickerPopup(const std::vector<SourceItem>& items);
        void resized() override;
        void paint(juce::Graphics& g) override;
        void setFilterType(KitLib::InstrumentSpec::Type type);
        std::function<void(int)> onItemSelected;
    private:
        const std::vector<SourceItem>& allItems;
        KitLib::InstrumentSpec::Type filterType = KitLib::InstrumentSpec::Type::Unknown;
        juce::TextEditor filterField;
        juce::ToggleButton relatedOnlyToggle { "related" };
        juce::ListBox listBox;
        struct FilteredEntry { int originalIndex; };
        std::vector<FilteredEntry> filteredEntries;
        void rebuildFiltered();
        class ListModel : public juce::ListBoxModel {
        public:
            explicit ListModel(SourcePickerPopup& owner) : owner(owner) {}
            int getNumRows() override;
            void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
            void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
        private:
            SourcePickerPopup& owner;
        };
        ListModel listModel { *this };
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourcePickerPopup)
    };

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
