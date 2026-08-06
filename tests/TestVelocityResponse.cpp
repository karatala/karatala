#include "../source/Engine/DrumKitEngine.h"
#include "../source/Engine/InstrumentPlayer.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include "catch2/catch_approx.hpp"
#include "catch2/catch_test_macros.hpp"

static InstrumentPlayer::VelocityLevelRuntime makeFloatingVelocityLevel(juce::uint8 velocity, float fittedSourceDb, std::initializer_list<float> hitIntensitiesDb) {
    InstrumentPlayer::VelocityLevelRuntime velocityLevel;
    velocityLevel.velocity = velocity;
    velocityLevel.fittedSourceDb = fittedSourceDb;
    velocityLevel.calibrationTrimDb = 0.0f;
    for (const float hitIntensityDb : hitIntensitiesDb) {
        InstrumentPlayer::LogicalHitRuntime logicalHit;
        logicalHit.preTrimIntensityDb = hitIntensityDb;
        velocityLevel.logicalHits.push_back(std::move(logicalHit));
    }
    return velocityLevel;
}

TEST_CASE("VelocityResponse - linear keeps the original gain", "[VelocityResponse]") {
    const auto response = VelocityResponse::linear();

    REQUIRE(response.getGain(0) == Catch::Approx(0.0f));
    REQUIRE(response.getGain(1) == Catch::Approx(1.0f / 127.0f));
    REQUIRE(response.getGain(64) == Catch::Approx(64.0f / 127.0f));
    REQUIRE(response.getGain(127) == Catch::Approx(1.0f));
    REQUIRE(response.getTargetDb(127) == Catch::Approx(0.0f));
}

TEST_CASE("VelocityResponse - built-in response codes provide stable normalized curves", "[VelocityResponse]") {
    const auto logarithmic = VelocityResponse::fromDefinition("LOGARITHMIC");
    const auto exponential = VelocityResponse::fromDefinition("exponential");
    const float normalizedVelocity = 64.0f / 127.0f;
    const float naturalBase = std::exp(1.0f);

    REQUIRE(logarithmic.ok());
    REQUIRE(exponential.ok());
    REQUIRE(logarithmic.value.getDefinition() == "logarithmic");
    REQUIRE(exponential.value.getDefinition() == "exponential");
    REQUIRE(logarithmic.value.getGain(0) == Catch::Approx(0.0f));
    REQUIRE(exponential.value.getGain(0) == Catch::Approx(0.0f));
    REQUIRE(logarithmic.value.getGain(64) == Catch::Approx(std::log1p((naturalBase - 1.0f) * normalizedVelocity)));
    REQUIRE(exponential.value.getGain(64) == Catch::Approx((std::exp(normalizedVelocity) - 1.0f) / (naturalBase - 1.0f)));
    REQUIRE(logarithmic.value.getGain(64) > normalizedVelocity);
    REQUIRE(exponential.value.getGain(64) < normalizedVelocity);
    REQUIRE(logarithmic.value.getGain(127) == Catch::Approx(1.0f));
    REQUIRE(exponential.value.getGain(127) == Catch::Approx(1.0f));
}

TEST_CASE("DrumKitEngine - instance global velocity response accepts valid definitions", "[VelocityResponse]") {
    DrumKitEngine engine;

    REQUIRE(engine.setVelocityResponseDefinition("LOGARITHMIC"));
    REQUIRE(engine.getVelocityResponse().getDefinition() == "logarithmic");
    const float logarithmicGain = engine.getVelocityResponse().getGain(64);

    REQUIRE_FALSE(engine.setVelocityResponseDefinition("invalid"));
    REQUIRE(engine.getVelocityResponse().getDefinition() == "logarithmic");
    REQUIRE(engine.getVelocityResponse().getGain(64) == Catch::Approx(logarithmicGain));

    REQUIRE(engine.setVelocityResponseDefinition("1:-48.0000,64:-12.0000,127:0"));
    REQUIRE(engine.getVelocityResponse().getDefinition() == "1:-48,64:-12,127:0");
}

