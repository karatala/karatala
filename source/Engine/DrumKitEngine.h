#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "InstrumentSynth.h"
#include "LayoutManager.h"
#include "SampleLoader.h"
#include <array>
#include <optional>

class DrumKitEngine {
public:
    // All output buses are mono; stereo mics use two mono buses with L/R suffixes
    static constexpr int TOTAL_HOST_OUTPUT_CHANNELS = 32;

    struct InstrumentMapEntry {
        juce::String slotId;
        std::vector<juce::String> slotArticulationFilter;
        juce::String kitRef;
        juce::String instCode;
        std::vector<juce::String> articulationFilter;
        std::optional<int> tuningCents;
        std::optional<int> attackTimeMs;
        std::optional<int> decayTimeMs;
    };

    struct InstrumentMap {
        std::vector<InstrumentMapEntry> entries;
    };

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

    static MicLayout resolveMicLayoutFromConfig();

    void reloadAll();

    KitLayout* getKitLayout() { return this->activeKitLayoutPtr.get(); }
    MicLayout* getMicLayout() { return this->activeMicLayoutPtr.get(); }

    bool useKitLayout(juce::String name);
    bool useMicLayout(juce::String name);
    bool useInstrumentMap(juce::String text);

    static ParseResult<InstrumentMap> parseInstrumentMapText(const juce::String& text);

    void mapDefaultKit();
    void mapKit(const KitLib::DrumKitSpec& kit);

    const KitLib::LibraryManager* getInstrumentManager() const { return this->libraryManagerPtr.get(); }
    const KitLayoutManager* getKitLayoutManager() const { return this->kitLayoutManagerPtr.get(); }
    const MicLayoutManager* getMicLayoutManager() const { return this->micLayoutManagerPtr.get(); }
    SampleLoader* getSampleLoader() { return this->sampleLoaderPtr.get(); }
    const SampleLoader* getSampleLoader() const { return this->sampleLoaderPtr.get(); }
    const juce::String& getLastInstrumentMapStatus() const { return this->lastInstrumentMapStatus; }

    // Returns true if `id` corresponds to a registered instrument slot.
    // Used by MicRoutingBuilder to mirror applyMicLayout's slot-resolution logic:
    // a mic whose instrument field names a known slot is exclusive to that slot and
    // must not spill into other slots via type fallback.
    bool hasLayoutSlotId(const juce::String& id) const { return this->instrumentSynthById.find(id) != this->instrumentSynthById.end(); }

    const int getLatestContinuousControllerValue(const int ccNum) const {
        const auto& it = this->latestCCValueMap.find(ccNum);
        if (it == this->latestCCValueMap.end()) return 0; //FIXME?
        const int& value = it->second;
        if (value < 0) return 0;
        if (value > 127) return 127;
        return value;
    }
    const int getLatestPolyphonicAftertouchValue(const int note) const {
        const auto& it = this->latestPAValueMap.find(note);
        if (it == this->latestPAValueMap.end()) return 0; //FIXME?
        const int& value = it->second;
        if (value < 0) return 0;
        if (value > 127) return 127;
        return value;
    }
    // Realtime-safe accessor: get latest CC values by const-ref (no copy) //FIXME: check the codebase and make it use similar 'no copy' pattern whereever possible
    const std::unordered_map<int, int>& getLatestContinuousControllerValuesRef() const { return this->latestCCValueMap; } //TODO: delete, probably will be unused
    const std::unordered_map<int, int>& getLatestPolyphonicAftertouchValuesRef() const { return this->latestPAValueMap; } //TODO: delete, probably will be unused

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& outputBuffer, juce::MidiBuffer& midiMessages);
    std::optional<juce::String> getNameForMidiNoteNumber(int note, int midiChannel);
    
    void logMessage(const juce::String& msg, DebugLevel debugLevel) { if (this->maxDebugLevel >= debugLevel) DBG(DebugLevelToString(debugLevel) + ": " + msg); }
    DebugLevel maxDebugLevel = DebugLevel::Error;

    const std::unordered_map<StringsDictSingleton::Id, std::vector<StringsDictSingleton::Id>>& getArticulationFallbackIdMap() const { return this->articulationFallbackIdMap; }

private:
    Config globalConfig;

    std::vector<std::unique_ptr<InstrumentSynth>> instrumentSynthPtrs;
    std::unordered_map<juce::String, InstrumentSynth*> instrumentSynthById; //TODO: review if this map is actually needed, maybe we can do without it 
    std::unordered_map<int, std::vector<InstrumentSynth*>> instrumentSynthsByNote;

    //TODO: maybe no need in unordered_map and make it std::array<int, 128> or what will be faster/lighter/safer?
    std::unordered_map<int, int> latestCCValueMap;
    std::unordered_map<int, int> latestPAValueMap;

    std::unique_ptr<KitLib::LibraryManager> libraryManagerPtr;
    std::unique_ptr<SampleLoader> sampleLoaderPtr;
    std::unique_ptr<KitLayoutManager> kitLayoutManagerPtr;
    std::unique_ptr<MicLayoutManager> micLayoutManagerPtr;

    std::unique_ptr<KitLayout> activeKitLayoutPtr;
    std::unique_ptr<MicLayout> activeMicLayoutPtr;

    std::array<juce::String, 128> noteNameCache;

    std::unordered_map<StringsDictSingleton::Id, std::vector<StringsDictSingleton::Id>> articulationFallbackIdMap;

    // Last known audio block size (samples per block).
    int lastSamplesPerBlock = 0;
    double lastSampleRate = 0.0;
    int primaryInstrumentSynthCount = 0;
    juce::String lastInstrumentMapStatus;

    void initGlobalSettings();
    void initDefaultLayouts();
    void initArticulationFallbackMap();
    void initKitLayouts();
    void initMicLayouts();
    void initSampleLibrary();

    bool setKitLayout(KitLayout layout);
    bool setMicLayout(MicLayout layout);

    bool applyKitLayout();
    bool applyMicLayout();
    void buildNoteNameCache();
    void clearShadowSynths();
    void rebuildPrimaryInstrumentNoteRouting();

    void saveLatestContinuousControllerValues(const juce::MidiBuffer& midiMessages);
    void saveLatestPolyphonicAftertouchValues(const juce::MidiBuffer& midiMessages);
};
