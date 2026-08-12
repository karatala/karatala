#pragma once

#include <juce_core/juce_core.h>
#include <regex>
#include <vector>
#include <map>
#include <array>
#include <optional>
#include <unordered_map>
#include "../Config/Helpers.h"
#include "../Config/Parsing.h"
#include "InstrumentType.h"
#include "KitLibConsts.h"
#include "LibraryKitOverrides.h"
#include "RoutingTypes.h"
#include "SampleMetadata.h"

/**
 * Class Hierarchy:
 * LibraryManager
 *   └─ DrumKitSpec
 *      └─ InstrumentSpec
 *          └─ ArticulationSpec
 *              └─ VelocityLevelSpec
 *                  └─ LogicalHitSpec
 *                      └─ SampleSpec
 *                          └─ MicSpec
 * Sample Filename Convention:
 *    <InstrumentsRoot>/
 *       └─ <kitInfo>/
 *          └─ <instrumentType>-<size>[-<balance>][-<description>]/
 *              └─ <articulation>[optionalExpressionShortName]-<optionalExpressionCode><value>-<anotherExpressionCode><value>/
 *                  └─ <instrumentType>-<size>-<articulation>[optionalExpressionShortName]-<velocity><roundRobinLetter>-<micChain>-<otherOptionalSampleParams>.<ext>
 * otherOptionalSampleParams:
 *     v{n|p}<gainDb>                  manual gain, where vn is negative and vp is positive
 *     o{n|p}<milliseconds>[_micCode...]  playback timing, where on advances and op delays; optional mic codes limit the offset
 * 
 *     instruments/                                                 LibraryManager
 *       └─ Kit Name by Author Name  [CC-BY-4.0]/                   DrumKitSpec
 *          └─ kick-22-Tama Starclassic etc/                        InstrumentSpec
 *              └─ center/                                          ArticulationSpec
 *                  └─ kick-22-center-127a-dim.flac                                VelocityLevelSpec, LogicalHitSpec, SampleSpec, MicSpec
 *          └─ hihat-14-Sabian HHX/
 *              └─ bow[half]-pdl_opn_cls043-zon_bel_edg085/
 *                  └─ hihat-14-bow[half]-043a-oni-vp5.flac
 *                  └─ hihat-14-bow[half]-085a-oni-vp3.flac
 *                  ...
 *              └─ bow[close]-pdl_opn_cls085/
 *                  ...
 *              └─ edge[half]-pdl_opn_cls043/
 *              ...
 *          └─ snare-X1/
 *              └─ center-zon_ctr_rim043/
 *                  └─ snare-X1-center-043a-dtm_dbm_ons.flac
 *                  └─ snare-X1-center-085a-dtm_dbm_ons.flac
 *              └─ center-zon_ctr_rim085/
 *                  ...
 *              └─ edge/
 *                  ...
 *              └─ rimshot/
 *                  ...
 *
 * Fields:
 *     kitInfo         = KitName by AuthorName [License]
 *     instrumentType  = kick snare tom hihat ride crash splash china stax cowbell tambourine shaker timbale octoban woodblock clap ...
 *     balance         = optional L1-L127 R1-R127 C0, used as instrument fallback when articulation has no balance; bare L/R/C is ignored with warning
 *     articulation    = center rim rimshot edge bell bow pedal
 *     dimensionCode   = pdl_opn_cls zon_bel_edg zon_ctr_edg zon_lft_rgt // or shorter codes poc zbe zce zlr?
 *     velocity        = 001-127
 *     roundRobinLetter = letter a-z
 *     micChain        = micCode("_"micCode)* to describe each channel for multichannel files
 *                         ex. dim_dom_ons_rfs for direct in mono, direct out mono, overhead normal stereo, room far stereo
 *                       type = d (direct) | o (overhead) | r (room)
 *                       position = (for direct) u (sub) | s (side) | t (top) | b (bottom) | i (in) | o (out)
 *                                  (for overhead) n (normal) | w (wide)
 *                                  (for room) c (close) | f (far)
 *                       format   = m (mono) | s (stereo) | i (mid-side)
 *                       variant  = optional number for any additional variant specification (ex. snare with 2 top mics: dtm1, dtm2)
 *
 * Identity resolution for instrument maps:
 *     Kit IDs use the shortest unique value from: KitName, KitName by AuthorName, KitName by AuthorName [License], full folder name.
 *     Instrument IDs use the shortest unique value from: <instrumentType>-<size>, +<balance>, +<description>, full folder name.
 *     Articulation IDs use the shortest unique value from: <articulation>[optionalExpressionShortName], full articulation folder name.
 *     Full folder names stay valid so old maps can still resolve when possible.
 * ===========================================
 * 

 //TODO: add configurable in global config articulation aliases (what string format, how to parse/use?) like:
        hihat bow poc<=43 'open'
        hihat bow poc<=52 'half'
        hihat bow poc<=77 'close'
        hihat bow poc>=85 'tight'
        hihat pedal poc>=90 'pedal'
        hihat pedal poc<90 'foot splash'
 */