TEST_CASE("VelocityResponse - custom points use smooth monotone dB interpolation", "[VelocityResponse]") {
    const auto parsed = VelocityResponse::fromDefinition("1:-48,64:-12,127:0");

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.getTargetDb(1) == Catch::Approx(-48.0f));
    REQUIRE(parsed.value.getTargetDb(64) == Catch::Approx(-12.0f));
    REQUIRE(parsed.value.getTargetDb(127) == Catch::Approx(0.0f));
    REQUIRE(parsed.value.getGain(64) == Catch::Approx(juce::Decibels::decibelsToGain(-12.0f)));
    const float linearDbAtVelocity32 = -48.0f + static_cast<float>(32 - 1) * 36.0f / static_cast<float>(64 - 1);
    REQUIRE(std::abs(parsed.value.getTargetDb(32) - linearDbAtVelocity32) > 0.1f);
    float previousTargetDb = parsed.value.getTargetDb(1);
    for (int velocity = 2; velocity <= 127; ++velocity) {
        const float targetDb = parsed.value.getTargetDb(static_cast<juce::uint8>(velocity));
        REQUIRE(targetDb + 0.0001f >= previousTargetDb);
        REQUIRE(targetDb <= 0.0001f);
        previousTargetDb = targetDb;
    }
    REQUIRE(parsed.value.findClosestVelocityForTargetDb(-12.0f) == 64);
    REQUIRE(parsed.value.getPoints().size() == 3);
    REQUIRE(parsed.value.getPoints()[1].velocity == 64);
    REQUIRE(parsed.value.getPoints()[1].targetDb == Catch::Approx(-12.0f));
}

TEST_CASE("VelocityResponse - invalid custom definitions are rejected", "[VelocityResponse]") {
    REQUIRE_FALSE(VelocityResponse::fromDefinition("1:-48,127:-1").ok());
    REQUIRE_FALSE(VelocityResponse::fromDefinition("1:-12,64:-24,127:0").ok());
    REQUIRE_FALSE(VelocityResponse::fromDefinition("10:-24,127:0").ok());
    REQUIRE_FALSE(VelocityResponse::fromDefinition("1:-24,127:1").ok());
}

TEST_CASE("VelocityResponse - editable points use the shared validation and serializer", "[VelocityResponse]") {
    const std::vector<VelocityResponse::Point> points {
        { 1, -48.0f },
        { 32, -24.0f },
        { 64, -12.0f },
        { 127, 0.0f },
    };
    const auto parsed = VelocityResponse::fromPoints(points);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.getDefinition() == "1:-48,32:-24,64:-12,127:0");
    REQUIRE(parsed.value.getTargetDb(32) == Catch::Approx(-24.0f));

    auto invalidPoints = points;
    invalidPoints[2].targetDb = -30.0f;
    REQUIRE_FALSE(VelocityResponse::fromPoints(invalidPoints).ok());
    REQUIRE_FALSE(VelocityResponse::fromPoints({}).ok());
}

TEST_CASE("VelocityResponse - user curve presets save and load normalized definitions", "[VelocityResponse]") {
    const auto tempParent = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto tempDirectory = tempParent.getNonexistentChildFile("karatala-velocity-response", "", true);
    DrumKitEngine engine;
    engine.dataPath = tempDirectory;

    const auto saved = engine.saveVelocityResponsePreset("Soft Curve", "1:-48.0000,64:-12.0000,127:0", false);
    REQUIRE(saved.ok());
    REQUIRE(saved.value.name == "Soft Curve");
    REQUIRE(saved.value.definition == "1:-48,64:-12,127:0");
    REQUIRE(saved.value.file.existsAsFile());

    const auto duplicate = engine.saveVelocityResponsePreset("Soft Curve", "1:-40,64:-10,127:0", false);
    REQUIRE_FALSE(duplicate.ok());

    const auto replaced = engine.saveVelocityResponsePreset("Soft Curve", "1:-40,64:-10,127:0", true);
    REQUIRE(replaced.ok());
    const auto presets = engine.getVelocityResponsePresets();
    REQUIRE(presets.size() == 4);
    REQUIRE(presets[0].name == "Linear");
    REQUIRE(presets[0].definition == "linear");
    REQUIRE_FALSE(presets[0].isUser);
    REQUIRE(presets[1].name == "Logarithmic");
    REQUIRE(presets[1].definition == "logarithmic");
    REQUIRE_FALSE(presets[1].isUser);
    REQUIRE(presets[2].name == "Exponential");
    REQUIRE(presets[2].definition == "exponential");
    REQUIRE_FALSE(presets[2].isUser);
    const auto presetIt = std::find_if(presets.begin(), presets.end(), [](const auto& preset) { return preset.isUser && preset.name == "Soft Curve"; });
    REQUIRE(presetIt != presets.end());
    REQUIRE(presetIt->definition == "1:-40,64:-10,127:0");

    REQUIRE(tempDirectory.deleteRecursively());
}

