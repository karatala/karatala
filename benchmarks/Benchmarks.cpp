#include "../source/Engine/ArticulationNameHelpers.h"
#include "../source/Engine/DrumKitEngine.h"
#include "../source/Engine/InstrumentPlayer.h"
#include "../source/Engine/MicRoutingPlan.h"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

struct InstrumentPlayerTestAccess {
    static std::vector<InstrumentPlayer::ArticulationRuntime>& articulations(InstrumentPlayer& player) {
        return player.runtimeArticulations;
    }

    static float getMicBleedLevelById(const InstrumentPlayer& player, StringsDictSingleton::Id micCodeId) {
        return player.getMicBleedLevel(micCodeId);
    }

    static InstrumentPlayer::ArtRtIdx selectArticulationForController(const InstrumentPlayer& player
        , InstrumentPlayer::ArtRtIdx baseArtIdx
        , KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType
        , int controllerValue
    ) {
        return player.selectArticulationForController(baseArtIdx, dimensionType, controllerValue);
    }

    static void configureController(InstrumentPlayer& player, int controllerNumber) {
        player.ccToArticulationMask[static_cast<size_t>(controllerNumber)] = true;
        auto& mapping = player.ccToArticulationTable[static_cast<size_t>(controllerNumber)];
        for (size_t controllerValue = 0; controllerValue < mapping.size(); ++controllerValue)
            mapping[controllerValue] = controllerValue < 64 ? 0 : 1;
    }

    static void advanceRenderedSamples(InstrumentPlayer& player, uint64_t sampleCount) {
        player.renderedSampleCount += sampleCount;
        player.processPendingHiHatControllers();
    }
};

namespace BenchmarkHelpers {
    static std::vector<juce::String> makeMicCodes(int count) {
        std::vector<juce::String> micCodes;
        micCodes.reserve(static_cast<size_t>(count));
        for (int micIdx = 0; micIdx < count; ++micIdx)
            micCodes.push_back("mic" + juce::String(micIdx));
        return micCodes;
    }

    static std::unordered_map<juce::String, juce::String> makeStringRouteOverrides(const std::vector<juce::String>& micCodes, const std::vector<juce::String>& outputIds) {
        std::unordered_map<juce::String, juce::String> routeOverrides;
        routeOverrides.reserve(micCodes.size());
        for (size_t micIdx = 0; micIdx < micCodes.size(); ++micIdx)
            routeOverrides[micCodes[micIdx]] = outputIds[micIdx % outputIds.size()];
        return routeOverrides;
    }

    static InstrumentPlayer::MicRouteOverrideIdMap makeIdRouteOverrides(const std::unordered_map<juce::String, juce::String>& routeOverrides) {
        InstrumentPlayer::MicRouteOverrideIdMap routeOverrideIds;
        routeOverrideIds.reserve(routeOverrides.size());
        for (const auto& [micCode, outputId] : routeOverrides) {
            const auto micCodeId = StringsDictSingleton::getId(micCode.trim().toLowerCase());
            const auto outputKey = StringsDictSingleton::getId(outputId.trim().toLowerCase());
            routeOverrideIds[micCodeId] = outputKey;
        }
        return routeOverrideIds;
    }

    static juce::String findStringRouteOverride(const std::unordered_map<juce::String, juce::String>& routeOverrides, const juce::String& micCode) {
        for (const auto& [overrideMicCode, outputId] : routeOverrides)
            if (overrideMicCode.compareIgnoreCase(micCode) == 0) return outputId;
        return {};
    }

    static MicLayout makeMicLayoutForRoutingPlan() {
        MicLayout micLayout;

        MicLayout::Mic directMic;
        directMic.id = "kick1/dtm";
        directMic.name = "Direct Top";
        directMic.code = "dtm";
        directMic.specs = KitLib::MicSpec::parseMicCode("dtm").value;
        micLayout.mics.push_back(directMic);

        MicLayout::Mic overheadMic;
        overheadMic.id = "overhead/ons";
        overheadMic.name = "Overhead";
        overheadMic.code = "ons";
        overheadMic.specs = KitLib::MicSpec::parseMicCode("ons").value;
        micLayout.mics.push_back(overheadMic);

        MicLayout::Mic roomMic;
        roomMic.id = "room/rfs";
        roomMic.name = "Room";
        roomMic.code = "rfs";
        roomMic.specs = KitLib::MicSpec::parseMicCode("rfs").value;
        micLayout.mics.push_back(roomMic);

        return micLayout;
    }

