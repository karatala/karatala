#include "../source/Engine/DrumKitEngine.h"
#include "../source/Engine/InstrumentSynth.h"
#include "catch2/catch_approx.hpp"
#include "catch2/catch_test_macros.hpp"

struct InstrumentSynthTestAccess {
    static std::vector<InstrumentSynth::ArticulationRuntime>& articulations(InstrumentSynth& synth) {
        return synth.runtimeArticulations;
    }

    static void setVoiceArticulation(InstrumentSynthVoice& voice, int articulationIdx) {
        voice.runtimeArticulationIdx = articulationIdx;
    }

    static bool transition(InstrumentSynth& synth, int midiChannel, InstrumentSynth::ArtRtIdx targetArtIdx) {
        return synth.transitionHiHatGroups(midiChannel, targetArtIdx);
    }
};

namespace {

KitLib::DecayEnvelope makeTransitionEnvelope() {
    KitLib::DecayEnvelope envelope;
    envelope.peakDb = -20.0f;
    envelope.peakMs = 10.0f;
    envelope.points[0] = 100.0f;
    envelope.points[1] = 200.0f;
    envelope.validPoints = 2;
    return envelope;
}

KitLib::MicSpec makeMicSpec(KitLib::MicSpec::Type type) {
    KitLib::MicSpec micSpec;
    micSpec.type = type;
    micSpec.channels = 1;
    return micSpec;
}

} // namespace

TEST_CASE("InstrumentSynth - explicit event ids keep multi-mic voices grouped", "[InstrumentSynth][HiHatTransition]") {
    InstrumentSynth synth(nullptr, KitLayout::InstrumentSlot {});
    synth.setCurrentPlaybackSampleRate(44100.0);

    auto* directVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* roomVoice = synth.addVoice(new InstrumentSynthVoice());
    SampledSound directSound;
    SampledSound roomSound;

    const auto eventId = synth.allocateEventId();
    synth.startVoice(directVoice, &directSound, 1, 42, 0.8f, eventId);
    synth.startVoice(roomVoice, &roomSound, 1, 42, 0.8f, eventId);

    REQUIRE(directVoice->getEventId() == eventId);
    REQUIRE(roomVoice->getEventId() == eventId);
    REQUIRE(directVoice->getEventId() == roomVoice->getEventId());
}

