#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <algorithm>
#include <limits>
#include <set>
#include <optional>
#include "AftertouchChokeProcessor.h"
#include "EngineSettings.h"
#include "HiHatTransitionPolicy.h"
#include "KitLibModel.h"
#include "LayoutManager.h"

class DrumKitEngine;
class SampleLoader;
class InstrumentSynth;
struct InstrumentSynthTestAccess;

class SampledSound {
public:
    KitLib::InstrumentSpec* instrumentSpec = nullptr;
    KitLib::SampleVariantSpec* sampleVariantSpec = nullptr;
    juce::File sampleFile;
    std::shared_ptr<juce::AudioBuffer<float>> buffer;
    std::atomic<bool> loaded{false};
    std::vector<uint8_t> notes; 

    double sampleRate = 0.0;
    int sampleLengthSamples = 0;
    float sampleLengthMs = 0.0f;
    bool transitionAudioValid = false;
    int micBufferChannelStart = 0;
    int micBufferChannelCount = 0;
    juce::String routedMicCode;
    bool useDirectBalance = false;
    int balance = 0;
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
    enum class EnvelopeState {
        Idle,
        Playing,
        FadingOut,
        Releasing,
        Dying
    };

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
    void requestFadeOut(float durationSec);
    void requestRelease();
    void killImmediately();
    float getEstimatedCurrentDb() const;
    void updateEstimatedDb();
    EnvelopeState getEnvelopeState() const noexcept { return this->envelopeState; }
    juce::uint32 getEventId() const noexcept { return this->noteOnTime; }

    SampledSound* getCurrentlyPlayingSound() const noexcept { return this->currentlyPlayingSound; }
    void setOwner(InstrumentSynth* synth) noexcept { this->owner = synth; }

    bool wasStartedBefore(const InstrumentSynthVoice& other) const noexcept { return this->noteOnTime < other.noteOnTime; }

    bool isPlayingButReleased() const noexcept {
        if (!this->isVoiceActive()) return false;
        return this->adsr.isActive() && (this->envelopeState == EnvelopeState::Releasing || this->envelopeState == EnvelopeState::FadingOut);
    }

    void clearCurrentNote() {
        this->currentlyPlayingNote = -1;
        this->currentlyPlayingSound = nullptr;
        this->currentPlayingMidiChannel = 0;
        this->envelopeState = EnvelopeState::Idle;
    }

    void setTailShorteningFactor(float factor, float minClamp) {
        factor = juce::jlimit(minClamp, 1.0f, factor);
        if (factor >= this->tailShortenFactor)
            return; // never lengthen here, only shorten
        this->tailShortenFactor = factor;

        juce::ADSR::Parameters p = this->adsr.getParameters();
        p.release = this->releaseTimeSec * this->tailShortenFactor;
        this->adsr.setParameters(p);
    }

private:
    friend class InstrumentSynth;
    friend struct InstrumentSynthTestAccess;

    InstrumentSynth* owner = nullptr;

    uint32_t artNameId;
    uint32_t artExprId;

    int currentlyPlayingNote = -1;
    int currentPlayingMidiChannel = 0;

    double currentSampleRate = 44100.0;
    juce::uint32 noteOnTime = 0;
    EnvelopeState envelopeState = EnvelopeState::Idle;

    // double releaseStartTimeMs = 0.0;
    SampledSound* currentlyPlayingSound = nullptr;

    float attackTimeSec = 0.001f;
    float releaseTimeSec = 0.1f;
    bool useOneShotDecayEnvelope = false;

public:
    void setAttackTimeSec(float sec) { this->attackTimeSec = sec; }
    float getAttackTimeSec() const noexcept { return this->attackTimeSec; }
    void setReleaseTimeSec(float sec) { this->releaseTimeSec = sec; }
    float getReleaseTimeSec() const noexcept { return this->releaseTimeSec; }
    void setUseOneShotDecayEnvelope(bool shouldUse) { this->useOneShotDecayEnvelope = shouldUse; }
    void setPitchRatioMultiplier(double multiplier) { this->pitchRatioMultiplier = multiplier; }
    
