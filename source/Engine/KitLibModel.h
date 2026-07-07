#pragma once

#include <juce_core/juce_core.h>
#include <regex>
#include <vector>
#include <map>
#include <array>
#include <optional>
#include <unordered_map>
#include "../Config/Helpers.h"
#include "KitLibConsts.h"
#include "RoutingTypes.h"

/**
 * Class Hierarchy:
 * LibraryManager
 *   └─ DrumKitSpec
 *      └─ InstrumentSpec
 *          └─ ArticulationSpec
 *              └─ VelocityLayerSpec
 *                  └─ SampleVariantSpec
 *                      └─ MicSpec
 * Sample Filename Convention:
 *    <InstrumentsRoot>/
 *       └─ <kitInfo>/
 *          └─ <instrumentType>-<size>[-<balance>][-<description>]/
 *              └─ <articulation>[optionalExpressionShortName]-<optionalExpressionCode><value>-<anotherExpressionCode><value>/
 *                  └─ <instrumentType>-<size>-<articulation>[optionalExpressionShortName]-<velocity><sampleVariant>-<micChain>-<otherOptionalSampleParams>.<ext>
 * otherOptionalSampleParams: includes RMS values for v{n or p}{volume_trim_db, vn - negative, vp - positive}-p{peak_db*-10}_{peak_ms}-d{decay_ms}-c{decay_code}), 
 * where decay_code is a base62-encoded string representing the decay curve of relative interval in ms from peak to -60dB with 6dB steps (can have less then 10 steps for quiet samples!)
 * (refer to scripts\analyze_drum_samples.py line decay_code: str = encode_time_intervals_as_base62(decay_points))
 * 
 *     instruments/                                                 LibraryManager
 *       └─ Kit Name by Author Name  [CC-BY-4.0]/                   DrumKitSpec
 *          └─ kick-22-Tama Starclassic etc/                        InstrumentSpec
 *              └─ center/                                          ArticulationSpec
 *                  └─ kick-22-center-127a-dim.flac                                VelocityLayerSpec, SampleVariantSpec, MicSpec
 *          └─ hihat-14-Sabian HHX/
 *              └─ bow[half]-pdl_opn_cls043-zon_bel_edg085/
 *                  └─ hihat-14-bow[half]-043a-oni-p556_51-d1650-2U_c26_2g_3S_1s_1C_26_42_6s_3S.flac
 *                  └─ hihat-14-bow[half]-085a-oni-p396_70-d2078-3y_2q_2g_2U_2g_26_2q_34_7K_2U.flac
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
 *     sampleVariant   = letter a-z
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

    bool isValid() const { return this->validPoints > 0 && this->peakDb < 0.0f; }

    float getAbsoluteDbAtMs(float elapsedMs) const;
    float getMsAtAbsoluteDb(float targetAbsDb) const;

    static int base62ToInt(const juce::String& s);
    static DecayEnvelope decode(const juce::String& decayCode, float peakDb, float peakMs);
};

class DrumKitSpec;
class InstrumentSpec;
class ArticulationSpec;
class VelocityLayerSpec;
class SampleVariantSpec;
class MicSpec;

/**
 * SampleVariantSpec
 * Represents a single audio file for a specific round-robin variation.
 */
class SampleVariantSpec {
public:
    VelocityLayerSpec* parent = nullptr;
    uint8_t id;
    juce::juce_wchar letter; // 'a'-'z'
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

    float volumeTrimDb = 0.0f;
    float peakDb = 0.0f;
    float peakMs = 0.0f;
    float decayMs = 0.0f;
    juce::String decayCode;

    MicSpec micSpec;

    explicit SampleVariantSpec(VelocityLayerSpec* parent, const juce::File& file, juce::juce_wchar letter, const MicSpec& micSpec, int fileChannelStart = 0)
        : parent(parent)
        , sampleFile(file)
        , letter(letter)
        , sampleFileName(file.getFileName())
        , micSpec(micSpec)
    {
        this->fileChannelStart = fileChannelStart;
        this->fileChannelCount = micSpec.channels;
    }

