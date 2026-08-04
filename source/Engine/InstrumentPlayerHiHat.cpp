#include "InstrumentPlayer.h"
#include "../Config/PluginLogger.h"
#include "DrumKitEngine.h"
#include "HiHatTransitionPolicy.h"
#include <cmath>

int InstrumentPlayer::getRuntimeExpressionValue(const ArticulationRuntime& art, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) const {
    switch (dimensionType) {
        case KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose: return art.pedalOpenCloseValue;
        case KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneCenterRim: return art.zoneCenterRimValue;
        case KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneBowEdge: return art.zoneBowEdgeValue;
        case KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneLeftRight:
        case KitLib::ArticulationSpec::ExpressionDimension::Type::Unknown:
            return -1;
    }
    return -1;
}

InstrumentPlayer::ArtRtIdx InstrumentPlayer::selectArticulationForController(ArtRtIdx baseArtIdx, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType, int controllerValue) const {
    if (baseArtIdx < 0 || static_cast<int>(baseArtIdx) >= static_cast<int>(this->runtimeArticulations.size()))
        return baseArtIdx;

    const auto& baseArt = this->runtimeArticulations[static_cast<size_t>(baseArtIdx)];
    const int clampedValue = juce::jlimit(0, 127, controllerValue);

    for (int pass = 0; pass < 2; ++pass) {
        const bool relaxExpressionMatch = pass > 0;
        ArtRtIdx bestAboveIdx = -1;
        int bestAboveValue = 128;
        ArtRtIdx bestBelowIdx = -1;
        int bestBelowValue = -1;

        for (size_t idx = 0; idx < this->runtimeArticulations.size(); ++idx) {
            const auto& candidate = this->runtimeArticulations[idx];
            const int candidateValue = this->getRuntimeExpressionValue(candidate, dimensionType);
            if (candidateValue < 0) continue;

            if (dimensionType == KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose) {
                if (candidate.nameId != baseArt.nameId) continue;
            } else if (dimensionType == KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneCenterRim
                    || dimensionType == KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneBowEdge) {
                if (!relaxExpressionMatch && candidate.exprId != baseArt.exprId) continue;
            } else {
                continue;
            }

            const auto candidateIdx = static_cast<ArtRtIdx>(idx);
            if (candidateValue >= clampedValue && candidateValue < bestAboveValue) {
                bestAboveValue = candidateValue;
                bestAboveIdx = candidateIdx;
            }
            if (candidateValue < clampedValue && candidateValue > bestBelowValue) {
                bestBelowValue = candidateValue;
                bestBelowIdx = candidateIdx;
            }
        }

        if (bestAboveIdx >= 0) return bestAboveIdx;
        if (bestBelowIdx >= 0) return bestBelowIdx;
    }

    return baseArtIdx;
}

InstrumentPlayer::ArtRtIdx InstrumentPlayer::getVoiceRuntimeArticulationIdx(const InstrumentPlayerVoice& voice) const {
    if (voice.artRuntimeIdx >= 0 && static_cast<size_t>(voice.artRuntimeIdx) < this->runtimeArticulations.size()) return voice.artRuntimeIdx;

    if (const auto* sound = voice.getCurrentlyPlayingSound(); sound != nullptr && sound->sampleSpec != nullptr) {
        const auto* velocityLevel = sound->sampleSpec->parent != nullptr ? sound->sampleSpec->parent->parent : nullptr;
        const auto* artSpec = velocityLevel != nullptr ? velocityLevel->parent : nullptr;
        if (artSpec != nullptr) {
            const uint64_t artKey = (static_cast<uint64_t>(artSpec->nameId) << 32) | static_cast<uint64_t>(artSpec->exprId);
            const auto artCacheIt = this->artIdByNameExprId.find(artKey);
            if (artCacheIt != this->artIdByNameExprId.end() && artCacheIt->second >= 0 && static_cast<size_t>(artCacheIt->second) < this->runtimeArticulations.size())
                return static_cast<ArtRtIdx>(artCacheIt->second);
        }
    }

    for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx) {
        const auto& art = this->runtimeArticulations[artIdx];
        if (art.nameId == voice.artNameId && art.exprId == voice.artExprId) return static_cast<ArtRtIdx>(artIdx);
    }
    return -1;
}

int InstrumentPlayer::getVoicePedalOpenCloseValue(const InstrumentPlayerVoice& voice) const {
    const ArtRtIdx artIdx = this->getVoiceRuntimeArticulationIdx(voice);
    if (artIdx < 0 || static_cast<size_t>(artIdx) >= this->runtimeArticulations.size()) return -1;
    return this->runtimeArticulations[static_cast<size_t>(artIdx)].pedalOpenCloseValue;
}

int InstrumentPlayer::getHiHatNotePedalOpenCloseValue(int midiNoteNumber) const {
    if (midiNoteNumber < 0 || midiNoteNumber >= static_cast<int>(this->noteToArticulation.size())) return -1;

    int notePedalOpenCloseValue = this->hihatNotePedalOpenCloseValues[static_cast<size_t>(midiNoteNumber)];
    const ArtRtIdx mappedArtIdx = this->noteToArticulation[static_cast<size_t>(midiNoteNumber)];
    if (mappedArtIdx >= 0 && static_cast<size_t>(mappedArtIdx) < this->runtimeArticulations.size()) {
        const int mappedValue = this->runtimeArticulations[static_cast<size_t>(mappedArtIdx)].pedalOpenCloseValue;
        notePedalOpenCloseValue = juce::jmax(notePedalOpenCloseValue, mappedValue);
    }
    return notePedalOpenCloseValue;
}