    float getCurrentAmplitude() const { return this->lastEnvelopeValue * this->velocityGain; }
    
    // juce::RangedAudioParameter* gainParam = nullptr; //TODO?
    // juce::RangedAudioParameter* panParam = nullptr; //TODO?
    // juce::AudioParameterBool* invertPhaseParam = nullptr; //TODO?

    juce::ADSR adsr;

    double sourceSamplePosition = 0.0;
    double pitchRatio = 1.0;
    double pitchRatioMultiplier = 1.0;
    
    float velocityGain = 1.0f;
    float tailShortenFactor = 1.0f;   // 1.0 = normal, <1 = shortened
    float lastEnvelopeValue = 0.0f;
    float cachedEstimatedDb = -100.0f;
    uint8_t dbUpdateCounter = 0; // throttle updateEstimatedDb to every N-th render call
    int hitGroupId = -1;

public:
    JUCE_LEAK_DETECTOR(InstrumentSynthVoice)
};

class MicRoutingBuilder;
class MicRoutingPlan;

class InstrumentSynth {
public:
    using ArtRtIdx = int8_t;
    using VelLayerIdx = int8_t;
    using ArticulationSourceMap = std::unordered_map<uint32_t, KitLib::InstrumentSpec*>;
    using ArticulationNameRemap = std::unordered_map<uint32_t, uint32_t>; // slotArtNameId -> sourceArtNameId
    using ArticulationAliasMap = std::unordered_map<uint32_t, uint32_t>; // fallbackArtNameId -> sourceArtNameId

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

    struct HiHatTransitionCounters {
        uint64_t processed = 0;
        uint64_t coalesced = 0;
        uint64_t skipped = 0;
        uint64_t completed = 0;
    };

    struct ArticulationRuntime {
        StringsDictSingleton::Id nameId = 0;
        StringsDictSingleton::Id exprId = 0;
        // pdl_opn_cls value extracted from ArticulationSpec expression dimensions.
        // Higher value = more closed/tight = stronger (mutes articulations with lower pocValue).
        // -1 = not a PedalOpenClose articulation.
        int pocValue = -1;
        // For each incoming MIDI velocity (0..127) map to a preferred velocity layer index
        std::array<VelLayerIdx, 128> velocityToLayer; // -1 means no layer
        std::vector<VelocityLayerRuntime> layers;

        explicit ArticulationRuntime(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId) : nameId(nameId), exprId(exprId) {
            velocityToLayer.fill(-1);
        }

