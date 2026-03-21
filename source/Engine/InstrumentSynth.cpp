#pragma once

#include "InstrumentSynth.h"
#include "DrumKitEngine.h"
#include <unordered_set>

static constexpr float POLYPHONY_CROSFADE_TIMEOUT_SEC = 0.1f;
// Small hysteresis (in CC units 0..127) to avoid rapid flips when controller
// value jitters around an articulation boundary.
static constexpr int HIHAT_PEDAL_CC_HYSTERESIS = 2;

void InstrumentSynth::handleMidiEvent(const juce::MidiMessage& midiMessage) {
    if (midiMessage.isNoteOn()) {
        const int note = midiMessage.getNoteNumber();
        if (this->noteToArticulation[note] == -1) return;
        this->playNote(midiMessage.getChannel(), note, midiMessage.getVelocity());
    }
    else if (midiMessage.isController()) {
        auto ccNum = static_cast<uint8_t>(midiMessage.getControllerNumber());
        if (this->ccToArticulation.find(ccNum) == this->ccToArticulation.end()) return;
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
    //FIXME: currently this spams sample change on each tiny change of controller, need to make it make change only if articulation index actually changes, and also avoid spamming when controller value is around articulation change threshold +-
    
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

    auto it = this->ccToArticulation.find(static_cast<int>(controllerNumber));
    if (it == this->ccToArticulation.end()) return;

    const auto& mapArr = it->second;
    uint8_t newArtIdx = mapArr[juce::jlimit(0, 127, controllerValue)];

    // Check cached last seen value/articulation for this CC to avoid spamming
    // articulation change logic on tiny controller changes.
    const auto lastValIt = this->lastCcValue.find(controllerNumber);
    const auto lastArtIt = this->lastCcArticulation.find(controllerNumber);

    if (lastArtIt != this->lastCcArticulation.end() && lastArtIt->second == newArtIdx) {
        // Articulation already applied for this controller; just remember latest value
        this->lastCcValue[controllerNumber] = controllerValue;
        return;
    }

    if (lastValIt != this->lastCcValue.end()) {
        const int lastVal = lastValIt->second;
        if (std::abs(controllerValue - lastVal) < HIHAT_PEDAL_CC_HYSTERESIS) {
            // Value changed only slightly within hysteresis window — skip processing
            // but keep cached last value up-to-date.
            this->lastCcValue[controllerNumber] = controllerValue;
            return;
        }
    }

    struct PedalReplacementState {
        bool decided = false;
        bool skip = false;
        bool hasDirectMicAnchor = false;
        double offsetMs = 0.0;
    };
    std::unordered_map<int, PedalReplacementState> replacementStates;

    // For each active voice, if its articulation differs from newArtIdx, crossfade
    for (auto* voice : this->voices) {
        if (voice == nullptr) continue;
        if (!voice->isVoiceActive()) continue;

        auto* ss = voice->getCurrentlyPlayingSound();
        if (ss == nullptr || ss->sampleVariantSpec == nullptr) continue;

        // Find articulation index for this voice by matching nameId/exprId
        int currentArtIdx = -1;
        for (size_t ai = 0; ai < this->runtimeArticulations.size(); ++ai) {
            const auto& art = this->runtimeArticulations[ai];
            if (art.nameId == ss->sampleVariantSpec->parent->parent->nameId && art.exprId == ss->sampleVariantSpec->parent->parent->exprId) {
                currentArtIdx = static_cast<int>(ai);
                break;
            }
        }
        if (currentArtIdx == -1) continue;

        if (currentArtIdx == static_cast<int>(newArtIdx)) continue; // already correct

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

        // find sample entry in target layer matching variant or fallback to first
        const InstrumentSynth::SampleEntry* targetSe = nullptr;
        for (const auto& se : layer.samples) {
            if (se.variantIdx == curVariant) { targetSe = &se; break; }
        }
        if (targetSe == nullptr && !layer.samples.empty()) targetSe = &layer.samples.front();
        if (targetSe == nullptr || targetSe->sound == nullptr) continue;

        if (targetSe->sound->buffer == nullptr) continue;

        const int replacementKey =
            ((voice->getCurrentlyPlayingNote() & 0xFF) << 16)
            | ((currentArtIdx & 0xFF) << 8)
            | (static_cast<int>(newArtIdx) & 0xFF);
        auto& state = replacementStates[replacementKey];

        const bool isDirectMic = ss->sampleVariantSpec && ss->sampleVariantSpec->micSpec.type == KitLib::MicSpec::Type::Direct;
        const bool shouldRecomputeForDirect = !state.skip && state.decided && isDirectMic && !state.hasDirectMicAnchor;

        if (!state.decided || shouldRecomputeForDirect) {
            const auto& oldEnv = ss->decayEnvelope;
            const auto& newEnv = targetSe->sound->decayEnvelope;
            if (!oldEnv.isValid() || !newEnv.isValid()) {
                state.skip = true;
                state.decided = true;
                continue;
            }

            const double oldSrcRate = ss->sampleRate > 0.0 ? ss->sampleRate : 44100.0;
            const double elapsedMs = (voice->sourceSamplePosition / oldSrcRate) * 1000.0;
            const float currentAbsDb = oldEnv.getAbsoluteDbAtMs(static_cast<float>(elapsedMs));
            float targetMs = newEnv.getMsAtAbsoluteDb(currentAbsDb);

            if (targetMs < 0.0f) {
                state.skip = true;
                state.decided = true;
                continue;
            }

            const auto& targetBuffer = *targetSe->sound->buffer;
            const double targetSrcRate = targetSe->sound->sampleRate > 0.0 ? targetSe->sound->sampleRate : 44100.0;
            const double sampleLengthMs = targetSrcRate > 0.0
                ? (static_cast<double>(targetBuffer.getNumSamples()) / targetSrcRate) * 1000.0
                : 0.0;

            const float clampedMs = juce::jlimit(newEnv.peakMs, static_cast<float>(sampleLengthMs), targetMs);
            state.offsetMs = static_cast<double>(clampedMs);
            state.decided = true;
            state.skip = false;
            state.hasDirectMicAnchor = isDirectMic;
        }

        if (state.skip) continue;

        const float fadeInSec = 0.005f;
        voice->fadeOutSec(fadeInSec);

        // Start replacement voice with small offset to avoid attack transient
        auto* newVoice = this->findFreeVoice(midiChannel, voice->getCurrentlyPlayingNote(), voice->hitGroupId);
        if (newVoice == nullptr) continue;
        float vel = voice->getVelocityGain();
        // Start voice; this will call startNote and set ADSR
        this->startVoice(newVoice, targetSe->sound, midiChannel, voice->getCurrentlyPlayingNote(), vel);

        const double srcRate = targetSe->sound->sampleRate > 0.0 ? targetSe->sound->sampleRate : 44100.0;
        const auto& targetBuffer = *targetSe->sound->buffer;
        const double offsetSamples = (state.offsetMs / 1000.0) * srcRate;
        newVoice->sourceSamplePosition = juce::jlimit(0.0, juce::jmax(0.0, static_cast<double>(targetBuffer.getNumSamples() - 1)), offsetSamples);
        newVoice->setVelocityGain(vel);

    }

    // Remember that we've applied this articulation for this controller and
    // the last seen value, so small jitter won't retrigger immediately.
    this->lastCcArticulation[controllerNumber] = newArtIdx;
    this->lastCcValue[controllerNumber] = controllerValue;
}

bool InstrumentSynth::playNote(const int midiChannel, const int midiNoteNumber, const juce::uint8 velocity) {
    int artIdx = this->resolveArticulation(midiNoteNumber);
    if (artIdx >= 0 && artIdx < static_cast<int>(this->runtimeArticulations.size())) {
        // LOG(Debug, "InstrumentSynth::playNote: note " + juce::String(midiNoteNumber) + " maps to expression " + this->getArtName(artIdx) + " (artIdx=" + juce::String(artIdx) + ")");
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

    bool anyStarted = this->startLayerVoices(art, layer, chosenVariant, midiChannel, midiNoteNumber, velf);
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
    if (!this->ccToArticulation.empty() && parent != nullptr) {
        auto pair = this->noteToControllers.find((uint8_t)midiNoteNumber);
        if (pair != this->noteToControllers.end()) {
            for (auto ccNum : pair->second) {
                auto artIt = this->ccToArticulation.find(ccNum);
                if (artIt == this->ccToArticulation.end()) continue;
                const auto ccVal = this->parent->getLatestContinuousControllerValue(ccNum);
                const auto& mapArr = artIt->second;
                artIdx = (int)mapArr[ccVal];
                // LOG(Debug, "CC " + juce::String(ccNum) + " value " + juce::String(ccVal) + " maps to expression " + this->getArtName(artIdx) + " (artIdx=" + juce::String(artIdx) + ") for note " + juce::String(midiNoteNumber) );
            }
        }
    }
    return artIdx;
}

int InstrumentSynth::resolveVelocityLayer(const ArticulationRuntime& art, juce::uint8 velocity) {
    int layerIdx = art.velocityToLayer[(int)velocity];
    if (layerIdx < 0 || layerIdx >= (int)art.layers.size()) return -1;
    if (art.layers[layerIdx].samples.empty()) return -1;
    return layerIdx;
}

uint8_t InstrumentSynth::chooseVariantGroup(VelocityLayerRuntime& layer) {
    //TODO001: since some samples are loading in another thread, not all variants are available for playback before instrument loaded, so need to change architecture, increase loaded layers/variants count that was loaded in SampleLoader and use only then
    std::vector<uint8_t> uniqueVariants;
    uniqueVariants.reserve(layer.samples.size());
    for (const auto& se : layer.samples) {
        uint8_t key = se.variantIdx;
        if (std::find(uniqueVariants.begin(), uniqueVariants.end(), key) == uniqueVariants.end())
            uniqueVariants.push_back(key);
    }
    if (uniqueVariants.empty()) return 0;
    if (uniqueVariants.size() == 1) return uniqueVariants[0];
    uint32_t rr = layer.roundRobinCounter->fetch_add(1u);
    size_t idx = rr % uniqueVariants.size();
    return uniqueVariants[idx];
}





bool InstrumentSynth::startLayerVoices(ArticulationRuntime& art, VelocityLayerRuntime& layer, uint8_t chosenVariant, int midiChannel, int midiNoteNumber, float velocity) {
    const juce::ScopedLock sl(this->lock);

    this->cullMaskedVoices(layer, chosenVariant, midiChannel, midiNoteNumber, velocity);
    this->fadeOutExistingVoices(art, midiChannel, midiNoteNumber, velocity);

    bool anyStarted = false;
    for (const auto& se : layer.samples) {
        if (!se.sound || se.variantIdx != chosenVariant) continue;
        
        if (se.sound->buffer == nullptr) {
            LOG(Warning, "Sample buffer not loaded for note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)) + " file=" + se.sound->sampleVariantSpec->sampleFile.getFullPathName());
            continue;
        }
        auto* voice = this->findFreeVoice(midiChannel, midiNoteNumber, se.sound->micBufferChannelStart);
        if (!voice) continue;
        this->startSampleVoice(*voice, art, se, midiChannel, midiNoteNumber, velocity);
        anyStarted = true;
    }
    return anyStarted;
}

void InstrumentSynth::fadeOutExistingVoices(const ArticulationRuntime& art, int midiChannel, int midiNoteNumber, float newVelocity) {
    for (auto* voice : voices) {
        if (!voice || !voice->isVoiceActive()) continue;
        if (voice->getCurrentlyPlayingNote() != midiNoteNumber) continue;
        if (!voice->isPlayingChannel(midiChannel)) continue;

        if (voice->artNameId == art.nameId && voice->artExprId == art.exprId) {
            if (newVelocity >= voice->getVelocityGain() - 0.05f)
                voice->fadeOutSec(POLYPHONY_CROSFADE_TIMEOUT_SEC);
            continue;
        }

        if (this->type == KitLib::InstrumentSpec::Type::HiHat) {
            voice->fadeOutSec(POLYPHONY_CROSFADE_TIMEOUT_SEC);
        }
    }
}

void InstrumentSynth::cullMaskedVoices(const VelocityLayerRuntime& layer, uint8_t chosenVariant, int midiChannel, int midiNoteNumber, float newVelocity) {
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

    std::unordered_set<juce::uint32> noteOnTimesToCull;
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

        if (oldAbsDb < (newPeakDb - VOICE_CULL_THRESHOLD_DB))
            noteOnTimesToCull.insert(voice->noteOnTime);
    }

    if (noteOnTimesToCull.empty())
        return;

    for (auto* voice : this->voices) {
        if (!voice || !voice->isVoiceActive())
            continue;

        if (noteOnTimesToCull.find(voice->noteOnTime) != noteOnTimesToCull.end()) {
            voice->envelopeState = InstrumentSynthVoice::EnvelopeState::Dying;
            voice->killImmediately();
        }
    }
}

void InstrumentSynth::startSampleVoice(InstrumentSynthVoice& voice, ArticulationRuntime& art, const SampleEntry& se, int midiChannel, int midiNoteNumber, float velocity) {
    if (false) {
        auto& instCode = se.sound->sampleVariantSpec->parent->parent->parent->meta.codeRaw;
        auto& artCode = se.sound->sampleVariantSpec->parent->parent->codeRaw;
        auto& filePath = se.sound->sampleVariantSpec->sampleFile.getFullPathName();
        LOG(Debug, "startVoice note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)) + " inst " + instCode + " art " + artCode + " micBuf(" + juce::String(se.sound->micBufferChannelStart) + "," + juce::String(se.sound->micBufferChannelCount) + ") file=" + filePath);
    }
    // use samples mic index as voice's hitGroupId so we can use it in stealing logic to prefer stealing voices with same mic
    voice.hitGroupId = se.sound->micBufferChannelStart;

    this->startVoice(&voice, se.sound, midiChannel, midiNoteNumber, velocity);

    voice.artNameId = art.nameId;
    voice.artExprId = art.exprId;
    voice.setVelocityGain(velocity);
}




void InstrumentSynth::renderNextBlock(juce::AudioBuffer<float>& outputAudio, const juce::MidiBuffer& inputMidi, int startSample, int numSamples) {
    jassert(!juce::exactlyEqual(sampleRate, 0.0));
    const int targetChannels = outputAudio.getNumChannels();

    auto midiIterator = inputMidi.findNextSamplePosition(startSample);

    bool firstEvent = true;

    const juce::ScopedLock sl(this->lock);

    for (; numSamples > 0; ++midiIterator) {
        if (midiIterator == inputMidi.cend()) {
            if (targetChannels > 0)
                this->renderVoices(outputAudio, startSample, numSamples);
            return;
        }

        const auto metadata = *midiIterator;
        const int samplesToNextMidiMessage = metadata.samplePosition - startSample;

        if (samplesToNextMidiMessage >= numSamples) {
            if (targetChannels > 0)
                this->renderVoices(outputAudio, startSample, numSamples);

            this->handleMidiEvent(metadata.getMessage());
            break;
        }

        if (samplesToNextMidiMessage < ((firstEvent && !subBlockSubdivisionIsStrict) ? 1 : minimumSubBlockSize)) {
            this->handleMidiEvent(metadata.getMessage());
            continue;
        }

        firstEvent = false;

        if (targetChannels > 0)
            this->renderVoices(outputAudio, startSample, samplesToNextMidiMessage);

        this->handleMidiEvent(metadata.getMessage());
        startSample += samplesToNextMidiMessage;
        numSamples -= samplesToNextMidiMessage;
    }

    std::for_each(midiIterator, inputMidi.cend(), [&](const auto& meta) { handleMidiEvent(meta.getMessage()); });
}

void InstrumentSynth::renderVoices(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) {
    this->applyAdaptiveTailShortening();
    for (auto* voice : this->voices) voice->renderNextBlock(buffer, startSample, numSamples);
}

InstrumentSynthVoice* InstrumentSynth::findVoiceToSteal(int /*midiChannel*/, int /*midiNoteNumber*/, int groupId) const {
    std::vector<InstrumentSynthVoice*> voiceCandidates;

    for (auto* voice : this->voices) {
        if (!voice->isVoiceActive()) continue;
        if (voice->hitGroupId != groupId) continue;
        voiceCandidates.push_back(voice);
    }

    if (voiceCandidates.empty()) return nullptr;

    auto pickOldest = [](const std::vector<InstrumentSynthVoice*>& arr) -> InstrumentSynthVoice* {
        if (arr.empty())
            return nullptr;
        auto* oldest = arr.front();
        for (auto* v : arr)
            if (v->wasStartedBefore(*oldest))
                oldest = v;
        return oldest;
    };

    auto collectByState = [&](InstrumentSynthVoice::EnvelopeState state) {
        std::vector<InstrumentSynthVoice*> result;
        for (auto* v : voiceCandidates)
            if (v->getEnvelopeState() == state)
                result.push_back(v);
        return result;
    };

    auto dying = collectByState(InstrumentSynthVoice::EnvelopeState::Dying);
    if (!dying.empty())
        return pickOldest(dying);

    auto fading = collectByState(InstrumentSynthVoice::EnvelopeState::FadingOut);
    if (!fading.empty()) {
        InstrumentSynthVoice* best = nullptr;
        float bestDb = std::numeric_limits<float>::infinity();
        for (auto* v : fading) {
            const float db = v->getEstimatedCurrentDb();
            if (!best || db < bestDb) {
                best = v;
                bestDb = db;
            }
        }
        if (best) return best;
    }

    auto releasing = collectByState(InstrumentSynthVoice::EnvelopeState::Releasing);
    if (!releasing.empty()) {
        InstrumentSynthVoice* best = nullptr;
        float bestDb = std::numeric_limits<float>::infinity();
        for (auto* v : releasing) {
            const float db = v->getEstimatedCurrentDb();
            if (!best || db < bestDb) {
                best = v;
                bestDb = db;
            }
        }
        if (best) return best;
    }

    std::vector<InstrumentSynthVoice*> playing;
    for (auto* v : voiceCandidates)
        if (v->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing)
            playing.push_back(v);

    if (!playing.empty()) {
        InstrumentSynthVoice* protectedVoice = nullptr;
        float protectedDb = -std::numeric_limits<float>::infinity();
        uint32_t protectedTime = 0;

        for (auto* candidate : playing) {
            const float db = candidate->getEstimatedCurrentDb();
            if (!protectedVoice || db > protectedDb || (juce::approximatelyEqual(db, protectedDb) && candidate->noteOnTime > protectedTime)) {
                protectedVoice = candidate;
                protectedDb = db;
                protectedTime = candidate->noteOnTime;
            }
        }

        InstrumentSynthVoice* best = nullptr;
        float bestDb = std::numeric_limits<float>::infinity();
        for (auto* candidate : playing) {
            if (playing.size() > 1 && candidate == protectedVoice)
                continue;

            const float db = candidate->getEstimatedCurrentDb();
            if (!best || db < bestDb) {
                best = candidate;
                bestDb = db;
            }
        }

        if (best != nullptr)
            return best;

        if (protectedVoice != nullptr)
            return protectedVoice;
    }

    return pickOldest(voiceCandidates);
}

void InstrumentSynth::prepareToPlay(double newSampleRate, int samplesPerBlock) {
    // Ensure mic buffer has correct size
    if (mMicChannelCount <= 0)
        mMicChannelCount = 2;
    micBuffer.setSize(mMicChannelCount, samplesPerBlock);

    // Configure voices for this synth
    this->clearVoices();

    // Dynamically determine required voices based on the maximum number of simultaneous
    // sample entries that may be started for a single note. This counts how many
    // samples belong to the same round-robin variant group (variantIdx) per layer,
    // and uses the largest such group across all articulations and layers.
    int maxSimultaneousSamples = 0;
    for (const auto& art : this->runtimeArticulations) {
        for (const auto& layer : art.layers) {
            std::unordered_map<uint8_t, int> groupCounts;
            for (const auto& se : layer.samples) {
                uint8_t key = se.variantIdx; // 0 means no-letter variants
                groupCounts[key]++;
            }
            for (const auto& kv : groupCounts) {
                if (kv.second > maxSimultaneousSamples) maxSimultaneousSamples = kv.second;
            }
        }
    }

    // Minimum voices to keep behavior stable
    const int minVoices = 10;
    const int numVoices = std::max(minVoices, maxSimultaneousSamples * minVoices);
    for (int i = 0; i < numVoices; ++i)
        this->addVoice(new InstrumentSynthVoice());
    LOG(Debug, "prepareToPlay - instrument: " + this->instrumentSlot.id + " sampleRate=" + juce::String(newSampleRate) + " samplesPerBlock=" + juce::String(samplesPerBlock) + " numVoices=" + juce::String(numVoices));

    // set playback sample rate for voices
    this->setCurrentPlaybackSampleRate(newSampleRate);

    for (auto* voice : this->voices) //TODO: maybe refactor to separate method
        voice->chokeProcessor.prepare(newSampleRate);
}

void InstrumentSynthVoice::startNote(int midiNoteNumber, float velocity, SampledSound* sound) {
    juce::ignoreUnused(midiNoteNumber);
    this->velocityGain = velocity;
    this->sourceSamplePosition = 0.0;
    this->envelopeState = EnvelopeState::Playing;
    // Apply ADSR using configured attack/release
    this->adsr.setParameters({ this->attackTimeSec, 0.0f, 1.0f, this->releaseTimeSec });
    this->adsr.setSampleRate(this->getSampleRate());
    this->adsr.noteOn();

    this->tailShortenFactor = 1.0f;

    this->chokeProcessor.noteStarted();
    // LOG(Debug, "InstrumentSynthVoice: startNote velocity=" + juce::String(velocity) + " pitchRatio=" + juce::String(this->pitchRatio));

    if (sound->sampleRate > 0 && getSampleRate() > 0)
        this->pitchRatio = sound->sampleRate / getSampleRate();
    else
        this->pitchRatio = 1.0;

}

void InstrumentSynthVoice::fadeOutSec(float sec) {
    this->requestFadeOut(sec);
}

void InstrumentSynthVoice::requestFadeOut(float durationSec) {
    if (this->envelopeState == EnvelopeState::Idle)
        return;
    this->envelopeState = EnvelopeState::FadingOut;
    const float releaseSec = juce::jmax(0.001f, durationSec);
    juce::ADSR::Parameters p = this->adsr.getParameters();
    p.release = releaseSec;
    this->adsr.setParameters(p);
    this->adsr.noteOff();
}

void InstrumentSynthVoice::requestRelease() {
    if (this->envelopeState == EnvelopeState::Idle)
        return;
    this->envelopeState = EnvelopeState::Releasing;
    juce::ADSR::Parameters p = this->adsr.getParameters();
    p.release = this->releaseTimeSec * this->tailShortenFactor;
    this->adsr.setParameters(p);
    this->adsr.noteOff();
}

void InstrumentSynthVoice::killImmediately() {
    this->envelopeState = EnvelopeState::Dying;
    this->adsr.reset();
    this->clearCurrentNote();
}

float InstrumentSynthVoice::getEstimatedCurrentDb() const {
    if (!this->isVoiceActive())
        return -100.0f;

    auto* s = this->currentlyPlayingSound;
    if (s == nullptr || !s->decayEnvelope.isValid()) {
        const float gain = juce::jmax(0.0001f, this->getCurrentAmplitude());
        return juce::Decibels::gainToDecibels(gain, -100.0f);
    }

    const double srcRate = s->sampleRate > 0.0 ? s->sampleRate : 44100.0;
    const float elapsedMs = static_cast<float>((this->sourceSamplePosition / srcRate) * 1000.0);
    const float velDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, this->velocityGain), -100.0f);
    return s->decayEnvelope.getAbsoluteDbAtMs(elapsedMs) + velDb;
}

void InstrumentSynthVoice::stopNote(bool allowTailOff) {
    if (allowTailOff) {
        this->requestRelease();
    } else {
        this->killImmediately();
    }
}

void InstrumentSynthVoice::aftertouchChanged(int newValue) {
    // LOG(Debug, "aftertouchChanged " + juce::String(newValue));
    this->chokeProcessor.handlePolyAftertouch(newValue);
}

void InstrumentSynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) {
    if (!this->isVoiceActive())
        return;

    SampledSound* s = this->getCurrentlyPlayingSound();
    if (s == nullptr || !s->buffer)
        return;

    auto& buf = *s->buffer;
    const int srcChannels = buf.getNumChannels();
    const int srcLen = buf.getNumSamples();
    // Determine source/destination channel mapping based on SampleVariantSpec metadata.
    int srcStart = 0;
    int srcCount = srcChannels;
    if (s->sampleVariantSpec) {
        srcStart = s->sampleVariantSpec->fileChannelStart;
        srcCount = s->sampleVariantSpec->fileChannelCount > 0 ? s->sampleVariantSpec->fileChannelCount : srcChannels;
    }

    const int destChCount = outputBuffer.getNumChannels();

    if (this->chokeProcessor.isMuted()) {
        this->stopNote(false);
        return;
    }
    for (int i = 0; i < numSamples; ++i) {
        //TODO: use peak offset for mics (found minimal (for example di) and take it as 0, based on that calculate for all other mics of that sample variant relatively, for example far room = 10ms etc...) first makke script to smart-parse from samples and save to filename
        // Process per-voice ducking for this block and get block gain
        const float chokeGain = this->chokeProcessor.getNextBlockGain();

        const double pos = this->sourceSamplePosition;
        const int idx = static_cast<int>(pos);
        const double frac = pos - idx;

        // For each source channel in this sample variant, fetch interpolated sample
        // and write to corresponding destination channel (offset by srcStart).
        if (idx >= 0 && idx < srcLen) {
            const auto env = this->adsr.getNextSample();
            this->lastEnvelopeValue = env;

            for (int sc = 0; sc < srcCount; ++sc) {
                int actualSrcCh = juce::jlimit(0, srcChannels - 1, srcStart + sc);
                float v0 = buf.getSample(actualSrcCh, idx);
                float v1 = (idx + 1 < srcLen) ? buf.getSample(actualSrcCh, idx + 1) : 0.0f;
                auto sample = static_cast<float>(v0 + (v1 - v0) * frac) * env * this->velocityGain * chokeGain;

                int destCh = s->micBufferChannelStart + sc;
                if (destCh >= 0 && destCh < destChCount) {
                    outputBuffer.addSample(destCh, startSample + i, sample);
                } else {
                    // Attempt to write outside of provided mic buffer — warn once per occurrence
                    // if (s->sampleVariantSpec) {
                    //     LOG(Warning, "Render: attempted write to micBuffer destCh=" + juce::String(destCh) + " but mic buffer has " + juce::String(destChCount) + " channels. Sample=" + s->sampleVariantSpec->sampleFileName);
                    // } else {
                    //     LOG(Warning, "Render: attempted write to micBuffer destCh=" + juce::String(destCh) + " but mic buffer has " + juce::String(destChCount) + " channels.");
                    // }
                }
            }
        } else {
            // still advance envelope even if out of source range
            (void) this->adsr.getNextSample();
        }

        this->sourceSamplePosition += this->pitchRatio;
        if (this->sourceSamplePosition >= srcLen) {
            
            // reached end
            this->stopNote(false);
            // LOG(Debug, "Reached end of sample for voice note " + juce::String(this->currentlyPlayingNote) + " sample=" + (s->sampleVariantSpec ? s->sampleVariantSpec->sampleFile.getFileName() : juce::String("unknown")) + " pos=" + juce::String(this->sourceSamplePosition) + " srcLen=" + juce::String(srcLen));
            break;
        }
    }
    if (!this->adsr.isActive()) {
        this->stopNote(false);
        // LOG(Debug, "ADSR release finished for voice note " + juce::String(this->currentlyPlayingNote) + " sample=" + (s->sampleVariantSpec ? s->sampleVariantSpec->sampleFile.getFileName() : juce::String("unknown")) + " pos=" + juce::String(this->sourceSamplePosition) + " srcLen=" + juce::String(srcLen));
    }
}