void InstrumentPlayer::rebuildHiHatNoteChokeValues() {
    this->hihatNotePedalOpenCloseValues.fill(-1);
    if (this->type != KitLib::InstrumentSpec::Type::HiHat) return;

    static const juce::String pedalOpenCloseCode = "pdl_opn_cls";
    const auto defaultPoints = KitLayout::getDefaultExpressionPoints();
    for (const auto& articulationSlot : this->instrumentSlot.articulations) {
        const juce::String expressionLabel = StringsDictSingleton::nameOf(articulationSlot.exprId != 0 ? articulationSlot.exprId : articulationSlot.nameId).trim().toLowerCase();
        if (expressionLabel.isEmpty()) continue;

        std::optional<uint8_t> expressionValue = KitLayout::getExpressionPoint(this->instrumentSlot.expressionPoints, pedalOpenCloseCode, expressionLabel);
        if (!expressionValue.has_value() && this->parent != nullptr) {
            if (const auto* layout = this->parent->getKitLayout()) expressionValue = KitLayout::getExpressionPoint(layout->expressionPoints, pedalOpenCloseCode, expressionLabel);
        }
        if (!expressionValue.has_value()) expressionValue = KitLayout::getExpressionPoint(defaultPoints, pedalOpenCloseCode, expressionLabel);
        if (!expressionValue.has_value()) continue;

        for (const auto note : articulationSlot.notes) this->hihatNotePedalOpenCloseValues[static_cast<size_t>(note)] = static_cast<int>(*expressionValue);
    }
}

bool InstrumentPlayer::controllerCanTransitionExistingVoices(int controllerNumber) const {
    for (const auto& controller : this->instrumentSlot.controllers) {
        if (controller.number == controllerNumber)
            return controller.codeIdx == KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose;
    }
    return false;
}

void InstrumentPlayer::handleController(int midiChannel, int controllerNumber, int controllerValue) {
    if (this->parent == nullptr
        || this->getType() != KitLib::InstrumentSpec::Type::HiHat
        || controllerNumber < 0
        || controllerNumber >= static_cast<int>(this->ccToArticulationMask.size())
        || !this->ccToArticulationMask[static_cast<size_t>(controllerNumber)]
        || !this->controllerCanTransitionExistingVoices(controllerNumber)) {
        return;
    }

    const int clampedControllerValue = juce::jlimit(0, 127, controllerValue);
    const size_t controllerIdx = static_cast<size_t>(controllerNumber);
    if (this->pendingCcArticulation[controllerIdx] >= 0
        && this->pendingCcValue[controllerIdx] != clampedControllerValue) {
        this->pendingCcArticulation[controllerIdx] = -1;
    }
    this->pendingCcValue[controllerIdx] = clampedControllerValue;
    this->pendingCcMidiChannel[controllerIdx] = midiChannel;

    const float smoothingSec = this->parent->getEngineSettings().hihatControllerSmoothingSec.load();
    const uint64_t smoothingSamples = static_cast<uint64_t>(
        juce::jmax(0.0, std::round(smoothingSec * juce::jmax(1.0, this->sampleRate))));
    const bool canProcessNow = !this->hasProcessedCcTransition[controllerIdx]
        || smoothingSamples == 0
        || this->renderedSampleCount - this->lastCcProcessedSample[controllerIdx] >= smoothingSamples;
    if (canProcessNow) {
        this->pendingCcTransition[controllerIdx] = false;
        this->hasProcessedCcTransition[controllerIdx] = true;
        this->lastCcProcessedSample[controllerIdx] = this->renderedSampleCount;
        this->processHiHatControllerTransition(midiChannel, controllerNumber, clampedControllerValue);
        return;
    }

    this->pendingCcTransition[controllerIdx] = true;
    this->coalescedHiHatTransitions.fetch_add(1, std::memory_order_relaxed);
}

void InstrumentPlayer::processPendingHiHatControllers() {
    if (this->getType() != KitLib::InstrumentSpec::Type::HiHat || this->parent == nullptr)
        return;

    const float smoothingSec = this->parent->getEngineSettings().hihatControllerSmoothingSec.load();
    const uint64_t smoothingSamples = static_cast<uint64_t>(juce::jmax(0.0, std::round(smoothingSec * juce::jmax(1.0, this->sampleRate))));
    for (size_t controllerIdx = 0; controllerIdx < this->pendingCcTransition.size(); ++controllerIdx) {
        if (!this->pendingCcTransition[controllerIdx]) continue;
        if (smoothingSamples > 0 && this->renderedSampleCount - this->lastCcProcessedSample[controllerIdx] < smoothingSamples) continue;

        this->pendingCcTransition[controllerIdx] = false;
        this->hasProcessedCcTransition[controllerIdx] = true;
        this->lastCcProcessedSample[controllerIdx] = this->renderedSampleCount;
        this->processHiHatControllerTransition(this->pendingCcMidiChannel[controllerIdx], static_cast<int>(controllerIdx), this->pendingCcValue[controllerIdx]);
    }
}

bool InstrumentPlayer::isHiHatTransitionTargetAudible(const SampledSound& targetSound, double offsetSamples, float expectedTargetDb, float silenceFloorDb) const {
    if (targetSound.buffer == nullptr) return false;

    const auto& targetBuffer = *targetSound.buffer;
    const int windowStart = juce::jlimit(0, juce::jmax(0, targetBuffer.getNumSamples() - 1), static_cast<int>(offsetSamples));
    const int windowSize = juce::jmin(1024, targetBuffer.getNumSamples() - windowStart);
    if (windowSize <= 0) return false;

    int sourceChannelStart = 0;
    int sourceChannelCount = targetBuffer.getNumChannels();
    if (targetSound.sampleSpec != nullptr) {
        sourceChannelStart = targetSound.sampleSpec->fileChannelStart;
        if (targetSound.sampleSpec->fileChannelCount > 0) sourceChannelCount = targetSound.sampleSpec->fileChannelCount;
    }

    double sumSquares = 0.0;
    int sampleCount = 0;
    for (int channelIdx = 0; channelIdx < sourceChannelCount; ++channelIdx) {
        const int sourceChannel = juce::jlimit(0, targetBuffer.getNumChannels() - 1, sourceChannelStart + channelIdx);
        const float* readPtr = targetBuffer.getReadPointer(sourceChannel, windowStart);
        for (int sampleIdx = 0; sampleIdx < windowSize; ++sampleIdx) {
            const float sample = readPtr[sampleIdx];
            sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
        }
        sampleCount += windowSize;
    }

    const float rms = sampleCount > 0 ? std::sqrt(static_cast<float>(sumSquares / static_cast<double>(sampleCount))) : 0.0f;
    const float rmsDb = juce::Decibels::gainToDecibels(juce::jmax(0.000001f, rms), -100.0f);
    const float silenceThreshold = juce::jmax(expectedTargetDb - 24.0f, silenceFloorDb);
    return rmsDb >= silenceThreshold;
}

