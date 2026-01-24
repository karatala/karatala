#include "helpers/test_helpers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "../source/Engine/DrumKitModel.h"
#include "../source/Engine/DrumKitEngine.h"

TEST_CASE("InstrumentSynth - basic velocity selection", "[InstrumentSynth]") {
    DrumKitInstrument::MetaInfo meta;
    meta.code = "test";
    DrumKitInstrument instr(nullptr, meta);

    auto* art = instr.getOrAddArticulation("center");
    auto* l1 = art->getOrAddVelocityLayer(10);
    auto* l2 = art->getOrAddVelocityLayer(100);

    l1->addSampleVariant(std::make_unique<LayerSampleVariant>(juce::File(), 'a', MicDescriptor()));
    l2->addSampleVariant(std::make_unique<LayerSampleVariant>(juce::File(), 'a', MicDescriptor()));

    InstrumentSynth synth(nullptr, KitLayout::Instrument());
    synth.loadInstrument(&instr);

    auto* s1 = synth.selectSampleFor("center", 55);
    REQUIRE(s1 != nullptr);
    REQUIRE(s1->velocityLayer == 10);

    auto* s2 = synth.selectSampleFor("center", 56);
    REQUIRE(s2 != nullptr);
    REQUIRE(s2->velocityLayer == 100);
}

TEST_CASE("InstrumentSynth - different round robins", "[InstrumentSynth]") {
    DrumKitInstrument::MetaInfo meta;
    meta.code = "test";
    DrumKitInstrument instr(nullptr, meta);

    auto* art = instr.getOrAddArticulation("center");
    auto* l1 = art->getOrAddVelocityLayer(50);
    l1->addSampleVariant(std::make_unique<LayerSampleVariant>(juce::File(), 'a', MicDescriptor()));
    l1->addSampleVariant(std::make_unique<LayerSampleVariant>(juce::File(), 'b', MicDescriptor()));

    InstrumentSynth synth(nullptr, KitLayout::Instrument());
    synth.loadInstrument(&instr);

    auto* s1 = synth.selectSampleFor("center", 50);
    auto* s2 = synth.selectSampleFor("center", 50);
    REQUIRE(s1 != nullptr);
    REQUIRE(s2 != nullptr);
    // Should be different variants 'a' and 'b'
    REQUIRE(s1->variantLetter != s2->variantLetter);
}

// TEST_CASE("InstrumentSynth - HiHat combined 2D selector", "[InstrumentSynth]") {
//     DrumKitInstrument::MetaInfo meta;
//     meta.code = "hhat";
//     DrumKitInstrument instr(nullptr, meta);
//     instr.meta.type = DrumKitInstrument::Type::HiHat;

//     auto* artPedal = instr.getOrAddArticulation("pedal");
//     auto* artOpen = instr.getOrAddArticulation("open");

//     auto* lp = artPedal->getOrAddVelocityLayer(10);
//     lp->addSampleVariant(std::make_unique<LayerSampleVariant>(juce::File(), 'a', MicDescriptor()));

//     auto* lo = artOpen->getOrAddVelocityLayer(10);
//     lo->addSampleVariant(std::make_unique<LayerSampleVariant>(juce::File(), 'b', MicDescriptor()));

//     InstrumentSynth synth(nullptr, KitLayout::Instrument());
//     synth.loadInstrument(&instr);

//     auto* sOpen = synth.selectSampleFor("open", 10);
//     auto* sPedal = synth.selectSampleFor("pedal", 10);
//     REQUIRE(sOpen != nullptr);
//     REQUIRE(sPedal != nullptr);
//     REQUIRE(sOpen->variantLetter != sPedal->variantLetter);
// }
