#pragma once

#include "../Engine/LayoutManager.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace DrumPadHelpers {

juce::String getPadDisplayName(const KitLayout::InstrumentSlot& slot);
void drawPadLabel(juce::Graphics& gfx, const juce::Rectangle<int>& bounds, const juce::String& text, const juce::Colour& colour, juce::Font font, int maxLines);
std::vector<int> buildDisplayOrder(const std::vector<KitLayout::InstrumentSlot::ArticulationTrigger>& articulations);
int findMainArticulationDisplayIdx(const std::vector<KitLayout::InstrumentSlot::ArticulationTrigger>& articulations,const std::vector<int>& displayOrder);

} // namespace DrumPadHelpers