    bool isValid() const { return sampleFile.existsAsFile(); }
    juce::String getContentFingerprint() const;

    /**
     * Converts a single letter a-z (case-insensitive) to 1-26.
     * @return uint8_t 1-26 or 0 if invalid
     */
    static uint8_t letterToIdx(const juce::String& letter) { return letterToIdx(letter[0]); }
    static uint8_t letterToIdx(const juce::juce_wchar& letter) {
        if (letter == '\0') return 0;
        juce::juce_wchar c = std::tolower(letter);
        return (c >= 'a' && c <= 'z') ? uint8_t(c - 'a' + 1) : 0;
    }
};

/**
 * VelocityLayerSpec
 * Represents a specific velocity level with multiple round-robin samples.
 */
class VelocityLayerSpec {
public:
    ArticulationSpec* parent = nullptr;
    uint8_t velocity;
    std::vector<std::unique_ptr<SampleVariantSpec>> sampleVariantSpecs;

    explicit VelocityLayerSpec(ArticulationSpec* parent, uint8_t velocity)
        : parent(parent), velocity(velocity) {}

    SampleVariantSpec* findSampleVariant(juce::juce_wchar variantLetter, const MicSpec& micSpec) {
        for (auto& existing : this->sampleVariantSpecs)
            if (existing->letter == variantLetter && existing->micSpec.type == micSpec.type && existing->micSpec.position == micSpec.position && existing->micSpec.format == micSpec.format)
                return existing.get();
        return nullptr;
    }    
    
    SampleVariantSpec* getOrAddSampleVariant(const juce::File& sampleFile, uint8_t variantIdx, const MicSpec& micSpec, int fileChannelStart) {
        juce::juce_wchar variantLetter = (variantIdx > 0 && variantIdx <= 26) ? static_cast<juce::juce_wchar>('a' + variantIdx - 1) : '\0';
        auto existing = this->findSampleVariant(variantLetter, micSpec);
        if (existing != nullptr) return existing;
        this->sampleVariantSpecs.push_back(std::make_unique<SampleVariantSpec>(this, sampleFile, variantLetter, micSpec, fileChannelStart));
        return this->sampleVariantSpecs.back().get();
    }

    bool isEmpty() const {
        return this->sampleVariantSpecs.empty();
    }
    juce::String getContentFingerprint() const;
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

        explicit ExpressionDimension(juce::String codeRaw) : codeRaw(codeRaw)
            , type(ExpressionDimension::stringToType(codeRaw))
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
    std::optional<int> balance;
    bool balanceConflict = false;
    StringsDictSingleton::Id nameId = 0;
    StringsDictSingleton::Id exprId = 0;
    std::vector<juce::String> folderCodeRaws; //FIXME: wtf is this naming
    std::vector<std::unique_ptr<ExpressionDimension>> expressionDimensions;
    std::vector<std::unique_ptr<VelocityLayerSpec>> velocityLayerSpecs;

    explicit ArticulationSpec(InstrumentSpec* parent, const juce::String& codeRaw)
        : parent(parent)
        , codeRaw(codeRaw)
    {
        auto wrapSegm = extractWrappedSegment(codeRaw, "[", "]");
        this->nameId = StringsDictSingleton::getId(wrapSegm.prefix);
        this->exprId = StringsDictSingleton::getId(wrapSegm.wrapped);
    }

    const std::vector<std::unique_ptr<VelocityLayerSpec>>& getVelocityLayers() const { return this->velocityLayerSpecs; }
    void addFolderIdentity(const juce::String& folderCodeRaw);
    std::vector<juce::String> getIdentityCandidates() const;
    std::optional<int> getEffectiveBalance() const;

