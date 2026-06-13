#include "../source/Engine/DrumKitEngine.h"
#include "catch2/catch_test_macros.hpp"

TEST_CASE("EngineSettings - realtime scalar settings load and store", "[EngineSettings]") {
    RealtimeSetting<float> floatSetting { 0.25f };
    RealtimeSetting<int> intSetting { 2 };

    REQUIRE(floatSetting.load() == 0.25f);
    REQUIRE(intSetting.load() == 2);

    floatSetting.store(0.75f);
    intSetting.store(4);

    REQUIRE(floatSetting.load() == 0.75f);
    REQUIRE(intSetting.load() == 4);
}

TEST_CASE("EngineSettings - legacy hi-hat YAML migrates without overriding new keys", "[EngineSettings][yaml]") {
    DrumKitEngine engine;
    const auto result = engine.parseConfigYaml(R"yaml(
engine:
  hihat:
    pedalCrossfadeSec: 0.12
    pedalMinRemainingSec: 0.08
    crossArtChokeDbMargin: 4.5
    transition:
      closeCrossfadeSec: 0.01
)yaml");

    REQUIRE(result.ok());
    REQUIRE(engine.engineSettings.hihatOpenCrossfadeSec.load() == 0.12f);
    REQUIRE(engine.engineSettings.hihatCloseCrossfadeSec.load() == 0.01f);
    REQUIRE(engine.engineSettings.hihatTransitionMinRemainingSec.load() == 0.08f);
    REQUIRE(engine.engineSettings.hihatHitChokeDbMargin.load() == 4.5f);
}

TEST_CASE("EngineSettings - hi-hat YAML clamps and serializes the new contract", "[EngineSettings][yaml]") {
    DrumKitEngine engine;
    const auto result = engine.parseConfigYaml(R"yaml(
engine:
  hihat:
    hitChokeDbMargin: 99
    transition:
      openCrossfadeSec: 1
      closeCrossfadeSec: 0
      controllerSmoothingSec: 0.02
      boundaryHysteresisCc: 20
      tailMatchAmount: 0.4
      tailMatchBiasDb: -5
      attackGuardSec: 0.03
      maxActiveGroups: 3
      curve: smooth
      footSampleMode: always
      minRemainingSec: 0.07
      silenceFloorDb: -60
      missingDecayPolicy: simple_fade
)yaml");

    REQUIRE(result.ok());
    REQUIRE(engine.engineSettings.hihatHitChokeDbMargin.load() == 20.0f);
    REQUIRE(engine.engineSettings.hihatOpenCrossfadeSec.load() == 0.250f);
    REQUIRE(engine.engineSettings.hihatCloseCrossfadeSec.load() == 0.002f);
    REQUIRE(engine.engineSettings.hihatBoundaryHysteresisCc.load() == 8);
    REQUIRE(engine.engineSettings.hihatTransitionCurve.load() == HiHatTransitionCurve::Smooth);
    REQUIRE(engine.engineSettings.hihatFootSampleMode.load() == HiHatFootSampleMode::Always);
    REQUIRE(engine.engineSettings.hihatMissingDecayPolicy.load() == HiHatMissingDecayPolicy::SimpleFade);

    const auto serialized = engine.serializeSettingsYaml();
    REQUIRE(serialized.contains("hitChokeDbMargin: 20"));
    REQUIRE(serialized.contains("openCrossfadeSec: 0.25"));
    REQUIRE(serialized.contains("curve: smooth"));
    REQUIRE(serialized.contains("footSampleMode: always"));
    REQUIRE(serialized.contains("missingDecayPolicy: simple_fade"));
    REQUIRE_FALSE(serialized.contains("pedalCrossfadeSec"));
    REQUIRE_FALSE(serialized.contains("crossArtChokeDbMargin"));

    const auto invalidEnumsResult = engine.parseConfigYaml(R"yaml(
engine:
  hihat:
    transition:
      curve: unknown
      footSampleMode: unknown
      missingDecayPolicy: unknown
)yaml");
    REQUIRE(invalidEnumsResult.ok());
    REQUIRE(engine.engineSettings.hihatTransitionCurve.load() == HiHatTransitionCurve::EqualPower);
    REQUIRE(engine.engineSettings.hihatFootSampleMode.load() == HiHatFootSampleMode::CloseOnly);
    REQUIRE(engine.engineSettings.hihatMissingDecayPolicy.load() == HiHatMissingDecayPolicy::KeepTail);
}

TEST_CASE("EngineSettings - realtime settings remain copyable snapshots", "[EngineSettings]") {
    EngineSettings sourceSettings;
    sourceSettings.hihatOpenCrossfadeSec.store(0.125f);
    sourceSettings.hihatTransitionCurve.store(HiHatTransitionCurve::Smooth);
    sourceSettings.roundRobinLock.store(3);

    EngineSettings copiedSettings = sourceSettings;
    REQUIRE(copiedSettings.hihatOpenCrossfadeSec.load() == 0.125f);
    REQUIRE(copiedSettings.hihatTransitionCurve.load() == HiHatTransitionCurve::Smooth);
    REQUIRE(copiedSettings.roundRobinLock.load() == 3);

    copiedSettings.hihatOpenCrossfadeSec.store(0.2f);
    copiedSettings.hihatTransitionCurve.store(HiHatTransitionCurve::Linear);
    copiedSettings.roundRobinLock.store(1);

    REQUIRE(sourceSettings.hihatOpenCrossfadeSec.load() == 0.125f);
    REQUIRE(sourceSettings.hihatTransitionCurve.load() == HiHatTransitionCurve::Smooth);
    REQUIRE(sourceSettings.roundRobinLock.load() == 3);
}
