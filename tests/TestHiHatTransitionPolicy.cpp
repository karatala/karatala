#include "../source/Engine/HiHatTransitionPolicy.h"
#include "catch2/catch_approx.hpp"
#include "catch2/catch_test_macros.hpp"
#include <array>

namespace {

KitLib::DecayEnvelope makeEnvelope() {
    KitLib::DecayEnvelope envelope;
    envelope.peakDb = -20.0f;
    envelope.peakMs = 10.0f;
    envelope.points[0] = 20.0f;
    envelope.points[1] = 30.0f;
    envelope.validPoints = 2;
    return envelope;
}

} // namespace

TEST_CASE("HiHatTransitionPolicy - hysteresis covers MIDI boundaries and both directions", "[HiHatTransitionPolicy]") {
    std::array<uint8_t, 128> articulationByCc {};
    articulationByCc.fill(0);
    for (size_t controllerIdx = 64; controllerIdx < articulationByCc.size(); ++controllerIdx)
        articulationByCc[controllerIdx] = 1;
    const std::array<int, 2> pocByArticulation { 0, 100 };

    REQUIRE(HiHatTransitionPolicy::selectOpennessArticulation(
                articulationByCc, pocByArticulation, 0, 0, -1, 2)
            == 0);
    REQUIRE(HiHatTransitionPolicy::selectOpennessArticulation(
                articulationByCc, pocByArticulation, 127, 126, 0, 2)
            == 1);

    REQUIRE(HiHatTransitionPolicy::selectOpennessArticulation(
                articulationByCc, pocByArticulation, 64, 60, 0, 2)
            == 0);
    REQUIRE(HiHatTransitionPolicy::selectOpennessArticulation(
                articulationByCc, pocByArticulation, 66, 64, 0, 2)
            == 1);

    REQUIRE(HiHatTransitionPolicy::selectOpennessArticulation(
                articulationByCc, pocByArticulation, 63, 68, 1, 2)
            == 1);
    REQUIRE(HiHatTransitionPolicy::selectOpennessArticulation(
                articulationByCc, pocByArticulation, 61, 63, 1, 2)
            == 0);
}

TEST_CASE("HiHatTransitionPolicy - direction distinguishes opening and closing", "[HiHatTransitionPolicy]") {
    REQUIRE(HiHatTransitionPolicy::getDirection(100, 40) == HiHatTransitionPolicy::Direction::Opening);
    REQUIRE(HiHatTransitionPolicy::getDirection(40, 100) == HiHatTransitionPolicy::Direction::Closing);
    REQUIRE(HiHatTransitionPolicy::getDirection(40, 40) == HiHatTransitionPolicy::Direction::Unchanged);
    REQUIRE(HiHatTransitionPolicy::getDirection(-1, 40) == HiHatTransitionPolicy::Direction::Unknown);

    const float openDurationSec = 0.060f;
    const float closeDurationSec = 0.020f;
    REQUIRE((HiHatTransitionPolicy::getDirection(100, 40) == HiHatTransitionPolicy::Direction::Closing
                ? closeDurationSec
                : openDurationSec)
            == openDurationSec);
    REQUIRE((HiHatTransitionPolicy::getDirection(40, 100) == HiHatTransitionPolicy::Direction::Closing
                ? closeDurationSec
                : openDurationSec)
            == closeDurationSec);
}

