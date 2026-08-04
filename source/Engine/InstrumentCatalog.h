#pragma once

#include "../Config/Parsing.h"
#include "../Config/YamlHelper.h"
#include "InstrumentType.h"
#include "KitLibConsts.h"
#include "RoutingTypes.h"

#include <map>
#include <optional>
#include <vector>

enum class InstrumentFamily {
    Unknown,
    Drum,
    Cymbal,
    Other,
};

std::optional<InstrumentFamily> instrumentFamilyFromString(const juce::String& raw);
juce::String instrumentFamilyToString(InstrumentFamily family);

class InstrumentCatalog {
public:
    struct KindInfo {
        InstrumentFamily family = InstrumentFamily::Unknown;
        std::optional<DirectMicOutPolicy> directMicOuts;
    };

    static InstrumentCatalog makeDefault();

    /**
     * Parses instrument catalog YAML.
     *
     * Example:
     *   kinds:
     *     kick: { family: drum, directMicOuts: own }
     *     crash: cymbal
     *   aliases:
     *     stax: stack
     */
    static ParseResult<InstrumentCatalog> parseYaml(const YamlHelper::YamlNode& node, InstrumentCatalog base = makeDefault());

    KitLib::InstrumentType resolveKind(const juce::String& raw) const;
    std::optional<KindInfo> kindInfo(KitLib::InstrumentType type) const;
    InstrumentFamily familyFor(KitLib::InstrumentType type) const;
    std::optional<DirectMicOutPolicy> defaultDirectMicOutsFor(KitLib::InstrumentType type) const;

private:
    std::map<KitLib::InstrumentType, KindInfo> kinds;
    std::map<juce::String, KitLib::InstrumentType> aliases;
};

enum class MicSelectorPriority {
    None = 0,
    EmptyCommon = 1,
    Family = 2,
    CommaList = 3,
    Kind = 4,
    WildcardPrefix = 5,
    ExactSlotId = 6,
};

struct MicInstrumentSelector {
    enum class Kind {
        EmptyCommon,
        ExactSlotId,
        WildcardPrefix,
        CatalogKind,
        CommaList,
        CatalogFamily,
    };

    Kind kind = Kind::EmptyCommon;
    juce::String raw;
    juce::String text;
    KitLib::InstrumentType instrumentType = KitLib::InstrumentType::Unknown;
    InstrumentFamily family = InstrumentFamily::Unknown;
    std::vector<KitLib::InstrumentType> listTypes;
    std::vector<InstrumentFamily> listFamilies;
};

/**
 * Parses a mic-layout instrument selector.
 *
 * Examples:
 *   "" means common mic.
 *   "snare1" means exact slot id.
 *   "tom*" means slot id or kind starts with tom.
 *   "snare,tom" means a list of catalog kinds or families.
 *   "cymbal" means catalog family.
 */
MicInstrumentSelector parseMicInstrumentSelector(const juce::String& raw, const InstrumentCatalog& catalog);
MicSelectorPriority matchMicInstrumentSelector(const MicInstrumentSelector& selector, const juce::String& slotId, KitLib::InstrumentType slotType, const InstrumentCatalog& catalog);
bool micSpecSelectorsMatchOutput(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic);
bool micSpecsMatchOutput(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic);
bool directMicSpecsMatchExactly(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic);
bool directMicSpecsCanUseGroupOutput(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic);
