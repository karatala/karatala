#include "InstrumentPlayer.h"
#include "../Config/PluginLogger.h"
#include "SampleLoader.h"
#include "DrumKitEngine.h"
#include "HiHatTransitionPolicy.h"
#include <limits>
#include <cmath>

void InstrumentPlayerVoice::clearCurrentNote() {
    this->currentlyPlayingNote = -1;
    this->currentlyPlayingSound = nullptr;
    this->currentBuffer.reset();
    this->currentPlayingMidiChannel = 0;
    this->artRuntimeIdx = -1;
    this->midiVelocity = 0;
    this->envelopeState = EnvelopeState::Idle;
    this->playbackDelaySamplesRemaining = 0;
}

void InstrumentPlayerVoice::setTailShorteningFactor(float factor, float minClamp) {
    factor = juce::jlimit(minClamp, 1.0f, factor);
    if (factor >= this->tailShortenFactor) return;
    this->tailShortenFactor = factor;

    juce::ADSR::Parameters parameters = this->adsr.getParameters();
    parameters.release = this->releaseTimeSec * this->tailShortenFactor;
    this->adsr.setParameters(parameters);
}

void InstrumentPlayerVoice::setSourceGainCorrectionDb(float correctionDb) {
    this->sourceGainCorrection.setTargetValue(juce::Decibels::decibelsToGain(correctionDb, -100.0f));
}

InstrumentPlayer::ArticulationRuntime::ArticulationRuntime(StringsDictSingleton::Id newNameId, StringsDictSingleton::Id newExprId) : nameId(newNameId), exprId(newExprId) {
    this->velocityToLevel.fill(-1);
    for (int velocity = 0; velocity <= MIDI_MAX_VALUE; ++velocity) this->velocityGain[static_cast<size_t>(velocity)] = static_cast<float>(velocity) / 127.0f;
    this->selectionTargetDb.fill(-100.0f);
    this->selectionOffsetDb.fill(0.0f);
    this->residualOutputDb.fill(0.0f);
}

juce::String InstrumentPlayer::ArticulationRuntime::getCodeRaw() const {
    juce::String result = StringsDictSingleton::nameOf(this->nameId);
    if (this->exprId != 0) result += "[" + StringsDictSingleton::nameOf(this->exprId) + "]";
    return result;
}

void InstrumentPlayer::RoutingContext::setMicRouteOverrides(const std::unordered_map<juce::String, juce::String>& overrides) {
    this->micRouteOverrides = overrides;
    this->rebuildMicRouteOverrideIds();
}

void InstrumentPlayer::RoutingContext::rebuildMicRouteOverrideIds() {
    this->micRouteOverrideIds.clear();
    this->micRouteOverrideIds.reserve(this->micRouteOverrides.size());
    for (const auto& [micCode, outputId] : this->micRouteOverrides) {
        const auto micCodeId = StringsDictSingleton::getId(micCode.trim().toLowerCase());
        const auto outputKey = StringsDictSingleton::getId(outputId.trim().toLowerCase());
        if (micCodeId != StringsDictSingleton::EMPTY && outputKey != StringsDictSingleton::EMPTY) this->micRouteOverrideIds[micCodeId] = outputKey;
    }
}

InstrumentPlayer::InstrumentPlayer(DrumKitEngine* parentEngine, KitLayout::InstrumentSlot layoutInstrument)
    : parent(parentEngine)
    , id(layoutInstrument.id)
    , type(layoutInstrument.type)
    , instrumentSlot(std::move(layoutInstrument)) {
    this->lastCcArticulation.fill(-1);
    this->pendingCcArticulation.fill(-1);
    this->hihatNotePedalOpenCloseValues.fill(-1);
    this->rebuildHiHatNoteChokeValues();
}

InstrumentPlayer::~InstrumentPlayer() { this->deactivateSampleLoadDemand(); }

