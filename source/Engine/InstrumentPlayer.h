#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <algorithm>
#include <limits>
#include <set>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "AftertouchChokeProcessor.h"
#include "EngineSettings.h"
#include "HiHatTransitionPolicy.h"
#include "KitLibModel.h"
#include "LayoutManager.h"
#include "VelocityResponse.h"

class DrumKitEngine;
class SampleLoader;
class InstrumentPlayer;
struct InstrumentPlayerTestAccess;

enum class MicPlacementMode : uint8_t {
    PreserveChannels,
    MonoCenter,
    MonoSourceAndLayerPan,
    StereoLayerPan
};

class SampledSound {
public:
    enum class SourceRecipeStatus : uint8_t {
        NotLoaded,
        Applied,
        Pending,
        Failed
    };

    KitLib::InstrumentSpec* instrumentSpec = nullptr;
    KitLib::SampleSpec* sampleSpec = nullptr;
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
    StringsDictSingleton::Id routedMicCodeId = StringsDictSingleton::EMPTY;
    std::atomic<float> cachedMicBleedGain { 1.0f };
    float playbackOffsetMs = 0.0f;
    int runtimeArticulationIndex = -1;
    int aftertouchChokeArticulationIndex = -1;
    bool isAftertouchChokeTarget = false;
    MicPlacementMode micPlacementMode = MicPlacementMode::PreserveChannels;
    int placementBalance = 0;
    std::atomic<float> cachedPlacementLeftGain { 1.0f };
    std::atomic<float> cachedPlacementRightGain { 1.0f };
    float canonicalFixedGainDb = 0.0f;
    float requestedFixedGainDb = 0.0f;
    std::atomic<float> cachedSourceGainCorrection { 1.0f };
    std::atomic<uint64_t> sourceRecipeRevision { 0 };
    std::atomic<SourceRecipeStatus> sourceRecipeStatus { SourceRecipeStatus::NotLoaded };
    std::atomic<bool> readinessReported { false };
    KitLib::DecayEnvelope decayEnvelope;

    SampledSound(KitLib::InstrumentSpec* instSpec = nullptr, KitLib::SampleSpec* newSampleSpec = nullptr, const juce::File& file = {}, std::vector<uint8_t> midiNotes = {})
        : instrumentSpec(instSpec)
        , sampleSpec(newSampleSpec)
        , sampleFile(file)
        , notes(std::move(midiNotes))
    {}
};


/**
 Lifecycle note for voices:
    - If `stopNote(true)` is called the voice will be placed into its ADSR release
    phase (via `adsr.noteOff()`) and will remain "active" until the ADSR reports
    it is no longer active. The voice is actually freed (marked available) when
    the subclass/voice rendering loop detects `adsr.isActive()` == false and
    calls `clearCurrentNote()` (see `InstrumentPlayerVoice::renderNextBlock`).
    - If `stopNote(false)` is called the ADSR is reset and `clearCurrentNote()`
    is called immediately, so the voice becomes available right away.
*/
class InstrumentPlayerVoice {
public:
    enum class EnvelopeState {
        Idle,
        Playing,
        FadingOut,
        Releasing,
        Dying
    };

    InstrumentPlayerVoice() {}
    ~InstrumentPlayerVoice() {}

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
    juce::uint8 getMidiVelocity() const { return this->midiVelocity; }
    void setMidiVelocity(juce::uint8 value) { this->midiVelocity = value; }
    void requestFadeOut(float durationSec);
    void requestRelease();
    void killImmediately();
    float getEstimatedCurrentDb() const;
    void updateEstimatedDb();
    EnvelopeState getEnvelopeState() const noexcept { return this->envelopeState; }
    juce::uint32 getEventId() const noexcept { return this->noteOnTime; }

    SampledSound* getCurrentlyPlayingSound() const noexcept { return this->currentlyPlayingSound; }
    void setOwner(InstrumentPlayer* instrumentPlayer) noexcept { this->owner = instrumentPlayer; }

    bool wasStartedBefore(const InstrumentPlayerVoice& other) const noexcept { return this->noteOnTime < other.noteOnTime; }

    bool isPlayingButReleased() const noexcept {
        if (!this->isVoiceActive()) return false;
        return this->adsr.isActive() && (this->envelopeState == EnvelopeState::Releasing || this->envelopeState == EnvelopeState::FadingOut);
    }

