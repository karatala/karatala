#include "../source/Engine/KitLibModel.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers_floating_point.hpp"

using namespace KitLib;

TEST_CASE("KitLib - DrumKitSpec::parseInfo parses name, author and license", "[DrumKitSpec]") {
    {
        auto r = DrumKitSpec::parseInfo("Some Kit [MIT] by John Doe");
        REQUIRE(r.ok());
        REQUIRE(r.value.name == "Some Kit");
        REQUIRE(r.value.license == "MIT");
        REQUIRE(r.value.author == "John Doe");
    }
    {
        auto r = DrumKitSpec::parseInfo("NoAuthorKit");
        REQUIRE(r.ok());
        REQUIRE(r.value.name == "NoAuthorKit");
        REQUIRE(r.value.license == "");
        REQUIRE(r.value.author == "");
    }
}

TEST_CASE("KitLib - InstrumentSpec::parseInfo handles optional description", "[InstrumentSpec]") {
    {
        auto r = InstrumentSpec::parseInfo("kick-22-YamahaRecordingCustom");
        REQUIRE(r.ok());
        REQUIRE(r.value.type == InstrumentSpec::Type::Kick);
        REQUIRE(r.value.name == "kick");
        REQUIRE(r.value.size == "22");
        REQUIRE(r.value.description == "YamahaRecordingCustom");
    }
    {
        auto r = InstrumentSpec::parseInfo("Splash-X1");
        REQUIRE(r.ok());
        REQUIRE(r.value.type == InstrumentSpec::Type::Splash);
        REQUIRE(r.value.name == "Splash");
        REQUIRE(r.value.size == "X1");
        REQUIRE(r.value.description == "");
    }
    {
        auto r = InstrumentSpec::parseInfo("something");
        REQUIRE(r.ok());
        REQUIRE(r.value.type == InstrumentSpec::Type::Unknown);
        REQUIRE(r.value.name == "something");
        REQUIRE(r.value.size == "");
        REQUIRE(r.value.description == "");
    }
}

TEST_CASE("KitLib - InstrumentSpec::parseSampleFile parses velocity and micChain", "[InstrumentSpec]") {
    {
        auto r = InstrumentSpec::parseSampleFile("127a-dim_dom_ons.flac");
        REQUIRE(r.ok());
        REQUIRE(r.value.velocity == 127);
        REQUIRE(r.value.sampleVariant == "a");
        REQUIRE(r.value.micDescriptors.size() == 3);
        REQUIRE(r.value.micDescriptors[0].raw == "dim");
        REQUIRE(r.value.micDescriptors[1].raw == "dom");
        REQUIRE(r.value.micDescriptors[2].raw == "ons");
    }
    {// missing leading digits
        auto r = InstrumentSpec::parseSampleFile("a12-dim.wav");
        REQUIRE(!r.ok());
    }
    {// velocity out of range
        auto r = InstrumentSpec::parseSampleFile("000a-dim.wav");
        REQUIRE(!r.ok());
    }
    {// multi-letter rr variant -> warning but still parsed; we check that first letter is used
        auto r = InstrumentSpec::parseSampleFile("100ab-dim.wav");
        REQUIRE(r.ok());
        REQUIRE(r.value.sampleVariant.length() >= 1);
        // first letter should be used by warning logic (function does not alter stored value here)
        REQUIRE(r.value.micDescriptors.size() == 1);
    }
}

