#include "../source/Engine/KitLibModel.h"
#include "../source/Engine/LayoutManager.h"
#include "../source/Engine/InstrumentCatalog.h"
#include "../source/Engine/MicRoutingPlan.h"
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


TEST_CASE("KitLib - InstrumentSpec::parseInfo parses balance identity metadata", "[InstrumentSpec][identity]") {
    auto parsed = InstrumentSpec::parseInfo("tom-10-L64-Sonor");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.type == InstrumentSpec::Type::Tom);
    REQUIRE(parsed.value.name == "tom");
    REQUIRE(parsed.value.size == "10");
    REQUIRE(parsed.value.balance.has_value());
    REQUIRE(*parsed.value.balance == -64);
    REQUIRE(parsed.value.description == "Sonor");

    const auto candidates = InstrumentSpec::getIdentityCandidates(parsed.value);
    REQUIRE(candidates.size() == 3);
    CHECK(candidates[0] == "tom-10");
    CHECK(candidates[1] == "tom-10-L64");
    CHECK(candidates[2] == "tom-10-L64-Sonor");
}

TEST_CASE("KitLib - InstrumentSpec::parseInfo warns and ignores incomplete balance tokens", "[InstrumentSpec][identity]") {
    auto left = InstrumentSpec::parseInfo("tom-10-L-Sonor");
    REQUIRE(left.ok());
    REQUIRE(!left.value.balance.has_value());
    REQUIRE(left.value.description == "Sonor");
    CHECK_THAT(left.error().toStdString(), Catch::Matchers::ContainsSubstring("Invalid or missing instrument balance value 'L'"));

    auto right = InstrumentSpec::parseInfo("tom-10-R-Sonor");
    REQUIRE(right.ok());
    REQUIRE(!right.value.balance.has_value());
    REQUIRE(right.value.description == "Sonor");
    CHECK_THAT(right.error().toStdString(), Catch::Matchers::ContainsSubstring("Invalid or missing instrument balance value 'R'"));

    auto center = InstrumentSpec::parseInfo("tom-10-C-Sonor");
    REQUIRE(center.ok());
    REQUIRE(!center.value.balance.has_value());
    REQUIRE(center.value.description == "Sonor");
    CHECK_THAT(center.error().toStdString(), Catch::Matchers::ContainsSubstring("Invalid or missing instrument balance value 'C'"));

    auto ludwig = InstrumentSpec::parseInfo("kick-22-Ludwig");
    REQUIRE(ludwig.ok());
    REQUIRE(!ludwig.value.balance.has_value());
    REQUIRE(ludwig.value.description == "Ludwig");
    REQUIRE(ludwig.errors.empty());
}

TEST_CASE("KitLib - DrumKitSpec resolves shortest unique instrument identity", "[DrumKitSpec][identity]") {
    DrumKitSpec kit { { {}, "KitA", "Kit A", {}, {} } };

    auto firstMeta = InstrumentSpec::parseInfo("tom-10-Sonor").value;
    kit.addInstrument(firstMeta);
    auto* first = kit.findInstrument("tom-10-Sonor");
    REQUIRE(first != nullptr);

    auto secondMeta = InstrumentSpec::parseInfo("tom-10-L64-Yamaha").value;
    kit.addInstrument(secondMeta);
    auto* second = kit.findInstrument("tom-10-L64-Yamaha");
    REQUIRE(second != nullptr);

    CHECK(kit.findInstrumentById("tom-10") == nullptr);
    CHECK(kit.findInstrumentById("tom-10-Sonor") == first);
    CHECK(kit.findInstrumentById("tom-10-L64") == second);
    CHECK(kit.findInstrumentById("TOM-10-l64") == second);
    CHECK(kit.getPreferredInstrumentId(*first) == "tom-10-Sonor");
    CHECK(kit.getPreferredInstrumentId(*second) == "tom-10-L64");
}

