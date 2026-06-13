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

    static InstrumentSynth::HiHatTransitionResult transition(InstrumentSynth& synth, int midiChannel, InstrumentSynth::ArtRtIdx targetArtIdx) {
        return synth.transitionHiHatGroups(midiChannel, targetArtIdx);
    }

    static void configureController(InstrumentSynth& synth, int controllerNumber, uint8_t lowArticulationIdx, uint8_t highArticulationIdx) {
        synth.ccToArticulationMask[static_cast<size_t>(controllerNumber)] = true;
        auto& mapping = synth.ccToArticulationTable[static_cast<size_t>(controllerNumber)];
        for (size_t controllerValue = 0; controllerValue < mapping.size(); ++controllerValue)
            mapping[controllerValue] = controllerValue < 64 ? lowArticulationIdx : highArticulationIdx;
    }

    static void advanceRenderedSamples(InstrumentSynth& synth, uint64_t sampleCount) {
        synth.renderedSampleCount += sampleCount;
        synth.processPendingHiHatControllers();
    }

    static void configureCachedController(InstrumentSynth& synth, int controllerNumber, int controllerValue, InstrumentSynth::ArtRtIdx articulationIdx) {
        synth.lastCcValue[static_cast<size_t>(controllerNumber)] = controllerValue;
        synth.lastCcArticulation[static_cast<size_t>(controllerNumber)] = articulationIdx;
    }

    static InstrumentSynth::ArtRtIdx pendingArticulation(const InstrumentSynth& synth, int controllerNumber) {
        return synth.pendingCcArticulation[static_cast<size_t>(controllerNumber)];
    }

    static void mapNote(InstrumentSynth& synth, int midiNoteNumber, InstrumentSynth::ArtRtIdx articulationIdx) {
        synth.noteToArticulation[static_cast<size_t>(midiNoteNumber)] = articulationIdx;
    }

    static bool triggerFootSample(InstrumentSynth& synth, int midiChannel, InstrumentSynth::ArtRtIdx desiredArtIdx, HiHatTransitionPolicy::Direction direction, int controllerDelta) {
        return synth.triggerHiHatFootSample(midiChannel, desiredArtIdx, direction, controllerDelta);
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

void publishTestBufferMetadata(SampledSound& sound) {
    sound.sampleLengthSamples = sound.buffer != nullptr ? sound.buffer->getNumSamples() : 0;
    sound.sampleLengthMs = sound.sampleRate > 0.0
        ? static_cast<float>((static_cast<double>(sound.sampleLengthSamples) / sound.sampleRate) * 1000.0)
        : 0.0f;
    sound.transitionAudioValid = sound.buffer != nullptr;
    sound.loaded.store(sound.buffer != nullptr);
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
    publishTestBufferMetadata(targetDirect);
    publishTestBufferMetadata(targetRoom);
    articulations.back().layers.back().samples.push_back({ &targetDirect, 1 });
    articulations.back().layers.back().samples.push_back({ &targetRoom, 1 });
    articulations.back().layers.back().rebuildTransitionLookup();

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

    REQUIRE(InstrumentSynthTestAccess::transition(synth, 1, 1).status == InstrumentSynth::HiHatTransitionStatus::Completed);
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
    publishTestBufferMetadata(targetDirect);
    articulations.back().layers.back().samples.push_back({ &targetDirect, 1 });
    articulations.back().layers.back().rebuildTransitionLookup();

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

    REQUIRE(InstrumentSynthTestAccess::transition(synth, 1, 1).status == InstrumentSynth::HiHatTransitionStatus::Skipped);
    REQUIRE(sourceDirectVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE(sourceRoomVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
}

TEST_CASE("InstrumentSynth - hi-hat controller coalescing retains the latest value", "[InstrumentSynth][HiHatTransition]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatControllerSmoothingSec.store(0.010f);

    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
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
    REQUIRE(counters.skipped == 0);
    REQUIRE(counters.completed == 0);
}

TEST_CASE("InstrumentSynth - skipped opening retries without another CC movement", "[InstrumentSynth][HiHatTransition]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatControllerSmoothingSec.store(0.0f);
    engine.engineSettings.hihatTransitionAttackGuardSec.store(0.025f);
    engine.engineSettings.hihatTransitionMinRemainingSec.store(0.01f);
    engine.engineSettings.hihatTransitionSilenceFloorDb.store(-90.0f);

    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    InstrumentSynth synth(&engine, slot);
    synth.setCurrentPlaybackSampleRate(1000.0);

    auto& articulations = InstrumentSynthTestAccess::articulations(synth);
    articulations.emplace_back(1, 1);
    articulations.back().pocValue = 90;
    articulations.emplace_back(1, 2);
    articulations.back().pocValue = 10;
    articulations.back().layers.emplace_back();
    articulations.back().velocityToLayer.fill(0);
    InstrumentSynthTestAccess::configureController(synth, 4, 1, 0);
    InstrumentSynthTestAccess::configureCachedController(synth, 4, 100, 0);

    const auto directMic = makeMicSpec(KitLib::MicSpec::Type::Direct);
    KitLib::SampleVariantSpec sourceVariant(nullptr, {}, 'a', directMic);
    KitLib::SampleVariantSpec targetVariant(nullptr, {}, 'a', directMic);
    SampledSound sourceSound(nullptr, &sourceVariant);
    SampledSound targetSound(nullptr, &targetVariant);
    sourceSound.sampleRate = 1000.0;
    targetSound.sampleRate = 1000.0;
    sourceSound.decayEnvelope = makeTransitionEnvelope();
    targetSound.decayEnvelope = makeTransitionEnvelope();
    targetSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 2000);
    publishTestBufferMetadata(targetSound);
    articulations[1].layers[0].samples.push_back({ &targetSound, 1 });
    articulations[1].layers[0].rebuildTransitionLookup();

    auto* sourceVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* targetVoice = synth.addVoice(new InstrumentSynthVoice());
    synth.startVoice(sourceVoice, &sourceSound, 1, 42, 0.8f);
    InstrumentSynthTestAccess::setVoiceArticulation(*sourceVoice, 0);
    sourceVoice->cachedEstimatedDb = -24.0f;

    synth.handleController(1, 4, 10);
    REQUIRE(InstrumentSynthTestAccess::pendingArticulation(synth, 4) == 1);
    REQUIRE(sourceVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE_FALSE(targetVoice->isVoiceActive());

    sourceVoice->sourceSamplePosition = 100.0;
    InstrumentSynthTestAccess::advanceRenderedSamples(synth, 1);

    REQUIRE(InstrumentSynthTestAccess::pendingArticulation(synth, 4) == -1);
    REQUIRE(sourceVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(targetVoice->getRuntimeArticulationIndex() == 1);
}

TEST_CASE("InstrumentSynth - openness transition preserves the struck zone", "[InstrumentSynth][HiHatTransition]") {
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
    articulations.back().pocValue = 10;
    articulations.emplace_back(1, 2);
    articulations.back().pocValue = 90;
    articulations.emplace_back(2, 1);
    articulations.back().pocValue = 90;
    articulations.emplace_back(2, 2);
    articulations.back().pocValue = 10;
    articulations.back().layers.emplace_back();
    articulations.back().velocityToLayer.fill(0);

    const auto directMic = makeMicSpec(KitLib::MicSpec::Type::Direct);
    KitLib::SampleVariantSpec sourceVariant(nullptr, {}, 'a', directMic);
    KitLib::SampleVariantSpec targetVariant(nullptr, {}, 'a', directMic);
    SampledSound sourceSound(nullptr, &sourceVariant);
    SampledSound targetSound(nullptr, &targetVariant);
    sourceSound.sampleRate = 44100.0;
    targetSound.sampleRate = 44100.0;
    sourceSound.decayEnvelope = makeTransitionEnvelope();
    targetSound.decayEnvelope = makeTransitionEnvelope();
    targetSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
    publishTestBufferMetadata(targetSound);
    articulations[3].layers[0].samples.push_back({ &targetSound, 1 });
    articulations[3].layers[0].rebuildTransitionLookup();

    auto* sourceVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* targetVoice = synth.addVoice(new InstrumentSynthVoice());
    synth.startVoice(sourceVoice, &sourceSound, 1, 42, 0.8f);
    InstrumentSynthTestAccess::setVoiceArticulation(*sourceVoice, 2);
    sourceVoice->sourceSamplePosition = 4410.0;
    sourceVoice->cachedEstimatedDb = -24.0f;

    const auto result = InstrumentSynthTestAccess::transition(synth, 1, 0);
    REQUIRE(result.status == InstrumentSynth::HiHatTransitionStatus::Completed);
    REQUIRE(targetVoice->getRuntimeArticulationIndex() == 3);
}

TEST_CASE("InstrumentSynth - openness transition falls back to mapped articulation when the source zone is unavailable", "[InstrumentSynth][HiHatTransition]") {
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
    articulations.back().pocValue = 10;
    articulations.back().layers.emplace_back();
    articulations.back().velocityToLayer.fill(0);
    articulations.emplace_back(2, 2);
    articulations.back().pocValue = 90;

    const auto directMic = makeMicSpec(KitLib::MicSpec::Type::Direct);
    KitLib::SampleVariantSpec sourceVariant(nullptr, {}, 'a', directMic);
    KitLib::SampleVariantSpec targetVariant(nullptr, {}, 'a', directMic);
    SampledSound sourceSound(nullptr, &sourceVariant);
    SampledSound targetSound(nullptr, &targetVariant);
    sourceSound.sampleRate = 44100.0;
    targetSound.sampleRate = 44100.0;
    sourceSound.decayEnvelope = makeTransitionEnvelope();
    targetSound.decayEnvelope = makeTransitionEnvelope();
    targetSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
    publishTestBufferMetadata(targetSound);
    articulations[0].layers[0].samples.push_back({ &targetSound, 1 });
    articulations[0].layers[0].rebuildTransitionLookup();

    auto* sourceVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* targetVoice = synth.addVoice(new InstrumentSynthVoice());
    synth.startVoice(sourceVoice, &sourceSound, 1, 42, 0.8f);
    InstrumentSynthTestAccess::setVoiceArticulation(*sourceVoice, 1);
    sourceVoice->sourceSamplePosition = 4410.0;
    sourceVoice->cachedEstimatedDb = -24.0f;

    REQUIRE(InstrumentSynthTestAccess::transition(synth, 1, 0).status == InstrumentSynth::HiHatTransitionStatus::Completed);
    REQUIRE(targetVoice->getRuntimeArticulationIndex() == 0);
}

TEST_CASE("InstrumentSynth - group fallback uses one loaded variant for every mic", "[InstrumentSynth][HiHatTransition]") {
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
    articulations.back().pocValue = 90;
    articulations.emplace_back(1, 2);
    articulations.back().pocValue = 10;
    articulations.back().layers.emplace_back();
    articulations.back().velocityToLayer.fill(0);

    const auto directMic = makeMicSpec(KitLib::MicSpec::Type::Direct);
    const auto roomMic = makeMicSpec(KitLib::MicSpec::Type::Room);
    KitLib::SampleVariantSpec sourceDirectVariant(nullptr, {}, 'a', directMic);
    KitLib::SampleVariantSpec sourceRoomVariant(nullptr, {}, 'a', roomMic);
    KitLib::SampleVariantSpec targetDirectVariant(nullptr, {}, 'b', directMic);
    KitLib::SampleVariantSpec targetRoomVariant(nullptr, {}, 'b', roomMic);
    SampledSound sourceDirect(nullptr, &sourceDirectVariant);
    SampledSound sourceRoom(nullptr, &sourceRoomVariant);
    SampledSound targetDirect(nullptr, &targetDirectVariant);
    SampledSound targetRoom(nullptr, &targetRoomVariant);
    for (auto* sound : { &sourceDirect, &sourceRoom, &targetDirect, &targetRoom }) {
        sound->sampleRate = 44100.0;
        sound->decayEnvelope = makeTransitionEnvelope();
    }
    sourceDirect.micBufferChannelStart = 0;
    sourceRoom.micBufferChannelStart = 2;
    targetDirect.micBufferChannelStart = 0;
    targetRoom.micBufferChannelStart = 2;
    targetDirect.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
    targetRoom.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
    publishTestBufferMetadata(targetDirect);
    publishTestBufferMetadata(targetRoom);
    articulations[1].layers[0].samples.push_back({ &targetDirect, 2 });
    articulations[1].layers[0].samples.push_back({ &targetRoom, 2 });
    articulations[1].layers[0].rebuildTransitionLookup();

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
    sourceRoomVoice->sourceSamplePosition = 4410.0;
    sourceDirectVoice->cachedEstimatedDb = -24.0f;
    sourceRoomVoice->cachedEstimatedDb = -28.0f;

    REQUIRE(InstrumentSynthTestAccess::transition(synth, 1, 1).status == InstrumentSynth::HiHatTransitionStatus::Completed);
    REQUIRE(targetDirectVoice->getCurrentlyPlayingSound() == &targetDirect);
    REQUIRE(targetRoomVoice->getCurrentlyPlayingSound() == &targetRoom);
}

TEST_CASE("InstrumentSynth - foot voices are excluded and foot mode triggers mapped samples", "[InstrumentSynth][HiHatTransition]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);

    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    InstrumentSynth synth(&engine, slot);
    synth.setCurrentPlaybackSampleRate(44100.0);

    auto& articulations = InstrumentSynthTestAccess::articulations(synth);
    articulations.emplace_back(1, 1);
    articulations.back().pocValue = 10;
    articulations.emplace_back(1, 2);
    articulations.back().pocValue = 90;
    articulations.emplace_back(3, 3);
    articulations.back().pocValue = 100;
    articulations.back().isFootEvent = true;
    articulations.back().isFootSplash = true;
    articulations.back().layers.emplace_back();
    articulations.back().velocityToLayer.fill(0);
    InstrumentSynthTestAccess::mapNote(synth, 44, 2);

    const auto directMic = makeMicSpec(KitLib::MicSpec::Type::Direct);
    KitLib::SampleVariantSpec footVariant(nullptr, {}, 'a', directMic);
    SampledSound footSound(nullptr, &footVariant);
    footSound.sampleRate = 44100.0;
    footSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 44100);
    publishTestBufferMetadata(footSound);
    articulations[2].layers[0].samples.push_back({ &footSound, 1 });
    articulations[2].layers[0].rebuildTransitionLookup();

    auto* footVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* spareVoice = synth.addVoice(new InstrumentSynthVoice());
    synth.startVoice(footVoice, &footSound, 1, 44, 0.8f);
    InstrumentSynthTestAccess::setVoiceArticulation(*footVoice, 2);
    footVoice->cachedEstimatedDb = -20.0f;

    REQUIRE(InstrumentSynthTestAccess::transition(synth, 1, 1).status == InstrumentSynth::HiHatTransitionStatus::NoRelevantTail);
    REQUIRE(footVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    footVoice->killImmediately();

    REQUIRE(InstrumentSynthTestAccess::triggerFootSample(
        synth,
        1,
        1,
        HiHatTransitionPolicy::Direction::Closing,
        40));
    REQUIRE((footVoice->getRuntimeArticulationIndex() == 2
        || spareVoice->getRuntimeArticulationIndex() == 2));

    engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
    footVoice->killImmediately();
    spareVoice->killImmediately();
    REQUIRE_FALSE(InstrumentSynthTestAccess::triggerFootSample(
        synth,
        1,
        1,
        HiHatTransitionPolicy::Direction::Closing,
        40));
}

TEST_CASE("InstrumentSynth - opening a ringing pedal sample creates an open tail", "[InstrumentSynth][HiHatTransition]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatTransitionAttackGuardSec.store(0.0f);
    engine.engineSettings.hihatTransitionMinRemainingSec.store(0.01f);
    engine.engineSettings.hihatTransitionSilenceFloorDb.store(-90.0f);
    engine.engineSettings.hihatMissingDecayPolicy.store(HiHatMissingDecayPolicy::KeepTail);

    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    InstrumentSynth synth(&engine, slot);
    synth.setCurrentPlaybackSampleRate(44100.0);

    auto& articulations = InstrumentSynthTestAccess::articulations(synth);
    articulations.emplace_back(1, 1);
    articulations.back().pocValue = 20;
    articulations.back().layers.emplace_back();
    articulations.back().velocityToLayer.fill(0);
    articulations.emplace_back(2, 2);
    articulations.back().pocValue = 90;
    articulations.back().isFootEvent = true;

    const auto directMic = makeMicSpec(KitLib::MicSpec::Type::Direct);
    KitLib::SampleVariantSpec pedalVariant(nullptr, {}, 'a', directMic);
    KitLib::SampleVariantSpec openVariant(nullptr, {}, 'a', directMic);
    SampledSound pedalSound(nullptr, &pedalVariant);
    SampledSound openSound(nullptr, &openVariant);
    pedalSound.sampleRate = 44100.0;
    openSound.sampleRate = 44100.0;
    openSound.decayEnvelope = makeTransitionEnvelope();
    openSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
    publishTestBufferMetadata(openSound);
    articulations[0].layers[0].samples.push_back({ &openSound, 1 });
    articulations[0].layers[0].rebuildTransitionLookup();

    auto* pedalVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* openVoice = synth.addVoice(new InstrumentSynthVoice());
    synth.startVoice(pedalVoice, &pedalSound, 1, 44, 0.8f);
    InstrumentSynthTestAccess::setVoiceArticulation(*pedalVoice, 1);
    pedalVoice->sourceSamplePosition = 4410.0;
    pedalVoice->cachedEstimatedDb = -24.0f;

    REQUIRE(InstrumentSynthTestAccess::transition(synth, 1, 0).status == InstrumentSynth::HiHatTransitionStatus::Completed);
    REQUIRE(pedalVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(openVoice->getRuntimeArticulationIndex() == 0);
}
