#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"

class InstrumentConfigPanel : public juce::Component {
public:
    explicit InstrumentConfigPanel(DrumKitEngine& engine);
    ~InstrumentConfigPanel() override = default;

    void resized() override;
    void populate(const juce::String& instrumentMapText);

    std::function<void(const juce::String&)> onStatusMessage;
    std::function<void(const juce::String&)> onMapTextChanged;

private:
    DrumKitEngine& engine;
    bool populated = false;

    // ─── Structured source item for picker ───
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

    // ─── Filterable picker popup ───
    class SourcePickerPopup : public juce::Component {
    public:
        explicit SourcePickerPopup(const std::vector<SourceItem>& items);
        void resized() override;
        void paint(juce::Graphics& g) override;
        void setFilterType(KitLib::InstrumentSpec::Type type);
        std::function<void(int)> onItemSelected;
        std::function<void(int, juce::uint8)> onPreview;
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

    // ─── Flat table row (one per articulation/layer) ───
    struct TableRow {
        juce::String slotId;
        juce::String artName;
        KitLib::InstrumentSpec::Type slotType = KitLib::InstrumentSpec::Type::Unknown;
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
    class PopupOverlay : public juce::Component {
    public:
        std::function<void()> onClick;
        void mouseDown(const juce::MouseEvent&) override { if (this->onClick) this->onClick(); }
    };
    std::unique_ptr<PopupOverlay> popupOverlay;
    std::unique_ptr<SourcePickerPopup> activePopup;

    void showPickerPopup(TableRow* row);
    void dismissPopup();
    void layoutContent();
    void applyToEngine();
    juce::String generateMapText() const;
    void addRow(const juce::String& slotId, const juce::String& artName, KitLib::InstrumentSpec::Type slotType,
                const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs);
    int findSourceIndex(const juce::String& fullPath) const;
    void updateRemoveButtonVisibility();

    static constexpr int rowHeight = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentConfigPanel)
};
