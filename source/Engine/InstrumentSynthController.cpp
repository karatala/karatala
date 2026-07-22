#include "InstrumentSynth.h"
#include "DrumKitEngine.h"
#include "HiHatTransitionPolicy.h"
#include <cmath>

void InstrumentSynth::handleMidiEvent(const juce::MidiMessage& midiMessage) {
    if (midiMessage.isNoteOn()) {
        const int note = midiMessage.getNoteNumber();
        if (this->noteToArticulation[static_cast<size_t>(note)] == -1) return;
        this->playNote(midiMessage.getChannel(), note, midiMessage.getVelocity());
    }
    else if (midiMessage.isController()) {
        auto ccNum = static_cast<uint8_t>(midiMessage.getControllerNumber());
        if (!this->ccToArticulationMask[ccNum]) return;
        this->handleController(midiMessage.getChannel(), ccNum, midiMessage.getControllerValue());
    }
    else if (midiMessage.isAftertouch()) this->handleAftertouch(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getAfterTouchValue());
 
    else if (midiMessage.isAllNotesOff() || midiMessage.isAllSoundOff()) this->allNotesOff(midiMessage.getChannel(), true);

    // else if (midiMessage.isNoteOff()) this->noteOff (channel, midiMessage.getNoteNumber(), midiMessage.getFloatVelocity(), true);
    // else if (midiMessage.isChannelPressure()) this->handleChannelPressure(channel, midiMessage.getChannelPressureValue());
    // else if (midiMessage.isProgramChange()) this->handleProgramChange(channel, midiMessage.getProgramChangeNumber());
    // else if (midiMessage.isPitchWheel()) {
    //     const int wheelPos = midiMessage.getPitchWheelValue();
    //     lastPitchWheelValues[channel - 1] = wheelPos;
    //     this->handlePitchWheel(channel, wheelPos);
    // }
    
}

void InstrumentSynth::applyAftertouch(int midiChannel, int midiNoteNumber) {
    const auto& paVal = this->parent->getLatestPolyphonicAftertouchValue(midiNoteNumber);
    if (paVal > 0) this->handleAftertouch(midiChannel, midiNoteNumber, paVal);
}

