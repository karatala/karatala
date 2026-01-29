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
 *    ${InstrumentsRoot}/
 *       └─ ${kitInfo}/
 *          └─ ${instrumentName}-${size}[-${description}]/
 *              └─ ${articulation}[-${dimensionCode}${dimensionValue}][]/
 *                  └─ ${velocity}${sampleVariant}-${micChain}.${ext}
 * Example:
 *     instruments/
 *       └─ NakedDrums by WilkinsonAudio [CC-BY-4.0]/
 *          └─ kick-22-YamahaRecordingCustom/
 *              └─ center/
 *                  └─ 127a-dim.flac
 *          └─ hihat-14/
 *              └─ bow-pdl_opn_cls043-zon_bel_edg085/
 *                  └─ 043a-oni.flac
 *                  └─ 085a-oni.flac
 *                  ...
 *              └─ bow-pdl_opn_cls085/
 *                  ...
 *              └─ edge-pdl_opn_cls043/
 *              ...
 *          └─ snare-X1/
 *              └─ center-zon_ctr_rim043/
 *                  └─ 043a-dtm.flac
 *                  └─ 085a-dtm.flac
 *              └─ center-zon_ctr_rim085/
 *                  ...
 *              └─ edge/
 *                  ...
 *              └─ rimshot/
 *                  ...
 *
 * Fields:
 *     kitInfo         = KitName by AuthorName [License]
 *     instrumentName  = one work like kick snare tom hihat ride crash splash china stax cowbell tambourine shaker timbale octoban woodblock clap ...
 *     instrumentType  = drum kick tom snare cymbal hihat ride percussion
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
 //TODO: in order to detect naming typos, after kit parsed, make summary on code/value usage, for example: 
        micCide: oni: 39, omi: 1, dtm: 40;
        sampleVariant: a: 40, b: 39, n: 1;
        velocity: 1: 2, 27: 20, 127: 18;
        etc...
        to collect values/codes use something like StringIdRegistry class from Helpers.h
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
        sampleVariants.push_back(std::move(entry));
    }

    bool isEmpty() const {
        return sampleVariants.empty();
    }
};

/**
 * ArticulationSpecs
 * Represents an articulation (e.g., "center", "edge") with multiple velocity levels.
 */
class ArticulationSpecs {
public:
    struct Dimension {
        juce::String code;
        std::vector<uint8_t> values;
    };

    juce::String name;
    std::vector<Dimension> dimensions;

    std::vector<std::unique_ptr<VelocityLayerSpecs>> velocityLayers;

    InstrumentSpecs* parent = nullptr;

    explicit ArticulationSpecs(InstrumentSpecs* parent, const juce::String& name)
        : parent(parent)
        , name(name) {
    }

    const std::vector<std::unique_ptr<VelocityLayerSpecs>>& getVelocityLayers() const { return velocityLayers; }

    bool isEmpty() const {
        return velocityLayers.empty() || std::all_of(velocityLayers.begin(), velocityLayers.end(), [](const auto& v) {
            return v->isEmpty();
        });
    }

    void addDimensionValue(const juce::String& code, uint8_t value) {
        auto* dim = getOrAddDimension(code);
        if (std::find(dim->values.begin(), dim->values.end(), value) == dim->values.end())
            dim->values.push_back(value);
    }

    Dimension* addDimension(const juce::String& code) {
        this->dimensions.push_back(Dimension{code, {}});
        return &this->dimensions.back();
    }

    Dimension* getOrAddDimension(const juce::String& code) {
        auto exist = this->findDimension(code);
        return (exist != nullptr) ? exist : this->addDimension(code);
    }

    Dimension* findDimension(const juce::String& code) {
        for (auto& dim : this->dimensions)
            if (dim.code == code)
                return &dim;
        return nullptr;
    }

    VelocityLayerSpecs* addVelocityLayer(uint8_t velocity) {
        auto newLevel = std::make_unique<VelocityLayerSpecs>(velocity);
        newLevel->parentArticulation = this;
        auto* ptr = newLevel.get();
        velocityLayers.push_back(std::move(newLevel));
        return ptr;
    }

    VelocityLayerSpecs* findVelocityLayer(uint8_t velocity) {
        for (auto& existing : velocityLayers)
            if (existing->velocity == velocity)
                return existing.get();
        return nullptr;
    }

    VelocityLayerSpecs* getOrAddVelocityLayer(uint8_t velocity) {
        auto exist = findVelocityLayer(velocity);
        return (exist != nullptr) ? exist : addVelocityLayer(velocity);
    }

    void sortVelocityLayers() {
        std::sort(velocityLayers.begin(), velocityLayers.end(), [](const auto& a, const auto& b) {
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

    //TODO: add global config for custom names mapping (like "chopper">"cymbal" "rototom">"tom" "vibraslap">"percussion" etc or maybe better use StringIdRegistry for new types?
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

    ArticulationSpecs* findArticulation(const juce::String& name) const {
        for (const auto& item : articulations)
            if (item->name == name)
                return item.get();
        return nullptr;
    }

    ArticulationSpecs* getOrAddArticulation(const juce::String& name) {
        auto existing = findArticulation(name);
        if (existing != nullptr)
            return existing;
        auto newArt = std::make_unique<ArticulationSpecs>(this, name);
        auto* ptr = newArt.get();
        articulations.push_back(std::move(newArt));
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