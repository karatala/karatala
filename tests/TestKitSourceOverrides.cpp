#include "../source/Engine/KitLibModel.h"
#include "../source/Engine/KitSourceOverrides.h"
#include "catch2/catch_approx.hpp"
#include "catch2/catch_test_macros.hpp"

using namespace KitLib;

TEST_CASE("Kit source overrides parse sparse velocity and sample values", "[KitSourceOverrides][yaml]") {
    KitSourceOverrides overrides;
    const auto parsed = overrides.parseYaml(R"yaml(
version: 1
instruments:
  "snare-X1-C0":
    balance: C0
    articulations:
      "center":
        balance: L12
        velocityUpperBounds:
          "043": 50
          "085": 91
samples:
  "snare-X1-C0/center/snare:X1-center-043a-dtm_ons.flac":
    contentHash: "0123456789abcdef0123456789abcdef"
    manualGainDb: -1.5
    playbackOffsetMs: -0.35
    mics:
      "ons":
        playbackOffsetMs: 0.2
)yaml");

    REQUIRE(parsed.ok());
    const auto* instrumentOverride = overrides.findInstrumentOverride("snare-X1-C0");
    REQUIRE(instrumentOverride != nullptr);
    REQUIRE(instrumentOverride->balance.has_value());
    REQUIRE(*instrumentOverride->balance == 0);
    const auto* articulationOverride = overrides.findArticulationOverride("snare-X1-C0", "center");
    REQUIRE(articulationOverride != nullptr);
    REQUIRE(articulationOverride->balance.has_value());
    REQUIRE(*articulationOverride->balance == -12);
    REQUIRE(articulationOverride->velocityUpperBounds.at(43) == 50);
    REQUIRE(articulationOverride->velocityUpperBounds.at(85) == 91);

    const auto* sampleOverride = overrides.findSampleOverride("snare-X1-C0\\center\\snare:X1-center-043a-dtm_ons.flac");
    REQUIRE(sampleOverride != nullptr);
    REQUIRE(sampleOverride->contentHash == "0123456789abcdef0123456789abcdef");
    REQUIRE(sampleOverride->manualGainDb.has_value());
    REQUIRE(*sampleOverride->manualGainDb == Catch::Approx(-1.5f));
    REQUIRE(sampleOverride->playbackOffsetMs.has_value());
    REQUIRE(*sampleOverride->playbackOffsetMs == Catch::Approx(-0.35f));
    REQUIRE(sampleOverride->micOverrides.at("ons").playbackOffsetMs.has_value());
    REQUIRE(*sampleOverride->micOverrides.at("ons").playbackOffsetMs == Catch::Approx(0.2f));
}

TEST_CASE("Kit source overrides resolve complete increasing velocity ranges", "[KitSourceOverrides][velocity]") {
    KitSourceOverrides overrides;
    overrides.setVelocityUpperBoundary("snare-X1", "center", 43, 50);
    overrides.setVelocityUpperBoundary("snare-X1", "center", 85, 91);

    const auto resolved = overrides.resolveVelocityUpperBoundaries("snare-X1", "center", { 43, 85, 127 });
    REQUIRE(resolved.ok());
    REQUIRE(resolved.value == std::vector<uint8_t> { 50, 91, 127 });

    overrides.setVelocityUpperBoundary("snare-X1", "center", 43, 100);
    const auto invalid = overrides.resolveVelocityUpperBoundaries("snare-X1", "center", { 43, 85, 127 });
    REQUIRE_FALSE(invalid.ok());

    overrides.setVelocityUpperBoundary("snare-X1", "center", 43, std::nullopt);
    const auto reset = overrides.resolveVelocityUpperBoundaries("snare-X1", "center", { 43, 85, 127 });
    REQUIRE(reset.ok());
    REQUIRE(reset.value == std::vector<uint8_t> { 43, 91, 127 });
}

TEST_CASE("Kit source overrides isolate invalid fields and paths", "[KitSourceOverrides][yaml]") {
    KitSourceOverrides overrides;
    const auto parsed = overrides.parseYaml(R"yaml(
version: 1
samples:
  "../outside.flac":
    manualGainDb: 4
  "snare-X1/center/good.flac":
    contentHash: "not-a-hash"
    manualGainDb: 0
)yaml");

    REQUIRE(parsed.ok());
    REQUIRE_FALSE(parsed.errors.empty());
    REQUIRE(overrides.findSampleOverride("../outside.flac") == nullptr);
    const auto* validSibling = overrides.findSampleOverride("snare-X1/center/good.flac");
    REQUIRE(validSibling != nullptr);
    REQUIRE(validSibling->contentHash.isEmpty());
    REQUIRE(validSibling->manualGainDb.has_value());
    REQUIRE(*validSibling->manualGainDb == Catch::Approx(0.0f));

    KitSourceOverrides unsupported;
    REQUIRE_FALSE(unsupported.parseYaml("version: 2\n").ok());
}

