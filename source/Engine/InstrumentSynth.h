#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <algorithm>
#include "AftertouchChokeProcessor.h"
#include "KitLibModel.h"
#include "LayoutManager.h"
#include "DrumKitEngine.h"

class SampledSound {
public:
    KitLib::InstrumentSpec* instrumentSpec = nullptr;
    KitLib::SampleVariantSpec* sampleVariantSpec = nullptr;
    juce::File sampleFile;
    std::shared_ptr<juce::AudioBuffer<float>> buffer;
    std::vector<uint8_t> notes; 

    double sampleRate = 0.0;
    int micBufferChannelStart = 0;
    int micBufferChannelCount = 0;

    SampledSound(KitLib::InstrumentSpec* instrumentSpec = nullptr, KitLib::SampleVariantSpec* sampleVariantSpec = nullptr, const juce::File& sampleFile = {}, std::vector<uint8_t> notes = {})
        : instrumentSpec(instrumentSpec)
        , sampleVariantSpec(sampleVariantSpec)
        , sampleFile(sampleFile)
        , notes(notes)
    {}
};


class InstrumentSynthVoice {
public:
    InstrumentSynthVoice() {}
    ~InstrumentSynthVoice() {};

    bool isVoiceActive() const { return this->getCurrentlyPlayingNote() >= 0; }
    int getCurrentlyPlayingNote() const noexcept { return this->currentlyPlayingNote; }
    double getSampleRate() const noexcept { return this->currentSampleRate; }
    void setCurrentPlaybackSampleRate (const double newRate) { this->currentSampleRate = newRate; }
    void stopNote(bool allowTailOff);

    bool canPlaySound(SampledSound* sound) { return sound != nullptr; }
    void startNote(int midiNoteNumber, float velocity, SampledSound* sound);
    bool isPlayingChannel (const int midiChannel) const { return this->currentPlayingMidiChannel == midiChannel; }

    void aftertouchChanged (int newValue);
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);
    AftertouchChokeProcessor chokeProcessor;
    void setSourceSamplePosition(double pos) { this->sourceSamplePosition = pos; }
    void setAdsrParameters(float attack, float decay, float sustain, float release) { this->adsr.setParameters({attack, decay, sustain, release}); }
    // Helpers for crossfade/voice management
    float getVelocityGain() const { return this->velocityGain; }
    void setVelocityGain(float value) { this->velocityGain = value; }
    void crossfadeOutSec(float ms);

    SampledSound* getCurrentlyPlayingSound() const noexcept { return this->currentlyPlayingSound; }

    bool wasStartedBefore(const InstrumentSynthVoice& other) const noexcept { return this->noteOnTime < other.noteOnTime; }
    
    // True while the voice is in the initial attack portion after a note-on
    bool isAttackHold() const noexcept {
        if (!this->isVoiceActive()) return false;
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        return (nowMs - this->noteOnTimeMs) < this->attackTimeSec;
    }


    bool isPlayingButReleased() const noexcept {
        if (!this->isVoiceActive()) return false;
        return this->adsr.isActive();
    }

    void clearCurrentNote() {
        this->currentlyPlayingNote = -1;
        this->currentlyPlayingSound = nullptr;
        this->currentPlayingMidiChannel = 0;
    }

    void applyTailShortening(float factor) {
        // clamp 0.1 .. 1.0
        factor = juce::jlimit(0.1f, 1.0f, factor);
        if (factor >= this->tailShortenFactor)
            return; // never lengthen here, only shorten
        this->tailShortenFactor = factor;

        juce::ADSR::Parameters p = this->adsr.getParameters();
        p.release = this->releaseTimeSec * this->tailShortenFactor;
        this->adsr.setParameters(p);
    }

private:
    friend class InstrumentSynth;

    uint32_t artNameId;
    uint32_t artExprId;

    int currentlyPlayingNote = -1;
    int currentPlayingMidiChannel = 0;

    double currentSampleRate = 44100.0;
    juce::uint32 noteOnTime = 0;


    double noteOnTimeMs = 0.0;//TODO: RM

    double releaseStartTimeMs = 0.0;
    SampledSound* currentlyPlayingSound = nullptr;

    float attackTimeSec = 0.005f;
    float releaseTimeSec = 0.1f;

