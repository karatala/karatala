#include "InstrumentPlayer.h"
#include "../Config/PluginLogger.h"
#include "DrumKitEngine.h"
#include "HiHatTransitionPolicy.h"
#include <cmath>

struct InstrumentPlayer::AftertouchChokePlan {
    static constexpr size_t maxVoices = 64;

    std::array<SampledSound*, maxVoices> targetSounds {};
    std::array<InstrumentPlayerVoice*, maxVoices> targetVoices {};
    std::array<double, maxVoices> targetOffsets {};
    size_t targetCount = 0;
    juce::uint8 midiVelocity = 0;
    float targetVelocityGain = 0.0f;
    float crossfadeSec = 0.0f;
    int pan = 0;
    int tuningCents = 0;
    int attackMs = -1;
    int decayMs = -1;
    uint32_t artNameId = 0;
    uint32_t artExprId = 0;
    ArtRtIdx artRuntimeIdx = -1;
};

void InstrumentPlayer::applyAftertouch(int midiChannel, int midiNoteNumber) {
    const auto& paVal = this->parent->getLatestPolyphonicAftertouchValue(midiNoteNumber);
    if (paVal > 0) this->handleAftertouch(midiChannel, midiNoteNumber, paVal);
}

void InstrumentPlayer::resetAftertouchChokeGesture(int midiChannel, int midiNoteNumber) {
    const int firstChannelIdx = midiChannel > 0 ? juce::jlimit(0, 15, midiChannel - 1) : 0;
    const int lastChannelIdx = midiChannel > 0 ? firstChannelIdx : 15;
    for (int channelIdx = firstChannelIdx; channelIdx <= lastChannelIdx; ++channelIdx) {
        if (midiNoteNumber >= 0 && midiNoteNumber < 128) this->aftertouchChokeTransitionCommitted[static_cast<size_t>(channelIdx)][static_cast<size_t>(midiNoteNumber)] = false;
        else this->aftertouchChokeTransitionCommitted[static_cast<size_t>(channelIdx)].fill(false);
    }
}

void InstrumentPlayer::handleAftertouch(int midiChannel, int midiNoteNumber, int aftertouchValue) {
    if (midiNoteNumber < 0 || midiNoteNumber >= 128) return;
    const int pressureThreshold = this->parent != nullptr ? this->parent->getEngineSettings().polyAftertouchMinPressure.load() : 10;
    const int channelIdx = midiChannel > 0 ? juce::jlimit(0, 15, midiChannel - 1) : 0;
    auto& transitionCommitted = this->aftertouchChokeTransitionCommitted[static_cast<size_t>(channelIdx)][static_cast<size_t>(midiNoteNumber)];

    if (aftertouchValue < pressureThreshold) {
        const bool wasCommitted = transitionCommitted;
        transitionCommitted = false;
        if (wasCommitted) return;
    } else {
        if (transitionCommitted) return;
        if (this->triggerAftertouchChokeSample(midiChannel, midiNoteNumber)) {
            transitionCommitted = true;
            return;
        }
    }

    for (auto* voice : this->voices) {
        if (voice == nullptr || voice->getCurrentlyPlayingNote() != midiNoteNumber) continue;
        if (midiChannel > 0 && !voice->isPlayingChannel(midiChannel)) continue;
        voice->aftertouchChanged(aftertouchValue);
    }
}

bool InstrumentPlayer::triggerAftertouchChokeSample(int midiChannel, int midiNoteNumber) {
    AftertouchChokePlan plan;
    if (!this->prepareAftertouchChokePlan(midiChannel, midiNoteNumber, plan)) return false;
    this->commitAftertouchChokePlan(midiChannel, midiNoteNumber, plan);
    return true;
}