TEST_CASE("Calibrated runtime keeps named velocity level boundaries", "[VelocityResponse]") {
    InstrumentPlayer::ArticulationRuntime articulation(1, 0);
    const std::array<juce::uint8, 5> namedVelocities { 27, 52, 77, 102, 127 };
    const std::array<float, 5> fittedLevelsDb { -38.0f, -37.5f, -37.0f, -36.0f, -35.5f };
    for (size_t velocityLevelIdx = 0; velocityLevelIdx < namedVelocities.size(); ++velocityLevelIdx) {
        InstrumentPlayer::VelocityLevelRuntime velocityLevel;
        velocityLevel.velocity = namedVelocities[velocityLevelIdx];
        velocityLevel.fittedSourceDb = fittedLevelsDb[velocityLevelIdx];
        velocityLevel.calibrationTrimDb = 0.0f;
        articulation.velocityLevels.push_back(std::move(velocityLevel));
    }

    articulation.buildVelocityMapping(VelocityResponse::linear());

    REQUIRE(articulation.calibrated);
    REQUIRE(articulation.velocityToLevel[100] == 3);
    REQUIRE(articulation.velocityToLevel[103] == 4);
    REQUIRE(articulation.selectionTargetDb[100] == Catch::Approx(fittedLevelsDb[3]));
    REQUIRE(articulation.velocityGain[100] == Catch::Approx(100.0f / 127.0f));
}

TEST_CASE("Velocity level response changes gain without changing named velocity selection", "[VelocityResponse]") {
    InstrumentPlayer::ArticulationRuntime articulation(1, 0);
    InstrumentPlayer::VelocityLevelRuntime softVelocityLevel;
    softVelocityLevel.velocity = 64;
    softVelocityLevel.fittedSourceDb = -40.0f;
    softVelocityLevel.calibrationTrimDb = 0.0f;
    articulation.velocityLevels.push_back(std::move(softVelocityLevel));
    InstrumentPlayer::VelocityLevelRuntime hardVelocityLevel;
    hardVelocityLevel.velocity = 127;
    hardVelocityLevel.fittedSourceDb = -30.0f;
    hardVelocityLevel.calibrationTrimDb = 0.0f;
    articulation.velocityLevels.push_back(std::move(hardVelocityLevel));

    auto customResponse = VelocityResponse::fromDefinition("1:-48,64:-12,127:0");
    REQUIRE(customResponse.ok());
    articulation.parameters.velocityResponse = customResponse.value;
    articulation.buildVelocityMapping(VelocityResponse::linear());

    REQUIRE(articulation.velocityToLevel[64] == 0);
    REQUIRE(articulation.velocityToLevel[65] == 1);
    REQUIRE(articulation.velocityGain[64] == Catch::Approx(juce::Decibels::decibelsToGain(-12.0f)));
}

