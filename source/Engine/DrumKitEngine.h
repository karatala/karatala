#pragma once

#include "../Config/LogLevel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "EngineSettings.h"
#include "ArticulationSlotFallbackRules.h"
#include "InstrumentCatalog.h"
#include "InstrumentMap/InstrumentMap.h"
#include "InstrumentPlayer.h"
#include "LayoutManager.h"
#include "MidiCrosstalkFilter.h"
#include "MicRoutingPlan.h"
#include "SampleLoader.h"
#include "VelocityResponse.h"
#include <array>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

struct DrumKitEngineTestAccess;

class DrumKitEngine {
public:
    // All output buses are mono; stereo mics use two mono buses with L/R suffixes
    static constexpr int TOTAL_HOST_OUTPUT_CHANNELS = 32;

    using InstrumentMapLayer = ::InstrumentMap::Layer;
    using InstrumentMap = ::InstrumentMap::Map;

    struct VelocityResponsePreset {
        juce::String name;
        juce::String definition;
        juce::File file;
        bool isUser = false;
    };

    struct VelocityResponseLevelInfo {
        int velocity = 0;
        std::optional<float> rawMedianRelativeDb;
        std::optional<float> fittedRelativeDb;
        std::optional<float> calibrationTrimDb;
        bool calibrationLimited = false;
    };

    struct LibraryVelocityResponseInfo {
        bool libraryArticulationFound = false;
        std::vector<VelocityResponseLevelInfo> levels;
    };

    struct LibraryVelocityLevelInfo {
        int namedVelocity = 0;
        int filenameUpperBoundary = 0;
        int effectiveUpperBoundary = 0;
        bool overridden = false;
    };

    struct PlacementBalanceInfo {
        std::optional<int> filenameValue;
        std::optional<int> overrideValue;
        std::optional<int> effectiveValue;
        bool filenameConflict = false;
    };

    struct LibrarySampleInfo {
        int namedVelocity = 0;
        juce::String roundRobin;
        juce::String relativePath;
        juce::String fileName;
        juce::String micCode;
        juce::String contentHash;
        juce::String overrideStoredRelativePath;
        bool overrideMatchedByContentHash = false;
        bool overrideContentHashAmbiguous = false;
        float filenameManualGainDb = 0.0f;
        std::optional<float> manualGainDbOverride;
        float effectiveManualGainDb = 0.0f;
        float filenamePlaybackOffsetMs = 0.0f;
        std::optional<float> filePlaybackOffsetMsOverride;
        std::optional<float> micPlaybackOffsetMsOverride;
        float effectivePlaybackOffsetMs = 0.0f;
        bool hasMetadata = false;
        std::optional<float> onsetMs;
        std::optional<float> intensityDb;
        std::optional<float> truePeakDb;
        SampledSound::SourceRecipeStatus recipeStatus = SampledSound::SourceRecipeStatus::NotLoaded;
    };

    struct LibraryArticulationInfo {
        bool libraryArticulationFound = false;
        bool writable = false;
        juce::String kitCodeRaw;
        juce::String instrumentCodeRaw;
        juce::String articulationCodeRaw;
        PlacementBalanceInfo instrumentBalance;
        PlacementBalanceInfo articulationBalance;
        std::vector<LibraryVelocityLevelInfo> levels;
        std::vector<LibrarySampleInfo> samples;
    };

    struct SamplePlaybackOffsetTarget {
        juce::String relativePath;
        std::optional<juce::String> micCode;
    };

    struct LibraryPreviewLayer {
        juce::String kitId;
        juce::String instrumentId;
        juce::String articulationId;
        float gainDb = 0.0f;
    };

    struct VelocityUpperBoundaryTarget {
        int namedVelocity = 0;
        int upperBoundary = 0;
    };

    explicit DrumKitEngine();
    ~DrumKitEngine();

    juce::File dataPath;
    const juce::File defaultDataPath { juce::File::getSpecialLocation(juce::File::SpecialLocationType::commonDocumentsDirectory).getChildFile("Karatala") };
    const char* DATA_PATH_ENV = "EDRUMS_DATA_PATH";