bool InstrumentPlayer::prepareAftertouchChokePlan(int midiChannel, int midiNoteNumber, AftertouchChokePlan& plan) {
    if (this->parent == nullptr) return false;
    InstrumentPlayerVoice* referenceVoice = nullptr;
    bool referenceIsDirect = false;
    float referenceDb = -100.0f;
    for (auto* voice : this->voices) {
        if (voice == nullptr || !voice->isVoiceActive() || voice->getEnvelopeState() != InstrumentPlayerVoice::EnvelopeState::Playing) continue;
        if (voice->getCurrentlyPlayingNote() != midiNoteNumber || (midiChannel > 0 && !voice->isPlayingChannel(midiChannel))) continue;
        auto* sound = voice->getCurrentlyPlayingSound();
        if (sound == nullptr || sound->isAftertouchChokeTarget || sound->aftertouchChokeArticulationIndex < 0) continue;
        if (static_cast<size_t>(sound->aftertouchChokeArticulationIndex) >= this->runtimeArticulations.size()) continue;

        const bool isDirect = sound->sampleSpec != nullptr && sound->sampleSpec->micSpec.type == KitLib::MicSpec::Type::Direct;
        const float currentDb = voice->getEstimatedCurrentDb();
        if (referenceVoice == nullptr || (isDirect && !referenceIsDirect) || (isDirect == referenceIsDirect && currentDb > referenceDb)) {
            referenceVoice = voice;
            referenceIsDirect = isDirect;
            referenceDb = currentDb;
        }
    }
    if (referenceVoice == nullptr) return false;

    auto* referenceSound = referenceVoice->getCurrentlyPlayingSound();
    if (referenceSound == nullptr) return false;
    const auto chokeArtIdx = static_cast<ArtRtIdx>(referenceSound->aftertouchChokeArticulationIndex);
    auto& chokeArt = this->runtimeArticulations[static_cast<size_t>(chokeArtIdx)];
    const juce::uint8 midiVelocity = referenceVoice->getMidiVelocity();
    this->recordSampleLoadDemand(chokeArt, chokeArt.velocityToLevel[static_cast<size_t>(midiVelocity)]);
    const int targetVelocityLevelIdx = this->resolveVelocityLevel(chokeArt, midiVelocity);
    if (targetVelocityLevelIdx < 0 || targetVelocityLevelIdx >= static_cast<int>(chokeArt.velocityLevels.size())) return false;
    auto& targetVelocityLevel = chokeArt.velocityLevels[static_cast<size_t>(targetVelocityLevelIdx)];
    auto* logicalHit = this->chooseLogicalHit(targetVelocityLevel);
    if (logicalHit == nullptr || logicalHit->sampleEntryIndexes.empty() || logicalHit->sampleEntryIndexes.size() > AftertouchChokePlan::maxVoices) return false;

    const auto& settings = this->parent->getEngineSettings();
    const float targetVelocityGain = referenceVoice->getVelocityGain();
    const float targetVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, targetVelocityGain), -100.0f);
    plan.targetCount = logicalHit->sampleEntryIndexes.size();
    for (size_t logicalEntryIdx = 0; logicalEntryIdx < logicalHit->sampleEntryIndexes.size(); ++logicalEntryIdx) {
        const size_t sampleEntryIdx = logicalHit->sampleEntryIndexes[logicalEntryIdx];
        if (sampleEntryIdx >= targetVelocityLevel.samples.size()) return false;
        auto* targetSound = targetVelocityLevel.samples[sampleEntryIdx].sound;
        if (targetSound == nullptr || !targetSound->loaded.load(std::memory_order_acquire) || targetSound->buffer == nullptr) return false;

        std::optional<double> selectedOffset;
        auto* exactSourceVoice = this->findAftertouchChokeSourceVoice(*referenceSound, midiChannel, midiNoteNumber, targetSound->micBufferChannelStart, KitLib::MicSpec::Type::Unknown);
        if (exactSourceVoice != nullptr) selectedOffset = this->calculateAftertouchChokeTargetOffset(*exactSourceVoice, *targetSound, targetVelocityDb, settings);
        const std::array<KitLib::MicSpec::Type, 2> fallbackMicTypes { KitLib::MicSpec::Type::Direct, KitLib::MicSpec::Type::Overhead };
        for (const auto fallbackMicType : fallbackMicTypes) {
            if (selectedOffset.has_value()) break;
            auto* fallbackVoice = this->findAftertouchChokeSourceVoice(*referenceSound, midiChannel, midiNoteNumber, -1, fallbackMicType);
            if (fallbackVoice != nullptr && fallbackVoice != exactSourceVoice) selectedOffset = this->calculateAftertouchChokeTargetOffset(*fallbackVoice, *targetSound, targetVelocityDb, settings);
        }
        if (!selectedOffset.has_value()) return false;
        plan.targetSounds[logicalEntryIdx] = targetSound;
        plan.targetOffsets[logicalEntryIdx] = *selectedOffset;
    }

    size_t reservedVoiceCount = 0;
    for (size_t logicalEntryIdx = 0; logicalEntryIdx < logicalHit->sampleEntryIndexes.size(); ++logicalEntryIdx) {
        for (auto* candidateVoice : this->voices) {
            if (candidateVoice == nullptr || candidateVoice->isVoiceActive()) continue;
            bool alreadyReserved = false;
            for (size_t reservedIdx = 0; reservedIdx < reservedVoiceCount; ++reservedIdx) {
                if (plan.targetVoices[reservedIdx] == candidateVoice) alreadyReserved = true;
            }
            if (alreadyReserved) continue;
            plan.targetVoices[reservedVoiceCount++] = candidateVoice;
            break;
        }
        if (reservedVoiceCount <= logicalEntryIdx) return false;
    }

    const ArtRtIdx sourceArtIdx = this->getVoiceRuntimeArticulationIdx(*referenceVoice);
    const auto& voiceParameters = sourceArtIdx >= 0 && static_cast<size_t>(sourceArtIdx) < this->runtimeArticulations.size() ? this->runtimeArticulations[static_cast<size_t>(sourceArtIdx)].parameters : chokeArt.parameters;
    plan.midiVelocity = midiVelocity;
    plan.targetVelocityGain = targetVelocityGain;
    plan.crossfadeSec = settings.chokeMuteTailSec.load();
    plan.pan = voiceParameters.pan;
    plan.tuningCents = voiceParameters.tuningCents;
    plan.attackMs = voiceParameters.attackMs;
    plan.decayMs = voiceParameters.decayMs;
    plan.artNameId = chokeArt.nameId;
    plan.artExprId = chokeArt.exprId;
    plan.artRuntimeIdx = sourceArtIdx >= 0 ? sourceArtIdx : chokeArtIdx;
    return true;
}

