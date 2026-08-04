#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <vector>

#include "../source/Engine/DrumKitEngine.h"
#include "../source/Engine/InstrumentMap/InstrumentMapOperations.h"

TEST_CASE("InstrumentMap parser rejects empty input", "[InstrumentMap]") {
    const auto parsed = DrumKitEngine::parseInstrumentMapText("");

    REQUIRE_FALSE(parsed.ok());
    CHECK_THAT(parsed.error().toStdString(), Catch::Matchers::ContainsSubstring("No valid instrument map layers found"));
}

TEST_CASE("InstrumentMap parser requires one complete library articulation path per map layer", "[InstrumentMap]") {
    const std::vector<juce::String> invalidMapLines {
        "kick1/center:MyKit",
        "kick1/center:MyKit/kick-24",
        "kick1/center:MyKit/kick-24/",
        "kick1/center:MyKit/kick-24/center,edge",
        "kick1/center:MyKit/kick-24/center/extra",
    };

    for (const auto& invalidMapLine : invalidMapLines) {
        const auto parsed = DrumKitEngine::parseInstrumentMapText(invalidMapLine);
        CHECK_FALSE(parsed.ok());
    }
}

TEST_CASE("InstrumentMap parser requires one articulation slot per map layer", "[InstrumentMap]") {
    const std::vector<juce::String> invalidMapLines {
        "kick1:MyKit/kick-24/center",
        "kick1/:MyKit/kick-24/center",
        "kick1/center,rim:MyKit/kick-24/center",
        "kick1/center/extra:MyKit/kick-24/center",
    };

    for (const auto& invalidMapLine : invalidMapLines) {
        const auto parsed = DrumKitEngine::parseInstrumentMapText(invalidMapLine);
        CHECK_FALSE(parsed.ok());
    }
}

TEST_CASE("InstrumentMap parser skips comments and parses basic map layers", "[InstrumentMap]") {
    const juce::String text = R"MAP(
# comment
kick1/center:MyKit/kick-24/center
snare1/center:MyKit/snare-14/center
)MAP";

    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 2);
    CHECK(parsed.value.layers[0].slotId == "kick1");
    CHECK(parsed.value.layers[0].kitRef == "MyKit");
    CHECK(parsed.value.layers[0].instCode == "kick-24");
    REQUIRE(parsed.value.layers[0].slotArticulationFilter.size() == 1);
    CHECK(parsed.value.layers[0].slotArticulationFilter[0] == "center");
    CHECK(parsed.value.layers[1].slotId == "snare1");
}

TEST_CASE("InstrumentMap parser reads params and articulation filters", "[InstrumentMap]") {
    const juce::String text = "ride1/bow:KitA/ride-20/bow:tuning=-2;attack=50;decay=300";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 1);

    const auto& mapLayer = parsed.value.layers.front();
    CHECK(mapLayer.slotId == "ride1");
    REQUIRE(mapLayer.slotArticulationFilter.size() == 1);
    CHECK(mapLayer.slotArticulationFilter[0] == "bow");
    REQUIRE(mapLayer.articulationFilter.size() == 1);
    CHECK(mapLayer.articulationFilter[0] == "bow");
    REQUIRE(mapLayer.tuningCents.has_value());
    CHECK(*mapLayer.tuningCents == -2);
    REQUIRE(mapLayer.attackTimeMs.has_value());
    CHECK(*mapLayer.attackTimeMs == 50);
    REQUIRE(mapLayer.decayTimeMs.has_value());
    CHECK(*mapLayer.decayTimeMs == 300);
}

TEST_CASE("InstrumentMap parser reads gain, flip and bleed params", "[InstrumentMap]") {
    const juce::String text = "kick1/center:MyKit/kick-24/center:vol=-6.5;flipLR=1;flipPhase=true;bleed=dtm:0.75,dom:0.25";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 1);

    const auto& mapLayer = parsed.value.layers.front();
    REQUIRE(mapLayer.volumeDb.has_value());
    CHECK_THAT(*mapLayer.volumeDb, Catch::Matchers::WithinAbs(-6.5f, 0.0001f));
    CHECK(mapLayer.flipLR);
    CHECK(mapLayer.flipPhase);
    REQUIRE(mapLayer.micBleedLevels.size() == 2);
    REQUIRE(mapLayer.micBleedLevels.find("dtm") != mapLayer.micBleedLevels.end());
    REQUIRE(mapLayer.micBleedLevels.find("dom") != mapLayer.micBleedLevels.end());
    CHECK_THAT(mapLayer.micBleedLevels.at("dtm"), Catch::Matchers::WithinAbs(0.75f, 0.0001f));
    CHECK_THAT(mapLayer.micBleedLevels.at("dom"), Catch::Matchers::WithinAbs(0.25f, 0.0001f));
}


