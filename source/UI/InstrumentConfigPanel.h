#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"

class InstrumentConfigPanel : public juce::Component {
public:
    explicit InstrumentConfigPanel(DrumKitEngine& engine);
    ~InstrumentConfigPanel() override = default;

    void resized() override;

    // Populate from given instrument map text (reads current state)
    void populate(const juce::String& instrumentMapText);

    std::function<void(const juce::String&)> onStatusMessage;
    // Called with new map text when user changes anything — editor should persist this
    std::function<void(const juce::String&)> onMapTextChanged;

private:
    DrumKitEngine& engine;
    bool populated = false;

    // ─── Structured source item for picker ───
    struct SourceItem {
        juce::String kitCodeRaw;     // full kit code for map text
        juce::String kitDisplayName; // short name (no author/license)
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

    // ─── Per-layer UI row ───
    struct LayerRow {
        int selectedSourceIndex = -1;
        std::unique_ptr<juce::TextButton> sourceButton;
        std::unique_ptr<juce::Label> tuningLabel;
        std::unique_ptr<juce::Slider> tuningSlider;
        std::unique_ptr<juce::Label> attackLabel;
        std::unique_ptr<juce::Slider> attackSlider;
        std::unique_ptr<juce::Label> decayLabel;
        std::unique_ptr<juce::Slider> decaySlider;
        std::unique_ptr<juce::TextButton> removeButton;
    };

    // ─── Per-articulation card ───
    struct ArticulationCard : public juce::Component {
        juce::String artName;
        juce::Label artLabel;
        juce::OwnedArray<LayerRow> layers;
        juce::TextButton addLayerButton { "+" };

        explicit ArticulationCard(const juce::String& artName);
        void resized() override;
        void paint(juce::Graphics& g) override;
        void updateRemoveButtonVisibility();

        static constexpr int layerHeight = 24;
        static constexpr int headerHeight = 18;
        static constexpr int addButtonHeight = 20;
        int getRequiredHeight() const;
    };

    // ─── Per-slot card ───
    struct SlotCard : public juce::Component {
        juce::String slotId;
        KitLib::InstrumentSpec::Type slotType = KitLib::InstrumentSpec::Type::Unknown;
        juce::Label headerLabel;
        juce::OwnedArray<ArticulationCard> artCards;

        explicit SlotCard(const juce::String& slotId, KitLib::InstrumentSpec::Type type);
        void resized() override;
        void paint(juce::Graphics& g) override;

        static constexpr int headerHeight = 20;
        int getRequiredHeight() const;
    };

    juce::OwnedArray<SlotCard> slotCards;

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
    juce::Button* activePopupButton = nullptr;

    void showPickerPopup(LayerRow* layer, SlotCard* slotCard);
    void dismissPopup();

    void layoutContent();
    void applyToEngine();
    juce::String generateMapText() const;
    void addLayerRow(ArticulationCard* artCard, const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs);
    int findSourceIndex(const juce::String& fullPath) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentConfigPanel)
};
