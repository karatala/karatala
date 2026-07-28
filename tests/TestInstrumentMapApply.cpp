#include "../source/Engine/DrumKitEngine.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>

struct DrumKitEngineTestAccess {
    static void setLibraryManager(DrumKitEngine& engine, std::shared_ptr<KitLib::LibraryManager> libraryManager) {
        engine.libraryManagerPtr = std::move(libraryManager);
    }

    static InstrumentSynth* primarySynth(DrumKitEngine& engine, const juce::String& slotId) {
        const auto synthIt = engine.instrumentSynthById.find(slotId);
        return synthIt != engine.instrumentSynthById.end() ? synthIt->second : nullptr;
    }

    static InstrumentSynth* shadowSynth(DrumKitEngine& engine, const juce::String& slotId, int occurrence = 0) {
        int currentOccurrence = 0;
        for (int synthIdx = engine.primaryInstrumentSynthCount; synthIdx < static_cast<int>(engine.instrumentSynthPtrs.size()); ++synthIdx) {
            auto* synth = engine.instrumentSynthPtrs[static_cast<size_t>(synthIdx)].get();
            if (synth == nullptr || synth->getId() != slotId) continue;
            if (currentOccurrence == occurrence) return synth;
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

    void addInstrument(const juce::String& kitName, const juce::String& instrumentName) {
        this->root.getChildFile(kitName).getChildFile(instrumentName).createDirectory();
    }

    juce::File root;
};

void addSlot(KitLayout& layout, const juce::String& slotId, KitLib::InstrumentSpec::Type type) {
    KitLayout::InstrumentSlot slot;
    slot.id = slotId;
    slot.type = type;
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

TEST_CASE("InstrumentMap apply keeps primary synths after the seeded map is applied", "[InstrumentMap][IncrementalApply]") {
    InstrumentMapApplyFixture fixture;
    fixture.engine.mapDefaultKit();

    auto* kickBefore = DrumKitEngineTestAccess::primarySynth(fixture.engine, "kick1");
    auto* snareBefore = DrumKitEngineTestAccess::primarySynth(fixture.engine, "snare1");
    const auto seededMap = fixture.engine.getSuggestedInstrumentMapText();
    REQUIRE(kickBefore != nullptr);
    REQUIRE(snareBefore != nullptr);
    REQUIRE(seededMap.isNotEmpty());

    CHECK_FALSE(fixture.engine.useInstrumentMap(seededMap));
    CHECK(DrumKitEngineTestAccess::primarySynth(fixture.engine, "kick1") == kickBefore);
    CHECK(DrumKitEngineTestAccess::primarySynth(fixture.engine, "snare1") == snareBefore);
    CHECK(fixture.engine.getLastInstrumentMapStatus().contains("unchanged"));
}

TEST_CASE("InstrumentMap apply reloads only the changed primary slot", "[InstrumentMap][IncrementalApply]") {
    InstrumentMapApplyFixture fixture;
    const juce::String initialMap = "kick1:KitA/kick-22\nsnare1:KitA/snare-14";
    REQUIRE(fixture.engine.useInstrumentMap(initialMap));

    auto* kickBefore = DrumKitEngineTestAccess::primarySynth(fixture.engine, "kick1");
    auto* snareBefore = DrumKitEngineTestAccess::primarySynth(fixture.engine, "snare1");
    REQUIRE(kickBefore != nullptr);
    REQUIRE(snareBefore != nullptr);

    const juce::String changedMap = "kick1:KitB/kick-24\nsnare1:KitA/snare-14";
    REQUIRE(fixture.engine.applyInstrumentMapSlotChange(changedMap, "kick1"));
    CHECK(DrumKitEngineTestAccess::primarySynth(fixture.engine, "kick1") != kickBefore);
    CHECK(DrumKitEngineTestAccess::primarySynth(fixture.engine, "snare1") == snareBefore);
}

TEST_CASE("InstrumentMap whole-instrument assignment replaces one slot with an instrument-level entry", "[InstrumentMap][assignment]") {
    InstrumentMapApplyFixture fixture;
    const juce::String currentMap = "kick1/center:KitA/kick-22/center:vol=-3\nkick1/edge:KitC/kick-26/center\nsnare1:KitA/snare-14:tuning=20";

    const auto updatedMap = fixture.engine.buildInstrumentMapWithSlotInstrument(currentMap, "kick1", "KitB", "kick-24");

    REQUIRE(updatedMap.ok());
    const auto parsed = DrumKitEngine::parseInstrumentMapText(updatedMap.value);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.entries.size() == 2);
    const auto kickIterator = std::find_if(parsed.value.entries.begin(), parsed.value.entries.end(), [](const auto& entry) { return entry.slotId == "kick1"; });
    const auto snareIterator = std::find_if(parsed.value.entries.begin(), parsed.value.entries.end(), [](const auto& entry) { return entry.slotId == "snare1"; });
    REQUIRE(kickIterator != parsed.value.entries.end());
    REQUIRE(snareIterator != parsed.value.entries.end());
    CHECK(kickIterator->kitRef == "KitB");
    CHECK(kickIterator->instCode == "kick-24");
    CHECK(kickIterator->slotArticulationFilter.empty());
    CHECK(kickIterator->articulationFilter.empty());
    CHECK_FALSE(kickIterator->volumeDb.has_value());
    CHECK(snareIterator->kitRef == "KitA");
    CHECK(snareIterator->instCode == "snare-14");
    CHECK(snareIterator->tuningCents == 20);
}

TEST_CASE("InstrumentMap whole-instrument assignment rejects an incompatible slot", "[InstrumentMap][assignment]") {
    InstrumentMapApplyFixture fixture;

    const auto updatedMap = fixture.engine.buildInstrumentMapWithSlotInstrument("kick1:KitA/kick-22", "kick1", "KitB", "snare-13");

    CHECK_FALSE(updatedMap.ok());
    CHECK(updatedMap.error().containsIgnoreCase("does not match"));
}

TEST_CASE("InstrumentMap apply rebuilds shadows only for the changed layered slot", "[InstrumentMap][IncrementalApply]") {
    InstrumentMapApplyFixture fixture;
    const juce::String initialMap = "snare1:KitA/snare-14\nsnare1:KitB/snare-13\ntom1:KitA/tom-10\ntom1:KitB/tom-12";
    REQUIRE(fixture.engine.useInstrumentMap(initialMap));

    auto* snareShadowBefore = DrumKitEngineTestAccess::shadowSynth(fixture.engine, "snare1");
    auto* tomShadowBefore = DrumKitEngineTestAccess::shadowSynth(fixture.engine, "tom1");
    REQUIRE(snareShadowBefore != nullptr);
    REQUIRE(tomShadowBefore != nullptr);

    const juce::String changedMap = "snare1:KitA/snare-14\nsnare1:KitB/snare-13\ntom1:KitA/tom-10\ntom1:KitC/tom-14";
    REQUIRE(fixture.engine.applyInstrumentMapSlotChange(changedMap, "tom1"));
    CHECK(DrumKitEngineTestAccess::shadowSynth(fixture.engine, "snare1") == snareShadowBefore);
    CHECK(DrumKitEngineTestAccess::shadowSynth(fixture.engine, "tom1") != tomShadowBefore);
}

TEST_CASE("InstrumentMap parameter-only apply keeps the loaded primary synth", "[InstrumentMap][IncrementalApply]") {
    InstrumentMapApplyFixture fixture;
    REQUIRE(fixture.engine.useInstrumentMap("kick1:KitA/kick-22"));

    auto* kickBefore = DrumKitEngineTestAccess::primarySynth(fixture.engine, "kick1");
    REQUIRE(kickBefore != nullptr);

    REQUIRE(fixture.engine.applyInstrumentMapSlotChange("kick1:KitA/kick-22:vol=-3", "kick1"));
    CHECK(DrumKitEngineTestAccess::primarySynth(fixture.engine, "kick1") == kickBefore);
    CHECK(kickBefore->getVolumeDb() == -3.0f);
}

} // namespace InstrumentMapApplyTests