    void clearCurrentNote() {
        this->currentlyPlayingNote = -1;
        this->currentlyPlayingSound = nullptr;
        this->currentBuffer.reset();
        this->currentPlayingMidiChannel = 0;
        this->artRuntimeIdx = -1;
        this->midiVelocity = 0;
        this->envelopeState = EnvelopeState::Idle;
        this->playbackDelaySamplesRemaining = 0;
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
    friend class InstrumentPlayer;
    friend struct InstrumentPlayerTestAccess;

    InstrumentPlayer* owner = nullptr;

    uint32_t artNameId = 0;
    uint32_t artExprId = 0;
    int8_t artRuntimeIdx = -1;

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
    
    float getCurrentAmplitude() const { return this->lastEnvelopeValue * this->velocityGain * this->lastSourceGainCorrection; }
    
    // juce::RangedAudioParameter* gainParam = nullptr; //TODO?
    // juce::RangedAudioParameter* panParam = nullptr; //TODO?
    // juce::AudioParameterBool* invertPhaseParam = nullptr; //TODO?

    juce::ADSR adsr;

    double sourceSamplePosition = 0.0;
    double pitchRatio = 1.0;
    double pitchRatioMultiplier = 1.0;
    
    float velocityGain = 1.0f;
    juce::uint8 midiVelocity = 0;
    float tailShortenFactor = 1.0f;   // 1.0 = normal, <1 = shortened
    float lastEnvelopeValue = 0.0f;
    float cachedEstimatedDb = -100.0f;
    uint8_t dbUpdateCounter = 0; // throttle updateEstimatedDb to every N-th render call
    int hitGroupId = -1;
    int playbackDelaySamplesRemaining = 0;
    std::shared_ptr<juce::AudioBuffer<float>> currentBuffer;
    float currentCanonicalFixedGainDb = 0.0f;
    KitLib::DecayEnvelope currentDecayEnvelope;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sourceGainCorrection { 1.0f };
    float lastSourceGainCorrection = 1.0f;

    void setSourceGainCorrectionDb(float correctionDb) {
        this->sourceGainCorrection.setTargetValue(juce::Decibels::decibelsToGain(correctionDb, -100.0f));
    }

public:
    JUCE_LEAK_DETECTOR(InstrumentPlayerVoice)
};

class MicRoutingBuilder;
class MicRoutingPlan;

class InstrumentPlayer {
public:
    using ArtRtIdx = int8_t;
    using VelocityLevelIdx = int16_t;
    using ArticulationLibraryMap = std::unordered_map<uint32_t, KitLib::InstrumentSpec*>;
    using ArticulationNameRemap = std::unordered_map<uint32_t, uint32_t>; // articulationSlotNameId -> libraryArticulationNameId
    struct ArticulationTarget {
        uint32_t nameId = 0;
        uint32_t exprId = 0;
    };
    using ArticulationExactRemap = std::unordered_map<uint64_t, ArticulationTarget>; // articulationSlot(name, expression) -> libraryArticulation(name, expression)
    using ArticulationAliasMap = std::unordered_map<uint32_t, ArticulationTarget>; // fallbackArticulationNameId -> libraryArticulation(name, expression)
    using MicRouteOverrideIdMap = std::unordered_map<StringsDictSingleton::Id, StringsDictSingleton::Id>; // micCodeId -> outputKey

    using ArticulationSlot = KitLayout::InstrumentSlot::ArticulationSlot;

    struct ArticulationPlaybackEntry {
        ArticulationSlot articulationSlot;
        uint32_t articulationSlotNameId = 0;
        uint32_t articulationSlotExpressionId = 0;
        KitLib::InstrumentSpec* libraryInstrument = nullptr;
        KitLib::ArticulationSpec* libraryArticulation = nullptr;
        std::vector<uint8_t> midiNotes;
        ArticulationTarget aliasTarget;
        bool usesExplicitMapLayer = false;
        bool isControllerSibling = false;
        bool isAliasEntry = false;
        explicit ArticulationPlaybackEntry(const ArticulationSlot& slot)
            : articulationSlot(slot)
            , articulationSlotNameId(articulationSlot.nameId)
            , articulationSlotExpressionId(articulationSlot.exprId)
            , midiNotes(articulationSlot.notes)
        {}
    };

    // Runtime data structures built at loadInstrument() (non-realtime)
    struct SampleEntry {
        SampledSound* sound = nullptr; // non-owning pointer into sampledSoundPtrs
        uint8_t roundRobinId = 0;
        std::weak_ptr<SampledSound> weakSound;
    };

    struct LogicalHitReadiness {
        std::atomic<int> loadedEntryCount { 0 };
        std::atomic<bool> ready { false };
        int requiredEntryCount = 0;

        void markLoaded(bool loadedSuccessfully) {
            if (!loadedSuccessfully) return;
            const int loadedCount = this->loadedEntryCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (loadedCount == this->requiredEntryCount) this->ready.store(true, std::memory_order_release);
        }
    };

    struct LogicalHitRuntime {
        uint8_t roundRobinId = 0;
        std::vector<size_t> sampleEntryIndexes;
        std::shared_ptr<LogicalHitReadiness> readiness = std::make_shared<LogicalHitReadiness>();
        std::optional<float> preTrimIntensityDb;
        bool earlyLoad = false;

