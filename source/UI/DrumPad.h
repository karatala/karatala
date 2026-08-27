#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>
#include "../Engine/LayoutManager.h"

struct ArticulationRoundRobinCount {
    int loadedCount = 0;
    int totalCount = 0;

    bool operator==(const ArticulationRoundRobinCount&) const = default;
};

class DrumPad : public juce::Component, private juce::Timer {
public:
    explicit DrumPad() {}
    ~DrumPad() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    void setSlot(const KitLayout::InstrumentSlot* slot);
    void setLoadProgress(std::optional<float> progress);
    void setArticulationRoundRobinCounts(std::vector<ArticulationRoundRobinCount> counts);
    void flash(float velocity);

    std::function<void(const KitLayout::InstrumentSlot*, int articulationIdx, const juce::String& previewArticulationCode, bool useDirectPreview, float velocity)> onPadClicked;
    std::function<void(const KitLayout::InstrumentSlot*, int articulationIdx, juce::Point<int> screenPos)> onPadRightClicked;

    float getClickVelocity() const { return this->clickVelocity; }
    void setClickVelocity(float vel) { this->clickVelocity = vel; }
    void setPadGap(int gap) { this->padGap = gap; repaint(); }
    void setVisualScale(float scale) { this->visualScale = juce::jlimit(0.5f, 1.0f, scale); this->repaint(); }

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
    std::optional<float> loadProgress;
    std::vector<ArticulationRoundRobinCount> articulationRoundRobinCounts;
    float visualScale = 1.0f;
    int padGap = 0;

    static juce::Colour getColourForType(KitLib::InstrumentSpec::Type type);
    void paintSimple(juce::Graphics& g);
    void paintZoned(juce::Graphics& g);
    void paintInstrumentTitle(juce::Graphics& g, const juce::Rectangle<int>& bounds) const;
    void refreshDisplayOrder();
    std::vector<ArticulationCell> buildArticulationCells(const juce::Rectangle<int>& bounds) const;
    std::vector<ArticulationCell> buildHiHatCells(const juce::Rectangle<int>& bounds) const;
    std::optional<ArticulationCell> getCellAt(juce::Point<int> position) const;
    int getScaledSize(int size) const;
    float getScaledFontHeight(float height) const;
    void timerCallback() override;

    static constexpr int flashFadeDurationMs = 240;
    static constexpr int flashTimerHz = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPad)
};
