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


TEST_CASE("EngineSettings - global config parses instrument catalog", "[EngineSettings][yaml][routing]") {
    DrumKitEngine engine;
    const auto result = engine.parseConfigYaml(R"yaml(
instrumentCatalog:
  kinds:
    octoban: { family: drum, directMicOuts: own }
    cowbell: { family: other, directMicOuts: shared }
  aliases:
    cowbel: cowbell
)yaml");

    REQUIRE(result.ok());
    REQUIRE(result.errors.empty());
    const auto& catalog = result.value.instrumentCatalog;
    REQUIRE(catalog.familyFor(KitLib::InstrumentSpec::Type::Octoban) == InstrumentFamily::Drum);
    REQUIRE(catalog.defaultDirectMicOutsFor(KitLib::InstrumentSpec::Type::Octoban).has_value());
    REQUIRE(*catalog.defaultDirectMicOutsFor(KitLib::InstrumentSpec::Type::Octoban) == DirectMicOutPolicy::Own);
    REQUIRE(catalog.defaultDirectMicOutsFor(KitLib::InstrumentSpec::Type::Ride).has_value());
    REQUIRE(*catalog.defaultDirectMicOutsFor(KitLib::InstrumentSpec::Type::Ride) == DirectMicOutPolicy::Own);
    REQUIRE(catalog.resolveKind("cowbel") == KitLib::InstrumentSpec::Type::Cowbell);
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

TEST_CASE("EngineSettings - all hi-hat transition settings survive YAML round trip", "[EngineSettings][yaml]") {
    DrumKitEngine sourceEngine;
    sourceEngine.engineSettings.polyphonyCrossfadeTimeoutSec.store(0.45f);
    sourceEngine.engineSettings.hihatHitChokeDbMargin.store(7.5f);
    sourceEngine.engineSettings.hihatOpenCrossfadeSec.store(0.125f);
    sourceEngine.engineSettings.hihatCloseCrossfadeSec.store(0.035f);
    sourceEngine.engineSettings.hihatControllerSmoothingSec.store(0.042f);
    sourceEngine.engineSettings.hihatBoundaryHysteresisCc.store(6);
    sourceEngine.engineSettings.hihatTailMatchAmount.store(0.35f);
    sourceEngine.engineSettings.hihatTailMatchBiasDb.store(-7.0f);
    sourceEngine.engineSettings.hihatTransitionAttackGuardSec.store(0.055f);
    sourceEngine.engineSettings.hihatTransitionMaxActiveGroups.store(3);
    sourceEngine.engineSettings.hihatTransitionCurve.store(HiHatTransitionCurve::Linear);
    sourceEngine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
    sourceEngine.engineSettings.hihatTransitionMinRemainingSec.store(0.175f);
    sourceEngine.engineSettings.hihatTransitionSilenceFloorDb.store(-54.0f);
    sourceEngine.engineSettings.hihatMissingDecayPolicy.store(HiHatMissingDecayPolicy::SimpleFade);

    DrumKitEngine restoredEngine;
    const auto parseResult = restoredEngine.parseConfigYaml(sourceEngine.serializeSettingsYaml());

    REQUIRE(parseResult.ok());
    REQUIRE(restoredEngine.engineSettings.polyphonyCrossfadeTimeoutSec.load() == 0.45f);
    REQUIRE(restoredEngine.engineSettings.hihatHitChokeDbMargin.load() == 7.5f);
    REQUIRE(restoredEngine.engineSettings.hihatOpenCrossfadeSec.load() == 0.125f);
    REQUIRE(restoredEngine.engineSettings.hihatCloseCrossfadeSec.load() == 0.035f);
    REQUIRE(restoredEngine.engineSettings.hihatControllerSmoothingSec.load() == 0.042f);
    REQUIRE(restoredEngine.engineSettings.hihatBoundaryHysteresisCc.load() == 6);
    REQUIRE(restoredEngine.engineSettings.hihatTailMatchAmount.load() == 0.35f);
    REQUIRE(restoredEngine.engineSettings.hihatTailMatchBiasDb.load() == -7.0f);
    REQUIRE(restoredEngine.engineSettings.hihatTransitionAttackGuardSec.load() == 0.055f);
    REQUIRE(restoredEngine.engineSettings.hihatTransitionMaxActiveGroups.load() == 3);
    REQUIRE(restoredEngine.engineSettings.hihatTransitionCurve.load() == HiHatTransitionCurve::Linear);
    REQUIRE(restoredEngine.engineSettings.hihatFootSampleMode.load() == HiHatFootSampleMode::Off);
    REQUIRE(restoredEngine.engineSettings.hihatTransitionMinRemainingSec.load() == 0.175f);
    REQUIRE(restoredEngine.engineSettings.hihatTransitionSilenceFloorDb.load() == -54.0f);
    REQUIRE(restoredEngine.engineSettings.hihatMissingDecayPolicy.load() == HiHatMissingDecayPolicy::SimpleFade);
}

TEST_CASE("EngineSettings - aftertouch noise threshold and crosstalk profile round trip", "[EngineSettings][yaml][MidiCrosstalk]") {
    DrumKitEngine sourceEngine;
    sourceEngine.engineSettings.polyAftertouchMinPressure.store(14);
    sourceEngine.engineSettings.crosstalkWindowMs.store(55);
    sourceEngine.setCrosstalkThreshold(49, 48, 31);
    sourceEngine.setCrosstalkThreshold(38, 40, 18);

    DrumKitEngine restoredEngine;
    const auto parseResult = restoredEngine.parseConfigYaml(sourceEngine.serializeSettingsYaml());

    REQUIRE(parseResult.ok());
    REQUIRE(restoredEngine.engineSettings.polyAftertouchMinPressure.load() == 14);
    REQUIRE(restoredEngine.engineSettings.crosstalkWindowMs.load() == 55);
    REQUIRE(restoredEngine.getCrosstalkProfileEntryCount() == 2);
    REQUIRE(restoredEngine.serializeSettingsYaml().contains("{ primary: 49, secondary: 48, ratio: 31 }"));
    REQUIRE(restoredEngine.buildCrosstalkProfileText().contains("49 "));
    REQUIRE(restoredEngine.buildCrosstalkProfileText().contains("-> 48 "));
    REQUIRE(restoredEngine.buildCrosstalkProfileText().contains("suppress <= 24%"));
}

TEST_CASE("EngineSettings - global log level YAML defaults and validation", "[EngineSettings][yaml][logging]") {
    DrumKitEngine defaultEngine;
    REQUIRE(defaultEngine.getDefaultYaml().contains("logLevel: off"));
    REQUIRE(defaultEngine.serializeSettingsYaml().contains("logLevel: off"));

    DrumKitEngine missingEngine;
    const auto missingResult = missingEngine.parseConfigYaml("kitLayout: default\n");
    REQUIRE(missingResult.ok());
    REQUIRE(missingResult.value.logLevel == DebugLevel::Off);

    DrumKitEngine warningEngine;
    const auto warningResult = warningEngine.parseConfigYaml("logLevel: warning\n");
    REQUIRE(warningResult.ok());
    REQUIRE(warningResult.value.logLevel == DebugLevel::Warning);
    warningEngine.setGlobalLogLevel(warningResult.value.logLevel);
    REQUIRE(warningEngine.getGlobalLogLevel() == DebugLevel::Warning);
    REQUIRE(GlobalLogger::instance().getLogLevel() == DebugLevel::Warning);

    DrumKitEngine invalidEngine;
    const auto invalidResult = invalidEngine.parseConfigYaml("logLevel: loud\n");
    REQUIRE(invalidResult.ok());
    REQUIRE(invalidResult.value.logLevel == DebugLevel::Off);
    REQUIRE_FALSE(invalidResult.errors.empty());
}

TEST_CASE("EngineSettings - global log level YAML round trip", "[EngineSettings][yaml][logging]") {
    const DebugLevel levels[] = {
        DebugLevel::Off,
        DebugLevel::Error,
        DebugLevel::Warning,
        DebugLevel::Notice,
        DebugLevel::Info,
        DebugLevel::Debug,
        DebugLevel::Verbose,
    };

    for (const auto level : levels) {
        DrumKitEngine sourceEngine;
        sourceEngine.setGlobalLogLevel(level);

        DrumKitEngine restoredEngine;
        const auto parseResult = restoredEngine.parseConfigYaml(sourceEngine.serializeSettingsYaml());

        REQUIRE(parseResult.ok());
        REQUIRE(parseResult.value.logLevel == level);
        REQUIRE(sourceEngine.serializeSettingsYaml().contains("logLevel: " + DebugLevelToYaml(level)));
    }
}