        bool isReady() const { return this->readiness->ready.load(std::memory_order_acquire); }
    };

    struct VelocityLevelRuntime {
        uint8_t velocity = 0;
        uint8_t selectionUpperVelocity = 0;
        const KitLib::VelocityLevelSpec* velocityLevelSpec = nullptr;
        std::vector<SampleEntry> samples; // contiguous sample entries
        std::vector<LogicalHitRuntime> logicalHits;
        std::optional<float> fittedSourceDb;
        std::optional<float> calibrationTrimDb;
        // Use shared_ptr to an atomic so the runtime struct remains copyable/movable
        std::shared_ptr<std::atomic<uint32_t>> roundRobinCounter = std::make_shared<std::atomic<uint32_t>>(0);
    };

    struct HiHatTransitionCounters {
        uint64_t processed = 0;
        uint64_t coalesced = 0;
        uint64_t skipped = 0;
        uint64_t completed = 0;
    };

    struct LayerParameterUpdate {
        std::optional<int> tuningCents;
        std::optional<int> attackMs;
        std::optional<int> decayMs;
        std::optional<float> volumeDb;
        std::optional<int> pan;
        std::optional<bool> flipLR;
        std::optional<bool> flipPhase;
        std::optional<std::pair<juce::String, float>> micBleedLevel;
        std::optional<std::unordered_map<juce::String, float>> micBleedLevels;
        bool updateVelocityResponse = false;
        std::optional<VelocityResponse> velocityResponse;
    };

    struct LayerParameters {
        int tuningCents = 0;
        int attackMs = -1;
        int decayMs = -1;
        float volumeDb = 0.0f;
        int pan = 0;
        bool flipLR = false;
        bool flipPhase = false;
        std::optional<VelocityResponse> velocityResponse;
    };

    struct ArticulationRuntime {
        StringsDictSingleton::Id nameId = 0;
        StringsDictSingleton::Id exprId = 0;
        StringsDictSingleton::Id routeArticulationSlotNameId = 0;
        StringsDictSingleton::Id routeArticulationSlotExpressionId = 0;
        // pdl_opn_cls value extracted from ArticulationSpec expression dimensions.
        // Higher value = more closed/tight = stronger (mutes articulations with lower pedalOpenCloseValue).
        // -1 = not a PedalOpenClose articulation.
        int pedalOpenCloseValue = -1;
        int zoneCenterRimValue = -1;
        int zoneBowEdgeValue = -1;
        // For each incoming MIDI velocity (0..127), map to a preferred velocity level index.
        std::array<VelocityLevelIdx, 128> velocityToLevel; // -1 means no velocity level
        std::array<float, 128> velocityGain;
        std::array<float, 128> selectionTargetDb;
        std::array<float, 128> selectionOffsetDb;
        std::array<float, 128> residualOutputDb;
        bool calibrated = false;
        LayerParameters parameters;
        std::vector<VelocityLevelRuntime> velocityLevels;

        explicit ArticulationRuntime(StringsDictSingleton::Id newNameId, StringsDictSingleton::Id newExprId) : nameId(newNameId), exprId(newExprId) {
            velocityToLevel.fill(-1);
            for (int velocity = 0; velocity <= MIDI_MAX_VALUE; ++velocity) velocityGain[static_cast<size_t>(velocity)] = static_cast<float>(velocity) / 127.0f;
            selectionTargetDb.fill(-100.0f);
            selectionOffsetDb.fill(0.0f);
            residualOutputDb.fill(0.0f);
        }

        juce::String getCodeRaw() const {
            juce::String result = StringsDictSingleton::nameOf(nameId);
            if (exprId != 0) result += "[" + StringsDictSingleton::nameOf(exprId) + "]";
            return result;
        }

        void buildVelocityMapping(const VelocityResponse& velocityResponse);
    };

    struct RoutingContext {
        KitLib::InstrumentSpec* defaultInstrument = nullptr;
        ArticulationLibraryMap articulationLibraryMap;
        std::vector<uint32_t> allowedArticulationNameIds;
        std::vector<juce::String> allowedArticulationFilters;
        ArticulationNameRemap artNameRemap;
        ArticulationExactRemap artExactRemap;
        ArticulationAliasMap artAliases;
        std::unordered_map<juce::String, juce::String> micRouteOverrides;
        MicRouteOverrideIdMap micRouteOverrideIds;
        bool strictArticulationResolution = false;
        bool isValid() const { return defaultInstrument != nullptr; }
        void setMicRouteOverrides(const std::unordered_map<juce::String, juce::String>& overrides) {
            this->micRouteOverrides = overrides;
            this->rebuildMicRouteOverrideIds();
        }
        void rebuildMicRouteOverrideIds() {
            this->micRouteOverrideIds.clear();
            this->micRouteOverrideIds.reserve(this->micRouteOverrides.size());
            for (const auto& [micCode, outputId] : this->micRouteOverrides) {
                const auto micCodeId = StringsDictSingleton::getId(micCode.trim().toLowerCase());
                const auto outputKey = StringsDictSingleton::getId(outputId.trim().toLowerCase());
                if (micCodeId != StringsDictSingleton::EMPTY && outputKey != StringsDictSingleton::EMPTY) this->micRouteOverrideIds[micCodeId] = outputKey;
            }
        }
    };

