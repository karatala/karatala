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

    static void mapNote(InstrumentSynth& synth, int midiNoteNumber, InstrumentSynth::ArtRtIdx articulationIdx) {
        synth.noteToArticulation[static_cast<size_t>(midiNoteNumber)] = articulationIdx;
    }

    static void setVoiceArticulation(InstrumentSynthVoice& voice, const KitLib::ArticulationSpec& articulationSpec) {
        voice.artNameId = articulationSpec.nameId;
        voice.artExprId = articulationSpec.exprId;
    }

    static void setPedalNote(InstrumentSynth& synth, int midiNoteNumber) {
        synth.hihatPedalNote = midiNoteNumber;
    }

    static void startVoiceForRender(InstrumentSynthVoice& voice, InstrumentSynth& owner, SampledSound& sound) {
        voice.setOwner(&owner);
        voice.setCurrentPlaybackSampleRate(44100.0);
        voice.setAttackTimeSec(0.0f);
        voice.currentlyPlayingNote = 60;
        voice.currentPlayingMidiChannel = 1;
        voice.currentlyPlayingSound = &sound;
        voice.startNote(60, 1.0f, &sound);
    }
};

namespace {

KitLayout::InstrumentSlot makeHiHatSlot() {
    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    KitLayout::InstrumentSlot::ArticulationTrigger pedalTrigger { "pedal" };
    pedalTrigger.notes.push_back(44);
    slot.articulations.push_back(std::move(pedalTrigger));
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
    KitLib::ArticulationSpec pedalSpec { &this->instrumentSpec, "pedal" };
    KitLib::VelocityLayerSpec tightLayerSpec { &this->tightSpec, 127 };
    KitLib::VelocityLayerSpec openLayerSpec { &this->openSpec, 127 };
    KitLib::VelocityLayerSpec pedalLayerSpec { &this->pedalSpec, 127 };
    KitLib::MicSpec directMic = makeDirectMic();
    KitLib::SampleVariantSpec tightVariant { &this->tightLayerSpec, {}, 'a', this->directMic };
    KitLib::SampleVariantSpec openVariant { &this->openLayerSpec, {}, 'a', this->directMic };
    KitLib::SampleVariantSpec pedalVariant { &this->pedalLayerSpec, {}, 'a', this->directMic };
    SampledSound tightSound { nullptr, &this->tightVariant };
    SampledSound openSound { nullptr, &this->openVariant };
    SampledSound pedalSound { nullptr, &this->pedalVariant };
    InstrumentSynthVoice* tightVoice = nullptr;
    InstrumentSynthVoice* openVoice = nullptr;
    InstrumentSynthVoice* pedalVoice = nullptr;

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
        articulations.back().layers.emplace_back();
        articulations.back().velocityToLayer.fill(0);
        articulations.emplace_back(this->openSpec.nameId, this->openSpec.exprId);
        articulations.back().pocValue = 10;
        articulations.back().layers.emplace_back();
        articulations.back().velocityToLayer.fill(0);
        articulations.emplace_back(this->pedalSpec.nameId, this->pedalSpec.exprId);
        articulations.back().pocValue = 90;
        articulations.back().layers.emplace_back();
        articulations.back().velocityToLayer.fill(0);

        InstrumentSynthTestAccess::mapRuntimeArticulation(this->synth, this->tightSpec, 0);
        InstrumentSynthTestAccess::mapRuntimeArticulation(this->synth, this->openSpec, 1);
        InstrumentSynthTestAccess::mapRuntimeArticulation(this->synth, this->pedalSpec, 2);
        InstrumentSynthTestAccess::mapNote(this->synth, 44, 2);
        InstrumentSynthTestAccess::setPedalNote(this->synth, 44);
        InstrumentSynthTestAccess::configureController(this->synth, 4, 1, 0);
        InstrumentSynthTestAccess::configureCachedController(this->synth, 4, 100, 0);

        this->tightSound.sampleRate = 44100.0;
        this->openSound.sampleRate = 44100.0;
        this->pedalSound.sampleRate = 44100.0;
        this->tightSound.decayEnvelope = makeTransitionEnvelope();
        this->openSound.decayEnvelope = makeTransitionEnvelope();
        this->pedalSound.decayEnvelope = makeTransitionEnvelope();
        this->tightSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
        this->openSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
        this->pedalSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
        for (int sampleIdx = 0; sampleIdx < this->openSound.buffer->getNumSamples(); ++sampleIdx) {
            this->tightSound.buffer->setSample(0, sampleIdx, 0.5f);
            this->openSound.buffer->setSample(0, sampleIdx, 0.5f);
            this->pedalSound.buffer->setSample(0, sampleIdx, 0.5f);
        }
        this->tightSound.loaded.store(true);
        this->openSound.loaded.store(true);
        this->pedalSound.loaded.store(true);
        articulations[0].layers[0].samples.push_back({ &this->tightSound, 1 });
        if (includeOpenTarget)
            articulations[1].layers[0].samples.push_back({ &this->openSound, 1 });
        articulations[2].layers[0].samples.push_back({ &this->pedalSound, 1 });

        this->tightVoice = this->synth.addVoice(new InstrumentSynthVoice());
        this->openVoice = this->synth.addVoice(new InstrumentSynthVoice());
        this->pedalVoice = this->synth.addVoice(new InstrumentSynthVoice());
        this->synth.startVoice(this->tightVoice, &this->tightSound, 1, 42, 0.8f);
        InstrumentSynthTestAccess::setVoiceArticulation(*this->tightVoice, this->tightSpec);
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

TEST_CASE("InstrumentSynth - attack guard retries a stopped slow-close controller move", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
    fixture.engine.engineSettings.hihatControllerSmoothingSec.store(0.020f);
    fixture.engine.engineSettings.hihatTransitionAttackGuardSec.store(0.094f);
    fixture.tightVoice->killImmediately();
    fixture.synth.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.8f);
    fixture.openVoice->sourceSamplePosition = 0.0;
    fixture.openVoice->cachedEstimatedDb = -24.0f;
    InstrumentSynthTestAccess::configureCachedController(fixture.synth, 4, 10, 1);

    fixture.synth.handleController(1, 4, 100);

    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE_FALSE(fixture.tightVoice->isVoiceActive());
    auto counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 0);

