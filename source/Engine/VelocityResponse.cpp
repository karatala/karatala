#include <juce_audio_processors/juce_audio_processors.h>
#include "VelocityResponse.h"
#include "../Config/Parsing.h"
#include <cmath>
#include <limits>
#include <vector>

namespace VelocityResponseHelpers {

static juce::String serializePoints(const std::vector<VelocityResponse::Point>& points) {
    juce::StringArray tokens;
    for (const auto& point : points) {
        auto targetDb = juce::String(point.targetDb, 4).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
        if (targetDb == "-0") targetDb = "0";
        tokens.add(juce::String(point.velocity) + ":" + targetDb);
    }
    return tokens.joinIntoString(",");
}

static float limitEndpointTangent(float tangent, float intervalSlope) {
    if (tangent <= 0.0f || intervalSlope <= 0.0f) return 0.0f;
    return juce::jmin(tangent, 3.0f * intervalSlope);
}

static std::vector<float> buildMonotoneTangents(const std::vector<VelocityResponse::Point>& points) {
    std::vector<float> intervalWidths(points.size() - 1);
    std::vector<float> intervalSlopes(points.size() - 1);
    for (size_t intervalIdx = 0; intervalIdx + 1 < points.size(); ++intervalIdx) {
        intervalWidths[intervalIdx] = static_cast<float>(points[intervalIdx + 1].velocity - points[intervalIdx].velocity);
        intervalSlopes[intervalIdx] = (points[intervalIdx + 1].targetDb - points[intervalIdx].targetDb) / intervalWidths[intervalIdx];
    }

    std::vector<float> tangents(points.size(), 0.0f);
    if (points.size() == 2) {
        tangents.front() = intervalSlopes.front();
        tangents.back() = intervalSlopes.front();
        return tangents;
    }

    for (size_t pointIdx = 1; pointIdx + 1 < points.size(); ++pointIdx) {
        const float previousSlope = intervalSlopes[pointIdx - 1];
        const float nextSlope = intervalSlopes[pointIdx];
        if (previousSlope <= 0.0f || nextSlope <= 0.0f) continue;
        const float previousWeight = 2.0f * intervalWidths[pointIdx] + intervalWidths[pointIdx - 1];
        const float nextWeight = intervalWidths[pointIdx] + 2.0f * intervalWidths[pointIdx - 1];
        tangents[pointIdx] = (previousWeight + nextWeight) / (previousWeight / previousSlope + nextWeight / nextSlope);
    }

    const float firstTangent = ((2.0f * intervalWidths[0] + intervalWidths[1]) * intervalSlopes[0] - intervalWidths[0] * intervalSlopes[1]) / (intervalWidths[0] + intervalWidths[1]);
    tangents.front() = VelocityResponseHelpers::limitEndpointTangent(firstTangent, intervalSlopes.front());
    const size_t lastIntervalIdx = intervalSlopes.size() - 1;
    const float lastIntervalWidth = intervalWidths[lastIntervalIdx];
    const float previousIntervalWidth = intervalWidths[lastIntervalIdx - 1];
    const float lastTangent = ((2.0f * lastIntervalWidth + previousIntervalWidth) * intervalSlopes[lastIntervalIdx] - lastIntervalWidth * intervalSlopes[lastIntervalIdx - 1]) / (lastIntervalWidth + previousIntervalWidth);
    tangents.back() = VelocityResponseHelpers::limitEndpointTangent(lastTangent, intervalSlopes.back());
    return tangents;
}

static float interpolateMonotoneDb(const VelocityResponse::Point& leftPoint, const VelocityResponse::Point& rightPoint, float leftTangent, float rightTangent, int velocity) {
    const float intervalWidth = static_cast<float>(rightPoint.velocity - leftPoint.velocity);
    if (intervalWidth <= 0.0f) return leftPoint.targetDb;
    const float position = juce::jlimit(0.0f, 1.0f, static_cast<float>(velocity - leftPoint.velocity) / intervalWidth);
    const float positionSquared = position * position;
    const float positionCubed = positionSquared * position;
    const float leftValueWeight = 2.0f * positionCubed - 3.0f * positionSquared + 1.0f;
    const float leftTangentWeight = positionCubed - 2.0f * positionSquared + position;
    const float rightValueWeight = -2.0f * positionCubed + 3.0f * positionSquared;
    const float rightTangentWeight = positionCubed - positionSquared;
    const float targetDb = leftValueWeight * leftPoint.targetDb + leftTangentWeight * intervalWidth * leftTangent + rightValueWeight * rightPoint.targetDb + rightTangentWeight * intervalWidth * rightTangent;
    return juce::jlimit(leftPoint.targetDb, rightPoint.targetDb, targetDb);
}

} // namespace VelocityResponseHelpers

VelocityResponse VelocityResponse::linear() {
    VelocityResponse response;
    response.definition = "linear";
    response.targetDbByVelocity[0] = -100.0f;
    response.gainByVelocity[0] = 0.0f;
    for (int velocity = 1; velocity <= 127; ++velocity) {
        const float gain = static_cast<float>(velocity) / 127.0f;
        response.targetDbByVelocity[static_cast<size_t>(velocity)] = 20.0f * std::log10(gain);
        response.gainByVelocity[static_cast<size_t>(velocity)] = gain;
    }
    return response;
}

