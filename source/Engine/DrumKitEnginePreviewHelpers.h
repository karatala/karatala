#pragma once

#include "KitLibModel.h"

namespace DrumKitEnginePreviewHelpers {

int getPreviewVelocityBucket(juce::uint8 velocity);
juce::uint8 getPreviewVelocityForBucket(juce::uint8 velocity);
bool isBetterPreviewSampleVariant(const KitLib::SampleVariantSpec& candidate, const KitLib::SampleVariantSpec& current);

} // namespace DrumKitEnginePreviewHelpers