void InstrumentPlayer::recordSampleLoadDemand(ArticulationRuntime& artRt, int velocityLevelIdx) noexcept {
    auto* demandState = artRt.loadDemandState.get();
    if (demandState != nullptr) demandState->request(velocityLevelIdx);
}

void InstrumentPlayer::deactivateSampleLoadDemand() noexcept {
    for (auto& articulation : this->runtimeArticulations) {
        if (articulation.loadDemandState != nullptr) articulation.loadDemandState->deactivate();
    }
}

void InstrumentPlayer::clearSounds() {
    const juce::ScopedLock scopedLock(this->lock);
    this->sampledSoundPtrs.clear();
}

void InstrumentPlayer::clearVoices() {
    const juce::ScopedLock scopedLock(this->lock);
    this->voices.clear();
}




void InstrumentPlayer::renderNextBlock(juce::AudioBuffer<float>& outputAudio, const juce::MidiBuffer& inputMidi, int startSample, int numSamples) {
    jassert(!juce::exactlyEqual(sampleRate, 0.0));
    if (juce::exactlyEqual(sampleRate, 0.0))
        return;

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

void InstrumentPlayer::renderVoices(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) {
    int activeCount = 0;
    for (auto* voice : this->voices) {
        voice->renderNextBlock(buffer, startSample, numSamples);
        if (voice->isVoiceActive()) ++activeCount;
    }
    this->activeVoiceCount = activeCount;
    this->applyAdaptiveTailShortening();
    this->renderedSampleCount += static_cast<uint64_t>(juce::jmax(0, numSamples));
    this->processPendingHiHatControllers();
}

void InstrumentPlayer::applyAdaptiveTailShortening() {
    const auto& settings = this->parent->getEngineSettings();
    const float loadThreshold = settings.tailShortenLoadThreshold.load();
    const float minimumFactor = settings.tailShortenMinFactor.load();
    const float amplitudeProtect = settings.tailShortenAmpProtect.load();
    const float ageMultiplier = settings.tailShortenAgeMultiplier.load();
    const float minimumClamp = settings.voiceTailShortenMinClamp.load();
    const int maxVoices = this->voices.size();
    if (static_cast<float>(this->activeVoiceCount) < static_cast<float>(maxVoices) * loadThreshold) return;
    const int activeVoices = this->activeVoiceCount;
    const float load = static_cast<float>(activeVoices) / static_cast<float>(maxVoices);
    const float minTailShortenFactor = juce::jmap(load, loadThreshold, 1.0f, 1.0f, minimumFactor);
    const float protectDb = juce::Decibels::gainToDecibels(amplitudeProtect, -100.0f);
    for (auto* voice : this->voices) {
        if (!voice->isVoiceActive()) continue;
        const auto state = voice->getEnvelopeState();
        if (state == InstrumentPlayerVoice::EnvelopeState::FadingOut
            || state == InstrumentPlayerVoice::EnvelopeState::Releasing
            || state == InstrumentPlayerVoice::EnvelopeState::Dying)
            continue;

        const float estimatedDb = voice->getEstimatedCurrentDb();
        if (estimatedDb > protectDb) continue;
        const float age = static_cast<float>(lastNoteOnCounter - voice->noteOnTime);
        const float ageFactor = juce::jlimit(0.0f, 1.0f, age * ageMultiplier);
        const float finalFactor = juce::jmap(ageFactor, 0.0f, 1.0f, 1.0f, minTailShortenFactor);
        voice->setTailShorteningFactor(finalFactor, minimumClamp);
    }
}

InstrumentPlayerVoice* InstrumentPlayer::findVoiceToSteal(int /*midiChannel*/, int /*midiNoteNumber*/, int groupId, InstrumentPlayerVoice* const* excludedVoices, size_t excludedVoiceCount) const {
    // Single-pass scan: find best candidate per state category without heap allocations.
    // Priority order: Dying (oldest) > FadingOut (quietest) > Releasing (quietest)
    //               > Playing (quietest non-protected) > oldest in group.
    InstrumentPlayerVoice* oldestDying = nullptr;
    InstrumentPlayerVoice* quietestFading = nullptr;
    float quietestFadingDb = std::numeric_limits<float>::infinity();
    InstrumentPlayerVoice* quietestReleasing = nullptr;
    float quietestReleasingDb = std::numeric_limits<float>::infinity();

    // Playing state: track both the loudest (protected) and quietest (steal target)
    InstrumentPlayerVoice* protectedPlaying = nullptr;
    float protectedPlayingDb = -std::numeric_limits<float>::infinity();
    uint32_t protectedPlayingTime = 0;
    InstrumentPlayerVoice* quietestPlaying = nullptr;
    float quietestPlayingDb = std::numeric_limits<float>::infinity();
    int playingCount = 0;

    InstrumentPlayerVoice* oldestInGroup = nullptr;

    for (auto* voice : this->voices) {
        bool excluded = false;
        for (size_t excludedIdx = 0; excludedIdx < excludedVoiceCount; ++excludedIdx) {
            if (excludedVoices[excludedIdx] == voice) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;
        if (!voice->isVoiceActive()) continue;
        if (voice->hitGroupId != groupId) continue;

        // Track oldest overall as final fallback
        if (oldestInGroup == nullptr || voice->wasStartedBefore(*oldestInGroup))
            oldestInGroup = voice;

        const auto state = voice->getEnvelopeState();
        switch (state) {
            case InstrumentPlayerVoice::EnvelopeState::Idle:
                break;
            case InstrumentPlayerVoice::EnvelopeState::Dying:
                if (oldestDying == nullptr || voice->wasStartedBefore(*oldestDying))
                    oldestDying = voice;
                break;
            case InstrumentPlayerVoice::EnvelopeState::FadingOut: {
                const float db = voice->getEstimatedCurrentDb();
                if (quietestFading == nullptr || db < quietestFadingDb) {
                    quietestFading = voice;
                    quietestFadingDb = db;
                }
                break;
            }
            case InstrumentPlayerVoice::EnvelopeState::Releasing: {
                const float db = voice->getEstimatedCurrentDb();
                if (quietestReleasing == nullptr || db < quietestReleasingDb) {
                    quietestReleasing = voice;
                    quietestReleasingDb = db;
                }
                break;
            }
            case InstrumentPlayerVoice::EnvelopeState::Playing: {
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

void InstrumentPlayer::prepareToPlay(double newSampleRate, int samplesPerBlock) {
    // micBuffer is no longer used for rendering - voices write directly to the global output
    // buffer using globally-indexed micBufferChannelStart values.  Free any previous allocation.
    micBuffer.setSize(0, 0);

    // Configure voices for this instrument player.
    this->clearVoices();

    // Dynamically determine required voices based on the maximum number of simultaneous
    // sample entries that may be started for a single note. This counts how many
    // samples belong to the same round-robin ID in one velocity level, and uses the
    // largest such group across all articulations and velocity levels.
    int maxSimultaneousSamples = 0;
    for (const auto& art : this->runtimeArticulations) {
        for (const auto& velocityLevel : art.velocityLevels) {
            for (const auto& logicalHit : velocityLevel.logicalHits)
                maxSimultaneousSamples = juce::jmax(maxSimultaneousSamples, static_cast<int>(logicalHit.sampleEntryIndexes.size()));
        }
    }

    // Minimum voices to keep behavior stable
    const int minVoices = 10;
    const int numVoices = std::max(minVoices, maxSimultaneousSamples * minVoices);
    for (int i = 0; i < numVoices; ++i)
        this->addVoice(new InstrumentPlayerVoice());
    LOG(Debug, "prepareToPlay - instrument: " + this->instrumentSlot.id + " sampleRate=" + juce::String(newSampleRate) + " samplesPerBlock=" + juce::String(samplesPerBlock) + " numVoices=" + juce::String(numVoices));

    // set playback sample rate for voices
    this->setCurrentPlaybackSampleRate(newSampleRate);

    for (auto* voice : this->voices) { //TODO: maybe refactor to separate method
        voice->chokeProcessor.engineSettings = &this->parent->getEngineSettings();
        voice->chokeProcessor.prepare(newSampleRate);
    }
}

void InstrumentPlayerVoice::startNote(int midiNoteNumber, float velocity, SampledSound* sound) {
    juce::ignoreUnused(midiNoteNumber);
    this->velocityGain = velocity;
    this->sourceSamplePosition = 0.0;
    this->playbackDelaySamplesRemaining = 0;
    this->envelopeState = EnvelopeState::Playing;
    this->currentBuffer = sound->buffer;
    this->currentCanonicalFixedGainDb = sound->canonicalFixedGainDb;
    this->currentDecayEnvelope = sound->decayEnvelope;
    this->lastSourceGainCorrection = sound->cachedSourceGainCorrection.load(std::memory_order_relaxed);
    this->sourceGainCorrection.reset(this->getSampleRate(), 0.012);
    this->sourceGainCorrection.setCurrentAndTargetValue(this->lastSourceGainCorrection);

    if (this->useOneShotDecayEnvelope) {
        // Decay override should shape one-shot loudness over time, even without note-off.
        const float decaySec = juce::jmax(0.001f, this->releaseTimeSec);
        this->adsr.setParameters({ this->attackTimeSec, decaySec, 0.0f, decaySec });
    } else {
        // Default behavior: natural sample tail, release only used on note-off/choke.
        this->adsr.setParameters({ this->attackTimeSec, 0.0f, 1.0f, this->releaseTimeSec });
    }

    this->adsr.setSampleRate(this->getSampleRate());
    this->adsr.noteOn();

    this->tailShortenFactor = 1.0f;
    this->dbUpdateCounter = 3; // force immediate dB update on first render

    this->chokeProcessor.noteStarted();
    // LOG(Debug, "InstrumentPlayerVoice: startNote velocity=" + juce::String(velocity) + " pitchRatio=" + juce::String(this->pitchRatio));

    if (sound->sampleRate > 0 && getSampleRate() > 0)
        this->pitchRatio = sound->sampleRate / getSampleRate();
    else
        this->pitchRatio = 1.0;

    this->pitchRatio *= this->pitchRatioMultiplier;

    if (sound->playbackOffsetMs > 0.0f && this->currentSampleRate > 0.0) {
        this->playbackDelaySamplesRemaining = juce::jmax(0, juce::roundToInt(static_cast<double>(sound->playbackOffsetMs) * this->currentSampleRate / 1000.0));
        this->cachedEstimatedDb = -100.0f;
    } else if (sound->playbackOffsetMs < 0.0f && sound->sampleRate > 0.0) {
        int sampleLength = sound->sampleLengthSamples;
        if (sampleLength <= 0 && this->currentBuffer != nullptr) sampleLength = this->currentBuffer->getNumSamples();
        const double advanceSamples = static_cast<double>(-sound->playbackOffsetMs) * sound->sampleRate / 1000.0;
        this->sourceSamplePosition = juce::jlimit(0.0, static_cast<double>(juce::jmax(0, sampleLength)), advanceSamples);
    }
}

void InstrumentPlayerVoice::requestFadeOut(float durationSec) {
    if (this->envelopeState == EnvelopeState::Idle)
        return;
    this->envelopeState = EnvelopeState::FadingOut;
    const float releaseSec = juce::jmax(0.001f, durationSec);
    juce::ADSR::Parameters p = this->adsr.getParameters();
    p.release = releaseSec;
    this->adsr.setParameters(p);
    this->adsr.noteOff();
}

void InstrumentPlayerVoice::requestRelease() {
    if (this->envelopeState == EnvelopeState::Idle)
        return;
    this->envelopeState = EnvelopeState::Releasing;
    juce::ADSR::Parameters p = this->adsr.getParameters();
    p.release = this->releaseTimeSec * this->tailShortenFactor;
    this->adsr.setParameters(p);
    this->adsr.noteOff();
}

void InstrumentPlayerVoice::killImmediately() {
    this->envelopeState = EnvelopeState::Dying;
    this->adsr.reset();
    this->clearCurrentNote();
}

float InstrumentPlayerVoice::getEstimatedCurrentDb() const {
    return this->cachedEstimatedDb;
}

void InstrumentPlayerVoice::updateEstimatedDb() {
    if (!this->isVoiceActive()) {
        this->cachedEstimatedDb = -100.0f;
        return;
    }
    if (this->playbackDelaySamplesRemaining > 0) {
        this->cachedEstimatedDb = -100.0f;
        return;
    }

    // Throttle: only recompute every 4th render call to reduce log10 overhead.
    // The cached value is still fresh enough for voice stealing and tail shortening.
    if (++this->dbUpdateCounter < 4)
        return;
    this->dbUpdateCounter = 0;

    auto* s = this->currentlyPlayingSound;
    if (s == nullptr || !s->decayEnvelope.isValid()) {
        const float gain = juce::jmax(0.0001f, this->getCurrentAmplitude());
        this->cachedEstimatedDb = juce::Decibels::gainToDecibels(gain, -100.0f);
        return;
    }

    const double srcRate = s->sampleRate > 0.0 ? s->sampleRate : 44100.0;
    const float elapsedMs = static_cast<float>((this->sourceSamplePosition / srcRate) * 1000.0);
    const float velDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, this->velocityGain), -100.0f);
    float layerGainDb = 0.0f;
    if (this->owner != nullptr) {
        const auto* layerParameters = this->owner->getLayerParameters(this->artRuntimeIdx);
        const float volumeDb = layerParameters != nullptr ? layerParameters->volumeDb : this->owner->getVolumeDb();
        const float layerGain = juce::Decibels::decibelsToGain(volumeDb, -100.0f);
        const float bleedGain = s->cachedMicBleedGain.load(std::memory_order_relaxed);
        layerGainDb = juce::Decibels::gainToDecibels(juce::jmax(0.0001f, layerGain * bleedGain), -100.0f);
    }
    const float sourceCorrectionDb = juce::Decibels::gainToDecibels(juce::jmax(0.0001f, this->lastSourceGainCorrection), -100.0f);
    this->cachedEstimatedDb = this->currentDecayEnvelope.getAbsoluteDbAtMs(elapsedMs) + velDb + layerGainDb + sourceCorrectionDb;
}

void InstrumentPlayerVoice::stopNote(bool allowTailOff) {
    if (allowTailOff) {
        this->requestRelease();
    } else {
        this->killImmediately();
    }
}

void InstrumentPlayerVoice::aftertouchChanged(int newValue) {
    // LOG(Debug, "aftertouchChanged " + juce::String(newValue));
    if (this->currentlyPlayingSound != nullptr && this->currentlyPlayingSound->isAftertouchChokeTarget) return;
    this->chokeProcessor.handlePolyAftertouch(newValue);
}

void InstrumentPlayerVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) {
    if (!this->isVoiceActive())
        return;

    SampledSound* s = this->getCurrentlyPlayingSound();
    if (s == nullptr || this->currentBuffer == nullptr)
        return;

    if (this->playbackDelaySamplesRemaining > 0) {
        const int delayedSamples = juce::jmin(numSamples, this->playbackDelaySamplesRemaining);
        this->playbackDelaySamplesRemaining -= delayedSamples;
        startSample += delayedSamples;
        numSamples -= delayedSamples;
        this->cachedEstimatedDb = -100.0f;
        if (numSamples <= 0) return;
    }

    auto& buf = *this->currentBuffer;
    const int srcChannels = buf.getNumChannels();
    const int srcLen = buf.getNumSamples();
    // Determine source/destination channel mapping based on SampleSpec metadata.
    int srcStart = 0;
    int srcCount = srcChannels;
    if (s->sampleSpec) {
        srcStart = s->sampleSpec->fileChannelStart;
        srcCount = s->sampleSpec->fileChannelCount > 0 ? s->sampleSpec->fileChannelCount : srcChannels;
    }

    const int destChCount = outputBuffer.getNumChannels();
    const auto* layerParameters = this->owner != nullptr ? this->owner->getLayerParameters(this->artRuntimeIdx) : nullptr;
    const bool flipLR = layerParameters != nullptr ? layerParameters->flipLR : this->owner != nullptr && this->owner->getFlipLR();
    const bool flipPhase = layerParameters != nullptr ? layerParameters->flipPhase : this->owner != nullptr && this->owner->getFlipPhase();
    const float volumeDb = layerParameters != nullptr ? layerParameters->volumeDb : this->owner != nullptr ? this->owner->getVolumeDb() : 0.0f;
    const float phaseMultiplier = flipPhase ? -1.0f : 1.0f;
    const float layerGain = juce::Decibels::decibelsToGain(volumeDb, -100.0f);
    const float bleedGain = s->cachedMicBleedGain.load(std::memory_order_relaxed);
    const float voiceGain = layerGain * bleedGain;

    if (this->chokeProcessor.isMuted()) {
        this->stopNote(false);
        return;
    }

    // P1-B: Pre-validate destination channels - skip voice if all out of range.
    const int destChBase = s->micBufferChannelStart;
    if (destChBase < 0 || destChBase >= destChCount) {
        // Advance ADSR to keep envelope state consistent
        for (int i = 0; i < numSamples; ++i)
            (void) this->adsr.getNextSample();
        this->sourceSamplePosition += this->pitchRatio * numSamples;
        if (!this->adsr.isActive() || this->sourceSamplePosition >= srcLen)
            this->stopNote(false);
        this->updateEstimatedDb();
        return;
    }
    // P1-C: Cache raw read/write pointers outside the sample loop to avoid
    // repeated getReadPointer/getSample function call overhead.
    static constexpr int MAX_RENDER_CHANNELS = 32;
    const int requestedDestCount = s->micBufferChannelCount > 0 ? s->micBufferChannelCount : srcCount;
    const int effectiveSrcCount = juce::jlimit(0, MAX_RENDER_CHANNELS, juce::jmin(srcCount, srcChannels - srcStart));
    const int effectiveDestCount = juce::jlimit(0, MAX_RENDER_CHANNELS, juce::jmin(requestedDestCount, destChCount - destChBase));
    if (effectiveSrcCount <= 0 || effectiveDestCount <= 0) {
        for (int i = 0; i < numSamples; ++i)
            (void) this->adsr.getNextSample();
        this->sourceSamplePosition += this->pitchRatio * numSamples;
        if (!this->adsr.isActive() || this->sourceSamplePosition >= srcLen)
            this->stopNote(false);
        this->updateEstimatedDb();
        return;
    }

    const float* srcPtrs[MAX_RENDER_CHANNELS];
    float* destPtrs[MAX_RENDER_CHANNELS];
    for (int sc = 0; sc < effectiveSrcCount; ++sc)
        srcPtrs[sc] = buf.getReadPointer(juce::jlimit(0, srcChannels - 1, srcStart + sc));
    for (int dc = 0; dc < effectiveDestCount; ++dc)
        destPtrs[dc] = outputBuffer.getWritePointer(destChBase + dc);

    const bool useMonoPlacement = (s->micPlacementMode == MicPlacementMode::MonoCenter || s->micPlacementMode == MicPlacementMode::MonoSourceAndLayerPan) && effectiveSrcCount == 1 && effectiveDestCount >= 2;
    const bool useStereoPlacement = s->micPlacementMode == MicPlacementMode::StereoLayerPan && effectiveSrcCount >= 2 && effectiveDestCount >= 2;
    const float placementLeftGain = s->cachedPlacementLeftGain.load(std::memory_order_relaxed);
    const float placementRightGain = s->cachedPlacementRightGain.load(std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i) {
        // Per-sample choke gain - MUST stay per-sample, not per-block.
        // getNextBlockGain() advances elapsed time by 1/sampleRate each call;
        // calling once per block freezes ducking/muting and breaks aftertouch choke.
        const float chokeGain = this->chokeProcessor.getNextBlockGain();

        const double pos = this->sourceSamplePosition;
        const int idx = static_cast<int>(pos);
        const double frac = pos - idx;

        // For each source channel in this sample, fetch the interpolated sample.
        // and write to corresponding destination channel (offset by srcStart).
        if (idx >= 0 && idx < srcLen) {
            const auto env = this->adsr.getNextSample();
            this->lastEnvelopeValue = env;
            this->lastSourceGainCorrection = this->sourceGainCorrection.getNextValue();
            const float gain = env * this->velocityGain * chokeGain * voiceGain * phaseMultiplier * this->lastSourceGainCorrection;

            if (useMonoPlacement) {
                float v0 = srcPtrs[0][idx];
                float v1 = (idx + 1 < srcLen) ? srcPtrs[0][idx + 1] : 0.0f;
                const float sample = static_cast<float>(v0 + (v1 - v0) * frac) * gain;
                destPtrs[0][startSample + i] += sample * placementLeftGain;
                destPtrs[1][startSample + i] += sample * placementRightGain;
            } else if (useStereoPlacement) {
                const int leftSrc = flipLR ? 1 : 0;
                const int rightSrc = flipLR ? 0 : 1;
                float left0 = srcPtrs[leftSrc][idx];
                float left1 = (idx + 1 < srcLen) ? srcPtrs[leftSrc][idx + 1] : 0.0f;
                float right0 = srcPtrs[rightSrc][idx];
                float right1 = (idx + 1 < srcLen) ? srcPtrs[rightSrc][idx + 1] : 0.0f;
                destPtrs[0][startSample + i] += static_cast<float>(left0 + (left1 - left0) * frac) * gain * placementLeftGain;
                destPtrs[1][startSample + i] += static_cast<float>(right0 + (right1 - right0) * frac) * gain * placementRightGain;
            } else {
                const int renderCount = juce::jmin(effectiveSrcCount, effectiveDestCount);
                for (int sc = 0; sc < renderCount; ++sc) {
                    const int destIndex = (flipLR && renderCount == 2) ? (renderCount - 1 - sc) : sc;
                    float v0 = srcPtrs[sc][idx];
                    float v1 = (idx + 1 < srcLen) ? srcPtrs[sc][idx + 1] : 0.0f;
                    float sample = static_cast<float>(v0 + (v1 - v0) * frac) * gain;
                    destPtrs[destIndex][startSample + i] += sample;
                }
            }
        } else {
            // still advance envelope even if out of source range
            (void) this->adsr.getNextSample();
            this->lastSourceGainCorrection = this->sourceGainCorrection.getNextValue();
        }

        this->sourceSamplePosition += this->pitchRatio;
        if (this->sourceSamplePosition >= srcLen) {
            
            // reached end
            this->stopNote(false);
            // LOG(Debug, "Reached end of sample for voice note " + juce::String(this->currentlyPlayingNote) + " sample=" + (s->sampleSpec ? s->sampleSpec->sampleFile.getFileName() : juce::String("unknown")) + " pos=" + juce::String(this->sourceSamplePosition) + " srcLen=" + juce::String(srcLen));
            break;
        }
    }
    if (!this->adsr.isActive()) {
        this->stopNote(false);
        // LOG(Debug, "ADSR release finished for voice note " + juce::String(this->currentlyPlayingNote) + " sample=" + (s->sampleSpec ? s->sampleSpec->sampleFile.getFileName() : juce::String("unknown")) + " pos=" + juce::String(this->sourceSamplePosition) + " srcLen=" + juce::String(srcLen));
    }
    this->updateEstimatedDb();
}
