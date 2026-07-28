#pragma once

#include "DrumKitEngine.h"

#include <cstddef>
#include <map>

class InstrumentSynth;

namespace DrumKitEngineMapHelpers {

struct InstrumentMapLayerProjection {
    size_t entryIndex = 0;
    juce::String slotId;
    juce::String articulationName;
    int layerIndex = -1;
    KitLib::InstrumentSpec::Type slotType = KitLib::InstrumentSpec::Type::Unknown;
};

void appendUniqueId(std::vector<uint32_t>& values, uint32_t value);
std::vector<juce::String> effectiveArticulationFilter(const DrumKitEngine::InstrumentMapEntry& entry);
uint32_t resolveEntrySourceArtId(const DrumKitEngine::InstrumentMapEntry& entry, const juce::String& slotArtName);
juce::StringArray collectInstrumentArticulationNames(const KitLib::InstrumentSpec& instrumentSpec);
std::optional<bool> inferAutomaticFlipLR(const KitLayout::InstrumentSlot& slot, const KitLib::InstrumentSpec& instrumentSpec, const DrumKitEngine::InstrumentMapEntry& entry);
void applyAutomaticOrientation(DrumKitEngine::InstrumentMap& instrumentMap, const KitLayout& layout, KitLib::LibraryManager& libraryManager);
juce::String makeInstrumentMapEntryFingerprint(const DrumKitEngine::InstrumentMapEntry& entry);
juce::String makeInstrumentMapEntryReloadFingerprint(const DrumKitEngine::InstrumentMapEntry& entry);
std::unordered_map<juce::String, juce::String> buildSlotFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap);
std::unordered_map<juce::String, juce::String> buildResolvedSlotFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap, const KitLib::LibraryManager& libraryManager);
std::unordered_map<juce::String, juce::String> buildResolvedSlotReloadFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap, const KitLib::LibraryManager& libraryManager);
bool canReuseSlotWithoutReload(const KitLayout::InstrumentSlot& oldSlot, const KitLayout::InstrumentSlot& newSlot);
void prepareSynthIfNeeded(InstrumentSynth& synth, double sampleRate, int samplesPerBlock);
std::unique_ptr<InstrumentSynth> createReplacementSynth(DrumKitEngine* owner, const KitLayout::InstrumentSlot& layoutSlot, const InstrumentSynth& source, double sampleRate, int samplesPerBlock);
KitLib::InstrumentSpec* findMatchingKitInstrument(const KitLayout::InstrumentSlot& layoutInstrument, const KitLib::DrumKitSpec& kit, std::unordered_set<KitLib::InstrumentSpec*>& usedInstruments, std::map<KitLib::InstrumentSpec::Type, int>& nextIndexByType);
DrumKitEngine::InstrumentMapEntry buildInstrumentLevelEntry(const KitLayout::InstrumentSlot& slot, const KitLib::DrumKitSpec& kit, const KitLib::InstrumentSpec& instrument, const juce::String& preferredKitId);
void replaceSlotEntries(DrumKitEngine::InstrumentMap& instrumentMap, DrumKitEngine::InstrumentMapEntry replacement);
std::vector<InstrumentMapLayerProjection> projectDestinationLayers(const DrumKitEngine::InstrumentMap& instrumentMap, const KitLayout& layout);
std::vector<InstrumentMapLayerProjection> collectLinkedParameterTargets(const std::vector<InstrumentMapLayerProjection>& projections, const juce::String& sourceSlotId, const juce::String& sourceArticulation, int sourceLayerIndex, bool linkArticulations, bool linkAllTomBleed);
size_t isolateEntryForDestinationArticulation(DrumKitEngine::InstrumentMap& instrumentMap, const KitLayout::InstrumentSlot& slot, size_t entryIndex, const juce::String& articulationName);
juce::String sourceArticulationForDestination(const DrumKitEngine::InstrumentMapEntry& entry, const juce::String& destinationArticulation);

} // namespace DrumKitEngineMapHelpers