    static std::vector<juce::String> makeArticulationNames(int count) {
        std::vector<juce::String> names;
        names.reserve(static_cast<size_t>(count));
        for (int nameIdx = 0; nameIdx < count; ++nameIdx) {
            const auto suffix = juce::String(nameIdx % 8);
            names.push_back(nameIdx % 3 == 0 ? " Center " + suffix : "rim[edge] " + suffix);
        }
        return names;
    }

    static std::vector<juce::String> makeUiSearchRows(int count) {
        std::vector<juce::String> rows;
        rows.reserve(static_cast<size_t>(count));
        for (int rowIdx = 0; rowIdx < count; ++rowIdx) {
            if (rowIdx % 4 == 0) rows.push_back("  Snare Room Mic " + juce::String(rowIdx));
            else if (rowIdx % 4 == 1) rows.push_back("Kick Direct Top " + juce::String(rowIdx));
            else if (rowIdx % 4 == 2) rows.push_back("HiHat Overhead " + juce::String(rowIdx));
            else rows.push_back("Ride Edge " + juce::String(rowIdx));
        }
        return rows;
    }
}

TEST_CASE("Boot performance ENABLE DUMMY!") {
    // BENCHMARK_ADVANCED("Processor constructor")
    // (Catch::Benchmark::Chronometer meter) {
    //     std::vector<Catch::Benchmark::storage_for<PluginProcessor>> storage(size_t(meter.runs()));
    //     meter.measure([&](int i) { storage[(size_t) i].construct(); });
    // };

    // BENCHMARK_ADVANCED("Processor destructor")
    // (Catch::Benchmark::Chronometer meter) {
    //     std::vector<Catch::Benchmark::destructable_object<PluginProcessor>> storage(size_t(meter.runs()));
    //     for (auto& s : storage)
    //         s.construct();
    //     meter.measure([&](int i) { storage[(size_t) i].destruct(); });
    // };

    // BENCHMARK_ADVANCED("Editor open and close")
    // (Catch::Benchmark::Chronometer meter) {
    //     PluginProcessor plugin;

    //     // due to complex construction logic of the editor, let's measure open/close together
    //     meter.measure([&](int /* i */) {
    //         auto editor = plugin.createEditorIfNeeded();
    //         plugin.editorBeingDeleted(editor);
    //         delete editor;
    //         return plugin.getActiveEditor();
    //     });
    // };
}

TEST_CASE("Plan item 1 - mic bleed lookup performance", "[benchmark][PlanItem1][MicBleed]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    InstrumentPlayer player(&engine, slot);
    player.setMicBleedLevels({ { "mic42", 0.5f }, { "mic64", 0.25f } });

    const auto micCodeId = StringsDictSingleton::getId("mic42");
    SampledSound sound;
    sound.routedMicCode = "mic42";
    sound.routedMicCodeId = micCodeId;
    sound.cachedMicBleedGain.store(0.5f, std::memory_order_relaxed);

    BENCHMARK("String mic bleed lookup") {
        float total = 0.0f;
        for (int lookupIdx = 0; lookupIdx < 256; ++lookupIdx)
            total += player.getMicBleedLevel(sound.routedMicCode);
        return total;
    };

    BENCHMARK("ID mic bleed lookup") {
        float total = 0.0f;
        for (int lookupIdx = 0; lookupIdx < 256; ++lookupIdx)
            total += InstrumentPlayerTestAccess::getMicBleedLevelById(player, sound.routedMicCodeId);
        return total;
    };

    BENCHMARK("Cached sample mic bleed load") {
        float total = 0.0f;
        for (int lookupIdx = 0; lookupIdx < 256; ++lookupIdx)
            total += sound.cachedMicBleedGain.load(std::memory_order_relaxed);
        return total;
    };
}

TEST_CASE("Plan item 2 - StringsDictSingleton performance", "[benchmark][PlanItem2][StringsDict]") {
    std::vector<juce::String> names;
    std::vector<StringsDictSingleton::Id> nameIds;
    names.reserve(512);
    nameIds.reserve(512);
    for (int nameIdx = 0; nameIdx < 512; ++nameIdx) {
        names.push_back("bench_name_" + juce::String(nameIdx));
        nameIds.push_back(StringsDictSingleton::getId(names.back()));
    }

    BENCHMARK("Existing name to ID lookup") {
        uint32_t total = 0;
        for (const auto& name : names)
            total += StringsDictSingleton::getId(name);
        return total;
    };

    BENCHMARK("ID to name lookup") {
        int totalLength = 0;
        for (const auto nameId : nameIds)
            totalLength += StringsDictSingleton::nameOf(nameId).length();
        return totalLength;
    };
}