    bool isEmpty() const {
        return this->velocityLayerSpecs.empty() || std::all_of(this->velocityLayerSpecs.begin(), this->velocityLayerSpecs.end(), [](const auto& v) {
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

    VelocityLayerSpec* findVelocityLayer(uint8_t velocity) {
        for (auto& existing : this->velocityLayerSpecs)
            if (existing->velocity == velocity)
                return existing.get();
        return nullptr;
    }

    VelocityLayerSpec* getOrAddVelocityLayer(uint8_t velocity) {
        auto existing = this->findVelocityLayer(velocity);
        if (existing != nullptr) return existing;
        this->velocityLayerSpecs.push_back(std::make_unique<VelocityLayerSpec>(this, velocity));
        return this->velocityLayerSpecs.back().get();
    }

    void sortVelocityLayers() {
        std::sort(this->velocityLayerSpecs.begin(), this->velocityLayerSpecs.end(), [](const auto& a, const auto& b) {
            return a->velocity < b->velocity;
        });
    }
    juce::String getContentFingerprint() const;
};

/**
 * InstrumentSpec
 * Represents a drum/percussion instrument with articulations and metadata.
 */
class InstrumentSpec {
public:

    //TODO: add global config for custom names mapping (like "chopper">"cymbal" "rototom">"tom" "vibraslap">"percussion" etc or maybe better use StringsDictSingleton for new types?
    enum class Type {
        Unknown,
        Percussion,
        Other,
        Drum,
        Cymbal,
        Kick,
        Tom,
        Octoban,
        Rototom,
        Timbale,
        Timpani,
        Conga,
        Bongo,
        Djembe,
        Cajon,
        Taiko,
        Surdo,
        Mridangam,
        TalkingDrum,
        Snare,
        Ride,
        HiHat,
        Crash,
        Splash,
        China,
        Bell,
        Stack,
        Chopper,
        Gong,
        Karatala,
        Cowbell,
        Tambourine,
        Shaker,
        Maracas,
        Cabasa,
        Guiro,
        Claves,
        Agogo,
        WoodBlock,
        TempleBlock,
        Triangle,
        Vibraslap,
        Castanets,
        SleighBells,
        Whip,
        Ratchet,
    };
    struct TypeEntry {
        const char* name;
        Type type;
    };
    static constexpr TypeEntry typeTable[] = {
        {"percussion",   Type::Percussion},
        {"other",        Type::Other},
        {"drum",         Type::Drum},
        {"cymbal",       Type::Cymbal},
        {"kick",         Type::Kick},
        {"tom",          Type::Tom},
        {"octoban",      Type::Octoban},
        {"rototom",      Type::Rototom},
        {"timbale",      Type::Timbale},
        {"timpani",      Type::Timpani},
        {"conga",        Type::Conga},
        {"bongo",        Type::Bongo},
        {"djembe",       Type::Djembe},
        {"cajon",        Type::Cajon},
        {"taiko",        Type::Taiko},
        {"surdo",        Type::Surdo},
        {"mridangam",    Type::Mridangam},
        {"talking_drum", Type::TalkingDrum},
        {"snare",        Type::Snare},
        {"ride",         Type::Ride},
        {"hihat",        Type::HiHat},
        {"hi_hat",       Type::HiHat},
        {"crash",        Type::Crash},
        {"splash",       Type::Splash},
        {"china",        Type::China},
        {"bell",         Type::Bell},
        {"stack",        Type::Stack},
        {"stax",         Type::Stack},
        {"chopper",      Type::Chopper},
        {"gong",         Type::Gong},
        {"karatala",     Type::Karatala},
        {"cowbell",      Type::Cowbell},
        {"cowbel",       Type::Cowbell},
        {"tambourine",   Type::Tambourine},
        {"shaker",       Type::Shaker},
        {"maracas",      Type::Maracas},
        {"cabasa",       Type::Cabasa},
        {"guiro",        Type::Guiro},
        {"claves",       Type::Claves},
        {"agogo",        Type::Agogo},
        {"wood_block",   Type::WoodBlock},
        {"woodblock",    Type::WoodBlock},
        {"temple_block", Type::TempleBlock},
        {"triangle",     Type::Triangle},
        {"vibraslap",    Type::Vibraslap},
        {"castanets",    Type::Castanets},
        {"sleigh_bells", Type::SleighBells},
        {"whip",         Type::Whip},
        {"ratchet",      Type::Ratchet},
    };
    static juce::String typeToString(Type type){
        for (const auto& e : typeTable)
            if (e.type == type)  return e.name;
        return "unknown";
    }
    static Type typeFromString(const juce::String& name) {
        for (const auto& e : typeTable)
            if (name.toLowerCase() == e.name) return e.type;
        return Type::Unknown;
    }
    
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
        juce::String sampleVariant;
        std::vector<MicSpec> micDescriptors;
        juce::File file;
        
        float volumeTrimDb = 0.0f; // positive or negative volume adjustment in dB to apply to this sample for better level matching in the kit (ex. for cymbals with close mics that are too loud compared to other samples)
        float peakDb = 0.0f;
        float peakMs = 0.0f;
        float decayMs = 0.0f;
        float offsetMs = 0.0f; //TODO: calculated offset for distant mics based on peakMs difference with closest mic, used for making choke more realistic by delaying the choke for distant mics 
        juce::String decayCode; // base62-encoded decay curve code for more precise envelope reconstruction.  refer to scripts\analyze_drum_samples.py line decay_code: str = encode_time_intervals_as_base62(decay_points)
    };

    DrumKitSpec* parent = nullptr;
    MetaInfo meta;
    std::vector<std::unique_ptr<ArticulationSpec>> articulationSpecs;

    explicit InstrumentSpec(DrumKitSpec* kit, const MetaInfo& meta)
        : parent(kit)
        , meta(meta) {
    }

    static ParseResult<MetaInfo> parseInfo(const juce::String& dirName);
    static std::vector<juce::String> getIdentityCandidates(const MetaInfo& meta);

    bool isAudioFile(const juce::File& file);

    /**
     * Scan all samples from an instrument folder.
     * Parses each audio file and populates the instrument's articulation tree.
     *
     * @return number of samples loaded
     */
    struct ScanStats {
        std::map<juce::String, int> dimensionCodeCounts;
        std::map<juce::String, int> micCodeCounts;
        std::map<juce::String, int> sampleVariantCounts;
        std::map<int, int> velocityCounts;
    };

    int scanInstrumentSamples(ScanStats* stats = nullptr);

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

    explicit DrumKitSpec(const MetaInfo& meta) : meta(meta) {}

    static ParseResult<MetaInfo> parseInfo(const juce::String& dirName);
    static std::vector<juce::String> getIdentityCandidates(const MetaInfo& meta);

    int scanInstruments();

    void addInstrument(InstrumentSpec::MetaInfo instMeta) { this->instrumentSpecsMap[instMeta.codeRaw] = std::make_unique<InstrumentSpec>(this, instMeta); }
    InstrumentSpec* findInstrument(const juce::String& id);
    const InstrumentSpec* findInstrumentById(const juce::String& id) const;
    InstrumentSpec* findInstrumentById(const juce::String& id) { return const_cast<InstrumentSpec*>(static_cast<const DrumKitSpec*>(this)->findInstrumentById(id)); }
    juce::String getPreferredInstrumentId(const InstrumentSpec& instrument) const;
    std::vector<InstrumentSpec*> getInstrumentsByType(InstrumentSpec::Type type) const;
    juce::String getContentFingerprint() const;

    MetaInfo meta;
    std::unordered_map<juce::String, std::unique_ptr<InstrumentSpec>> instrumentSpecsMap;
    const std::unordered_map<juce::String, std::unique_ptr<InstrumentSpec>>& getInstrumentSpecsMap() const { return this->instrumentSpecsMap; }
};


class LibraryManager {
public:
    explicit LibraryManager(const juce::File& rootDir) : rootDir(rootDir) { this->validateKitsDir(rootDir); }

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
    juce::String getContentFingerprint() const;

private:
    juce::File rootDir;
    std::vector<std::unique_ptr<DrumKitSpec>> kitSpecs;
};

}
