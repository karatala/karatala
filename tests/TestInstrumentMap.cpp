#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../source/Engine/DrumKitEngine.h"

TEST_CASE("InstrumentMap parser rejects empty input", "[InstrumentMap]") {
    const auto parsed = DrumKitEngine::parseInstrumentMapText("");

    REQUIRE_FALSE(parsed.ok());
    CHECK_THAT(parsed.error().toStdString(), Catch::Matchers::ContainsSubstring("No valid instrument map entries found"));
}

TEST_CASE("InstrumentMap parser skips comments and parses basic entries", "[InstrumentMap]") {
    const juce::String text = R"MAP(
# comment
kick1:MyKit/kick-24
snare1:MyKit/snare-14
)MAP";

    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.entries.size() == 2);
    CHECK(parsed.value.entries[0].slotId == "kick1");
    CHECK(parsed.value.entries[0].kitRef == "MyKit");
    CHECK(parsed.value.entries[0].instCode == "kick-24");
    CHECK(parsed.value.entries[0].slotArticulationFilter.empty());
    CHECK(parsed.value.entries[1].slotId == "snare1");
}

TEST_CASE("InstrumentMap parser reads params and articulation filters", "[InstrumentMap]") {
    const juce::String text = "ride1/bow:KitA/ride-20/bow:tuning=-2,attack=50,decay=300";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.entries.size() == 1);

    const auto& entry = parsed.value.entries.front();
    CHECK(entry.slotId == "ride1");
    REQUIRE(entry.slotArticulationFilter.size() == 1);
    CHECK(entry.slotArticulationFilter[0] == "bow");
    REQUIRE(entry.articulationFilter.size() == 1);
    CHECK(entry.articulationFilter[0] == "bow");
    REQUIRE(entry.tuningCents.has_value());
    CHECK(*entry.tuningCents == -2);
    REQUIRE(entry.attackTimeMs.has_value());
    CHECK(*entry.attackTimeMs == 50);
    REQUIRE(entry.decayTimeMs.has_value());
    CHECK(*entry.decayTimeMs == 300);
}

TEST_CASE("InstrumentMap parser reads gain, flip and bleed params", "[InstrumentMap]") {
    const juce::String text = "kick1:MyKit/kick-24:vol=-6.5,flipLR=1,flipPhase=true,bleed=dtm:0.75,dom:0.25";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.entries.size() == 1);

    const auto& entry = parsed.value.entries.front();
    REQUIRE(entry.volumeDb.has_value());
    CHECK_THAT(*entry.volumeDb, Catch::Matchers::WithinAbs(-6.5f, 0.0001f));
    CHECK(entry.flipLR);
    CHECK(entry.flipPhase);
    REQUIRE(entry.micBleedLevels.size() == 2);
    REQUIRE(entry.micBleedLevels.find("dtm") != entry.micBleedLevels.end());
    REQUIRE(entry.micBleedLevels.find("dom") != entry.micBleedLevels.end());
    CHECK_THAT(entry.micBleedLevels.at("dtm"), Catch::Matchers::WithinAbs(0.75f, 0.0001f));
    CHECK_THAT(entry.micBleedLevels.at("dom"), Catch::Matchers::WithinAbs(0.25f, 0.0001f));
}

TEST_CASE("InstrumentMap parser supports wildcard and art param", "[InstrumentMap]") {
    const juce::String text = "*:MyKit\nride1:KitB/ride-18:art=bow,edge";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.entries.size() == 2);
    CHECK(parsed.value.entries[0].slotId == "*");
    CHECK(parsed.value.entries[0].kitRef == "MyKit");
    CHECK(parsed.value.entries[0].instCode.isEmpty());

    const auto& entry = parsed.value.entries[1];
    REQUIRE(entry.slotArticulationFilter.size() == 2);
    CHECK(entry.slotArticulationFilter[0] == "bow");
    CHECK(entry.slotArticulationFilter[1] == "edge");
    CHECK_FALSE(parsed.errors.empty());
    CHECK_THAT(parsed.error().toStdString(), Catch::Matchers::ContainsSubstring("'art=' is deprecated"));
}

TEST_CASE("InstrumentMap parser supports explicit source articulation path", "[InstrumentMap]") {
    const juce::String text = "ride1/edge:KitB/crash-18/bell:tuning=120";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.entries.size() == 1);

    const auto& entry = parsed.value.entries.front();
    REQUIRE(entry.slotArticulationFilter.size() == 1);
    CHECK(entry.slotArticulationFilter[0] == "edge");
    REQUIRE(entry.articulationFilter.size() == 1);
    CHECK(entry.articulationFilter[0] == "bell");

    const auto parserMessages = parsed.error().toStdString();
    CHECK(parserMessages.find("'art=' is deprecated") == std::string::npos);
}

TEST_CASE("InstrumentMap parser warns on malformed params and unknown keys", "[InstrumentMap]") {
    const juce::String text = "kick1:MyKit/kick-24:unknown=1,broken";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.entries.size() == 1);
    CHECK_FALSE(parsed.errors.empty());
    CHECK_THAT(parsed.error().toStdString(), Catch::Matchers::ContainsSubstring("unknown param 'unknown'"));
    CHECK(parsed.error().toStdString().find("malformed param 'broken'") == std::string::npos);
}
