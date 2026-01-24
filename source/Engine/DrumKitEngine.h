#pragma once

#include "DrumKitModel.h"
#include "InstrumentManager.h"
#include "LayoutManager.h"
#include "SampleLoader.h"
#include "SampleSelector.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <unordered_map>
#include <variant>

// Forward declarations
class DrumKitEngine;
class InstrumentSynth;
class InstrumentSynthVoice;

class InstrumentSynthVoice : public juce::SynthesiserVoice {
public:
    InstrumentSynthVoice();
    ~InstrumentSynthVoice() override;

    bool canPlaySound(juce::SynthesiserSound*) override;
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

class SampledSound : public juce::SynthesiserSound {
public:
    DrumKitInstrument* parentInst = nullptr;
    juce::File sampleFile;
    std::shared_ptr<juce::AudioBuffer<float>> buffer;

    double sampleRate = 0.0;
    juce::String articulation;
    int velocityLayer = 0;
    juce_wchar variantLetter = '\0';
    bool enabled = true;

    SampledSound(DrumKitInstrument* p = nullptr, const juce::File& f = {}, const juce::String& art = {}, int vel = 0, juce_wchar ltr = '\0')
        : parentInst(p)
        , sampleFile(f)
        , articulation(art)
        , velocityLayer(vel)
        , variantLetter(ltr)
    {}

    bool appliesToNote(int /*midiNote*/) override { return enabled; }
    bool appliesToChannel(int /*midiChannel*/) override { return true; }
};

class InstrumentSynth : public juce::Synthesiser {
public:
    InstrumentSynth(DrumKitEngine* parent, KitLayout::Instrument instrument
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
    void loadInstrument(DrumKitInstrument* instrument);
    bool isLoaded() { return mDrumKitInstrumentPtr != nullptr; }
    
    SampledSound* selectSampleFor(const juce::String& articulation, int midiVel);
    
    bool playNote(const int midiChannel, const int midiNoteNumber, const juce::uint8 velocity, int expressions);
    void handleMidiEvent(const MidiMessage& midiMessage) override;
    // virtual void noteOn(int midiChannel, int midiNoteNumber, float velocity) override;
    // virtual void noteOff(int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override;
    // virtual void handleController (int midiChannel, int controllerNumber, int controllerValue);
    // virtual void handleAftertouch (int midiChannel, int midiNoteNumber, int aftertouchValue)

    SampleLoader* getSampleLoader();

    void prepareToPlay(double sampleRate, int samplesPerBlock);

    int getMicChannelCount() const { return mMicChannelCount; }

    void setMicChannelCount(int channels) { mMicChannelCount = channels; }
    juce::AudioBuffer<float>& getMicBuffer() { return mMicBuffer; }

    const juce::String& getId() const { return id; }

private:
    DrumKitEngine* parent = nullptr;
    DrumKitInstrument* mDrumKitInstrumentPtr = nullptr;
    juce::String id;
    DrumKitInstrument::Type type;
    juce::AudioBuffer<float> mMicBuffer;
    int mMicChannelCount = 2;
    // Keep a copy of the kit layout instrument definition for note->articulation lookup
    KitLayout::Instrument instrumentDef;

    struct Articulation {
        std::variant<std::monostate, SampleSelector<1>, SampleSelector<2>, SampleSelector<3>> selector;
        // map zone pointer (const void*) -> index into samplesByZone
        std::unordered_map<const void*, size_t> zoneToIndex;
        // samples grouped by selector zone (one vector per zone)
        std::vector<std::vector<SampledSound*>> samplesByZone;
        // per-zone round-robin counters (stored as plain uint32_t to avoid issues with vector reallocation of non-movable atomics)
        std::vector<uint32_t> roundRobinCounters;
        // for HiHat combined instance: list of articulation names in art-index order
        std::vector<juce::String> artNames;

        void clear() {
            selector = std::monostate {};
            zoneToIndex.clear();
            samplesByZone.clear();
            roundRobinCounters.clear();
        }
    };

    std::unordered_map<juce::String, Articulation> mArticulations;
};

class DrumKitEngine {
public:
    DrumKitEngine();
    ~DrumKitEngine();

    juce::File dataPath;
    static const juce::File defaultDataPath;
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

    // Each non-empty, non-comment line should be: <slotId>:<KitName>/<InstrumentCode>
    static std::vector<std::pair<juce::String, juce::String>> parseInstrumentMapText(const juce::String& text);

    void mapDefaultKit();
    void mapKit(DrumKit& kit);

    InstrumentManager* getInstrumentManager() { return mInstrumentManager.get(); }
    KitLayoutManager* getKitLayoutManager() { return mKitLayoutManager.get(); }
    MicLayoutManager* getMicLayoutManager() { return mMicLayoutManager.get(); }
    SampleLoader* getSampleLoader() { return mSampleLoader.get(); }

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& outputBuffer, juce::MidiBuffer& midiMessages);
    // bool preprocessMidi(juce::MidiMessage msg);

private:
    Config mConfig;

    std::vector<std::unique_ptr<InstrumentSynth>> mInstrumentSynthPtrs;
    std::unordered_map<juce::String, InstrumentSynth*> mInstrumentSynthById;
    std::unordered_map<int, std::vector<InstrumentSynth*>> mInstrumentSynthsByNote;

    std::unique_ptr<InstrumentManager> mInstrumentManager;
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
};