public:
    void setAttackTimeSec(float sec) { this->attackTimeSec = sec; }
    float getAttackTimeSec() const noexcept { return this->attackTimeSec; }
    void setReleaseTimeSec(float sec) { this->releaseTimeSec = sec; }
    float getReleaseTimeSec() const noexcept { return this->releaseTimeSec; }
    
    float getCurrentAmplitude() const { return this->lastEnvelopeValue * this->velocityGain; }
    
    // juce::RangedAudioParameter* gainParam = nullptr; //TODO?
    // juce::RangedAudioParameter* panParam = nullptr; //TODO?
    // juce::AudioParameterBool* invertPhaseParam = nullptr; //TODO?

    juce::ADSR adsr;

    double sourceSamplePosition = 0.0;
    double pitchRatio = 1.0;
    
    float velocityGain = 1.0f;
    float tailShortenFactor = 1.0f;   // 1.0 = normal, <1 = shortened
    float lastEnvelopeValue = 0.0f;
    int hitGroupId = -1;

    JUCE_LEAK_DETECTOR(InstrumentSynthVoice)
};

class MicRoutingBuilder;

class InstrumentSynth {
public:

    // Runtime data structures built at loadInstrument() (non-realtime)
    struct SampleEntry {
        SampledSound* sound = nullptr; // non-owning pointer into Synth::sounds
        uint8_t variantIdx = 0; // variant index (optional)
    };

    struct VelocityLayerRuntime {
        uint8_t velocity = 0; // base velocity for layer
        std::vector<SampleEntry> samples; // contiguous sample entries
        // Use shared_ptr to an atomic so the runtime struct remains copyable/movable
        std::shared_ptr<std::atomic<uint32_t>> roundRobinCounter = std::make_shared<std::atomic<uint32_t>>(0);
    };

    struct ArticulationRuntime {
        StringsDictSingleton::Id nameId = 0;
        StringsDictSingleton::Id exprId = 0;
        // For each incoming MIDI velocity (0..127) map to a preferred velocity layer index
        std::array<int8_t, 128> velocityToLayer; // -1 means no layer
        std::vector<VelocityLayerRuntime> layers;

        explicit ArticulationRuntime(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId) : nameId(nameId), exprId(exprId) {
            velocityToLayer.fill(-1);
        }
    };

    InstrumentSynth(DrumKitEngine* parent, KitLayout::InstrumentSlot instrumentSlot
        //DUNNO: add params to instrument or do it somewhere else?
        // ,juce::RangedAudioParameter& gainParameter
        // ,juce::RangedAudioParameter& panParameter
        // ,juce::AudioParameterBool& phaseParameter
    )
        : parent(parent)
        , instrumentSlot(instrumentSlot)
        , id(instrumentSlot.id)
        , type(instrumentSlot.type)
    {

    }
    ~InstrumentSynth() {}

    void unloadInstrument();
    void loadInstrument(KitLib::InstrumentSpec* kitInstrument);
    bool isLoaded() { return instrumentSpecstPtr != nullptr; }
    
    void clearSounds() {
        const juce::ScopedLock sl(this->lock);
        this->sampledSoundPtrs.clear();
    }
    void clearVoices() {
        const juce::ScopedLock sl(this->lock);
        this->voices.clear();
    }
    
    void handleMidiEvent(const juce::MidiMessage& midiMessage);
    bool playNote(const int midiChannel, const int midiNoteNumber, const juce::uint8 velocity);
    virtual void noteOn(int midiChannel, int midiNoteNumber, float velocity);
    void handleController(int midiChannel, int controllerNumber, int controllerValue);
    void handleAftertouch(int midiChannel, int midiNoteNumber, int aftertouchValue) {
        const juce::ScopedLock sl(this->lock);
        for (auto* voice : voices)
            if (voice->getCurrentlyPlayingNote() == midiNoteNumber && (midiChannel <= 0 || voice->isPlayingChannel(midiChannel)))
                voice->aftertouchChanged(aftertouchValue);
    }
    SampledSound* addSound(SampledSound* newSound) {
        const juce::ScopedLock sl(this->lock);
        if (newSound == nullptr) return nullptr;
        this->sampledSoundPtrs.emplace_back(std::unique_ptr<SampledSound>(newSound));
        return newSound;
    }
    InstrumentSynthVoice* addVoice(InstrumentSynthVoice* const newVoice) {
        InstrumentSynthVoice* voice;
        {
            const juce::ScopedLock sl(this->lock);
            newVoice->setCurrentPlaybackSampleRate(this->sampleRate);
            voice = this->voices.add(newVoice);
        }
        {
            const juce::ScopedLock sl(this->stealLock);
            this->usableVoicesToStealArray.ensureStorageAllocated(voices.size() + 1);
        }
        return voice;
    }
    InstrumentSynthVoice* findFreeVoice(SampledSound* soundToPlay, int midiChannel, int midiNoteNumber, const int groupId) const {
        const juce::ScopedLock sl(this->lock);
        for (auto* voice : this->voices)
            if ((!voice->isVoiceActive()) && voice->canPlaySound(soundToPlay))
                return voice;
        // LOG(Debug, "No free voice -> attempting steal for note " + juce::String(midiNoteNumber));
        return this->findVoiceToSteal(soundToPlay, midiChannel, midiNoteNumber, groupId);
    }