namespace KitLib {

bool matchesIdentityCandidate(const std::vector<juce::String>& candidates, const juce::String& id);

struct DecayEnvelope {
    static constexpr int NUM_POINTS = 10;
    static constexpr float DB_STEP = -6.0f;

    float peakDb = 0.0f;
    float peakMs = 0.0f;
    std::array<float, NUM_POINTS> points;
    int validPoints = 0;

    DecayEnvelope() {
        this->points.fill(-1.0f);
    }

    bool isValid() const { return this->validPoints > 0; }

    float getAbsoluteDbAtMs(float elapsedMs) const;
    float getMsAtAbsoluteDb(float targetAbsDb) const;

    static DecayEnvelope fromPoints(float peakDb, float peakMs, const std::array<std::optional<float>, NUM_POINTS>& pointsMs);
};

class DrumKitSpec;
class InstrumentSpec;
class ArticulationSpec;
class VelocityLevelSpec;
class LogicalHitSpec;
class SampleSpec;
class MicSpec;

inline uint8_t roundRobinIdFromLetter(juce::juce_wchar letter) {
    if (letter >= 'A' && letter <= 'Z') letter = static_cast<juce::juce_wchar>(letter - 'A' + 'a');
    return letter >= 'a' && letter <= 'z' ? static_cast<uint8_t>(letter - 'a' + 1) : 0;
}

inline uint8_t roundRobinIdFromText(const juce::String& text) { return text.isEmpty() ? 0 : roundRobinIdFromLetter(text[0]); }

inline juce::juce_wchar roundRobinLetterFromId(uint8_t roundRobinId) {
    return roundRobinId >= 1 && roundRobinId <= 26 ? static_cast<juce::juce_wchar>('a' + roundRobinId - 1) : '\0';
}

/**
 * SampleSpec
 * Represents one mic entry in a logical hit.
 */
class SampleSpec {
public:
    LogicalHitSpec* parent = nullptr;
    juce::File sampleFile;
    juce::String sampleFileName;

    //TODO: use optional metadata cached at parse time (lazy-loaded from file)
    juce::int64 fileSizeBytes = 0;
    double sampleRate = 0.0;
    int bitDepth = 0;
    int numChannels = 0;
    juce::int64 numSamples = 0;
    // Start channel index inside the source file (0-based)
    int fileChannelStart = 0;
    // Number of channels occupied by this mic inside the source file (usually equals micSpec.channels)
    int fileChannelCount = 0;

    float filenameManualGainDb = 0.0f;
    std::optional<float> manualGainDbOverride;
    float manualGainDb = 0.0f;
    float filenamePlaybackOffsetMs = 0.0f;
    std::optional<float> playbackOffsetMsOverride;
    float playbackOffsetMs = 0.0f;
    float peakDb = 0.0f;
    float peakMs = 0.0f;
    float decayMs = 0.0f;
    DecayEnvelope decayEnvelope;
    bool hasCsvMetadata = false;
    juce::String metadataContentHash;
    juce::int64 metadataFileSizeBytes = 0;
    std::optional<float> onsetMs;
    std::optional<float> intensityDb;
    std::optional<float> truePeakDb;
    std::optional<float> stereoBalance;
    std::optional<float> timingOffsetMs;

    MicSpec micSpec;

    explicit SampleSpec(LogicalHitSpec* parentSpec, const juce::File& file, const MicSpec& newMicSpec, int channelStart = 0)
        : parent(parentSpec)
        , sampleFile(file)
        , sampleFileName(file.getFileName())
        , fileChannelStart(channelStart)
        , fileChannelCount(newMicSpec.channels)
        , micSpec(newMicSpec)
    {}

    bool isValid() const { return this->sampleFile.existsAsFile(); }
};

/**
 * LogicalHitSpec
 * Groups all mic samples that must play together for one round-robin hit.
 */
class LogicalHitSpec {
public:
    VelocityLevelSpec* parent = nullptr;
    uint8_t roundRobinId = 0;
    std::vector<std::unique_ptr<SampleSpec>> samples;
    std::optional<float> intensityDb;
    std::optional<float> maxTruePeakDb;

