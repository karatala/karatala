#include "../source/Engine/KitLibModel.h"
#include "catch2/catch_test_macros.hpp"

using namespace juce;
using namespace KitLib;

TEST_CASE("KitLib - DrumKitSpecs::parseInfo parses name, author and license", "[DrumKitSpecs]") {
    {
        auto r = DrumKitSpecs::parseInfo("Some Kit [MIT] by John Doe");
        REQUIRE(r.ok());
        REQUIRE(r.value.name == "Some Kit");
        REQUIRE(r.value.license == "MIT");
        REQUIRE(r.value.author == "John Doe");
    }
    {
        auto r = DrumKitSpecs::parseInfo("NoAuthorKit");
        REQUIRE(r.ok());
        REQUIRE(r.value.name == "NoAuthorKit");
        REQUIRE(r.value.license == "");
        REQUIRE(r.value.author == "");
    }
}

TEST_CASE("KitLib - InstrumentSpecs::parseInfo handles optional description", "[InstrumentSpecs]") {
    {
        auto r = InstrumentSpecs::parseInfo("kick-22-YamahaRecordingCustom");
        REQUIRE(r.ok());
        REQUIRE(r.value.type == InstrumentSpecs::Type::Kick);
        REQUIRE(r.value.name == "kick");
        REQUIRE(r.value.size == "22");
        REQUIRE(r.value.description == "YamahaRecordingCustom");
    }
    {
        auto r = InstrumentSpecs::parseInfo("Splash-X1");
        REQUIRE(r.ok());
        REQUIRE(r.value.type == InstrumentSpecs::Type::Splash);
        REQUIRE(r.value.name == "Splash");
        REQUIRE(r.value.size == "X1");
        REQUIRE(r.value.description == "");
    }
    {
        auto r = InstrumentSpecs::parseInfo("something");
        REQUIRE(r.ok());
        REQUIRE(r.value.type == InstrumentSpecs::Type::Unknown);
        REQUIRE(r.value.name == "something");
        REQUIRE(r.value.size == "");
        REQUIRE(r.value.description == "");
    }
}

TEST_CASE("KitLib - InstrumentSpecs::parseSampleFile parses velocity and micChain", "[InstrumentSpecs]") {
    {
        auto r = InstrumentSpecs::parseSampleFile("127a-dim_dom_ons.flac");
        REQUIRE(r.ok());
        REQUIRE(r.value.velocity == 127);
        REQUIRE(r.value.sampleVariant == "a");
        REQUIRE(r.value.micDescriptors.size() == 3);
        REQUIRE(r.value.micDescriptors[0].raw == "dim");
        REQUIRE(r.value.micDescriptors[1].raw == "dom");
        REQUIRE(r.value.micDescriptors[2].raw == "ons");
    }
    {// missing leading digits
        auto r = InstrumentSpecs::parseSampleFile("a12-dim.wav");
        REQUIRE(!r.ok());
    }
    {// velocity out of range
        auto r = InstrumentSpecs::parseSampleFile("000a-dim.wav");
        REQUIRE(!r.ok());
    }
    {// multi-letter rr variant -> warning but still parsed; we check that first letter is used
        auto r = InstrumentSpecs::parseSampleFile("100ab-dim.wav");
        REQUIRE(r.ok());
        REQUIRE(r.value.sampleVariant.length() >= 1);
        // first letter should be used by warning logic (function does not alter stored value here)
        REQUIRE(r.value.micDescriptors.size() == 1);
    }
}

TEST_CASE("KitLib - MicSpecs::parseMicCode validates and parses codes", "[MicSpecs]") {
    {
        auto r = MicSpecs::parseMicCode("dtm1");
        REQUIRE(r.ok());
        REQUIRE(r.value.isValid());
        REQUIRE(r.value.raw == "dtm1");
        REQUIRE(r.value.type == MicSpecs::Type::Direct);
        REQUIRE(r.value.position == MicSpecs::Position::Top);
        REQUIRE(r.value.format == MicSpecs::Format::Mono);
        REQUIRE(r.value.variant == "1");
        REQUIRE(r.value.channels == 1);
    }
    {
        auto r = MicSpecs::parseMicCode("ons");
        REQUIRE(r.ok());
        REQUIRE(r.value.isValid());
        REQUIRE(r.value.raw == "ons");
        REQUIRE(r.value.type == MicSpecs::Type::Overhead);
        REQUIRE(r.value.position == MicSpecs::Position::Normal);
        REQUIRE(r.value.format == MicSpecs::Format::Stereo);
        REQUIRE(r.value.variant == "");
        REQUIRE(r.value.channels == 2);
    }
    {// invalid type
        auto r = MicSpecs::parseMicCode("aaaaa");
        REQUIRE(!r.ok());
    }
    {// invalid type
        auto r = MicSpecs::parseMicCode("xsm");
        REQUIRE(!r.ok());
        REQUIRE(!r.value.isValid());
    }
    {// invalid position for direct type
        auto r = MicSpecs::parseMicCode("dxm");
        REQUIRE(!r.ok());
        REQUIRE(!r.value.isValid());
    }
    {// invalid format
        auto r = MicSpecs::parseMicCode("dsx");
        REQUIRE(!r.ok());
        REQUIRE(!r.value.isValid());
    }
}

TEST_CASE("KitLib - MicSpecs::parseMicCodesChain parses chains and skips invalid entries", "[MicSpecs]") {
    auto r = MicSpecs::parseMicCodesChain("dsm_onm_bad");
    // chain parsing returns a ParseResult with value containing only valid entries
    REQUIRE(r.ok());
    REQUIRE(r.value.size() == 2);
    REQUIRE(r.value[0].raw == "dsm");
    REQUIRE(r.value[1].raw == "onm");
}
