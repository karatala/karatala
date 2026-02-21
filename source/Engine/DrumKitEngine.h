#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "InstrumentSynth.h"
#include "LayoutManager.h"
#include "SampleLoader.h"

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
