#include "InstrumentSynth.h"
#include "DrumKitEngine.h"
#include "HiHatTransitionPolicy.h"
#include <cmath>

void InstrumentSynth::handleMidiEvent(const juce::MidiMessage& midiMessage) {
    if (midiMessage.isNoteOn()) {
        const int note = midiMessage.getNoteNumber();
        if (this->noteToArticulation[note] == -1) return;
        this->playNote(midiMessage.getChannel(), note, midiMessage.getVelocity());
    }
    else if (midiMessage.isController()) {
        auto ccNum = static_cast<uint8_t>(midiMessage.getControllerNumber());
        if (!this->ccToArticulationMask[ccNum]) return;
        this->handleController(midiMessage.getChannel(), ccNum, midiMessage.getControllerValue());
    }
    else if (midiMessage.isAftertouch())
        this->handleAftertouch(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getAfterTouchValue());
 
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

void InstrumentSynth::handleController(int midiChannel, int controllerNumber, int controllerValue) {
    const auto& settings = this->parent->getEngineSettings();
    const int boundaryHysteresisCc = settings.hihatBoundaryHysteresisCc.load();
    
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
    if (this->getType() != KitLib::InstrumentSpec::Type::HiHat) return;

    if (!this->ccToArticulationMask[controllerNumber]) return;

    const auto& mapArr = this->ccToArticulationTable[controllerNumber];
    const uint8_t mappedArtIdx = mapArr[juce::jlimit(0, 127, controllerValue)];
    if (mappedArtIdx >= this->runtimeArticulations.size()) {
        if (LOG_ENABLED(Warning))
            LOG(Warning, "handleController: CC " + juce::String(controllerNumber) 
                + " mapped to out-of-range articulation idx=" + juce::String((int)mappedArtIdx)
                + " (runtime size=" + juce::String((int)this->runtimeArticulations.size()) + ")");
        this->lastCcValue[controllerNumber] = controllerValue;
        return;
    }

    const int cachedArtIdx = this->lastCcArticulation[controllerNumber];
    std::array<int, 256> pocByArticulation {};
    const size_t articulationCount = juce::jmin(this->runtimeArticulations.size(), pocByArticulation.size());
    for (size_t articulationIdx = 0; articulationIdx < articulationCount; ++articulationIdx)
        pocByArticulation[articulationIdx] = this->runtimeArticulations[articulationIdx].pocValue;

    const int selectedArtIdx = HiHatTransitionPolicy::selectOpennessArticulation(
        mapArr,
        std::span<const int>(pocByArticulation.data(), articulationCount),
        controllerValue,
        this->lastCcValue[controllerNumber],
        cachedArtIdx,
        boundaryHysteresisCc);
    if (selectedArtIdx == cachedArtIdx) {
        this->lastCcValue[controllerNumber] = controllerValue;
        return;
    }
    const uint8_t newArtIdx = static_cast<uint8_t>(selectedArtIdx);
    if (this->transitionHiHatGroups(midiChannel, static_cast<ArtRtIdx>(newArtIdx)))
        this->lastCcArticulation[controllerNumber] = static_cast<int8_t>(newArtIdx);
    this->lastCcValue[controllerNumber] = controllerValue;
    return;
/*
    // Superseded by the group-coherent transaction above. Remove with the Phase 4
    // controller hot-path cleanup after its performance counters are extracted.
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

    // For each active voice, if its articulation differs from newArtIdx, crossfade
    for (auto* voice : this->voices) {
        if (voice == nullptr) continue;
        if (!voice->isVoiceActive()) continue;

        auto* ss = voice->getCurrentlyPlayingSound();
        if (ss == nullptr || ss->sampleVariantSpec == nullptr) continue;

        // Find articulation index for this voice via O(1) precomputed cache
        const auto* artSpec = ss->sampleVariantSpec->parent->parent;
        const uint64_t artKey = (static_cast<uint64_t>(artSpec->nameId) << 32) | static_cast<uint64_t>(artSpec->exprId);
        const auto artCacheIt = this->artIdByNameExprId.find(artKey);
        if (artCacheIt == this->artIdByNameExprId.end()) continue;
        const int currentArtIdx = artCacheIt->second;

        if (currentArtIdx == static_cast<int>(newArtIdx)) continue; // already correct
        ++activeMismatchedVoices;

        // Now prepare to start a replacement voice from new articulation
        const auto& newArt = this->runtimeArticulations[newArtIdx];
        // choose layer by current voice velocity
        int midiVel = static_cast<int>(juce::jlimit(0.0f, 1.0f, voice->getVelocityGain()) * 127.0f);
        int layerIdx = newArt.velocityToLayer[midiVel];
        if (layerIdx < 0 || layerIdx >= static_cast<int>(newArt.layers.size())) continue;
        const auto& layer = newArt.layers[layerIdx];

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

        // Compute offset for THIS voice independently from its own mic-specific
        // decay envelopes. Each mic has different decay characteristics (direct
        // mics peak early, room mics peak late), so sharing a single offset
        // across all mics produces wrong results — room mic replacements end up
        // in pre-peak / near-silent regions and die quickly.
        const auto& oldEnv = ss->decayEnvelope;
        const auto& newEnv = targetSe->sound->decayEnvelope;
        const double oldSrcRate = ss->sampleRate > 0.0 ? ss->sampleRate : 44100.0;
        const float elapsedMs = static_cast<float>((voice->sourceSamplePosition / oldSrcRate) * 1000.0);

        const float oldVelDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, voice->getVelocityGain()), -100.0f);
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
            oldVelDb,
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

        const double offsetMs = static_cast<double>(clampedMs);

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
            continue;
        }

        const double srcRate = targetSe->sound->sampleRate > 0.0 ? targetSe->sound->sampleRate : 44100.0;
        const double offsetSamples = (offsetMs / 1000.0) * srcRate;
        const double clampedOffset = juce::jlimit(0.0, juce::jmax(0.0, static_cast<double>(targetBuffer.getNumSamples() - 1)), offsetSamples);

        // Guard against replacing with an effectively silent region.
        // Use a threshold RELATIVE to the expected level from the decay envelope,
        // not an absolute floor. Quiet mics (overheads, rooms) have naturally low
        // peak levels and at late offsets their content can be at -60 to -80dB —
        // legitimate level-matched audio that must not be rejected.
        // Only skip if measured content is >24dB below the envelope's prediction
        // (indicating corrupt/empty sample), with an absolute floor at -90dB
        // to catch true digital silence.
        {
            const int windowStart = juce::jlimit(0, juce::jmax(0, targetBuffer.getNumSamples() - 1), static_cast<int>(clampedOffset));
            const int windowSize = juce::jmin(1024, targetBuffer.getNumSamples() - windowStart);
            if (windowSize <= 0) {
                ++skippedSilentTarget;
                continue;
            }

            // Only measure the channels this sample variant actually uses
            int srcStart = 0;
            int srcCount = targetBuffer.getNumChannels();
            if (targetSe->sound->sampleVariantSpec != nullptr) {
                srcStart = targetSe->sound->sampleVariantSpec->fileChannelStart;
                if (targetSe->sound->sampleVariantSpec->fileChannelCount > 0)
                    srcCount = targetSe->sound->sampleVariantSpec->fileChannelCount;
            }

            double sumSquares = 0.0;
            int sampleCount = 0;
            for (int sc = 0; sc < srcCount; ++sc) {
                const int ch = juce::jlimit(0, targetBuffer.getNumChannels() - 1, srcStart + sc);
                const float* readPtr = targetBuffer.getReadPointer(ch, windowStart);
                for (int i = 0; i < windowSize; ++i) {
                    const float sample = readPtr[i];
                    sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
                }
                sampleCount += windowSize;
            }

            const float rms = sampleCount > 0
                ? std::sqrt(static_cast<float>(sumSquares / static_cast<double>(sampleCount)))
                : 0.0f;
            const float rmsDb = juce::Decibels::gainToDecibels(juce::jmax(0.000001f, rms), -100.0f);

            // Relative threshold: envelope predicts content at ~targetRelativeDb,
            // allow 24dB margin for cymbal interference patterns & envelope imprecision.
            // Absolute floor at -90dB catches true digital silence.
            const float silenceThreshold = juce::jmax(targetRelativeDb - 24.0f, silenceFloorDb);
            if (rmsDb < silenceThreshold) {
                ++skippedSilentTarget;
                continue;
            }
        }

        const auto& currentArt = this->runtimeArticulations[static_cast<size_t>(currentArtIdx)];
        const auto transitionDirection = HiHatTransitionPolicy::getDirection(currentArt.pocValue, newArt.pocValue);
        const float transitionCrossfadeSec = transitionDirection == HiHatTransitionPolicy::Direction::Closing
            ? closeCrossfadeSec
            : openCrossfadeSec;
        voice->requestFadeOut(transitionCrossfadeSec);

        float vel = voice->getVelocityGain();
        this->startVoice(newVoice, targetSe->sound, midiChannel, voice->getCurrentlyPlayingNote(), vel);
        newVoice->hitGroupId = targetSe->sound->micBufferChannelStart;
        newVoice->artNameId = newArt.nameId;
        newVoice->artExprId = newArt.exprId;
        newVoice->sourceSamplePosition = clampedOffset;
        newVoice->setVelocityGain(vel);
        ++replacementsStarted;

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
            + " vel=" + juce::String(vel, 3)
            + " fromArt=" + oldArtCode
            + " toArt=" + newArtCode
            + " micStart=" + juce::String(targetSe->sound ? targetSe->sound->micBufferChannelStart : -1)
            + " micCount=" + juce::String(targetSe->sound ? targetSe->sound->micBufferChannelCount : 0)
            + " offsetMs=" + juce::String(offsetMs, 2)
            + " offsetSamples=" + juce::String(clampedOffset, 2)
            + " sample=" + sampleName
        );

    }

    // Only cache new articulation once at least one replacement starts, or when
    // there were no active mismatched voices to transition.
    if (replacementsStarted > 0 || activeMismatchedVoices == 0)
        this->lastCcArticulation[controllerNumber] = static_cast<int8_t>(newArtIdx);
    this->lastCcValue[controllerNumber] = controllerValue;

    if (activeMismatchedVoices > 0 && replacementsStarted == 0) {
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
    if (replacementsStarted > 0 && totalSkipped > 0) {
        LOG(Verbose, "HiHat pedal transition partial: cc=" + juce::String(controllerNumber)
            + " value=" + juce::String(controllerValue)
            + " targetArt=" + juce::String((int)newArtIdx)
            + " started=" + juce::String(replacementsStarted)
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
*/
}

bool InstrumentSynth::playNote(const int midiChannel, const int midiNoteNumber, const juce::uint8 velocity) {
    int artIdx = this->resolveArticulation(midiNoteNumber);
    if (artIdx >= 0 && artIdx < static_cast<int>(this->runtimeArticulations.size())) {
        LOG(Verbose, "InstrumentSynth::playNote: note " + juce::String(midiNoteNumber) + " maps to expression " + this->getArtName(artIdx) + " (artIdx=" + juce::String(artIdx) + ")");
    } else {
        // LOG(Warning, "InstrumentSynth::playNote - no articulation available for note " + juce::String(midiNoteNumber));
        return false;
    }
    auto& art = this->runtimeArticulations[artIdx];

    int layerIdx = this->resolveVelocityLayer(art, velocity);
    if (layerIdx < 0)
        return false;

    auto& layer = art.layers[layerIdx];

    uint8_t chosenVariant = this->chooseVariantGroup(layer);
    float velf = juce::jlimit(0.0f, 1.0f, velocity / 127.0f);

    this->lastRequestedVelocity.store(velf);

    bool anyStarted = this->startLayerVoices(static_cast<ArtRtIdx>(artIdx), art, layer, chosenVariant, midiChannel, midiNoteNumber, velf);
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
    int artIdx = this->noteToArticulation[midiNoteNumber];
    if (parent != nullptr) {
        auto pair = this->noteToControllers.find((uint8_t)midiNoteNumber);
        if (pair != this->noteToControllers.end()) {
            for (auto ccNum : pair->second) {
                if (!this->ccToArticulationMask[ccNum]) continue;
                const auto ccVal = this->parent->getLatestContinuousControllerValue(ccNum);
                const auto& mapArr = this->ccToArticulationTable[ccNum];
                artIdx = (int)mapArr[ccVal];
                // LOG(Debug, "CC " + juce::String(ccNum) + " value " + juce::String(ccVal) + " maps to expression " + this->getArtName(artIdx) + " (artIdx=" + juce::String(artIdx) + ") for note " + juce::String(midiNoteNumber) );
            }
        }
    }
    return artIdx;
}

int InstrumentSynth::resolveVelocityLayer(const ArticulationRuntime& art, juce::uint8 velocity) {
    int layerIdx = art.velocityToLayer[(int)velocity];
    if (layerIdx >= 0 && layerIdx < (int)art.layers.size()) {
        if (this->hasAnyLoadedVariant(art.layers[layerIdx]))
            return layerIdx;
    }

    int bestIdx = -1;
    int bestDist = 128;
    for (int idx = 0; idx < (int)art.layers.size(); ++idx) {
        const auto& layer = art.layers[idx];
        if (layer.samples.empty()) continue;
        if (!this->hasAnyLoadedVariant(layer)) continue;
        const int dist = std::abs((int)layer.velocity - (int)velocity);
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = idx;
        }
    }
    return bestIdx;
}