    explicit LogicalHitSpec(VelocityLevelSpec* parentSpec, uint8_t newRoundRobinId)
        : parent(parentSpec), roundRobinId(newRoundRobinId) {}

    SampleSpec* findSample(const MicSpec& micSpec) {
        for (auto& existing : this->samples)
            if (existing->micSpec.raw.compareIgnoreCase(micSpec.raw) == 0) return existing.get();
        return nullptr;
    }

    SampleSpec* getOrAddSample(const juce::File& sampleFile, const MicSpec& micSpec, int fileChannelStart) {
        auto* existing = this->findSample(micSpec);
        if (existing != nullptr) return existing;
        this->samples.push_back(std::make_unique<SampleSpec>(this, sampleFile, micSpec, fileChannelStart));
        return this->samples.back().get();
    }

    bool isEmpty() const { return this->samples.empty(); }
};

/**
 * VelocityLevelSpec
 * Represents a specific velocity level with multiple logical hits.
 */
class VelocityLevelSpec {
public:
    ArticulationSpec* parent = nullptr;
    uint8_t velocity;
    uint8_t selectionUpperVelocity = 0;
    std::vector<std::unique_ptr<LogicalHitSpec>> logicalHits;
    std::optional<float> rawVelocityMedianDb;
    std::optional<float> fittedSourceDb;
    std::optional<float> calibrationTrimDb;
    bool calibrationLimited = false;

    explicit VelocityLevelSpec(ArticulationSpec* parentSpec, uint8_t namedVelocity)
        : parent(parentSpec), velocity(namedVelocity) {}

    LogicalHitSpec* findLogicalHit(uint8_t roundRobinId) {
        for (auto& existing : this->logicalHits)
            if (existing->roundRobinId == roundRobinId) return existing.get();
        return nullptr;
    }

    LogicalHitSpec* getOrAddLogicalHit(uint8_t roundRobinId) {
        auto* existing = this->findLogicalHit(roundRobinId);
        if (existing != nullptr) return existing;
        this->logicalHits.push_back(std::make_unique<LogicalHitSpec>(this, roundRobinId));
        return this->logicalHits.back().get();
    }

    bool isEmpty() const { return this->logicalHits.empty() || std::all_of(this->logicalHits.begin(), this->logicalHits.end(), [](const auto& logicalHit) { return logicalHit->isEmpty(); }); }
};

/**
 * ArticulationSpec
 * Represents an articulation (e.g., "center", "edge") with multiple velocity levels.
 */
class ArticulationSpec {
public:
    struct ExpressionDimension {
        enum class Type { Unknown, PedalOpenClose, ZoneCenterRim, ZoneBowEdge, ZoneLeftRight }; 
        struct TypeEntry {
            const char* name;
            Type type;
        };
        static constexpr TypeEntry typeTable[] = {
            {"pdl_opn_cls", Type::PedalOpenClose},
            {"zon_ctr_rim", Type::ZoneCenterRim},
            {"zon_bow_edg", Type::ZoneBowEdge},
            {"zon_lft_rgt", Type::ZoneLeftRight},
        };

        explicit ExpressionDimension(juce::String newCodeRaw) : codeRaw(newCodeRaw)
            , type(ExpressionDimension::stringToType(newCodeRaw))
        {}

        static juce::String typeToString(Type type) {
            for (const auto& e : typeTable) if (e.type == type) return e.name;
            return "unknown";
        }
        static Type stringToType(const juce::String& name) {
            for (const auto& e : typeTable) if (name.toLowerCase() == e.name) return e.type;
            return Type::Unknown;
        }
        
        juce::String codeRaw;
        std::vector<uint8_t> values;
        Type type;
    };
    
    juce::String codeRaw;
    InstrumentSpec* parent = nullptr;
    std::optional<int> filenameBalance;
    std::optional<int> balanceOverride;
    std::optional<int> balance;
    bool balanceConflict = false;
    bool balanceResolved = false;
    StringsDictSingleton::Id nameId = 0;
    StringsDictSingleton::Id exprId = 0;
    std::vector<juce::String> folderCodeRaws; //FIXME: wtf is this naming
    std::vector<std::unique_ptr<ExpressionDimension>> expressionDimensions;
    std::vector<std::unique_ptr<VelocityLevelSpec>> velocityLevels;

