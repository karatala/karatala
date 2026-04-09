#pragma once

#include "DrumKitEngine.h"

namespace DrumKitEngineMappingModel {
ParseResult<DrumKitEngine::InstrumentMap> parseInstrumentMapText(const juce::String& text);
}