    struct Config {
        juce::File libraryPath;
        uint64_t sampleLoaderCacheLimitMegabytes = 1024ull;
        juce::String kitLayout;
        juce::String micLayout;
        juce::String velocityResponse { "linear" };
        DebugLevel logLevel = DebugLevel::Off;
        InstrumentCatalog instrumentCatalog = InstrumentCatalog::makeDefault();
        ArticulationSlotFallbackRules articulationSlotFallbackRules = ArticulationSlotFallbackRules::makeDefault();
    };

    ParseResult<Config> parseConfigYaml(const juce::String yamlStr);
    juce::String getDefaultYaml();
    juce::String serializeSettingsYaml() const;

    void init(juce::AudioProcessorValueTreeState& treeState);
    void useState(const juce::AudioProcessorValueTreeState& treeState);
    void saveSettings();

    static MicLayout resolveMicLayoutFromConfig();

    void reloadAll();

    KitLayout* getKitLayout() { return this->activeKitLayoutPtr.get(); }
    MicLayout* getMicLayout() { return this->activeMicLayoutPtr.get(); }
    juce::File getUserPresetsDir() const { return this->dataPath.getChildFile("presets"); }
    juce::File getUserVelocityResponsePresetsDir() const { return this->dataPath.getChildFile("velocity_response_presets"); }
    std::vector<VelocityResponsePreset> getVelocityResponsePresets(juce::StringArray* warnings = nullptr) const;
    ParseResult<VelocityResponsePreset> saveVelocityResponsePreset(const juce::String& name, const juce::String& definition, bool replaceExisting) const;
    LibraryVelocityResponseInfo getLibraryVelocityResponseInfo(const juce::String& qualifiedLibraryArticulationId) const;
    LibraryArticulationInfo getLibraryArticulationInfo(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId) const;
    ParseResult<LibraryArticulationInfo> setLibraryVelocityUpperBoundary(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, int namedVelocity, std::optional<int> upperBoundary, bool persist = true);
    ParseResult<LibraryArticulationInfo> setLibraryVelocityUpperBoundariesBatch(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const std::vector<VelocityUpperBoundaryTarget>& targets);
    ParseResult<LibraryArticulationInfo> resetLibraryVelocityUpperBoundaries(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId);
    ParseResult<LibraryArticulationInfo> setLibraryInstrumentBalance(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, std::optional<int> balance);
    ParseResult<LibraryArticulationInfo> setLibraryArticulationBalance(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, std::optional<int> balance);
    ParseResult<LibraryArticulationInfo> setSampleManualGain(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const juce::String& relativePath, std::optional<float> manualGainDb);
    ParseResult<LibraryArticulationInfo> setSampleManualGainBatch(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const std::vector<juce::String>& relativePaths, std::optional<float> manualGainDb);
    ParseResult<LibraryArticulationInfo> setSamplePlaybackOffset(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const juce::String& relativePath, const std::optional<juce::String>& micCode, std::optional<float> playbackOffsetMs);
    ParseResult<LibraryArticulationInfo> setSamplePlaybackOffsetBatch(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const std::vector<SamplePlaybackOffsetTarget>& targets, std::optional<float> playbackOffsetMs);
    ParseResult<LibraryArticulationInfo> reloadLibraryKitOverrides(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId);

    bool useKitLayout(juce::String name);
    bool setKitLayoutDirect(KitLayout layout, bool reapplyCurrentMap = true) { return this->setKitLayout(std::move(layout), reapplyCurrentMap); }
    bool useMicLayout(juce::String name);
    bool useInstrumentMap(juce::String text);
    bool applyInstrumentMapSlotChange(const juce::String& fullMapText, const juce::String& changedSlotId);

