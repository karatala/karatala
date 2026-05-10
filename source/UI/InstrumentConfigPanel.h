#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"

class InstrumentConfigPanel : public juce::Component {
public:
    explicit InstrumentConfigPanel(DrumKitEngine& engine);
    ~InstrumentConfigPanel() override = default;

    void resized() override;

    void populate();

    std::function<void(const juce::String&)> onStatusMessage;

private:
    DrumKitEngine& engine;

    // Dropdown item list: "kitRef/instCode/artCode"
    juce::StringArray dropdownItems;
    void rebuildDropdownItems();

    // --- Per-layer UI row ---
    struct LayerRow {
        std::unique_ptr<juce::ComboBox> sourceCombo;
        std::unique_ptr<juce::Slider> tuningSlider;
        std::unique_ptr<juce::Slider> attackSlider;
        std::unique_ptr<juce::Slider> decaySlider;
        std::unique_ptr<juce::TextButton> removeButton;
    };

    // --- Per-articulation card ---
    struct ArticulationCard : public juce::Component {
        juce::String artName;
        juce::OwnedArray<LayerRow> layers;
        juce::TextButton addLayerButton { "+ layer" };

        explicit ArticulationCard(const juce::String& artName);
        void resized() override;

        static constexpr int layerHeight = 28;
        static constexpr int addButtonHeight = 22;
        int getRequiredHeight() const;
    };

    // --- Per-slot card ---
    struct SlotCard : public juce::Component {
        juce::String slotId;
        juce::Label headerLabel;
        juce::OwnedArray<ArticulationCard> artCards;

        explicit SlotCard(const juce::String& slotId);
        void resized() override;

        static constexpr int headerHeight = 22;
        int getRequiredHeight() const;
    };

    juce::OwnedArray<SlotCard> slotCards;

    // Scrollable content
    juce::Component contentComponent;
    juce::Viewport viewport;

    void layoutContent();
    void applyToEngine();
    juce::String generateMapText() const;
    void addLayerRow(ArticulationCard* artCard, const juce::String& sourceText, int tuningCents, int attackMs, int decayMs);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentConfigPanel)
};