TEST_CASE("Library velocity upper boundaries change selection without changing output response", "[VelocityResponse][LibraryKitOverrides]") {
    InstrumentPlayer::ArticulationRuntime articulation(1, 0);
    InstrumentPlayer::VelocityLevelRuntime softVelocityLevel;
    softVelocityLevel.velocity = 43;
    softVelocityLevel.selectionUpperVelocity = 50;
    softVelocityLevel.fittedSourceDb = -40.0f;
    softVelocityLevel.calibrationTrimDb = 0.0f;
    articulation.velocityLevels.push_back(std::move(softVelocityLevel));
    InstrumentPlayer::VelocityLevelRuntime mediumVelocityLevel;
    mediumVelocityLevel.velocity = 85;
    mediumVelocityLevel.selectionUpperVelocity = 91;
    mediumVelocityLevel.fittedSourceDb = -34.0f;
    mediumVelocityLevel.calibrationTrimDb = 0.0f;
    articulation.velocityLevels.push_back(std::move(mediumVelocityLevel));
    InstrumentPlayer::VelocityLevelRuntime hardVelocityLevel;
    hardVelocityLevel.velocity = 127;
    hardVelocityLevel.selectionUpperVelocity = 127;
    hardVelocityLevel.fittedSourceDb = -30.0f;
    hardVelocityLevel.calibrationTrimDb = 0.0f;
    articulation.velocityLevels.push_back(std::move(hardVelocityLevel));

    articulation.buildVelocityMapping(VelocityResponse::linear());

    REQUIRE(articulation.velocityToLevel[50] == 0);
    REQUIRE(articulation.velocityToLevel[51] == 1);
    REQUIRE(articulation.velocityToLevel[91] == 1);
    REQUIRE(articulation.velocityToLevel[92] == 2);
    REQUIRE(articulation.velocityGain[50] == Catch::Approx(50.0f / 127.0f));
}

TEST_CASE("Floating sample windows use only the exact and adjacent velocity levels", "[VelocityResponse][FloatingSampleSelection]") {
    InstrumentPlayer::ArticulationRuntime articulation(1, 0);
    articulation.velocityLevels.push_back(makeFloatingVelocityLevel(32, -30.0f, { -21.5f }));
    articulation.velocityLevels.push_back(makeFloatingVelocityLevel(64, -20.0f, { -20.0f, -19.0f }));
    articulation.velocityLevels.push_back(makeFloatingVelocityLevel(96, -10.0f, { -17.5f }));
    articulation.velocityLevels.push_back(makeFloatingVelocityLevel(127, 0.0f, { -20.1f }));

    articulation.buildVelocityMapping(VelocityResponse::linear());

    REQUIRE(articulation.floatingSelectionAvailable);
    const auto& window = articulation.floatingCandidateWindows[64];
    REQUIRE(window.count == 4);
    CHECK(window.candidates[0].velocityLevelIdx == 1);
    CHECK(window.candidates[0].logicalHitIdx == 0);
    for (size_t candidateIdx = 0; candidateIdx < window.count; ++candidateIdx) CHECK(window.candidates[candidateIdx].velocityLevelIdx != 3);
}

TEST_CASE("Floating sample windows require valid intensity and at most two hits per velocity level", "[VelocityResponse][FloatingSampleSelection]") {
    InstrumentPlayer::ArticulationRuntime missingIntensity(1, 0);
    missingIntensity.velocityLevels.push_back(makeFloatingVelocityLevel(64, -20.0f, { -20.0f }));
    missingIntensity.velocityLevels.push_back(makeFloatingVelocityLevel(127, 0.0f, { -10.0f }));
    missingIntensity.velocityLevels[0].logicalHits[0].preTrimIntensityDb.reset();
    missingIntensity.buildVelocityMapping(VelocityResponse::linear());
    CHECK_FALSE(missingIntensity.floatingSelectionAvailable);

    InstrumentPlayer::ArticulationRuntime tooManyHits(1, 0);
    tooManyHits.velocityLevels.push_back(makeFloatingVelocityLevel(64, -20.0f, { -20.0f, -19.5f, -19.0f }));
    tooManyHits.velocityLevels.push_back(makeFloatingVelocityLevel(127, 0.0f, { -10.0f }));
    tooManyHits.buildVelocityMapping(VelocityResponse::linear());
    CHECK_FALSE(tooManyHits.floatingSelectionAvailable);
}