TEST_CASE("Plan item 3 - route override ID lookup performance", "[benchmark][PlanItem3][MicRoute]") {
    const auto micCodes = BenchmarkHelpers::makeMicCodes(96);
    const auto outputIds = BenchmarkHelpers::makeMicCodes(16);
    const auto stringOverrides = BenchmarkHelpers::makeStringRouteOverrides(micCodes, outputIds);
    const auto idOverrides = BenchmarkHelpers::makeIdRouteOverrides(stringOverrides);

    BENCHMARK("Old-style string route override scan") {
        int matches = 0;
        for (const auto& micCode : micCodes)
            if (BenchmarkHelpers::findStringRouteOverride(stringOverrides, micCode).isNotEmpty()) ++matches;
        return matches;
    };

    BENCHMARK("ID route override map lookup") {
        int matches = 0;
        for (const auto& micCode : micCodes) {
            const auto micCodeId = StringsDictSingleton::getId(micCode.trim().toLowerCase());
            if (idOverrides.find(micCodeId) != idOverrides.end()) ++matches;
        }
        return matches;
    };
}

TEST_CASE("Plan item 3 - routing plan override target lookup performance", "[benchmark][PlanItem3][MicRoute]") {
    const auto micLayout = BenchmarkHelpers::makeMicLayoutForRoutingPlan();

    MicRoutingPlan::SlotRequest request;
    request.slotId = "kick1";
    request.slotType = KitLib::InstrumentSpec::Type::Kick;
    request.directPolicy = DirectMicOutPolicy::Shared;
    request.directMicSpecs.push_back(micLayout.mics.front().specs);

    const auto plan = MicRoutingPlan::build(micLayout, { request }, InstrumentCatalog::makeDefault());
    const auto outputKey = StringsDictSingleton::getId("room/rfs");
    const auto directMicSpec = micLayout.mics.front().specs;

    BENCHMARK("Resolve override target by ID") {
        int matches = 0;
        for (int lookupIdx = 0; lookupIdx < 256; ++lookupIdx)
            if (plan.resolveOverride("kick1", directMicSpec, outputKey).matched()) ++matches;
        return matches;
    };
}

TEST_CASE("Plan item 4 - controller selection scan performance", "[benchmark][PlanItem4][ControllerLookup]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    InstrumentPlayer player(&engine, slot);

    auto& articulations = InstrumentPlayerTestAccess::articulations(player);
    const auto bowNameId = StringsDictSingleton::getId("bow");
    for (int artIdx = 0; artIdx < 16; ++artIdx) {
        articulations.emplace_back(bowNameId, StringsDictSingleton::getId("expr" + juce::String(artIdx)));
        articulations.back().pedalOpenCloseValue = artIdx * 8;
    }

    BENCHMARK("Select articulation for 128 controller values") {
        int selectedTotal = 0;
        for (int controllerValue = 0; controllerValue < 128; ++controllerValue)
            selectedTotal += InstrumentPlayerTestAccess::selectArticulationForController(player, 0, KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose, controllerValue);
        return selectedTotal;
    };
}

TEST_CASE("Plan item 5 - empty render lock baseline", "[benchmark][PlanItem5][RenderLock]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    InstrumentPlayer player(&engine, slot);
    player.setCurrentPlaybackSampleRate(48000.0);
    player.addVoice(new InstrumentPlayerVoice());

    juce::AudioBuffer<float> outputBuffer(2, 128);
    juce::MidiBuffer midiBuffer;

    BENCHMARK("Render empty player block") {
        player.renderNextBlock(outputBuffer, midiBuffer, 0, 128);
        return outputBuffer.getNumSamples();
    };
}