TEST_CASE("KitLib - LibraryManager resolves shortest unique kit identity", "[DrumKitSpec][identity]") {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("karatala-kit-identity", "", false);
    REQUIRE(root.createDirectory());

    struct TempDirCleanup {
        juce::File file;
        ~TempDirCleanup() { file.deleteRecursively(); }
    } cleanup { root };

    REQUIRE(root.getChildFile("AasiMonster by DIE [CC-BY-4.0]").createDirectory());
    REQUIRE(root.getChildFile("AasiMonster by Other [CC-BY-4.0]").createDirectory());
    REQUIRE(root.getChildFile("SameName by SameAuthor [MIT]").createDirectory());
    REQUIRE(root.getChildFile("SameName by SameAuthor [GPL]").createDirectory());

    LibraryManager manager(root);
    REQUIRE(manager.scanAll() == 4);

    auto* die = manager.findKitById("AasiMonster by DIE");
    REQUIRE(die != nullptr);
    CHECK(die->meta.author == "DIE");
    CHECK(manager.findKitById("AasiMonster") == nullptr);
    CHECK(manager.getPreferredKitId(*die) == "AasiMonster by DIE");

    auto* mit = manager.findKitById("SameName by SameAuthor [MIT]");
    REQUIRE(mit != nullptr);
    CHECK(mit->meta.license == "MIT");
    CHECK(manager.findKitById("SameName by SameAuthor") == nullptr);
    CHECK(manager.getPreferredKitId(*mit) == "SameName by SameAuthor [MIT]");
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


TEST_CASE("InstrumentCatalog - defaults and YAML overrides", "[InstrumentCatalog][routing]") {
    auto catalog = InstrumentCatalog::makeDefault();
    REQUIRE(catalog.familyFor(InstrumentSpec::Type::Octoban) == InstrumentFamily::Drum);
    REQUIRE(catalog.defaultDirectMicOutsFor(InstrumentSpec::Type::Octoban).has_value());
    REQUIRE(*catalog.defaultDirectMicOutsFor(InstrumentSpec::Type::Octoban) == DirectMicOutPolicy::Auto);
    REQUIRE(catalog.resolveKind("cowbel") == InstrumentSpec::Type::Cowbell);

    const auto root = YamlHelper::parse(R"yaml(
instrumentCatalog:
  kinds:
    octoban: { family: drum, directMicOuts: own }
    shaker: { family: other, directMicOuts: shared }
  aliases:
    shaker_box: shaker
)yaml");

    auto parsed = InstrumentCatalog::parseYaml(root["instrumentCatalog"]);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.errors.empty());
    REQUIRE(parsed.value.defaultDirectMicOutsFor(InstrumentSpec::Type::Octoban).has_value());
    REQUIRE(*parsed.value.defaultDirectMicOutsFor(InstrumentSpec::Type::Octoban) == DirectMicOutPolicy::Own);
    REQUIRE(parsed.value.defaultDirectMicOutsFor(InstrumentSpec::Type::Shaker).has_value());
    REQUIRE(*parsed.value.defaultDirectMicOutsFor(InstrumentSpec::Type::Shaker) == DirectMicOutPolicy::Shared);
    REQUIRE(parsed.value.resolveKind("shaker_box") == InstrumentSpec::Type::Shaker);
}