TEST_CASE("InstrumentMap parser does not split top-level params on comma", "[InstrumentMap]") {
    const juce::String text = "kick1/center:MyKit/kick-24/center:bleed=dtm:0.5,room:0.8,tuning=-20";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 1);
    const auto& mapLayer = parsed.value.layers.front();
    CHECK_FALSE(mapLayer.tuningCents.has_value());
    REQUIRE(mapLayer.micBleedLevels.size() == 2);
    CHECK_THAT(mapLayer.micBleedLevels.at("dtm"), Catch::Matchers::WithinAbs(0.5f, 0.0001f));
    CHECK_THAT(mapLayer.micBleedLevels.at("room"), Catch::Matchers::WithinAbs(0.8f, 0.0001f));
}

TEST_CASE("InstrumentMap parser reads mic route overrides", "[InstrumentMap]") {
    const juce::String text = "kick1/center:MyKit/kick-24/center:micRoute=dtm:room/rfs,dsm:overhead/ons";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 1);
    const auto& mapLayer = parsed.value.layers.front();
    REQUIRE(mapLayer.micRouteOverrides.size() == 2);
    CHECK(mapLayer.micRouteOverrides.at("dtm") == "room/rfs");
    CHECK(mapLayer.micRouteOverrides.at("dsm") == "overhead/ons");
}

TEST_CASE("InstrumentMap parser preserves wildcard aux output ids", "[InstrumentMap][routing]") {
    const juce::String text = "snare1/center:MyKit/snare-14/center:micRoute=dtm1:aux1/**m,oni:aux1/**i";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 1);
    CHECK(parsed.value.layers.front().micRouteOverrides.at("dtm1") == "aux1/**m");
    CHECK(parsed.value.layers.front().micRouteOverrides.at("oni") == "aux1/**i");
    CHECK(DrumKitEngine::serializeInstrumentMap(parsed.value) == text);
}

TEST_CASE("InstrumentMap serializer preserves mic route overrides in stable order", "[InstrumentMap]") {
    DrumKitEngine::InstrumentMap map;
    DrumKitEngine::InstrumentMapLayer mapLayer;
    mapLayer.slotId = "kick1";
    mapLayer.slotArticulationFilter = { "center" };
    mapLayer.kitRef = "MyKit";
    mapLayer.instCode = "kick-24";
    mapLayer.articulationFilter = { "center" };
    mapLayer.micRouteOverrides["dtm"] = "room/rfs";
    mapLayer.micRouteOverrides["dsm"] = "overhead/ons";
    map.layers.push_back(mapLayer);

    CHECK(DrumKitEngine::serializeInstrumentMap(map) == "kick1/center:MyKit/kick-24/center:micRoute=dsm:overhead/ons,dtm:room/rfs");
}

TEST_CASE("InstrumentMap fingerprints include mic route overrides", "[InstrumentMap]") {
    DrumKitEngine::InstrumentMapLayer first;
    first.slotId = "kick1";
    first.slotArticulationFilter = { "center" };
    first.kitRef = "MyKit";
    first.instCode = "kick-24";
    first.articulationFilter = { "center" };

    DrumKitEngine::InstrumentMapLayer second = first;
    second.micRouteOverrides["dtm"] = "room/rfs";

    CHECK(InstrumentMap::makeLayerFingerprint(first) != InstrumentMap::makeLayerFingerprint(second));
    CHECK(InstrumentMap::makeLayerReloadFingerprint(first) != InstrumentMap::makeLayerReloadFingerprint(second));
}

TEST_CASE("InstrumentMap reload fingerprint changes when a shared map layer becomes a parameter layer", "[InstrumentMap]") {
    DrumKitEngine::InstrumentMapLayer sharedMapLayer;
    sharedMapLayer.slotId = "ride1";
    sharedMapLayer.kitRef = "MyKit";
    sharedMapLayer.instCode = "ride-20";
    sharedMapLayer.slotArticulationFilter = { "bow" };
    sharedMapLayer.articulationFilter = { "bow" };

    auto parameterLayer = sharedMapLayer;
    parameterLayer.volumeDb = -3.0f;

    CHECK(InstrumentMap::makeLayerReloadFingerprint(sharedMapLayer) != InstrumentMap::makeLayerReloadFingerprint(parameterLayer));
}