VelocityResponse VelocityResponse::logarithmic() {
    VelocityResponse response;
    response.definition = "logarithmic";
    response.targetDbByVelocity[0] = -100.0f;
    response.gainByVelocity[0] = 0.0f;
    const float naturalBase = std::exp(1.0f);
    for (int velocity = 1; velocity <= 127; ++velocity) {
        const float normalizedVelocity = static_cast<float>(velocity) / 127.0f;
        const float gain = std::log1p((naturalBase - 1.0f) * normalizedVelocity);
        response.targetDbByVelocity[static_cast<size_t>(velocity)] = 20.0f * std::log10(gain);
        response.gainByVelocity[static_cast<size_t>(velocity)] = gain;
    }
    return response;
}

VelocityResponse VelocityResponse::exponential() {
    VelocityResponse response;
    response.definition = "exponential";
    response.targetDbByVelocity[0] = -100.0f;
    response.gainByVelocity[0] = 0.0f;
    const float naturalBase = std::exp(1.0f);
    for (int velocity = 1; velocity <= 127; ++velocity) {
        const float normalizedVelocity = static_cast<float>(velocity) / 127.0f;
        const float gain = (std::exp(normalizedVelocity) - 1.0f) / (naturalBase - 1.0f);
        response.targetDbByVelocity[static_cast<size_t>(velocity)] = 20.0f * std::log10(gain);
        response.gainByVelocity[static_cast<size_t>(velocity)] = gain;
    }
    return response;
}

ParseResult<VelocityResponse> VelocityResponse::fromDefinition(const juce::String& definition) {
    ParseResult<VelocityResponse> result;
    const auto trimmedDefinition = definition.trim();
    if (trimmedDefinition.isEmpty() || trimmedDefinition.compareIgnoreCase("linear") == 0) {
        result.value = VelocityResponse::linear();
        return result;
    }
    if (trimmedDefinition.compareIgnoreCase("logarithmic") == 0) {
        result.value = VelocityResponse::logarithmic();
        return result;
    }
    if (trimmedDefinition.compareIgnoreCase("exponential") == 0) {
        result.value = VelocityResponse::exponential();
        return result;
    }

    std::vector<VelocityResponse::Point> points;
    const auto pointTokens = juce::StringArray::fromTokens(trimmedDefinition, ",", "");
    for (const auto& pointToken : pointTokens) {
        const auto pointParts = juce::StringArray::fromTokens(pointToken, ":", "");
        if (pointParts.size() != 2 || !pointParts[0].trim().containsOnly("0123456789")) return result.returnWithError("Velocity response point must use velocity:db format: " + pointToken);
        VelocityResponse::Point point;
        point.velocity = pointParts[0].trim().getIntValue();
        const auto targetDb = Parsing::parseFiniteFloat(pointParts[1]);
        if (point.velocity < 1 || point.velocity > 127 || !targetDb.has_value()) return result.returnWithError("Invalid velocity response point: " + pointToken);
        point.targetDb = *targetDb;
        if (!points.empty() && point.velocity <= points.back().velocity) return result.returnWithError("Velocity response velocities must increase");
        if (!points.empty() && point.targetDb < points.back().targetDb) return result.returnWithError("Velocity response dB values must not decrease");
        if (point.targetDb > 0.0f) return result.returnWithError("Velocity response dB values must not exceed 0 dB");
        points.push_back(point);
    }

    if (points.size() < 2 || points.front().velocity != 1 || points.back().velocity != 127 || std::abs(points.back().targetDb) > 0.0001f) {
        return result.returnWithError("Velocity response needs points at velocity 1 and velocity 127, with velocity 127 fixed at 0 dB");
    }

    VelocityResponse response;
    response.definition = VelocityResponseHelpers::serializePoints(points);
    response.points = points;
    response.targetDbByVelocity[0] = -100.0f;
    response.gainByVelocity[0] = 0.0f;
    const auto tangents = VelocityResponseHelpers::buildMonotoneTangents(points);
    size_t pointIdx = 0;
    for (int velocity = 1; velocity <= 127; ++velocity) {
        while (pointIdx + 1 < points.size() && velocity > points[pointIdx + 1].velocity) ++pointIdx;
        const size_t rightPointIdx = juce::jmin(pointIdx + 1, points.size() - 1);
        const auto& leftPoint = points[pointIdx];
        const auto& rightPoint = points[rightPointIdx];
        const float targetDb = VelocityResponseHelpers::interpolateMonotoneDb(leftPoint, rightPoint, tangents[pointIdx], tangents[rightPointIdx], velocity);
        response.targetDbByVelocity[static_cast<size_t>(velocity)] = targetDb;
        response.gainByVelocity[static_cast<size_t>(velocity)] = juce::Decibels::decibelsToGain(targetDb, -100.0f);
    }
    result.value = std::move(response);
    return result;
}

ParseResult<VelocityResponse> VelocityResponse::fromPoints(const std::vector<Point>& points) {
    if (points.empty()) {
        ParseResult<VelocityResponse> result;
        return result.returnWithError("Velocity response needs at least two points");
    }
    return VelocityResponse::fromDefinition(VelocityResponseHelpers::serializePoints(points));
}

juce::uint8 VelocityResponse::findClosestVelocityForTargetDb(float targetDb) const {
    int bestVelocity = 1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int velocity = 1; velocity <= 127; ++velocity) {
        const float distance = std::abs(this->targetDbByVelocity[static_cast<size_t>(velocity)] - targetDb);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestVelocity = velocity;
        }
    }
    return static_cast<juce::uint8>(bestVelocity);
}
