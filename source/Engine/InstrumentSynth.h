#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <algorithm>
#include "AftertouchChokeProcessor.h"
#include "KitLibModel.h"
#include "LayoutManager.h"

// Tail-shortening / adaptive release control tuning constants
static constexpr float INSTRUMENT_TAIL_SHORTEN_LOAD_THRESHOLD = 0.5f; // start applying when load >= 70%
static constexpr float INSTRUMENT_TAIL_SHORTEN_AGE_MULTIPLIER = 0.02f; // age multiplier (note-on count -> 0..1)
static constexpr float INSTRUMENT_TAIL_SHORTEN_AMP_PROTECT = 0.6f; // voices louder than this are protected
static constexpr float INSTRUMENT_TAIL_SHORTEN_MIN_FACTOR = 0.3f; // minimum shorten factor at full load
static constexpr float VOICE_TAIL_SHORTEN_MIN_CLAMP = 0.1f; // voice-level clamp for shortening factor

class DrumKitEngine;
class SampleLoader;

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
    KitLib::DecayEnvelope decayEnvelope;

    SampledSound(KitLib::InstrumentSpec* instrumentSpec = nullptr, KitLib::SampleVariantSpec* sampleVariantSpec = nullptr, const juce::File& sampleFile = {}, std::vector<uint8_t> notes = {})
        : instrumentSpec(instrumentSpec)
        , sampleVariantSpec(sampleVariantSpec)
        , sampleFile(sampleFile)
        , notes(notes)
    {}
};


/**
 Lifecycle note for voices:
    - If `stopNote(true)` is called the voice will be placed into its ADSR release
    phase (via `adsr.noteOff()`) and will remain "active" until the ADSR reports
    it is no longer active. The voice is actually freed (marked available) when
    the subclass/voice rendering loop detects `adsr.isActive()` == false and
    calls `clearCurrentNote()` (see `InstrumentSynthVoice::renderNextBlock`).
    - If `stopNote(false)` is called the ADSR is reset and `clearCurrentNote()`
    is called immediately, so the voice becomes available right away.
*/
class InstrumentSynthVoice {
public:
    InstrumentSynthVoice() {}
    ~InstrumentSynthVoice() {};

    bool isVoiceActive() const { return this->getCurrentlyPlayingNote() >= 0; }
    int getCurrentlyPlayingNote() const noexcept { return this->currentlyPlayingNote; }
    double getSampleRate() const noexcept { return this->currentSampleRate; }
    void setCurrentPlaybackSampleRate (const double newRate) { this->currentSampleRate = newRate; }
    void stopNote(bool allowTailOff);

    void startNote(int midiNoteNumber, float velocity, SampledSound* sound);
    bool isPlayingChannel (const int midiChannel) const { return this->currentPlayingMidiChannel == midiChannel; }

    void aftertouchChanged (int newValue);
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);
    AftertouchChokeProcessor chokeProcessor;
    void setAdsrParameters(float attack, float decay, float sustain, float release) { this->adsr.setParameters({attack, decay, sustain, release}); }

    
    float getVelocityGain() const { return this->velocityGain; }
    void setVelocityGain(float value) { this->velocityGain = value; }
    void fadeOutSec(float ms);

    SampledSound* getCurrentlyPlayingSound() const noexcept { return this->currentlyPlayingSound; }

    bool wasStartedBefore(const InstrumentSynthVoice& other) const noexcept { return this->noteOnTime < other.noteOnTime; }

    bool isPlayingButReleased() const noexcept {
        if (!this->isVoiceActive()) return false;
        return this->adsr.isActive();
    }

    void clearCurrentNote() {
        this->currentlyPlayingNote = -1;
        this->currentlyPlayingSound = nullptr;
        this->currentPlayingMidiChannel = 0;
    }

    void setTailShorteningFactor(float factor) {
        factor = juce::jlimit(VOICE_TAIL_SHORTEN_MIN_CLAMP, 1.0f, factor);
        if (factor >= this->tailShortenFactor)
            return; // never lengthen here, only shorten
        this->tailShortenFactor = factor;

        juce::ADSR::Parameters p = this->adsr.getParameters();
        p.release = this->releaseTimeSec * this->tailShortenFactor;
        this->adsr.setParameters(p); //FIXME ADSR
    }