    fixture.openVoice->sourceSamplePosition = 4410.0;
    InstrumentSynthTestAccess::advanceRenderedSamples(fixture.synth, 882);

    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.tightVoice->getCurrentlyPlayingSound() == &fixture.tightSound);
    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 1);
}

TEST_CASE("InstrumentSynth - voice pressure retries after a replacement voice becomes free", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
    fixture.engine.engineSettings.hihatControllerSmoothingSec.store(0.020f);
    fixture.synth.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.5f);
    fixture.synth.startVoice(fixture.pedalVoice, &fixture.openSound, 1, 42, 0.5f);

    fixture.synth.handleController(1, 4, 10);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    auto counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 0);

    fixture.openVoice->killImmediately();
    InstrumentSynthTestAccess::advanceRenderedSamples(fixture.synth, 882);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.openVoice->getCurrentlyPlayingSound() == &fixture.openSound);
    counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 1);
}

TEST_CASE("InstrumentSynth - audible open tail creates a level-matched pedal event and preserves tight voices", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);
    fixture.engine.engineSettings.hihatTransitionAttackGuardSec.store(0.0f);
    auto& articulations = InstrumentSynthTestAccess::articulations(fixture.synth);
    articulations[0].pocValue = 85;
    articulations[2].pocValue = 85;
    articulations[0].layers[0].samples.clear();
    fixture.synth.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.8f);
    InstrumentSynthTestAccess::setVoiceArticulation(*fixture.openVoice, fixture.openSpec);
    fixture.openVoice->sourceSamplePosition = 4410.0;
    fixture.openVoice->cachedEstimatedDb = -24.0f;
    InstrumentSynthTestAccess::configureCachedController(fixture.synth, 4, 10, 1);

    fixture.synth.handleController(1, 4, 100);

    REQUIRE(fixture.pedalVoice->getCurrentlyPlayingSound() == &fixture.pedalSound);
    REQUIRE(fixture.pedalVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE(fixture.pedalVoice->getVelocityGain() == Catch::Approx(80.0f / 127.0f));

    const auto pedalEventId = fixture.pedalVoice->getEventId();
    fixture.synth.handleController(1, 4, 100);
    REQUIRE(fixture.pedalVoice->getEventId() == pedalEventId);
}

TEST_CASE("InstrumentSynth - closing the pedal in silence does not create a pedal event", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);
    fixture.tightVoice->killImmediately();
    InstrumentSynthTestAccess::configureCachedController(fixture.synth, 4, 10, 1);

    fixture.synth.handleController(1, 4, 100);

    REQUIRE_FALSE(fixture.openVoice->isVoiceActive());
    REQUIRE_FALSE(fixture.pedalVoice->isVoiceActive());
}

TEST_CASE("InstrumentSynth - tight-only playback does not create an implicit pedal event", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);
    auto& articulations = InstrumentSynthTestAccess::articulations(fixture.synth);
    articulations[0].pocValue = 85;
    articulations[2].pocValue = 85;
    InstrumentSynthTestAccess::configureCachedController(fixture.synth, 4, 10, 1);

    fixture.synth.handleController(1, 4, 100);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE_FALSE(fixture.pedalVoice->isVoiceActive());
}

TEST_CASE("InstrumentSynthVoice - pans mono direct mic into stereo output", "[InstrumentSynth][routing]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "cow1";
    slot.type = KitLib::InstrumentSpec::Type::Cowbell;
    InstrumentSynth synth { &engine, slot };

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(1, 2);
    buffer->setSample(0, 0, 1.0f);
    buffer->setSample(0, 1, 1.0f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.micBufferChannelStart = 0;
    sound.micBufferChannelCount = 2;
    sound.useDirectBalance = true;
    sound.balance = 0;

    InstrumentSynthVoice voice;
    InstrumentSynthTestAccess::startVoiceForRender(voice, synth, sound);

    juce::AudioBuffer<float> output(2, 2);
    output.clear();
    voice.renderNextBlock(output, 0, 2);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.7071067f).margin(0.0001f));
    REQUIRE(output.getSample(1, 0) == Catch::Approx(0.7071067f).margin(0.0001f));
}

TEST_CASE("InstrumentSynthVoice - flips stereo direct mic before balance", "[InstrumentSynth][routing]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "perc1";
    slot.type = KitLib::InstrumentSpec::Type::Cowbell;
    InstrumentSynth synth { &engine, slot };
    synth.setFlipLR(true);

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(2, 2);
    buffer->setSample(0, 0, 0.25f);
    buffer->setSample(0, 1, 0.25f);
    buffer->setSample(1, 0, 1.0f);
    buffer->setSample(1, 1, 1.0f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.micBufferChannelStart = 0;
    sound.micBufferChannelCount = 2;
    sound.useDirectBalance = true;
    sound.balance = -127;

    InstrumentSynthVoice voice;
    InstrumentSynthTestAccess::startVoiceForRender(voice, synth, sound);

    juce::AudioBuffer<float> output(2, 2);
    output.clear();
    voice.renderNextBlock(output, 0, 2);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(1.0f).margin(0.0001f));
    REQUIRE(output.getSample(1, 0) == Catch::Approx(0.0f).margin(0.0001f));
}
