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

TEST_CASE("HiHatTransitionPolicy - large closing jump can select tight directly", "[HiHatTransitionPolicy]") {
    std::array<uint8_t, 128> articulationByCc {};
    for (size_t controllerValue = 0; controllerValue < articulationByCc.size(); ++controllerValue) {
        if (controllerValue <= 24)
            articulationByCc[controllerValue] = 0;
        else if (controllerValue <= 52)
            articulationByCc[controllerValue] = 1;
        else if (controllerValue <= 78)
            articulationByCc[controllerValue] = 2;
        else
            articulationByCc[controllerValue] = 3;
    }
    const std::array<int, 4> pocByArticulation { 24, 52, 78, 85 };

    REQUIRE(HiHatTransitionPolicy::selectOpennessArticulation(articulationByCc, pocByArticulation, 100, 10, 0, 2) == 3);
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

TEST_CASE("HiHatTransitionPolicy - zero tail matching does not require decay metadata", "[HiHatTransitionPolicy]") {
    HiHatTransitionPolicy::OffsetInput input;
    input.elapsedMs = 40.0f;
    input.currentAudibleDb = -30.0f;
    input.sampleLengthMs = 200.0f;
    input.minimumRemainingMs = 50.0f;
    input.matchAmount = 0.0f;
    input.missingDecayPolicy = HiHatMissingDecayPolicy::KeepTail;

    const auto result = HiHatTransitionPolicy::calculateDecayMatchedOffset(input);
    REQUIRE(result.succeeded());
    REQUIRE(result.offsetMs == Catch::Approx(40.0f));
}