TEST_CASE("InstrumentMap slot replacement removes every old layer and preserves other slots", "[InstrumentMap][assignment]") {
    DrumKitEngine::InstrumentMap map;

    DrumKitEngine::InstrumentMapLayer kickCenter;
    kickCenter.slotId = "kick1";
    kickCenter.slotArticulationFilter = { "center" };
    kickCenter.kitRef = "KitA";
    kickCenter.instCode = "kick-22";
    kickCenter.articulationFilter = { "center" };
    map.layers.push_back(kickCenter);

    auto kickEdge = kickCenter;
    kickEdge.slotArticulationFilter = { "edge" };
    kickEdge.articulationFilter = { "edge" };
    kickEdge.volumeDb = -3.0f;
    map.layers.push_back(kickEdge);

    DrumKitEngine::InstrumentMapLayer snare;
    snare.slotId = "snare1";
    snare.slotArticulationFilter = { "center" };
    snare.kitRef = "KitA";
    snare.instCode = "snare-14";
    snare.articulationFilter = { "center" };
    snare.tuningCents = 20;
    map.layers.push_back(snare);

    DrumKitEngine::InstrumentMapLayer replacement;
    replacement.slotId = "kick1";
    replacement.slotArticulationFilter = { "center" };
    replacement.kitRef = "KitB";
    replacement.instCode = "kick-24";
    replacement.articulationFilter = { "center" };
    InstrumentMap::replaceSlotLayers(map, "kick1", { replacement });

    REQUIRE(map.layers.size() == 2);
    CHECK(map.layers[0].slotId == "snare1");
    CHECK(map.layers[0].tuningCents == 20);
    CHECK(map.layers[1].slotId == "kick1");
    CHECK(map.layers[1].kitRef == "KitB");
    CHECK(map.layers[1].instCode == "kick-24");
    CHECK(map.layers[1].slotArticulationFilter == std::vector<juce::String> { "center" });
    CHECK(map.layers[1].articulationFilter == std::vector<juce::String> { "center" });
}

TEST_CASE("InstrumentMap projections match map-layer numbers inside each articulation slot", "[InstrumentMap][linked-edit]") {
    KitLayout layout;
    KitLayout::InstrumentSlot slot;
    slot.id = "snare1";
    slot.type = KitLib::InstrumentSpec::Type::Snare;
    slot.articulations.emplace_back("center");
    slot.articulations.emplace_back("rim");
    layout.instrumentSlots.push_back(slot);

    DrumKitEngine::InstrumentMap map;
    DrumKitEngine::InstrumentMapLayer centerLayer;
    centerLayer.slotId = "snare1";
    centerLayer.kitRef = "KitA";
    centerLayer.instCode = "snare-14";
    centerLayer.slotArticulationFilter = { "center" };
    centerLayer.articulationFilter = { "center" };
    map.layers.push_back(centerLayer);

    auto centerParameterLayer = centerLayer;
    centerParameterLayer.articulationFilter = { "edge" };
    map.layers.push_back(centerParameterLayer);

    auto rimLayer = centerLayer;
    rimLayer.slotArticulationFilter = { "rim" };
    rimLayer.articulationFilter = { "rim" };
    map.layers.push_back(rimLayer);

    auto rimParameterLayer = rimLayer;
    rimParameterLayer.articulationFilter = { "rimshot" };
    map.layers.push_back(rimParameterLayer);

    const auto projections = InstrumentMap::projectLayers(map, layout);
    REQUIRE(projections.size() == 4);
    CHECK(projections[0].articulationSlotCode == "center");
    CHECK(projections[0].mapLayerIndex == 0);
    CHECK(projections[0].articulationSlotLayerIndex == 0);
    CHECK(projections[1].articulationSlotCode == "center");
    CHECK(projections[1].mapLayerIndex == 1);
    CHECK(projections[1].articulationSlotLayerIndex == 1);
    CHECK(projections[2].articulationSlotCode == "rim");
    CHECK(projections[2].mapLayerIndex == 2);
    CHECK(projections[2].articulationSlotLayerIndex == 0);
    CHECK(projections[3].articulationSlotCode == "rim");
    CHECK(projections[3].mapLayerIndex == 3);
    CHECK(projections[3].articulationSlotLayerIndex == 1);
}

TEST_CASE("InstrumentMap projections use the explicit articulation-slot filter", "[InstrumentMap][linked-edit]") {
    KitLayout layout;
    KitLayout::InstrumentSlot slot;
    slot.id = "snare1";
    slot.articulations.emplace_back("center");
    slot.articulations.emplace_back("rim");
    layout.instrumentSlots.push_back(slot);

    DrumKitEngine::InstrumentMap map;
    DrumKitEngine::InstrumentMapLayer centerOnly;
    centerOnly.slotId = "snare1";
    centerOnly.slotArticulationFilter = { "center" };
    centerOnly.kitRef = "KitA";
    centerOnly.instCode = "snare-14";
    centerOnly.articulationFilter = { "center" };
    map.layers.push_back(centerOnly);

    const auto projections = InstrumentMap::projectLayers(map, layout);
    REQUIRE(projections.size() == 1);
    CHECK(projections[0].articulationSlotCode == "center");
    CHECK(InstrumentMap::libraryArticulationForSlot(map.layers[0], "center") == "center");
}

TEST_CASE("InstrumentMap linked mic edits resolve the library articulation path", "[InstrumentMap][linked-edit][routing]") {
    DrumKitEngine::InstrumentMapLayer mapLayer;
    mapLayer.slotId = "snare1";
    mapLayer.slotArticulationFilter = { "rim" };
    mapLayer.kitRef = "KitA";
    mapLayer.instCode = "snare-14";
    mapLayer.articulationFilter = { "rim" };
    CHECK(InstrumentMap::qualifiedLibraryArticulationIdForSlot(mapLayer, "rim") == "KitA/snare-14/rim");

    mapLayer.slotArticulationFilter = { "rim" };
    mapLayer.articulationFilter = { "rimshot" };
    CHECK(InstrumentMap::qualifiedLibraryArticulationIdForSlot(mapLayer, "rim") == "KitA/snare-14/rimshot");
}