    InstrumentPlayer(DrumKitEngine* parentEngine, KitLayout::InstrumentSlot layoutInstrument
        //DUNNO: add params to instrument or do it somewhere else?
        // ,juce::RangedAudioParameter& gainParameter
        // ,juce::RangedAudioParameter& panParameter
        // ,juce::AudioParameterBool& phaseParameter
    )
        : parent(parentEngine)
        , id(layoutInstrument.id)
        , type(layoutInstrument.type)
        , instrumentSlot(std::move(layoutInstrument))
    {
        this->lastCcArticulation.fill(-1);
        this->pendingCcArticulation.fill(-1);
        this->hihatNotePedalOpenCloseValues.fill(-1);
        this->rebuildHiHatNoteChokeValues();
    }
    ~InstrumentPlayer() {}

    void unloadInstrument();
    void loadInstrument(KitLib::InstrumentSpec* kitInstrument);
    void loadInstrument(KitLib::InstrumentSpec* kitInstrument, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot);
    void loadInstrumentWithRouting(KitLib::InstrumentSpec* defaultInstrument, const ArticulationLibraryMap& articulationLibraryMap);
    void loadInstrumentWithRouting(KitLib::InstrumentSpec* defaultInstrument, const ArticulationLibraryMap& articulationLibraryMap, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters = {}, const ArticulationNameRemap& artNameRemap = {}, const ArticulationExactRemap& artExactRemap = {}, const ArticulationAliasMap& artAliases = {}, bool strictArticulationResolution = false, const std::unordered_map<juce::String, juce::String>& micRouteOverrides = {});
    void loadInstrumentWithRouting(KitLib::InstrumentSpec* defaultInstrument, const ArticulationLibraryMap& articulationLibraryMap, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot, bool strictArticulationResolution = false, const std::unordered_map<juce::String, juce::String>& micRouteOverrides = {});
    std::vector<ArticulationPlaybackEntry> buildArticulationPlaybackEntries(const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext);
    void loadInstrumentWithPlaybackEntries(KitLib::InstrumentSpec* defaultInstrument, const RoutingContext& routingContext, std::vector<ArticulationPlaybackEntry> playbackEntries, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot = nullptr);
    bool isLoaded() { return instrumentSpecstPtr != nullptr; }
    void setTuningOffsetCents(int cents);
    int getTuningOffsetCents() const { return this->tuningOffsetCents; }
    void setAttackOverrideMs(int ms);
    std::optional<int> getAttackOverrideMs() const { return this->attackOverrideMs; }
    void setDecayOverrideMs(int ms);
    std::optional<int> getDecayOverrideMs() const { return this->decayOverrideMs; }
    void setVolumeDb(float db);
    float getVolumeDb() const { return this->volumeDb; }
    void setPan(int newPan);
    int getPan() const { return this->pan.load(std::memory_order_relaxed); }
    void setFlipLR(bool shouldFlip);
    bool getFlipLR() const { return this->flipLR; }
    void setFlipPhase(bool shouldFlip);
    bool getFlipPhase() const { return this->flipPhase; }
    void setMicBleedLevels(std::unordered_map<juce::String, float> levels);
    bool updateArticulationParameters(const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes, const LayerParameterUpdate& update);
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
    bool playPreviewArticulation(const int midiChannel, const int midiNoteNumber, const juce::String& articulationSlotCode, const juce::uint8 velocity);
    bool playLibraryLogicalHitPreview(const KitLib::ArticulationSpec& libraryArticulation, int namedVelocity, uint8_t roundRobinId, int midiChannel, int midiNoteNumber, juce::uint8 playbackVelocity);
    int resolveArticulation(int midiNoteNumber);
    int resolveVelocityLevel(const ArticulationRuntime& art, juce::uint8 velocity);
    bool startVelocityLevelVoices(ArticulationRuntime& art, ArtRtIdx artIdx, VelocityLevelRuntime& velocityLevel, LogicalHitRuntime& logicalHit, int midiChannel, int midiNoteNumber, float velocityGain, juce::uint8 midiVelocity);
    void fadeOutExistingVoices(const ArticulationRuntime& art, int midiChannel, int midiNoteNumber, float newVelocity);
    void startSampleVoice(InstrumentPlayerVoice& voice, ArticulationRuntime& art, ArtRtIdx artIdx, const SampleEntry& se, int midiChannel, int midiNoteNumber, float velocityGain, juce::uint8 midiVelocity);
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
    void handleAftertouch(int midiChannel, int midiNoteNumber, int aftertouchValue);
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
    InstrumentPlayerVoice* addVoice(InstrumentPlayerVoice* const newVoice) {
        const juce::ScopedLock sl(this->lock);
        newVoice->setCurrentPlaybackSampleRate(this->sampleRate);
        newVoice->setOwner(this);
        return this->voices.add(newVoice);
    }
    InstrumentPlayerVoice* findFreeVoice(int midiChannel, int midiNoteNumber, const int groupId) const {
        const juce::ScopedLock sl(this->lock);
        for (auto* voice : this->voices)
            if (!voice->isVoiceActive()) return voice;
        // LOG(Debug, "No free voice -> attempting steal for note " + juce::String(midiNoteNumber));
        return this->findVoiceToSteal(midiChannel, midiNoteNumber, groupId);
    }