int InstrumentSynth::getRuntimeExpressionValue(const ArticulationRuntime& art, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) const {
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

InstrumentSynth::ArtRtIdx InstrumentSynth::selectArticulationForController(ArtRtIdx baseArtIdx, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType, int controllerValue) const {
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

InstrumentSynth::ArtRtIdx InstrumentSynth::getVoiceRuntimeArticulationIdx(const InstrumentSynthVoice& voice) const {
    if (voice.artRuntimeIdx >= 0 && static_cast<size_t>(voice.artRuntimeIdx) < this->runtimeArticulations.size()) return voice.artRuntimeIdx;

    if (const auto* sound = voice.getCurrentlyPlayingSound(); sound != nullptr && sound->sampleVariantSpec != nullptr) {
        const auto* velocityLayer = sound->sampleVariantSpec->parent;
        const auto* artSpec = velocityLayer != nullptr ? velocityLayer->parent : nullptr;
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

int InstrumentSynth::getVoicePedalOpenCloseValue(const InstrumentSynthVoice& voice) const {
    const ArtRtIdx artIdx = this->getVoiceRuntimeArticulationIdx(voice);
    if (artIdx < 0 || static_cast<size_t>(artIdx) >= this->runtimeArticulations.size()) return -1;
    return this->runtimeArticulations[static_cast<size_t>(artIdx)].pedalOpenCloseValue;
}

bool InstrumentSynth::controllerCanTransitionExistingVoices(int controllerNumber) const {
    for (const auto& controller : this->instrumentSlot.controllers) {
        if (controller.number == controllerNumber)
            return controller.codeIdx == KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose;
    }
    return false;
}

void InstrumentSynth::handleController(int midiChannel, int controllerNumber, int controllerValue) {
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

void InstrumentSynth::processPendingHiHatControllers() {
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

bool InstrumentSynth::isHiHatTransitionTargetAudible(const SampledSound& targetSound, double offsetSamples, float expectedTargetDb, float silenceFloorDb) const {
    if (targetSound.buffer == nullptr) return false;

    const auto& targetBuffer = *targetSound.buffer;
    const int windowStart = juce::jlimit(0, juce::jmax(0, targetBuffer.getNumSamples() - 1), static_cast<int>(offsetSamples));
    const int windowSize = juce::jmin(1024, targetBuffer.getNumSamples() - windowStart);
    if (windowSize <= 0) return false;

    int sourceChannelStart = 0;
    int sourceChannelCount = targetBuffer.getNumChannels();
    if (targetSound.sampleVariantSpec != nullptr) {
        sourceChannelStart = targetSound.sampleVariantSpec->fileChannelStart;
        if (targetSound.sampleVariantSpec->fileChannelCount > 0) sourceChannelCount = targetSound.sampleVariantSpec->fileChannelCount;
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

std::optional<float> InstrumentSynth::findHiHatReferenceOffsetMs(const InstrumentSynthVoice& sourceVoice, ArtRtIdx sourceArtIdx, const ArticulationRuntime& targetArt, int targetLayerIdx, uint8_t targetVariant, const SampledSound& replacementTargetSound, KitLib::MicSpec::Type& referenceMicType) const {
    if (this->parent == nullptr || targetLayerIdx < 0 || targetLayerIdx >= static_cast<int>(targetArt.layers.size())) return std::nullopt;

    const auto* sourceSound = sourceVoice.getCurrentlyPlayingSound();
    if (sourceSound == nullptr) return std::nullopt;
    const double sourceRate = sourceSound->sampleRate > 0.0 ? sourceSound->sampleRate : 44100.0;
    const float sourceElapsedMs = static_cast<float>((sourceVoice.sourceSamplePosition / sourceRate) * 1000.0);
    const auto& targetLayer = targetArt.layers[static_cast<size_t>(targetLayerIdx)];
    const auto& settings = this->parent->getEngineSettings();

    auto findReferenceVoice = [this, &sourceVoice, sourceArtIdx, sourceElapsedMs, targetVariant](KitLib::MicSpec::Type micType) -> InstrumentSynthVoice* {
        InstrumentSynthVoice* bestVoice = nullptr;
        float bestElapsedDifferenceMs = 25.0f;
        for (auto* candidateVoice : this->voices) {
            if (candidateVoice == nullptr || !candidateVoice->isVoiceActive()) continue;
            if (candidateVoice->getCurrentlyPlayingNote() != sourceVoice.getCurrentlyPlayingNote()) continue;
            if (candidateVoice->currentPlayingMidiChannel != sourceVoice.currentPlayingMidiChannel) continue;
            if (candidateVoice->getMidiVelocity() != sourceVoice.getMidiVelocity()) continue;
            if (this->getVoiceRuntimeArticulationIdx(*candidateVoice) != sourceArtIdx) continue;

            const auto* candidateSound = candidateVoice->getCurrentlyPlayingSound();
            if (candidateSound == nullptr || candidateSound->sampleVariantSpec == nullptr) continue;
            if (candidateSound->sampleVariantSpec->micSpec.type != micType) continue;
            if (KitLib::SampleVariantSpec::letterToIdx(candidateSound->sampleVariantSpec->letter) != targetVariant) continue;

            const double candidateRate = candidateSound->sampleRate > 0.0 ? candidateSound->sampleRate : 44100.0;
            const float candidateElapsedMs = static_cast<float>((candidateVoice->sourceSamplePosition / candidateRate) * 1000.0);
            const float elapsedDifferenceMs = std::abs(candidateElapsedMs - sourceElapsedMs);
            if (elapsedDifferenceMs > bestElapsedDifferenceMs) continue;
            bestElapsedDifferenceMs = elapsedDifferenceMs;
            bestVoice = candidateVoice;
        }
        return bestVoice;
    };

    auto findTargetEntry = [&targetLayer, targetVariant](const SampledSound& referenceSound) -> const SampleEntry* {
        const int referenceMicStart = referenceSound.micBufferChannelStart;
        const SampleEntry* targetEntry = nullptr;
        for (const auto& sampleEntry : targetLayer.samples) {
            if (sampleEntry.variantIdx == targetVariant && sampleEntry.sound != nullptr && sampleEntry.sound->micBufferChannelStart == referenceMicStart) {
                targetEntry = &sampleEntry;
                break;
            }
        }
        if (targetEntry != nullptr && targetEntry->sound->loaded.load(std::memory_order_acquire) && targetEntry->sound->buffer != nullptr) return targetEntry;
        for (const auto& sampleEntry : targetLayer.samples) {
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

void InstrumentSynth::processHiHatControllerTransition(int midiChannel, int controllerNumber, int controllerValue) {
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
        if (voice == nullptr || !voice->isVoiceActive() || voice->getEnvelopeState() != InstrumentSynthVoice::EnvelopeState::Playing)
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
        if (ss == nullptr || ss->sampleVariantSpec == nullptr) continue;

        const int currentArtIdx = this->getVoiceRuntimeArticulationIdx(*voice);
        if (currentArtIdx < 0) continue;

        if (currentArtIdx == static_cast<int>(newArtIdx)) continue; // already correct
        ++activeMismatchedVoices;

        // Now prepare to start a replacement voice from new articulation
        const auto& newArt = this->runtimeArticulations[static_cast<size_t>(newArtIdx)];
        // Choose the target layer from the original MIDI velocity, not from the calibrated output gain.
        const juce::uint8 midiVelocity = voice->getMidiVelocity();
        int layerIdx = newArt.velocityToLayer[static_cast<size_t>(midiVelocity)];
        if (layerIdx < 0 || layerIdx >= static_cast<int>(newArt.layers.size())) continue;
        const auto& layer = newArt.layers[static_cast<size_t>(layerIdx)];

        // find variant index of current voice
        uint8_t curVariant = 0;
        if (ss->sampleVariantSpec) curVariant = KitLib::SampleVariantSpec::letterToIdx(ss->sampleVariantSpec->letter);

        // find sample entry in target layer matching variant AND mic channel,
        // so each replacement voice writes to the same mic buffer channel as the
        // voice it replaces (overhead stays overhead, room stays room, etc.).
        const int voiceMicStart = ss->micBufferChannelStart;
        const InstrumentSynth::SampleEntry* targetSe = nullptr;
        for (const auto& se : layer.samples) {
            if (se.variantIdx == curVariant && se.sound != nullptr
                && se.sound->micBufferChannelStart == voiceMicStart) {
                targetSe = &se;
                break;
            }
        }
        // fallback: match variant only (different mic layout between articulations)
        if (targetSe == nullptr) {
            for (const auto& se : layer.samples) {
                if (se.variantIdx == curVariant) { targetSe = &se; break; }
            }
        }
        if (targetSe == nullptr && !layer.samples.empty()) targetSe = &layer.samples.front();
        if (targetSe == nullptr || targetSe->sound == nullptr) {
            ++skippedNoTarget;
            continue;
        }

        if (!targetSe->sound->loaded.load(std::memory_order_acquire) || targetSe->sound->buffer == nullptr) {
            for (const auto& se : layer.samples) {
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
        InstrumentSynthVoice* newVoice = nullptr;
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
            const auto referenceOffsetMs = this->findHiHatReferenceOffsetMs(*voice, static_cast<ArtRtIdx>(currentArtIdx), newArt, layerIdx, curVariant, *targetSe->sound, referenceMicType);
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
            if (ss->sampleVariantSpec != nullptr && ss->sampleVariantSpec->parent != nullptr && ss->sampleVariantSpec->parent->parent != nullptr)
                oldArtCode = ss->sampleVariantSpec->parent->parent->codeRaw;

            juce::String newArtCode = "unknown";
            if (targetSe->sound != nullptr && targetSe->sound->sampleVariantSpec != nullptr
                && targetSe->sound->sampleVariantSpec->parent != nullptr
                && targetSe->sound->sampleVariantSpec->parent->parent != nullptr)
                newArtCode = targetSe->sound->sampleVariantSpec->parent->parent->codeRaw;

            juce::String sampleName = "unknown";
            if (targetSe->sound != nullptr && targetSe->sound->sampleVariantSpec != nullptr)
                sampleName = targetSe->sound->sampleVariantSpec->sampleFile.getFileName();

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

bool InstrumentSynth::triggerHiHatPedalSample(int midiChannel, float sourceAudibleDb) {
    if (this->hihatPedalNote < 0)
        return false;

    const int pedalArtIdx = this->noteToArticulation[static_cast<size_t>(this->hihatPedalNote)];
    if (pedalArtIdx < 0 || pedalArtIdx >= static_cast<int>(this->runtimeArticulations.size()))
        return false;

    float pedalPeakDb = -100.0f;
    for (const auto& layer : this->runtimeArticulations[static_cast<size_t>(pedalArtIdx)].layers) {
        for (const auto& sampleEntry : layer.samples) {
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

bool InstrumentSynth::playNote(const int midiChannel, const int midiNoteNumber, const juce::uint8 velocity) {
    int artIdx = this->resolveArticulation(midiNoteNumber);
    if (artIdx >= 0 && artIdx < static_cast<int>(this->runtimeArticulations.size())) {
        if (LOG_ENABLED(Verbose))
            LOG(Verbose, "InstrumentSynth::playNote: note " + juce::String(midiNoteNumber) + " maps to expression " + this->getArtName(artIdx) + " (artIdx=" + juce::String(artIdx) + ")");
    } else {
        // LOG(Warning, "InstrumentSynth::playNote - no articulation available for note " + juce::String(midiNoteNumber));
        return false;
    }
    auto& art = this->runtimeArticulations[static_cast<size_t>(artIdx)];

    int layerIdx = this->resolveVelocityLayer(art, velocity);
    if (layerIdx < 0)
        return false;

    auto& layer = art.layers[static_cast<size_t>(layerIdx)];
    auto* logicalHit = this->chooseLogicalHit(layer);
    if (logicalHit == nullptr) return false;

    const float velocityGain = art.velocityGain[static_cast<size_t>(velocity)];
    this->lastRequestedVelocity.store(velocityGain);

    bool anyStarted = this->startLayerVoices(art, static_cast<ArtRtIdx>(artIdx), layer, *logicalHit, midiChannel, midiNoteNumber, velocityGain, velocity);
    if (!anyStarted) return false;

    this->applyAftertouch(midiChannel, midiNoteNumber);

    return true;
}

int InstrumentSynth::resolveArticulation(int midiNoteNumber) {
    if (this->runtimeArticulations.empty()) {
        if (!this->instrumentSlot.articulations.empty()) return 0;
        return -1;
    }
    if (midiNoteNumber < 0 || midiNoteNumber >= 128) return -1;
    int artIdx = this->noteToArticulation[static_cast<size_t>(midiNoteNumber)];
    if (parent != nullptr) {
        auto pair = this->noteToControllers.find(static_cast<uint8_t>(midiNoteNumber));
        if (pair != this->noteToControllers.end()) {
            for (auto ccNum : pair->second) {
                if (!this->ccToArticulationMask[ccNum]) continue;
                const auto ccVal = this->parent->getLatestContinuousControllerValue(ccNum);

                for (const auto& controller : this->instrumentSlot.controllers) {
                    if (controller.number != static_cast<int>(ccNum)) continue;
                    if (!vectorContains(controller.notes, static_cast<uint8_t>(midiNoteNumber))) continue;
                    artIdx = this->selectArticulationForController(static_cast<ArtRtIdx>(artIdx), controller.codeIdx, ccVal);
                    break;
                }
            }
        }
    }
    return artIdx;
}

int InstrumentSynth::resolveVelocityLayer(const ArticulationRuntime& art, juce::uint8 velocity) {
    int layerIdx = art.velocityToLayer[static_cast<size_t>(velocity)];
    if (layerIdx >= 0 && layerIdx < (int)art.layers.size()) {
        if (this->hasReadyLogicalHit(art.layers[static_cast<size_t>(layerIdx)])) return layerIdx;
    }

    int bestIdx = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int idx = 0; idx < (int)art.layers.size(); ++idx) {
        const auto& layer = art.layers[static_cast<size_t>(idx)];
        if (!this->hasReadyLogicalHit(layer)) continue;
        const float distance = static_cast<float>(std::abs(static_cast<int>(layer.velocity) - static_cast<int>(velocity)));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIdx = idx;
        }
    }
    return bestIdx;
}

bool InstrumentSynth::hasReadyLogicalHit(const VelocityLayerRuntime& layer) const {
    for (const auto& logicalHit : layer.logicalHits) {
        if (logicalHit.isReady()) return true;
    }
    return false;
}

InstrumentSynth::LogicalHitRuntime* InstrumentSynth::chooseLogicalHit(VelocityLayerRuntime& layer) {
    if (layer.logicalHits.empty()) return nullptr;

    const auto& settings = this->parent->getEngineSettings();
    const int roundRobinLock = settings.roundRobinLock.load();
    if (roundRobinLock > 0) {
        const size_t lockedIdx = static_cast<size_t>(roundRobinLock - 1) % layer.logicalHits.size();
        return layer.logicalHits[lockedIdx].isReady() ? &layer.logicalHits[lockedIdx] : nullptr;
    }

    size_t readyCount = 0;
    for (const auto& logicalHit : layer.logicalHits) {
        if (logicalHit.isReady()) ++readyCount;
    }
    if (readyCount == 0) return nullptr;

    const uint32_t roundRobinValue = layer.roundRobinCounter->fetch_add(1u, std::memory_order_relaxed);
    size_t readyTargetIdx = static_cast<size_t>(roundRobinValue) % readyCount;
    for (auto& logicalHit : layer.logicalHits) {
        if (!logicalHit.isReady()) continue;
        if (readyTargetIdx == 0) return &logicalHit;
        --readyTargetIdx;
    }
    return nullptr;
}

bool InstrumentSynth::startLayerVoices(ArticulationRuntime& art, ArtRtIdx artIdx, VelocityLayerRuntime& layer, LogicalHitRuntime& logicalHit, int midiChannel, int midiNoteNumber, float velocityGain, juce::uint8 midiVelocity) {
    const juce::ScopedLock sl(this->lock);
    const auto& settings = this->parent->getEngineSettings();
    const float closeCrossfadeSec = settings.hihatCloseCrossfadeSec.load();
    static constexpr size_t MAX_LOGICAL_HIT_VOICES = 64;
    if (!logicalHit.isReady() || logicalHit.sampleEntryIndexes.empty() || logicalHit.sampleEntryIndexes.size() > MAX_LOGICAL_HIT_VOICES) return false;

    std::array<InstrumentSynthVoice*, MAX_LOGICAL_HIT_VOICES> reservedVoices {};
    size_t reservedVoiceCount = 0;
    for (const size_t sampleEntryIdx : logicalHit.sampleEntryIndexes) {
        if (sampleEntryIdx >= layer.samples.size()) return false;
        const auto& sampleEntry = layer.samples[sampleEntryIdx];
        if (sampleEntry.sound == nullptr || !sampleEntry.sound->loaded.load(std::memory_order_acquire) || sampleEntry.sound->buffer == nullptr) return false;

        InstrumentSynthVoice* reservedVoice = nullptr;
        for (auto* candidateVoice : this->voices) {
            if (candidateVoice == nullptr || candidateVoice->isVoiceActive()) continue;
            bool alreadyReserved = false;
            for (size_t reservedIdx = 0; reservedIdx < reservedVoiceCount; ++reservedIdx) {
                if (reservedVoices[reservedIdx] == candidateVoice) {
                    alreadyReserved = true;
                    break;
                }
            }
            if (!alreadyReserved) {
                reservedVoice = candidateVoice;
                break;
            }
        }
        if (reservedVoice == nullptr) reservedVoice = this->findVoiceToSteal(midiChannel, midiNoteNumber, sampleEntry.sound->micBufferChannelStart, reservedVoices.data(), reservedVoiceCount);
        if (reservedVoice == nullptr) return false;
        reservedVoices[reservedVoiceCount++] = reservedVoice;
    }

    this->cullMaskedVoices(layer, logicalHit.variantIdx, midiChannel, midiNoteNumber, velocityGain);

    // Hi-hat articulation-order muting: a tighter/more-closed hit (higher poc rank)
    // fades out ringing voices from more-open articulations.
    if (this->type == KitLib::InstrumentSpec::Type::HiHat)
        this->chokeHiHatVoicesByPoc(art.pedalOpenCloseValue, closeCrossfadeSec);

    for (size_t logicalEntryIdx = 0; logicalEntryIdx < logicalHit.sampleEntryIndexes.size(); ++logicalEntryIdx) {
        const auto& sampleEntry = layer.samples[logicalHit.sampleEntryIndexes[logicalEntryIdx]];
        this->startSampleVoice(*reservedVoices[logicalEntryIdx], art, artIdx, sampleEntry, midiChannel, midiNoteNumber, velocityGain, midiVelocity);
    }
    return true;
}

void InstrumentSynth::fadeOutExistingVoices(const ArticulationRuntime& art, int midiChannel, int midiNoteNumber, float newVelocity) {
    const auto& settings = this->parent->getEngineSettings();
    const float polyphonyCrossfadeSec = settings.polyphonyCrossfadeTimeoutSec.load();
    const float crossArtChokeDbMargin = settings.hihatHitChokeDbMargin.load();
    const float newVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, newVelocity), -100.0f);

    for (auto* voice : voices) {
        if (!voice || !voice->isVoiceActive()) continue;
        if (voice->getCurrentlyPlayingNote() != midiNoteNumber) continue;
        if (!voice->isPlayingChannel(midiChannel)) continue;

        if (voice->artNameId == art.nameId && voice->artExprId == art.exprId) {
            if (newVelocity >= voice->getVelocityGain() - 0.05f) voice->requestFadeOut(polyphonyCrossfadeSec);
            continue;
        }

        // Cross-articulation choke for hi-hat: a new hit should choke old articulations,
        // but only if the new hit is loud enough relative to the old voice.
        // A quiet ghost note should not fully choke a loud ringing cymbal.
        if (this->type == KitLib::InstrumentSpec::Type::HiHat) {
            float newHitPeakDbEstimate = newVelocityDb;
            if (auto* currentSound = voice->getCurrentlyPlayingSound(); currentSound != nullptr && currentSound->decayEnvelope.isValid()) {
                newHitPeakDbEstimate = currentSound->decayEnvelope.peakDb + newVelocityDb;
            }

            const float oldCurrentDb = voice->getEstimatedCurrentDb();
            if (newHitPeakDbEstimate >= oldCurrentDb - crossArtChokeDbMargin) {
                voice->requestFadeOut(polyphonyCrossfadeSec);
            }
        }
    }
}

void InstrumentSynth::cullMaskedVoices(const VelocityLayerRuntime& layer, uint8_t chosenVariant, int midiChannel, int midiNoteNumber, float newVelocity) {
    const auto& settings = this->parent->getEngineSettings();
    const float voiceCullThresholdDb = settings.voiceCullThresholdDb.load();
    float newPeakDb = -100.0f;
    bool hasPeak = false;
    bool hasDirectPeak = false;
    const float newVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, newVelocity), -100.0f);

    for (const auto& se : layer.samples) {
        if (!se.sound || se.variantIdx != chosenVariant) continue;

        const auto& env = se.sound->decayEnvelope;
        if (!env.isValid()) continue;

        const bool isDirect = se.sound->sampleVariantSpec
            && se.sound->sampleVariantSpec->micSpec.type == KitLib::MicSpec::Type::Direct;
        const float candidatePeakDb = env.peakDb + newVelocityDb;

        if (!hasPeak || (isDirect && !hasDirectPeak) || (isDirect == hasDirectPeak && candidatePeakDb > newPeakDb)) {
            newPeakDb = candidatePeakDb;
            hasPeak = true;
            hasDirectPeak = isDirect;
        }
    }

    if (!hasPeak) return;

    // Stack-allocated flat arrays instead of std::unordered_map/set — avoids
    // heap allocation on every note-on. 128 entries covers the maximum voice
    // count per instrument (typically 10-100).
    static constexpr int MAX_GROUPS = 128;
    struct NoteOnGroup { juce::uint32 noteOnTime; float peakDb; };
    NoteOnGroup groups[MAX_GROUPS];
    int groupCount = 0;

    for (auto* voice : this->voices) {
        if (!voice || !voice->isVoiceActive()) continue;
        if (voice->getCurrentlyPlayingNote() != midiNoteNumber) continue;
        if (!(midiChannel <= 0 || voice->isPlayingChannel(midiChannel))) continue;

        auto* oldSound = voice->getCurrentlyPlayingSound();
        if (oldSound == nullptr) continue;

        const auto& oldEnv = oldSound->decayEnvelope;
        if (!oldEnv.isValid()) continue;

        const double oldSrcRate = oldSound->sampleRate > 0.0 ? oldSound->sampleRate : 44100.0;
        const float elapsedMs = static_cast<float>((voice->sourceSamplePosition / oldSrcRate) * 1000.0);
        const float oldVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, voice->velocityGain), -100.0f);
        const float oldAbsDb = oldEnv.getAbsoluteDbAtMs(elapsedMs) + oldVelocityDb;

        // Linear scan to find or insert group by noteOnTime
        int found = -1;
        for (int idx = 0; idx < groupCount; ++idx) {
            if (groups[idx].noteOnTime == voice->noteOnTime) { found = idx; break; }
        }
        if (found >= 0) {
            groups[found].peakDb = juce::jmax(groups[found].peakDb, oldAbsDb);
        } else if (groupCount < MAX_GROUPS) {
            groups[groupCount++] = { voice->noteOnTime, oldAbsDb };
        }
    }

    // Mark groups whose peak is below threshold for culling (reuse the array in-place)
    juce::uint32 cullTimes[MAX_GROUPS];
    int cullCount = 0;
    for (int idx = 0; idx < groupCount; ++idx) {
        if (groups[idx].peakDb < (newPeakDb - voiceCullThresholdDb))
            cullTimes[cullCount++] = groups[idx].noteOnTime;
    }

    if (cullCount == 0) return;

    for (auto* voice : this->voices) {
        if (!voice || !voice->isVoiceActive()) continue;

        for (int idx = 0; idx < cullCount; ++idx) {
            if (voice->noteOnTime == cullTimes[idx]) {
                voice->envelopeState = InstrumentSynthVoice::EnvelopeState::Dying;
                voice->killImmediately();
                break;
            }
        }
    }
}

void InstrumentSynth::chokeHiHatVoicesByPoc(int newArtPedalOpenCloseValue, float fadeTimeSec) {
    // newArtPedalOpenCloseValue < 0 means the triggering articulation carries no poc rank
    // (e.g. a non-hi-hat-openness hit) — nothing to do.
    if (newArtPedalOpenCloseValue < 0) return;

    for (auto* voice : this->voices) {
        if (!voice || !voice->isVoiceActive()) continue;
        if (voice->envelopeState == InstrumentSynthVoice::EnvelopeState::FadingOut
            || voice->envelopeState == InstrumentSynthVoice::EnvelopeState::Dying)
            continue;

        // Look up the poc rank stored in the matching ArticulationRuntime.
        // artNameId / artExprId are stamped onto the voice when it starts.
        const int voicePedalOpenCloseValue = this->getVoicePedalOpenCloseValue(*voice);

        if (voicePedalOpenCloseValue < 0) continue; // voice's articulation has no poc rank — skip

        // A strictly more-closed new hit (higher poc) chokes softer-ringing voices.
        // Equal-poc same-articulation retriggers are handled by existing cullMaskedVoices.
        if (newArtPedalOpenCloseValue > voicePedalOpenCloseValue) {
            if (LOG_ENABLED(Verbose))
                LOG(Verbose, "chokeHiHatVoicesByPoc: choking voice artPoc=" + juce::String(voicePedalOpenCloseValue)
                    + " newPoc=" + juce::String(newArtPedalOpenCloseValue)
                    + " note=" + juce::String(voice->getCurrentlyPlayingNote()));
            voice->requestFadeOut(fadeTimeSec);
        }
    }
}

void InstrumentSynth::startSampleVoice(InstrumentSynthVoice& voice, ArticulationRuntime& art, ArtRtIdx artIdx, const SampleEntry& se, int midiChannel, int midiNoteNumber, float velocityGain, juce::uint8 midiVelocity) {
    if (LOG_ENABLED(Verbose)) {
        auto& instCode = se.sound->sampleVariantSpec->parent->parent->parent->meta.codeRaw;
        auto& artCode = se.sound->sampleVariantSpec->parent->parent->codeRaw;
        auto& filePath = se.sound->sampleVariantSpec->sampleFile.getFullPathName();
        LOG(Verbose, "startVoice note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(midiVelocity)) + " inst " + instCode + " art " + artCode + " micBuf(" + juce::String(se.sound->micBufferChannelStart) + "," + juce::String(se.sound->micBufferChannelCount) + ") file=" + filePath);
    }
    // use samples mic index as voice's hitGroupId so we can use it in stealing logic to prefer stealing voices with same mic
    voice.hitGroupId = se.sound->micBufferChannelStart;

    // Apply overrides BEFORE startVoice so startNote() reads the correct values when
    // baking pitchRatio and ADSR parameters. Setting these after startVoice means the
    // first (and every) note-on uses stale defaults.
    voice.setPitchRatioMultiplier(std::pow(2.0, static_cast<double>(art.parameters.tuningCents) / 1200.0));
    voice.setAttackTimeSec(art.parameters.attackMs >= 0 ? static_cast<float>(art.parameters.attackMs) / 1000.0f : 0.001f);
    voice.setReleaseTimeSec(art.parameters.decayMs >= 0 ? static_cast<float>(art.parameters.decayMs) / 1000.0f : 0.1f);
    voice.setUseOneShotDecayEnvelope(art.parameters.decayMs >= 0);

    this->startVoice(&voice, se.sound, midiChannel, midiNoteNumber, velocityGain);

    voice.artNameId = art.nameId;
    voice.artExprId = art.exprId;
    voice.artRuntimeIdx = artIdx;
    voice.setVelocityGain(velocityGain);
    voice.setMidiVelocity(midiVelocity);
}
