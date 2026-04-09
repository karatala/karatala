#pragma once

#include "DrumKitEngine.h"

class InstrumentSynth;

namespace DrumKitEngineTextView {
juce::String buildSuggestedInstrumentMapText(const KitLayout* layout, const std::unordered_map<juce::String, InstrumentSynth*>& instrumentSynthById);
juce::String buildReferenceText(const KitLayout* layout, const KitLib::LibraryManager* libraryManager);
}