    void startVoice(InstrumentPlayerVoice* const voice, SampledSound* const sound, const int midiChannel, const int midiNoteNumber, const float velocity) {
        if (voice == nullptr || sound == nullptr) return;
        // LOG(Debug, "Start voice note=" + juce::String(midiNoteNumber) + " group=" + juce::String(voice->hitGroupId) + " active voices=" + juce::String(voices.size()));
        if (voice->currentlyPlayingSound != nullptr)
            voice->stopNote(false);

        voice->currentlyPlayingNote = midiNoteNumber;
        voice->currentPlayingMidiChannel = midiChannel;
        voice->noteOnTime = ++this->lastNoteOnCounter;
        voice->currentlyPlayingSound = sound;
        voice->midiVelocity = static_cast<juce::uint8>(juce::jlimit(0, 127, juce::roundToInt(velocity * 127.0f)));
        voice->startNote(midiNoteNumber, velocity, sound);
    }
    
    void stopVoice(InstrumentPlayerVoice* voice, const bool allowTailOff) { //unused
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

    InstrumentPlayerVoice* findVoiceToSteal(int midiChannel, int midiNoteNumber, int groupId, InstrumentPlayerVoice* const* excludedVoices = nullptr, size_t excludedVoiceCount = 0) const;

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
    std::shared_ptr<const KitLib::LibraryManager> getLibrarySnapshot() const { return this->librarySnapshotPtr; }
    const KitLayout::InstrumentSlot& getInstrumentSlot() const { return this->instrumentSlot; }
    const RoutingContext& getLastRoutingContext() const { return this->lastRoutingContext; }

    /** Rebuild note/CC routing from the given layout slot without reloading samples.
     *  Call after MIDI note or controller assignments change in the UI. */
    void rebuildRoutingFromLayout(const KitLayout::InstrumentSlot& layoutSlot);
    bool updateMicRoute(const juce::String& micCode, const juce::String& outputId);

    int8_t findArticulationIndex(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId) const { //FIXME: unused
        const int8_t count = static_cast<int8_t>(this->runtimeArticulations.size());
        for (int8_t artIdx = 0; artIdx < count; ++artIdx) {
            const auto& artRt = this->runtimeArticulations[static_cast<size_t>(artIdx)];
            if (artRt.nameId == nameId && artRt.exprId == exprId && artRt.routeArticulationSlotNameId == 0 && artRt.routeArticulationSlotExpressionId == 0)
                return artIdx;
        }
        for (int8_t artIdx = 0; artIdx < count; ++artIdx) {
            const auto& artRt = this->runtimeArticulations[static_cast<size_t>(artIdx)];
            if (artRt.nameId == nameId && artRt.exprId == exprId)
                return artIdx;
        }
        return -1;
    }
    juce::String getArtName(const int artIdx) const {
        const auto& art = this->runtimeArticulations[static_cast<size_t>(artIdx)];
        juce::String artLabel = StringsDictSingleton::nameOf(art.nameId);
        if (art.exprId != 0) artLabel += "[" + StringsDictSingleton::nameOf(art.exprId) + "]";
        return artLabel;
    }
    bool hasArticulationSlotRoutes() const {
        for (const auto& playbackEntry : this->articulationPlaybackEntries) {
            if (!playbackEntry.isControllerSibling) return true;
        }
        return !this->articulationSlotRouteKeys.empty();
    }
    void clearArticulationSlotRoutes() { this->articulationSlotRouteKeys.clear(); }
    void setArticulationSlotRoutes(const std::vector<std::pair<uint32_t, uint32_t>>& routes) {
        this->articulationSlotRouteKeys.clear();
        for (const auto& route : routes)
            this->articulationSlotRouteKeys.insert(makeArticulationSlotRouteKey(route.first, route.second));
    }
    void copyArticulationSlotRoutesFrom(const InstrumentPlayer& source) { this->articulationSlotRouteKeys = source.articulationSlotRouteKeys; }
    bool routesArticulationSlot(uint32_t articulationSlotNameId, uint32_t articulationSlotExpressionId) const {
        for (const auto& playbackEntry : this->articulationPlaybackEntries) {
            if (!playbackEntry.isControllerSibling && playbackEntry.articulationSlotNameId == articulationSlotNameId && playbackEntry.articulationSlotExpressionId == articulationSlotExpressionId) return true;
        }
        return this->articulationSlotRouteKeys.find(makeArticulationSlotRouteKey(articulationSlotNameId, articulationSlotExpressionId)) != this->articulationSlotRouteKeys.end();
    }
    bool routesLibraryArticulation(const KitLib::InstrumentSpec& libraryInstrument, const KitLib::ArticulationSpec* libraryArticulation) const {
        for (const auto& playbackEntry : this->articulationPlaybackEntries) {
            if (playbackEntry.isAliasEntry || playbackEntry.isControllerSibling) continue;
            if (playbackEntry.libraryInstrument != &libraryInstrument) continue;
            if (libraryArticulation == nullptr || playbackEntry.libraryArticulation == libraryArticulation) return true;
        }
        return libraryArticulation == nullptr && this->articulationPlaybackEntries.empty() && this->instrumentSpecstPtr == &libraryInstrument;
    }
    ArtRtIdx getOrAddRuntimeArticulationIdx(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId) {
        for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx)
            if (this->runtimeArticulations[artIdx].nameId == nameId && this->runtimeArticulations[artIdx].exprId == exprId && this->runtimeArticulations[artIdx].routeArticulationSlotNameId == 0 && this->runtimeArticulations[artIdx].routeArticulationSlotExpressionId == 0)
                return static_cast<ArtRtIdx>(artIdx);
        this->runtimeArticulations.emplace_back(nameId, exprId);
        return static_cast<ArtRtIdx>(this->runtimeArticulations.size() - 1);
    }

