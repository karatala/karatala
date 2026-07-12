#pragma once

#include "../Engine/ArticulationNameHelpers.h"
#include "../Engine/LayoutManager.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <numeric>

namespace DrumPadHelpers {

inline juce::String getPadDisplayName(const KitLayout::InstrumentSlot& slot) { return slot.display.isNotEmpty() ? slot.display : slot.id; }

inline juce::String getArticulationExpressionName(const juce::String& codeRaw) {
    if (!codeRaw.containsChar('[')) return {};
    return codeRaw.fromFirstOccurrenceOf("[", false, false).upToFirstOccurrenceOf("]", false, false).trim().toLowerCase();
}

inline void drawPadLabel(juce::Graphics& gfx, const juce::Rectangle<int>& bounds, const juce::String& text, const juce::Colour& colour, juce::Font font, int maxLines
) {
    gfx.setColour(colour);
    gfx.setFont(std::move(font));
    gfx.drawFittedText(text, bounds, juce::Justification::centred, maxLines, 0.9f);
}

inline std::vector<int> buildDisplayOrder(const std::vector<KitLayout::InstrumentSlot::ArticulationTrigger>& articulations) {
    std::vector<int> order(articulations.size());
    std::iota(order.begin(), order.end(), 0);
    return order;
}

} // namespace DrumPadHelpers
