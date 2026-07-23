#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>
#include "../Engine/LayoutManager.h"

class DrumPad : public juce::Component, private juce::Timer {
public:
    explicit DrumPad() {}
    ~DrumPad() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    void setSlot(const KitLayout::InstrumentSlot* slot);
    void flash(float velocity);

    std::function<void(const KitLayout::InstrumentSlot*, int articulationIdx, const juce::String& previewArticulationCode, bool useDirectPreview, float velocity)> onPadClicked;
    std::function<void(const KitLayout::InstrumentSlot*, int articulationIdx, juce::Point<int> screenPos)> onPadRightClicked;

    float getClickVelocity() const { return this->clickVelocity; }
    void setClickVelocity(float vel) { this->clickVelocity = vel; }
    void setPadGap(int gap) { this->padGap = gap; repaint(); }

private:
    struct ArticulationCell {
        juce::Rectangle<int> bounds;
        juce::String label;
        juce::String previewArticulationCode;
        int articulationIdx = -1;
        bool useDirectPreview = false;
    };

    const KitLayout::InstrumentSlot* slotPtr = nullptr;
    std::vector<int> displayOrder;
    float clickVelocity = 100.0f;
    float flashAlpha = 0.0f;
    int padGap = 0;

    static juce::Colour getColourForType(KitLib::InstrumentSpec::Type type);
    void paintSimple(juce::Graphics& g);
    void paintZoned(juce::Graphics& g);
    void paintInstrumentTitle(juce::Graphics& g, const juce::Rectangle<int>& bounds) const;
    void refreshDisplayOrder();
    std::vector<ArticulationCell> buildArticulationCells(const juce::Rectangle<int>& bounds) const;
    std::vector<ArticulationCell> buildHiHatCells(const juce::Rectangle<int>& bounds) const;
    std::optional<ArticulationCell> getCellAt(juce::Point<int> position) const;
    void timerCallback() override;

    static constexpr int flashFadeDurationMs = 240;
    static constexpr int flashTimerHz = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPad)
};
