#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/LayoutManager.h"

enum class PadMode { Zen, Zoned };

class DrumPad : public juce::Component {
public:
    explicit DrumPad();
    ~DrumPad() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    void setSlot(const KitLayout::InstrumentSlot* slot);
    void setPadMode(PadMode mode);
    void flash(float velocity);

    std::function<void(const KitLayout::InstrumentSlot*, int articulationIdx, float velocity)> onPadClicked;
    std::function<void(const KitLayout::InstrumentSlot*, int articulationIdx, juce::Point<int> screenPos)> onPadRightClicked;

    float getClickVelocity() const { return this->clickVelocity; }
    void setClickVelocity(float vel) { this->clickVelocity = vel; }

private:
    const KitLayout::InstrumentSlot* slotPtr = nullptr;
    PadMode padMode = PadMode::Zen;
    float clickVelocity = 100.0f;
    float flashAlpha = 0.0f;

    static juce::Colour getColourForType(KitLib::InstrumentSpec::Type type);
    void paintZen(juce::Graphics& g);
    void paintZoned(juce::Graphics& g);
    int getZoneIndexAt(int yPos) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPad)
};