bool InstrumentSynth::hasAnyLoadedVariant(const VelocityLayerRuntime& layer) const {
    for (const auto& se : layer.samples) {
        if (se.sound == nullptr) continue;
        if (!se.sound->loaded.load(std::memory_order_acquire)) continue;
        if (se.sound->buffer == nullptr) continue;
        return true;
    }
    return false;
}

bool InstrumentSynth::isVariantReady(const VelocityLayerRuntime& layer, uint8_t variantIdx) const {
    bool seen = false;
    for (const auto& se : layer.samples) {
        if (se.variantIdx != variantIdx) continue;
        if (se.sound == nullptr) return false;
        seen = true;
        if (!se.sound->loaded.load(std::memory_order_acquire)) return false;
        if (se.sound->buffer == nullptr) return false;
    }
    return seen;
}

// Portable bit helpers for bitmask-based variant selection (no heap allocation).
namespace {
    inline uint8_t lowestSetBit(uint32_t mask) {
        for (uint8_t idx = 0; idx < 32; ++idx)
            if (mask & (1u << idx)) return idx;
        return 0;
    }
    inline int popCount(uint32_t mask) {
        int count = 0;
        while (mask) { mask &= (mask - 1); ++count; }
        return count;
    }
    inline uint32_t clearLowestSetBit(uint32_t mask) {
        return mask & (mask - 1);
    }
}