    void applyAdaptiveTailShortening();
    void rebuildVelocityResponseTables();
    bool refreshLibraryVelocityBoundaries(const KitLib::ArticulationSpec& libraryArticulation);
    bool refreshLibraryVelocityBoundaries(const KitLib::ArticulationSpec& libraryArticulation, const std::vector<uint8_t>& upperBoundaries);
    bool refreshLibraryBalance(const KitLib::InstrumentSpec& libraryInstrument, const KitLib::ArticulationSpec* libraryArticulation = nullptr);
    bool refreshSamplePlaybackOffset(const KitLib::SampleSpec& sampleSpec);
    bool refreshLibraryCalibrationAndReload(const KitLib::ArticulationSpec& libraryArticulation);
    SampledSound::SourceRecipeStatus getSourceRecipeStatus(const KitLib::SampleSpec& sampleSpec) const;

private:
    friend class InstrumentPlayerVoice;
    friend struct InstrumentPlayerTestAccess;

    bool playResolvedArticulation(const int midiChannel, const int midiNoteNumber, ArtRtIdx articulationIdx, const juce::uint8 velocity);
    DrumKitEngine* parent = nullptr;
    KitLib::InstrumentSpec* instrumentSpecstPtr = nullptr;
    std::shared_ptr<const KitLib::LibraryManager> librarySnapshotPtr;
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
    juce::OwnedArray<InstrumentPlayerVoice> voices;
    int activeVoiceCount = 0;
    std::vector<std::shared_ptr<SampledSound>> sampledSoundPtrs;
    int minimumSubBlockSize = 32;
    bool subBlockSubdivisionIsStrict = false;

    juce::AudioBuffer<float> tempBuffer;


    using CcDimensionsMap = std::unordered_map<int8_t, std::vector<std::pair<int8_t, int8_t>>>;

    struct ResolvedArticulationSlotBinding {
        ArticulationSlot articulationSlot;
        ArticulationSlot resolvedArticulationSlot;
        KitLib::InstrumentSpec* libraryInstrument = nullptr;
        KitLib::ArticulationSpec* resolvedSpec = nullptr;
        std::vector<KitLib::ArticulationSpec*> controllerSiblingSpecs;
        bool strictArticulationResolution = false;
        explicit ResolvedArticulationSlotBinding(const ArticulationSlot& originalArticulationSlot)
            : articulationSlot(originalArticulationSlot), resolvedArticulationSlot(originalArticulationSlot)
        {}
    };


