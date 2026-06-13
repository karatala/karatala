#include "../source/Engine/DrumKitEngine.h"
#include "../source/Engine/InstrumentSynth.h"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

struct InstrumentSynthTestAccess {
    static std::vector<InstrumentSynth::ArticulationRuntime>& articulations(InstrumentSynth& synth) {
        return synth.runtimeArticulations;
    }

    static void configureController(InstrumentSynth& synth, int controllerNumber) {
        synth.ccToArticulationMask[static_cast<size_t>(controllerNumber)] = true;
        auto& mapping = synth.ccToArticulationTable[static_cast<size_t>(controllerNumber)];
        for (size_t controllerValue = 0; controllerValue < mapping.size(); ++controllerValue)
            mapping[controllerValue] = controllerValue < 64 ? 0 : 1;
    }
};

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

TEST_CASE("Hi-hat controller transition performance", "[benchmark][HiHatTransition]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatControllerSmoothingSec.store(0.0f);
    engine.engineSettings.hihatBoundaryHysteresisCc.store(2);

    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    InstrumentSynth synth(&engine, slot);
    synth.setCurrentPlaybackSampleRate(48000.0);

    auto& articulations = InstrumentSynthTestAccess::articulations(synth);
    articulations.emplace_back(1, 1);
    articulations.back().pocValue = 10;
    articulations.emplace_back(1, 2);
    articulations.back().pocValue = 90;
    InstrumentSynthTestAccess::configureController(synth, 4);

    BENCHMARK("Process a 128-value hi-hat CC sweep") {
        for (int controllerValue = 0; controllerValue < 128; ++controllerValue)
            synth.handleController(1, 4, controllerValue);
        return synth.getHiHatTransitionCounters().processed;
    };
}