    explicit ArticulationSpec(InstrumentSpec* parentSpec, const juce::String& newCodeRaw)
        : codeRaw(newCodeRaw), parent(parentSpec)
    {
        auto wrapSegm = Parsing::extractWrappedSegment(newCodeRaw, "[", "]");
        this->nameId = StringsDictSingleton::getId(wrapSegm.prefix);
        this->exprId = StringsDictSingleton::getId(wrapSegm.wrapped);
    }

    const std::vector<std::unique_ptr<VelocityLevelSpec>>& getVelocityLevels() const { return this->velocityLevels; }
    void addFolderIdentity(const juce::String& folderCodeRaw);
    std::vector<juce::String> getIdentityCandidates() const;
    std::optional<int> getEffectiveBalance() const;

    bool isEmpty() const {
        return this->velocityLevels.empty() || std::all_of(this->velocityLevels.begin(), this->velocityLevels.end(), [](const auto& v) {
            return v->isEmpty();
        });
    }

    void addExpressionDimensionValue(const juce::String& code, uint8_t value) {
        auto* expSpec = this->getOrAddExpressionDimension(code);
        if (!vectorContains(expSpec->values, value)) expSpec->values.push_back(value);
    }

    ExpressionDimension* findExpressionDimension(const juce::String& code) {
        for (auto& existing : this->expressionDimensions)
            if (existing->codeRaw == code) return existing.get();
        return nullptr;
    }

    ExpressionDimension* getOrAddExpressionDimension(const juce::String& code) {
        auto existing = this->findExpressionDimension(code);
        if (existing != nullptr) return existing;
        this->expressionDimensions.push_back(std::make_unique<ExpressionDimension>(code));
        return this->expressionDimensions.back().get();
    }

    VelocityLevelSpec* findVelocityLevel(uint8_t velocity) {
        for (auto& existing : this->velocityLevels)
            if (existing->velocity == velocity)
                return existing.get();
        return nullptr;
    }

    VelocityLevelSpec* getOrAddVelocityLevel(uint8_t velocity) {
        auto existing = this->findVelocityLevel(velocity);
        if (existing != nullptr) return existing;
        this->velocityLevels.push_back(std::make_unique<VelocityLevelSpec>(this, velocity));
        return this->velocityLevels.back().get();
    }

    void sortVelocityLevels() {
        std::sort(this->velocityLevels.begin(), this->velocityLevels.end(), [](const auto& a, const auto& b) {
            return a->velocity < b->velocity;
        });
    }
};

/**
 * InstrumentSpec
 * Represents a drum/percussion instrument with articulations and metadata.
 */
class InstrumentSpec {
public:

    //TODO: add global config for custom names mapping (like "chopper">"cymbal" "rototom">"tom" "vibraslap">"percussion" etc or maybe better use StringsDictSingleton for new types?
    using Type = InstrumentType;
    static juce::String typeToString(Type type) { return instrumentTypeToString(type); }
    static Type typeFromString(const juce::String& name) { return instrumentTypeFromString(name); }
    
    struct MetaInfo {
        juce::File rootDir;
        juce::String codeRaw;
        juce::String name;
        InstrumentSpec::Type type = InstrumentSpec::Type::Unknown;
        juce::String size = "";
        std::optional<int> balance;
        juce::String description = "";
    };
    struct SampleFileInfo {
        juce::String type;
        juce::String articulation;
        uint8_t velocity;
        juce::String roundRobinLetter;
        std::vector<MicSpec> micDescriptors;
        juce::File file;
        
        float manualGainDb = 0.0f; // positive or negative volume adjustment in dB to apply to this sample for better level matching in the kit (ex. for cymbals with close mics that are too loud compared to other samples)
        float playbackOffsetMs = 0.0f; // negative advances by skipping the beginning; positive delays playback
        std::unordered_map<juce::String, float> playbackOffsetMsByMic;
        float peakDb = 0.0f;
        float peakMs = 0.0f;
        float decayMs = 0.0f;
    };

    DrumKitSpec* parent = nullptr;
    MetaInfo meta;
    std::optional<int> filenameBalance;
    std::optional<int> balanceOverride;
    std::vector<std::unique_ptr<ArticulationSpec>> articulationSpecs;
    mutable std::optional<juce::String> contentFingerprintCache;

    explicit InstrumentSpec(DrumKitSpec* kit, const MetaInfo& metaInfo);

    static ParseResult<MetaInfo> parseInfo(const juce::String& dirName);
    static std::vector<juce::String> getIdentityCandidates(const MetaInfo& meta);

    static bool isAudioFile(const juce::File& file);