TEST_CASE("InstrumentMap isolates a base articulation-slot filter before an unlinked articulation edit", "[InstrumentMap][linked-edit]") {
    KitLayout::InstrumentSlot slot;
    slot.id = "snare1";
    slot.type = KitLib::InstrumentSpec::Type::Snare;
    slot.articulations.emplace_back("bow[half]");
    slot.articulations.emplace_back("bow[open]");

    DrumKitEngine::InstrumentMap map;
    DrumKitEngine::InstrumentMapLayer mapLayer;
    mapLayer.slotId = "snare1";
    mapLayer.slotArticulationFilter = { "bow" };
    mapLayer.kitRef = "KitA";
    mapLayer.instCode = "snare-14";
    mapLayer.articulationFilter = { "bow" };
    mapLayer.volumeDb = -2.0f;
    map.layers.push_back(mapLayer);

    const auto halfLayerIndex = InstrumentMap::isolateLayerForArticulationSlot(map, slot, 0, "bow[half]");

    REQUIRE(map.layers.size() == 2);
    CHECK(halfLayerIndex == 0);
    CHECK(map.layers[0].slotArticulationFilter == std::vector<juce::String> { "bow[half]" });
    CHECK(map.layers[1].slotArticulationFilter == std::vector<juce::String> { "bow[open]" });
    CHECK(map.layers[0].articulationFilter == std::vector<juce::String> { "bow" });
    CHECK(map.layers[1].articulationFilter == std::vector<juce::String> { "bow" });
    CHECK(map.layers[0].volumeDb == -2.0f);
    CHECK(map.layers[1].volumeDb == -2.0f);
}

TEST_CASE("InstrumentMap linked targets use same articulation layer and deduplicate both link modes", "[InstrumentMap][linked-edit]") {
    using Projection = InstrumentMap::LayerProjection;
    const std::vector<Projection> projections {
        { 0, "tom1", "center", 0, KitLib::InstrumentSpec::Type::Tom },
        { 1, "tom1", "center", 1, KitLib::InstrumentSpec::Type::Tom },
        { 0, "tom1", "rim", 0, KitLib::InstrumentSpec::Type::Tom },
        { 2, "tom2", "center", 0, KitLib::InstrumentSpec::Type::Tom },
        { 3, "snare1", "center", 0, KitLib::InstrumentSpec::Type::Snare },
    };

    const auto selectedOnly = InstrumentMap::collectLinkedMapLayerTargets(projections, "tom1", "center", 0, false, false);
    REQUIRE(selectedOnly.size() == 1);
    CHECK(selectedOnly[0].slotId == "tom1");
    CHECK(selectedOnly[0].articulationSlotCode == "center");

    const auto linkedArticulations = InstrumentMap::collectLinkedMapLayerTargets(projections, "tom1", "center", 0, true, false);
    REQUIRE(linkedArticulations.size() == 2);
    CHECK(std::none_of(linkedArticulations.begin(), linkedArticulations.end(), [](const auto& projection) { return projection.articulationSlotLayerIndex == 1; }));

    const auto linkedTomBleed = InstrumentMap::collectLinkedMapLayerTargets(projections, "tom1", "center", 0, false, true);
    REQUIRE(linkedTomBleed.size() == 4);
    CHECK(std::none_of(linkedTomBleed.begin(), linkedTomBleed.end(), [](const auto& projection) { return projection.slotType != KitLib::InstrumentSpec::Type::Tom; }));

    const auto bothLinks = InstrumentMap::collectLinkedMapLayerTargets(projections, "tom1", "center", 0, true, true);
    CHECK(bothLinks.size() == linkedTomBleed.size());
}

