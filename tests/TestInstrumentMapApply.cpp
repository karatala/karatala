#include "../source/Engine/DrumKitEngine.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

struct DrumKitEngineTestAccess {
    static void setLibraryManager(DrumKitEngine& engine, std::shared_ptr<KitLib::LibraryManager> libraryManager) {
        engine.libraryManagerPtr = std::move(libraryManager);
    }

    static InstrumentPlayer* primaryPlayer(DrumKitEngine& engine, const juce::String& slotId) {
        const auto playerIterator = engine.instrumentPlayerById.find(slotId);
        return playerIterator != engine.instrumentPlayerById.end() ? playerIterator->second : nullptr;
    }

    static InstrumentPlayer* shadowPlayer(DrumKitEngine& engine, const juce::String& slotId, int occurrence = 0) {
        int currentOccurrence = 0;
        for (int playerIdx = engine.primaryInstrumentPlayerCount; playerIdx < static_cast<int>(engine.instrumentPlayerPtrs.size()); ++playerIdx) {
            auto* player = engine.instrumentPlayerPtrs[static_cast<size_t>(playerIdx)].get();
            if (player == nullptr || player->getId() != slotId) continue;
            if (currentOccurrence == occurrence) return player;
            ++currentOccurrence;
        }
        return nullptr;
    }
};

namespace InstrumentMapApplyTests {

class ScopedInstrumentMapLibrary {
public:
    ScopedInstrumentMapLibrary() {
        const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory);
        this->root = tempDirectory.getNonexistentChildFile("karatala-map-apply-test", juce::String(), false);
        this->root.createDirectory();
    }

    ~ScopedInstrumentMapLibrary() { this->root.deleteRecursively(); }

    void addInstrument(const juce::String& libraryKitName, const juce::String& libraryInstrumentName) {
        this->root.getChildFile(libraryKitName).getChildFile(libraryInstrumentName).getChildFile("center").createDirectory();
    }

    juce::File root;
};

static void addSlot(KitLayout& layout, const juce::String& slotId, KitLib::InstrumentSpec::Type type) {
    KitLayout::InstrumentSlot slot;
    slot.id = slotId;
    slot.type = type;
    slot.articulations.emplace_back("center");
    layout.instrumentSlots.push_back(std::move(slot));
}

class InstrumentMapApplyFixture {
public:
    InstrumentMapApplyFixture() {
        this->library.addInstrument("KitA", "kick-22");
        this->library.addInstrument("KitA", "snare-14");
        this->library.addInstrument("KitA", "tom-10");
        this->library.addInstrument("KitB", "kick-24");
        this->library.addInstrument("KitB", "snare-13");
        this->library.addInstrument("KitB", "tom-12");
        this->library.addInstrument("KitC", "kick-26");
        this->library.addInstrument("KitC", "snare-12");
        this->library.addInstrument("KitC", "tom-14");

        auto libraryManager = std::make_shared<KitLib::LibraryManager>(this->library.root);
        libraryManager->scanAll();
        DrumKitEngineTestAccess::setLibraryManager(this->engine, std::move(libraryManager));

        KitLayout layout;
        layout.id = "map-apply-test";
        addSlot(layout, "kick1", KitLib::InstrumentSpec::Type::Kick);
        addSlot(layout, "snare1", KitLib::InstrumentSpec::Type::Snare);
        addSlot(layout, "tom1", KitLib::InstrumentSpec::Type::Tom);
        this->engine.setKitLayoutDirect(std::move(layout), false);
    }

    ScopedInstrumentMapLibrary library;
    DrumKitEngine engine;
};

TEST_CASE("InstrumentMap apply keeps primary players after the seeded map is applied", "[InstrumentMap][IncrementalApply]") {
    InstrumentMapApplyFixture fixture;
    fixture.engine.mapDefaultKit();

    auto* kickBefore = DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "kick1");
    auto* snareBefore = DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "snare1");
    const auto seededMap = fixture.engine.getSuggestedInstrumentMapText();
    REQUIRE(kickBefore != nullptr);
    REQUIRE(snareBefore != nullptr);
    REQUIRE(seededMap.isNotEmpty());

    CHECK_FALSE(fixture.engine.useInstrumentMap(seededMap));
    CHECK(DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "kick1") == kickBefore);
    CHECK(DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "snare1") == snareBefore);
    CHECK(fixture.engine.getLastInstrumentMapStatus().contains("unchanged"));
}

TEST_CASE("InstrumentMap apply reloads only the changed primary slot", "[InstrumentMap][IncrementalApply]") {
    InstrumentMapApplyFixture fixture;
    const juce::String initialMap = "kick1:KitA/kick-22/center\nsnare1:KitA/snare-14/center";
    REQUIRE(fixture.engine.useInstrumentMap(initialMap));

    auto* kickBefore = DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "kick1");
    auto* snareBefore = DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "snare1");
    REQUIRE(kickBefore != nullptr);
    REQUIRE(snareBefore != nullptr);

    const juce::String changedMap = "kick1:KitB/kick-24/center\nsnare1:KitA/snare-14/center";
    REQUIRE(fixture.engine.applyInstrumentMapSlotChange(changedMap, "kick1"));
    CHECK(DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "kick1") != kickBefore);
    CHECK(DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "snare1") == snareBefore);
}

