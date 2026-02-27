#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <algorithm>
#include "AftertouchChokeProcessor.h"
#include "KitLibModel.h"
#include "LayoutManager.h"


class DrumKitEngine;

class SampledSound {
public:
    KitLib::InstrumentSpec* instrumentSpec = nullptr;
    KitLib::SampleVariantSpec* sampleVariantSpec = nullptr;
    juce::File sampleFile;
    std::shared_ptr<juce::AudioBuffer<float>> buffer;
    std::vector<uint8_t> notes; 

    double sampleRate = 0.0;
    // start channel index inside the owning InstrumentSynth's micBuffer
    int micBufferChannelStart = 0;
    // number of mic channels this sound occupies in the mic buffer
    int micBufferChannelCount = 0;

    SampledSound(KitLib::InstrumentSpec* instrumentSpec = nullptr, KitLib::SampleVariantSpec* sampleVariantSpec = nullptr, const juce::File& sampleFile = {}, std::vector<uint8_t> notes = {})
        : instrumentSpec(instrumentSpec)
        , sampleVariantSpec(sampleVariantSpec)
        , sampleFile(sampleFile)
        , notes(notes)
    {}

    bool appliesToNote(int midiNote) { return std::find(notes.begin(), notes.end(), static_cast<uint8_t>(midiNote)) != notes.end(); }
    bool appliesToChannel(int /*midiChannel*/) { return true; }
};


class InstrumentSynthVoice {
public:
    struct VoiceArtIdentity {
        uint32_t nameId;
        uint32_t exprId;
    };
    VoiceArtIdentity artIdentity;

    InstrumentSynthVoice();
    ~InstrumentSynthVoice() {};

    bool isVoiceActive() const { return this->getCurrentlyPlayingNote() >= 0; }
    int getCurrentlyPlayingNote() const noexcept { return this->currentlyPlayingNote; }
    double getSampleRate() const noexcept { return this->currentSampleRate; }
    void setCurrentPlaybackSampleRate (const double newRate) { this->currentSampleRate = newRate; }
    // void stopNote(float velocity, bool allowTailOff) override;
    void stopNote(float velocity, bool allowTailOff);

    bool canPlaySound(SampledSound* sound) { return sound != nullptr; }
    void startNote(int midiNoteNumber, float velocity, SampledSound* sound);
    bool isPlayingChannel (const int midiChannel) const { return this->currentPlayingMidiChannel == midiChannel; }

    void aftertouchChanged (int newValue);
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);
    void renderNextBlock(juce::AudioBuffer<double>& outputBuffer, int startSample, int numSamples);
    AftertouchChokeProcessor chokeProcessor;
    void setSourceSamplePosition(double pos) { this->sourceSamplePosition = pos; }
    void setAdsrParameters(float attack, float decay, float sustain, float release) { this->adsr.setParameters({attack, decay, sustain, release}); }
    // Helpers for crossfade/voice management
    float getVelocityGain() const { return this->velocityGain; }
    void setVelocityGain(float value) { this->velocityGain = value; }
    void crossfadeOutMs(float ms);

    SampledSound* getCurrentlyPlayingSound() const noexcept { return this->currentlyPlayingSound; }

    bool wasStartedBefore(const InstrumentSynthVoice& other) const noexcept { return this->noteOnTime < other.noteOnTime; }
    // True while the voice is in the initial attack portion after a note-on
    bool isAttackHold() const noexcept {
        if (!isVoiceActive()) return false;
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        return (nowMs - this->noteOnTimeMs) < this->attackTimeSec;
    }

    // True when the key has been released but the envelope is still active
    bool isPlayingButReleased() const noexcept {
        if (!isVoiceActive()) return false;
        return this->adsr.isActive();
    }

    void clearCurrentNote() {
        this->currentlyPlayingNote = -1;
        this->currentlyPlayingSound = nullptr;
        this->currentPlayingMidiChannel = 0;
    }
private:
    friend class InstrumentSynth;

    double currentSampleRate = 44100.0;
    int currentlyPlayingNote = -1, currentPlayingMidiChannel = 0;
    juce::uint32 noteOnTime = 0;
    // Monotonic timestamps (ms) recorded on note on / note off for attack/release detection
    double noteOnTimeMs = 0.0;
    double releaseStartTimeMs = 0.0;
    SampledSound* currentlyPlayingSound = nullptr;

    float attackTimeSec = 0.005f;
    float releaseTimeSec = 2.0f;

