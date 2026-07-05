#include "DrumPad.h"
#include "DrumPadHelpers.h"
#include "UITheme.h"
#include <algorithm>
#include <numeric>

void DrumPad::setSlot(const KitLayout::InstrumentSlot* slot) {
    this->slotPtr = slot;
    this->refreshDisplayOrder();
    repaint();
}

void DrumPad::setPadMode(PadMode mode) {
    this->padMode = mode;
    repaint();
}

void DrumPad::refreshDisplayOrder() { //FIXME: get rid of display order, we will keep default order as it was specified in kit layout yaml
    this->displayOrder.clear();
    if (!this->slotPtr || this->slotPtr->articulations.empty())
        return;

    this->displayOrder = DrumPadHelpers::buildDisplayOrder(this->slotPtr->articulations);
}

void DrumPad::flash(float velocity) {
    if (this->slotPtr == nullptr) {
        this->flashAlpha = 0.0f;
        this->stopTimer();
        return;
    }

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

juce::Colour DrumPad::getColourForType(KitLib::InstrumentSpec::Type type) { return UITheme::Colours::instrumentType(type); }

void DrumPad::paint(juce::Graphics& g) {
    if (this->padMode == PadMode::Zone) this->paintZoned(g);
    else this->paintZen(g);
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
        DrumPadHelpers::drawPadLabel(g, labelBounds, "Empty", UITheme::Colours::textMuted, UITheme::Fonts::small().withHeight(11.0f), 1);
        return;
    }

    const float labelHeight = static_cast<float>(juce::jlimit(10, 15, juce::jmin(bounds.getWidth() / 5, bounds.getHeight() / 3)));
    DrumPadHelpers::drawPadLabel(g, labelBounds, DrumPadHelpers::getPadDisplayName(*this->slotPtr), UITheme::Colours::textPrimary.withAlpha(0.9f), UITheme::Fonts::labelBold().withHeight(labelHeight), 2);
}

void DrumPad::paintZoned(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    auto baseColour = this->slotPtr ? getColourForType(this->slotPtr->type) : UITheme::Colours::padEmpty;

    if (!this->slotPtr || this->slotPtr->articulations.empty() || this->displayOrder.empty()) {
        this->paintZen(g);
        return;
    }

    const auto& arts = this->slotPtr->articulations;
    auto artCount = juce::jmin(static_cast<int>(arts.size()), static_cast<int>(this->displayOrder.size()));
    if (artCount <= 0) {
        this->paintZen(g);
        return;
    }
    int mainDisplayIdx = DrumPadHelpers::findMainArticulationDisplayIdx(arts, this->displayOrder);

    const float fontSize = static_cast<float>(juce::jlimit(9, 13, juce::jmin(bounds.getWidth() / 6, bounds.getHeight() / (artCount * 2))));

    // Use integer arithmetic to avoid sub-pixel gaps between zones
    for (int displayIdx = 0; displayIdx < artCount; ++displayIdx) {
        int originalIdx = this->displayOrder[static_cast<size_t>(displayIdx)];
        int zoneTop = bounds.getY() + bounds.getHeight() * displayIdx / artCount;
        int zoneBottom = bounds.getY() + bounds.getHeight() * (displayIdx + 1) / artCount;
        auto zoneRect = juce::Rectangle<int>(bounds.getX(), zoneTop, bounds.getWidth(), zoneBottom - zoneTop);

        // Fill zone
        float brightVariation = 0.25f + 0.1f * static_cast<float>(displayIdx % 3);
        g.setColour(baseColour.withMultipliedBrightness(brightVariation));
        g.fillRect(zoneRect);

        // Label
        auto labelText = arts[static_cast<size_t>(originalIdx)].codeRaw;
        int maxLines = 1;
        if (displayIdx == mainDisplayIdx) {
            labelText = DrumPadHelpers::getPadDisplayName(*this->slotPtr) + "\n" + labelText;
            maxLines = 2;
        }
        DrumPadHelpers::drawPadLabel(g, zoneRect.reduced(4, 1), labelText, UITheme::Colours::textPrimary.withAlpha(0.85f), UITheme::Fonts::labelBold().withHeight(fontSize), maxLines);
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
    if (!this->slotPtr || this->slotPtr->articulations.empty() || this->displayOrder.empty())
        return 0;

    auto artCount = static_cast<int>(this->displayOrder.size());
    // Use same integer arithmetic as paintZoned
    for (int displayIdx = 0; displayIdx < artCount; ++displayIdx) {
        int zoneBottom = getHeight() * (displayIdx + 1) / artCount;
        if (yPos < zoneBottom)
            return this->displayOrder[static_cast<size_t>(displayIdx)];
    }
    return this->displayOrder.back();
}

void DrumPad::mouseDown(const juce::MouseEvent& event) {
    if (!this->slotPtr)
        return;
    int artIdx = 0;
    if (this->padMode == PadMode::Zone)
        artIdx = this->getZoneIndexAt(event.getPosition().getY());

    if (event.mods.isPopupMenu()) {
        if (this->onPadRightClicked) this->onPadRightClicked(this->slotPtr, artIdx, event.getScreenPosition());
        return;
    }
    if (this->onPadClicked) this->onPadClicked(this->slotPtr, artIdx, this->clickVelocity);
}

void DrumPad::resized() {
}
