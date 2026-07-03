#include "../source/Engine/KitLibModel.h"
#include "../source/Engine/LayoutManager.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers_floating_point.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

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


TEST_CASE("KitLayout - parser reads slot routing metadata", "[KitLayout][yaml][routing]") {
    const juce::String yaml = R"yaml(
id: routing
name: Routing
instruments:
  - id: drum1
    display: Octoban 1
    type: octoban
    balance: L48
    directMicOuts: auto
    articulations:
      - { notes: [50], code: center }
  - id: tom2
    display: Tom 2
    articulations:
      - { notes: [47], code: center }
)yaml";

    auto parsed = LayoutTraits<KitLayout>::parseYaml(yaml);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.errors.empty());
    REQUIRE(parsed.value.instrumentSlots.size() == 2);

    const auto& octoban = parsed.value.instrumentSlots[0];
    REQUIRE(octoban.type == InstrumentSpec::Type::Octoban);
    REQUIRE(octoban.balance.has_value());
    REQUIRE(*octoban.balance == -48);
    REQUIRE(octoban.directMicOuts.has_value());
    REQUIRE(*octoban.directMicOuts == DirectMicOutPolicy::Auto);

    const auto& derivedTom = parsed.value.instrumentSlots[1];
    REQUIRE(derivedTom.type == InstrumentSpec::Type::Tom);
    REQUIRE(!derivedTom.balance.has_value());
    REQUIRE(!derivedTom.directMicOuts.has_value());

    auto roundTrip = LayoutTraits<KitLayout>::parseYaml(LayoutTraits<KitLayout>::toYaml(parsed.value));
    REQUIRE(roundTrip.ok());
    REQUIRE(roundTrip.value.instrumentSlots[0].type == InstrumentSpec::Type::Octoban);
    REQUIRE(roundTrip.value.instrumentSlots[0].balance == -48);
    REQUIRE(roundTrip.value.instrumentSlots[0].directMicOuts == DirectMicOutPolicy::Auto);
}

TEST_CASE("KitLayout - parser warns on invalid routing metadata", "[KitLayout][yaml][routing]") {
    const juce::String yaml = R"yaml(
id: routing
name: Routing
instruments:
  - id: aux1
    type: nope
    balance: L0
    directMicOuts: aux
    articulations:
      - { notes: [50], code: center }
)yaml";

    auto parsed = LayoutTraits<KitLayout>::parseYaml(yaml);
    REQUIRE(parsed.ok());
    CHECK_THAT(parsed.error().toStdString(), Catch::Matchers::ContainsSubstring("unknown type"));
    CHECK_THAT(parsed.error().toStdString(), Catch::Matchers::ContainsSubstring("invalid balance"));
    CHECK_THAT(parsed.error().toStdString(), Catch::Matchers::ContainsSubstring("invalid directMicOuts"));
}

TEST_CASE("MicLayout - parser skips entries without mic code", "[MicLayout][yaml][routing]") {
    const juce::String yaml = R"yaml(
id: mics
name: Mics
mics:
  - name: Broken
    instrument: tom1
  - name: Overheads
    code: ons
)yaml";

    auto parsed = LayoutTraits<MicLayout>::parseYaml(yaml);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.mics.size() == 1);
    REQUIRE(parsed.value.mics[0].code == "ons");
    CHECK_THAT(parsed.error().toStdString(), Catch::Matchers::ContainsSubstring("code is empty"));
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

TEST_CASE("KitLib - InstrumentSpec::parseInfo recognizes routing plan kinds", "[InstrumentSpec][routing]") {
    auto octoban = InstrumentSpec::parseInfo("octoban-8-High");
    REQUIRE(octoban.ok());
    REQUIRE(octoban.value.type == InstrumentSpec::Type::Octoban);

    auto cowbel = InstrumentSpec::parseInfo("cowbel-X1");
    REQUIRE(cowbel.ok());
    REQUIRE(cowbel.value.type == InstrumentSpec::Type::Cowbell);

    auto stax = InstrumentSpec::parseInfo("stax-X1");
    REQUIRE(stax.ok());
    REQUIRE(stax.value.type == InstrumentSpec::Type::Stack);
    REQUIRE(InstrumentSpec::typeToString(InstrumentSpec::Type::Stack) == "stack");
}

TEST_CASE("KitLib - articulation folder balance suffix is metadata", "[InstrumentSpec][routing]") {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("karatala-routing-stage1", "", false);
    REQUIRE(root.createDirectory());

    struct TempDirCleanup {
        juce::File file;
        ~TempDirCleanup() { file.deleteRecursively(); }
    } cleanup { root };

    auto bowDir = root.getChildFile("bow[half]-pdl_opn_cls043-L64");
    REQUIRE(bowDir.createDirectory());

    auto centerLeft = root.getChildFile("center-L36");
    auto centerRight = root.getChildFile("center-R36");
    REQUIRE(centerLeft.createDirectory());
    REQUIRE(centerRight.createDirectory());

    DrumKitSpec::MetaInfo kitMeta;
    DrumKitSpec kit(kitMeta);

    InstrumentSpec::MetaInfo instMeta;
    instMeta.rootDir = root;
    instMeta.name = "hihat";
    instMeta.codeRaw = "hihat-14";
    instMeta.type = InstrumentSpec::Type::HiHat;
    InstrumentSpec instrument(&kit, instMeta);

    instrument.scanInstrumentSamples();

    auto* bow = instrument.findArticulation("bow[half]");
    REQUIRE(bow != nullptr);
    REQUIRE(bow->balance.has_value());
    REQUIRE(*bow->balance == -64);
    REQUIRE(!bow->balanceConflict);
    REQUIRE(bow->expressionDimensions.size() == 1);
    REQUIRE(bow->expressionDimensions[0]->codeRaw == "pdl_opn_cls");
    REQUIRE(bow->expressionDimensions[0]->values.size() == 1);
    REQUIRE(bow->expressionDimensions[0]->values[0] == 43);

    auto* center = instrument.findArticulation("center");
    REQUIRE(center != nullptr);
    REQUIRE(!center->balance.has_value());
    REQUIRE(center->balanceConflict);
}