void InstrumentPlayer::commitAftertouchChokePlan(int midiChannel, int midiNoteNumber, const AftertouchChokePlan& plan) {
    for (size_t targetIdx = 0; targetIdx < plan.targetCount; ++targetIdx) {
        auto* targetSound = plan.targetSounds[targetIdx];
        auto* targetVoice = plan.targetVoices[targetIdx];
        this->updateSoundMicPlacementGains(*targetSound, plan.pan);
        targetVoice->hitGroupId = targetSound->micBufferChannelStart;
        targetVoice->setPitchRatioMultiplier(std::pow(2.0, static_cast<double>(plan.tuningCents) / 1200.0));
        targetVoice->setAttackTimeSec(plan.attackMs >= 0 ? static_cast<float>(plan.attackMs) / 1000.0f : 0.001f);
        targetVoice->setReleaseTimeSec(plan.decayMs >= 0 ? static_cast<float>(plan.decayMs) / 1000.0f : 0.1f);
        targetVoice->setUseOneShotDecayEnvelope(plan.decayMs >= 0);
        this->startVoice(targetVoice, targetSound, midiChannel, midiNoteNumber, plan.targetVelocityGain);
        targetVoice->artNameId = plan.artNameId;
        targetVoice->artExprId = plan.artExprId;
        targetVoice->artRuntimeIdx = plan.artRuntimeIdx;
        targetVoice->sourceSamplePosition = plan.targetOffsets[targetIdx];
        targetVoice->setVelocityGain(plan.targetVelocityGain);
        targetVoice->setMidiVelocity(plan.midiVelocity);
    }

    for (auto* voice : this->voices) {
        if (voice == nullptr || !voice->isVoiceActive() || voice->getCurrentlyPlayingNote() != midiNoteNumber) continue;
        if (midiChannel > 0 && !voice->isPlayingChannel(midiChannel)) continue;
        const auto* sound = voice->getCurrentlyPlayingSound();
        if (sound != nullptr && !sound->isAftertouchChokeTarget) voice->requestFadeOut(plan.crossfadeSec);
    }

    if (LOG_ENABLED(Verbose)) LOG(Verbose, "Aftertouch choke sample transition: inst=" + this->getId() + " ch=" + juce::String(midiChannel) + " note=" + juce::String(midiNoteNumber) + " voices=" + juce::String(static_cast<int>(plan.targetCount)));
}

