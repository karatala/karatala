#include "DrumKitEngineTextView.h"
#include "DrumKitEngineMapHelpers.h"
#include "InstrumentPlayer.h"
#include <utility>

//FIXME: move stuff from here to new helper for engine parsers/serializers
namespace DrumKitEngineTextView {

juce::String buildSuggestedInstrumentMapText(const KitLayout* layout, const std::unordered_map<juce::String, InstrumentPlayer*>& instrumentPlayerById, const ArticulationSlotFallbackRules& fallbackRules, juce::StringArray* warnings) {
    if (layout == nullptr)
        return {};

    DrumKitEngine::InstrumentMap instrumentMap;
    for (const auto& slot : layout->instrumentSlots) {
        auto slotIt = instrumentPlayerById.find(slot.id);
        if (slotIt == instrumentPlayerById.end())
            continue;

        const auto* loadedInstrument = slotIt->second->getLoadedInstrumentSpec();
        if (loadedInstrument == nullptr || loadedInstrument->parent == nullptr)
            continue;

        auto mapLayers = DrumKitEngineMapHelpers::buildInstrumentMapLayers(slot, *loadedInstrument->parent, *loadedInstrument, loadedInstrument->parent->meta.codeRaw, fallbackRules);
        for (const auto& warning : mapLayers.errors) {
            LOG(Warning, warning);
            if (warnings != nullptr) warnings->add(warning);
        }
        for (auto& mapLayer : mapLayers.value) instrumentMap.layers.push_back(std::move(mapLayer));
    }

    return DrumKitEngine::serializeInstrumentMap(instrumentMap);
}

}
