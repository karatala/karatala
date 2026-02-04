#pragma once

#include <juce_core/juce_core.h>
#include <regex>
#include <vector>
#include "../Config/Helpers.h"
#include "KitLibConsts.h"

/**
 * Class Hierarchy:
 * LibraryManager
 *   └─ DrumKitSpecs
 *      └─ InstrumentSpecs
 *          └─ ArticulationSpecs
 *              └─ VelocityLayerSpecs
 *                  └─ SampleVariantSpecs
 *                      └─ MicSpecs
 * Sample Filename Convention:
 *    <InstrumentsRoot>/
 *       └─ <kitInfo>/
 *          └─ <instrumentType>-<size>[-<description>]/
 *              └─ <articulation>[optionalExpressionShortName]-<optionalExpressionCode><value>-<anotherExpressionCode><value>/
 *                  └─ <velocity><sampleVariant>-<micChain>-<otherOptionalSampleParams>.<ext>
 * Example:
 *     instruments/                                                 LibraryManager
 *       └─ Kit Name by Author Name  [CC-BY-4.0]/                   DrumKitSpecs
 *          └─ kick-22-Tama Starclassic etc/                        InstrumentSpecs
 *              └─ center/                                          ArticulationSpecs
 *                  └─ 127a-dim.flac                                VelocityLayerSpecs, SampleVariantSpecs, MicSpecs
 *          └─ hihat-14-Sabian HHX/
 *              └─ bow[half]-pdl_opn_cls043-zon_bel_edg085/         
 *                  └─ 043a-oni.flac
 *                  └─ 085a-oni.flac
 *                  ...
 *              └─ bow[close]-pdl_opn_cls085/
 *                  ...
 *              └─ edge[half]-pdl_opn_cls043/
 *              ...
 *          └─ snare-X1/
 *              └─ center-zon_ctr_rim043/
 *                  └─ 043a-dtm_dbm_ons.flac
 *                  └─ 085a-dtm_dbm_ons.flac
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

class DrumKitSpecs;
class InstrumentSpecs;
class ArticulationSpecs;
class VelocityLayerSpecs;
class SampleVariantSpecs;
class MicSpecs;

/**
 * SampleVariantSpecs
 * Represents a single audio file for a specific round-robin variation.
 */
class SampleVariantSpecs {
public:
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

    MicSpecs micSpecs;
    VelocityLayerSpecs* parentVelocityLayer = nullptr;

    explicit SampleVariantSpecs(const juce::File& file, juce::juce_wchar letter, const MicSpecs& micSpecs)
        : sampleFile(file)
        , letter(letter)
        , sampleFileName(file.getFileName())
        , micSpecs(micSpecs)
    {
    }

    bool isValid() const { return sampleFile.existsAsFile(); }

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
 * VelocityLayerSpecs
 * Represents a specific velocity level with multiple round-robin samples.
 */
class VelocityLayerSpecs {
public:
    uint8_t velocity;
    ArticulationSpecs* parentArticulation = nullptr;
    std::vector<std::unique_ptr<SampleVariantSpecs>> sampleVariants;

    explicit VelocityLayerSpecs(uint8_t velocity)
        : velocity(velocity) {}

    void addSampleVariant(std::unique_ptr<SampleVariantSpecs> entry) {
        entry->parentVelocityLayer = this;
        this->sampleVariants.push_back(std::move(entry));
    }

    bool isEmpty() const {
        return this->sampleVariants.empty();
    }
};

/**
 * ArticulationSpecs
 * Represents an articulation (e.g., "center", "edge") with multiple velocity levels.
 */
class ArticulationSpecs {
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
    juce::String alias;
    StringIdRegistrySingleton::Id codeIdx = 0;
    std::vector<std::unique_ptr<ExpressionDimension>> dimensions;

    std::vector<std::unique_ptr<VelocityLayerSpecs>> velocityLayers;

    InstrumentSpecs* parent = nullptr;

    explicit ArticulationSpecs(InstrumentSpecs* parent, const juce::String& code)
        : parent(parent)
        , codeRaw(code)
        , codeIdx(StringIdRegistrySingleton::getId(code)) {
    }

    const std::vector<std::unique_ptr<VelocityLayerSpecs>>& getVelocityLayers() const { return this->velocityLayers; }

    bool isEmpty() const {
        return this->velocityLayers.empty() || std::all_of(this->velocityLayers.begin(), this->velocityLayers.end(), [](const auto& v) {
            return v->isEmpty();
        });
    }

    void addDimensionValue(const juce::String& code, uint8_t value) {
        auto* dim = this->getOrAddDimension(code);
        if (std::find(dim->values.begin(), dim->values.end(), value) == dim->values.end())
            dim->values.push_back(value);
    }

    ExpressionDimension* findDimension(const juce::String& code) {
        for (auto& existing : this->dimensions)
            if (existing->codeRaw == code)
                return existing.get();
        return nullptr;
    }

    ExpressionDimension* getOrAddDimension(const juce::String& code) {
        auto existing = this->findDimension(code);
        if (existing != nullptr)
            return existing;
        auto newObj = std::make_unique<ExpressionDimension>(code);
        auto* ptr = newObj.get();
        this->dimensions.push_back(std::move(newObj));
        return ptr;
    }