private:
    friend class InstrumentSynth;

    uint32_t artNameId;
    uint32_t artExprId;

    int currentlyPlayingNote = -1;
    int currentPlayingMidiChannel = 0;

    double currentSampleRate = 44100.0;
    juce::uint32 noteOnTime = 0;

    // double releaseStartTimeMs = 0.0;
    SampledSound* currentlyPlayingSound = nullptr;

    float attackTimeSec = 0.001f;
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
    using ArtRtIdx = int8_t;
    using VelLayerIdx = int8_t;

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
        juce::String codeRaw; //FIXME: remove after debugged or hide under something like ifdef DEBUG_SOMETHING
        StringsDictSingleton::Id nameId = 0;
        StringsDictSingleton::Id exprId = 0;
        // For each incoming MIDI velocity (0..127) map to a preferred velocity layer index
        std::array<VelLayerIdx, 128> velocityToLayer; // -1 means no layer
        std::vector<VelocityLayerRuntime> layers;

        explicit ArticulationRuntime(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId) : nameId(nameId), exprId(exprId) {
            velocityToLayer.fill(-1);
            
            //FIXME: remove after debugged or hide under something like ifdef DEBUG_SOMETHING
            this->codeRaw = StringsDictSingleton::nameOf(nameId);
            if (exprId != 0) this->codeRaw += "[" + StringsDictSingleton::nameOf(exprId) + "]";
        }

        // choose the first layer whose configured velocity is >= current velocity.
        // Example: layers {27,52,77,102,127} -> 0..27->0, 28..52->1, 53..77->2, 78..102->3, 103..127->4
        void buildVelocityMapping() {
            if (this->layers.empty()) return;
            const auto count = static_cast<VelLayerIdx>(this->layers.size());
            for (uint8_t value = 0; value <= MIDI_MAX_VALUE; ++value) {
                VelLayerIdx chosen = this->layers[count - 1].velocity == 0; // default to last layer
                for (VelLayerIdx idx = 0; idx < count; ++idx) {
                    if (value <= this->layers[idx].velocity) {
                        chosen = idx;
                        break;
                    }
                }
                this->velocityToLayer[value] = chosen;
            }
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
    int resolveArticulation(int midiNoteNumber);
    int resolveVelocityLayer(const ArticulationRuntime& art, juce::uint8 velocity);
    bool startLayerVoices(ArticulationRuntime& art, VelocityLayerRuntime& layer, uint8_t chosenVariant, int midiChannel, int midiNoteNumber, float velocity);
    void fadeOutExistingVoices(const ArticulationRuntime& art, int midiChannel, int midiNoteNumber, float newVelocity);
    void startSampleVoice(InstrumentSynthVoice& voice, ArticulationRuntime& art, const SampleEntry& se, int midiChannel, int midiNoteNumber, float velocity);
    void applyAftertouch(int midiChannel, int midiNoteNumber);

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
    InstrumentSynthVoice* findFreeVoice(int midiChannel, int midiNoteNumber, const int groupId) const {
        const juce::ScopedLock sl(this->lock);
        for (auto* voice : this->voices)
            if (!voice->isVoiceActive()) return voice;
        // LOG(Debug, "No free voice -> attempting steal for note " + juce::String(midiNoteNumber));
        return this->findVoiceToSteal(midiChannel, midiNoteNumber, groupId);
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
    
    void stopVoice(InstrumentSynthVoice* voice, const bool allowTailOff) { //unused
        jassert(voice != nullptr);
        voice->stopNote(allowTailOff);
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
    const MicLayout* getMicLayout() const;
    SampleLoader* getSampleLoader();

    void prepareToPlay(double sampleRate, int samplesPerBlock);

    void renderNextBlock(juce::AudioBuffer<float>& outputAudio, const juce::MidiBuffer& inputMidi, int startSample, int numSamples);
    void renderVoices(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    InstrumentSynthVoice* findVoiceToSteal(int midiChannel, int midiNoteNumber, int groupId) const;

    int getMicChannelCount() const { return mMicChannelCount; }

    void setMicChannelCount(int channels) { mMicChannelCount = channels; }
    juce::AudioBuffer<float>& getMicBuffer() { return micBuffer; }

    // Per-instrument mapping: mic buffer channel index -> host output channel index
    void setMicOutputMapping(const std::vector<int>& mapping) { this->micOutputMapping = mapping; }
    const std::vector<int>& getMicOutputMapping() const { return this->micOutputMapping; }

    KitLib::InstrumentSpec::Type getType() const { return this->type; }

    const juce::String& getId() const { return id; }

    int8_t findArticulationIndex(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId) const { //FIXME: unused
        const int8_t count = static_cast<int8_t>(this->runtimeArticulations.size());
        for (int8_t i = 0; i < count; ++i) {
            const auto& artRt = this->runtimeArticulations[i];
            if (artRt.nameId == nameId && artRt.exprId == exprId)
                return i;
        }
        return -1;
    }
    juce::String getArtName(const int artIdx) const {
        const auto& art = this->runtimeArticulations[artIdx];
        juce::String artLabel = StringsDictSingleton::nameOf(art.nameId);
        if (art.exprId != 0) artLabel += "[" + StringsDictSingleton::nameOf(art.exprId) + "]";
        return artLabel;
    }
    ArtRtIdx getOrAddRuntimeArticulationIdx(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId) {
        for (size_t i = 0; i < this->runtimeArticulations.size(); ++i)
            if (this->runtimeArticulations[i].nameId == nameId && this->runtimeArticulations[i].exprId == exprId)
                return static_cast<ArtRtIdx>(i);
        this->runtimeArticulations.emplace_back(nameId, exprId);
        return static_cast<ArtRtIdx>(this->runtimeArticulations.size() - 1);
    }

    void applyAdaptiveTailShortening() {
        const int activeVoices = [&]() {
            int c = 0;
            for (auto* v : this->voices)
                if (v->isVoiceActive())
                    ++c;
            return c;
        }();

        const int maxVoices = this->voices.size();
        if (activeVoices < maxVoices * INSTRUMENT_TAIL_SHORTEN_LOAD_THRESHOLD) return; // no need
        const float load = static_cast<float>(activeVoices) / static_cast<float>(maxVoices);
        // load TAIL_SHORTEN_LOAD_THRESHOLD → 1.0 maps to factor 1.0 → INSTRUMENT_TAIL_SHORTEN_MIN_FACTOR
        const float minTailShortenFactor = juce::jmap(load, INSTRUMENT_TAIL_SHORTEN_LOAD_THRESHOLD, 1.0f, 1.0f, INSTRUMENT_TAIL_SHORTEN_MIN_FACTOR);
        for (auto* voice : this->voices) {
            if (!voice->isVoiceActive()) continue;
            const float amp = voice->getCurrentAmplitude();
            // Do NOT shorten loud fresh hits
            if (amp > INSTRUMENT_TAIL_SHORTEN_AMP_PROTECT) continue;
            // Older voices get more shortening
            const float age = static_cast<float>(lastNoteOnCounter - voice->noteOnTime);
            const float ageFactor = juce::jlimit(0.0f, 1.0f, age * INSTRUMENT_TAIL_SHORTEN_AGE_MULTIPLIER);
            const float finalFactor = juce::jmap(ageFactor, 0.0f, 1.0f, 1.0f, minTailShortenFactor);
            // LOG(Debug, "Applying tail shortening to voice " + juce::String(voice->currentlyPlayingNote) + " age=" + juce::String(age) + " ageFactor=" + juce::String(ageFactor) + " load=" + juce::String(load) + " minTailShortenFactor=" + juce::String(minTailShortenFactor) + " finalFactor=" + juce::String(finalFactor));
            voice->setTailShorteningFactor(finalFactor);
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


    using LayoutArticulation = KitLayout::InstrumentSlot::ArticulationTrigger;
    using CcDimensionsMap = std::unordered_map<int8_t, std::vector<std::pair<int8_t, int8_t>>>;

    // Runtime optimized tables for fast selection (populated by loadInstrument)
    // Map midi note [0..127] -> articulation index in runtimeArticulations (-1 = none)
    std::array<ArtRtIdx, 128> noteToArticulation;
    // Runtime articulations vector, index-stable after load
    std::vector<ArticulationRuntime> runtimeArticulations;
    
    std::unordered_map<int8_t, std::vector<uint8_t>> noteToControllers;
    // If this articulation subscribes to controllers -> for each CC id store 128-entry mapping to expression index
    // Stored as map CC -> array[valueIndex for 0..127]
    std::unordered_map<int, std::array<uint8_t, 128>> ccToArticulation;
    // Cache last seen controller value and resulting articulation index to avoid
    // spamming articulation change logic when controller jitters around a
    // threshold.
    std::unordered_map<int, int> lastCcValue; // controller -> last 0..127 value
    std::unordered_map<int, uint8_t> lastCcArticulation; // controller -> last applied articulation index
    
    // (owned) storage for all SampledSound instances created for this synth
    // (use `sampledSoundPtrs` above which holds unique_ptrs)
    std::vector<int> micOutputMapping;

    // Velocity of the note currently being requested (0..1). Used by custom stealing logic.
    mutable std::atomic<float> lastRequestedVelocity{0.0f};


    void processLayoutArticulations(KitLib::InstrumentSpec* instSpec);
    void processArticulationSpec(KitLib::InstrumentSpec* instSpec
        , const LayoutArticulation& layoutArt
        , KitLib::ArticulationSpec* artSpec
        , ArticulationRuntime& artRt
        , ArtRtIdx artRtIdx
        , CcDimensionsMap& foundCcDimensions
    );
    void createVelocityLayers(KitLib::InstrumentSpec* instSpec
        , const KitLayout::MidiNotes& notes
        , KitLib::ArticulationSpec* artSpec
        , ArticulationRuntime& artRt
        , const std::vector<int>& micStarts
        , const std::vector<int>& micChannels
        , int& unmatchedStart
        , MicRoutingBuilder& micRouting
    );
    void buildCcMappings(const LayoutArticulation& layoutArt, KitLib::ArticulationSpec* artSpec, CcDimensionsMap& foundCcDimensions);
    void processCcDimensions(CcDimensionsMap& foundCcDimensions);

    uint8_t chooseVariantGroup(VelocityLayerRuntime& layer);

    KitLib::ArticulationSpec* resolveArticulationSpec(KitLib::InstrumentSpec* instSpec, const LayoutArticulation& layoutArt);
};