    void startVoice(InstrumentSynthVoice* const voice, SampledSound* const sound, const int midiChannel, const int midiNoteNumber, const float velocity) {
        if (voice == nullptr || sound == nullptr) return;
        // LOG(Debug, "Start voice note=" + juce::String(midiNoteNumber) + " group=" + juce::String(voice->hitGroupId) + " active voices=" + juce::String(voices.size()));
        if (voice->currentlyPlayingSound != nullptr)
            voice->stopNote(false);

        voice->currentlyPlayingNote = midiNoteNumber;
        voice->currentPlayingMidiChannel = midiChannel;
        voice->noteOnTime = ++lastNoteOnCounter;
        voice->currentlyPlayingSound = sound;
        voice->startNote(midiNoteNumber, velocity, sound);
    }
    
    void stopVoice(InstrumentSynthVoice* voice, const bool allowTailOff) {
        jassert(voice != nullptr);
        voice->stopNote(allowTailOff);
        // the subclass MUST call clearCurrentNote() if it's not tailing off! RTFM for stopNote()!
        jassert(allowTailOff || (voice->getCurrentlyPlayingNote() < 0 && voice->getCurrentlyPlayingSound() == nullptr));
    }
    void setCurrentPlaybackSampleRate(const double newRate) {
        if (!juce::approximatelyEqual(sampleRate, newRate)) {
            const juce::ScopedLock sl(this->lock);
            this->allNotesOff(0, false);
            this->sampleRate = newRate;

            for (auto* voice : this->voices)
                voice->setCurrentPlaybackSampleRate(newRate);
        }
    }
    void allNotesOff(const int midiChannel, const bool allowTailOff) {
        const juce::ScopedLock sl (this->lock);
        for (auto* voice : this->voices)
            if (midiChannel <= 0 || voice->isPlayingChannel(midiChannel))
                voice->stopNote(allowTailOff);
    }

    const DrumKitEngine* getEngine() const { return this->parent; }
    const MicLayout* getMicLayout() const { return this->parent->getMicLayout(); }

    void prepareToPlay(double sampleRate, int samplesPerBlock);

