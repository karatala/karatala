#pragma once

#include <array>
#include <juce_core/juce_core.h>
#include <vector>
#include "../Config/Helpers.h"

class VelocityResponse {
public:
    struct Point {
        int velocity = 0;
        float targetDb = 0.0f;
    };

    static ParseResult<VelocityResponse> fromDefinition(const juce::String& definition);
    static ParseResult<VelocityResponse> fromPoints(const std::vector<Point>& points);
    static VelocityResponse linear();
    static VelocityResponse logarithmic();
    static VelocityResponse exponential();

    float getTargetDb(juce::uint8 velocity) const { return this->targetDbByVelocity[static_cast<size_t>(velocity)]; }
    float getGain(juce::uint8 velocity) const { return this->gainByVelocity[static_cast<size_t>(velocity)]; }
    juce::uint8 findClosestVelocityForTargetDb(float targetDb) const;
    const juce::String& getDefinition() const { return this->definition; }
    const std::vector<Point>& getPoints() const { return this->points; }

private:
    juce::String definition { "linear" };
    std::vector<Point> points;
    std::array<float, 128> targetDbByVelocity {};
    std::array<float, 128> gainByVelocity {};
};