TEST_CASE("InstrumentSynth - implicit voice starts allocate distinct event ids", "[InstrumentSynth][HiHatTransition]") {
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

TEST_CASE("InstrumentSynth - hi-hat group transition commits every mic with one time offset", "[InstrumentSynth][HiHatTransition]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatTransitionAttackGuardSec.store(0.0f);
    engine.engineSettings.hihatTransitionMinRemainingSec.store(0.01f);
    engine.engineSettings.hihatTransitionSilenceFloorDb.store(-90.0f);

    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    InstrumentSynth synth(&engine, slot);
    synth.setCurrentPlaybackSampleRate(44100.0);

    auto& articulations = InstrumentSynthTestAccess::articulations(synth);
    articulations.emplace_back(1, 1);
    articulations.back().pocValue = 20;
    articulations.emplace_back(1, 2);
    articulations.back().pocValue = 80;
    articulations.back().layers.emplace_back();
    articulations.back().layers.back().velocity = 127;
    articulations.back().velocityToLayer.fill(0);

    const auto directMic = makeMicSpec(KitLib::MicSpec::Type::Direct);
    const auto roomMic = makeMicSpec(KitLib::MicSpec::Type::Room);
    KitLib::SampleVariantSpec sourceDirectVariant(nullptr, {}, 'a', directMic);
    KitLib::SampleVariantSpec sourceRoomVariant(nullptr, {}, 'a', roomMic);
    KitLib::SampleVariantSpec targetDirectVariant(nullptr, {}, 'a', directMic);
    KitLib::SampleVariantSpec targetRoomVariant(nullptr, {}, 'a', roomMic);

    SampledSound sourceDirect(nullptr, &sourceDirectVariant);
    SampledSound sourceRoom(nullptr, &sourceRoomVariant);
    SampledSound targetDirect(nullptr, &targetDirectVariant);
    SampledSound targetRoom(nullptr, &targetRoomVariant);
    sourceDirect.sampleRate = 44100.0;
    sourceRoom.sampleRate = 48000.0;
    targetDirect.sampleRate = 44100.0;
    targetRoom.sampleRate = 48000.0;
    sourceDirect.micBufferChannelStart = 0;
    sourceRoom.micBufferChannelStart = 2;
    targetDirect.micBufferChannelStart = 0;
    targetRoom.micBufferChannelStart = 2;
    sourceDirect.decayEnvelope = makeTransitionEnvelope();
    sourceRoom.decayEnvelope = makeTransitionEnvelope();
    targetDirect.decayEnvelope = makeTransitionEnvelope();
    targetRoom.decayEnvelope = makeTransitionEnvelope();
    targetDirect.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
    targetRoom.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 96000);
    targetDirect.loaded.store(true);
    targetRoom.loaded.store(true);
    articulations.back().layers.back().samples.push_back({ &targetDirect, 1 });
    articulations.back().layers.back().samples.push_back({ &targetRoom, 1 });

    auto* sourceDirectVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* sourceRoomVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* targetDirectVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* targetRoomVoice = synth.addVoice(new InstrumentSynthVoice());
    const auto eventId = synth.allocateEventId();
    synth.startVoice(sourceDirectVoice, &sourceDirect, 1, 42, 0.8f, eventId);
    synth.startVoice(sourceRoomVoice, &sourceRoom, 1, 42, 0.8f, eventId);
    InstrumentSynthTestAccess::setVoiceArticulation(*sourceDirectVoice, 0);
    InstrumentSynthTestAccess::setVoiceArticulation(*sourceRoomVoice, 0);
    sourceDirectVoice->sourceSamplePosition = 4410.0;
    sourceRoomVoice->sourceSamplePosition = 4800.0;
    sourceDirectVoice->cachedEstimatedDb = -26.0f;
    sourceRoomVoice->cachedEstimatedDb = -30.0f;

    REQUIRE(InstrumentSynthTestAccess::transition(synth, 1, 1));
    REQUIRE(sourceDirectVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(sourceRoomVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(targetDirectVoice->getRuntimeArticulationIndex() == 1);
    REQUIRE(targetRoomVoice->getRuntimeArticulationIndex() == 1);
    REQUIRE(targetDirectVoice->getEventId() == eventId);
    REQUIRE(targetRoomVoice->getEventId() == eventId);

    const double directOffsetMs = targetDirectVoice->sourceSamplePosition / targetDirect.sampleRate * 1000.0;
    const double roomOffsetMs = targetRoomVoice->sourceSamplePosition / targetRoom.sampleRate * 1000.0;
    REQUIRE(directOffsetMs == Catch::Approx(roomOffsetMs).margin(0.001));
}

TEST_CASE("InstrumentSynth - missing target mic leaves the source group unchanged", "[InstrumentSynth][HiHatTransition]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatTransitionAttackGuardSec.store(0.0f);
    engine.engineSettings.hihatTransitionSilenceFloorDb.store(-90.0f);

    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    InstrumentSynth synth(&engine, slot);
    synth.setCurrentPlaybackSampleRate(44100.0);

    auto& articulations = InstrumentSynthTestAccess::articulations(synth);
    articulations.emplace_back(1, 1);
    articulations.back().pocValue = 20;
    articulations.emplace_back(1, 2);
    articulations.back().pocValue = 80;
    articulations.back().layers.emplace_back();
    articulations.back().velocityToLayer.fill(0);

    const auto directMic = makeMicSpec(KitLib::MicSpec::Type::Direct);
    const auto roomMic = makeMicSpec(KitLib::MicSpec::Type::Room);
    KitLib::SampleVariantSpec sourceDirectVariant(nullptr, {}, 'a', directMic);
    KitLib::SampleVariantSpec sourceRoomVariant(nullptr, {}, 'a', roomMic);
    KitLib::SampleVariantSpec targetDirectVariant(nullptr, {}, 'a', directMic);
    SampledSound sourceDirect(nullptr, &sourceDirectVariant);
    SampledSound sourceRoom(nullptr, &sourceRoomVariant);
    SampledSound targetDirect(nullptr, &targetDirectVariant);
    sourceDirect.sampleRate = 44100.0;
    sourceRoom.sampleRate = 44100.0;
    targetDirect.sampleRate = 44100.0;
    sourceDirect.micBufferChannelStart = 0;
    sourceRoom.micBufferChannelStart = 2;
    targetDirect.micBufferChannelStart = 0;
    sourceDirect.decayEnvelope = makeTransitionEnvelope();
    sourceRoom.decayEnvelope = makeTransitionEnvelope();
    targetDirect.decayEnvelope = makeTransitionEnvelope();
    targetDirect.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
    targetDirect.loaded.store(true);
    articulations.back().layers.back().samples.push_back({ &targetDirect, 1 });

    auto* sourceDirectVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* sourceRoomVoice = synth.addVoice(new InstrumentSynthVoice());
    synth.addVoice(new InstrumentSynthVoice());
    synth.addVoice(new InstrumentSynthVoice());
    const auto eventId = synth.allocateEventId();
    synth.startVoice(sourceDirectVoice, &sourceDirect, 1, 42, 0.8f, eventId);
    synth.startVoice(sourceRoomVoice, &sourceRoom, 1, 42, 0.8f, eventId);
    InstrumentSynthTestAccess::setVoiceArticulation(*sourceDirectVoice, 0);
    InstrumentSynthTestAccess::setVoiceArticulation(*sourceRoomVoice, 0);
    sourceDirectVoice->cachedEstimatedDb = -26.0f;
    sourceRoomVoice->cachedEstimatedDb = -30.0f;

    REQUIRE_FALSE(InstrumentSynthTestAccess::transition(synth, 1, 1));
    REQUIRE(sourceDirectVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE(sourceRoomVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
}
