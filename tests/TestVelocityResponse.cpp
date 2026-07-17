#include "../source/Engine/InstrumentSynth.h"
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

TEST_CASE("Calibrated runtime keeps named velocity layer boundaries", "[VelocityResponse]") {
    InstrumentSynth::ArticulationRuntime articulation(1, 0);
    const std::array<juce::uint8, 5> namedVelocities { 27, 52, 77, 102, 127 };
    const std::array<float, 5> fittedLevelsDb { -38.0f, -37.5f, -37.0f, -36.0f, -35.5f };
    for (size_t layerIdx = 0; layerIdx < namedVelocities.size(); ++layerIdx) {
        InstrumentSynth::VelocityLayerRuntime layer;
        layer.velocity = namedVelocities[layerIdx];
        layer.fittedSourceDb = fittedLevelsDb[layerIdx];
        layer.calibrationTrimDb = 0.0f;
        articulation.layers.push_back(std::move(layer));
    }

    articulation.buildVelocityMapping(VelocityResponse::linear());

    REQUIRE(articulation.calibrated);
    REQUIRE(articulation.velocityToLayer[100] == 3);
    REQUIRE(articulation.velocityToLayer[103] == 4);
    REQUIRE(articulation.selectionTargetDb[100] == Catch::Approx(fittedLevelsDb[3]));
    REQUIRE(articulation.velocityGain[100] == Catch::Approx(100.0f / 127.0f));
}

TEST_CASE("Layer velocity response changes gain without changing named velocity selection", "[VelocityResponse]") {
    InstrumentSynth::ArticulationRuntime articulation(1, 0);
    InstrumentSynth::VelocityLayerRuntime softLayer;
    softLayer.velocity = 64;
    softLayer.fittedSourceDb = -40.0f;
    softLayer.calibrationTrimDb = 0.0f;
    articulation.layers.push_back(std::move(softLayer));
    InstrumentSynth::VelocityLayerRuntime hardLayer;
    hardLayer.velocity = 127;
    hardLayer.fittedSourceDb = -30.0f;
    hardLayer.calibrationTrimDb = 0.0f;
    articulation.layers.push_back(std::move(hardLayer));

    auto customResponse = VelocityResponse::fromDefinition("1:-48,64:-12,127:0");
    REQUIRE(customResponse.ok());
    articulation.parameters.velocityResponse = customResponse.value;
    articulation.buildVelocityMapping(VelocityResponse::linear());

    REQUIRE(articulation.velocityToLayer[64] == 0);
    REQUIRE(articulation.velocityToLayer[65] == 1);
    REQUIRE(articulation.velocityGain[64] == Catch::Approx(juce::Decibels::decibelsToGain(-12.0f)));
}