TEST_CASE("MicRoutingPlan resolves explicit mic route override by output id", "[InstrumentMap][routing]") {
    MicLayout micLayout;
    MicLayout::Mic directMic;
    directMic.id = "kick1/dtm";
    directMic.name = "Direct Top";
    directMic.code = "dtm";
    directMic.specs = KitLib::MicSpec::parseMicCode("dtm").value;
    micLayout.mics.push_back(directMic);

    MicLayout::Mic roomMic;
    roomMic.id = "room/rfs";
    roomMic.name = "Room";
    roomMic.code = "rfs";
    roomMic.specs = KitLib::MicSpec::parseMicCode("rfs").value;
    micLayout.mics.push_back(roomMic);

    MicRoutingPlan::SlotRequest request;
    request.slotId = "kick1";
    request.slotType = KitLib::InstrumentSpec::Type::Kick;
    request.directPolicy = DirectMicOutPolicy::Shared;
    request.directMicSpecs.push_back(directMic.specs);

    const auto plan = MicRoutingPlan::build(micLayout, { request }, InstrumentCatalog::makeDefault());
    const auto assignment = plan.resolveOverride("kick1", directMic.specs, "room/rfs");

    REQUIRE(assignment.matched());
    CHECK(assignment.globalStart == 1);
    CHECK(assignment.channels == 2);
    CHECK(assignment.micCode == "dtm");
    CHECK(assignment.outputId == "room/rfs");
    CHECK(assignment.applyBalance);

    MicLayout reorderedMicLayout;
    reorderedMicLayout.mics = { roomMic, directMic };
    const auto reorderedPlan = MicRoutingPlan::build(reorderedMicLayout, { request }, InstrumentCatalog::makeDefault());
    const auto reorderedAssignment = reorderedPlan.resolveOverride("kick1", directMic.specs, "room/rfs");
    REQUIRE(reorderedAssignment.matched());
    CHECK(reorderedAssignment.globalStart == 0);
    CHECK(reorderedAssignment.outputId == "room/rfs");

    const auto invalid = plan.resolveOverride("kick1", directMic.specs, "bad");
    CHECK_FALSE(invalid.matched());
    CHECK_THAT(invalid.warning.toStdString(), Catch::Matchers::ContainsSubstring("output 'bad' was not found"));
}

TEST_CASE("InstrumentMap parser supports an explicit library articulation path", "[InstrumentMap]") {
    const juce::String text = "ride1/edge:KitB/crash-18/bell:tuning=120";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 1);

    const auto& mapLayer = parsed.value.layers.front();
    REQUIRE(mapLayer.slotArticulationFilter.size() == 1);
    CHECK(mapLayer.slotArticulationFilter[0] == "edge");
    REQUIRE(mapLayer.articulationFilter.size() == 1);
    CHECK(mapLayer.articulationFilter[0] == "bell");
}

TEST_CASE("InstrumentMap serializer preserves parsed assignments and parameters", "[InstrumentMap]") {
    const juce::String original =
        "snare1/rim:MixedKit/snare-14/sidestick:tuning=-25;attack=12;decay=450;vol=-3.5;velocityResponse=1:-48,64:-12,127:0;flipLR=1;flipPhase=1;bleed=oh:0.25,room:0.5";

    auto parsed = DrumKitEngine::parseInstrumentMapText(original);
    REQUIRE(parsed.ok());

    const auto serialized = DrumKitEngine::serializeInstrumentMap(parsed.value);
    auto reparsed = DrumKitEngine::parseInstrumentMapText(serialized);
    REQUIRE(reparsed.ok());
    REQUIRE(reparsed.value.layers.size() == 1);

    const auto& mapLayer = reparsed.value.layers.front();
    CHECK(mapLayer.slotId == "snare1");
    REQUIRE(mapLayer.slotArticulationFilter.size() == 1);
    CHECK(mapLayer.slotArticulationFilter.front() == "rim");
    CHECK(mapLayer.kitRef == "MixedKit");
    CHECK(mapLayer.instCode == "snare-14");
    REQUIRE(mapLayer.articulationFilter.size() == 1);
    CHECK(mapLayer.articulationFilter.front() == "sidestick");
    CHECK(mapLayer.tuningCents == -25);
    CHECK(mapLayer.attackTimeMs == 12);
    CHECK(mapLayer.decayTimeMs == 450);
    CHECK(mapLayer.volumeDb == -3.5f);
    REQUIRE(mapLayer.velocityResponse.has_value());
    CHECK(*mapLayer.velocityResponse == "1:-48,64:-12,127:0");
    CHECK(mapLayer.flipLR);
    CHECK(mapLayer.flipPhase);
    CHECK(mapLayer.micBleedLevels.at("oh") == 0.25f);
    CHECK(mapLayer.micBleedLevels.at("room") == 0.5f);
}

TEST_CASE("InstrumentMap parser rejects an invalid layer velocity response", "[InstrumentMap][VelocityResponse]") {
    const auto parsed = DrumKitEngine::parseInstrumentMapText("snare1/center:MyKit/snare-14/center:velocityResponse=1:-12,127:-1");

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 1);
    CHECK_FALSE(parsed.value.layers.front().velocityResponse.has_value());
    CHECK_THAT(parsed.error().toStdString(), Catch::Matchers::ContainsSubstring("invalid value for 'velocityResponse'"));
}

TEST_CASE("InstrumentMap velocity response changes without changing an existing custom layer shape", "[InstrumentMap][VelocityResponse]") {
    DrumKitEngine::InstrumentMapLayer first;
    first.slotId = "snare1";
    first.slotArticulationFilter = { "center" };
    first.kitRef = "MyKit";
    first.instCode = "snare-14";
    first.articulationFilter = { "center" };
    first.velocityResponse = "1:-48,64:-12,127:0";

    auto second = first;
    second.velocityResponse = "1:-36,64:-9,127:0";

    CHECK(InstrumentMap::makeLayerFingerprint(first) != InstrumentMap::makeLayerFingerprint(second));
    CHECK(InstrumentMap::makeLayerReloadFingerprint(first) == InstrumentMap::makeLayerReloadFingerprint(second));
}

