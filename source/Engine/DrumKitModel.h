#pragma once

#include <juce_core/juce_core.h>
#include <regex>
#include <vector>
#include "../Config/Helpers.h"

using namespace juce;

/**
 * DrumKitModel.h
 *
 * Class Hierarchy:
 * InstrumentManager
 *   └─ DrumKit
 *      └─ DrumKitInstrument
 *          └─ InstrumentArticulation
 *              └─ ArticulationVelocityLayer
 *                  └─ LayerSampleVariant
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

// Forward declarations
class DrumKit;
class DrumKitInstrument;
class InstrumentArticulation;
class ArticulationVelocityLayer;
class LayerSampleVariant;

/**
 * MicDescriptor
 * Represents a parsed mic token from the micChain.
 * Example: "ons" → {type=Overhead, position=Normak, format=Stereo, variant=""}
 * Example: "dtm1" → {type=Direct, position=Top, format=mono, variant="1"}
 * Example: "rfi" → {type=Room, position=Far, format=Mid-Side, variant=""}
 */
struct MicDescriptor {
    enum class Type { Unknown = 0,
        Direct = 'd',
        Overhead = 'o',
        Room = 'r'
    };
    enum class Position { Unknown = 0,
        Sub = 'u',
        Side = 's',
        Top = 't',
        Bottom = 'b',
        In = 'i',
        Out = 'o',

        Normal = 'n', // (overhead)
        Wide = 'w', // (overhead)
        Close = 'c', // (room)
        Far = 'f', // (room)
    };
    enum class Format { Unknown = 0,
        Mono = 'm',
        Stereo = 's',
        MidSide = 'i'
    };

    Type type = Type::Unknown;
    Position position = Position::Unknown;
    Format format = Format::Unknown;
    String variant;
    String raw;
    String code;

    MicDescriptor() = default;

    static ParseResult<MicDescriptor> tryCreateFromCode(const String& code);

    /**
     * Parse a full mic chain string (e.g., "dim_dom_ons_rfs") into a vector of MicDescriptors.
     */
    static ParseResult<std::vector<MicDescriptor>> parseMicCodesChain(const String& codeChain);

    static Type typeFromChar(juce_wchar c) {
        switch (c) {
            case 'd': return Type::Direct;
            case 'o': return Type::Overhead;
            case 'r': return Type::Room;
            default: return Type::Unknown;
        }
    }

    static Position positionFromChar(juce_wchar c) {
        switch (c) {
            case 'u': return Position::Sub;
            case 's': return Position::Side;
            case 't': return Position::Top;
            case 'b': return Position::Bottom;
            case 'i': return Position::In;
            case 'o': return Position::Out;
            case 'n': return Position::Normal;
            case 'w': return Position::Wide;
            case 'c': return Position::Close;
            case 'f': return Position::Far;
            default: return Position::Unknown;
        }
    }

    static Format formatFromChar(juce_wchar c) {
        switch (c) {
            case 'm': return Format::Mono;
            case 's': return Format::Stereo;
            case 'i': return Format::MidSide;
            default: return Format::Unknown;
        }
    }

    bool isValid() const {
        return type != Type::Unknown && position != Position::Unknown && format != Format::Unknown;
    }

    int getChannelCount() const {
        switch (format) {
            case Format::Stereo: case Format::MidSide: return 2;
            case Format::Mono: default: return 1;
        }
    }

    String getDescription() const {
        StringArray parts;
        switch (type) {
            case Type::Direct: parts.add("Direct"); break;
            case Type::Overhead: parts.add("Overhead"); break;
            case Type::Room: parts.add("Room"); break;
            default: parts.add("?"); break;
        }

        switch (position) {
            case Position::Sub: parts.add("Sub"); break;
            case Position::Side: parts.add("Side"); break;
            case Position::Top: parts.add("Top"); break;
            case Position::Bottom: parts.add("Bottom"); break;
            case Position::In: parts.add("In"); break;
            case Position::Out: parts.add("Out"); break;
            case Position::Normal: parts.add("Normal"); break;
            case Position::Wide: parts.add("Wide"); break;
            case Position::Close: parts.add("Close"); break;
            case Position::Far: parts.add("Far"); break;
            default: parts.add("?"); break;
        }

        switch (format) {
            case Format::Mono: parts.add("Mono"); break;
            case Format::Stereo: parts.add("Stereo"); break;
            case Format::MidSide: parts.add("MidSide"); break;
            default: parts.add("?"); break;
        }

        if (!variant.isEmpty())
            parts.add(variant);
        return parts.joinIntoString(" ");
    }
};