TEST_CASE("Plan item 6 - runtime allocation baseline", "[benchmark][PlanItem6][Allocations]") {
    BENCHMARK("Create velocity levels with shared atomic counters") {
        std::vector<InstrumentPlayer::VelocityLevelRuntime> velocityLevels;
        velocityLevels.reserve(128);
        for (int velocityLevelIdx = 0; velocityLevelIdx < 128; ++velocityLevelIdx) {
            auto& velocityLevel = velocityLevels.emplace_back();
            velocityLevel.velocity = static_cast<uint8_t>(velocityLevelIdx);
        }
        return velocityLevels.size();
    };

    BENCHMARK("Create small MIDI unordered map") {
        std::unordered_map<uint8_t, int> noteCounts;
        noteCounts.reserve(32);
        for (uint8_t note = 0; note < 32; ++note)
            noteCounts[note] = static_cast<int>(note);
        return noteCounts.size();
    };
}

TEST_CASE("Plan item 7 - memory layout size baseline", "[benchmark][PlanItem7][MemoryLayout]") {
    BENCHMARK("Collect candidate type size and alignment constants") {
        size_t total = 0;
        total += sizeof(SampledSound) + alignof(SampledSound);
        total += sizeof(InstrumentPlayerVoice) + alignof(InstrumentPlayerVoice);
        total += sizeof(KitLib::LogicalHitSpec) + alignof(KitLib::LogicalHitSpec);
        total += sizeof(KitLib::SampleSpec) + alignof(KitLib::SampleSpec);
        total += sizeof(KitLib::MicSpec) + alignof(KitLib::MicSpec);
        total += sizeof(DrumKitEngine::InstrumentMapLayer) + alignof(DrumKitEngine::InstrumentMapLayer);
        return total;
    };
}

TEST_CASE("Plan item 8 - repeated string helper baseline", "[benchmark][PlanItem8][StringHelpers]") {
    const auto names = BenchmarkHelpers::makeArticulationNames(512);

    BENCHMARK("Repeated articulation trim and case matching") {
        int matches = 0;
        for (const auto& name : names)
            if (ArticulationNameHelpers::articulationCodeMatches(name, "center 0")) ++matches;
        return matches;
    };
}

TEST_CASE("Plan item 9 - UI-style string cleanup baseline", "[benchmark][PlanItem9][UiStrings]") {
    const auto rows = BenchmarkHelpers::makeUiSearchRows(512);
    const juce::String needle = "snare";

    BENCHMARK("Normalize and search UI text rows") {
        int matches = 0;
        for (const auto& row : rows) {
            const auto normalized = row.trim().toLowerCase();
            if (normalized.contains(needle)) ++matches;
        }
        return matches;
    };
}

TEST_CASE("Hi-hat controller transition performance", "[benchmark][HiHatTransition]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatControllerSmoothingSec.store(0.0f);
    engine.engineSettings.hihatBoundaryHysteresisCc.store(2);

    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    InstrumentPlayer player(&engine, slot);
    player.setCurrentPlaybackSampleRate(48000.0);

    auto& articulations = InstrumentPlayerTestAccess::articulations(player);
    articulations.emplace_back(1, 1);
    articulations.back().pedalOpenCloseValue = 10;
    articulations.emplace_back(1, 2);
    articulations.back().pedalOpenCloseValue = 90;
    InstrumentPlayerTestAccess::configureController(player, 4);

    BENCHMARK("Process a 128-value hi-hat CC sweep") {
        for (int controllerValue = 0; controllerValue < 128; ++controllerValue)
            player.handleController(1, 4, controllerValue);
        return player.getHiHatTransitionCounters().processed;
    };
}

TEST_CASE("Hi-hat controller coalescing performance", "[benchmark][HiHatTransition]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatControllerSmoothingSec.store(0.015f);

    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    InstrumentPlayer player(&engine, slot);
    player.setCurrentPlaybackSampleRate(48000.0);

    auto& articulations = InstrumentPlayerTestAccess::articulations(player);
    articulations.emplace_back(1, 1);
    articulations.back().pedalOpenCloseValue = 10;
    articulations.emplace_back(1, 2);
    articulations.back().pedalOpenCloseValue = 90;
    InstrumentPlayerTestAccess::configureController(player, 4);

    BENCHMARK("Coalesce a 128-message burst and process its latest value") {
        for (int controllerValue = 0; controllerValue < 128; ++controllerValue)
            player.handleController(1, 4, controllerValue);
        InstrumentPlayerTestAccess::advanceRenderedSamples(player, 720);
        return player.getHiHatTransitionCounters().processed;
    };
}