TEST_CASE("Kit source overrides save and reload deterministic YAML", "[KitSourceOverrides][yaml][file]") {
    const auto kitRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("karatala-kit-overrides", "", false);
    REQUIRE(kitRoot.createDirectory());

    struct TempDirectoryCleanup {
        juce::File directory;
        ~TempDirectoryCleanup() { this->directory.deleteRecursively(); }
    } cleanup { kitRoot };

    KitSourceOverrides overrides;
    overrides.setVelocityUpperBoundary("ride-R1", "bow", 64, 72);
    REQUIRE(overrides.saveToKitRoot(kitRoot).ok());

    KitSourceOverrides loaded;
    const auto loadedResult = loaded.loadFromKitRoot(kitRoot);
    REQUIRE(loadedResult.ok());
    REQUIRE(loaded.findArticulationOverride("ride-R1", "bow") != nullptr);
    REQUIRE(loaded.findArticulationOverride("ride-R1", "bow")->velocityUpperBounds.at(64) == 72);
    REQUIRE(loaded.toYaml() == overrides.toYaml());

    overrides.setVelocityUpperBoundary("ride-R1", "bow", 64, 73);
    REQUIRE(overrides.saveToKitRoot(kitRoot).ok());
    REQUIRE(kitRoot.getChildFile("kit-overrides.yml.bak").existsAsFile());
}

TEST_CASE("Instrument source model applies velocity boundaries without changing named layers", "[KitSourceOverrides][velocity][KitLib]") {
    DrumKitSpec kit { { {}, "KitA", "Kit A", {}, {} } };
    kit.sourceOverrides.setVelocityUpperBoundary("snare-X1", "center", 43, 50);
    kit.sourceOverrides.setVelocityUpperBoundary("snare-X1", "center", 85, 91);

    InstrumentSpec::MetaInfo instrumentMeta;
    instrumentMeta.codeRaw = "snare-X1";
    InstrumentSpec instrument(&kit, instrumentMeta);
    auto* articulation = instrument.getOrAddArticulation("center");
    articulation->getOrAddVelocityLayer(43);
    articulation->getOrAddVelocityLayer(85);
    articulation->getOrAddVelocityLayer(127);

    const auto applied = instrument.applyVelocityUpperBoundaryOverrides(*articulation);
    REQUIRE(applied.ok());
    REQUIRE(articulation->velocityLayerSpecs[0]->velocity == 43);
    REQUIRE(articulation->velocityLayerSpecs[0]->selectionUpperVelocity == 50);
    REQUIRE(articulation->velocityLayerSpecs[1]->velocity == 85);
    REQUIRE(articulation->velocityLayerSpecs[1]->selectionUpperVelocity == 91);
    REQUIRE(articulation->velocityLayerSpecs[2]->velocity == 127);
    REQUIRE(articulation->velocityLayerSpecs[2]->selectionUpperVelocity == 127);
}

TEST_CASE("Kit source values preserve filename defaults and apply loaded overrides", "[KitSourceOverrides][KitLib]") {
    const auto kitRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("karatala-source-values", "", false);
    const auto instrumentRoot = kitRoot.getChildFile("snare-X1-L64");
    const auto articulationRoot = instrumentRoot.getChildFile("center-L12");
    REQUIRE(articulationRoot.createDirectory());
    const auto sampleFile = articulationRoot.getChildFile("snare-X1-center-043a-dtm-vn4-on2.flac");
    REQUIRE(sampleFile.replaceWithText("fixture"));

    struct TempDirectoryCleanup {
        juce::File directory;
        ~TempDirectoryCleanup() { this->directory.deleteRecursively(); }
    } cleanup { kitRoot };

    DrumKitSpec kit { { kitRoot, "KitA", "Kit A", {}, {} } };
    kit.sourceOverrides.instrumentOverrides["snare-X1-L64"].balance = 0;
    kit.sourceOverrides.instrumentOverrides["snare-X1-L64"].articulationOverrides["center"].balance = 20;
    SampleSourceOverride sampleOverride;
    sampleOverride.manualGainDb = -1.5f;
    sampleOverride.playbackOffsetMs = 0.25f;
    kit.sourceOverrides.sampleOverrides["snare-X1-L64/center-L12/snare-X1-center-043a-dtm-vn4-on2.flac"] = sampleOverride;

    auto instrumentMeta = InstrumentSpec::parseInfo("snare-X1-L64").value;
    instrumentMeta.rootDir = instrumentRoot;
    InstrumentSpec instrument(&kit, instrumentMeta);
    REQUIRE(instrument.filenameBalance.has_value());
    REQUIRE(*instrument.filenameBalance == -64);
    REQUIRE(instrument.meta.balance.has_value());
    REQUIRE(*instrument.meta.balance == -64);
    REQUIRE(instrument.getEffectiveBalance().has_value());
    REQUIRE(*instrument.getEffectiveBalance() == 0);
    REQUIRE(instrument.scanInstrumentSamples() == 1);

    const auto* articulation = instrument.findArticulation("center");
    REQUIRE(articulation != nullptr);
    REQUIRE(articulation->filenameBalance.has_value());
    REQUIRE(*articulation->filenameBalance == -12);
    REQUIRE(articulation->balance.has_value());
    REQUIRE(*articulation->balance == -12);
    REQUIRE(articulation->getEffectiveBalance().has_value());
    REQUIRE(*articulation->getEffectiveBalance() == 20);
    const auto* sample = articulation->velocityLayerSpecs.front()->sampleVariantSpecs.front().get();
    REQUIRE(sample->filenameManualGainDb == Catch::Approx(-4.0f));
    REQUIRE(sample->manualGainDb == Catch::Approx(-1.5f));
    REQUIRE(sample->filenamePlaybackOffsetMs == Catch::Approx(-2.0f));
    REQUIRE(sample->playbackOffsetMs == Catch::Approx(0.25f));
}