/**
 * LayerSampleVariant
 * Represents a single audio file for a specific round-robin variation.
 */
class LayerSampleVariant {
public:
    uint8_t id;
    juce_wchar variantLetter; // 'a'-'z'
    File sampleFile;
    String sampleFileName;

    //TODO: use optional metadata cached at parse time (lazy-loaded from file)
    int64 fileSizeBytes = 0;
    double sampleRate = 0.0;
    int bitDepth = 0;
    int numChannels = 0;
    int64 numSamples = 0;

    MicDescriptor micDescriptor;
    ArticulationVelocityLayer* parentVelocityLayer = nullptr;

    explicit LayerSampleVariant(const File& file, juce_wchar variantLetter, const MicDescriptor& micDescriptor)
        : sampleFile(file)
        , variantLetter(variantLetter)
        , sampleFileName(file.getFileName())
        , micDescriptor(micDescriptor)
    {
    }

    bool isValid() const { return sampleFile.existsAsFile(); }

    /**
     * Converts a single letter a-z (case-insensitive) to 1-26.
     * @return uint8_t 1-26 or 0 if invalid
     */
    static uint8_t letterToIdx(const juce::String& letter) { return letterToIdx(letter[0]); }
    static uint8_t letterToIdx(const juce_wchar& letter) {
        if (letter == '\0') return 0;
        juce_wchar c = std::tolower(letter);
        return (c >= 'a' && c <= 'z') ? uint8_t(c - 'a' + 1) : 0;
    }
};

/**
 * ArticulationVelocityLayer
 * Represents a specific velocity level with multiple round-robin samples.
 */
class ArticulationVelocityLayer {
public:
    uint8_t velocity;
    InstrumentArticulation* parentArticulation = nullptr;
    std::vector<std::unique_ptr<LayerSampleVariant>> sampleVariants;


    explicit ArticulationVelocityLayer(uint8_t velocity)
        : velocity(velocity) {}

    void addSampleVariant(std::unique_ptr<LayerSampleVariant> entry) {
        entry->parentVelocityLayer = this;
        sampleVariants.push_back(std::move(entry));
    }

    bool isEmpty() const {
        return sampleVariants.empty();
    }
};

/**
 * InstrumentArticulation
 * Represents an articulation (e.g., "center", "edge") with multiple velocity levels.
 */
class InstrumentArticulation {
public:
    struct Dimension {
        String code;
        std::vector<uint8_t> values;
    };

    String name;
    std::vector<Dimension> dimensions;

    std::vector<std::unique_ptr<ArticulationVelocityLayer>> velocityLayers;

    DrumKitInstrument* parent = nullptr;

    explicit InstrumentArticulation(DrumKitInstrument* parent, const String& name)
        : parent(parent)
        , name(name) {
    }

    const std::vector<std::unique_ptr<ArticulationVelocityLayer>>& getVelocityLayers() const { return velocityLayers; }

    bool isEmpty() const {
        return velocityLayers.empty() || std::all_of(velocityLayers.begin(), velocityLayers.end(), [](const auto& v) {
            return v->isEmpty();
        });
    }

    void addDimensionValue(const String& code, uint8_t value) {
        auto* dim = getOrAddDimension(code);
        if (std::find(dim->values.begin(), dim->values.end(), value) == dim->values.end())
            dim->values.push_back(value);
    }

    Dimension* addDimension(const String& code) {
        this->dimensions.push_back(Dimension{code, {}});
        return &this->dimensions.back();
    }

    Dimension* getOrAddDimension(const String& code) {
        auto exist = this->findDimension(code);
        return (exist != nullptr) ? exist : this->addDimension(code);
    }

    Dimension* findDimension(const String& code) {
        for (auto& dim : this->dimensions)
            if (dim.code == code)
                return &dim;
        return nullptr;
    }

    ArticulationVelocityLayer* addVelocityLayer(uint8_t velocity) {
        auto newLevel = std::make_unique<ArticulationVelocityLayer>(velocity);
        newLevel->parentArticulation = this;
        auto* ptr = newLevel.get();
        velocityLayers.push_back(std::move(newLevel));
        return ptr;
    }

    ArticulationVelocityLayer* findVelocityLayer(uint8_t velocity) {
        for (auto& existing : velocityLayers)
            if (existing->velocity == velocity)
                return existing.get();
        return nullptr;
    }

