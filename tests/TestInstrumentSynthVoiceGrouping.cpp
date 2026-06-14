#include "../source/Engine/DrumKitEngine.h"
#include "../source/Engine/InstrumentSynth.h"
#include "catch2/catch_approx.hpp"
#include "catch2/catch_test_macros.hpp"

struct InstrumentSynthTestAccess {
    static std::vector<InstrumentSynth::ArticulationRuntime>& articulations(InstrumentSynth& synth) {
        return synth.runtimeArticulations;
    }

    static void configureController(InstrumentSynth& synth, int controllerNumber, uint8_t lowArticulationIdx, uint8_t highArticulationIdx) {
        synth.ccToArticulationMask[static_cast<size_t>(controllerNumber)] = true;
        auto& mapping = synth.ccToArticulationTable[static_cast<size_t>(controllerNumber)];
        for (size_t controllerValue = 0; controllerValue < mapping.size(); ++controllerValue)
            mapping[controllerValue] = controllerValue < 64 ? lowArticulationIdx : highArticulationIdx;
    }

    static void configureCachedController(InstrumentSynth& synth, int controllerNumber, int controllerValue, InstrumentSynth::ArtRtIdx articulationIdx) {
        synth.lastCcValue[static_cast<size_t>(controllerNumber)] = controllerValue;
        synth.lastCcArticulation[static_cast<size_t>(controllerNumber)] = articulationIdx;
    }

    static void mapRuntimeArticulation(InstrumentSynth& synth, const KitLib::ArticulationSpec& articulationSpec, InstrumentSynth::ArtRtIdx articulationIdx) {
        const uint64_t articulationKey = (static_cast<uint64_t>(articulationSpec.nameId) << 32)
            | static_cast<uint64_t>(articulationSpec.exprId);
        synth.artIdByNameExprId[articulationKey] = articulationIdx;
    }

    static void advanceRenderedSamples(InstrumentSynth& synth, uint64_t sampleCount) {
        synth.renderedSampleCount += sampleCount;
        synth.processPendingHiHatControllers();
    }

    static int cachedArticulation(const InstrumentSynth& synth, int controllerNumber) {
        return synth.lastCcArticulation[static_cast<size_t>(controllerNumber)];
    }
};

namespace {

KitLayout::InstrumentSlot makeHiHatSlot() {
    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    return slot;
}

KitLib::InstrumentSpec::MetaInfo makeInstrumentMeta() {
    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = "hihat-test";
    meta.type = KitLib::InstrumentSpec::Type::HiHat;
    return meta;
}

KitLib::MicSpec makeDirectMic() {
    KitLib::MicSpec micSpec;
    micSpec.type = KitLib::MicSpec::Type::Direct;
    micSpec.channels = 1;
    return micSpec;
}

KitLib::DecayEnvelope makeTransitionEnvelope() {
    KitLib::DecayEnvelope envelope;
    envelope.peakDb = -20.0f;
    envelope.peakMs = 10.0f;
    envelope.points[0] = 100.0f;
    envelope.points[1] = 200.0f;
    envelope.validPoints = 2;
    return envelope;
}

struct ControllerTransitionFixture {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot = makeHiHatSlot();
    InstrumentSynth synth { &this->engine, this->slot };
    KitLib::InstrumentSpec::MetaInfo instrumentMeta = makeInstrumentMeta();
    KitLib::InstrumentSpec instrumentSpec { nullptr, this->instrumentMeta };
    KitLib::ArticulationSpec tightSpec { &this->instrumentSpec, "edge[tight]" };
    KitLib::ArticulationSpec openSpec { &this->instrumentSpec, "edge[open]" };
    KitLib::VelocityLayerSpec tightLayerSpec { &this->tightSpec, 127 };
    KitLib::VelocityLayerSpec openLayerSpec { &this->openSpec, 127 };
    KitLib::MicSpec directMic = makeDirectMic();
    KitLib::SampleVariantSpec tightVariant { &this->tightLayerSpec, {}, 'a', this->directMic };
    KitLib::SampleVariantSpec openVariant { &this->openLayerSpec, {}, 'a', this->directMic };
    SampledSound tightSound { nullptr, &this->tightVariant };
    SampledSound openSound { nullptr, &this->openVariant };
    InstrumentSynthVoice* tightVoice = nullptr;
    InstrumentSynthVoice* openVoice = nullptr;

