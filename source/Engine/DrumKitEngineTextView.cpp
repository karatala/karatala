#include "DrumKitEngineTextView.h"
#include "InstrumentSynth.h"
#include <algorithm>

//FIXME: move stuff from here to new helper for engine parsers/serializers
namespace DrumKitEngineTextView {

juce::String buildSuggestedInstrumentMapText(const KitLayout* layout, const std::unordered_map<juce::String, InstrumentSynth*>& instrumentSynthById) {
    if (layout == nullptr)
        return {};

    juce::StringArray lines;
    for (const auto& slot : layout->instrumentSlots) {
        auto slotIt = instrumentSynthById.find(slot.id);
        if (slotIt == instrumentSynthById.end())
            continue;

        const auto* loadedInstrument = slotIt->second->getLoadedInstrumentSpec();
        if (loadedInstrument == nullptr || loadedInstrument->parent == nullptr)
            continue;

        lines.add(slot.id + ":" + loadedInstrument->parent->meta.codeRaw + "/" + loadedInstrument->meta.codeRaw);
    }

    return lines.joinIntoString("\n");
}

}