std::optional<float> InstrumentPlayer::findHiHatReferenceOffsetMs(const InstrumentPlayerVoice& sourceVoice, ArtRtIdx sourceArtIdx, const ArticulationRuntime& targetArt, int targetVelocityLevelIdx, uint8_t targetRoundRobinId, const SampledSound& replacementTargetSound, KitLib::MicSpec::Type& referenceMicType) const {
    if (this->parent == nullptr || targetVelocityLevelIdx < 0 || targetVelocityLevelIdx >= static_cast<int>(targetArt.velocityLevels.size())) return std::nullopt;

    const auto* sourceSound = sourceVoice.getCurrentlyPlayingSound();
    if (sourceSound == nullptr) return std::nullopt;
    const double sourceRate = sourceSound->sampleRate > 0.0 ? sourceSound->sampleRate : 44100.0;
    const float sourceElapsedMs = static_cast<float>((sourceVoice.sourceSamplePosition / sourceRate) * 1000.0);
    const auto& targetVelocityLevel = targetArt.velocityLevels[static_cast<size_t>(targetVelocityLevelIdx)];
    const auto& settings = this->parent->getEngineSettings();

    auto findReferenceVoice = [this, &sourceVoice, sourceArtIdx, sourceElapsedMs, targetRoundRobinId](KitLib::MicSpec::Type micType) -> InstrumentPlayerVoice* {
        InstrumentPlayerVoice* bestVoice = nullptr;
        float bestElapsedDifferenceMs = 25.0f;
        for (auto* candidateVoice : this->voices) {
            if (candidateVoice == nullptr || !candidateVoice->isVoiceActive()) continue;
            if (candidateVoice->getCurrentlyPlayingNote() != sourceVoice.getCurrentlyPlayingNote()) continue;
            if (candidateVoice->currentPlayingMidiChannel != sourceVoice.currentPlayingMidiChannel) continue;
            if (candidateVoice->getMidiVelocity() != sourceVoice.getMidiVelocity()) continue;
            if (this->getVoiceRuntimeArticulationIdx(*candidateVoice) != sourceArtIdx) continue;

            const auto* candidateSound = candidateVoice->getCurrentlyPlayingSound();
            if (candidateSound == nullptr || candidateSound->sampleSpec == nullptr) continue;
            if (candidateSound->sampleSpec->micSpec.type != micType) continue;
            if (candidateSound->sampleSpec->parent == nullptr || candidateSound->sampleSpec->parent->roundRobinId != targetRoundRobinId) continue;

            const double candidateRate = candidateSound->sampleRate > 0.0 ? candidateSound->sampleRate : 44100.0;
            const float candidateElapsedMs = static_cast<float>((candidateVoice->sourceSamplePosition / candidateRate) * 1000.0);
            const float elapsedDifferenceMs = std::abs(candidateElapsedMs - sourceElapsedMs);
            if (elapsedDifferenceMs > bestElapsedDifferenceMs) continue;
            bestElapsedDifferenceMs = elapsedDifferenceMs;
            bestVoice = candidateVoice;
        }
        return bestVoice;
    };

    auto findTargetEntry = [&targetVelocityLevel, targetRoundRobinId](const SampledSound& referenceSound) -> const SampleEntry* {
        const int referenceMicStart = referenceSound.micBufferChannelStart;
        const SampleEntry* targetEntry = nullptr;
        for (const auto& sampleEntry : targetVelocityLevel.samples) {
            if (sampleEntry.roundRobinId == targetRoundRobinId && sampleEntry.sound != nullptr && sampleEntry.sound->micBufferChannelStart == referenceMicStart) {
                targetEntry = &sampleEntry;
                break;
            }
        }
        if (targetEntry != nullptr && targetEntry->sound->loaded.load(std::memory_order_acquire) && targetEntry->sound->buffer != nullptr) return targetEntry;
        for (const auto& sampleEntry : targetVelocityLevel.samples) {
            if (sampleEntry.sound == nullptr || sampleEntry.sound->micBufferChannelStart != referenceMicStart) continue;
            if (!sampleEntry.sound->loaded.load(std::memory_order_acquire) || sampleEntry.sound->buffer == nullptr) continue;
            return &sampleEntry;
        }
        return nullptr;
    };

    const std::array<KitLib::MicSpec::Type, 2> referenceTypes { KitLib::MicSpec::Type::Direct, KitLib::MicSpec::Type::Overhead };
    for (const auto referenceType : referenceTypes) {
        auto* referenceVoice = findReferenceVoice(referenceType);
        if (referenceVoice == nullptr) continue;
        const auto* referenceSound = referenceVoice->getCurrentlyPlayingSound();
        if (referenceSound == nullptr) continue;
        const auto* targetEntry = findTargetEntry(*referenceSound);
        if (targetEntry == nullptr || targetEntry->sound == nullptr || targetEntry->sound->buffer == nullptr) continue;

        const auto& sourceEnvelope = referenceSound->decayEnvelope;
        const auto& targetEnvelope = targetEntry->sound->decayEnvelope;
        const double referenceSourceRate = referenceSound->sampleRate > 0.0 ? referenceSound->sampleRate : 44100.0;
        const float elapsedMs = static_cast<float>((referenceVoice->sourceSamplePosition / referenceSourceRate) * 1000.0);
        const float sourceVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, referenceVoice->getVelocityGain()), -100.0f);
        const float targetVelocityGain = targetArt.velocityGain[static_cast<size_t>(referenceVoice->getMidiVelocity())];
        const float targetVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, targetVelocityGain), -100.0f);
        const float currentAudibleDb = sourceEnvelope.isValid() ? sourceEnvelope.getAbsoluteDbAtMs(elapsedMs) + sourceVelocityDb : referenceVoice->getEstimatedCurrentDb();
        const double targetRate = targetEntry->sound->sampleRate > 0.0 ? targetEntry->sound->sampleRate : 44100.0;
        const float targetLengthMs = static_cast<float>((static_cast<double>(targetEntry->sound->buffer->getNumSamples()) / targetRate) * 1000.0);
        const auto offsetResult = HiHatTransitionPolicy::calculateDecayMatchedOffset({
            &sourceEnvelope,
            &targetEnvelope,
            elapsedMs,
            currentAudibleDb,
            targetVelocityDb,
            targetLengthMs,
            settings.hihatTransitionAttackGuardSec.load() * 1000.0f,
            settings.hihatTransitionSilenceFloorDb.load(),
            settings.hihatTailMatchAmount.load(),
            settings.hihatTailMatchBiasDb.load(),
            settings.hihatTransitionMinRemainingSec.load() * 1000.0f,
            settings.hihatMissingDecayPolicy.load(),
        });
        if (!offsetResult.succeeded()) continue;

        if (replacementTargetSound.buffer == nullptr) continue;
        const double replacementTargetRate = replacementTargetSound.sampleRate > 0.0 ? replacementTargetSound.sampleRate : 44100.0;
        const float replacementTargetLengthMs = static_cast<float>((static_cast<double>(replacementTargetSound.buffer->getNumSamples()) / replacementTargetRate) * 1000.0);
        if (replacementTargetLengthMs - offsetResult.offsetMs < settings.hihatTransitionMinRemainingSec.load() * 1000.0f) continue;
        const double offsetSamples = (static_cast<double>(offsetResult.offsetMs) / 1000.0) * replacementTargetRate;
        const double clampedOffset = juce::jlimit(0.0, juce::jmax(0.0, static_cast<double>(replacementTargetSound.buffer->getNumSamples() - 1)), offsetSamples);
        const float expectedReplacementDb = replacementTargetSound.decayEnvelope.isValid() ? replacementTargetSound.decayEnvelope.getAbsoluteDbAtMs(offsetResult.offsetMs) : offsetResult.targetRelativeDb;
        if (!this->isHiHatTransitionTargetAudible(replacementTargetSound, clampedOffset, expectedReplacementDb, settings.hihatTransitionSilenceFloorDb.load())) continue;

        referenceMicType = referenceType;
        return offsetResult.offsetMs;
    }

    return std::nullopt;
}