    void renderNextBlock(juce::AudioBuffer<float>& outputAudio, const juce::MidiBuffer& inputMidi, int startSample, int numSamples);
    void renderVoices(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    // Custom voice stealing logic: we use lastRequestedVelocity to prefer stealing
    // quiet/released voices over loud ones when deciding which voice to steal.
    InstrumentSynthVoice* findVoiceToSteal(SampledSound* soundToPlay, int midiNoteNumber) const;
    InstrumentSynthVoice* findVoiceToSteal(SampledSound* soundToPlay, int midiChannel, int midiNoteNumber, int groupId) const;

    int getMicChannelCount() const { return mMicChannelCount; }

    void setMicChannelCount(int channels) { mMicChannelCount = channels; }
    juce::AudioBuffer<float>& getMicBuffer() { return micBuffer; }

    // Per-instrument mapping: mic buffer channel index -> host output channel index
    void setMicOutputMapping(const std::vector<int>& mapping) { this->micOutputMapping = mapping; }
    const std::vector<int>& getMicOutputMapping() const { return this->micOutputMapping; }

    KitLib::InstrumentSpec::Type getType() const { return this->type; }

    const juce::String& getId() const { return id; }

    int8_t findArticulationIndex(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId) const;

    void applyAdaptiveTailCompression() {
        const int activeVoices = [&]() {
            int c = 0;
            for (auto* v : this->voices)
                if (v->isVoiceActive())
                    ++c;
            return c;
        }();

        const int maxVoices = this->voices.size();

        if (activeVoices < maxVoices * 0.7f)
            return; // no need

        const float load = static_cast<float>(activeVoices) / static_cast<float>(maxVoices);

        // load 0.7 → 1.0 maps to compression 1.0 → 0.3
        const float compression = juce::jmap(load, 0.7f, 1.0f, 1.0f, 0.3f);

        for (auto* voice : this->voices) {
            if (!voice->isVoiceActive())
                continue;

            const float amp = voice->getCurrentAmplitude();
            // Do NOT shorten loud fresh hits
            if (amp > 0.6f) continue;

            // Older voices get more compression
            const float age = static_cast<float>(lastNoteOnCounter - voice->noteOnTime);
            const float ageFactor = juce::jlimit(0.0f, 1.0f, age * 0.02f);
            const float finalFactor = juce::jmap(ageFactor, 0.0f, 1.0f, 1.0f, compression);
            // LOG(Debug, "Applying tail shortening to voice " + juce::String(voice->currentlyPlayingNote) + " age=" + juce::String(age) + " ageFactor=" + juce::String(ageFactor) + " load=" + juce::String(load) + " compression=" + juce::String(compression) + " finalFactor=" + juce::String(finalFactor));
            voice->applyTailShortening(finalFactor);
        }
    }
private:
    DrumKitEngine* parent = nullptr;
    KitLib::InstrumentSpec* instrumentSpecstPtr = nullptr;
    juce::String id;
    KitLib::InstrumentSpec::Type type;
    juce::AudioBuffer<float> micBuffer;
    int mMicChannelCount = 2;
    // Keep a copy of the kit layout instrument definition for note->articulation lookup
    KitLayout::InstrumentSlot instrumentSlot;

    juce::uint32 lastNoteOnCounter = 0;
    juce::CriticalSection lock;
    mutable juce::CriticalSection stealLock;
    double sampleRate = 0;
    juce::OwnedArray<InstrumentSynthVoice> voices;
    mutable juce::Array<InstrumentSynthVoice*> usableVoicesToStealArray;
    std::vector<std::unique_ptr<SampledSound>> sampledSoundPtrs;
    int minimumSubBlockSize = 32;
    bool subBlockSubdivisionIsStrict = false;

    juce::AudioBuffer<float> tempBuffer;

    // Runtime optimized tables for fast selection (populated by loadInstrument)
    // Map midi note [0..127] -> articulation index in runtimeArticulations (-1 = none)
    std::array<int8_t, 128> noteToArticulation;
    // Runtime articulations vector, index-stable after load
    std::vector<ArticulationRuntime> runtimeArticulations;
    
    std::unordered_map<int8_t, std::vector<uint8_t>> noteToControllers;
    // If this articulation subscribes to controllers -> for each CC id store 128-entry mapping to expression index
    // Stored as map CC -> array[valueIndex for 0..127]
    std::unordered_map<int, std::array<uint8_t, 128>> ccToArticulation;
    
    // (owned) storage for all SampledSound instances created for this synth
    // (use `sampledSoundPtrs` above which holds unique_ptrs)
    std::vector<int> micOutputMapping;

    // Velocity of the note currently being requested (0..1). Used by custom stealing logic.
    mutable std::atomic<float> lastRequestedVelocity{0.0f};

    using LayoutArticulation = KitLayout::InstrumentSlot::ArticulationTrigger;
    using CcDimensionsMap = std::unordered_map<int8_t, std::vector<std::pair<int8_t, int8_t>>>;

    void processLayoutArticulations(KitLib::InstrumentSpec* instSpec);
    void processArticulationSpec(KitLib::InstrumentSpec* instSpec
        , const LayoutArticulation& layoutArt
        , KitLib::ArticulationSpec* artSpec
        , ArticulationRuntime& artRt
        , CcDimensionsMap& foundCcDimensions
    );
    void createVelocityLayers(KitLib::InstrumentSpec* instSpec
        , const LayoutArticulation& layoutArt
        , KitLib::ArticulationSpec* artSpec
        , ArticulationRuntime& artRt
        , const std::vector<int>& micStarts
        , const std::vector<int>& micChannels
        , int& unmatchedStart
        , MicRoutingBuilder& micRouting
    );
    void buildVelocityMapping(ArticulationRuntime& artRt);
    void mapNotesToArticulation(const LayoutArticulation& layoutArt);
    void buildCcMappings(const LayoutArticulation& layoutArt, KitLib::ArticulationSpec* artSpec, CcDimensionsMap& foundCcDimensions);
    void processCcDimensions(CcDimensionsMap& foundCcDimensions);

    KitLib::ArticulationSpec* resolveArticulationSpec(KitLib::InstrumentSpec* instSpec, const LayoutArticulation& layoutArt);
};