TEST_CASE("HiHatTransitionPolicy - eligible groups are bounded newest playing audible groups", "[HiHatTransitionPolicy]") {
    const std::array<HiHatTransitionPolicy::HitGroupCandidate, 6> candidates {
        HiHatTransitionPolicy::HitGroupCandidate { 1, 10, -20.0f, HiHatTransitionPolicy::HitGroupState::Playing },
        HiHatTransitionPolicy::HitGroupCandidate { 2, 30, -30.0f, HiHatTransitionPolicy::HitGroupState::Playing },
        HiHatTransitionPolicy::HitGroupCandidate { 3, 40, -10.0f, HiHatTransitionPolicy::HitGroupState::FadingOut },
        HiHatTransitionPolicy::HitGroupCandidate { 4, 50, -80.0f, HiHatTransitionPolicy::HitGroupState::Playing },
        HiHatTransitionPolicy::HitGroupCandidate { 5, 20, -15.0f, HiHatTransitionPolicy::HitGroupState::Playing },
        HiHatTransitionPolicy::HitGroupCandidate { 6, 25, -12.0f, HiHatTransitionPolicy::HitGroupState::Dying },
    };
    std::array<size_t, 4> selectedIndices {};

    const size_t selectedCount = HiHatTransitionPolicy::selectEligibleHitGroups(
        candidates, 2, -72.0f, selectedIndices);

    REQUIRE(selectedCount == 2);
    REQUIRE(candidates[selectedIndices[0]].groupId == 2);
    REQUIRE(candidates[selectedIndices[1]].groupId == 5);
}

TEST_CASE("HiHatTransitionPolicy - reference mic prefers direct then overhead then room", "[HiHatTransitionPolicy]") {
    const std::array<HiHatTransitionPolicy::ReferenceMicCandidate, 5> candidates {
        HiHatTransitionPolicy::ReferenceMicCandidate { KitLib::MicSpec::Type::Room, -20.0f, true },
        HiHatTransitionPolicy::ReferenceMicCandidate { KitLib::MicSpec::Type::Overhead, -30.0f, true },
        HiHatTransitionPolicy::ReferenceMicCandidate { KitLib::MicSpec::Type::Direct, -40.0f, true },
        HiHatTransitionPolicy::ReferenceMicCandidate { KitLib::MicSpec::Type::Direct, -10.0f, false },
        HiHatTransitionPolicy::ReferenceMicCandidate { KitLib::MicSpec::Type::Direct, -25.0f, true },
    };

    REQUIRE(HiHatTransitionPolicy::selectReferenceMic(candidates) == 4);
    REQUIRE(HiHatTransitionPolicy::selectReferenceMic({}) == -1);
}

TEST_CASE("HiHatTransitionPolicy - decay matching blends fallback and matched offsets", "[HiHatTransitionPolicy]") {
    const auto sourceEnvelope = makeEnvelope();
    const auto targetEnvelope = makeEnvelope();
    HiHatTransitionPolicy::OffsetInput input;
    input.sourceEnvelope = &sourceEnvelope;
    input.targetEnvelope = &targetEnvelope;
    input.elapsedMs = 15.0f;
    input.currentAudibleDb = -29.0f;
    input.replacementGainDb = 0.0f;
    input.sampleLengthMs = 100.0f;
    input.matchAmount = 0.5f;
    input.matchBiasDb = 0.0f;
    input.minimumRemainingMs = 20.0f;

    const auto result = HiHatTransitionPolicy::calculateDecayMatchedOffset(input);
    REQUIRE(result.succeeded());
    REQUIRE(result.targetRelativeDb == Catch::Approx(-29.0f));
    REQUIRE(result.offsetMs == Catch::Approx(20.0f));

    input.elapsedMs = 5.0f;
    input.attackGuardMs = 10.0f;
    REQUIRE(HiHatTransitionPolicy::calculateDecayMatchedOffset(input).status
            == HiHatTransitionPolicy::OffsetStatus::AttackGuard);

    input.elapsedMs = 15.0f;
    input.attackGuardMs = 0.0f;
    input.currentAudibleDb = -80.0f;
    REQUIRE(HiHatTransitionPolicy::calculateDecayMatchedOffset(input).status
            == HiHatTransitionPolicy::OffsetStatus::BelowSilenceFloor);
}

