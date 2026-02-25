#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AftertouchChokeProcessor.h"
#include "KitLibModel.h"
#include "LayoutManager.h"


class DrumKitEngine;

class SampledSound : public juce::SynthesiserSound {
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

    bool appliesToNote(int midiNote) override { return std::find(notes.begin(), notes.end(), static_cast<uint8_t>(midiNote)) != notes.end(); }
    bool appliesToChannel(int /*midiChannel*/) override { return true; }
};


class InstrumentSynthVoice : public juce::SynthesiserVoice {
public:
    InstrumentSynthVoice();
    ~InstrumentSynthVoice() {};

    bool canPlaySound(juce::SynthesiserSound* sound) override { return dynamic_cast<SampledSound*>(sound) != nullptr; }
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int newValue) override;
    void controllerMoved(int controllerNumber, int newValue) override;
    void aftertouchChanged (int newValue);
    void renderNextBlock(juce::AudioBuffer<float>&, int startSample, int numSamples) override;
    using SynthesiserVoice::renderNextBlock;
    AftertouchChokeProcessor chokeProcessor;
private:
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

class InstrumentSynth : public juce::Synthesiser {
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
    
    // Select best SampledSound for given articulation index (index into runtimeArticulations) and velocity
    SampledSound* selectSampleFor(int articulationIdx, int midiVel, int micIdx = -1);
    
    void handleMidiEvent(const juce::MidiMessage& midiMessage) override;
    bool playNote(const int midiChannel, const int midiNoteNumber, const juce::uint8 velocity, int expressions);
    virtual void noteOn(int midiChannel, int midiNoteNumber, float velocity) override;
    // virtual void noteOff(int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override;
    // virtual void handleController (int midiChannel, int controllerNumber, int controllerValue);
    // virtual void handleAftertouch (int midiChannel, int midiNoteNumber, int aftertouchValue)

    DrumKitEngine* getEngine() { return this->parent; }

    void prepareToPlay(double sampleRate, int samplesPerBlock);

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

    // Runtime optimized tables for fast selection (populated by loadInstrument)
    // Map midi note [0..127] -> articulation index in runtimeArticulations (-1 = none)
    std::array<int8_t, 128> noteToArticulation;
    // Runtime articulations vector, index-stable after load
    std::vector<ArticulationRuntime> runtimeArticulations;
    
    std::unordered_map<int8_t, std::vector<uint8_t>> noteToControllers;
    // If this articulation subscribes to controllers -> for each CC id store 128-entry mapping to expression index
    // Stored as map CC -> array[valueIndex for 0..127]
    std::unordered_map<int, std::array<uint8_t, 128>> ccToArticulation;
    
    // Keep a vector of all SampledSound pointers created for this synth (owned by Synth via addSound)
    std::vector<SampledSound*> sampledSoundPtrs;
    std::vector<int> micOutputMapping;

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