TEST_CASE("InstrumentMap serializer omits map layers without complete slot and library paths", "[InstrumentMap]") {
    DrumKitEngine::InstrumentMap map;
    map.layers.push_back({});

    DrumKitEngine::InstrumentMapLayer missingArticulationSlot;
    missingArticulationSlot.slotId = "kick1";
    missingArticulationSlot.kitRef = "FactoryKit";
    missingArticulationSlot.instCode = "kick-22";
    missingArticulationSlot.articulationFilter = { "center" };
    map.layers.push_back(missingArticulationSlot);

    DrumKitEngine::InstrumentMapLayer valid;
    valid.slotId = "kick1";
    valid.slotArticulationFilter = { "center" };
    valid.kitRef = "FactoryKit";
    valid.instCode = "kick-22";
    valid.articulationFilter = { "center" };
    map.layers.push_back(valid);

    CHECK(DrumKitEngine::serializeInstrumentMap(map) == "kick1/center:FactoryKit/kick-22/center");
}

TEST_CASE("InstrumentMap parser warns on malformed params and unknown keys", "[InstrumentMap]") {
    const juce::String text = "kick1/center:MyKit/kick-24/center:unknown=1,broken";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 1);
    CHECK_FALSE(parsed.errors.empty());
    CHECK_THAT(parsed.error().toStdString(), Catch::Matchers::ContainsSubstring("unknown param 'unknown'"));
    CHECK(parsed.error().toStdString().find("malformed param 'broken'") == std::string::npos);
}

TEST_CASE("InstrumentMap parser preserves explicit false flipLR", "[InstrumentMap][orientation]") {
    const juce::String text = "ride1/bow:MyKit/ride-20/bow:flipLR=0";
    const auto parsed = DrumKitEngine::parseInstrumentMapText(text);

    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 1);
    CHECK_FALSE(parsed.value.layers.front().flipLR);
    CHECK(parsed.value.layers.front().flipLRExplicit);
    CHECK(DrumKitEngine::serializeInstrumentMap(parsed.value) == text);
}

TEST_CASE("InstrumentMap builder infers flipLR from opposite slot and library placement balance", "[InstrumentMap][orientation]") {
    KitLayout layout;

    KitLayout::InstrumentSlot leftSlot;
    leftSlot.id = "ride1";
    leftSlot.type = KitLib::InstrumentSpec::Type::Ride;
    leftSlot.balance = -64;
    leftSlot.articulations.emplace_back("bow");
    layout.instrumentSlots.push_back(leftSlot);

    KitLayout::InstrumentSlot rightSlot;
    rightSlot.id = "ride2";
    rightSlot.type = KitLib::InstrumentSpec::Type::Ride;
    rightSlot.balance = 64;
    rightSlot.articulations.emplace_back("bow");
    layout.instrumentSlots.push_back(rightSlot);

    KitLayout::InstrumentSlot mixedSlot;
    mixedSlot.id = "ride3";
    mixedSlot.type = KitLib::InstrumentSpec::Type::Ride;
    mixedSlot.balance = -64;
    mixedSlot.articulations.emplace_back("bow");
    mixedSlot.articulations.emplace_back("edge");
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
    REQUIRE(parsed.value.layers.size() == 4);
    CHECK(parsed.value.layers[0].slotId == "ride1");
    CHECK(parsed.value.layers[0].instCode == "ride-a");
    CHECK(parsed.value.layers[0].articulationFilter == std::vector<juce::String> { "bow" });
    CHECK(parsed.value.layers[0].flipLR);
    CHECK(parsed.value.layers[0].flipLRExplicit);
    CHECK(parsed.value.layers[1].slotId == "ride2");
    CHECK(parsed.value.layers[1].instCode == "ride-b");
    CHECK(parsed.value.layers[1].articulationFilter == std::vector<juce::String> { "bow" });
    CHECK_FALSE(parsed.value.layers[1].flipLR);
    CHECK_FALSE(parsed.value.layers[1].flipLRExplicit);
    CHECK(parsed.value.layers[2].slotId == "ride3");
    CHECK(parsed.value.layers[2].instCode == "ride-c");
    CHECK(parsed.value.layers[2].articulationFilter == std::vector<juce::String> { "bow" });
    CHECK(parsed.value.layers[2].flipLR);
    CHECK(parsed.value.layers[2].flipLRExplicit);
    CHECK(parsed.value.layers[3].slotId == "ride3");
    CHECK(parsed.value.layers[3].instCode == "ride-c");
    CHECK(parsed.value.layers[3].articulationFilter == std::vector<juce::String> { "edge" });
    CHECK_FALSE(parsed.value.layers[3].flipLR);
    CHECK_FALSE(parsed.value.layers[3].flipLRExplicit);
}