TEST_CASE("KitLib - MicSpec::parseMicCode validates and parses codes", "[MicSpec]") {
    {
        auto r = MicSpec::parseMicCode("dtm1");
        REQUIRE(r.ok());
        REQUIRE(r.value.isValid());
        REQUIRE(r.value.raw == "dtm1");
        REQUIRE(r.value.type == MicSpec::Type::Direct);
        REQUIRE(r.value.position == MicSpec::Position::Top);
        REQUIRE(r.value.format == MicSpec::Format::Mono);
        REQUIRE(r.value.variant == "1");
        REQUIRE(r.value.channels == 1);
    }
    {
        auto r = MicSpec::parseMicCode("ons");
        REQUIRE(r.ok());
        REQUIRE(r.value.isValid());
        REQUIRE(r.value.raw == "ons");
        REQUIRE(r.value.type == MicSpec::Type::Overhead);
        REQUIRE(r.value.position == MicSpec::Position::Normal);
        REQUIRE(r.value.format == MicSpec::Format::Stereo);
        REQUIRE(r.value.variant == "");
        REQUIRE(r.value.channels == 2);
    }
    {// invalid type
        auto r = MicSpec::parseMicCode("aaaaa");
        REQUIRE(!r.ok());
    }
    {// invalid type
        auto r = MicSpec::parseMicCode("xsm");
        REQUIRE(!r.ok());
        REQUIRE(!r.value.isValid());
    }
    {// invalid position for direct type
        auto r = MicSpec::parseMicCode("dxm");
        REQUIRE(!r.ok());
        REQUIRE(!r.value.isValid());
    }
    {// invalid format
        auto r = MicSpec::parseMicCode("dsx");
        REQUIRE(!r.ok());
        REQUIRE(!r.value.isValid());
    }
}

TEST_CASE("KitLib - MicSpec::parseMicCodesChain parses chains and skips invalid entries", "[MicSpec]") {
    auto r = MicSpec::parseMicCodesChain("dsm_onm_bad");
    // chain parsing returns a ParseResult with value containing only valid entries
    REQUIRE(r.ok());
    REQUIRE(r.value.size() == 2);
    REQUIRE(r.value[0].raw == "dsm");
    REQUIRE(r.value[1].raw == "onm");
}

TEST_CASE("KitLib - DecayEnvelope base62 decode", "[DecayEnvelope]") {
    REQUIRE(DecayEnvelope::base62ToInt("0") == 0);
    REQUIRE(DecayEnvelope::base62ToInt("9") == 9);
    REQUIRE(DecayEnvelope::base62ToInt("a") == 10);
    REQUIRE(DecayEnvelope::base62ToInt("Z") == 61);
    REQUIRE(DecayEnvelope::base62ToInt("10") == 62);
    REQUIRE(DecayEnvelope::base62ToInt("!") == -1);
}

TEST_CASE("KitLib - DecayEnvelope decode accumulates deltas", "[DecayEnvelope]") {
    const auto env = DecayEnvelope::decode("A_2_3", -20.0f, 10.0f);
    REQUIRE(env.validPoints == 3);
    REQUIRE(env.points[0] == 36.0f);
    REQUIRE(env.points[1] == 38.0f);
    REQUIRE(env.points[2] == 41.0f);
}

TEST_CASE("KitLib - DecayEnvelope level and inverse lookups", "[DecayEnvelope]") {
    DecayEnvelope env;
    env.peakDb = -20.0f;
    env.peakMs = 10.0f;
    env.validPoints = 3;
    env.points[0] = 20.0f; // -6dB
    env.points[1] = 40.0f; // -12dB
    env.points[2] = 60.0f; // -18dB

    CHECK_THAT(env.getAbsoluteDbAtMs(10.0f), Catch::Matchers::WithinAbs(-20.0f, 0.001f));
    CHECK_THAT(env.getAbsoluteDbAtMs(15.0f), Catch::Matchers::WithinAbs(-23.0f, 0.001f));
    CHECK_THAT(env.getAbsoluteDbAtMs(30.0f), Catch::Matchers::WithinAbs(-29.0f, 0.001f));
    CHECK_THAT(env.getAbsoluteDbAtMs(100.0f), Catch::Matchers::WithinAbs(-38.0f, 0.001f));

    CHECK_THAT(env.getMsAtAbsoluteDb(-19.0f), Catch::Matchers::WithinAbs(10.0f, 0.001f));
    CHECK_THAT(env.getMsAtAbsoluteDb(-23.0f), Catch::Matchers::WithinAbs(15.0f, 0.001f));
    CHECK_THAT(env.getMsAtAbsoluteDb(-29.0f), Catch::Matchers::WithinAbs(30.0f, 0.001f));
    CHECK(env.getMsAtAbsoluteDb(-50.0f) < 0.0f);
}