TEST_CASE("HiHatTransitionPolicy - missing decay policy keeps or simply fades", "[HiHatTransitionPolicy]") {
    HiHatTransitionPolicy::OffsetInput input;
    input.elapsedMs = 40.0f;
    input.currentAudibleDb = -30.0f;
    input.sampleLengthMs = 200.0f;
    input.minimumRemainingMs = 50.0f;
    input.missingDecayPolicy = HiHatMissingDecayPolicy::KeepTail;

    REQUIRE(HiHatTransitionPolicy::calculateDecayMatchedOffset(input).status
            == HiHatTransitionPolicy::OffsetStatus::MissingEnvelope);

    input.missingDecayPolicy = HiHatMissingDecayPolicy::SimpleFade;
    const auto simpleFadeResult = HiHatTransitionPolicy::calculateDecayMatchedOffset(input);
    REQUIRE(simpleFadeResult.succeeded());
    REQUIRE(simpleFadeResult.offsetMs == Catch::Approx(40.0f));

    input.elapsedMs = 180.0f;
    REQUIRE(HiHatTransitionPolicy::calculateDecayMatchedOffset(input).status
            == HiHatTransitionPolicy::OffsetStatus::TooLittleRemaining);
}

TEST_CASE("HiHatTransitionPolicy - transition curves have stable endpoints and midpoint", "[HiHatTransitionPolicy]") {
    const auto linearMidpoint = HiHatTransitionPolicy::calculateTransitionGains(0.5f, HiHatTransitionCurve::Linear);
    REQUIRE(linearMidpoint.sourceGain == Catch::Approx(0.5f));
    REQUIRE(linearMidpoint.targetGain == Catch::Approx(0.5f));

    const auto equalPowerMidpoint = HiHatTransitionPolicy::calculateTransitionGains(0.5f, HiHatTransitionCurve::EqualPower);
    REQUIRE(equalPowerMidpoint.sourceGain == Catch::Approx(0.7071067f).margin(0.0001f));
    REQUIRE(equalPowerMidpoint.targetGain == Catch::Approx(0.7071067f).margin(0.0001f));

    const auto smoothStart = HiHatTransitionPolicy::calculateTransitionGains(-1.0f, HiHatTransitionCurve::Smooth);
    const auto smoothEnd = HiHatTransitionPolicy::calculateTransitionGains(2.0f, HiHatTransitionCurve::Smooth);
    REQUIRE(smoothStart.sourceGain == Catch::Approx(1.0f));
    REQUIRE(smoothStart.targetGain == Catch::Approx(0.0f));
    REQUIRE(smoothEnd.sourceGain == Catch::Approx(0.0f));
    REQUIRE(smoothEnd.targetGain == Catch::Approx(1.0f));
}

TEST_CASE("HiHatTransitionPolicy - foot samples are independent policy decisions", "[HiHatTransitionPolicy]") {
    REQUIRE_FALSE(HiHatTransitionPolicy::shouldTriggerFootSample(
        HiHatFootSampleMode::Off, HiHatTransitionPolicy::Direction::Closing, true));
    REQUIRE(HiHatTransitionPolicy::shouldTriggerFootSample(
        HiHatFootSampleMode::CloseOnly, HiHatTransitionPolicy::Direction::Closing, true));
    REQUIRE_FALSE(HiHatTransitionPolicy::shouldTriggerFootSample(
        HiHatFootSampleMode::CloseOnly, HiHatTransitionPolicy::Direction::Opening, true));
    REQUIRE_FALSE(HiHatTransitionPolicy::shouldTriggerFootSample(
        HiHatFootSampleMode::CloseOnly, HiHatTransitionPolicy::Direction::Closing, false));
    REQUIRE(HiHatTransitionPolicy::shouldTriggerFootSample(
        HiHatFootSampleMode::Always, HiHatTransitionPolicy::Direction::Opening, false));
    REQUIRE_FALSE(HiHatTransitionPolicy::shouldTriggerFootSample(
        HiHatFootSampleMode::Always, HiHatTransitionPolicy::Direction::Closing, true, false));
}