TEST_CASE("InstrumentMap serializer groups by slot without changing layer order", "[InstrumentMap]") {
    DrumKitEngine::InstrumentMap map;

    DrumKitEngine::InstrumentMapLayer snareRimFirst;
    snareRimFirst.slotId = "snare1";
    snareRimFirst.slotArticulationFilter = { "rim" };
    snareRimFirst.kitRef = "KitA";
    snareRimFirst.instCode = "snare-14";
    snareRimFirst.articulationFilter = { "rim" };
    snareRimFirst.tuningCents = -10;
    map.layers.push_back(snareRimFirst);

    DrumKitEngine::InstrumentMapLayer kick;
    kick.slotId = "kick1";
    kick.slotArticulationFilter = { "center" };
    kick.kitRef = "KitA";
    kick.instCode = "kick-22";
    kick.articulationFilter = { "center" };
    map.layers.push_back(kick);

    DrumKitEngine::InstrumentMapLayer snareCenter;
    snareCenter.slotId = "snare1";
    snareCenter.slotArticulationFilter = { "center" };
    snareCenter.kitRef = "KitA";
    snareCenter.instCode = "snare-14";
    snareCenter.articulationFilter = { "center" };
    map.layers.push_back(snareCenter);

    DrumKitEngine::InstrumentMapLayer snareRimSecond;
    snareRimSecond.slotId = "snare1";
    snareRimSecond.slotArticulationFilter = { "rim" };
    snareRimSecond.kitRef = "KitB";
    snareRimSecond.instCode = "snare-14";
    snareRimSecond.articulationFilter = { "rim" };
    snareRimSecond.tuningCents = 15;
    map.layers.push_back(snareRimSecond);

    const auto serialized = DrumKitEngine::serializeInstrumentMap(map);
    const juce::String expected = "kick1/center:KitA/kick-22/center\nsnare1/center:KitA/snare-14/center\nsnare1/rim:KitA/snare-14/rim:tuning=-10\nsnare1/rim:KitB/snare-14/rim:tuning=15";
    CHECK(serialized == expected);
}

TEST_CASE("InstrumentMap builder writes preferred kit and instrument ids", "[InstrumentMap]") {
    KitLayout layout;
    KitLayout::InstrumentSlot slot;
    slot.id = "kick1";
    slot.type = KitLib::InstrumentSpec::Type::Kick;
    slot.articulations.emplace_back("center");
    layout.instrumentSlots.push_back(slot);

    KitLib::DrumKitSpec kit { { {}, "StudioKit by Sam [CC-BY-4.0]", "StudioKit", "Sam", "CC-BY-4.0" } };

    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = "kick-22-R64-Yamaha";
    meta.name = "kick";
    meta.type = KitLib::InstrumentSpec::Type::Kick;
    meta.size = "22";
    meta.balance = 64;
    meta.description = "Yamaha";
    kit.addInstrument(meta);
    auto* instrument = kit.findInstrument("kick-22-R64-Yamaha");
    REQUIRE(instrument != nullptr);
    instrument->getOrAddArticulation("center");

    DrumKitEngine engine;
    const auto mapText = engine.buildInstrumentMapForKit(layout, kit);

    CHECK(mapText == "kick1/center:StudioKit/kick-22/center");
}

TEST_CASE("InstrumentMap builder creates one map layer per library articulation and matches compatible articulation slots", "[InstrumentMap][assignment]") {
    KitLayout::InstrumentSlot slot;
    slot.id = "ride1";
    slot.articulations.emplace_back("bow");
    slot.articulations.emplace_back("edge");

    KitLib::DrumKitSpec libraryKit { { {}, "KitA", "Kit A", {}, {} } };
    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = "ride-20";
    meta.type = KitLib::InstrumentSpec::Type::Ride;
    libraryKit.addInstrument(meta);
    auto* libraryInstrument = libraryKit.findInstrument("ride-20");
    REQUIRE(libraryInstrument != nullptr);
    libraryInstrument->getOrAddArticulation("bow");
    libraryInstrument->getOrAddArticulation("bell");

    const auto mapLayers = InstrumentMap::buildLayers(slot, libraryKit, *libraryInstrument, "KitA", ArticulationSlotFallbackRules::makeDefault());

    REQUIRE(mapLayers.ok());
    REQUIRE(mapLayers.value.size() == 2);
    REQUIRE(mapLayers.errors.size() == 1);
    CHECK(mapLayers.value[0].slotArticulationFilter == std::vector<juce::String> { "bow" });
    CHECK(mapLayers.value[0].articulationFilter == std::vector<juce::String> { "bow" });
    CHECK(mapLayers.value[1].slotArticulationFilter == std::vector<juce::String> { "bow" });
    CHECK(mapLayers.value[1].articulationFilter == std::vector<juce::String> { "bell" });
    CHECK_THAT(mapLayers.error().toStdString(), Catch::Matchers::ContainsSubstring("mapped to fallback slot 'bow'"));
}

