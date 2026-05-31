#include "DrumPad.h"
#include "UITheme.h"

namespace {

juce::String getPadDisplayName(const KitLayout::InstrumentSlot& slot) { return slot.display.isNotEmpty() ? slot.display : slot.id; }
juce::String getArticulationDisplayName(const KitLayout::InstrumentSlot::ArticulationTrigger& articulation) { return articulation.codeRaw; }

void drawPadLabel(juce::Graphics& g,
                  const juce::Rectangle<int>& bounds,
                  const juce::String& text,
                  const juce::Colour& colour,
                  juce::Font font,
                  int maxLines) {
    g.setColour(colour);
    g.setFont(std::move(font));
    g.drawFittedText(text, bounds, juce::Justification::centred, maxLines, 0.9f);
}

} // namespace

DrumPad::DrumPad() {
}

void DrumPad::setSlot(const KitLayout::InstrumentSlot* slot) {
    this->slotPtr = slot;
    repaint();
}

void DrumPad::setPadMode(PadMode mode) {
    this->padMode = mode;
    repaint();
}

void DrumPad::flash(float velocity) {
    this->flashAlpha = juce::jlimit(0.0f, 1.0f, velocity / 127.0f);
    this->startTimerHz(flashTimerHz);
    repaint();
}

void DrumPad::timerCallback() {
    const float flashStep = 1.0f / static_cast<float>((flashFadeDurationMs * flashTimerHz) / 1000);
    this->flashAlpha = juce::jmax(0.0f, this->flashAlpha - flashStep);

    if (this->flashAlpha <= 0.0f) {
        this->flashAlpha = 0.0f;
        this->stopTimer();
    }

    repaint();
}

juce::Colour DrumPad::getColourForType(KitLib::InstrumentSpec::Type type) {
    return UITheme::Colours::instrumentType(type);
}

void DrumPad::paint(juce::Graphics& g) {
    if (this->padMode == PadMode::Zoned)
        this->paintZoned(g);
    else
        this->paintZen(g);
}

void DrumPad::paintZen(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    auto baseColour = this->slotPtr ? getColourForType(this->slotPtr->type) : UITheme::Colours::padEmpty;

    // Fill with base colour, darkened
    g.setColour(baseColour.withMultipliedBrightness(0.3f));
    g.fillRect(bounds);

    // Flash overlay
    if (this->flashAlpha > 0.0f) {
        g.setColour(baseColour.withAlpha(this->flashAlpha * 0.6f));
        g.fillRect(bounds);
    }

    // Border — only when there's a visible gap between pads
    if (this->padGap > 0) {
        g.setColour(UITheme::Colours::separator);
        g.drawRect(bounds, 1);
    }

    const auto labelBounds = bounds.reduced(6);
    if (this->slotPtr == nullptr) {
        drawPadLabel(g,
                     labelBounds,
                     "Empty",
                     UITheme::Colours::textMuted,
                     UITheme::Fonts::small().withHeight(11.0f),
                     1);
        return;
    }

    const float labelHeight = static_cast<float>(juce::jlimit(10, 15,
        juce::jmin(bounds.getWidth() / 5, bounds.getHeight() / 3)));
    drawPadLabel(g,
                 labelBounds,
                 getPadDisplayName(*this->slotPtr),
                 UITheme::Colours::textPrimary.withAlpha(0.9f),
                 UITheme::Fonts::labelBold().withHeight(labelHeight),
                 2);
}

void DrumPad::paintZoned(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    auto baseColour = this->slotPtr ? getColourForType(this->slotPtr->type) : UITheme::Colours::padEmpty;

    if (!this->slotPtr || this->slotPtr->articulations.empty()) {
        this->paintZen(g);
        return;
    }

    auto artCount = static_cast<int>(this->slotPtr->articulations.size());

    // Use integer arithmetic to avoid sub-pixel gaps between zones
    for (int idx = 0; idx < artCount; ++idx) {
        int zoneTop = bounds.getY() + bounds.getHeight() * idx / artCount;
        int zoneBottom = bounds.getY() + bounds.getHeight() * (idx + 1) / artCount;
        auto zoneRect = juce::Rectangle<int>(bounds.getX(), zoneTop, bounds.getWidth(), zoneBottom - zoneTop);

        // Fill zone
        float brightVariation = 0.25f + 0.1f * static_cast<float>(idx % 3);
        g.setColour(baseColour.withMultipliedBrightness(brightVariation));
        g.fillRect(zoneRect);

        // Articulation label
        auto labelText = getArticulationDisplayName(this->slotPtr->articulations[static_cast<size_t>(idx)]);
        auto font = UITheme::Fonts::label().withHeight(static_cast<float>(juce::jlimit(8, 12, zoneRect.getHeight() / 3)));
        int maxLines = 1;
        if (idx == 0 && artCount == 1) {
            // Single articulation — show instrument name + articulation
            labelText = getPadDisplayName(*this->slotPtr) + "\n" + labelText;
            font = UITheme::Fonts::labelBold().withHeight(static_cast<float>(juce::jlimit(9, 12, zoneRect.getHeight() / 3)));
            maxLines = 2;
        }
        drawPadLabel(g,
                     zoneRect.reduced(4, 1),
                     labelText,
                     UITheme::Colours::textPrimary.withAlpha(0.85f),
                     font,
                     maxLines);
    }

    // Flash overlay
    if (this->flashAlpha > 0.0f) {
        g.setColour(baseColour.withAlpha(this->flashAlpha * 0.5f));
        g.fillRect(bounds);
    }

    // Outer border — only when there's a visible gap between pads
    if (this->padGap > 0) {
        g.setColour(UITheme::Colours::separator);
        g.drawRect(bounds, 1);
    }
}

int DrumPad::getZoneIndexAt(int yPos) const {
    if (!this->slotPtr || this->slotPtr->articulations.empty())
        return 0;
    auto artCount = static_cast<int>(this->slotPtr->articulations.size());
    // Use same integer arithmetic as paintZoned
    for (int idx = 0; idx < artCount; ++idx) {
        int zoneBottom = getHeight() * (idx + 1) / artCount;
        if (yPos < zoneBottom)
            return idx;
    }
    return artCount - 1;
}

void DrumPad::mouseDown(const juce::MouseEvent& event) {
    if (!this->slotPtr)
        return;

    int artIdx = 0;
    if (this->padMode == PadMode::Zoned)
        artIdx = this->getZoneIndexAt(event.getPosition().getY());

    if (event.mods.isPopupMenu()) {
        if (this->onPadRightClicked)
            this->onPadRightClicked(this->slotPtr, artIdx, event.getScreenPosition());
        return;
    }

    if (this->onPadClicked)
        this->onPadClicked(this->slotPtr, artIdx, this->clickVelocity);
}

void DrumPad::resized() {
}