TEST_CASE("Mic instrument selectors parse and match by priority", "[InstrumentCatalog][routing]") {
    const auto catalog = InstrumentCatalog::makeDefault();

    const auto exact = parseMicInstrumentSelector("tom1", catalog);
    REQUIRE(matchMicInstrumentSelector(exact, "tom1", InstrumentSpec::Type::Tom, catalog) == MicSelectorPriority::ExactSlotId);
    REQUIRE(matchMicInstrumentSelector(exact, "tom2", InstrumentSpec::Type::Tom, catalog) == MicSelectorPriority::None);

    const auto wildcard = parseMicInstrumentSelector("rototom*", catalog);
    REQUIRE(matchMicInstrumentSelector(wildcard, "rototom1", InstrumentSpec::Type::Rototom, catalog) == MicSelectorPriority::WildcardPrefix);

    const auto kind = parseMicInstrumentSelector("cowbell", catalog);
    REQUIRE(matchMicInstrumentSelector(kind, "perc1", InstrumentSpec::Type::Cowbell, catalog) == MicSelectorPriority::Kind);

    const auto list = parseMicInstrumentSelector("tambourine,cowbell,vibraslap", catalog);
    REQUIRE(matchMicInstrumentSelector(list, "perc1", InstrumentSpec::Type::Cowbell, catalog) == MicSelectorPriority::CommaList);

    const auto family = parseMicInstrumentSelector("other", catalog);
    REQUIRE(matchMicInstrumentSelector(family, "perc1", InstrumentSpec::Type::Cowbell, catalog) == MicSelectorPriority::Family);

    const auto common = parseMicInstrumentSelector("", catalog);
    REQUIRE(matchMicInstrumentSelector(common, "kick1", InstrumentSpec::Type::Kick, catalog) == MicSelectorPriority::EmptyCommon);

    REQUIRE(static_cast<int>(MicSelectorPriority::ExactSlotId) > static_cast<int>(MicSelectorPriority::WildcardPrefix));
    REQUIRE(static_cast<int>(MicSelectorPriority::WildcardPrefix) > static_cast<int>(MicSelectorPriority::Kind));
    REQUIRE(static_cast<int>(MicSelectorPriority::Kind) > static_cast<int>(MicSelectorPriority::CommaList));
    REQUIRE(static_cast<int>(MicSelectorPriority::CommaList) > static_cast<int>(MicSelectorPriority::Family));
    REQUIRE(static_cast<int>(MicSelectorPriority::Family) > static_cast<int>(MicSelectorPriority::EmptyCommon));
}

TEST_CASE("Direct mic compatibility helpers compare full codes and group outputs", "[InstrumentCatalog][routing]") {
    const auto dtm1 = MicSpec::parseMicCode("dtm1").value;
    const auto dtm1b = MicSpec::parseMicCode("dtm1").value;
    const auto dtm2 = MicSpec::parseMicCode("dtm2").value;
    const auto dsm = MicSpec::parseMicCode("dsm").value;
    const auto dss = MicSpec::parseMicCode("dss").value;

    REQUIRE(directMicSpecsMatchExactly(dtm1, dtm1b));
    REQUIRE_FALSE(directMicSpecsMatchExactly(dtm1, dtm2));
    REQUIRE_FALSE(directMicSpecsMatchExactly(dsm, dss));
    REQUIRE(directMicSpecsCanUseGroupOutput(dsm, dss));
    REQUIRE(directMicSpecsCanUseGroupOutput(dtm1, dtm1b));
    REQUIRE_FALSE(directMicSpecsCanUseGroupOutput(dtm1, dtm2));
}


