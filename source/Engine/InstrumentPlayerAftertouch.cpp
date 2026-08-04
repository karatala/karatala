#include "InstrumentPlayer.h"
#include "../Config/PluginLogger.h"
#include "DrumKitEngine.h"
#include "HiHatTransitionPolicy.h"
#include <cmath>

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
    if (this->parent == nullptr) return false;
    static constexpr size_t MAX_CHOKE_VOICES = 64;

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
    const int targetVelocityLevelIdx = this->resolveVelocityLevel(chokeArt, midiVelocity);
    if (targetVelocityLevelIdx < 0 || targetVelocityLevelIdx >= static_cast<int>(chokeArt.velocityLevels.size())) return false;
    auto& targetVelocityLevel = chokeArt.velocityLevels[static_cast<size_t>(targetVelocityLevelIdx)];
    auto* logicalHit = this->chooseLogicalHit(targetVelocityLevel);
    if (logicalHit == nullptr || logicalHit->sampleEntryIndexes.empty() || logicalHit->sampleEntryIndexes.size() > MAX_CHOKE_VOICES) return false;

    const auto& settings = this->parent->getEngineSettings();
    const float targetVelocityGain = referenceVoice->getVelocityGain();
    const float targetVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, targetVelocityGain), -100.0f);
    std::array<double, MAX_CHOKE_VOICES> targetOffsets {};
    std::array<InstrumentPlayerVoice*, MAX_CHOKE_VOICES> reservedVoices {};

    auto findSourceVoice = [this, referenceSound, midiChannel, midiNoteNumber](int micBufferChannelStart, KitLib::MicSpec::Type fallbackMicType) -> InstrumentPlayerVoice* {
        InstrumentPlayerVoice* bestVoice = nullptr;
        float bestDb = -100.0f;
        for (auto* candidateVoice : this->voices) {
            if (candidateVoice == nullptr || !candidateVoice->isVoiceActive() || candidateVoice->getEnvelopeState() != InstrumentPlayerVoice::EnvelopeState::Playing) continue;
            if (candidateVoice->getCurrentlyPlayingNote() != midiNoteNumber || (midiChannel > 0 && !candidateVoice->isPlayingChannel(midiChannel))) continue;
            auto* candidateSound = candidateVoice->getCurrentlyPlayingSound();
            if (candidateSound == nullptr || candidateSound->isAftertouchChokeTarget || candidateSound->instrumentSpec != referenceSound->instrumentSpec) continue;
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
    };

    auto calculateTargetOffset = [this, &settings, targetVelocityDb](const InstrumentPlayerVoice& sourceVoice, const SampledSound& targetSound) -> std::optional<double> {
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
    };

    for (size_t logicalEntryIdx = 0; logicalEntryIdx < logicalHit->sampleEntryIndexes.size(); ++logicalEntryIdx) {
        const size_t sampleEntryIdx = logicalHit->sampleEntryIndexes[logicalEntryIdx];
        if (sampleEntryIdx >= targetVelocityLevel.samples.size()) return false;
        auto* targetSound = targetVelocityLevel.samples[sampleEntryIdx].sound;
        if (targetSound == nullptr || !targetSound->loaded.load(std::memory_order_acquire) || targetSound->buffer == nullptr) return false;

        std::optional<double> selectedOffset;
        auto* exactSourceVoice = findSourceVoice(targetSound->micBufferChannelStart, KitLib::MicSpec::Type::Unknown);
        if (exactSourceVoice != nullptr) selectedOffset = calculateTargetOffset(*exactSourceVoice, *targetSound);
        const std::array<KitLib::MicSpec::Type, 2> fallbackMicTypes { KitLib::MicSpec::Type::Direct, KitLib::MicSpec::Type::Overhead };
        for (const auto fallbackMicType : fallbackMicTypes) {
            if (selectedOffset.has_value()) break;
            auto* fallbackVoice = findSourceVoice(-1, fallbackMicType);
            if (fallbackVoice != nullptr && fallbackVoice != exactSourceVoice) selectedOffset = calculateTargetOffset(*fallbackVoice, *targetSound);
        }
        if (!selectedOffset.has_value()) return false;
        targetOffsets[logicalEntryIdx] = *selectedOffset;
    }

    size_t reservedVoiceCount = 0;
    for (size_t logicalEntryIdx = 0; logicalEntryIdx < logicalHit->sampleEntryIndexes.size(); ++logicalEntryIdx) {
        for (auto* candidateVoice : this->voices) {
            if (candidateVoice == nullptr || candidateVoice->isVoiceActive()) continue;
            bool alreadyReserved = false;
            for (size_t reservedIdx = 0; reservedIdx < reservedVoiceCount; ++reservedIdx) {
                if (reservedVoices[reservedIdx] == candidateVoice) alreadyReserved = true;
            }
            if (alreadyReserved) continue;
            reservedVoices[reservedVoiceCount++] = candidateVoice;
            break;
        }
        if (reservedVoiceCount <= logicalEntryIdx) return false;
    }

    const ArtRtIdx sourceArtIdx = this->getVoiceRuntimeArticulationIdx(*referenceVoice);
    const auto& voiceParameters = sourceArtIdx >= 0 && static_cast<size_t>(sourceArtIdx) < this->runtimeArticulations.size() ? this->runtimeArticulations[static_cast<size_t>(sourceArtIdx)].parameters : chokeArt.parameters;
    for (size_t logicalEntryIdx = 0; logicalEntryIdx < logicalHit->sampleEntryIndexes.size(); ++logicalEntryIdx) {
        const auto& targetEntry = targetVelocityLevel.samples[logicalHit->sampleEntryIndexes[logicalEntryIdx]];
        auto* targetSound = targetEntry.sound;
        auto* targetVoice = reservedVoices[logicalEntryIdx];
        this->updateSoundMicPlacementGains(*targetSound, voiceParameters.pan);
        targetVoice->hitGroupId = targetSound->micBufferChannelStart;
        targetVoice->setPitchRatioMultiplier(std::pow(2.0, static_cast<double>(voiceParameters.tuningCents) / 1200.0));
        targetVoice->setAttackTimeSec(voiceParameters.attackMs >= 0 ? static_cast<float>(voiceParameters.attackMs) / 1000.0f : 0.001f);
        targetVoice->setReleaseTimeSec(voiceParameters.decayMs >= 0 ? static_cast<float>(voiceParameters.decayMs) / 1000.0f : 0.1f);
        targetVoice->setUseOneShotDecayEnvelope(voiceParameters.decayMs >= 0);
        this->startVoice(targetVoice, targetSound, midiChannel, midiNoteNumber, targetVelocityGain);
        targetVoice->artNameId = chokeArt.nameId;
        targetVoice->artExprId = chokeArt.exprId;
        targetVoice->artRuntimeIdx = sourceArtIdx >= 0 ? sourceArtIdx : chokeArtIdx;
        targetVoice->sourceSamplePosition = targetOffsets[logicalEntryIdx];
        targetVoice->setVelocityGain(targetVelocityGain);
        targetVoice->setMidiVelocity(midiVelocity);
    }

    const float crossfadeSec = settings.chokeMuteTailSec.load();
    for (auto* voice : this->voices) {
        if (voice == nullptr || !voice->isVoiceActive() || voice->getCurrentlyPlayingNote() != midiNoteNumber) continue;
        if (midiChannel > 0 && !voice->isPlayingChannel(midiChannel)) continue;
        const auto* sound = voice->getCurrentlyPlayingSound();
        if (sound != nullptr && !sound->isAftertouchChokeTarget) voice->requestFadeOut(crossfadeSec);
    }

    if (LOG_ENABLED(Verbose)) LOG(Verbose, "Aftertouch choke sample transition: inst=" + this->getId() + " ch=" + juce::String(midiChannel) + " note=" + juce::String(midiNoteNumber) + " voices=" + juce::String(static_cast<int>(logicalHit->sampleEntryIndexes.size())));
    return true;
}