    // Runtime optimized tables for fast selection (populated by loadInstrument)
    // Map midi note [0..127] -> articulation index in runtimeArticulations (-1 = none)
    std::array<ArtRtIdx, 128> noteToArticulation;
    std::array<int, 128> hihatNotePedalOpenCloseValues;
    std::unordered_set<uint64_t> articulationSlotRouteKeys;
    std::vector<ArticulationPlaybackEntry> articulationPlaybackEntries;
    // Runtime articulations vector, index-stable after load
    std::vector<ArticulationRuntime> runtimeArticulations;
    // Flat map for O(1) articulation-index lookup by (nameId<<32|exprId) key - rebuilt after every load
    std::unordered_map<uint64_t, int> artIdByNameExprId;
    
    std::unordered_map<uint8_t, std::vector<uint8_t>> noteToControllers;
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
    std::array<std::array<bool, 128>, 16> aftertouchChokeTransitionCommitted {};
    std::array<uint64_t, 128> lastCcProcessedSample {};
    std::array<bool, 128> hasProcessedCcTransition {};
    int hihatPedalNote = -1;
    uint64_t renderedSampleCount = 0;
    std::atomic<uint64_t> processedHiHatTransitions { 0 };
    std::atomic<uint64_t> coalescedHiHatTransitions { 0 };
    std::atomic<uint64_t> skippedHiHatTransitions { 0 };
    std::atomic<uint64_t> completedHiHatTransitions { 0 };
    
    // Owned storage for all SampledSound instances created for this instrument player.
    // (use `sampledSoundPtrs` above which holds shared_ptrs)
    std::vector<int> micOutputMapping;

    // Velocity of the note currently being requested (0..1). Used by custom stealing logic.
    mutable std::atomic<float> lastRequestedVelocity{0.0f};

    int tuningOffsetCents = 0;
    std::optional<int> attackOverrideMs;
    std::optional<int> decayOverrideMs;
    float volumeDb = 0.0f;
    std::atomic<int> pan { 0 };
    bool flipLR = false;
    bool flipPhase = false;
    std::unordered_map<juce::String, float> micBleedLevels;
    std::unordered_map<StringsDictSingleton::Id, float> micBleedLevelsById;

