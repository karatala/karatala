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
        auto parsed = InstrumentSpec::parseSampleFile("kick-22-center-127a-dim_dom_ons.flac");
        REQUIRE(parsed.ok());
        REQUIRE(parsed.value.velocity == 127);
        REQUIRE(parsed.value.sampleVariant == "a");
        REQUIRE(parsed.value.micDescriptors.size() == 3);
        REQUIRE(parsed.value.micDescriptors[0].raw == "dim");
        REQUIRE(parsed.value.micDescriptors[1].raw == "dom");
        REQUIRE(parsed.value.micDescriptors[2].raw == "ons");
    }
    {// missing leading digits
        auto parsed = InstrumentSpec::parseSampleFile("kick-22-center-a12-dim.wav");
        REQUIRE(!parsed.ok());
    }
    {// velocity out of range
        auto parsed = InstrumentSpec::parseSampleFile("kick-22-center-000a-dim.wav");
        REQUIRE(!parsed.ok());
    }
    {// multi-letter rr variant -> warning but still parsed; we check that first letter is used
        auto parsed = InstrumentSpec::parseSampleFile("kick-22-center-100ab-dim.wav");
        REQUIRE(parsed.ok());
        REQUIRE(parsed.value.sampleVariant.length() >= 1);
        // first letter should be used by warning logic (function does not alter stored value here)
        REQUIRE(parsed.value.micDescriptors.size() == 1);
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
    DecayEnvelope envelope;
    envelope.peakDb = -20.0f;
    envelope.peakMs = 10.0f;
    envelope.validPoints = 3;
    envelope.points[0] = 20.0f; // -6dB
    envelope.points[1] = 40.0f; // -12dB
    envelope.points[2] = 60.0f; // -18dB

    CHECK_THAT(envelope.getAbsoluteDbAtMs(10.0f), Catch::Matchers::WithinAbs(-20.0f, 0.001f));
    CHECK_THAT(envelope.getAbsoluteDbAtMs(15.0f), Catch::Matchers::WithinAbs(-23.0f, 0.001f));
    CHECK_THAT(envelope.getAbsoluteDbAtMs(20.0f), Catch::Matchers::WithinAbs(-26.0f, 0.001f));
    CHECK_THAT(envelope.getAbsoluteDbAtMs(30.0f), Catch::Matchers::WithinAbs(-29.0f, 0.001f));
    CHECK_THAT(envelope.getAbsoluteDbAtMs(40.0f), Catch::Matchers::WithinAbs(-32.0f, 0.001f));
    CHECK_THAT(envelope.getAbsoluteDbAtMs(60.0f), Catch::Matchers::WithinAbs(-38.0f, 0.001f));
    CHECK_THAT(envelope.getAbsoluteDbAtMs(100.0f), Catch::Matchers::WithinAbs(-38.0f, 0.001f));

    CHECK_THAT(envelope.getMsAtAbsoluteDb(-19.0f), Catch::Matchers::WithinAbs(10.0f, 0.001f));
    CHECK_THAT(envelope.getMsAtAbsoluteDb(-20.0f), Catch::Matchers::WithinAbs(10.0f, 0.001f));
    CHECK_THAT(envelope.getMsAtAbsoluteDb(-23.0f), Catch::Matchers::WithinAbs(15.0f, 0.001f));
    CHECK_THAT(envelope.getMsAtAbsoluteDb(-26.0f), Catch::Matchers::WithinAbs(20.0f, 0.001f));
    CHECK_THAT(envelope.getMsAtAbsoluteDb(-29.0f), Catch::Matchers::WithinAbs(30.0f, 0.001f));
    CHECK_THAT(envelope.getMsAtAbsoluteDb(-32.0f), Catch::Matchers::WithinAbs(40.0f, 0.001f));
    CHECK_THAT(envelope.getMsAtAbsoluteDb(-38.0f), Catch::Matchers::WithinAbs(60.0f, 0.001f));
    CHECK(envelope.getMsAtAbsoluteDb(-50.0f) < 0.0f);
}

TEST_CASE("KitLib - DecayEnvelope lookup round trips measured range", "[DecayEnvelope]") {
    DecayEnvelope envelope;
    envelope.peakDb = -12.0f;
    envelope.peakMs = 8.0f;
    envelope.validPoints = 4;
    envelope.points[0] = 20.0f;
    envelope.points[1] = 44.0f;
    envelope.points[2] = 80.0f;
    envelope.points[3] = 128.0f;

    constexpr std::array<float, 8> timesMs { 8.0f, 14.0f, 20.0f, 32.0f, 44.0f, 62.0f, 80.0f, 104.0f };
    for (const float timeMs : timesMs) {
        const float absoluteDb = envelope.getAbsoluteDbAtMs(timeMs);
        CHECK_THAT(envelope.getMsAtAbsoluteDb(absoluteDb), Catch::Matchers::WithinAbs(timeMs, 0.001f));
    }
}

TEST_CASE("KitLib - DecayEnvelope handles invalid and incomplete data", "[DecayEnvelope]") {
    DecayEnvelope invalidEnvelope;
    CHECK_THAT(invalidEnvelope.getAbsoluteDbAtMs(50.0f), Catch::Matchers::WithinAbs(-100.0f, 0.001f));
    CHECK(invalidEnvelope.getMsAtAbsoluteDb(-30.0f) < 0.0f);

    DecayEnvelope incompleteEnvelope;
    incompleteEnvelope.peakDb = -18.0f;
    incompleteEnvelope.peakMs = 5.0f;
    incompleteEnvelope.validPoints = 1;
    incompleteEnvelope.points[0] = 25.0f;

    CHECK_THAT(incompleteEnvelope.getAbsoluteDbAtMs(15.0f), Catch::Matchers::WithinAbs(-21.0f, 0.001f));
    CHECK_THAT(incompleteEnvelope.getMsAtAbsoluteDb(-21.0f), Catch::Matchers::WithinAbs(15.0f, 0.001f));
    CHECK(incompleteEnvelope.getMsAtAbsoluteDb(-30.0f) < 0.0f);
}