    static ParseResult<InstrumentMap> parseInstrumentMapText(const juce::String& text);
    static juce::String serializeInstrumentMap(const InstrumentMap& instrumentMap);
    juce::String normalizeInstrumentMapText(const juce::String& text) const;
    juce::String buildInstrumentMapForKit(const KitLayout& layout, const KitLib::DrumKitSpec& libraryKit, juce::StringArray* warnings = nullptr) const;
    ParseResult<juce::String> buildInstrumentMapWithSlotInstrument(const juce::String& currentMapText, const juce::String& slotId, const juce::String& libraryKitId, const juce::String& libraryInstrumentId) const;

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
    const juce::File& getGlobalLibraryPath() const { return this->globalConfig.libraryPath; }
    uint64_t getGlobalSampleLoaderCacheLimitMegabytes() const { return this->globalConfig.sampleLoaderCacheLimitMegabytes; }
    const InstrumentCatalog& getInstrumentCatalog() const { return this->globalConfig.instrumentCatalog; }
    const ArticulationSlotFallbackRules& getArticulationSlotFallbackRules() const { return this->globalConfig.articulationSlotFallbackRules; }
    DebugLevel getGlobalLogLevel() const { return this->globalConfig.logLevel; }
    void setGlobalLogLevel(DebugLevel level);
    bool setGlobalLibraryPath(const juce::File& libraryPath);
    void setGlobalSampleLoaderCacheLimitMegabytes(uint64_t limitMegabytes);
    bool setGlobalKitLayoutId(const juce::String& layoutId);
    bool setGlobalMicLayoutId(const juce::String& layoutId);

    // Returns true if `id` corresponds to a registered instrument slot.
    // Used by MicRoutingBuilder to mirror applyMicLayout's slot-resolution logic:
    // a mic whose instrument field names a known slot is exclusive to that slot and
    // must not spill into other slots via type fallback.
    bool hasLayoutSlotId(const juce::String& id) const { return this->instrumentPlayerById.find(id) != this->instrumentPlayerById.end(); }

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
    void playLayerPreview(const juce::String& slotId, const juce::String& artName, int layerIndex, juce::uint8 velocity);
    void playPreviewArticulation(const juce::String& slotId, const juce::String& articulationCode, juce::uint8 velocity);
    bool playLibraryLogicalHitPreview(const juce::String& slotId, const juce::String& articulationSlot, const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, int namedVelocity, const juce::String& roundRobin, juce::uint8 playbackVelocity);

    /** Play a one-shot preview of a sample file from the library.
     *  Finds the closest velocity sample for the given kit/instrument/articulation
     *  and plays it directly without affecting the current instrument mapping.
     *  @param kitCodeRaw     kit folder name (e.g. "NakedDrums")
     *  @param instCode       instrument code (e.g. "kick-22")
     *  @param artCode        articulation code (e.g. "center"), empty = first articulation
     *  @param velocity       desired velocity 1-127
     */
    void previewLibraryArticulation(const juce::String& kitCodeRaw, const juce::String& instCode, const juce::String& artCode, juce::uint8 velocity, const juce::String& relativePath = {}, const juce::String& micCode = {});
    void previewLibraryArticulationGroup(const std::vector<LibraryPreviewLayer>& layers, juce::uint8 velocity);
    void rebuildMidiNoteRoutingFromLayout();
    void clearCrosstalkProfile();
    void beginCrosstalkCalibration();
    void cancelCrosstalkCalibration();
    void finishCrosstalkCalibration();
    void setCrosstalkThreshold(int primaryNote, int secondaryNote, int velocityRatio);
    int getCrosstalkProfileEntryCount() const { return this->crosstalkProfile.getEntryCount(); }
    juce::String buildCrosstalkProfileText() const;
    using LayerParameterUpdate = InstrumentPlayer::LayerParameterUpdate;
    struct LayerParameterTarget {
        juce::String slotId;
        juce::String articulationName;
        int layerIndex = -1;
        LayerParameterUpdate update;
    };
    struct MicRouteTarget {
        juce::String slotId;
        juce::String articulationName;
        int layerIndex = -1;
        juce::String micCode;
        juce::String outputId;
    };
    void updateSlotParameters(const juce::String& slotId, const juce::String& artName, int layerIndex, std::optional<int> tuningCents, std::optional<int> attackMs, std::optional<int> decayMs);
    void updateSlotParameters(const juce::String& slotId, const juce::String& artName, int layerIndex, const LayerParameterUpdate& update);
    bool updateSlotParametersBatch(const std::vector<LayerParameterTarget>& targets);
    bool updateMappedLibraryArticulationParameters(const juce::String& slotId, const juce::String& artName, const juce::String& qualifiedLibraryArticulationId, int libraryArticulationOccurrenceIndex, const LayerParameterUpdate& update);
    bool updateSlotMicRoutesBatch(const std::vector<MicRouteTarget>& targets);
    std::optional<juce::String> getNameForMidiNoteNumber(int note, int midiChannel);

    void logMessage(const juce::String& msg, DebugLevel debugLevel);
    DebugLevel maxDebugLevel = DebugLevel::Error;

