#pragma once

#include "InstrumentSynth.h"
#include <limits>




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
    int activeCount = 0;
    for (auto* voice : this->voices) {
        voice->renderNextBlock(buffer, startSample, numSamples);
        if (voice->isVoiceActive()) ++activeCount;
    }
    this->activeVoiceCount = activeCount;
    this->applyAdaptiveTailShortening();
}

InstrumentSynthVoice* InstrumentSynth::findVoiceToSteal(int /*midiChannel*/, int /*midiNoteNumber*/, int groupId) const {
    // Single-pass scan: find best candidate per state category without heap allocations.
    // Priority order: Dying (oldest) > FadingOut (quietest) > Releasing (quietest)
    //               > Playing (quietest non-protected) > oldest in group.
    InstrumentSynthVoice* oldestDying = nullptr;
    InstrumentSynthVoice* quietestFading = nullptr;
    float quietestFadingDb = std::numeric_limits<float>::infinity();
    InstrumentSynthVoice* quietestReleasing = nullptr;
    float quietestReleasingDb = std::numeric_limits<float>::infinity();

    // Playing state: track both the loudest (protected) and quietest (steal target)
    InstrumentSynthVoice* protectedPlaying = nullptr;
    float protectedPlayingDb = -std::numeric_limits<float>::infinity();
    uint32_t protectedPlayingTime = 0;
    InstrumentSynthVoice* quietestPlaying = nullptr;
    float quietestPlayingDb = std::numeric_limits<float>::infinity();
    int playingCount = 0;

    InstrumentSynthVoice* oldestInGroup = nullptr;

    for (auto* voice : this->voices) {
        if (!voice->isVoiceActive()) continue;
        if (voice->hitGroupId != groupId) continue;

        // Track oldest overall as final fallback
        if (oldestInGroup == nullptr || voice->wasStartedBefore(*oldestInGroup))
            oldestInGroup = voice;

        const auto state = voice->getEnvelopeState();
        switch (state) {
            case InstrumentSynthVoice::EnvelopeState::Dying:
                if (oldestDying == nullptr || voice->wasStartedBefore(*oldestDying))
                    oldestDying = voice;
                break;
            case InstrumentSynthVoice::EnvelopeState::FadingOut: {
                const float db = voice->getEstimatedCurrentDb();
                if (quietestFading == nullptr || db < quietestFadingDb) {
                    quietestFading = voice;
                    quietestFadingDb = db;
                }
                break;
            }
            case InstrumentSynthVoice::EnvelopeState::Releasing: {
                const float db = voice->getEstimatedCurrentDb();
                if (quietestReleasing == nullptr || db < quietestReleasingDb) {
                    quietestReleasing = voice;
                    quietestReleasingDb = db;
                }
                break;
            }
            case InstrumentSynthVoice::EnvelopeState::Playing: {
                ++playingCount;
                const float db = voice->getEstimatedCurrentDb();
                // Track loudest/newest for protection
                if (protectedPlaying == nullptr || db > protectedPlayingDb
                    || (juce::approximatelyEqual(db, protectedPlayingDb) && voice->noteOnTime > protectedPlayingTime)) {
                    protectedPlaying = voice;
                    protectedPlayingDb = db;
                    protectedPlayingTime = voice->noteOnTime;
                }
                // Track quietest for stealing
                if (quietestPlaying == nullptr || db < quietestPlayingDb) {
                    quietestPlaying = voice;
                    quietestPlayingDb = db;
                }
                break;
            }
            default:
                break;
        }
    }

    if (oldestDying != nullptr) return oldestDying;
    if (quietestFading != nullptr) return quietestFading;
    if (quietestReleasing != nullptr) return quietestReleasing;

    // For Playing voices: steal quietest, but protect loudest if multiple exist
    if (playingCount > 0) {
        if (playingCount > 1 && quietestPlaying != protectedPlaying && quietestPlaying != nullptr)
            return quietestPlaying;
        if (protectedPlaying != nullptr)
            return protectedPlaying;
    }

    return oldestInGroup;
}

void InstrumentSynth::prepareToPlay(double newSampleRate, int samplesPerBlock) {
    // micBuffer is no longer used for rendering — voices write directly to the global output
    // buffer using globally-indexed micBufferChannelStart values.  Free any previous allocation.
    micBuffer.setSize(0, 0);

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

    this->pitchRatio *= this->pitchRatioMultiplier;
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
    return this->cachedEstimatedDb;
}

void InstrumentSynthVoice::updateEstimatedDb() {
    if (!this->isVoiceActive()) {
        this->cachedEstimatedDb = -100.0f;
        return;
    }

    auto* s = this->currentlyPlayingSound;
    if (s == nullptr || !s->decayEnvelope.isValid()) {
        const float gain = juce::jmax(0.0001f, this->getCurrentAmplitude());
        this->cachedEstimatedDb = juce::Decibels::gainToDecibels(gain, -100.0f);
        return;
    }

    const double srcRate = s->sampleRate > 0.0 ? s->sampleRate : 44100.0;
    const float elapsedMs = static_cast<float>((this->sourceSamplePosition / srcRate) * 1000.0);
    const float velDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, this->velocityGain), -100.0f);
    this->cachedEstimatedDb = s->decayEnvelope.getAbsoluteDbAtMs(elapsedMs) + velDb;
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
    this->updateEstimatedDb();
}