    ArticulationVelocityLayer* getOrAddVelocityLayer(uint8_t velocity) {
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
 * DrumKitInstrument
 * Represents a drum/percussion instrument with articulations and metadata.
 */
class DrumKitInstrument {
public:

    enum class Type { Unknown, Percussion, Drum, Cymbal, Kick, Tom, Snare, Ride, HiHat, Crash, Splash, China }; //TODO: probably need to leave less types, like only percussion, drum, cymbal, hihat, and rely on available articulations
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
        for (auto& e : typeTable)
            if (e.type == type)  return e.name;
        return "unknown";
    }
    static Type typeFromString(const juce::String& name) {
        //TODO: add global config for custom names mapping (like "chopper">"cymbal" "rototom">"tom" "vibraslap">"percussion" etc or maaybe better use StringIdRegistry for new types?
        for (auto& e : typeTable)
            if (name == e.name) return e.type;
        return Type::Unknown;
    }
    
    struct MetaInfo {
        File rootDir;
        String code;
        String name;
        DrumKitInstrument::Type type = DrumKitInstrument::Type::Unknown;
        String size = "";
        String description = "";
    };
    struct SampleFileInfo {
        String type;
        String articulation;
        uint8_t velocity;
        String sampleVariant;
        std::vector<MicDescriptor> micDescriptors;
        String extension;
        File file;
    };



    DrumKit* parentKit = nullptr;

    MetaInfo meta;
    double tuning = 0;
    int sampleCount = 0;

    std::vector<std::unique_ptr<InstrumentArticulation>> articulations;

    DrumKitInstrument(DrumKit* kit, const MetaInfo& meta)
        : parentKit(kit)
        , meta(meta) {
    }

    static ParseResult<MetaInfo> parseInfo(const File& dir);

    bool isAudioFile(const File& file);

    /**
     * Scan all samples from an instrument folder.
     * Parses each audio file and populates the instrument's articulation tree.
     *
     * @return number of samples loaded
     */
    int scanInstrumentSamples();

    /**
     * Parse a sample filename.
     * Expected format: ${instrumentType}-${articulation}-${velocity}${sampleVariant}-${micChain}.${ext}
     * Example: "kick-center-1a-dim.flac"
     */
    static ParseResult<DrumKitInstrument::SampleFileInfo> parseSampleFile(const File& file);

    InstrumentArticulation* findArticulation(const String& name) const {
        for (const auto& item : articulations) {
            if (item->name == name) {
                return item.get();
            }
        }
        return nullptr;
    }

    InstrumentArticulation* getOrAddArticulation(const String& name) {
        auto existing = findArticulation(name);
        if (existing != nullptr) {
            return existing;
        }

        auto newArt = std::make_unique<InstrumentArticulation>(this, name);
        auto* ptr = newArt.get();
        articulations.push_back(std::move(newArt));
        return ptr;
    }

    void sortArticulations() {
        std::sort(articulations.begin(), articulations.end(), [](const auto& a, const auto& b) {
            return a->name < b->name;
        });
    }

    bool isEmpty() const {
        return articulations.empty() || std::all_of(articulations.begin(), articulations.end(), [](const auto& a) {
            return a->isEmpty();
        });
    }
};

/**
 * DrumKit
 * Represents a complete drum kit with metadata and instruments.
 */
class DrumKit {
public:
    struct MetaInfo {
        File rootDir;
        String code;
        String name;
        String author;
        String license;
    };

    MetaInfo meta;
    std::unordered_map<juce::String, std::unique_ptr<DrumKitInstrument>> instruments;

    int64 lastModifiedTime = 0;

    DrumKit(const MetaInfo& meta)
        : meta(meta) {
    }

    static ParseResult<MetaInfo> parseInfo(const File& dir);

    int scanInstruments();

    DrumKitInstrument* findInstrument(const String& code);

    std::vector<DrumKitInstrument*> getInstrumentsByType(DrumKitInstrument::Type type) const;

    // std::vector<DrumKitInstrument*> findInstruments(const String& name) const;

    // void sortInstruments();

    bool hasChanged() const;

    void updateModifiedTime();

    bool isEmpty() const {
        return false;
        //return instruments.empty() || std::all_of(instruments.begin(), instruments.end(), [](const auto& i) {
        //    return i->isEmpty();
        //});
    }
};
