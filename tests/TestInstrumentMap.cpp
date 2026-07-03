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

TEST_CASE("InstrumentMap serializer preserves parsed assignments and parameters", "[InstrumentMap]") {
    const juce::String original =
        "snare1/rim:MixedKit/snare-14/sidestick:tuning=-25,attack=12,decay=450,vol=-3.5,flipLR=1,flipPhase=1,bleed=oh:0.25,room:0.5";

    auto parsed = DrumKitEngine::parseInstrumentMapText(original);
    REQUIRE(parsed.ok());

    const auto serialized = DrumKitEngine::serializeInstrumentMap(parsed.value);
    auto reparsed = DrumKitEngine::parseInstrumentMapText(serialized);
    REQUIRE(reparsed.ok());
    REQUIRE(reparsed.value.entries.size() == 1);

    const auto& entry = reparsed.value.entries.front();
    CHECK(entry.slotId == "snare1");
    REQUIRE(entry.slotArticulationFilter.size() == 1);
    CHECK(entry.slotArticulationFilter.front() == "rim");
    CHECK(entry.kitRef == "MixedKit");
    CHECK(entry.instCode == "snare-14");
    REQUIRE(entry.articulationFilter.size() == 1);
    CHECK(entry.articulationFilter.front() == "sidestick");
    CHECK(entry.tuningCents == -25);
    CHECK(entry.attackTimeMs == 12);
    CHECK(entry.decayTimeMs == 450);
    CHECK(entry.volumeDb == -3.5f);
    CHECK(entry.flipLR);
    CHECK(entry.flipPhase);
    CHECK(entry.micBleedLevels.at("oh") == 0.25f);
    CHECK(entry.micBleedLevels.at("room") == 0.5f);
}

TEST_CASE("InstrumentMap serializer omits entries without slot or kit", "[InstrumentMap]") {
    DrumKitEngine::InstrumentMap map;
    map.entries.push_back({});

    DrumKitEngine::InstrumentMapEntry valid;
    valid.slotId = "kick1";
    valid.kitRef = "FactoryKit";
    valid.instCode = "kick-22";
    map.entries.push_back(valid);

    CHECK(DrumKitEngine::serializeInstrumentMap(map) == "kick1:FactoryKit/kick-22");
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

TEST_CASE("InstrumentMap parser preserves explicit false flipLR", "[InstrumentMap][orientation]") {
    const juce::String text = "ride1:MyKit/ride-20:flipLR=0";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.entries.size() == 1);
    CHECK_FALSE(parsed.value.entries.front().flipLR);
    CHECK(parsed.value.entries.front().flipLRExplicit);
    CHECK(DrumKitEngine::serializeInstrumentMap(parsed.value) == text);
}

TEST_CASE("InstrumentMap builder infers flipLR from opposite slot and source balance", "[InstrumentMap][orientation]") {
    KitLayout layout;

    KitLayout::InstrumentSlot leftSlot;
    leftSlot.id = "ride1";
    leftSlot.type = KitLib::InstrumentSpec::Type::Ride;
    leftSlot.balance = -64;
    layout.instrumentSlots.push_back(leftSlot);

    KitLayout::InstrumentSlot rightSlot;
    rightSlot.id = "ride2";
    rightSlot.type = KitLib::InstrumentSpec::Type::Ride;
    rightSlot.balance = 64;
    layout.instrumentSlots.push_back(rightSlot);

    KitLayout::InstrumentSlot mixedSlot;
    mixedSlot.id = "ride3";
    mixedSlot.type = KitLib::InstrumentSpec::Type::Ride;
    mixedSlot.balance = -64;
    layout.instrumentSlots.push_back(mixedSlot);

    KitLib::DrumKitSpec kit { { {}, "KitA", "Kit A", {}, {} } };

    KitLib::InstrumentSpec::MetaInfo firstMeta;
    firstMeta.codeRaw = "ride-a";
    firstMeta.type = KitLib::InstrumentSpec::Type::Ride;
    kit.addInstrument(firstMeta);
    auto* first = kit.findInstrument("ride-a");
    REQUIRE(first != nullptr);
    auto* firstArt = first->getOrAddArticulation("bow");
    firstArt->balance = 64;

    KitLib::InstrumentSpec::MetaInfo secondMeta;
    secondMeta.codeRaw = "ride-b";
    secondMeta.type = KitLib::InstrumentSpec::Type::Ride;
    kit.addInstrument(secondMeta);
    auto* second = kit.findInstrument("ride-b");
    REQUIRE(second != nullptr);
    auto* secondArt = second->getOrAddArticulation("bow");
    secondArt->balance = 64;

    KitLib::InstrumentSpec::MetaInfo thirdMeta;
    thirdMeta.codeRaw = "ride-c";
    thirdMeta.type = KitLib::InstrumentSpec::Type::Ride;
    kit.addInstrument(thirdMeta);
    auto* third = kit.findInstrument("ride-c");
    REQUIRE(third != nullptr);
    auto* thirdBow = third->getOrAddArticulation("bow");
    thirdBow->balance = 64;
    auto* thirdEdge = third->getOrAddArticulation("edge");
    thirdEdge->balance = -64;

    DrumKitEngine engine;
    const auto mapText = engine.buildInstrumentMapForKit(layout, kit);
    const auto parsed = DrumKitEngine::parseInstrumentMapText(mapText);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.entries.size() == 3);
    CHECK(parsed.value.entries[0].slotId == "ride1");
    CHECK(parsed.value.entries[0].instCode == "ride-a");
    CHECK(parsed.value.entries[0].flipLR);
    CHECK(parsed.value.entries[0].flipLRExplicit);
    CHECK(parsed.value.entries[1].slotId == "ride2");
    CHECK(parsed.value.entries[1].instCode == "ride-b");
    CHECK_FALSE(parsed.value.entries[1].flipLR);
    CHECK_FALSE(parsed.value.entries[1].flipLRExplicit);
    CHECK(parsed.value.entries[2].slotId == "ride3");
    CHECK(parsed.value.entries[2].instCode == "ride-c");
    CHECK_FALSE(parsed.value.entries[2].flipLR);
    CHECK_FALSE(parsed.value.entries[2].flipLRExplicit);
}
