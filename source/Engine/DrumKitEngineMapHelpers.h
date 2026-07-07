#pragma once

#include "DrumKitEngine.h"

#include <map>

class InstrumentSynth;

namespace DrumKitEngineMapHelpers {

void appendUniqueId(std::vector<uint32_t>& values, uint32_t value);
std::vector<juce::String> effectiveArticulationFilter(const DrumKitEngine::InstrumentMapEntry& entry);
uint32_t resolveEntrySourceArtId(const DrumKitEngine::InstrumentMapEntry& entry, const juce::String& slotArtName);
juce::StringArray collectInstrumentArticulationNames(const KitLib::InstrumentSpec& instrumentSpec);
std::optional<bool> inferAutomaticFlipLR(const KitLayout::InstrumentSlot& slot, const KitLib::InstrumentSpec& instrumentSpec, const DrumKitEngine::InstrumentMapEntry& entry);
void applyAutomaticOrientation(DrumKitEngine::InstrumentMap& instrumentMap, const KitLayout& layout, KitLib::LibraryManager& libraryManager);
juce::String makeInstrumentMapEntryFingerprint(const DrumKitEngine::InstrumentMapEntry& entry);
std::unordered_map<juce::String, juce::String> buildSlotFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap);
std::unordered_map<juce::String, juce::String> buildResolvedSlotFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap, const KitLib::LibraryManager& libraryManager);
std::unordered_map<juce::String, juce::String> buildResolvedSlotReloadFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap, const KitLib::LibraryManager& libraryManager);
bool canReuseSlotWithoutReload(const KitLayout::InstrumentSlot& oldSlot, const KitLayout::InstrumentSlot& newSlot);
void prepareSynthIfNeeded(InstrumentSynth& synth, double sampleRate, int samplesPerBlock);
std::unique_ptr<InstrumentSynth> createReplacementSynth(DrumKitEngine* owner, const KitLayout::InstrumentSlot& layoutSlot, const InstrumentSynth& source, double sampleRate, int samplesPerBlock);
juce::String getSlotArticulationName(const KitLayout::InstrumentSlot::ArticulationTrigger& art);
KitLib::InstrumentSpec* findMatchingKitInstrument(const KitLayout::InstrumentSlot& layoutInstrument, const KitLib::DrumKitSpec& kit, std::unordered_set<KitLib::InstrumentSpec*>& usedInstruments, std::map<KitLib::InstrumentSpec::Type, int>& nextIndexByType);

} // namespace DrumKitEngineMapHelpers
