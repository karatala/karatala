#include "../source/Engine/DrumKitModel.h"
#include "catch2/catch_test_macros.hpp"

using namespace juce;

TEST_CASE("DrumKitInstrument::parseInfo handles optional description", "[DrumKitInstrument]") {
    File dir("kick-22-[YamahaRecordingCustom]");
    auto result = DrumKitInstrument::parseInfo(dir);

    REQUIRE(result.ok());
    REQUIRE(result.value.name == "kick");
    REQUIRE(result.value.size == "22");
    REQUIRE(result.value.description == "YamahaRecordingCustom");
}

TEST_CASE("DrumKitInstrument::scanInstrumentSamples handles articulation dimensions", "[DrumKitInstrument]") {
    File artDir("center-pdl_opn_cls043-zon_bel_edg085");
    DrumKitInstrument instrument(nullptr, {});

    auto* articulation = instrument.getOrAddArticulation("center");
    REQUIRE(articulation != nullptr);

    REQUIRE_NOTHROW({
        articulation->getOrAddVelocityLayer(43);
        articulation->getOrAddVelocityLayer(85);
    });
}

TEST_CASE("DrumKitInstrument::parseSampleFile parses velocity and micChain", "[DrumKitInstrument]") {
    File sampleFile("127a-dim.flac");
    auto result = DrumKitInstrument::parseSampleFile(sampleFile);

    REQUIRE(result.ok());
    REQUIRE(result.value.velocityLayer == 127);
    REQUIRE(result.value.sampleVariant == "a");
    REQUIRE(result.value.micDescriptors.size() == 1);
    REQUIRE(result.value.micDescriptors[0].raw == "dim");
}