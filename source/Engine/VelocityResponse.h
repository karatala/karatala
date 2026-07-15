#pragma once

#include <array>
#include <juce_core/juce_core.h>
#include "../Config/Helpers.h"

class VelocityResponse {
public:
    static ParseResult<VelocityResponse> fromDefinition(const juce::String& definition);
    static VelocityResponse linear();

    float getTargetDb(juce::uint8 velocity) const { return this->targetDbByVelocity[static_cast<size_t>(velocity)]; }
    float getGain(juce::uint8 velocity) const { return this->gainByVelocity[static_cast<size_t>(velocity)]; }
    juce::uint8 findClosestVelocityForTargetDb(float targetDb) const;
    const juce::String& getDefinition() const { return this->definition; }

private:
    juce::String definition { "linear" };
    std::array<float, 128> targetDbByVelocity {};
    std::array<float, 128> gainByVelocity {};
};
