#pragma once

#include "../Config/Helpers.h"
#include "KitLibModel.h"
#include "LayoutManager.h"
#include "SampleLoader.h"
#include "SampleSelector.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <unordered_map>
#include <array>
#include <atomic>
#include <memory>
#include <variant>


// class KitEngine; // was DrumKitEngine
// class SlotSynth; // was InstrumentSynth
// class SlotVoice; // was InstrumentSynthVoice
// class SampleSound;


// Forward declarations
class DrumKitEngine;
class InstrumentSynth;
class InstrumentSynthVoice;
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
    void renderNextBlock(juce::AudioBuffer<float>&, int startSample, int numSamples) override;
    using SynthesiserVoice::renderNextBlock;

private:
    juce::RangedAudioParameter* mGainParameter = nullptr;
    juce::RangedAudioParameter* mPanParameter = nullptr;
    juce::AudioParameterBool* mInvertPhaseParameter = nullptr;

    double mPitchRatio = 0;
    float mVelocityGain = 0;
    double mSourceSamplePosition = 0;
    juce::ADSR mAdsr;

    JUCE_LEAK_DETECTOR(InstrumentSynthVoice)
};

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
        juce::String codeRaw; // used only outside audio thread
        // For each incoming MIDI velocity (0..127) map to a preferred velocity layer index
        std::array<int8_t, 128> velocityToLayer; // -1 means no layer
        std::vector<VelocityLayerRuntime> layers;

        ArticulationRuntime() {
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

    SampleLoader* getSampleLoader();

    void prepareToPlay(double sampleRate, int samplesPerBlock);

    int getMicChannelCount() const { return mMicChannelCount; }

    void setMicChannelCount(int channels) { mMicChannelCount = channels; }
    juce::AudioBuffer<float>& getMicBuffer() { return micBuffer; }

    // Per-instrument mapping: mic buffer channel index -> host output channel index
    void setMicOutputMapping(const std::vector<int>& mapping) { this->micOutputMapping = mapping; }
    const std::vector<int>& getMicOutputMapping() const { return this->micOutputMapping; }

    KitLib::InstrumentSpec::Type getType() const { return this->type; }

    const juce::String& getId() const { return id; }

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
};

class DrumKitEngine {
public:
    explicit DrumKitEngine() {
        this->kitLayoutManagerPtr = std::make_unique<KitLayoutManager>();
        this->micLayoutManagerPtr = std::make_unique<MicLayoutManager>();
    }
    ~DrumKitEngine() {};

    juce::File dataPath;
    const juce::File defaultDataPath { juce::File::getSpecialLocation(juce::File::SpecialLocationType::commonDocumentsDirectory).getChildFile(PRODUCT_NAME_WITHOUT_VERSION) };
    const char* DATA_PATH_ENV = "EDRUMS_DATA_PATH";

    struct Config {
        juce::File libraryPath;
        uint64_t sampleLoaderCacheLimitMegabytes = 1024ull;
        juce::String kitLayout;
        juce::String micLayout;
    };

    ParseResult<Config> parseConfigYaml(const juce::String yamlStr);
    juce::String getDefaultYaml();

    void init(const juce::AudioProcessorValueTreeState& treeState);
    void useState(const juce::AudioProcessorValueTreeState& treeState);
    void saveSettings();

    void reloadAll();

    KitLayout* getKitLayout() { return this->activeKitLayoutPtr.get(); }
    MicLayout* getMicLayout() { return this->activeMicLayoutPtr.get(); }

    bool useKitLayout(juce::String name);
    bool useMicLayout(juce::String name);
    bool useInstrumentMap(juce::String text);

    static std::vector<std::pair<juce::String, juce::String>> parseInstrumentMapText(const juce::String& text);

    void mapDefaultKit();
    void mapKit(KitLib::DrumKitSpec& kit);

    KitLib::LibraryManager* getInstrumentManager() { return this->libraryManagerPtr.get(); }
    KitLayoutManager* getKitLayoutManager() { return this->kitLayoutManagerPtr.get(); }
    MicLayoutManager* getMicLayoutManager() { return this->micLayoutManagerPtr.get(); }
    SampleLoader* getSampleLoader() { return this->sampleLoaderPtr.get(); }

    // Realtime-safe accessor: get latest CC values by const-ref (no copy)
    const std::unordered_map<int, int>& getLatestContinuousControllerValuesRef() const { return this->latestCcValueMap; }

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& outputBuffer, juce::MidiBuffer& midiMessages);
    // bool preprocessMidi(juce::MidiMessage msg);
        
    void logMessage(const juce::String& msg, DebugLevel debugLevel) { if (this->maxDebugLevel >= debugLevel) DBG(DebugLevelToString(debugLevel) + ": " + msg); }
    DebugLevel maxDebugLevel = DebugLevel::Error;

private:
    Config globalConfig;

    std::vector<std::unique_ptr<InstrumentSynth>> instrumentSynthPtrs;
    std::unordered_map<juce::String, InstrumentSynth*> instrumentSynthById;
    std::unordered_map<int, std::vector<InstrumentSynth*>> instrumentSynthsByNote;

    std::unordered_map<int, int> latestCcValueMap;

    std::unique_ptr<KitLib::LibraryManager> libraryManagerPtr;
    std::unique_ptr<SampleLoader> sampleLoaderPtr;
    std::unique_ptr<KitLayoutManager> kitLayoutManagerPtr;
    std::unique_ptr<MicLayoutManager> micLayoutManagerPtr;

    std::unique_ptr<KitLayout> activeKitLayoutPtr;
    std::unique_ptr<MicLayout> activeMicLayoutPtr;

    // Last known audio block size (samples per block). Used to size mic buffers when applying layouts.
    int lastSamplesPerBlock = 0;

    void initGlobalSettings();
    void initDefaultLayouts();
    void initKitLayouts();
    void initMicLayouts();
    void initSampleLibrary();

    bool setKitLayout(KitLayout layout);
    bool setMicLayout(MicLayout layout);

    bool applyKitLayout();
    bool applyMicLayout();


    std::unordered_map<int, int> getLatestContinuousControllerValues() { return this->latestCcValueMap; }
    void saveLatestContinuousControllerValues(const juce::MidiBuffer& midiMessages);
};
