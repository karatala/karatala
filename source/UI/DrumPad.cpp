#include "DrumPad.h"
#include "UITheme.h"
#include <algorithm>
#include <numeric>

namespace {

juce::String getPadDisplayName(const KitLayout::InstrumentSlot& slot) { return slot.display.isNotEmpty() ? slot.display : slot.id; }

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

// Returns a sort key for articulation display order (lower = top).
// Drum-like: center(0) > rim(1) > sidestick(2)
// Cymbal-like: bell(0) > bow(1) > edge(2)
// HiHat: bow[tight](0) > bow[open](1) > edge[tight](2) > edge[open](3) > pedal(4) > pedal[*](5)
int articulationSortKey(const KitLayout::InstrumentSlot::ArticulationTrigger& art) {
    auto name = art.codeRaw.toLowerCase();
    auto baseName = name.upToFirstOccurrenceOf("[", false, false).trim();
    auto expr = name.fromFirstOccurrenceOf("[", false, false).upToFirstOccurrenceOf("]", false, false).trim();

    if (baseName == "bell")       return 0;
    if (baseName == "center")     return 0;
    if (baseName == "bow") {
        if (expr == "tight")      return 10;
        if (expr == "open")       return 11;
        if (expr.isEmpty())       return 12;
        return 13;
    }
    if (baseName == "edge") {
        if (expr == "tight")      return 20;
        if (expr == "open")       return 21;
        if (expr.isEmpty())       return 22;
        return 23;
    }
    if (baseName == "rim")        return 30;
    if (baseName == "sidestick")  return 40;
    if (baseName == "pedal") {
        if (expr.isEmpty())       return 50;
        if (expr == "splash")     return 51;
        return 52;
    }
    return 100;
}

// Build a display-order permutation: result[displayIdx] = originalIdx
std::vector<int> buildDisplayOrder(const std::vector<KitLayout::InstrumentSlot::ArticulationTrigger>& articulations) {
    std::vector<int> order(articulations.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
        return articulationSortKey(articulations[static_cast<size_t>(lhs)])
             < articulationSortKey(articulations[static_cast<size_t>(rhs)]);
    });
    return order;
}

// Determine the "main" articulation index (where the instrument name is shown).
// Priority: bow > edge > bell  or  center > rim > other
int findMainArticulationDisplayIdx(const std::vector<KitLayout::InstrumentSlot::ArticulationTrigger>& articulations,
                                   const std::vector<int>& displayOrder) {
    // Priority list — first match wins
    static const juce::StringArray priority = { "bow", "center", "edge", "bell" };
    for (const auto& target : priority) {
        for (int displayIdx = 0; displayIdx < static_cast<int>(displayOrder.size()); ++displayIdx) {
            auto baseName = articulations[static_cast<size_t>(displayOrder[static_cast<size_t>(displayIdx)])].codeRaw
                                .toLowerCase().upToFirstOccurrenceOf("[", false, false).trim();
            if (baseName == target)
                return displayIdx;
        }
    }
    return 0;
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

    const auto& arts = this->slotPtr->articulations;
    auto artCount = static_cast<int>(arts.size());
    auto displayOrder = buildDisplayOrder(arts);
    int mainDisplayIdx = findMainArticulationDisplayIdx(arts, displayOrder);

    const float fontSize = static_cast<float>(juce::jlimit(9, 13,
        juce::jmin(bounds.getWidth() / 6, bounds.getHeight() / (artCount * 2))));

    // Use integer arithmetic to avoid sub-pixel gaps between zones
    for (int displayIdx = 0; displayIdx < artCount; ++displayIdx) {
        int originalIdx = displayOrder[static_cast<size_t>(displayIdx)];
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
            labelText = getPadDisplayName(*this->slotPtr) + "\n" + labelText;
            maxLines = 2;
        }
        drawPadLabel(g,
                     zoneRect.reduced(4, 1),
                     labelText,
                     UITheme::Colours::textPrimary.withAlpha(0.85f),
                     UITheme::Fonts::labelBold().withHeight(fontSize),
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
    auto displayOrder = buildDisplayOrder(this->slotPtr->articulations);
    auto artCount = static_cast<int>(displayOrder.size());
    // Use same integer arithmetic as paintZoned
    for (int displayIdx = 0; displayIdx < artCount; ++displayIdx) {
        int zoneBottom = getHeight() * (displayIdx + 1) / artCount;
        if (yPos < zoneBottom)
            return displayOrder[static_cast<size_t>(displayIdx)];
    }
    return displayOrder.back();
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
