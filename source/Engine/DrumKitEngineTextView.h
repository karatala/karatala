#pragma once

#include "DrumKitEngine.h"

class InstrumentPlayer;

namespace DrumKitEngineTextView {
juce::String buildSuggestedInstrumentMapText(const KitLayout* layout, const std::unordered_map<juce::String, InstrumentPlayer*>& instrumentPlayerById);
}