TEST_CASE("InstrumentMap whole-instrument assignment replaces one slot with one map layer per library articulation", "[InstrumentMap][assignment]") {
    InstrumentMapApplyFixture fixture;
    const juce::String currentMap = "kick1/center:KitA/kick-22/center:vol=-3\nkick1/edge:KitC/kick-26/center\nsnare1:KitA/snare-14/center:tuning=20";

    const auto updatedMap = fixture.engine.buildInstrumentMapWithSlotInstrument(currentMap, "kick1", "KitB", "kick-24");

    REQUIRE(updatedMap.ok());
    const auto parsed = DrumKitEngine::parseInstrumentMapText(updatedMap.value);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 2);
    const auto kickIterator = std::find_if(parsed.value.layers.begin(), parsed.value.layers.end(), [](const auto& mapLayer) { return mapLayer.slotId == "kick1"; });
    const auto snareIterator = std::find_if(parsed.value.layers.begin(), parsed.value.layers.end(), [](const auto& mapLayer) { return mapLayer.slotId == "snare1"; });
    REQUIRE(kickIterator != parsed.value.layers.end());
    REQUIRE(snareIterator != parsed.value.layers.end());
    CHECK(kickIterator->kitRef == "KitB");
    CHECK(kickIterator->instCode == "kick-24");
    CHECK(kickIterator->slotArticulationFilter == std::vector<juce::String> { "center" });
    CHECK(kickIterator->articulationFilter == std::vector<juce::String> { "center" });
    CHECK_FALSE(kickIterator->volumeDb.has_value());
    CHECK(snareIterator->kitRef == "KitA");
    CHECK(snareIterator->instCode == "snare-14");
    CHECK(snareIterator->tuningCents == 20);
}

TEST_CASE("InstrumentMap whole-instrument assignment allows a different instrument type", "[InstrumentMap][assignment]") {
    InstrumentMapApplyFixture fixture;

    const auto updatedMap = fixture.engine.buildInstrumentMapWithSlotInstrument("kick1:KitA/kick-22/center", "kick1", "KitB", "snare-13");

    REQUIRE(updatedMap.ok());
    const auto parsed = DrumKitEngine::parseInstrumentMapText(updatedMap.value);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.layers.size() == 1);
    CHECK(parsed.value.layers.front().slotId == "kick1");
    CHECK(parsed.value.layers.front().kitRef == "KitB");
    CHECK(parsed.value.layers.front().instCode == "snare-13");
    CHECK(parsed.value.layers.front().articulationFilter == std::vector<juce::String> { "center" });
}

TEST_CASE("InstrumentMap apply rebuilds shadows only for the changed layered slot", "[InstrumentMap][IncrementalApply]") {
    InstrumentMapApplyFixture fixture;
    const juce::String initialMap = "snare1:KitA/snare-14/center\nsnare1:KitB/snare-13/center\ntom1:KitA/tom-10/center\ntom1:KitB/tom-12/center";
    REQUIRE(fixture.engine.useInstrumentMap(initialMap));

    auto* snareShadowBefore = DrumKitEngineTestAccess::shadowPlayer(fixture.engine, "snare1");
    auto* tomShadowBefore = DrumKitEngineTestAccess::shadowPlayer(fixture.engine, "tom1");
    REQUIRE(snareShadowBefore != nullptr);
    REQUIRE(tomShadowBefore != nullptr);

    const juce::String changedMap = "snare1:KitA/snare-14/center\nsnare1:KitB/snare-13/center\ntom1:KitA/tom-10/center\ntom1:KitC/tom-14/center";
    REQUIRE(fixture.engine.applyInstrumentMapSlotChange(changedMap, "tom1"));
    CHECK(DrumKitEngineTestAccess::shadowPlayer(fixture.engine, "snare1") == snareShadowBefore);
    CHECK(DrumKitEngineTestAccess::shadowPlayer(fixture.engine, "tom1") != tomShadowBefore);
}

TEST_CASE("InstrumentMap parameter-only apply keeps the loaded primary player", "[InstrumentMap][IncrementalApply]") {
    InstrumentMapApplyFixture fixture;
    REQUIRE(fixture.engine.useInstrumentMap("kick1:KitA/kick-22/center"));

    auto* kickBefore = DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "kick1");
    REQUIRE(kickBefore != nullptr);

    REQUIRE(fixture.engine.applyInstrumentMapSlotChange("kick1:KitA/kick-22/center:vol=-3", "kick1"));
    CHECK(DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "kick1") == kickBefore);
}

TEST_CASE("InstrumentMap apply rejects incomplete library paths without replacing the loaded player", "[InstrumentMap][IncrementalApply]") {
    InstrumentMapApplyFixture fixture;
    REQUIRE(fixture.engine.useInstrumentMap("kick1:KitA/kick-22/center"));

    auto* kickBefore = DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "kick1");
    REQUIRE(kickBefore != nullptr);

    const std::vector<juce::String> incompleteMaps {
        "kick1:KitA",
        "kick1:KitA/kick-22",
        "kick1:KitA/kick-22/",
    };

    for (const auto& incompleteMap : incompleteMaps) {
        CHECK_FALSE(fixture.engine.applyInstrumentMapSlotChange(incompleteMap, "kick1"));
        CHECK(DrumKitEngineTestAccess::primaryPlayer(fixture.engine, "kick1") == kickBefore);
    }
}

} // namespace InstrumentMapApplyTests
