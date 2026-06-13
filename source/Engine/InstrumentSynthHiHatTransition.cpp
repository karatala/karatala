#include "InstrumentSynth.h"
#include "DrumKitEngine.h"
#include "HiHatTransitionPolicy.h"

namespace {

constexpr size_t maximumTransitionVoices = 128;
constexpr size_t maximumTransitionGroups = 32;

} // namespace

bool InstrumentSynth::transitionHiHatGroups(int midiChannel, ArtRtIdx targetArtIdx) {
    if (targetArtIdx < 0 || targetArtIdx >= static_cast<int>(this->runtimeArticulations.size()))
        return false;

    const auto& settings = this->parent->getEngineSettings();
    const float openCrossfadeSec = settings.hihatOpenCrossfadeSec.load();
    const float closeCrossfadeSec = settings.hihatCloseCrossfadeSec.load();
    const int maximumActiveGroups = settings.hihatTransitionMaxActiveGroups.load();
    const float tailMatchAmount = settings.hihatTailMatchAmount.load();
    const float tailMatchBiasDb = settings.hihatTailMatchBiasDb.load();
    const float attackGuardMs = settings.hihatTransitionAttackGuardSec.load() * 1000.0f;
    const float minimumRemainingMs = settings.hihatTransitionMinRemainingSec.load() * 1000.0f;
    const float silenceFloorDb = settings.hihatTransitionSilenceFloorDb.load();
    const auto transitionCurve = settings.hihatTransitionCurve.load();
    const auto missingDecayPolicy = settings.hihatMissingDecayPolicy.load();

    std::array<HiHatTransitionPolicy::HitGroupCandidate, maximumTransitionGroups> groupCandidates {};
    size_t groupCandidateCount = 0;

    for (auto* voice : this->voices) {
        if (voice == nullptr
            || !voice->isVoiceActive()
            || voice->getEnvelopeState() != InstrumentSynthVoice::EnvelopeState::Playing
            || voice->getRuntimeArticulationIndex() < 0
            || voice->getRuntimeArticulationIndex() == targetArtIdx
            || (midiChannel > 0 && !voice->isPlayingChannel(midiChannel))) {
            continue;
        }

        size_t groupIdx = 0;
        while (groupIdx < groupCandidateCount
               && groupCandidates[groupIdx].groupId != voice->getEventId()) {
            ++groupIdx;
        }
        if (groupIdx == groupCandidateCount) {
            if (groupCandidateCount >= groupCandidates.size())
                continue;
            groupCandidates[groupIdx] = {
                voice->getEventId(),
                voice->getEventId(),
                voice->getEstimatedCurrentDb(),
                HiHatTransitionPolicy::HitGroupState::Playing,
            };
            ++groupCandidateCount;
        } else {
            groupCandidates[groupIdx].audibleDb = juce::jmax(
                groupCandidates[groupIdx].audibleDb,
                voice->getEstimatedCurrentDb());
        }
    }

    std::array<size_t, 4> selectedGroupIndices {};
    const size_t selectedGroupCount = HiHatTransitionPolicy::selectEligibleHitGroups(
        std::span<const HiHatTransitionPolicy::HitGroupCandidate>(groupCandidates.data(), groupCandidateCount),
        maximumActiveGroups,
        silenceFloorDb,
        selectedGroupIndices);
    size_t completedGroupCount = 0;

    for (size_t selectedGroupIdx = 0; selectedGroupIdx < selectedGroupCount; ++selectedGroupIdx) {
        const auto eventId = groupCandidates[selectedGroupIndices[selectedGroupIdx]].groupId;
        std::array<InstrumentSynthVoice*, maximumTransitionVoices> sourceVoices {};
        std::array<InstrumentSynthVoice*, maximumTransitionVoices> reservedVoices {};
        std::array<const SampleEntry*, maximumTransitionVoices> targetEntries {};
        std::array<HiHatTransitionPolicy::ReferenceMicCandidate, maximumTransitionVoices> referenceCandidates {};
        size_t sourceVoiceCount = 0;
        int sourceArtIdx = -1;
        bool sourceGroupValid = true;

        for (auto* voice : this->voices) {
            if (voice == nullptr || voice->getEventId() != eventId)
                continue;
            if (!voice->isVoiceActive()
                || voice->getEnvelopeState() != InstrumentSynthVoice::EnvelopeState::Playing
                || voice->getRuntimeArticulationIndex() < 0
                || sourceVoiceCount >= sourceVoices.size()) {
                sourceGroupValid = false;
                break;
            }
            if (sourceArtIdx < 0)
                sourceArtIdx = voice->getRuntimeArticulationIndex();
            if (sourceArtIdx != voice->getRuntimeArticulationIndex()) {
                sourceGroupValid = false;
                break;
            }

            auto* sourceSound = voice->getCurrentlyPlayingSound();
            if (sourceSound == nullptr || sourceSound->sampleVariantSpec == nullptr) {
                sourceGroupValid = false;
                break;
            }
            sourceVoices[sourceVoiceCount] = voice;
            referenceCandidates[sourceVoiceCount] = {
                sourceSound->sampleVariantSpec->micSpec.type,
                voice->getEstimatedCurrentDb(),
                true,
            };
            ++sourceVoiceCount;
        }

        if (!sourceGroupValid
            || sourceVoiceCount == 0
            || sourceArtIdx == targetArtIdx
            || sourceArtIdx >= static_cast<int>(this->runtimeArticulations.size())) {
            continue;
        }

        const auto& targetArt = this->runtimeArticulations[static_cast<size_t>(targetArtIdx)];
        const int referenceSourceIdx = HiHatTransitionPolicy::selectReferenceMic(
            std::span<const HiHatTransitionPolicy::ReferenceMicCandidate>(referenceCandidates.data(), sourceVoiceCount));
        if (referenceSourceIdx < 0)
            continue;

        auto* referenceVoice = sourceVoices[static_cast<size_t>(referenceSourceIdx)];
        const int midiVelocity = static_cast<int>(
            juce::jlimit(0.0f, 1.0f, referenceVoice->getVelocityGain()) * 127.0f);
        const int layerIdx = targetArt.velocityToLayer[static_cast<size_t>(midiVelocity)];
        if (layerIdx < 0 || layerIdx >= static_cast<int>(targetArt.layers.size()))
            continue;
        const auto& targetLayer = targetArt.layers[static_cast<size_t>(layerIdx)];

        std::array<HiHatTransitionPolicy::TargetSampleCandidate, maximumTransitionVoices> targetCandidates {};
        const size_t targetCandidateCount = juce::jmin(targetLayer.samples.size(), targetCandidates.size());
        for (size_t targetCandidateIdx = 0; targetCandidateIdx < targetCandidateCount; ++targetCandidateIdx) {
            const auto& targetEntry = targetLayer.samples[targetCandidateIdx];
            targetCandidates[targetCandidateIdx] = {
                targetEntry.variantIdx,
                targetEntry.sound != nullptr ? targetEntry.sound->micBufferChannelStart : -1,
                targetEntry.sound != nullptr
                    && targetEntry.sound->loaded.load(std::memory_order_acquire)
                    && targetEntry.sound->buffer != nullptr,
            };
        }

        size_t resolvedTargetCount = 0;
        for (size_t sourceIdx = 0; sourceIdx < sourceVoiceCount; ++sourceIdx) {
            auto* sourceSound = sourceVoices[sourceIdx]->getCurrentlyPlayingSound();
            const uint8_t variantIdx = KitLib::SampleVariantSpec::letterToIdx(
                sourceSound->sampleVariantSpec->letter);
            const int targetEntryIdx = HiHatTransitionPolicy::findExactTargetSample(
                std::span<const HiHatTransitionPolicy::TargetSampleCandidate>(targetCandidates.data(), targetCandidateCount),
                variantIdx,
                sourceSound->micBufferChannelStart);
            if (targetEntryIdx < 0)
                break;
            targetEntries[sourceIdx] = &targetLayer.samples[static_cast<size_t>(targetEntryIdx)];
            ++resolvedTargetCount;
        }

        size_t reservedVoiceCount = 0;
        if (resolvedTargetCount == sourceVoiceCount) {
            for (auto* candidateVoice : this->voices) {
                if (candidateVoice == nullptr || candidateVoice->isVoiceActive())
                    continue;
                reservedVoices[reservedVoiceCount++] = candidateVoice;
                if (reservedVoiceCount == sourceVoiceCount)
                    break;
            }
        }

        if (!HiHatTransitionPolicy::canCommitGroupTransition(
                sourceVoiceCount, resolvedTargetCount, reservedVoiceCount)) {
            continue;
        }

        auto* referenceSourceSound = referenceVoice->getCurrentlyPlayingSound();
        auto* referenceTargetSound = targetEntries[static_cast<size_t>(referenceSourceIdx)]->sound;
        const double sourceRate = referenceSourceSound->sampleRate > 0.0
            ? referenceSourceSound->sampleRate
            : 44100.0;
        const float elapsedMs = static_cast<float>(
            (referenceVoice->sourceSamplePosition / sourceRate) * 1000.0);
        const float velocityDb = juce::Decibels::gainToDecibels(
            juce::jmax(0.001f, referenceVoice->getVelocityGain()),
            -100.0f);
        const float currentAudibleDb = referenceSourceSound->decayEnvelope.isValid()
            ? referenceSourceSound->decayEnvelope.getAbsoluteDbAtMs(elapsedMs) + velocityDb
            : referenceVoice->getEstimatedCurrentDb();
        const double referenceTargetRate = referenceTargetSound->sampleRate > 0.0
            ? referenceTargetSound->sampleRate
            : 44100.0;
        const float referenceTargetLengthMs = static_cast<float>(
            (static_cast<double>(referenceTargetSound->buffer->getNumSamples()) / referenceTargetRate) * 1000.0);
        const auto offsetResult = HiHatTransitionPolicy::calculateDecayMatchedOffset({
            &referenceSourceSound->decayEnvelope,
            &referenceTargetSound->decayEnvelope,
            elapsedMs,
            currentAudibleDb,
            velocityDb,
            referenceTargetLengthMs,
            attackGuardMs,
            silenceFloorDb,
            tailMatchAmount,
            tailMatchBiasDb,
            minimumRemainingMs,
            missingDecayPolicy,
        });
        if (!offsetResult.succeeded())
            continue;

        bool targetsHaveRemainingAudio = true;
        for (size_t sourceIdx = 0; sourceIdx < sourceVoiceCount; ++sourceIdx) {
            auto* targetSound = targetEntries[sourceIdx]->sound;
            const double targetRate = targetSound->sampleRate > 0.0 ? targetSound->sampleRate : 44100.0;
            const float targetLengthMs = static_cast<float>(
                (static_cast<double>(targetSound->buffer->getNumSamples()) / targetRate) * 1000.0);
            if (targetLengthMs - offsetResult.offsetMs < minimumRemainingMs) {
                targetsHaveRemainingAudio = false;
                break;
            }
        }
        if (!targetsHaveRemainingAudio)
            continue;

        const auto& sourceArt = this->runtimeArticulations[static_cast<size_t>(sourceArtIdx)];
        const auto transitionDirection = HiHatTransitionPolicy::getDirection(
            sourceArt.pocValue,
            targetArt.pocValue);
        const float transitionCrossfadeSec = transitionDirection == HiHatTransitionPolicy::Direction::Closing
            ? closeCrossfadeSec
            : openCrossfadeSec;

        for (size_t sourceIdx = 0; sourceIdx < sourceVoiceCount; ++sourceIdx) {
            auto* sourceVoice = sourceVoices[sourceIdx];
            auto* targetVoice = reservedVoices[sourceIdx];
            auto* targetSound = targetEntries[sourceIdx]->sound;
            const float velocity = sourceVoice->getVelocityGain();
            this->startVoice(
                targetVoice,
                targetSound,
                sourceVoice->currentPlayingMidiChannel,
                sourceVoice->getCurrentlyPlayingNote(),
                velocity,
                eventId);
            targetVoice->hitGroupId = targetSound->micBufferChannelStart;
            targetVoice->runtimeArticulationIdx = targetArtIdx;
            targetVoice->artNameId = targetArt.nameId;
            targetVoice->artExprId = targetArt.exprId;
            const double targetRate = targetSound->sampleRate > 0.0 ? targetSound->sampleRate : 44100.0;
            targetVoice->sourceSamplePosition = juce::jlimit(
                0.0,
                juce::jmax(0.0, static_cast<double>(targetSound->buffer->getNumSamples() - 1)),
                (static_cast<double>(offsetResult.offsetMs) / 1000.0) * targetRate);
            targetVoice->setVelocityGain(velocity);
            targetVoice->requestTransitionFadeIn(transitionCrossfadeSec, transitionCurve);
        }
        for (size_t sourceIdx = 0; sourceIdx < sourceVoiceCount; ++sourceIdx)
            sourceVoices[sourceIdx]->requestTransitionFadeOut(transitionCrossfadeSec, transitionCurve);

        ++completedGroupCount;
    }

    return selectedGroupCount == 0 || completedGroupCount == selectedGroupCount;
}
