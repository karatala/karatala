#include "helpers/test_helpers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "../source/Config/YamlHelper.h"
#include "../source/Engine/LayoutManager.h"

TEST_CASE("YamlHelper - default kit parse", "[yaml]") {
    const juce::String yaml = R"yaml(
id: default
name: default
description: default layout
vendor: Haribol
version: 1.0
instruments: # comment
  - id: kick1
    display: Kick
    type: kick
    articulations:
      - { code: center, notes: [35, 36] }
  - id: snare1
    display: Snare
    type: snare
    articulations:
      - { notes: [38], code: center }
      - { notes: [40], code: rim } # commment
      - { notes: [37], code: sidestick }
    controllers:
      - { number: 16, code: zone-center-rim, notes: [38] }
)yaml";

    auto root = YamlHelper::parse(yaml);

    REQUIRE(root.isMap());
    REQUIRE(root["instruments"].isSequence());
    REQUIRE(root["instruments"].size() == 2);

    const auto& first = root["instruments"][0];
    REQUIRE(first["id"].asString() == "kick1");
    //FIXME: also first.mapValue shows in debugger size = 6 (with keys '- type', 'articulations', 'display', 'id', 'notes', 'type') - where '- type' and 'articulations' are clearly because of bug in parser
    //FIXME: this is imposible to debug, when adding a watch first["id"] it says no operator "[]" matches these operands
    
    REQUIRE(first["articulations"].isSequence());
    REQUIRE(first["articulations"].size() == 1);
    REQUIRE(first["articulations"][0]["code"].asString() == "center");
    REQUIRE(first["articulations"][0]["notes"][0].asString() == "35");
    REQUIRE(first["articulations"][0]["notes"][1].asString() == "36");

    const auto& second = root["instruments"][1];
    REQUIRE(second["id"].asString() == "snare1");

    REQUIRE(second["articulations"].isSequence());
    REQUIRE(second["articulations"].size() == 3);
    REQUIRE(second["articulations"][0]["code"].asString() == "center");
    REQUIRE(second["articulations"][0]["notes"][0].asString() == "38");
    REQUIRE(second["articulations"][1]["code"].asString() == "rim");
    REQUIRE(second["articulations"][1]["notes"][0].asString() == "40");
    REQUIRE(second["articulations"][2]["code"].asString() == "sidestick");
    REQUIRE(second["articulations"][2]["notes"][0].asString() == "37");

    REQUIRE(second["controllers"].isSequence());
    REQUIRE(second["controllers"].size() == 1);
    const auto& ctrl = second["controllers"][0];
    REQUIRE(ctrl["number"].asString() == "16");
    REQUIRE(ctrl["code"].asString() == "zone-center-rim");
}

TEST_CASE("YamlHelper - comments and quoted hash", "[yaml]") {
    const juce::String withComment = R"yaml(
instruments:
  - id: kick1 # primary kick
    display: Kick
)yaml";
    auto r2 = YamlHelper::parse(withComment);
    REQUIRE(r2.isMap());
    REQUIRE(r2["instruments"][0]["id"].asString() == "kick1");
    REQUIRE(r2["instruments"][0]["display"].asString() == "Kick");

    const juce::String quoted = R"yaml(
name: "A # B"
)yaml";
    auto r3 = YamlHelper::parse(quoted);
    DBG(">"+ r3["name"].asString() + "<");
    REQUIRE(r3["name"].asString() == "A # B");
}

  TEST_CASE("YamlHelper - inline map in list", "[yaml]") {
    const juce::String yaml = R"yaml(
instruments:
  - { code: bow, notes: [28,29], id: "test,ok" , more: 5 5 }
  )yaml";

    auto r = YamlHelper::parse(yaml);

    REQUIRE(r.isMap());
    REQUIRE(r["instruments"].isSequence());
    REQUIRE(r["instruments"].size() == 1);

    const auto& item = r["instruments"][0];
    REQUIRE(item.isMap());
    REQUIRE(item["code"].asString() == "bow");
    REQUIRE(item["notes"].isSequence());
    REQUIRE(item["notes"].size() == 2);
    REQUIRE(item["notes"][0].asString() == "28");
    REQUIRE(item["notes"][1].asString() == "29");
    REQUIRE(item["id"].asString() == "test,ok");
    REQUIRE(item["more"].asString() == "5 5");
  }



TEST_CASE("KitLayout - expression points parse serialize and validate", "[yaml][ExpressionPoints]") {
    const juce::String yaml = R"yaml(
id: expr-test
name: Expression Test
expressionPoints:
  pdl_opn_cls: { open: 24, half: 52, close: 78, tight: 85, pedal: 85 }
  zon_bow_edg: { bow: 20, edge: 90 }
instruments:
  - id: hihat1
    display: Hi-Hat
    expressionPoints:
      pdl_opn_cls: { open: 20, half: 50, close: 76, tight: 90, pedal: 90 }
    articulations:
      - { notes: [22], code: edge[open] }
    controllers:
      - { notes: [22], code: pdl_opn_cls, number: 4 }
)yaml";

    auto parsed = LayoutTraits<KitLayout>::parseYaml(yaml);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.expressionPoints["pdl_opn_cls"]["open"] == 24);
    REQUIRE(parsed.value.expressionPoints["zon_bow_edg"]["edge"] == 90);
    REQUIRE(parsed.value.instrumentSlots.size() == 1);
    REQUIRE(parsed.value.instrumentSlots[0].expressionPoints["pdl_opn_cls"]["tight"] == 90);

    const auto serialized = LayoutTraits<KitLayout>::toYaml(parsed.value);
    CHECK(serialized.contains("expressionPoints:"));
    CHECK(serialized.contains("pdl_opn_cls: { open: 24, half: 52, close: 78, tight: 85, pedal: 85 }"));
    CHECK(serialized.contains("pdl_opn_cls: { open: 20, half: 50, close: 76, tight: 90, pedal: 90 }"));
}

TEST_CASE("KitLayout - expression point validation warns for invalid controller data", "[yaml][ExpressionPoints]") {
    const juce::String yaml = R"yaml(
id: expr-invalid
name: Expression Invalid
expressionPoints:
  pdl_opn_cls: { open: 60, half: 52, close: 78, tight: 85, pedal: 90 }
instruments:
  - id: ride1
    display: Ride
    articulations:
      - { notes: [51], code: bow }
    controllers:
      - { code: zon_bow_edg, number: 17 }
      - { notes: [59], code: zon_bow_edg, number: 18 }
)yaml";

    auto parsed = LayoutTraits<KitLayout>::parseYaml(yaml);

    REQUIRE(parsed.ok());
    const auto warnings = parsed.error("\n");
    CHECK(warnings.contains("open < half < close < tight"));
    CHECK(warnings.contains("pedal should be <= tight"));
    CHECK(warnings.contains("notes are required"));
    CHECK(warnings.contains("is not assigned to any articulation"));
}