        juce::String getCodeRaw() const {
            juce::String result = StringsDictSingleton::nameOf(nameId);
            if (exprId != 0) result += "[" + StringsDictSingleton::nameOf(exprId) + "]";
            return result;
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

    struct RoutingContext {
        KitLib::InstrumentSpec* defaultInstrument = nullptr;
        ArticulationSourceMap artSources;
        std::vector<uint32_t> allowedArticulationNameIds;
        ArticulationNameRemap artNameRemap;
        ArticulationAliasMap artAliases;

        bool isValid() const { return defaultInstrument != nullptr; }
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
        this->lastCcArticulation.fill(-1);
        this->pendingCcArticulation.fill(-1);
    }
    ~InstrumentSynth() {}

    void unloadInstrument();
    void loadInstrument(KitLib::InstrumentSpec* kitInstrument);
    void loadInstrumentWithSources(KitLib::InstrumentSpec* defaultInstrument, const ArticulationSourceMap& artSources);
    void loadInstrumentWithSources(KitLib::InstrumentSpec* defaultInstrument, const ArticulationSourceMap& artSources, const std::vector<uint32_t>& allowedArticulationNameIds, const ArticulationNameRemap& artNameRemap = {}, const ArticulationAliasMap& artAliases = {});
    bool isLoaded() { return instrumentSpecstPtr != nullptr; }
    void setTuningOffsetCents(int cents) { this->tuningOffsetCents = cents; }
    int getTuningOffsetCents() const { return this->tuningOffsetCents; }
    void setAttackOverrideMs(int ms) { if (ms < 0) this->attackOverrideMs.reset(); else this->attackOverrideMs = ms; }
    std::optional<int> getAttackOverrideMs() const { return this->attackOverrideMs; }
    void setDecayOverrideMs(int ms) { if (ms < 0) this->decayOverrideMs.reset(); else this->decayOverrideMs = ms; }
    std::optional<int> getDecayOverrideMs() const { return this->decayOverrideMs; }
    void setVolumeDb(float db) { this->volumeDb = db; }
    float getVolumeDb() const { return this->volumeDb; }
    void setFlipLR(bool shouldFlip) { this->flipLR = shouldFlip; }
    bool getFlipLR() const { return this->flipLR; }
    void setFlipPhase(bool shouldFlip) { this->flipPhase = shouldFlip; }
    bool getFlipPhase() const { return this->flipPhase; }
    void setMicBleedLevels(std::unordered_map<juce::String, float> levels);
    float getMicBleedLevel(const juce::String& micCode) const;
    const std::unordered_map<juce::String, float>& getMicBleedLevels() const { return this->micBleedLevels; }
    void clearParameterOverrides();
    
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
    HiHatTransitionCounters getHiHatTransitionCounters() const noexcept {
        return {
            this->processedHiHatTransitions.load(std::memory_order_relaxed),
            this->coalescedHiHatTransitions.load(std::memory_order_relaxed),
            this->skippedHiHatTransitions.load(std::memory_order_relaxed),
            this->completedHiHatTransitions.load(std::memory_order_relaxed),
        };
    }
    void handleAftertouch(int midiChannel, int midiNoteNumber, int aftertouchValue) {
        for (auto* voice : voices)
            if (voice->getCurrentlyPlayingNote() == midiNoteNumber && (midiChannel <= 0 || voice->isPlayingChannel(midiChannel)))
                voice->aftertouchChanged(aftertouchValue);
    }
    SampledSound* addSound(SampledSound* newSound) {
        const juce::ScopedLock sl(this->lock);
        if (newSound == nullptr) return nullptr;
        this->sampledSoundPtrs.emplace_back(std::shared_ptr<SampledSound>(newSound));
        return newSound;
    }
    SampledSound* addSound(std::shared_ptr<SampledSound> newSound) {
        const juce::ScopedLock sl(this->lock);
        if (!newSound) return nullptr;
        auto* rawPtr = newSound.get();
        this->sampledSoundPtrs.emplace_back(std::move(newSound));
        return rawPtr;
    }
    InstrumentSynthVoice* addVoice(InstrumentSynthVoice* const newVoice) {
        const juce::ScopedLock sl(this->lock);
        newVoice->setCurrentPlaybackSampleRate(this->sampleRate);
        newVoice->setOwner(this);
        return this->voices.add(newVoice);
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
        voice->noteOnTime = ++this->lastNoteOnCounter;
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
    const MicRoutingPlan* getMicRoutingPlan() const;
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
    const KitLib::InstrumentSpec* getLoadedInstrumentSpec() const { return this->instrumentSpecstPtr; }
    KitLib::InstrumentSpec* getLoadedInstrumentSpec() { return this->instrumentSpecstPtr; }
    const KitLayout::InstrumentSlot& getInstrumentSlot() const { return this->instrumentSlot; }
    const RoutingContext& getLastRoutingContext() const { return this->lastRoutingContext; }

    /** Rebuild note/CC routing from the given layout slot without reloading samples.
     *  Call after MIDI note or controller assignments change in the UI. */
    void rebuildRoutingFromLayout(const KitLayout::InstrumentSlot& layoutSlot);

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
    bool supportsArticulation(const juce::String& artName) const {
        const auto normalizedTarget = artName.upToFirstOccurrenceOf("[", false, false).trim();
        if (normalizedTarget.isEmpty() || normalizedTarget == "(default)")
            return true;

        for (const auto& art : this->runtimeArticulations) {
            const auto runtimeArtName = StringsDictSingleton::nameOf(art.nameId).upToFirstOccurrenceOf("[", false, false).trim();
            if (runtimeArtName.compareIgnoreCase(normalizedTarget) == 0)
                return true;
        }

        return false;
    }
    ArtRtIdx getOrAddRuntimeArticulationIdx(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId) {
        for (size_t i = 0; i < this->runtimeArticulations.size(); ++i)
            if (this->runtimeArticulations[i].nameId == nameId && this->runtimeArticulations[i].exprId == exprId)
                return static_cast<ArtRtIdx>(i);
        this->runtimeArticulations.emplace_back(nameId, exprId);
        return static_cast<ArtRtIdx>(this->runtimeArticulations.size() - 1);
    }

    void applyAdaptiveTailShortening();

private:
    friend struct InstrumentSynthTestAccess;

    DrumKitEngine* parent = nullptr;
    KitLib::InstrumentSpec* instrumentSpecstPtr = nullptr;
    juce::String id;
    KitLib::InstrumentSpec::Type type;
    juce::AudioBuffer<float> micBuffer;
    int mMicChannelCount = 2;
    // Keep a copy of the kit layout instrument definition for note->articulation lookup
    KitLayout::InstrumentSlot instrumentSlot;
    RoutingContext lastRoutingContext;

    juce::uint32 lastNoteOnCounter = 0;
    juce::CriticalSection lock;
    double sampleRate = 0;
    juce::OwnedArray<InstrumentSynthVoice> voices;
    int activeVoiceCount = 0;
    std::vector<std::shared_ptr<SampledSound>> sampledSoundPtrs;
    int minimumSubBlockSize = 32;
    bool subBlockSubdivisionIsStrict = false;

    juce::AudioBuffer<float> tempBuffer;


    using LayoutArticulation = KitLayout::InstrumentSlot::ArticulationTrigger;
    using CcDimensionsMap = std::unordered_map<int8_t, std::vector<std::pair<int8_t, int8_t>>>;

    struct ResolvedArticulationBinding {
        LayoutArticulation layoutArt;
        LayoutArticulation resolvedLayoutArt;
        KitLib::InstrumentSpec* sourceInstrument = nullptr;
        KitLib::ArticulationSpec* resolvedSpec = nullptr;
        std::vector<KitLib::ArticulationSpec*> controllerSiblingSpecs;

        explicit ResolvedArticulationBinding(const LayoutArticulation& layoutArt)
            : layoutArt(layoutArt)
            , resolvedLayoutArt(layoutArt)
        {}
    };

    // Runtime optimized tables for fast selection (populated by loadInstrument)
    // Map midi note [0..127] -> articulation index in runtimeArticulations (-1 = none)
    std::array<ArtRtIdx, 128> noteToArticulation;
    // Runtime articulations vector, index-stable after load
    std::vector<ArticulationRuntime> runtimeArticulations;
    // Flat map for O(1) articulation-index lookup by (nameId<<32|exprId) key — rebuilt after every load
    std::unordered_map<uint64_t, int> artIdByNameExprId;
    
    std::unordered_map<int8_t, std::vector<uint8_t>> noteToControllers;
    // Flat lookup table for CC -> articulation mapping (replaces unordered_map for cache locality)
    // ccToArticulationMask[cc] = true if CC number has an articulation mapping
    std::array<bool, 128> ccToArticulationMask {};
    std::array<std::array<uint8_t, 128>, 128> ccToArticulationTable {};
    // Cache last seen controller value and resulting articulation index to avoid
    // spamming articulation change logic when controller jitters around a
    // threshold.
    std::array<int, 128> lastCcValue {};
    std::array<int8_t, 128> lastCcArticulation; // -1 = not cached
    std::array<int, 128> pendingCcValue {};
    std::array<int, 128> pendingCcMidiChannel {};
    std::array<bool, 128> pendingCcTransition {};
    std::array<ArtRtIdx, 128> pendingCcArticulation {};
    std::array<bool, 128> hihatPedalClosedState {};
    std::array<bool, 128> hihatImplicitPedalEligible {};
    std::array<uint64_t, 128> lastCcProcessedSample {};
    std::array<bool, 128> hasProcessedCcTransition {};
    int hihatPedalNote = -1;
    uint64_t renderedSampleCount = 0;
    std::atomic<uint64_t> processedHiHatTransitions { 0 };
    std::atomic<uint64_t> coalescedHiHatTransitions { 0 };
    std::atomic<uint64_t> skippedHiHatTransitions { 0 };
    std::atomic<uint64_t> completedHiHatTransitions { 0 };
    
    // (owned) storage for all SampledSound instances created for this synth
    // (use `sampledSoundPtrs` above which holds shared_ptrs)
    std::vector<int> micOutputMapping;

    // Velocity of the note currently being requested (0..1). Used by custom stealing logic.
    mutable std::atomic<float> lastRequestedVelocity{0.0f};

    int tuningOffsetCents = 0;
    std::optional<int> attackOverrideMs;
    std::optional<int> decayOverrideMs;
    float volumeDb = 0.0f;
    bool flipLR = false;
    bool flipPhase = false;
    std::unordered_map<juce::String, float> micBleedLevels;

    void storeRoutingContext(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationSourceMap& artSources, const std::vector<uint32_t>& allowedArticulationNameIds, const ArticulationNameRemap& artNameRemap, const ArticulationAliasMap& artAliases);
    void resetRoutingTables();
    void rebuildArtLookupCache();
    bool tryGetRuntimeArticulationIdx(const KitLib::ArticulationSpec* artSpec, bool createIfMissing, ArtRtIdx& artIdx);
    std::vector<ResolvedArticulationBinding> collectResolvedLayoutArticulations(const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext);
    void applyResolvedArticulationBinding(const LayoutArticulation& layoutArt, KitLib::InstrumentSpec* sourceInstrument, KitLib::ArticulationSpec* artSpec, bool loadSamples, bool createIfMissing, CcDimensionsMap& foundCcDimensions, std::set<juce::String>& skippedMicCodes);
    void applyResolvedRouting(const std::vector<ResolvedArticulationBinding>& bindings, bool loadSamples, bool createIfMissing);
    void applyArticulationAliases(const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext);
    void processLayoutArticulations(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationSourceMap& artSources, const std::vector<uint32_t>& allowedArticulationNameIds = {}, const ArticulationNameRemap& artNameRemap = {}, const ArticulationAliasMap& artAliases = {});
    void processArticulationSpec(KitLib::InstrumentSpec* instSpec
        , const LayoutArticulation& layoutArt
        , KitLib::ArticulationSpec* artSpec
        , ArticulationRuntime& artRt
        , CcDimensionsMap& foundCcDimensions
        , std::set<juce::String>& skippedMicCodes
    );
    void createVelocityLayers(KitLib::InstrumentSpec* instSpec
        , const KitLayout::MidiNotes& notes
        , KitLib::ArticulationSpec* artSpec
        , ArticulationRuntime& artRt
        , MicRoutingBuilder& micRouting
    );
    void buildCcMappings(const LayoutArticulation& layoutArt, KitLib::ArticulationSpec* artSpec, CcDimensionsMap& foundCcDimensions);
    void processCcDimensions(CcDimensionsMap& foundCcDimensions);

    uint8_t chooseVariantGroup(VelocityLayerRuntime& layer);
    bool hasAnyLoadedVariant(const VelocityLayerRuntime& layer) const;
    bool isVariantReady(const VelocityLayerRuntime& layer, uint8_t variantIdx) const;
    void cullMaskedVoices(const VelocityLayerRuntime& layer, uint8_t chosenVariant, int midiChannel, int midiNoteNumber, float newVelocity);
    void processHiHatControllerTransition(int midiChannel, int controllerNumber, int controllerValue);
    void processPendingHiHatControllers();
    bool triggerHiHatPedalSample(int midiChannel, float sourceAudibleDb);
    // Hi-hat only: fade out active voices whose pocValue is strictly less than newArtPocValue,
    // implementing the rule: tighter/more-closed articulations choke more-open ringing voices.
    void chokeHiHatVoicesByPoc(int newArtPocValue, float fadeTimeSec);

    KitLib::ArticulationSpec* resolveArticulationSpec(KitLib::InstrumentSpec* instSpec, const LayoutArticulation& layoutArt);
};