InstrumentPlayerVoice* InstrumentPlayer::findAftertouchChokeSourceVoice(const SampledSound& referenceSound, int midiChannel, int midiNoteNumber, int micBufferChannelStart, KitLib::MicSpec::Type fallbackMicType) const {
    InstrumentPlayerVoice* bestVoice = nullptr;
    float bestDb = -100.0f;
    for (auto* candidateVoice : this->voices) {
        if (candidateVoice == nullptr || !candidateVoice->isVoiceActive() || candidateVoice->getEnvelopeState() != InstrumentPlayerVoice::EnvelopeState::Playing) continue;
        if (candidateVoice->getCurrentlyPlayingNote() != midiNoteNumber || (midiChannel > 0 && !candidateVoice->isPlayingChannel(midiChannel))) continue;
        auto* candidateSound = candidateVoice->getCurrentlyPlayingSound();
        if (candidateSound == nullptr || candidateSound->isAftertouchChokeTarget || candidateSound->instrumentSpec != referenceSound.instrumentSpec) continue;
        const bool exactMic = micBufferChannelStart >= 0 && candidateSound->micBufferChannelStart == micBufferChannelStart;
        const bool fallbackMic = micBufferChannelStart < 0 && candidateSound->sampleSpec != nullptr && candidateSound->sampleSpec->micSpec.type == fallbackMicType;
        if (!exactMic && !fallbackMic) continue;
        const float candidateDb = candidateVoice->getEstimatedCurrentDb();
        if (bestVoice == nullptr || candidateDb > bestDb) {
            bestVoice = candidateVoice;
            bestDb = candidateDb;
        }
    }
    return bestVoice;
}

std::optional<double> InstrumentPlayer::calculateAftertouchChokeTargetOffset(const InstrumentPlayerVoice& sourceVoice, const SampledSound& targetSound, float targetVelocityDb, const EngineSettings& settings) const {
    const auto* sourceSound = sourceVoice.getCurrentlyPlayingSound();
    if (sourceSound == nullptr || targetSound.buffer == nullptr) return std::nullopt;
    const double sourceRate = sourceSound->sampleRate > 0.0 ? sourceSound->sampleRate : 44100.0;
    const float elapsedMs = static_cast<float>((sourceVoice.sourceSamplePosition / sourceRate) * 1000.0);
    const float sourceVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, sourceVoice.getVelocityGain()), -100.0f);
    const float currentAudibleDb = sourceSound->decayEnvelope.isValid() ? sourceSound->decayEnvelope.getAbsoluteDbAtMs(elapsedMs) + sourceVelocityDb : sourceVoice.getEstimatedCurrentDb();
    const double targetRate = targetSound.sampleRate > 0.0 ? targetSound.sampleRate : 44100.0;
    const float targetLengthMs = static_cast<float>((static_cast<double>(targetSound.buffer->getNumSamples()) / targetRate) * 1000.0);
    const auto offsetResult = HiHatTransitionPolicy::calculateDecayMatchedOffset({
        &sourceSound->decayEnvelope,
        &targetSound.decayEnvelope,
        elapsedMs,
        currentAudibleDb,
        targetVelocityDb,
        targetLengthMs,
        0.0f,
        settings.hihatTransitionSilenceFloorDb.load(),
        settings.hihatTailMatchAmount.load(),
        settings.hihatTailMatchBiasDb.load(),
        settings.hihatTransitionMinRemainingSec.load() * 1000.0f,
        settings.hihatMissingDecayPolicy.load(),
    });
    if (!offsetResult.succeeded()) return std::nullopt;

    const double offsetSamples = juce::jlimit(0.0, juce::jmax(0.0, static_cast<double>(targetSound.buffer->getNumSamples() - 1)), static_cast<double>(offsetResult.offsetMs) * targetRate / 1000.0);
    const float expectedTargetDb = targetSound.decayEnvelope.isValid() ? targetSound.decayEnvelope.getAbsoluteDbAtMs(offsetResult.offsetMs) : offsetResult.targetRelativeDb;
    if (!this->isHiHatTransitionTargetAudible(targetSound, offsetSamples, expectedTargetDb, settings.hihatTransitionSilenceFloorDb.load())) return std::nullopt;
    return offsetSamples;
}