TEST_CASE("InstrumentMap builder uses the configured articulation-slot fallback order", "[InstrumentMap][assignment][fallback]") {
    KitLayout::InstrumentSlot slot;
    slot.id = "ride1";
    slot.type = KitLib::InstrumentSpec::Type::Ride;
    slot.articulations.emplace_back("bow");
    slot.articulations.emplace_back("edge");

    KitLib::DrumKitSpec libraryKit { { {}, "KitA", "Kit A", {}, {} } };
    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = "ride-20";
    meta.type = KitLib::InstrumentSpec::Type::Ride;
    libraryKit.addInstrument(meta);
    auto* libraryInstrument = libraryKit.findInstrument("ride-20");
    REQUIRE(libraryInstrument != nullptr);
    libraryInstrument->getOrAddArticulation("sidestick");

    const auto parsedRules = ArticulationSlotFallbackRules::parseYaml(YamlHelper::parse("ride: [edge, bow]"), InstrumentCatalog::makeDefault());
    REQUIRE(parsedRules.ok());
    const auto mapLayers = InstrumentMap::buildLayers(slot, libraryKit, *libraryInstrument, "KitA", parsedRules.value);

    REQUIRE(mapLayers.ok());
    REQUIRE(mapLayers.value.size() == 1);
    REQUIRE(mapLayers.errors.size() == 1);
    CHECK(mapLayers.value.front().slotArticulationFilter == std::vector<juce::String> { "edge" });
    CHECK(mapLayers.value.front().articulationFilter == std::vector<juce::String> { "sidestick" });
    CHECK_THAT(mapLayers.error().toStdString(), Catch::Matchers::ContainsSubstring("mapped to fallback slot 'edge'"));
}

TEST_CASE("InstrumentMap builder skips an unmatched articulation when fallback is disabled", "[InstrumentMap][assignment][fallback]") {
    KitLayout::InstrumentSlot slot;
    slot.id = "ride1";
    slot.type = KitLib::InstrumentSpec::Type::Ride;
    slot.articulations.emplace_back("bow");

    KitLib::DrumKitSpec libraryKit { { {}, "KitA", "Kit A", {}, {} } };
    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = "ride-20";
    meta.type = KitLib::InstrumentSpec::Type::Ride;
    libraryKit.addInstrument(meta);
    auto* libraryInstrument = libraryKit.findInstrument("ride-20");
    REQUIRE(libraryInstrument != nullptr);
    libraryInstrument->getOrAddArticulation("sidestick");

    const auto parsedRules = ArticulationSlotFallbackRules::parseYaml(YamlHelper::parse("ride: []"), InstrumentCatalog::makeDefault());
    REQUIRE(parsedRules.ok());
    const auto mapLayers = InstrumentMap::buildLayers(slot, libraryKit, *libraryInstrument, "KitA", parsedRules.value);

    REQUIRE(mapLayers.ok());
    CHECK(mapLayers.value.empty());
    REQUIRE(mapLayers.errors.size() == 1);
    CHECK_THAT(mapLayers.error().toStdString(), Catch::Matchers::ContainsSubstring("map layer skipped"));
}

TEST_CASE("InstrumentMap detects only legacy articulation-slot expression filters", "[InstrumentMap][HiHat]") {
    CHECK(InstrumentMap::usesLegacyArticulationSlotExpressionFilter({ "bow" }, "bow[open]"));
    CHECK_FALSE(InstrumentMap::usesLegacyArticulationSlotExpressionFilter({}, "bow[tight]"));
    CHECK_FALSE(InstrumentMap::usesLegacyArticulationSlotExpressionFilter({ "bow[open]" }, "bow[open]"));
    CHECK_FALSE(InstrumentMap::usesLegacyArticulationSlotExpressionFilter({ "edge" }, "bow[open]"));
    CHECK_FALSE(InstrumentMap::usesLegacyArticulationSlotExpressionFilter({ "bow" }, "bow"));
}

TEST_CASE("InstrumentMap resolves legacy hihat expressions to available library articulations", "[InstrumentMap][HiHat]") {
    KitLib::DrumKitSpec libraryKit { { {}, "KitA", "Kit A", {}, {} } };
    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = "hihat-14";
    meta.type = KitLib::InstrumentSpec::Type::HiHat;
    libraryKit.addInstrument(meta);
    auto* libInst = libraryKit.findInstrument("hihat-14");
    REQUIRE(libInst != nullptr);
    libInst->getOrAddArticulation("edge[close]");
    libInst->getOrAddArticulation("edge[open]");
    libInst->getOrAddArticulation("edge[tight]");

    CHECK(InstrumentMap::resolveLegacyHihatLibraryArticulationId(libraryKit, "KitA/hihat-14/edge[close]", "bow[open]") == "KitA/hihat-14/edge[open]");
    CHECK(InstrumentMap::resolveLegacyHihatLibraryArticulationId(libraryKit, "KitA/hihat-14/edge[close]", "bow[tight]") == "KitA/hihat-14/edge[tight]");
    CHECK(InstrumentMap::resolveLegacyHihatLibraryArticulationId(libraryKit, "KitA/hihat-14/edge[close]", "bow[half]") == "KitA/hihat-14/edge[close]");
}