void InstrumentPlayer::processHiHatControllerTransition(int midiChannel, int controllerNumber, int controllerValue) {
    this->processedHiHatTransitions.fetch_add(1, std::memory_order_relaxed);
    const auto controllerIdx = static_cast<size_t>(controllerNumber);
    const auto& settings = this->parent->getEngineSettings();
    const float openCrossfadeSec = settings.hihatOpenCrossfadeSec.load();
    const float closeCrossfadeSec = settings.hihatCloseCrossfadeSec.load();
    const int boundaryHysteresisCc = settings.hihatBoundaryHysteresisCc.load();
    const float tailMatchAmount = settings.hihatTailMatchAmount.load();
    const float tailMatchBiasDb = settings.hihatTailMatchBiasDb.load();
    const float attackGuardSec = settings.hihatTransitionAttackGuardSec.load();
    const float minRemainingSec = settings.hihatTransitionMinRemainingSec.load();
    const float silenceFloorDb = settings.hihatTransitionSilenceFloorDb.load();
    const auto missingDecayPolicy = settings.hihatMissingDecayPolicy.load();

    // TODO01: pedal state without note should make sound transition based on latest rms level that we can take from parsed decayCode data, like if hitted closed hat and start opening pedal it should make transition to more opened sound
    // so new more open sample plays from offset based on rms level, for example we have analyzed data
    // instr,art,vel,rr,mic,rms_peak_ms, rms_peak_db, rms_decayed_ms-6dB,-12dB,-18dB,-24dB,-30dB,-36dB,-42dB,-48dB,-54dB,-60dB
    // hihat-X,edge[close],127,a,dtm,20, -15.76, 80,120,150,170,190,210,240,270,320,400
    // hihat-X,edge[half],127,a,dtm,50, -20.89, 250,380,550,720,800,860,930,1020,1180,1400
    // hihat-X,edge[open],127,a,dtm,40, -22.52, 210,380,590,930,1680,2320,2800,3530,3760,4050
    // so if we have played closed hat for 200ms (rms level at that time 20+170 < 300 < 20+190 was between -24 and -30 db from its peak -15.76-27 = about -42dB)
    // then when we start opening pedal, and we know that half-open hat has -42dB at about 50+2800ms, we can start new half-open sample from offset around 2850ms, (question what will be faster and seamless, phase align or crossfade)
    // so it need to sound seamless and natural, without noticeable attack transient if thing is happened early or unnatural choke+fadein effect that happens when you just stop one sample and start another from the beginning.
    // Only handle hi-hat pedal crossfades here for now
    const auto& mapArr = this->ccToArticulationTable[controllerIdx];
    const auto controllerValueIdx = static_cast<size_t>(juce::jlimit(0, 127, controllerValue));
    const uint8_t mappedArtIdx = mapArr[controllerValueIdx];
    if (mappedArtIdx >= this->runtimeArticulations.size()) {
        if (LOG_ENABLED(Warning))
            LOG(Warning, "handleController: CC " + juce::String(controllerNumber)
                + " mapped to out-of-range articulation idx=" + juce::String((int)mappedArtIdx)
                + " (runtime size=" + juce::String((int)this->runtimeArticulations.size()) + ")");
        this->lastCcValue[controllerIdx] = controllerValue;
        return;
    }

    const int cachedArtIdx = this->lastCcArticulation[controllerIdx];
    std::array<int, 256> pocByArticulation {};
    const size_t articulationCount = juce::jmin(this->runtimeArticulations.size(), pocByArticulation.size());
    for (size_t articulationIdx = 0; articulationIdx < articulationCount; ++articulationIdx)
        pocByArticulation[articulationIdx] = this->runtimeArticulations[articulationIdx].pedalOpenCloseValue;

    const int pendingArtIdx = this->pendingCcArticulation[controllerIdx];
    const int selectedArtIdx = pendingArtIdx >= 0
        ? pendingArtIdx
        : HiHatTransitionPolicy::selectOpennessArticulation(
            mapArr,
            std::span<const int>(pocByArticulation.data(), articulationCount),
            controllerValue,
            this->lastCcValue[controllerIdx],
            cachedArtIdx,
            boundaryHysteresisCc);
    int maximumMappedPoc = -1;
    int secondHighestMappedPoc = -1;
    std::array<bool, 128> mappedPedalOpenCloseValues {};
    int mappedPedalOpenCloseValueCount = 0;
    for (int mappedControllerValue = 0; mappedControllerValue <= 127; ++mappedControllerValue) {
        const auto articulationIdx = mapArr[static_cast<size_t>(mappedControllerValue)];
        if (articulationIdx >= this->runtimeArticulations.size())
            continue;
        const int mappedPoc = this->runtimeArticulations[articulationIdx].pedalOpenCloseValue;
        if (mappedPoc >= 0 && mappedPoc < static_cast<int>(mappedPedalOpenCloseValues.size()) && !mappedPedalOpenCloseValues[static_cast<size_t>(mappedPoc)]) {
            mappedPedalOpenCloseValues[static_cast<size_t>(mappedPoc)] = true;
            ++mappedPedalOpenCloseValueCount;
        }
        if (mappedPoc > maximumMappedPoc) {
            secondHighestMappedPoc = maximumMappedPoc;
            maximumMappedPoc = mappedPoc;
        } else if (mappedPoc < maximumMappedPoc && mappedPoc > secondHighestMappedPoc) {
            secondHighestMappedPoc = mappedPoc;
        }
    }
    const bool isMostClosedState = selectedArtIdx >= 0 && maximumMappedPoc >= 0
        && selectedArtIdx < static_cast<int>(this->runtimeArticulations.size())
        && this->runtimeArticulations[static_cast<size_t>(selectedArtIdx)].pedalOpenCloseValue >= maximumMappedPoc;
    const bool enteredMostClosedState = isMostClosedState && !this->hihatPedalClosedState[controllerIdx];
    this->hihatPedalClosedState[controllerIdx] = isMostClosedState;
    const int openHalfPocLimit = mappedPedalOpenCloseValueCount <= 2 || secondHighestMappedPoc < 0 ? maximumMappedPoc : secondHighestMappedPoc;
    float loudestLowerPocDb = -100.0f;
    bool hasAudibleLowerPocVoice = false;
    bool hasAudibleOpenHalfVoice = false;
    for (auto* voice : this->voices) {
        if (voice == nullptr || !voice->isVoiceActive() || voice->getEnvelopeState() != InstrumentPlayerVoice::EnvelopeState::Playing)
            continue;
        if (midiChannel > 0 && !voice->isPlayingChannel(midiChannel))
            continue;

        const int voicePedalOpenCloseValue = this->getVoicePedalOpenCloseValue(*voice);
        if (voicePedalOpenCloseValue < 0 || voicePedalOpenCloseValue >= maximumMappedPoc)
            continue;

        const float voiceAudibleDb = voice->getEstimatedCurrentDb();
        if (voiceAudibleDb < silenceFloorDb)
            continue;
        hasAudibleLowerPocVoice = true;
        loudestLowerPocDb = juce::jmax(loudestLowerPocDb, voiceAudibleDb);
        if (voicePedalOpenCloseValue < openHalfPocLimit)
            hasAudibleOpenHalfVoice = true;
    }
    if (hasAudibleOpenHalfVoice)
        this->hihatImplicitPedalEligible[controllerIdx] = true;
    else if (!isMostClosedState && !hasAudibleLowerPocVoice)
        this->hihatImplicitPedalEligible[controllerIdx] = false;
    if (selectedArtIdx == cachedArtIdx) {
        this->pendingCcArticulation[controllerIdx] = -1;
        this->lastCcValue[controllerIdx] = controllerValue;
        return;
    }

    this->pendingCcArticulation[controllerIdx] = -1;
    const uint8_t newArtIdx = static_cast<uint8_t>(selectedArtIdx);
    if (LOG_ENABLED(Verbose))
        LOG(Verbose, "HiHat pedal transition: cc=" + juce::String(controllerNumber) + " value=" + juce::String(controllerValue));

    int activeMismatchedVoices = 0;
    int replacementsStarted = 0;
    int skippedNoEnvelope = 0;
    int skippedNoTarget = 0;
    int skippedNoVoice = 0;
    int skippedTooLate = 0;
    int skippedNoDbMatch = 0;
    int skippedSilentTarget = 0;
    int skippedOther = 0;
    int referenceOffsetReplacements = 0;
    int fadedSilentTargets = 0;
    bool encounteredTransientSkip = false;

    // For each active voice, if its articulation differs from newArtIdx, crossfade
    for (auto* voice : this->voices) {
        if (voice == nullptr) continue;
        if (!voice->isVoiceActive()) continue;

        auto* ss = voice->getCurrentlyPlayingSound();
        if (ss == nullptr || ss->sampleSpec == nullptr) continue;

        const int currentArtIdx = this->getVoiceRuntimeArticulationIdx(*voice);
        if (currentArtIdx < 0) continue;

        if (currentArtIdx == static_cast<int>(newArtIdx)) continue; // already correct
        ++activeMismatchedVoices;

        // Now prepare to start a replacement voice from new articulation
        const auto& newArt = this->runtimeArticulations[static_cast<size_t>(newArtIdx)];
        // Choose the target velocityLevel from the original MIDI velocity, not from the calibrated output gain.
        const juce::uint8 midiVelocity = voice->getMidiVelocity();
        int velocityLevelIdx = newArt.velocityToLevel[static_cast<size_t>(midiVelocity)];
        if (velocityLevelIdx < 0 || velocityLevelIdx >= static_cast<int>(newArt.velocityLevels.size())) continue;
        const auto& velocityLevel = newArt.velocityLevels[static_cast<size_t>(velocityLevelIdx)];

        // Find the round-robin ID of the current voice.
        uint8_t currentRoundRobinId = 0;
        if (ss->sampleSpec->parent != nullptr) currentRoundRobinId = ss->sampleSpec->parent->roundRobinId;

        // Find the sample entry in the target velocity level with the same round-robin ID and mic channel,
        // so each replacement voice writes to the same mic buffer channel as the
        // voice it replaces (overhead stays overhead, room stays room, etc.).
        const int voiceMicStart = ss->micBufferChannelStart;
        const InstrumentPlayer::SampleEntry* targetSe = nullptr;
        for (const auto& se : velocityLevel.samples) {
            if (se.roundRobinId == currentRoundRobinId && se.sound != nullptr
                && se.sound->micBufferChannelStart == voiceMicStart) {
                targetSe = &se;
                break;
            }
        }
        // Fallback: match only the round-robin ID when articulations have different mic layouts.
        if (targetSe == nullptr) {
            for (const auto& se : velocityLevel.samples) {
                if (se.roundRobinId == currentRoundRobinId) { targetSe = &se; break; }
            }
        }
        if (targetSe == nullptr && !velocityLevel.samples.empty()) targetSe = &velocityLevel.samples.front();
        if (targetSe == nullptr || targetSe->sound == nullptr) {
            ++skippedNoTarget;
            continue;
        }

        if (!targetSe->sound->loaded.load(std::memory_order_acquire) || targetSe->sound->buffer == nullptr) {
            for (const auto& se : velocityLevel.samples) {
                if (se.sound == nullptr) continue;
                if (se.sound->micBufferChannelStart != voiceMicStart) continue;
                if (!se.sound->loaded.load(std::memory_order_acquire)) continue;
                if (se.sound->buffer == nullptr) continue;
                targetSe = &se;
                break;
            }
            if (!targetSe->sound->loaded.load(std::memory_order_acquire) || targetSe->sound->buffer == nullptr) {
                ++skippedNoTarget;
                continue;
            }
        }

        // Compute the offset independently from this voice's mic-specific decay
        // envelopes. Sharing one offset puts room replacements in pre-peak or
        // near-silent regions because direct and room mics decay differently.
        const auto& oldEnv = ss->decayEnvelope;
        const auto& newEnv = targetSe->sound->decayEnvelope;
        const double oldSrcRate = ss->sampleRate > 0.0 ? ss->sampleRate : 44100.0;
        const float elapsedMs = static_cast<float>((voice->sourceSamplePosition / oldSrcRate) * 1000.0);

        const float oldVelDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, voice->getVelocityGain()), -100.0f);
        const float targetVelocityGain = newArt.velocityGain[static_cast<size_t>(midiVelocity)];
        const float targetVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, targetVelocityGain), -100.0f);
        const float currentAudibleDb = oldEnv.isValid()
            ? oldEnv.getAbsoluteDbAtMs(elapsedMs) + oldVelDb
            : voice->getEstimatedCurrentDb();

        const auto& targetBuffer = *targetSe->sound->buffer;
        const double targetSrcRate = targetSe->sound->sampleRate > 0.0 ? targetSe->sound->sampleRate : 44100.0;
        const float sampleLengthMs = targetSrcRate > 0.0
            ? static_cast<float>((static_cast<double>(targetBuffer.getNumSamples()) / targetSrcRate) * 1000.0)
            : 0.0f;
        const HiHatTransitionPolicy::OffsetResult offsetResult = HiHatTransitionPolicy::calculateDecayMatchedOffset({
            &oldEnv,
            &newEnv,
            elapsedMs,
            currentAudibleDb,
            targetVelocityDb,
            sampleLengthMs,
            attackGuardSec * 1000.0f,
            silenceFloorDb,
            tailMatchAmount,
            tailMatchBiasDb,
            minRemainingSec * 1000.0f,
            missingDecayPolicy,
        });
        if (!offsetResult.succeeded()) {
            switch (offsetResult.status) {
                case HiHatTransitionPolicy::OffsetStatus::MissingEnvelope:
                    ++skippedNoEnvelope;
                    break;
                case HiHatTransitionPolicy::OffsetStatus::NoDecayMatch:
                    ++skippedNoDbMatch;
                    break;
                case HiHatTransitionPolicy::OffsetStatus::TooLittleRemaining:
                    ++skippedTooLate;
                    break;
                case HiHatTransitionPolicy::OffsetStatus::InvalidSampleLength:
                    ++skippedNoTarget;
                    break;
                case HiHatTransitionPolicy::OffsetStatus::AttackGuard:
                    encounteredTransientSkip = true;
                    ++skippedOther;
                    break;
                case HiHatTransitionPolicy::OffsetStatus::BelowSilenceFloor:
                    ++skippedOther;
                    break;
                case HiHatTransitionPolicy::OffsetStatus::Success:
                    break;
            }
            continue;
        }

        const float targetRelativeDb = offsetResult.targetRelativeDb;
        const float clampedMs = offsetResult.offsetMs;

        double selectedOffsetMs = static_cast<double>(clampedMs);

        // For pedal transitions, never steal active voices. If no idle voice exists,
        // keep the old voice ringing instead of risking an abrupt mute.
        InstrumentPlayerVoice* newVoice = nullptr;
        for (auto* candidate : this->voices) {
            if (candidate == nullptr || candidate == voice) continue;
            if (!candidate->isVoiceActive()) {
                newVoice = candidate;
                break;
            }
        }

        if (newVoice == nullptr) {
            ++skippedNoVoice;
            encounteredTransientSkip = true;
            continue;
        }

        const double srcRate = targetSe->sound->sampleRate > 0.0 ? targetSe->sound->sampleRate : 44100.0;
        const double offsetSamples = (selectedOffsetMs / 1000.0) * srcRate;
        double clampedOffset = juce::jlimit(0.0, juce::jmax(0.0, static_cast<double>(targetBuffer.getNumSamples() - 1)), offsetSamples);

        const auto& currentArt = this->runtimeArticulations[static_cast<size_t>(currentArtIdx)];
        const auto transitionDirection = HiHatTransitionPolicy::getDirection(currentArt.pedalOpenCloseValue, newArt.pedalOpenCloseValue);
        const float transitionCrossfadeSec = transitionDirection == HiHatTransitionPolicy::Direction::Closing ? closeCrossfadeSec : openCrossfadeSec;
        bool usedReferenceOffset = false;
        KitLib::MicSpec::Type referenceMicType = KitLib::MicSpec::Type::Unknown;

        // Guard against replacing with an effectively silent region.
        // Use a threshold relative to the expected decay-envelope level. Quiet
        // overhead and room content at -60 to -80 dB can still be legitimate.
        // Only skip if measured content is >24dB below the envelope's prediction
        // (indicating corrupt/empty sample), with an absolute floor at -90dB
        // to catch true digital silence.
        if (!this->isHiHatTransitionTargetAudible(*targetSe->sound, clampedOffset, targetRelativeDb, silenceFloorDb)) {
            const auto referenceOffsetMs = this->findHiHatReferenceOffsetMs(*voice, static_cast<ArtRtIdx>(currentArtIdx), newArt, velocityLevelIdx, currentRoundRobinId, *targetSe->sound, referenceMicType);
            if (referenceOffsetMs.has_value()) {
                const double referenceOffsetSamples = (static_cast<double>(*referenceOffsetMs) / 1000.0) * srcRate;
                const double referenceClampedOffset = juce::jlimit(0.0, juce::jmax(0.0, static_cast<double>(targetBuffer.getNumSamples() - 1)), referenceOffsetSamples);
                selectedOffsetMs = *referenceOffsetMs;
                clampedOffset = referenceClampedOffset;
                usedReferenceOffset = true;
                ++referenceOffsetReplacements;
            }

            if (!usedReferenceOffset) {
                ++skippedSilentTarget;
                if (transitionDirection == HiHatTransitionPolicy::Direction::Closing) {
                    voice->requestFadeOut(closeCrossfadeSec);
                    ++fadedSilentTargets;
                }
                continue;
            }
        }

        voice->requestFadeOut(transitionCrossfadeSec);

        this->startVoice(newVoice, targetSe->sound, midiChannel, voice->getCurrentlyPlayingNote(), targetVelocityGain);
        newVoice->hitGroupId = targetSe->sound->micBufferChannelStart;
        newVoice->artNameId = newArt.nameId;
        newVoice->artExprId = newArt.exprId;
        newVoice->artRuntimeIdx = static_cast<ArtRtIdx>(newArtIdx);
        newVoice->sourceSamplePosition = clampedOffset;
        newVoice->setVelocityGain(targetVelocityGain);
        newVoice->setMidiVelocity(midiVelocity);
        ++replacementsStarted;

        if (LOG_ENABLED(Verbose)) {
            juce::String oldArtCode = "unknown";
            if (ss->sampleSpec != nullptr && ss->sampleSpec->parent != nullptr && ss->sampleSpec->parent->parent != nullptr && ss->sampleSpec->parent->parent->parent != nullptr)
                oldArtCode = ss->sampleSpec->parent->parent->parent->codeRaw;

            juce::String newArtCode = "unknown";
            if (targetSe->sound != nullptr && targetSe->sound->sampleSpec != nullptr
                && targetSe->sound->sampleSpec->parent != nullptr
                && targetSe->sound->sampleSpec->parent->parent != nullptr
                && targetSe->sound->sampleSpec->parent->parent->parent != nullptr)
                newArtCode = targetSe->sound->sampleSpec->parent->parent->parent->codeRaw;

            juce::String sampleName = "unknown";
            if (targetSe->sound != nullptr && targetSe->sound->sampleSpec != nullptr)
                sampleName = targetSe->sound->sampleSpec->sampleFile.getFileName();

            LOG(Verbose, "PedalVoiceStart inst=" + this->getId()
                + " ch=" + juce::String(midiChannel)
                + " note=" + juce::String(voice->getCurrentlyPlayingNote())
                + " vel=" + juce::String(targetVelocityGain, 3)
                + " fromArt=" + oldArtCode
                + " toArt=" + newArtCode
                + " micStart=" + juce::String(targetSe->sound ? targetSe->sound->micBufferChannelStart : -1)
                + " micCount=" + juce::String(targetSe->sound ? targetSe->sound->micBufferChannelCount : 0)
                + " offsetMs=" + juce::String(selectedOffsetMs, 2)
                + " offsetSource=" + (usedReferenceOffset ? (referenceMicType == KitLib::MicSpec::Type::Direct ? juce::String("direct") : juce::String("overhead")) : juce::String("own"))
                + " offsetSamples=" + juce::String(clampedOffset, 2)
                + " sample=" + sampleName);
        }

    }

    // Only cache new articulation once at least one replacement starts, or when
    // there were no active mismatched voices to transition.
    const int handledTransitions = replacementsStarted + fadedSilentTargets;
    if (handledTransitions > 0 || activeMismatchedVoices == 0)
        this->lastCcArticulation[controllerIdx] = static_cast<ArtRtIdx>(newArtIdx);
    this->lastCcValue[controllerIdx] = controllerValue;

    if (handledTransitions == 0 && activeMismatchedVoices > 0 && encounteredTransientSkip) {
        this->pendingCcArticulation[controllerIdx] = static_cast<ArtRtIdx>(newArtIdx);
        this->pendingCcTransition[controllerIdx] = true;
    }

    const auto footSampleMode = settings.hihatFootSampleMode.load();
    const auto controllerDirection = cachedArtIdx >= 0 && cachedArtIdx < static_cast<int>(this->runtimeArticulations.size())
        ? HiHatTransitionPolicy::getDirection(
            this->runtimeArticulations[static_cast<size_t>(cachedArtIdx)].pedalOpenCloseValue,
            this->runtimeArticulations[static_cast<size_t>(newArtIdx)].pedalOpenCloseValue)
        : HiHatTransitionPolicy::Direction::Unknown;
    const bool isClosingTransition = controllerDirection == HiHatTransitionPolicy::Direction::Closing;
    const bool shouldTriggerPedal = footSampleMode != HiHatFootSampleMode::Off
        && isClosingTransition
        && enteredMostClosedState
        && this->hihatImplicitPedalEligible[controllerIdx]
        && hasAudibleLowerPocVoice;
    if (shouldTriggerPedal)
        this->triggerHiHatPedalSample(midiChannel, loudestLowerPocDb);
    if (enteredMostClosedState)
        this->hihatImplicitPedalEligible[controllerIdx] = false;

    if (handledTransitions > 0)
        this->completedHiHatTransitions.fetch_add(1, std::memory_order_relaxed);
    else if (activeMismatchedVoices > 0)
        this->skippedHiHatTransitions.fetch_add(1, std::memory_order_relaxed);

    if (LOG_ENABLED(Verbose) && activeMismatchedVoices > 0 && handledTransitions == 0) {
        LOG(Verbose, "HiHat pedal transition skipped: cc=" + juce::String(controllerNumber)
            + " value=" + juce::String(controllerValue)
            + " targetArt=" + juce::String((int)newArtIdx)
            + " activeMismatchedVoices=" + juce::String(activeMismatchedVoices)
            + " reasons{noEnv=" + juce::String(skippedNoEnvelope)
            + ", noTarget=" + juce::String(skippedNoTarget)
            + ", noVoice=" + juce::String(skippedNoVoice)
            + ", tooLate=" + juce::String(skippedTooLate)
            + ", noDbMatch=" + juce::String(skippedNoDbMatch)
            + ", silentTarget=" + juce::String(skippedSilentTarget)
            + ", other=" + juce::String(skippedOther) + "}"
        );
    }

    const int totalSkipped = skippedNoEnvelope + skippedNoTarget + skippedNoVoice + skippedTooLate + skippedNoDbMatch + skippedSilentTarget + skippedOther;
    if (LOG_ENABLED(Verbose) && handledTransitions > 0 && totalSkipped > 0) {
        LOG(Verbose, "HiHat pedal transition partial: cc=" + juce::String(controllerNumber)
            + " value=" + juce::String(controllerValue)
            + " targetArt=" + juce::String((int)newArtIdx)
            + " started=" + juce::String(replacementsStarted)
            + " referenceOffsets=" + juce::String(referenceOffsetReplacements)
            + " fadedSilent=" + juce::String(fadedSilentTargets)
            + " skipped=" + juce::String(totalSkipped)
            + " reasons{noEnv=" + juce::String(skippedNoEnvelope)
            + ", noTarget=" + juce::String(skippedNoTarget)
            + ", noVoice=" + juce::String(skippedNoVoice)
            + ", tooLate=" + juce::String(skippedTooLate)
            + ", noDbMatch=" + juce::String(skippedNoDbMatch)
            + ", silentTarget=" + juce::String(skippedSilentTarget)
            + ", other=" + juce::String(skippedOther) + "}"
        );
    }
}

