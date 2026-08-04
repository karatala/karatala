#pragma once

#include "../ArticulationSlotFallbackRules.h"
#include "InstrumentMap.h"
#include "../LayoutManager.h"

#include <cstddef>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace InstrumentMap {

struct LayerProjection {
    size_t mapLayerIndex = 0;
    juce::String slotId;
    juce::String articulationSlotCode;
    int articulationSlotLayerIndex = -1;
    KitLib::InstrumentSpec::Type slotType = KitLib::InstrumentSpec::Type::Unknown;
};

void applyAutomaticOrientation(Map& instrumentMap, const KitLayout& layout, KitLib::LibraryManager& libraryManager);
juce::String makeLayerFingerprint(const Layer& mapLayer);
juce::String makeLayerReloadFingerprint(const Layer& mapLayer);
std::unordered_map<juce::String, juce::String> buildResolvedSlotFingerprintMap(const Map& instrumentMap, const KitLib::LibraryManager& libraryManager);
std::unordered_map<juce::String, juce::String> buildResolvedSlotReloadFingerprintMap(const Map& instrumentMap, const KitLib::LibraryManager& libraryManager);
KitLib::InstrumentSpec* findMatchingLibraryInstrument(const KitLayout::InstrumentSlot& layoutInstrument, const KitLib::DrumKitSpec& libraryKit, std::unordered_set<KitLib::InstrumentSpec*>& usedLibraryInstruments, std::map<KitLib::InstrumentSpec::Type, int>& nextIndexByType);
ParseResult<std::vector<Layer>> buildLayers(const KitLayout::InstrumentSlot& slot, const KitLib::DrumKitSpec& libraryKit, const KitLib::InstrumentSpec& libraryInstrument, const juce::String& preferredLibraryKitId, const ArticulationSlotFallbackRules& fallbackRules);
void replaceSlotLayers(Map& instrumentMap, const juce::String& slotId, std::vector<Layer> replacements);
std::vector<LayerProjection> projectLayers(const Map& instrumentMap, const KitLayout& layout);
std::vector<LayerProjection> collectLinkedMapLayerTargets(const std::vector<LayerProjection>& projections, const juce::String& selectedSlotId, const juce::String& selectedArticulationSlotCode, int selectedArticulationSlotLayerIndex, bool linkArticulations, bool linkAllTomBleed);
size_t isolateLayerForArticulationSlot(Map& instrumentMap, const KitLayout::InstrumentSlot& slot, size_t mapLayerIndex, const juce::String& articulationSlotCode);
juce::String libraryArticulationForSlot(const Layer& mapLayer, const juce::String& articulationSlotCode);
juce::String libraryPathForArticulationSlot(const Layer& mapLayer, const juce::String& articulationSlotCode);

} // namespace InstrumentMap