    VelocityLayerSpecs* findVelocityLayer(uint8_t velocity) {
        for (auto& existing : this->velocityLayers)
            if (existing->velocity == velocity)
                return existing.get();
        return nullptr;
    }

    VelocityLayerSpecs* getOrAddVelocityLayer(uint8_t velocity) {
        auto exist = this->findVelocityLayer(velocity);
        if (exist != nullptr) return exist;
        auto newObj = std::make_unique<VelocityLayerSpecs>(velocity);
        newObj->parentArticulation = this;
        auto* ptr = newObj.get();
        this->velocityLayers.push_back(std::move(newObj));
        return ptr;
    }

    void sortVelocityLayers() {
        std::sort(this->velocityLayers.begin(), this->velocityLayers.end(), [](const auto& a, const auto& b) {
            return a->velocity < b->velocity;
        });
    }
};

/**
 * InstrumentSpecs
 * Represents a drum/percussion instrument with articulations and metadata.
 */
class InstrumentSpecs {
public:

    //TODO: add global config for custom names mapping (like "chopper">"cymbal" "rototom">"tom" "vibraslap">"percussion" etc or maybe better use StringIdRegistrySingleton for new types?
    enum class Type { Unknown, Percussion, Drum, Cymbal, Kick, Tom, Snare, Ride, HiHat, Crash, Splash, China }; 
    struct TypeEntry {
        const char* name;
        Type type;
    };
    static constexpr TypeEntry typeTable[] = {
        {"percussion", Type::Percussion},
        {"drum",       Type::Drum},
        {"cymbal",     Type::Cymbal},
        {"kick",       Type::Kick},
        {"tom",        Type::Tom},
        {"snare",      Type::Snare},
        {"ride",       Type::Ride},
        {"hihat",      Type::HiHat},
        {"crash",      Type::Crash},
        {"splash",     Type::Splash},
        {"china",      Type::China},
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
        juce::String code;
        juce::String name;
        InstrumentSpecs::Type type = InstrumentSpecs::Type::Unknown;
        juce::String size = "";
        juce::String description = "";
    };
    struct SampleFileInfo {
        juce::String type;
        juce::String articulation;
        uint8_t velocity;
        juce::String sampleVariant;
        std::vector<MicSpecs> micDescriptors;
        juce::File file;
    };

    DrumKitSpecs* parentKit = nullptr;
    MetaInfo meta;
    std::vector<std::unique_ptr<ArticulationSpecs>> articulations;

    InstrumentSpecs(DrumKitSpecs* kit, const MetaInfo& meta)
        : parentKit(kit)
        , meta(meta) {
    }

    static ParseResult<MetaInfo> parseInfo(const juce::String& dirName);

    bool isAudioFile(const juce::File& file);

    /**
     * Scan all samples from an instrument folder.
     * Parses each audio file and populates the instrument's articulation tree.
     *
     * @return number of samples loaded
     */
    int scanInstrumentSamples();

    static ParseResult<InstrumentSpecs::SampleFileInfo> parseSampleFile(const juce::String& fileName);

    ArticulationSpecs* findArticulation(const StringIdRegistrySingleton::Id& codeIdx) const {
        for (const auto& existing : this->articulations)
            if (existing->codeIdx == codeIdx)
                return existing.get();
        return nullptr;
    }

    ArticulationSpecs* findArticulation(const juce::String& name, const juce::String& alias = "") const {
        for (const auto& existing : this->articulations)
            if (existing->codeRaw == name && existing->alias == alias)
                return existing.get();
        return nullptr;
    }

    ArticulationSpecs* getOrAddArticulation(const juce::String& name, const juce::String& alias = "") {
        auto existing = this->findArticulation(name, alias);
        if (existing != nullptr)
            return existing;
        auto newObj = std::make_unique<ArticulationSpecs>(this, name);
        newObj->alias = alias;
        auto* ptr = newObj.get();
        this->articulations.push_back(std::move(newObj));
        return ptr;
    }
};

/**
 * Represents a complete drum kit with metadata and instruments.
 */
class DrumKitSpecs {
public:
    struct MetaInfo {
        juce::File rootDir;
        juce::String code;
        juce::String name;
        juce::String author;
        juce::String license;
    };

    explicit DrumKitSpecs(const MetaInfo& meta) : meta(meta) {}

    static ParseResult<MetaInfo> parseInfo(const juce::String& dirName);

    int scanInstruments();
    InstrumentSpecs* findInstrument(const juce::String& code);
    std::vector<InstrumentSpecs*> getInstrumentsByType(InstrumentSpecs::Type type) const;

    MetaInfo meta;
    std::unordered_map<juce::String, std::unique_ptr<InstrumentSpecs>> instruments;
};


class LibraryManager {
public:
    explicit LibraryManager(const juce::File& kitsDir) : mKitsDir(kitsDir) { this->validateKitsDir(kitsDir); }

    bool validateKitsDir(const juce::File& dir);
    void setKitsDir(const juce::File& dir);

    int scanAll();
    DrumKitSpecs* getKit(int index);
    DrumKitSpecs* findKit(const juce::String& name);
    const std::vector<std::unique_ptr<DrumKitSpecs>>& getKits() const { return this->mKits; }

private:
    juce::File mKitsDir;
    std::vector<std::unique_ptr<DrumKitSpecs>> mKits;
};

}