#include <juce_audio_processors/juce_audio_processors.h>
#include "VelocityResponse.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

namespace VelocityResponseHelpers {

static std::optional<float> parseFloat(const juce::String& text) {
    const auto trimmed = text.trim();
    if (trimmed.isEmpty()) return std::nullopt;
    const auto utf8Text = trimmed.toRawUTF8();
    char* endPtr = nullptr;
    errno = 0;
    const float value = std::strtof(utf8Text, &endPtr);
    if (errno != 0 || endPtr == utf8Text || *endPtr != '\0' || !std::isfinite(value)) return std::nullopt;
    return value;
}

static juce::String serializePoints(const std::vector<VelocityResponse::Point>& points) {
    juce::StringArray tokens;
    for (const auto& point : points) {
        auto targetDb = juce::String(point.targetDb, 4).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
        if (targetDb == "-0") targetDb = "0";
        tokens.add(juce::String(point.velocity) + ":" + targetDb);
    }
    return tokens.joinIntoString(",");
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

ParseResult<VelocityResponse> VelocityResponse::fromDefinition(const juce::String& definition) {
    ParseResult<VelocityResponse> result;
    const auto trimmedDefinition = definition.trim();
    if (trimmedDefinition.isEmpty() || trimmedDefinition.compareIgnoreCase("linear") == 0) {
        result.value = VelocityResponse::linear();
        return result;
    }

    std::vector<VelocityResponse::Point> points;
    const auto pointTokens = juce::StringArray::fromTokens(trimmedDefinition, ",", "");
    for (const auto& pointToken : pointTokens) {
        const auto pointParts = juce::StringArray::fromTokens(pointToken, ":", "");
        if (pointParts.size() != 2 || !pointParts[0].trim().containsOnly("0123456789")) return result.returnWithError("Velocity response point must use velocity:db format: " + pointToken);
        VelocityResponse::Point point;
        point.velocity = pointParts[0].trim().getIntValue();
        const auto targetDb = VelocityResponseHelpers::parseFloat(pointParts[1]);
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
    size_t pointIdx = 0;
    for (int velocity = 1; velocity <= 127; ++velocity) {
        while (pointIdx + 1 < points.size() && velocity > points[pointIdx + 1].velocity) ++pointIdx;
        const auto& leftPoint = points[pointIdx];
        const auto& rightPoint = points[juce::jmin(pointIdx + 1, points.size() - 1)];
        const float span = static_cast<float>(juce::jmax(1, rightPoint.velocity - leftPoint.velocity));
        const float position = juce::jlimit(0.0f, 1.0f, static_cast<float>(velocity - leftPoint.velocity) / span);
        const float targetDb = leftPoint.targetDb + position * (rightPoint.targetDb - leftPoint.targetDb);
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
