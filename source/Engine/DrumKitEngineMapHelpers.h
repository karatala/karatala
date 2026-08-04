#pragma once

#include "ArticulationSlotFallbackRules.h"
#include "InstrumentMap/InstrumentMap.h"
#include "LayoutManager.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class DrumKitEngine;
class InstrumentPlayer;

namespace DrumKitEngineMapHelpers {

struct InstrumentMapLayerProjection {
    size_t mapLayerIndex = 0;
    juce::String slotId;
    juce::String articulationSlotCode;
    int articulationSlotLayerIndex = -1;
    KitLib::InstrumentSpec::Type slotType = KitLib::InstrumentSpec::Type::Unknown;
};

void appendUniqueId(std::vector<uint32_t>& values, uint32_t value);
std::vector<juce::String> effectiveArticulationSlotFilter(const InstrumentMap::Layer& mapLayer);
uint32_t resolveMapLayerLibraryArticulationId(const InstrumentMap::Layer& mapLayer, const juce::String& articulationSlotCode);
juce::StringArray collectLibraryArticulationNames(const KitLib::InstrumentSpec& libraryInstrument);
std::optional<bool> inferAutomaticFlipLR(const KitLayout::InstrumentSlot& slot, const KitLib::InstrumentSpec& libraryInstrument, const InstrumentMap::Layer& mapLayer);
void applyAutomaticOrientation(InstrumentMap::Map& instrumentMap, const KitLayout& layout, KitLib::LibraryManager& libraryManager);
juce::String makeInstrumentMapLayerFingerprint(const InstrumentMap::Layer& mapLayer);
juce::String makeInstrumentMapLayerReloadFingerprint(const InstrumentMap::Layer& mapLayer);
std::unordered_map<juce::String, juce::String> buildSlotFingerprintMap(const InstrumentMap::Map& instrumentMap);
std::unordered_map<juce::String, juce::String> buildResolvedSlotFingerprintMap(const InstrumentMap::Map& instrumentMap, const KitLib::LibraryManager& libraryManager);
std::unordered_map<juce::String, juce::String> buildResolvedSlotReloadFingerprintMap(const InstrumentMap::Map& instrumentMap, const KitLib::LibraryManager& libraryManager);
bool canReuseSlotWithoutReload(const KitLayout::InstrumentSlot& oldSlot, const KitLayout::InstrumentSlot& newSlot);
void preparePlayerIfNeeded(InstrumentPlayer& player, double sampleRate, int samplesPerBlock);
std::unique_ptr<InstrumentPlayer> createReplacementPlayer(DrumKitEngine* owner, const KitLayout::InstrumentSlot& layoutSlot, const InstrumentPlayer& player, double sampleRate, int samplesPerBlock);
KitLib::InstrumentSpec* findMatchingLibraryInstrument(const KitLayout::InstrumentSlot& layoutInstrument, const KitLib::DrumKitSpec& libraryKit, std::unordered_set<KitLib::InstrumentSpec*>& usedLibraryInstruments, std::map<KitLib::InstrumentSpec::Type, int>& nextIndexByType);
ParseResult<std::vector<InstrumentMap::Layer>> buildInstrumentMapLayers(const KitLayout::InstrumentSlot& slot, const KitLib::DrumKitSpec& libraryKit, const KitLib::InstrumentSpec& libraryInstrument, const juce::String& preferredLibraryKitId, const ArticulationSlotFallbackRules& fallbackRules);
void replaceSlotMapLayers(InstrumentMap::Map& instrumentMap, const juce::String& slotId, std::vector<InstrumentMap::Layer> replacements);
std::vector<InstrumentMapLayerProjection> projectMapLayers(const InstrumentMap::Map& instrumentMap, const KitLayout& layout);
std::vector<InstrumentMapLayerProjection> collectLinkedMapLayerTargets(const std::vector<InstrumentMapLayerProjection>& projections, const juce::String& selectedSlotId, const juce::String& selectedArticulationSlotCode, int selectedArticulationSlotLayerIndex, bool linkArticulations, bool linkAllTomBleed);
size_t isolateMapLayerForArticulationSlot(InstrumentMap::Map& instrumentMap, const KitLayout::InstrumentSlot& slot, size_t mapLayerIndex, const juce::String& articulationSlotCode);
juce::String libraryArticulationForSlot(const InstrumentMap::Layer& mapLayer, const juce::String& articulationSlotCode);
juce::String libraryPathForArticulationSlot(const InstrumentMap::Layer& mapLayer, const juce::String& articulationSlotCode);

} // namespace DrumKitEngineMapHelpers