    static uint64_t makeArticulationSlotRouteKey(uint32_t articulationSlotNameId, uint32_t articulationSlotExpressionId) {
        return (static_cast<uint64_t>(articulationSlotNameId) << 32) | static_cast<uint64_t>(articulationSlotExpressionId);
    }
    float getMicBleedLevel(StringsDictSingleton::Id micCodeId) const;
    const LayerParameters* getLayerParameters(ArtRtIdx artIdx) const;
    void applyParameterUpdate(ArticulationRuntime& art, const LayerParameterUpdate& update);
    void refreshSampleMicBleedGains();
    static void configureSoundMicPlacement(
        SampledSound& sound, const KitLib::MicSpec& micSpec, int sourceChannelCount, int destinationChannelCount,
        bool routingAllowsPan, std::optional<int> placementBalance, int layerPan);
    static void updateSoundMicPlacementGains(SampledSound& sound, int layerPan);
    void refreshArticulationMicPlacementGains(ArticulationRuntime& art);
    void storeRoutingContext(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationLibraryMap& articulationLibraryMap, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, bool strictArticulationResolution, const std::unordered_map<juce::String, juce::String>& micRouteOverrides = {});
    void resetRoutingTables();
    void rebuildArtLookupCache();
    bool tryGetRuntimeArticulationIdx(const KitLib::ArticulationSpec* artSpec, bool createIfMissing, ArtRtIdx& artIdx);
    bool tryGetRuntimeArticulationIdx(const ArticulationPlaybackEntry& playbackEntry, bool createIfMissing, ArtRtIdx& artIdx);
    bool playbackEntryUsesArticulationSlotRuntimeKey(const ArticulationPlaybackEntry& playbackEntry) const;
    ArtRtIdx findRuntimeArticulationIndex(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId, StringsDictSingleton::Id articulationSlotNameId, StringsDictSingleton::Id articulationSlotExpressionId) const;
    ArtRtIdx getOrAddRuntimeArticulationIdx(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId, StringsDictSingleton::Id articulationSlotNameId, StringsDictSingleton::Id articulationSlotExpressionId);
    std::vector<ResolvedArticulationSlotBinding> collectResolvedArticulationSlots(const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext);
    void appendArticulationPlaybackEntries(std::vector<ArticulationPlaybackEntry>& playbackEntries, const std::vector<ResolvedArticulationSlotBinding>& bindings);
    void appendAliasPlaybackEntries(std::vector<ArticulationPlaybackEntry>& playbackEntries, const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext);
    void applyArticulationPlaybackEntry(const ArticulationPlaybackEntry& playbackEntry, bool loadSamples, bool createIfMissing, CcDimensionsMap& foundCcDimensions, std::set<juce::String>& skippedMicCodes);
    void applyArticulationPlaybackEntries(bool loadSamples, bool createIfMissing);
    void loadAftertouchChokeTargets(CcDimensionsMap& foundCcDimensions, std::set<juce::String>& skippedMicCodes);
    void applyResolvedRouting(const std::vector<ResolvedArticulationSlotBinding>& bindings, const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext, bool loadSamples, bool createIfMissing);
    std::vector<uint8_t> getEffectiveExpressionValues(const KitLib::ArticulationSpec& artSpec, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) const;
    bool hasEffectiveExpressionValues(const KitLib::ArticulationSpec& artSpec, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) const;
    void refreshRuntimeExpressionValues(const KitLib::ArticulationSpec& artSpec, ArticulationRuntime& artRt);
    void processArticulationSlots(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationLibraryMap& articulationLibraryMap, const std::vector<uint32_t>& allowedArticulationNameIds = {}, const std::vector<juce::String>& allowedArticulationFilters = {}, const ArticulationNameRemap& artNameRemap = {}, const ArticulationExactRemap& artExactRemap = {}, const ArticulationAliasMap& artAliases = {}, bool strictArticulationResolution = false);
    void processArticulationSpec(KitLib::InstrumentSpec* instSpec
        , const ArticulationSlot& articulationSlot
        , KitLib::ArticulationSpec* artSpec
        , ArticulationRuntime& artRt
        , ArtRtIdx artIdx
        , CcDimensionsMap& foundCcDimensions
        , std::set<juce::String>& skippedMicCodes
    );
    void createVelocityLevels(KitLib::InstrumentSpec* instSpec
        , const KitLayout::MidiNotes& notes
        , KitLib::ArticulationSpec* artSpec
        , ArticulationRuntime& artRt
        , ArtRtIdx artIdx
        , MicRoutingBuilder& micRouting
    );
    void buildCcMappings(const ArticulationSlot& articulationSlot, KitLib::ArticulationSpec* artSpec, ArtRtIdx artIdx, CcDimensionsMap& foundCcDimensions);
    void processCcDimensions(CcDimensionsMap& foundCcDimensions);
    int getRuntimeExpressionValue(const ArticulationRuntime& art, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) const;
    ArtRtIdx getVoiceRuntimeArticulationIdx(const InstrumentPlayerVoice& voice) const;
    int getVoicePedalOpenCloseValue(const InstrumentPlayerVoice& voice) const;
    int getHiHatNotePedalOpenCloseValue(int midiNoteNumber) const;
    void rebuildHiHatNoteChokeValues();
    ArtRtIdx selectArticulationForController(ArtRtIdx baseArtIdx, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType, int controllerValue) const;
    bool controllerCanTransitionExistingVoices(int controllerNumber) const;
    bool isHiHatTransitionTargetAudible(const SampledSound& targetSound, double offsetSamples, float expectedTargetDb, float silenceFloorDb) const;
    std::optional<float> findHiHatReferenceOffsetMs(const InstrumentPlayerVoice& sourceVoice, ArtRtIdx sourceArtIdx, const ArticulationRuntime& targetArt, int targetVelocityLevelIdx, uint8_t targetRoundRobinId, const SampledSound& replacementTargetSound, KitLib::MicSpec::Type& referenceMicType) const;

    LogicalHitRuntime* chooseLogicalHit(VelocityLevelRuntime& velocityLevel);
    bool hasReadyLogicalHit(const VelocityLevelRuntime& velocityLevel) const;
    void cullMaskedVoices(const VelocityLevelRuntime& velocityLevel, uint8_t chosenRoundRobinId, int midiChannel, int midiNoteNumber, float newVelocity);
    void processHiHatControllerTransition(int midiChannel, int controllerNumber, int controllerValue);
    void processPendingHiHatControllers();
    bool triggerHiHatPedalSample(int midiChannel, float sourceAudibleDb);
    bool triggerAftertouchChokeSample(int midiChannel, int midiNoteNumber);
    void resetAftertouchChokeGesture(int midiChannel, int midiNoteNumber);
    // Hi-hat only: fade out active voices whose pedalOpenCloseValue is strictly less than newArtPedalOpenCloseValue,
    // implementing the rule: tighter/more-closed articulations choke more-open ringing voices.
    void chokeHiHatVoicesByPoc(int newArtPedalOpenCloseValue, float fadeTimeSec);

    KitLib::ArticulationSpec* resolveArticulationSpec(KitLib::InstrumentSpec* instSpec, const ArticulationSlot& articulationSlot, bool allowFallback);
};
