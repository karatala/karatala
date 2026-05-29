#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "EngineSettings.h"
#include "InstrumentSynth.h"
#include "LayoutManager.h"
#include "SampleLoader.h"
#include <array>
#include <optional>
#include <unordered_map>

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
        std::optional<float> volumeDb;    // -60 to +12 dB
        bool flipLR = false;
        bool flipPhase = false;
        // Per-mic bleed levels: mic code -> gain (0.0 to 1.0)
        std::unordered_map<juce::String, float> micBleedLevels;
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
    const juce::File defaultDataPath { juce::File::getSpecialLocation(juce::File::SpecialLocationType::commonDocumentsDirectory).getChildFile("Karatala") };
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
    const juce::String& getSuggestedInstrumentMapText() const { return this->suggestedInstrumentMapText; }
    void seedInstrumentMapFingerprints(const juce::String& mapText);
    juce::String buildReferenceText() const;

    // Returns true if `id` corresponds to a registered instrument slot.
    // Used by MicRoutingBuilder to mirror applyMicLayout's slot-resolution logic:
    // a mic whose instrument field names a known slot is exclusive to that slot and
    // must not spill into other slots via type fallback.
    bool hasLayoutSlotId(const juce::String& id) const { return this->instrumentSynthById.find(id) != this->instrumentSynthById.end(); }

    const int getLatestContinuousControllerValue(const int ccNum) const {
        if (ccNum < 0 || ccNum > 127) return 0;
        if (!this->trackedCCMask[ccNum]) return 0;
        return juce::jlimit(0, 127, this->latestCCValues[ccNum]);
    }
    const int getLatestPolyphonicAftertouchValue(const int note) const {
        if (note < 0 || note > 127) return 0;
        if (!this->trackedPAMask[note]) return 0;
        return juce::jlimit(0, 127, this->latestPAValues[note]);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& outputBuffer, juce::MidiBuffer& midiMessages);
    void playPreviewNote(const juce::String& slotId, const juce::String& artName, juce::uint8 velocity);

    /** Play a one-shot preview of a sample file from the library.
     *  Finds the closest velocity sample for the given kit/instrument/articulation
     *  and plays it directly without affecting the current instrument mapping.
     *  @param kitCodeRaw     kit folder name (e.g. "NakedDrums")
     *  @param instCode       instrument code (e.g. "kick-22")
     *  @param artCode        articulation code (e.g. "center"), empty = first articulation
     *  @param velocity       desired velocity 1-127
     */
    void previewSourceItem(const juce::String& kitCodeRaw, const juce::String& instCode,
                           const juce::String& artCode, juce::uint8 velocity);
    void updateSlotParameters(const juce::String& slotId,
                              const juce::String& artName,
                              int layerIndex,
                              std::optional<int> tuningCents,
                              std::optional<int> attackMs,
                              std::optional<int> decayMs);
    std::optional<juce::String> getNameForMidiNoteNumber(int note, int midiChannel);
    
    void logMessage(const juce::String& msg, DebugLevel debugLevel) { if (this->maxDebugLevel >= debugLevel) DBG(DebugLevelToString(debugLevel) + ": " + msg); }
    DebugLevel maxDebugLevel = DebugLevel::Error;

    EngineSettings engineSettings;
    const EngineSettings& getEngineSettings() const { return this->engineSettings; }

    const std::unordered_map<StringsDictSingleton::Id, std::vector<StringsDictSingleton::Id>>& getArticulationFallbackIdMap() const { return this->articulationFallbackIdMap; }

private:
    Config globalConfig;

    std::vector<std::unique_ptr<InstrumentSynth>> instrumentSynthPtrs;
    std::unordered_map<juce::String, InstrumentSynth*> instrumentSynthById; //TODO: review if this map is actually needed, maybe we can do without it 
    std::unordered_map<int, std::vector<InstrumentSynth*>> instrumentSynthsByNote;

    std::array<int, 128> latestCCValues {};
    std::array<bool, 128> trackedCCMask {};
    std::array<int, 128> latestPAValues {};
    std::array<bool, 128> trackedPAMask {};
    mutable juce::CriticalSection engineGraphLock;

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
    juce::String suggestedInstrumentMapText;
    std::unordered_map<juce::String, juce::String> lastAppliedSlotFingerprints;

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
    void regenerateSuggestedInstrumentMapText();
    void clearShadowSynths();
    void rebuildPrimaryInstrumentNoteRouting();

    void saveLatestControllerValues(const juce::MidiBuffer& midiMessages);

    // One-shot preview playback (mixed into processBlock output)
    std::shared_ptr<juce::AudioBuffer<float>> previewBuffer;
    double previewSampleRate = 0.0;
    std::atomic<int> previewReadPos { -1 };
    float previewGain = 1.0f;
    juce::SpinLock previewLock;
};