    /**
     * Scan all samples from an instrument folder.
     * Parses each audio file and populates the instrument's articulation tree.
     *
     * @return number of samples loaded
     */
    struct ScanStats {
        std::map<juce::String, int> dimensionCodeCounts;
        std::map<juce::String, int> micCodeCounts;
        std::map<juce::String, int> roundRobinCounts;
        std::map<int, int> velocityCounts;
    };

    int scanInstrumentSamples(ScanStats* stats = nullptr);
    void buildCalibrationProfiles();
    void applyLibraryKitOverrides();
    ParseResult<int> applyVelocityUpperBoundaryOverrides(ArticulationSpec& articulation);

    static ParseResult<InstrumentSpec::SampleFileInfo> parseSampleFile(const juce::String& fileName);

    ArticulationSpec* findArticulation(const juce::String& codeRaw) const {
        for (const auto& existing : this->articulationSpecs)
            if (existing->codeRaw == codeRaw)
                return existing.get();
        return nullptr;
    }

    ArticulationSpec* getOrAddArticulation(const juce::String& codeRaw) {
        auto existing = this->findArticulation(codeRaw);
        if (existing != nullptr) return existing;
        this->articulationSpecs.push_back(std::make_unique<ArticulationSpec>(this, codeRaw));
        return this->articulationSpecs.back().get();
    }

    const ArticulationSpec* findArticulationById(const juce::String& id) const;
    ArticulationSpec* findArticulationById(const juce::String& id) { return const_cast<ArticulationSpec*>(static_cast<const InstrumentSpec*>(this)->findArticulationById(id)); }
    juce::String getPreferredArticulationId(const ArticulationSpec& articulation) const;
    std::optional<int> getEffectiveBalance() const { return this->balanceOverride.has_value() ? this->balanceOverride : this->filenameBalance; }
    juce::String getContentFingerprint() const;
};

/**
 * Represents a complete drum kit with metadata and instruments.
 */
class DrumKitSpec {
public:
    struct MetaInfo {
        juce::File rootDir;
        juce::String codeRaw;
        juce::String name;
        juce::String author;
        juce::String license;
    };

    explicit DrumKitSpec(const MetaInfo& metaInfo) : meta(metaInfo) {}

    static ParseResult<MetaInfo> parseInfo(const juce::String& dirName);
    static std::vector<juce::String> getIdentityCandidates(const MetaInfo& meta);

    int scanInstruments();

    void addInstrument(InstrumentSpec::MetaInfo instMeta) { this->instrumentSpecsMap[instMeta.codeRaw] = std::make_unique<InstrumentSpec>(this, instMeta); }
    InstrumentSpec* findInstrument(const juce::String& id);
    const InstrumentSpec* findInstrumentById(const juce::String& id) const;
    InstrumentSpec* findInstrumentById(const juce::String& id) { return const_cast<InstrumentSpec*>(static_cast<const DrumKitSpec*>(this)->findInstrumentById(id)); }
    juce::String getPreferredInstrumentId(const InstrumentSpec& instrument) const;
    std::vector<InstrumentSpec*> getInstrumentsByType(InstrumentSpec::Type type) const;

    MetaInfo meta;
    juce::String readmeMarkdown;
    SampleMetadataTable sampleMetadata;
    LibraryKitOverrides libraryKitOverrides;
    std::unordered_map<juce::String, std::unique_ptr<InstrumentSpec>> instrumentSpecsMap;
    const std::unordered_map<juce::String, std::unique_ptr<InstrumentSpec>>& getInstrumentSpecsMap() const { return this->instrumentSpecsMap; }
};


class LibraryManager {
public:
    explicit LibraryManager(const juce::File& rootDirectory) : rootDir(rootDirectory) { this->validateKitsDir(rootDirectory); }

    bool validateKitsDir(const juce::File& dir);
    void setKitsDir(const juce::File& dir);

    int scanAll();
    DrumKitSpec* getKit(int index);
    DrumKitSpec* findKit(const juce::String& name);
    const DrumKitSpec* findKitById(const juce::String& id) const;
    DrumKitSpec* findKitById(const juce::String& id) { return const_cast<DrumKitSpec*>(static_cast<const LibraryManager*>(this)->findKitById(id)); }
    juce::String getPreferredKitId(const DrumKitSpec& kit) const;
    static std::vector<juce::String> getIdentityCandidates(const DrumKitSpec::MetaInfo& meta);
    const std::vector<std::unique_ptr<DrumKitSpec>>& getKits() const { return this->kitSpecs; }

private:
    juce::File rootDir;
    std::vector<std::unique_ptr<DrumKitSpec>> kitSpecs;
};

}