public:
    void setAttackTimeSec(float sec) { attackTimeSec = sec; }
    float getAttackTimeSec() const noexcept { return attackTimeSec; }
    void setReleaseTimeSec(float sec) { releaseTimeSec = sec; }
    float getReleaseTimeSec() const noexcept { return releaseTimeSec; }


    juce::RangedAudioParameter* gainParam = nullptr;
    juce::RangedAudioParameter* panParam = nullptr;
    juce::AudioParameterBool* invertPhaseParam = nullptr;
    
    double pitchRatio = 0;
    float velocityGain = 0;
    double sourceSamplePosition = 0;
    juce::ADSR adsr;

    JUCE_LEAK_DETECTOR(InstrumentSynthVoice)
};

class MicRoutingBuilder;

class InstrumentSynth {
public:
    struct Articulation {}; //TODO

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
    // Select best SampledSound for given articulation index (index into runtimeArticulations) and velocity
    SampledSound* selectSampleFor(int articulationIdx, int midiVel, int micIdx = -1);
    
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
    InstrumentSynthVoice* findFreeVoice(SampledSound* soundToPlay, int midiChannel, int midiNoteNumber, const bool stealIfNoneAvailable) const {
        const juce::ScopedLock sl(this->lock);
        for (auto* voice : this->voices)
            if ((!voice->isVoiceActive()) && voice->canPlaySound(soundToPlay))
                return voice;

        if (stealIfNoneAvailable)
            return this->findVoiceToSteal(soundToPlay, midiChannel, midiNoteNumber);

        return nullptr;
    }

    void startVoice(InstrumentSynthVoice* const voice, SampledSound* const sound, const int midiChannel, const int midiNoteNumber, const float velocity) {
        if (voice != nullptr && sound != nullptr) {
            if (voice->currentlyPlayingSound != nullptr)
                voice->stopNote(0.0f, false);

            voice->currentlyPlayingNote = midiNoteNumber;
            voice->currentPlayingMidiChannel = midiChannel;
            voice->noteOnTime = ++lastNoteOnCounter;
            voice->currentlyPlayingSound = sound;
            voice->startNote(midiNoteNumber, velocity, sound);
        }
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
                voice->stopNote (1.0f, allowTailOff);
    }

    DrumKitEngine* getEngine() { return this->parent; }

    void prepareToPlay(double sampleRate, int samplesPerBlock);

    void processNextBlock(juce::AudioBuffer<float>&, const juce::MidiBuffer&, int startSample, int numSamples);
    void processNextBlock(juce::AudioBuffer<double>&, const juce::MidiBuffer&, int startSample, int numSamples);
    void renderNextBlock(juce::AudioBuffer<float>& outputAudio, const juce::MidiBuffer& inputMidi, int startSample, int numSamples);
    void renderNextBlock(juce::AudioBuffer<double>& outputAudio, const juce::MidiBuffer& inputMidi, int startSample, int numSamples);
    void renderVoices(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void renderVoices(juce::AudioBuffer<double>& buffer, int startSample, int numSamples);

    // Custom voice stealing logic: we use lastRequestedVelocity to prefer stealing
    // quiet/released voices over loud ones when deciding which voice to steal.
    InstrumentSynthVoice* findVoiceToSteal(SampledSound* soundToPlay, int midiChannel, int midiNoteNumber) const;

    int getMicChannelCount() const { return mMicChannelCount; }

    void setMicChannelCount(int channels) { mMicChannelCount = channels; }
    juce::AudioBuffer<float>& getMicBuffer() { return micBuffer; }

    // Per-instrument mapping: mic buffer channel index -> host output channel index
    void setMicOutputMapping(const std::vector<int>& mapping) { this->micOutputMapping = mapping; }
    const std::vector<int>& getMicOutputMapping() const { return this->micOutputMapping; }

    KitLib::InstrumentSpec::Type getType() const { return this->type; }

    const juce::String& getId() const { return id; }

    int8_t findArticulationIndex(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId) const;

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


class MicRoutingBuilder {
public:
    MicRoutingBuilder(InstrumentSynth* synth) : synth(synth) {}
    void build(KitLib::ArticulationSpec* artSpec, std::vector<int>& micStarts, std::vector<int>& micChannels, int& unmatchedStart);
    int getVariant(size_t& index) {
        if (index >= this->allVariants.size()) return -1;
        return this->allVariants[index].second; 
    }
private:
    void collectMicEntries();
    void assignVariants(KitLib::ArticulationSpec* artSpec);
    void computeMicGroups(std::vector<int>& micStarts, std::vector<int>& micChannels, int& unmatchedStart);

    InstrumentSynth* synth = nullptr;
    std::vector<const MicLayout::Mic*> micEntriesForInst;
    std::vector<std::pair<const KitLib::SampleVariantSpec*, int>> allVariants; // keep order for assignment
    std::unordered_map<size_t, int> variantsPerMic; // index in micEntriesForInst -> count
    int unmatchedVariantCount = 0;
};