    explicit ControllerTransitionFixture(bool includeOpenTarget = true) {
        this->engine.engineSettings.hihatControllerSmoothingSec.store(0.0f);
        this->engine.engineSettings.hihatOpenCrossfadeSec.store(0.080f);
        this->engine.engineSettings.hihatTransitionAttackGuardSec.store(0.0f);
        this->engine.engineSettings.hihatTransitionMinRemainingSec.store(0.01f);
        this->engine.engineSettings.hihatTransitionSilenceFloorDb.store(-90.0f);
        this->engine.engineSettings.hihatTailMatchAmount.store(0.0f);
        this->synth.setCurrentPlaybackSampleRate(44100.0);

        auto& articulations = InstrumentSynthTestAccess::articulations(this->synth);
        articulations.emplace_back(this->tightSpec.nameId, this->tightSpec.exprId);
        articulations.back().pocValue = 90;
        articulations.emplace_back(this->openSpec.nameId, this->openSpec.exprId);
        articulations.back().pocValue = 10;
        articulations.back().layers.emplace_back();
        articulations.back().velocityToLayer.fill(0);

        InstrumentSynthTestAccess::mapRuntimeArticulation(this->synth, this->tightSpec, 0);
        InstrumentSynthTestAccess::mapRuntimeArticulation(this->synth, this->openSpec, 1);
        InstrumentSynthTestAccess::configureController(this->synth, 4, 1, 0);
        InstrumentSynthTestAccess::configureCachedController(this->synth, 4, 100, 0);

        this->tightSound.sampleRate = 44100.0;
        this->openSound.sampleRate = 44100.0;
        this->tightSound.decayEnvelope = makeTransitionEnvelope();
        this->openSound.decayEnvelope = makeTransitionEnvelope();
        this->openSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
        for (int sampleIdx = 0; sampleIdx < this->openSound.buffer->getNumSamples(); ++sampleIdx)
            this->openSound.buffer->setSample(0, sampleIdx, 0.5f);
        this->openSound.loaded.store(true);
        if (includeOpenTarget)
            articulations[1].layers[0].samples.push_back({ &this->openSound, 1 });

        this->tightVoice = this->synth.addVoice(new InstrumentSynthVoice());
        this->openVoice = this->synth.addVoice(new InstrumentSynthVoice());
        this->synth.startVoice(this->tightVoice, &this->tightSound, 1, 42, 0.8f);
        this->tightVoice->sourceSamplePosition = 4410.0;
        this->tightVoice->cachedEstimatedDb = -24.0f;
    }
};

} // namespace

TEST_CASE("InstrumentSynth - voice starts receive independent event ids", "[InstrumentSynth]") {
    InstrumentSynth synth(nullptr, KitLayout::InstrumentSlot {});
    synth.setCurrentPlaybackSampleRate(44100.0);

    auto* firstVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* secondVoice = synth.addVoice(new InstrumentSynthVoice());
    SampledSound firstSound;
    SampledSound secondSound;

    synth.startVoice(firstVoice, &firstSound, 1, 42, 0.8f);
    synth.startVoice(secondVoice, &secondSound, 1, 42, 0.8f);

    REQUIRE(firstVoice->getEventId() != 0);
    REQUIRE(secondVoice->getEventId() != 0);
    REQUIRE(firstVoice->getEventId() != secondVoice->getEventId());
}

TEST_CASE("InstrumentSynth - controller uses accepted per-voice tail replacement", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;

    fixture.synth.handleController(1, 4, 10);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.tightVoice->adsr.getParameters().release == Catch::Approx(0.080f));
    REQUIRE(fixture.openVoice->getCurrentlyPlayingSound() == &fixture.openSound);
    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE(fixture.openVoice->sourceSamplePosition == Catch::Approx(4410.0));
    const auto counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.completed == 1);
    REQUIRE(counters.skipped == 0);
}

TEST_CASE("InstrumentSynth - unavailable target leaves the ringing voice unchanged", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture(false);

    fixture.synth.handleController(1, 4, 10);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE_FALSE(fixture.openVoice->isVoiceActive());
    REQUIRE(InstrumentSynthTestAccess::cachedArticulation(fixture.synth, 4) == 0);
    const auto counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.completed == 0);
    REQUIRE(counters.skipped == 1);
}

TEST_CASE("InstrumentSynth - controller smoothing retains the latest value", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatControllerSmoothingSec.store(0.010f);

    const auto slot = makeHiHatSlot();
    InstrumentSynth synth(&engine, slot);
    synth.setCurrentPlaybackSampleRate(1000.0);
    auto& articulations = InstrumentSynthTestAccess::articulations(synth);
    articulations.emplace_back(1, 1);
    articulations.back().pocValue = 10;
    articulations.emplace_back(1, 2);
    articulations.back().pocValue = 90;
    InstrumentSynthTestAccess::configureController(synth, 4, 0, 1);

    synth.handleController(1, 4, 10);
    synth.handleController(1, 4, 80);
    synth.handleController(1, 4, 100);

    auto counters = synth.getHiHatTransitionCounters();
    REQUIRE(counters.processed == 1);
    REQUIRE(counters.coalesced == 2);

    InstrumentSynthTestAccess::advanceRenderedSamples(synth, 10);

    counters = synth.getHiHatTransitionCounters();
    REQUIRE(counters.processed == 2);
    REQUIRE(counters.coalesced == 2);
}