uint8_t InstrumentSynth::chooseVariantGroup(VelocityLayerRuntime& layer) {
    // Collect unique variant indices using a bitmask — no heap allocation.
    // Supports up to 32 variant indices (0..31), far more than any round-robin group.
    uint32_t allMask = 0;
    for (const auto& se : layer.samples) {
        if (se.variantIdx < 32)
            allMask |= (1u << se.variantIdx);
    }
    if (allMask == 0) return 0;

    // RR lock: force a specific variant index (1-based setting)
    const auto& settings = this->parent->getEngineSettings();
    const int roundRobinLock = settings.roundRobinLock.load();
    if (roundRobinLock > 0) {
        const int lockIdx = roundRobinLock - 1;
        int count = 0;
        for (uint32_t mask = allMask; mask != 0; mask = clearLowestSetBit(mask)) {
            if (count == lockIdx)
                return lowestSetBit(mask);
            ++count;
        }
        return lowestSetBit(allMask);
    }

    uint32_t readyMask = 0;
    for (uint32_t remaining = allMask; remaining != 0; ) {
        uint8_t variantIdx = lowestSetBit(remaining);
        remaining &= ~(1u << variantIdx);
        if (this->isVariantReady(layer, variantIdx))
            readyMask |= (1u << variantIdx);
    }

    const uint32_t choiceMask = readyMask != 0 ? readyMask : allMask;
    const int choiceCount = popCount(choiceMask);
    if (choiceCount == 1) return lowestSetBit(choiceMask);

    uint32_t rr = layer.roundRobinCounter->fetch_add(1u);
    int target = static_cast<int>(rr % static_cast<uint32_t>(choiceCount));
    uint32_t remaining = choiceMask;
    for (int idx = 0; idx < target; ++idx)
        remaining = clearLowestSetBit(remaining);
    return lowestSetBit(remaining);
}

