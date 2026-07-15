#include "../source/Engine/VelocityResponse.h"
#include "catch2/catch_approx.hpp"
#include "catch2/catch_test_macros.hpp"

TEST_CASE("VelocityResponse - linear keeps the original gain", "[VelocityResponse]") {
    const auto response = VelocityResponse::linear();

    REQUIRE(response.getGain(0) == Catch::Approx(0.0f));
    REQUIRE(response.getGain(1) == Catch::Approx(1.0f / 127.0f));
    REQUIRE(response.getGain(64) == Catch::Approx(64.0f / 127.0f));
    REQUIRE(response.getGain(127) == Catch::Approx(1.0f));
    REQUIRE(response.getTargetDb(127) == Catch::Approx(0.0f));
}

TEST_CASE("VelocityResponse - custom points interpolate in dB", "[VelocityResponse]") {
    const auto parsed = VelocityResponse::fromDefinition("1:-48,64:-12,127:0");

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.getTargetDb(1) == Catch::Approx(-48.0f));
    REQUIRE(parsed.value.getTargetDb(64) == Catch::Approx(-12.0f));
    REQUIRE(parsed.value.getTargetDb(127) == Catch::Approx(0.0f));
    REQUIRE(parsed.value.getGain(64) == Catch::Approx(juce::Decibels::decibelsToGain(-12.0f)));
    REQUIRE(parsed.value.findClosestVelocityForTargetDb(-12.0f) == 64);
}

TEST_CASE("VelocityResponse - invalid custom definitions are rejected", "[VelocityResponse]") {
    REQUIRE_FALSE(VelocityResponse::fromDefinition("1:-48,127:-1").ok());
    REQUIRE_FALSE(VelocityResponse::fromDefinition("1:-12,64:-24,127:0").ok());
    REQUIRE_FALSE(VelocityResponse::fromDefinition("10:-24,127:0").ok());
    REQUIRE_FALSE(VelocityResponse::fromDefinition("1:-24,127:1").ok());
}