bool InstrumentPlayer::triggerHiHatPedalSample(int midiChannel, float sourceAudibleDb) {
    if (this->hihatPedalNote < 0)
        return false;

    const int pedalArtIdx = this->noteToArticulation[static_cast<size_t>(this->hihatPedalNote)];
    if (pedalArtIdx < 0 || pedalArtIdx >= static_cast<int>(this->runtimeArticulations.size()))
        return false;

    float pedalPeakDb = -100.0f;
    for (const auto& velocityLevel : this->runtimeArticulations[static_cast<size_t>(pedalArtIdx)].velocityLevels) {
        for (const auto& sampleEntry : velocityLevel.samples) {
            if (sampleEntry.sound == nullptr || !sampleEntry.sound->decayEnvelope.isValid())
                continue;
            pedalPeakDb = juce::jmax(pedalPeakDb, sampleEntry.sound->decayEnvelope.peakDb);
        }
    }
    if (pedalPeakDb <= -100.0f)
        return false;

    const auto& pedalArt = this->runtimeArticulations[static_cast<size_t>(pedalArtIdx)];
    const float relativeTargetDb = juce::jlimit(-100.0f, 0.0f, sourceAudibleDb - pedalPeakDb);
    const int velocity = pedalArt.calibrated
        ? juce::jlimit(1, 127, static_cast<int>(this->parent->getVelocityResponse().findClosestVelocityForTargetDb(relativeTargetDb)))
        : juce::jlimit(1, 127, static_cast<int>(std::round(juce::Decibels::decibelsToGain(relativeTargetDb, -100.0f) * 127.0f)));
    return this->playNote(midiChannel, this->hihatPedalNote, static_cast<juce::uint8>(velocity));
}