TEST_CASE("MicRoutingPlan - allocates direct claims and group fallbacks", "[MicRoutingPlan][routing]") {
    const juce::String yaml = R"yaml(
id: routing
name: Routing
mics:
  - name: Tom 1
    instrument: tom1
    code: dtm
  - name: Drum Shared
    instrument: drum
    code: dts
  - name: Other Shared
    instrument: other
    code: dss
  - name: Overheads
    code: ons
)yaml";

    auto parsedLayout = LayoutTraits<MicLayout>::parseYaml(yaml);
    REQUIRE(parsedLayout.ok());

    auto dtm = MicSpec::parseMicCode("dtm").value;
    auto dsm = MicSpec::parseMicCode("dsm").value;
    auto dom = MicSpec::parseMicCode("dom").value;
    auto ons = MicSpec::parseMicCode("ons").value;

    std::vector<MicRoutingPlan::SlotRequest> requests;
    requests.push_back({ "tom1", InstrumentSpec::Type::Tom, DirectMicOutPolicy::Own, { dtm } });
    requests.push_back({ "octo1", InstrumentSpec::Type::Octoban, DirectMicOutPolicy::Auto, { dtm } });
    requests.push_back({ "cow1", InstrumentSpec::Type::Cowbell, DirectMicOutPolicy::Shared, { dsm } });
    requests.push_back({ "ride1", InstrumentSpec::Type::Ride, DirectMicOutPolicy::Shared, { dom } });

    const auto catalog = InstrumentCatalog::makeDefault();
    const auto plan = MicRoutingPlan::build(parsedLayout.value, requests, catalog);

    const auto tomDirect = plan.resolve("tom1", InstrumentSpec::Type::Tom, dtm);
    REQUIRE(tomDirect.matched());
    REQUIRE(tomDirect.globalStart == 0);
    REQUIRE(tomDirect.channels == 1);
    REQUIRE(tomDirect.outputName == "Tom 1");
    REQUIRE_FALSE(tomDirect.applyBalance);

    const auto octoDirect = plan.resolve("octo1", InstrumentSpec::Type::Octoban, dtm);
    REQUIRE(octoDirect.matched());
    REQUIRE(octoDirect.globalStart == 1);
    REQUIRE(octoDirect.channels == 2);
    REQUIRE(octoDirect.outputName == "Drum Shared");
    REQUIRE(octoDirect.applyBalance);

    const auto cowDirect = plan.resolve("cow1", InstrumentSpec::Type::Cowbell, dsm);
    REQUIRE(cowDirect.matched());
    REQUIRE(cowDirect.globalStart == 3);
    REQUIRE(cowDirect.channels == 2);
    REQUIRE(cowDirect.outputName == "Other Shared");
    REQUIRE(cowDirect.applyBalance);

    const auto rideDirect = plan.resolve("ride1", InstrumentSpec::Type::Ride, dom);
    REQUIRE_FALSE(rideDirect.matched());
    REQUIRE(plan.getWarnings().size() == 1);
    CHECK_THAT(plan.getWarnings()[0].toStdString(), Catch::Matchers::ContainsSubstring("ride1"));

    const auto overhead = plan.resolve("cow1", InstrumentSpec::Type::Cowbell, ons);
    REQUIRE(overhead.matched());
    REQUIRE(overhead.globalStart == 5);
    REQUIRE(overhead.channels == 2);
    REQUIRE(overhead.outputName == "Overheads");
    REQUIRE_FALSE(overhead.applyBalance);
    REQUIRE(plan.getAssignedHostChannelCount() == 7);
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
    auto rimDir = root.getChildFile("rim");
    REQUIRE(centerLeft.createDirectory());
    REQUIRE(centerRight.createDirectory());
    REQUIRE(rimDir.createDirectory());

    DrumKitSpec::MetaInfo kitMeta;
    DrumKitSpec kit(kitMeta);

    InstrumentSpec::MetaInfo instMeta;
    instMeta.rootDir = root;
    instMeta.name = "hihat";
    instMeta.codeRaw = "hihat-14";
    instMeta.type = InstrumentSpec::Type::HiHat;
    instMeta.balance = -12;
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
    REQUIRE(bow->getEffectiveBalance().has_value());
    REQUIRE(*bow->getEffectiveBalance() == -64);
    REQUIRE(instrument.findArticulationById("bow[half]-pdl_opn_cls043") == bow);
    REQUIRE(instrument.getPreferredArticulationId(*bow) == "bow[half]");

    auto* rim = instrument.findArticulation("rim");
    REQUIRE(rim != nullptr);
    REQUIRE(!rim->balance.has_value());
    REQUIRE(rim->getEffectiveBalance().has_value());
    REQUIRE(*rim->getEffectiveBalance() == -12);

    auto* center = instrument.findArticulation("center");
    REQUIRE(center != nullptr);
    REQUIRE(!center->balance.has_value());
    REQUIRE(center->balanceConflict);
}
