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
    KitLib::InstrumentSpecs* instrumentSpec = nullptr;
    KitLib::SampleVariantSpecs* sampleVariantSpec = nullptr;
    juce::File sampleFile;
    std::shared_ptr<juce::AudioBuffer<float>> buffer;
    std::vector<uint8_t> notes; 

    double sampleRate = 0.0;

    SampledSound(KitLib::InstrumentSpecs* instrumentSpec = nullptr, KitLib::SampleVariantSpecs* sampleVariantSpec = nullptr, const juce::File& sampleFile = {}, std::vector<uint8_t> notes = {})
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

    InstrumentSynth(DrumKitEngine* parent, KitLayout::InstrumentSlot instrument
        //DUNNO: add params to instrument or do it somewhere else?
        // ,juce::RangedAudioParameter& gainParameter
        // ,juce::RangedAudioParameter& panParameter
        // ,juce::AudioParameterBool& phaseParameter
        )
        : parent(parent)
        , instrumentDef(instrument)
        , id(instrument.id)
        , type(instrument.type)
    {
        // this->instrumentDef.controllers
    }
    ~InstrumentSynth() {}

    void unloadInstrument();
    void loadInstrument(KitLib::InstrumentSpecs* kitInstrument);
    bool isLoaded() { return instrumentSpecstPtr != nullptr; }
    
    // Select best SampledSound for given articulation index (index into runtimeArticulations) and velocity
    SampledSound* selectSampleFor(int articulationIdx, int midiVel, int micIdx = -1);
    
    void handleMidiEvent(const MidiMessage& midiMessage) override;
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

    const juce::String& getId() const { return id; }

private:
    DrumKitEngine* parent = nullptr;
    KitLib::InstrumentSpecs* instrumentSpecstPtr = nullptr;
    juce::String id;
    KitLib::InstrumentSpecs::Type type;
    juce::AudioBuffer<float> micBuffer;
    int mMicChannelCount = 2;
    // Keep a copy of the kit layout instrument definition for note->articulation lookup
    KitLayout::InstrumentSlot instrumentDef;

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
};

class DrumKitEngine {
public:
    explicit DrumKitEngine() {
        this->mKitLayoutManager = std::make_unique<KitLayoutManager>();
        this->mMicLayoutManager = std::make_unique<MicLayoutManager>();
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

    KitLayout* getKitLayout() { return mKitLayout.get(); }
    MicLayout* getMicLayout() { return mMicLayout.get(); }

    bool useKitLayout(juce::String name);
    bool useMicLayout(juce::String name);
    bool useInstrumentMap(juce::String text);

    static std::vector<std::pair<juce::String, juce::String>> parseInstrumentMapText(const juce::String& text);

    void mapDefaultKit();
    void mapKit(KitLib::DrumKitSpecs& kit);

    KitLib::LibraryManager* getInstrumentManager() { return mLibraryManager.get(); }
    KitLayoutManager* getKitLayoutManager() { return mKitLayoutManager.get(); }
    MicLayoutManager* getMicLayoutManager() { return mMicLayoutManager.get(); }
    SampleLoader* getSampleLoader() { return mSampleLoader.get(); }

    // Realtime-safe accessor: get latest CC values by const-ref (no copy)
    const std::unordered_map<int, int>& getLatestContinuousControllerValuesRef() const { return this->mContinuousControllerValues; }

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& outputBuffer, juce::MidiBuffer& midiMessages);
    // bool preprocessMidi(juce::MidiMessage msg);
        
    void logMessage(const juce::String& msg, DebugLevel debugLevel) { if (mDebugLevel >= debugLevel) DBG(DebugLevelToString(debugLevel) + ": " + msg); }
    DebugLevel mDebugLevel = DebugLevel::Error;

private:
    Config mConfig;

    std::vector<std::unique_ptr<InstrumentSynth>> mInstrumentSynthPtrs;
    std::unordered_map<juce::String, InstrumentSynth*> mInstrumentSynthById;
    std::unordered_map<int, std::vector<InstrumentSynth*>> mInstrumentSynthsByNote;

    std::unordered_map<int, int> mContinuousControllerValues;

    std::unique_ptr<KitLib::LibraryManager> mLibraryManager;
    std::unique_ptr<SampleLoader> mSampleLoader;
    std::unique_ptr<KitLayoutManager> mKitLayoutManager;
    std::unique_ptr<MicLayoutManager> mMicLayoutManager;

    std::unique_ptr<KitLayout> mKitLayout;
    std::unique_ptr<MicLayout> mMicLayout;

    void initGlobalSettings();
    void initDefaultLayouts();
    void initKitLayouts();
    void initMicLayouts();
    void initSampleLibrary();

    bool setKitLayout(KitLayout layout);
    bool setMicLayout(MicLayout layout);

    bool applyKitLayout();
    bool applyMicLayout();


    std::unordered_map<int, int> getLatestContinuousControllerValues() { return this->mContinuousControllerValues; }
    void saveLatestContinuousControllerValues(const juce::MidiBuffer& midiMessages);
};
