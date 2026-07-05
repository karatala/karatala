#include "DrumKitEnginePreviewHelpers.h"

#include <limits>

namespace DrumKitEnginePreviewHelpers {
namespace Detail {

constexpr int previewVelocityBandSize = 8;

int getPreviewMicRank(const KitLib::SampleVariantSpec& variant) {
    if (!variant.isValid() || !variant.micSpec.isValid())
        return std::numeric_limits<int>::max();

    const auto& mic = variant.micSpec;
    const bool stereoCapable = mic.channels >= 2
        || mic.format == KitLib::MicSpec::Format::Stereo
        || mic.format == KitLib::MicSpec::Format::MidSide;

    if (mic.type == KitLib::MicSpec::Type::Overhead) {
        if (mic.raw.equalsIgnoreCase("ons"))
            return 0;

        int rank = stereoCapable ? 10 : 30;
        if (mic.position == KitLib::MicSpec::Position::Normal)
            rank -= 2;
        else if (mic.position == KitLib::MicSpec::Position::Wide)
            rank += 2;
        return rank;
    }

    if (mic.type == KitLib::MicSpec::Type::Direct) {
        int rank = stereoCapable ? 90 : 100;
        if (mic.position == KitLib::MicSpec::Position::Top || mic.position == KitLib::MicSpec::Position::In)
            rank -= 4;
        else if (mic.position == KitLib::MicSpec::Position::Out)
            rank -= 2;
        else if (mic.position == KitLib::MicSpec::Position::Bottom)
            rank += 2;
        return rank;
    }

    if (mic.type == KitLib::MicSpec::Type::Room) {
        int rank = stereoCapable ? 200 : 220;
        if (mic.position == KitLib::MicSpec::Position::Close)
            rank -= 2;
        else if (mic.position == KitLib::MicSpec::Position::Far)
            rank += 2;
        return rank;
    }

    return 300;
}

} // namespace Detail

int getPreviewVelocityBucket(juce::uint8 velocity) {
    return juce::jlimit(0, 15, (static_cast<int>(velocity) - 1) / Detail::previewVelocityBandSize);
}

juce::uint8 getPreviewVelocityForBucket(juce::uint8 velocity) {
    const int bucket = getPreviewVelocityBucket(velocity);
    const int bucketVelocity = bucket * Detail::previewVelocityBandSize + Detail::previewVelocityBandSize / 2;
    return static_cast<juce::uint8>(juce::jlimit(1, 127, bucketVelocity));
}

bool isBetterPreviewSampleVariant(const KitLib::SampleVariantSpec& candidate, const KitLib::SampleVariantSpec& current) {
    const int candidateRank = Detail::getPreviewMicRank(candidate);
    const int currentRank = Detail::getPreviewMicRank(current);
    if (candidateRank != currentRank)
        return candidateRank < currentRank;

    const int rawCompare = candidate.micSpec.raw.compareIgnoreCase(current.micSpec.raw);
    if (rawCompare != 0)
        return rawCompare < 0;

    if (candidate.fileChannelStart != current.fileChannelStart)
        return candidate.fileChannelStart < current.fileChannelStart;

    return candidate.sampleFileName.compareIgnoreCase(current.sampleFileName) < 0;
}

} // namespace DrumKitEnginePreviewHelpers