    EngineSettings engineSettings;
    const EngineSettings& getEngineSettings() const { return this->engineSettings; }
    const VelocityResponse& getVelocityResponse() const { return this->velocityResponse; }
    bool setVelocityResponseDefinition(const juce::String& definition);

    const std::unordered_map<StringsDictSingleton::Id, std::vector<StringsDictSingleton::Id>>& getArticulationFallbackIdMap() const { return this->articulationFallbackIdMap; }

private:
    friend struct DrumKitEngineTestAccess;
    friend class InstrumentPlayer;

    Config globalConfig;
    VelocityResponse velocityResponse = VelocityResponse::linear();

    std::vector<std::unique_ptr<InstrumentPlayer>> instrumentPlayerPtrs;
    std::unordered_map<juce::String, InstrumentPlayer*> instrumentPlayerById; //TODO: review if this map is actually needed, maybe we can do without it
    std::unordered_map<int, std::vector<InstrumentPlayer*>> instrumentPlayersByNote;

    std::array<int, 128> latestCCValues {};
    std::array<bool, 128> trackedCCMask {};
    std::array<int, 128> latestPAValues {};
    std::array<bool, 128> trackedPAMask {};
    mutable juce::CriticalSection engineGraphLock;
    mutable juce::CriticalSection libraryModelLock;
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
        juce::String contentHash;
        juce::int64 metadataFileSizeBytes = 0;
        juce::String micCode;
        int fileChannelStart = 0;
        int fileChannelCount = 0;
        float fixedGainDb = 0.0f;
        float playbackOffsetMs = 0.0f;
    };

    struct LoadedPreviewLayer {
        std::shared_ptr<juce::AudioBuffer<float>> buffer;
        double sampleRate = 0.0;
        float gain = 1.0f;
    };

    struct PreviewGroupLoadState {
        juce::CriticalSection lock;
        std::vector<LoadedPreviewLayer> loadedLayers;
        int remainingLoads = 0;
        float playbackGain = 1.0f;
        uint64_t generation = 0;
    };

    class PreviewResolveJob : public juce::ThreadPoolJob {
    public:
        PreviewResolveJob(DrumKitEngine& jobOwner, std::string jobCacheKey, juce::String newKitCodeRaw, juce::String newInstCode, juce::String newArtCode, juce::String newRelativePath, juce::String newMicCode, juce::uint8 targetResolveVelocity, juce::uint8 targetPlaybackVelocity, uint64_t targetGeneration)
            : juce::ThreadPoolJob("Preview Resolve Job")
            , owner(jobOwner)
            , cacheKey(std::move(jobCacheKey))
            , kitCodeRaw(std::move(newKitCodeRaw))
            , instCode(std::move(newInstCode))
            , artCode(std::move(newArtCode))
            , relativePath(std::move(newRelativePath))
            , micCode(std::move(newMicCode))
            , resolveVelocity(targetResolveVelocity)
            , playbackVelocity(targetPlaybackVelocity)
            , generation(targetGeneration) {
        }

        JobStatus runJob() override;

    private:
        DrumKitEngine& owner;
        std::string cacheKey;
        juce::String kitCodeRaw;
        juce::String instCode;
        juce::String artCode;
        juce::String relativePath;
        juce::String micCode;
        juce::uint8 resolveVelocity = 0;
        juce::uint8 playbackVelocity = 0;
        uint64_t generation = 0;
    };

    class PreviewGroupResolveJob : public juce::ThreadPoolJob {
    public:
        PreviewGroupResolveJob(DrumKitEngine& jobOwner, std::vector<LibraryPreviewLayer> newLayers, juce::uint8 newVelocity, float newPlaybackGain, uint64_t newGeneration)
            : juce::ThreadPoolJob("Preview Group Resolve Job"), owner(jobOwner), layers(std::move(newLayers)), velocity(newVelocity), playbackGain(newPlaybackGain), generation(newGeneration) {}

        JobStatus runJob() override;

    private:
        DrumKitEngine& owner;
        std::vector<LibraryPreviewLayer> layers;
        juce::uint8 velocity = 0;
        float playbackGain = 1.0f;
        uint64_t generation = 0;
    };

    // Last known audio block size (samples per block).
    int lastSamplesPerBlock = 0;
    double lastSampleRate = 0.0;
    int primaryInstrumentPlayerCount = 0;
    juce::String lastInstrumentMapStatus;
    juce::String suggestedInstrumentMapText;
    juce::String lastAppliedInstrumentMapText;
    std::unordered_map<juce::String, juce::String> lastAppliedSlotFingerprints;
    std::unordered_map<juce::String, juce::String> lastAppliedSlotReloadFingerprints;
    std::unordered_map<InstrumentPlayer*, juce::String> lastAppliedShadowFingerprints;
    std::unordered_map<InstrumentPlayer*, juce::String> lastAppliedShadowReloadFingerprints;

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
    struct InstrumentMapApplyPlan;
    struct InstrumentMapCommitResult;
    bool useInstrumentMapInternal(juce::String text, const juce::String& changedSlotIdHint);
    InstrumentMapCommitResult commitInstrumentMapApply(InstrumentMapApplyPlan& plan);
    void buildNoteNameCache();
    void regenerateSuggestedInstrumentMapText();
    void clearShadowPlayers();
    void rebuildPrimaryInstrumentNoteRouting();
    void addShadowPlayerNoteRoutes(InstrumentPlayer& shadowPlayer, const KitLayout::InstrumentSlot& layoutSlot);
    std::vector<std::pair<uint32_t, uint32_t>> getTargetRoutesForArticulationFilter(const juce::String& slotId, const juce::String& artName) const;
    static bool playerMatchesTargetRoutes(const InstrumentPlayer& instrumentPlayer, const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes);
    static bool playerRoutesArticulationSlot(const InstrumentPlayer& instrumentPlayer, const KitLayout::InstrumentSlot::ArticulationSlot& art);
    static void applyLayerParameterUpdate(InstrumentPlayer& instrumentPlayer, const LayerParameterUpdate& update);
    static void applyPreparedMapLayerUpdate(InstrumentPlayer& instrumentPlayer, const LayerParameterUpdate& update, const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes);
    std::string makePreviewTargetCacheKey(const juce::String& kitCodeRaw,
                                          const juce::String& instCode,
                                          const juce::String& artCode,
                                          juce::uint8 velocity,
                                          const juce::String& relativePath,
                                          const juce::String& micCode) const;
    std::optional<PreviewTarget> resolvePreviewTarget(const juce::String& kitCodeRaw,
                                                      const juce::String& instCode,
                                                      const juce::String& artCode,
                                                      juce::uint8 velocity,
                                                      const juce::String& relativePath,
                                                      const juce::String& micCode);
    void resolveAndLoadPreviewTarget(std::string cacheKey,
                                     const juce::String& kitCodeRaw,
                                     const juce::String& instCode,
                                     const juce::String& artCode,
                                     const juce::String& relativePath,
                                     const juce::String& micCode,
                                     juce::uint8 resolveVelocity,
                                     juce::uint8 playbackVelocity,
                                     uint64_t generation);
    void requestPreviewPlayback(const PreviewTarget& target, juce::uint8 velocity);
    std::shared_ptr<juce::AudioBuffer<float>> preparePreviewBuffer(const PreviewTarget& target, const std::shared_ptr<juce::AudioBuffer<float>>& buffer, double sampleRate) const;
    void resolveAndLoadPreviewGroup(std::vector<LibraryPreviewLayer> layers, juce::uint8 velocity, float playbackGain, uint64_t generation);
    void completePreviewGroupLayer(const std::shared_ptr<PreviewGroupLoadState>& state, std::shared_ptr<juce::AudioBuffer<float>> buffer, double sampleRate, float gain);
    void publishPreviewGroup(const std::shared_ptr<PreviewGroupLoadState>& state);
    void invalidateLibraryPreviewCache();
    ParseResult<LibraryArticulationInfo> setLibraryPlacementBalance(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, std::optional<int> balance, bool articulationScope);
    bool publishSourceRecipeReload(const std::shared_ptr<SampledSound>& sound, uint64_t revision, float fixedGainDb, std::shared_ptr<juce::AudioBuffer<float>> buffer, double sampleRate);

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
    std::atomic<uint64_t> previewTargetGeneration { 0 };
    std::atomic<uint64_t> previewGeneration { 0 };
    juce::ThreadPool previewResolveThreadPool { 1 };
};
