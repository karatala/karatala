#pragma once

#include "DrumKitEngine.h"

#include <cstddef>
#include <map>

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
std::vector<juce::String> effectiveArticulationSlotFilter(const DrumKitEngine::InstrumentMapLayer& mapLayer);
uint32_t resolveMapLayerLibraryArticulationId(const DrumKitEngine::InstrumentMapLayer& mapLayer, const juce::String& articulationSlotCode);
juce::StringArray collectLibraryArticulationNames(const KitLib::InstrumentSpec& libraryInstrument);
std::optional<bool> inferAutomaticFlipLR(const KitLayout::InstrumentSlot& slot, const KitLib::InstrumentSpec& libraryInstrument, const DrumKitEngine::InstrumentMapLayer& mapLayer);
void applyAutomaticOrientation(DrumKitEngine::InstrumentMap& instrumentMap, const KitLayout& layout, KitLib::LibraryManager& libraryManager);
juce::String makeInstrumentMapLayerFingerprint(const DrumKitEngine::InstrumentMapLayer& mapLayer);
juce::String makeInstrumentMapLayerReloadFingerprint(const DrumKitEngine::InstrumentMapLayer& mapLayer);
std::unordered_map<juce::String, juce::String> buildSlotFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap);
std::unordered_map<juce::String, juce::String> buildResolvedSlotFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap, const KitLib::LibraryManager& libraryManager);
std::unordered_map<juce::String, juce::String> buildResolvedSlotReloadFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap, const KitLib::LibraryManager& libraryManager);
bool canReuseSlotWithoutReload(const KitLayout::InstrumentSlot& oldSlot, const KitLayout::InstrumentSlot& newSlot);
void preparePlayerIfNeeded(InstrumentPlayer& player, double sampleRate, int samplesPerBlock);
std::unique_ptr<InstrumentPlayer> createReplacementPlayer(DrumKitEngine* owner, const KitLayout::InstrumentSlot& layoutSlot, const InstrumentPlayer& player, double sampleRate, int samplesPerBlock);
KitLib::InstrumentSpec* findMatchingLibraryInstrument(const KitLayout::InstrumentSlot& layoutInstrument, const KitLib::DrumKitSpec& libraryKit, std::unordered_set<KitLib::InstrumentSpec*>& usedLibraryInstruments, std::map<KitLib::InstrumentSpec::Type, int>& nextIndexByType);
ParseResult<std::vector<DrumKitEngine::InstrumentMapLayer>> buildInstrumentMapLayers(const KitLayout::InstrumentSlot& slot, const KitLib::DrumKitSpec& libraryKit, const KitLib::InstrumentSpec& libraryInstrument, const juce::String& preferredLibraryKitId, const ArticulationSlotFallbackRules& fallbackRules);
void replaceSlotMapLayers(DrumKitEngine::InstrumentMap& instrumentMap, const juce::String& slotId, std::vector<DrumKitEngine::InstrumentMapLayer> replacements);
std::vector<InstrumentMapLayerProjection> projectMapLayers(const DrumKitEngine::InstrumentMap& instrumentMap, const KitLayout& layout);
std::vector<InstrumentMapLayerProjection> collectLinkedMapLayerTargets(const std::vector<InstrumentMapLayerProjection>& projections, const juce::String& selectedSlotId, const juce::String& selectedArticulationSlotCode, int selectedArticulationSlotLayerIndex, bool linkArticulations, bool linkAllTomBleed);
size_t isolateMapLayerForArticulationSlot(DrumKitEngine::InstrumentMap& instrumentMap, const KitLayout::InstrumentSlot& slot, size_t mapLayerIndex, const juce::String& articulationSlotCode);
juce::String libraryArticulationForSlot(const DrumKitEngine::InstrumentMapLayer& mapLayer, const juce::String& articulationSlotCode);
juce::String libraryPathForArticulationSlot(const DrumKitEngine::InstrumentMapLayer& mapLayer, const juce::String& articulationSlotCode);

} // namespace DrumKitEngineMapHelpers
