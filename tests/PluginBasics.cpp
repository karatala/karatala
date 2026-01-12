#include "helpers/test_helpers.h"
#include <PluginProcessor.h>
#include <Config/YamlHelper.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

TEST_CASE("one is equal to one", "[dummy]") {
    REQUIRE(1 == 1);
}

TEST_CASE("Plugin instance", "[instance]") {
    PluginProcessor testPlugin;

    SECTION("name") {
        CHECK_THAT(testPlugin.getName().toStdString(),
            Catch::Matchers::Equals("Karatala Drum Kit Engine"));
    }
}

#include "../source/Config/YamlHelper.h"
TEST_CASE("YamlHelper - default kit parse", "[yaml]") {
    const juce::String yaml = R"yaml(
id: default
name: default
description: default layout
vendor: Haribol
version: 1.0
instruments:
  - id: kick1
    display: Kick
    type: kick
    articulations:
      - type: center
        notes: [35, 36]
  - id: snare1
    display: Snare
    type: snare
    articulations:
      - type: center
        notes: [38]
      - type: rim
        notes: [40]
      - type: sidestick
        notes: [37]
    controllers:
      - id: 16
        type: position
        name: Snare Position
        description: Adjust snare hit position from center to rim
)yaml";

    auto root = YamlHelper::parse(yaml);

    REQUIRE(root.isMap());
    REQUIRE(root["instruments"].isSequence());
    REQUIRE(root["instruments"].size() == 2);

    const auto& first = root["instruments"][0];
    REQUIRE(first["id"].asString() == "kick1");
    //FIXME: also first.mapValue shows in debugger size = 6 (with keys '- type', 'articulations', 'display', 'id', 'notes', 'type') - where '- type' and 'articulations' are clearly because of bug in parser
    //FIXME: another problem is this is imposible to debug, when adding a watch first["id"] it says no operator "[]" matches these operands
    
    REQUIRE(first["articulations"].isSequence());
    REQUIRE(first["articulations"].size() == 1);
    REQUIRE(first["articulations"][0]["type"].asString() == "center");
    REQUIRE(first["articulations"][0]["notes"][0].asString() == "35");
    REQUIRE(first["articulations"][0]["notes"][1].asString() == "36");

    const auto& second = root["instruments"][1];
    REQUIRE(second["id"].asString() == "snare1");
    REQUIRE(second["controllers"].isSequence());
    REQUIRE(second["controllers"].size() == 1);
    const auto& ctrl = second["controllers"][0];
    REQUIRE(ctrl["id"].asString() == "16");
    REQUIRE(ctrl["type"].asString() == "position");
    REQUIRE(ctrl["name"].asString() == "Snare Position");
    REQUIRE(ctrl["description"].asString() == "Adjust snare hit position from center to rim");
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


#ifdef PAMPLEJUCE_IPP
    #include <ipp.h>

TEST_CASE("IPP version", "[ipp]") {
    #if defined(__APPLE__)
    // macOS uses 2021.9.1 from pip wheel (only x86_64 version available)
    CHECK_THAT(ippsGetLibVersion()->Version, Catch::Matchers::Equals("2021.9.1 (r0x7e208212)"));
    #else
    CHECK_THAT(ippsGetLibVersion()->Version, Catch::Matchers::Equals("2022.3.0 (r0x0fc08bb1)"));
    #endif
}
#endif
