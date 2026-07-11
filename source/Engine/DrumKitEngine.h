#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "EngineSettings.h"
#include "InstrumentCatalog.h"
#include "InstrumentSynth.h"
#include "LayoutManager.h"
#include "MidiCrosstalkFilter.h"
#include "MicRoutingPlan.h"
#include "SampleLoader.h"
#include <array>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
        std::optional<int> pan;           // -127 left to +127 right
        bool flipLR = false;
        bool flipLRExplicit = false;
        bool flipPhase = false;
        // Per-mic bleed levels: mic code -> gain (0.0 to 1.0)
        std::unordered_map<juce::String, float> micBleedLevels;
        // Per-source-mic route override: sample mic code -> output mic code.
        std::unordered_map<juce::String, juce::String> micRouteOverrides;
    };

    struct InstrumentMap {
        std::vector<InstrumentMapEntry> entries;
    };

    explicit DrumKitEngine() {
        this->kitLayoutManagerPtr = std::make_unique<KitLayoutManager>();
        this->micLayoutManagerPtr = std::make_unique<MicLayoutManager>();
    }
    ~DrumKitEngine() {
        this->previewResolveThreadPool.removeAllJobs(true, 2000);
    }

    juce::File dataPath;
    const juce::File defaultDataPath { juce::File::getSpecialLocation(juce::File::SpecialLocationType::commonDocumentsDirectory).getChildFile("Karatala") };
    const char* DATA_PATH_ENV = "EDRUMS_DATA_PATH";

    struct Config {
        juce::File libraryPath;
        uint64_t sampleLoaderCacheLimitMegabytes = 1024ull;
        juce::String kitLayout;
        juce::String micLayout;
        DebugLevel logLevel = DebugLevel::Off;
        InstrumentCatalog instrumentCatalog = InstrumentCatalog::makeDefault();
    };

    ParseResult<Config> parseConfigYaml(const juce::String yamlStr);
    juce::String getDefaultYaml();
    juce::String serializeSettingsYaml() const;

    void init(const juce::AudioProcessorValueTreeState& treeState);
    void useState(const juce::AudioProcessorValueTreeState& treeState);
    void saveSettings();

    static MicLayout resolveMicLayoutFromConfig();

    void reloadAll();

    KitLayout* getKitLayout() { return this->activeKitLayoutPtr.get(); }
    MicLayout* getMicLayout() { return this->activeMicLayoutPtr.get(); }
    juce::File getUserPresetsDir() const { return this->dataPath.getChildFile("presets"); }

    bool useKitLayout(juce::String name);
    bool setKitLayoutDirect(KitLayout layout, bool reapplyCurrentMap = true) { return this->setKitLayout(std::move(layout), reapplyCurrentMap); }
    bool useMicLayout(juce::String name);
    bool useInstrumentMap(juce::String text);
    bool applyInstrumentMapSlotChange(const juce::String& fullMapText, const juce::String& changedSlotId);

    static ParseResult<InstrumentMap> parseInstrumentMapText(const juce::String& text);
    static juce::String serializeInstrumentMap(const InstrumentMap& instrumentMap);
    juce::String normalizeInstrumentMapText(const juce::String& text) const;
    juce::String buildInstrumentMapForKit(const KitLayout& layout, const KitLib::DrumKitSpec& kit) const;

    void mapDefaultKit();
    void mapKit(const KitLib::DrumKitSpec& kit);

    const KitLib::LibraryManager* getInstrumentManager() const { return this->libraryManagerPtr.get(); }
    const KitLayoutManager* getKitLayoutManager() const { return this->kitLayoutManagerPtr.get(); }
    const MicLayoutManager* getMicLayoutManager() const { return this->micLayoutManagerPtr.get(); }
    const MicRoutingPlan* getMicRoutingPlan() const { return this->activeMicRoutingPlanPtr.get(); }
    SampleLoader* getSampleLoader() { return this->sampleLoaderPtr.get(); }
    const SampleLoader* getSampleLoader() const { return this->sampleLoaderPtr.get(); }
    const juce::String& getLastInstrumentMapStatus() const { return this->lastInstrumentMapStatus; }
    const juce::String& getSuggestedInstrumentMapText() const { return this->suggestedInstrumentMapText; }
    const juce::String& getGlobalKitLayoutId() const { return this->globalConfig.kitLayout; }
    const juce::String& getGlobalMicLayoutId() const { return this->globalConfig.micLayout; }
    const InstrumentCatalog& getInstrumentCatalog() const { return this->globalConfig.instrumentCatalog; }
    DebugLevel getGlobalLogLevel() const { return this->globalConfig.logLevel; }
    void setGlobalLogLevel(DebugLevel level);
    bool setGlobalKitLayoutId(const juce::String& layoutId);
    bool setGlobalMicLayoutId(const juce::String& layoutId);
    void seedInstrumentMapFingerprints(const juce::String& mapText);

    // Returns true if `id` corresponds to a registered instrument slot.
    // Used by MicRoutingBuilder to mirror applyMicLayout's slot-resolution logic:
    // a mic whose instrument field names a known slot is exclusive to that slot and
    // must not spill into other slots via type fallback.
    bool hasLayoutSlotId(const juce::String& id) const { return this->instrumentSynthById.find(id) != this->instrumentSynthById.end(); }

    const int getLatestContinuousControllerValue(const int ccNum) const {
        if (ccNum < 0 || ccNum > 127) return 0;
        if (!this->trackedCCMask[static_cast<size_t>(ccNum)]) return 0;
        return juce::jlimit(0, 127, this->latestCCValues[static_cast<size_t>(ccNum)]);
    }
    const int getLatestPolyphonicAftertouchValue(const int note) const {
        if (note < 0 || note > 127) return 0;
        if (!this->trackedPAMask[static_cast<size_t>(note)]) return 0;
        return juce::jlimit(0, 127, this->latestPAValues[static_cast<size_t>(note)]);
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
    void rebuildMidiNoteRoutingFromLayout();
    void clearCrosstalkProfile();
    void beginCrosstalkCalibration();
    void cancelCrosstalkCalibration();
    void finishCrosstalkCalibration();
    void setCrosstalkThreshold(int primaryNote, int secondaryNote, int velocityRatio);
    int getCrosstalkProfileEntryCount() const { return this->crosstalkProfile.getEntryCount(); }
    juce::String buildCrosstalkProfileText() const;
    struct LayerParameterUpdate {
        std::optional<int> tuningCents;
        std::optional<int> attackMs;
        std::optional<int> decayMs;
        std::optional<float> volumeDb;
        std::optional<int> pan;
        std::optional<bool> flipLR;
        std::optional<bool> flipPhase;
        std::optional<std::unordered_map<juce::String, float>> micBleedLevels;
    };
    void updateSlotParameters(const juce::String& slotId, const juce::String& artName, int layerIndex, std::optional<int> tuningCents, std::optional<int> attackMs, std::optional<int> decayMs);
    void updateSlotParameters(const juce::String& slotId, const juce::String& artName, int layerIndex, const LayerParameterUpdate& update);
    bool updateSlotSourceParameters(const juce::String& slotId, const juce::String& artName, const juce::String& sourcePath, int sourceOccurrenceIndex, const LayerParameterUpdate& update);
    std::optional<juce::String> getNameForMidiNoteNumber(int note, int midiChannel);
    
    void logMessage(const juce::String& msg, DebugLevel debugLevel) { if (this->maxDebugLevel >= debugLevel) DBG(DebugLevelToString(debugLevel) + ": " + msg); } //FIXME: check is this dedicated only for ui and why cant we just use there LOG(Debug, ...) like everywhere else
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
    MidiCrosstalkProfile crosstalkProfile;
    std::optional<MidiCrosstalkProfile> crosstalkProfileBeforeCalibration;
    MidiCrosstalkFilter crosstalkFilter;

    std::shared_ptr<KitLib::LibraryManager> libraryManagerPtr;
    std::unique_ptr<SampleLoader> sampleLoaderPtr;
    std::unique_ptr<KitLayoutManager> kitLayoutManagerPtr;
    std::unique_ptr<MicLayoutManager> micLayoutManagerPtr;

    std::unique_ptr<KitLayout> activeKitLayoutPtr;
    std::unique_ptr<MicLayout> activeMicLayoutPtr;
    std::unique_ptr<MicRoutingPlan> activeMicRoutingPlanPtr;

    std::array<juce::String, 128> noteNameCache;

    std::unordered_map<StringsDictSingleton::Id, std::vector<StringsDictSingleton::Id>> articulationFallbackIdMap;

    struct PreviewTarget {
        juce::File sampleFile;
        juce::String micCode;
        int fileChannelStart = 0;
        int fileChannelCount = 0;
        float volumeTrimDb = 0.0f;
    };

    class PreviewResolveJob : public juce::ThreadPoolJob {
    public:
        PreviewResolveJob(DrumKitEngine& jobOwner, std::string jobCacheKey, juce::String sourceKitCodeRaw, juce::String sourceInstCode, juce::String sourceArtCode, juce::uint8 targetResolveVelocity, juce::uint8 targetPlaybackVelocity)
            : juce::ThreadPoolJob("Preview Resolve Job")
            , owner(jobOwner)
            , cacheKey(std::move(jobCacheKey))
            , kitCodeRaw(std::move(sourceKitCodeRaw))
            , instCode(std::move(sourceInstCode))
            , artCode(std::move(sourceArtCode))
            , resolveVelocity(targetResolveVelocity)
            , playbackVelocity(targetPlaybackVelocity) {
        }

        JobStatus runJob() override;

    private:
        DrumKitEngine& owner;
        std::string cacheKey;
        juce::String kitCodeRaw;
        juce::String instCode;
        juce::String artCode;
        juce::uint8 resolveVelocity = 0;
        juce::uint8 playbackVelocity = 0;
    };

    // Last known audio block size (samples per block).
    int lastSamplesPerBlock = 0;
    double lastSampleRate = 0.0;
    int primaryInstrumentSynthCount = 0;
    juce::String lastInstrumentMapStatus;
    juce::String suggestedInstrumentMapText;
    juce::String lastAppliedInstrumentMapText;
    std::unordered_map<juce::String, juce::String> lastAppliedSlotFingerprints;
    std::unordered_map<juce::String, juce::String> lastAppliedSlotReloadFingerprints;
    std::unordered_map<InstrumentSynth*, juce::String> lastAppliedShadowFingerprints;
    std::unordered_map<InstrumentSynth*, juce::String> lastAppliedShadowReloadFingerprints;

    void initGlobalSettings();
    void initDefaultLayouts();
    void initArticulationFallbackMap();
    void initKitLayouts();
    void initMicLayouts();
    void initSampleLibrary();

    bool setKitLayout(KitLayout layout, bool reapplyCurrentMap = true);
    bool setMicLayout(MicLayout layout);

    bool applyKitLayout();
    bool applyMicLayout();
    bool applyMicLayoutUnlocked();
    bool useInstrumentMapInternal(juce::String text, const juce::String& changedSlotIdHint);
    void buildNoteNameCache();
    void regenerateSuggestedInstrumentMapText();
    void clearShadowSynths();
    void rebuildPrimaryInstrumentNoteRouting();
    void addShadowSynthNoteRoutes(InstrumentSynth& shadowSynth, const KitLayout::InstrumentSlot& layoutSlot);
    std::vector<int> getTargetNotesForArticulationFilter(const juce::String& slotId, const juce::String& artName) const;
    std::vector<std::pair<uint32_t, uint32_t>> getTargetRoutesForArticulationFilter(const juce::String& slotId, const juce::String& artName) const;
    static bool synthMatchesTargetNotes(const InstrumentSynth& synth, const std::vector<int>& targetNotes);
    static bool synthMatchesTargetRoutes(const InstrumentSynth& synth, const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes, const std::vector<int>& fallbackTargetNotes);
    static bool synthRoutesLayoutArticulation(const InstrumentSynth& synth, const KitLayout::InstrumentSlot::ArticulationTrigger& art);
    static void applyLayerParameterUpdate(InstrumentSynth& synth, const LayerParameterUpdate& update);
    std::string makePreviewTargetCacheKey(const juce::String& kitCodeRaw,
                                          const juce::String& instCode,
                                          const juce::String& artCode,
                                          juce::uint8 velocity) const;
    std::optional<PreviewTarget> resolvePreviewTarget(const juce::String& kitCodeRaw,
                                                      const juce::String& instCode,
                                                      const juce::String& artCode,
                                                      juce::uint8 velocity);
    void resolveAndLoadPreviewTarget(std::string cacheKey,
                                     const juce::String& kitCodeRaw,
                                     const juce::String& instCode,
                                     const juce::String& artCode,
                                     juce::uint8 resolveVelocity,
                                     juce::uint8 playbackVelocity);
    void requestPreviewPlayback(const PreviewTarget& target, juce::uint8 velocity);

    void saveLatestControllerValues(const juce::MidiBuffer& midiMessages);

    // One-shot preview playback (mixed into processBlock output)
    std::shared_ptr<juce::AudioBuffer<float>> previewBuffer;
    double previewSampleRate = 0.0;
    std::atomic<int> previewReadPos { -1 };
    float previewGain = 1.0f;
    juce::SpinLock previewLock;
    juce::CriticalSection previewTargetResolutionLock;
    std::unordered_map<std::string, PreviewTarget> previewTargetCache;
    std::unordered_set<std::string> pendingPreviewTargetKeys;
    juce::ThreadPool previewResolveThreadPool { 1 };
};