bool InstrumentSynth::startLayerVoices(ArtRtIdx artIdx, ArticulationRuntime& art, VelocityLayerRuntime& layer, uint8_t chosenVariant, int midiChannel, int midiNoteNumber, float velocity) {
    const juce::ScopedLock sl(this->lock);
    const auto& settings = this->parent->getEngineSettings();
    const float closeCrossfadeSec = settings.hihatCloseCrossfadeSec.load();

    //temporary disabled to debug cc hat transitions
    this->cullMaskedVoices(layer, chosenVariant, midiChannel, midiNoteNumber, velocity);
    // this->fadeOutExistingVoices(art, midiChannel, midiNoteNumber, velocity);

    // Hi-hat articulation-order muting: a tighter/more-closed hit (higher poc rank)
    // fades out ringing voices from more-open articulations.
    if (this->type == KitLib::InstrumentSpec::Type::HiHat)
        this->chokeHiHatVoicesByPoc(art.pocValue, closeCrossfadeSec);

    const juce::uint32 eventId = this->allocateEventId();
    bool anyStarted = false;
    for (const auto& se : layer.samples) {
        if (!se.sound || se.variantIdx != chosenVariant) continue;

        if (!se.sound->loaded.load(std::memory_order_acquire) || se.sound->buffer == nullptr) {
            LOG(Debug, "Sample buffer not loaded for note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)) + " file=" + se.sound->sampleVariantSpec->sampleFile.getFullPathName());
            continue;
        }
        auto* voice = this->findFreeVoice(midiChannel, midiNoteNumber, se.sound->micBufferChannelStart);
        if (!voice) continue;
        this->startSampleVoice(*voice, artIdx, art, se, midiChannel, midiNoteNumber, velocity, eventId);
        anyStarted = true;
    }

    if (!anyStarted) {
        for (const auto& se : layer.samples) {
            if (!se.sound) continue;
            if (!se.sound->loaded.load(std::memory_order_acquire) || se.sound->buffer == nullptr) continue;
            auto* voice = this->findFreeVoice(midiChannel, midiNoteNumber, se.sound->micBufferChannelStart);
            if (!voice) continue;
            this->startSampleVoice(*voice, artIdx, art, se, midiChannel, midiNoteNumber, velocity, eventId);
            anyStarted = true;
        }
    }

    return anyStarted;
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
            if (newVelocity >= voice->getVelocityGain() - 0.05f)
                voice->requestFadeOut(polyphonyCrossfadeSec);
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
        if (!se.sound || se.variantIdx != chosenVariant)
            continue;

        const auto& env = se.sound->decayEnvelope;
        if (!env.isValid())
            continue;

        const bool isDirect = se.sound->sampleVariantSpec
            && se.sound->sampleVariantSpec->micSpec.type == KitLib::MicSpec::Type::Direct;
        const float candidatePeakDb = env.peakDb + newVelocityDb;

        if (!hasPeak || (isDirect && !hasDirectPeak) || (isDirect == hasDirectPeak && candidatePeakDb > newPeakDb)) {
            newPeakDb = candidatePeakDb;
            hasPeak = true;
            hasDirectPeak = isDirect;
        }
    }

    if (!hasPeak)
        return;

    // Stack-allocated flat arrays instead of std::unordered_map/set — avoids
    // heap allocation on every note-on. 128 entries covers the maximum voice
    // count per instrument (typically 10-100).
    static constexpr int MAX_GROUPS = 128;
    struct NoteOnGroup { juce::uint32 noteOnTime; float peakDb; };
    NoteOnGroup groups[MAX_GROUPS];
    int groupCount = 0;

    for (auto* voice : this->voices) {
        if (!voice || !voice->isVoiceActive())
            continue;
        if (voice->getCurrentlyPlayingNote() != midiNoteNumber)
            continue;
        if (!(midiChannel <= 0 || voice->isPlayingChannel(midiChannel)))
            continue;

        auto* oldSound = voice->getCurrentlyPlayingSound();
        if (oldSound == nullptr)
            continue;

        const auto& oldEnv = oldSound->decayEnvelope;
        if (!oldEnv.isValid())
            continue;

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

    if (cullCount == 0)
        return;

    for (auto* voice : this->voices) {
        if (!voice || !voice->isVoiceActive())
            continue;

        for (int idx = 0; idx < cullCount; ++idx) {
            if (voice->noteOnTime == cullTimes[idx]) {
                voice->envelopeState = InstrumentSynthVoice::EnvelopeState::Dying;
                voice->killImmediately();
                break;
            }
        }
    }
}

void InstrumentSynth::chokeHiHatVoicesByPoc(int newArtPocValue, float fadeTimeSec) {
    // newArtPocValue < 0 means the triggering articulation carries no poc rank
    // (e.g. a non-hi-hat-openness hit) — nothing to do.
    if (newArtPocValue < 0) return;

    for (auto* voice : this->voices) {
        if (!voice || !voice->isVoiceActive()) continue;
        if (voice->envelopeState == InstrumentSynthVoice::EnvelopeState::FadingOut
            || voice->envelopeState == InstrumentSynthVoice::EnvelopeState::Dying)
            continue;

        // Look up the poc rank stored in the matching ArticulationRuntime.
        // artNameId / artExprId are stamped onto the voice when it starts.
        int voicePocValue = -1;
        for (const auto& art : this->runtimeArticulations) {
            if (art.nameId == voice->artNameId && art.exprId == voice->artExprId) {
                voicePocValue = art.pocValue;
                break;
            }
        }

        if (voicePocValue < 0) continue; // voice's articulation has no poc rank — skip

        // A strictly more-closed new hit (higher poc) chokes softer-ringing voices.
        // Equal-poc same-articulation retriggers are handled by existing cullMaskedVoices.
        if (newArtPocValue > voicePocValue) {
            LOG(Verbose, "chokeHiHatVoicesByPoc: choking voice artPoc=" + juce::String(voicePocValue)
                + " newPoc=" + juce::String(newArtPocValue)
                + " note=" + juce::String(voice->getCurrentlyPlayingNote()));
            voice->requestFadeOut(fadeTimeSec);
        }
    }
}

void InstrumentSynth::startSampleVoice(InstrumentSynthVoice& voice, ArtRtIdx artIdx, ArticulationRuntime& art, const SampleEntry& se, int midiChannel, int midiNoteNumber, float velocity, juce::uint32 eventId) {
    if (LOG_ENABLED(Verbose)) {
        auto& instCode = se.sound->sampleVariantSpec->parent->parent->parent->meta.codeRaw;
        auto& artCode = se.sound->sampleVariantSpec->parent->parent->codeRaw;
        auto& filePath = se.sound->sampleVariantSpec->sampleFile.getFullPathName();
        LOG(Verbose, "startVoice note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)) + " inst " + instCode + " art " + artCode + " micBuf(" + juce::String(se.sound->micBufferChannelStart) + "," + juce::String(se.sound->micBufferChannelCount) + ") file=" + filePath);
    }
    // use samples mic index as voice's hitGroupId so we can use it in stealing logic to prefer stealing voices with same mic
    voice.hitGroupId = se.sound->micBufferChannelStart;

    // Apply overrides BEFORE startVoice so startNote() reads the correct values when
    // baking pitchRatio and ADSR parameters. Setting these after startVoice means the
    // first (and every) note-on uses stale defaults.
    voice.setPitchRatioMultiplier(std::pow(2.0, static_cast<double>(this->tuningOffsetCents) / 1200.0));
    voice.setAttackTimeSec(this->attackOverrideMs.has_value() ? static_cast<float>(*this->attackOverrideMs) / 1000.0f : 0.001f);
    voice.setReleaseTimeSec(this->decayOverrideMs.has_value() ? static_cast<float>(*this->decayOverrideMs) / 1000.0f : 0.1f);
    voice.setUseOneShotDecayEnvelope(this->decayOverrideMs.has_value());

    this->startVoice(&voice, se.sound, midiChannel, midiNoteNumber, velocity, eventId);

    voice.runtimeArticulationIdx = artIdx;
    voice.artNameId = art.nameId;
    voice.artExprId = art.exprId;
    voice.setVelocityGain(velocity);
}
