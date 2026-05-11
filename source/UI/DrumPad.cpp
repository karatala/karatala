#include "DrumPad.h"

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
    this->flashAlpha = velocity / 127.0f;
    repaint();
    // TODO: animate fade-out with Timer
}

juce::Colour DrumPad::getColourForType(KitLib::InstrumentSpec::Type type) {
    switch (type) {
        case KitLib::InstrumentSpec::Type::Kick:    return juce::Colour(0xffcc3333);
        case KitLib::InstrumentSpec::Type::Snare:   return juce::Colour(0xffcc8833);
        case KitLib::InstrumentSpec::Type::HiHat:   return juce::Colour(0xffcccc33);
        case KitLib::InstrumentSpec::Type::Tom:     return juce::Colour(0xff3366cc);
        case KitLib::InstrumentSpec::Type::Ride:    return juce::Colour(0xff33cccc);
        case KitLib::InstrumentSpec::Type::Crash:   return juce::Colour(0xff9933cc);
        case KitLib::InstrumentSpec::Type::Splash:  return juce::Colour(0xff33cc99);
        case KitLib::InstrumentSpec::Type::China:   return juce::Colour(0xffcc3399);
        case KitLib::InstrumentSpec::Type::Cymbal:  return juce::Colour(0xff6699cc);
        case KitLib::InstrumentSpec::Type::Drum:    return juce::Colour(0xff6666cc);
        case KitLib::InstrumentSpec::Type::Percussion: return juce::Colour(0xff999966);
        default:                                    return juce::Colour(0xff555555);
    }
}

void DrumPad::paint(juce::Graphics& g) {
    if (this->padMode == PadMode::Zoned)
        this->paintZoned(g);
    else
        this->paintZen(g);
}

void DrumPad::paintZen(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    auto baseColour = this->slotPtr ? getColourForType(this->slotPtr->type) : juce::Colour(0xff333333);

    // Fill with base colour, darkened
    g.setColour(baseColour.withMultipliedBrightness(0.3f));
    g.fillRect(bounds);

    // Flash overlay
    if (this->flashAlpha > 0.0f) {
        g.setColour(baseColour.withAlpha(this->flashAlpha * 0.6f));
        g.fillRect(bounds);
    }

    // Border
    g.setColour(juce::Colour(0xff111111));
    g.drawRect(bounds, 1);

    // Instrument name
    if (this->slotPtr) {
        auto displayName = this->slotPtr->display.isNotEmpty() ? this->slotPtr->display : this->slotPtr->id;
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
        g.drawText(displayName, bounds.reduced(4), juce::Justification::centred);
    }
}

void DrumPad::paintZoned(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    auto baseColour = this->slotPtr ? getColourForType(this->slotPtr->type) : juce::Colour(0xff333333);

    if (!this->slotPtr || this->slotPtr->articulations.empty()) {
        this->paintZen(g);
        return;
    }

    auto artCount = static_cast<int>(this->slotPtr->articulations.size());
    float zoneHeight = static_cast<float>(bounds.getHeight()) / static_cast<float>(artCount);

    for (int idx = 0; idx < artCount; ++idx) {
        auto zoneRect = juce::Rectangle<float>(
            static_cast<float>(bounds.getX()),
            static_cast<float>(bounds.getY()) + zoneHeight * static_cast<float>(idx),
            static_cast<float>(bounds.getWidth()),
            zoneHeight
        ).toNearestInt();

        // Fill zone
        float brightVariation = 0.25f + 0.1f * static_cast<float>(idx % 3);
        g.setColour(baseColour.withMultipliedBrightness(brightVariation));
        g.fillRect(zoneRect);

        // Zone divider (thin line between zones)
        if (idx > 0) {
            g.setColour(juce::Colour(0xff111111));
            g.drawHorizontalLine(zoneRect.getY(), static_cast<float>(zoneRect.getX()), static_cast<float>(zoneRect.getRight()));
        }

        // Articulation label
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        auto labelText = this->slotPtr->articulations[static_cast<size_t>(idx)].codeRaw;
        if (idx == 0 && artCount == 1) {
            // Single articulation — show instrument name + articulation
            auto displayName = this->slotPtr->display.isNotEmpty() ? this->slotPtr->display : this->slotPtr->id;
            labelText = displayName + " [" + labelText + "]";
            g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        }
        g.drawText(labelText, zoneRect.reduced(4, 1), juce::Justification::centredLeft);
    }

    // Flash overlay
    if (this->flashAlpha > 0.0f) {
        g.setColour(baseColour.withAlpha(this->flashAlpha * 0.5f));
        g.fillRect(bounds);
    }

    // Outer border
    g.setColour(juce::Colour(0xff111111));
    g.drawRect(bounds, 1);
}

int DrumPad::getZoneIndexAt(int yPos) const {
    if (!this->slotPtr || this->slotPtr->articulations.empty())
        return 0;
    auto artCount = static_cast<int>(this->slotPtr->articulations.size());
    float zoneHeight = static_cast<float>(getHeight()) / static_cast<float>(artCount);
    int zoneIdx = static_cast<int>(static_cast<float>(yPos) / zoneHeight);
    return juce::jlimit(0, artCount - 1, zoneIdx);
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
