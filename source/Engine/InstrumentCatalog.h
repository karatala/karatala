#pragma once

#include "../Config/Helpers.h"
#include "../Config/YamlHelper.h"
#include "KitLibModel.h"
#include "RoutingTypes.h"

#include <algorithm>
#include <map>
#include <optional>
#include <vector>

enum class InstrumentFamily {
    Unknown,
    Drum,
    Cymbal,
    Other,
};

inline std::optional<InstrumentFamily> instrumentFamilyFromString(const juce::String& raw) {
    const auto value = raw.trim().toLowerCase();
    if (value == "drum") return InstrumentFamily::Drum;
    if (value == "cymbal") return InstrumentFamily::Cymbal;
    if (value == "other") return InstrumentFamily::Other;
    return std::nullopt;
}

inline juce::String instrumentFamilyToString(InstrumentFamily family) {
    switch (family) {
        case InstrumentFamily::Drum:   return "drum";
        case InstrumentFamily::Cymbal: return "cymbal";
        case InstrumentFamily::Other:  return "other";
        case InstrumentFamily::Unknown:
            break;
    }
    return "unknown";
}

class InstrumentCatalog {
public:
    struct KindInfo {
        InstrumentFamily family = InstrumentFamily::Unknown;
        std::optional<DirectMicOutPolicy> directMicOuts;
    };

    static InstrumentCatalog makeDefault() {
        InstrumentCatalog catalog;

        auto add = [&catalog](KitLib::InstrumentSpec::Type type, InstrumentFamily family, std::optional<DirectMicOutPolicy> directMicOuts = std::nullopt) {
            catalog.kinds[type] = { family, directMicOuts };
        };

        add(KitLib::InstrumentSpec::Type::Kick, InstrumentFamily::Drum, DirectMicOutPolicy::Own);
        add(KitLib::InstrumentSpec::Type::Tom, InstrumentFamily::Drum, DirectMicOutPolicy::Own);
        add(KitLib::InstrumentSpec::Type::Snare, InstrumentFamily::Drum, DirectMicOutPolicy::Own);
        add(KitLib::InstrumentSpec::Type::Octoban, InstrumentFamily::Drum, DirectMicOutPolicy::Auto);
        add(KitLib::InstrumentSpec::Type::Rototom, InstrumentFamily::Drum, DirectMicOutPolicy::Auto);
        add(KitLib::InstrumentSpec::Type::Timbale, InstrumentFamily::Drum, DirectMicOutPolicy::Auto);
        add(KitLib::InstrumentSpec::Type::Timpani, InstrumentFamily::Drum);
        add(KitLib::InstrumentSpec::Type::Conga, InstrumentFamily::Drum);
        add(KitLib::InstrumentSpec::Type::Bongo, InstrumentFamily::Drum);
        add(KitLib::InstrumentSpec::Type::Djembe, InstrumentFamily::Drum);
        add(KitLib::InstrumentSpec::Type::Cajon, InstrumentFamily::Drum);
        add(KitLib::InstrumentSpec::Type::Taiko, InstrumentFamily::Drum);
        add(KitLib::InstrumentSpec::Type::Surdo, InstrumentFamily::Drum);
        add(KitLib::InstrumentSpec::Type::Mridangam, InstrumentFamily::Drum);
        add(KitLib::InstrumentSpec::Type::TalkingDrum, InstrumentFamily::Drum);

        add(KitLib::InstrumentSpec::Type::HiHat, InstrumentFamily::Cymbal, DirectMicOutPolicy::Own);
        add(KitLib::InstrumentSpec::Type::Ride, InstrumentFamily::Cymbal, DirectMicOutPolicy::Own);
        add(KitLib::InstrumentSpec::Type::Crash, InstrumentFamily::Cymbal);
        add(KitLib::InstrumentSpec::Type::Splash, InstrumentFamily::Cymbal);
        add(KitLib::InstrumentSpec::Type::China, InstrumentFamily::Cymbal);
        add(KitLib::InstrumentSpec::Type::Bell, InstrumentFamily::Cymbal);
        add(KitLib::InstrumentSpec::Type::Stack, InstrumentFamily::Cymbal);
        add(KitLib::InstrumentSpec::Type::Chopper, InstrumentFamily::Cymbal);
        add(KitLib::InstrumentSpec::Type::Gong, InstrumentFamily::Cymbal);
        add(KitLib::InstrumentSpec::Type::Karatala, InstrumentFamily::Cymbal);

        add(KitLib::InstrumentSpec::Type::Cowbell, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Tambourine, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Shaker, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Maracas, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Cabasa, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Guiro, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Claves, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Agogo, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::WoodBlock, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::TempleBlock, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Triangle, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Vibraslap, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Castanets, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::SleighBells, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Whip, InstrumentFamily::Other);
        add(KitLib::InstrumentSpec::Type::Ratchet, InstrumentFamily::Other);

        catalog.aliases["cowbel"] = KitLib::InstrumentSpec::Type::Cowbell;
        catalog.aliases["stax"] = KitLib::InstrumentSpec::Type::Stack;
        catalog.aliases["hi_hat"] = KitLib::InstrumentSpec::Type::HiHat;
        catalog.aliases["woodblock"] = KitLib::InstrumentSpec::Type::WoodBlock;
        return catalog;
    }

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
    static ParseResult<InstrumentCatalog> parseYaml(const YamlHelper::YamlNode& node, InstrumentCatalog base = makeDefault()) {
        ParseResult<InstrumentCatalog> result;

        if (!node.isMap()) {
            result.value = base;
            return result;
        }

        const auto& kindsNode = node["kinds"];
        if (kindsNode.isMap()) {
            for (const auto& kindName : kindsNode.keys()) {
                auto kindType = base.resolveKind(kindName);
                if (kindType == KitLib::InstrumentSpec::Type::Unknown) {
                    result.addWarning("instrumentCatalog: unknown kind '" + kindName + "'");
                    continue;
                }

                auto info = base.kindInfo(kindType).value_or(KindInfo {});
                const auto& kindNode = kindsNode[kindName];
                if (kindNode.isMap()) {
                    const auto familyText = kindNode["family"].asString().trim();
                    if (familyText.isNotEmpty()) {
                        auto family = instrumentFamilyFromString(familyText);
                        if (family.has_value())
                            info.family = *family;
                        else
                            result.addWarning("instrumentCatalog: invalid family '" + familyText + "' for kind '" + kindName + "'");
                    }

                    const auto directMicOutsText = kindNode["directMicOuts"].asString().trim();
                    if (directMicOutsText.isNotEmpty()) {
                        auto policy = directMicOutPolicyFromString(directMicOutsText);
                        if (policy.has_value())
                            info.directMicOuts = *policy;
                        else
                            result.addWarning("instrumentCatalog: invalid directMicOuts '" + directMicOutsText + "' for kind '" + kindName + "'");
                    }
                } else if (kindNode.isScalar() && kindNode.asString().isNotEmpty()) {
                    auto family = instrumentFamilyFromString(kindNode.asString());
                    if (family.has_value())
                        info.family = *family;
                    else
                        result.addWarning("instrumentCatalog: invalid family '" + kindNode.asString() + "' for kind '" + kindName + "'");
                }
                base.kinds[kindType] = info;
            }
        }

        const auto& aliasesNode = node["aliases"];
        if (aliasesNode.isMap()) {
            for (const auto& alias : aliasesNode.keys()) {
                const auto targetText = aliasesNode[alias].asString().trim();
                const auto targetType = base.resolveKind(targetText);
                if (targetType == KitLib::InstrumentSpec::Type::Unknown) {
                    result.addWarning("instrumentCatalog: alias '" + alias + "' targets unknown kind '" + targetText + "'");
                    continue;
                }
                base.aliases[alias.trim().toLowerCase()] = targetType;
            }
        }

        result.value = base;
        return result;
    }

    KitLib::InstrumentSpec::Type resolveKind(const juce::String& raw) const {
        const auto key = raw.trim().toLowerCase();
        auto aliasIt = this->aliases.find(key);
        if (aliasIt != this->aliases.end())
            return aliasIt->second;
        return KitLib::InstrumentSpec::typeFromString(key);
    }

    std::optional<KindInfo> kindInfo(KitLib::InstrumentSpec::Type type) const {
        auto it = this->kinds.find(type);
        if (it == this->kinds.end())
            return std::nullopt;
        return it->second;
    }

    InstrumentFamily familyFor(KitLib::InstrumentSpec::Type type) const {
        auto info = this->kindInfo(type);
        return info.has_value() ? info->family : InstrumentFamily::Unknown;
    }

    std::optional<DirectMicOutPolicy> defaultDirectMicOutsFor(KitLib::InstrumentSpec::Type type) const {
        auto info = this->kindInfo(type);
        if (!info.has_value())
            return std::nullopt;
        return info->directMicOuts;
    }

private:
    std::map<KitLib::InstrumentSpec::Type, KindInfo> kinds;
    std::map<juce::String, KitLib::InstrumentSpec::Type> aliases;
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
    KitLib::InstrumentSpec::Type instrumentType = KitLib::InstrumentSpec::Type::Unknown;
    InstrumentFamily family = InstrumentFamily::Unknown;
    std::vector<KitLib::InstrumentSpec::Type> listTypes;
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
inline MicInstrumentSelector parseMicInstrumentSelector(const juce::String& raw, const InstrumentCatalog& catalog) {
    MicInstrumentSelector selector;
    selector.raw = raw;
    const auto value = raw.trim().toLowerCase();
    selector.text = value;

    if (value.isEmpty()) {
        selector.kind = MicInstrumentSelector::Kind::EmptyCommon;
        return selector;
    }

    if (value.contains(",")) {
        selector.kind = MicInstrumentSelector::Kind::CommaList;
        auto tokens = juce::StringArray::fromTokens(value, ",", "");
        tokens.removeEmptyStrings();
        for (auto token : tokens) {
            token = token.trim();
            auto type = catalog.resolveKind(token);
            if (type != KitLib::InstrumentSpec::Type::Unknown) {
                selector.listTypes.push_back(type);
                continue;
            }
            auto family = instrumentFamilyFromString(token);
            if (family.has_value())
                selector.listFamilies.push_back(*family);
        }
        return selector;
    }

    if (value.endsWithChar('*')) {
        selector.kind = MicInstrumentSelector::Kind::WildcardPrefix;
        selector.text = value.dropLastCharacters(1);
        return selector;
    }

    auto family = instrumentFamilyFromString(value);
    if (family.has_value()) {
        selector.kind = MicInstrumentSelector::Kind::CatalogFamily;
        selector.family = *family;
        return selector;
    }

    auto type = catalog.resolveKind(value);
    if (type != KitLib::InstrumentSpec::Type::Unknown) {
        selector.kind = MicInstrumentSelector::Kind::CatalogKind;
        selector.instrumentType = type;
        return selector;
    }

    selector.kind = MicInstrumentSelector::Kind::ExactSlotId;
    return selector;
}

inline MicSelectorPriority matchMicInstrumentSelector(const MicInstrumentSelector& selector,
                                                      const juce::String& slotId,
                                                      KitLib::InstrumentSpec::Type slotType,
                                                      const InstrumentCatalog& catalog) {
    switch (selector.kind) {
        case MicInstrumentSelector::Kind::EmptyCommon:
            return MicSelectorPriority::EmptyCommon;
        case MicInstrumentSelector::Kind::ExactSlotId:
            return selector.text == slotId.trim().toLowerCase() ? MicSelectorPriority::ExactSlotId : MicSelectorPriority::None;
        case MicInstrumentSelector::Kind::WildcardPrefix: {
            const auto normalizedSlotId = slotId.trim().toLowerCase();
            const auto kindName = KitLib::InstrumentSpec::typeToString(slotType).toLowerCase();
            return (normalizedSlotId.startsWith(selector.text) || kindName.startsWith(selector.text)) ? MicSelectorPriority::WildcardPrefix : MicSelectorPriority::None;
        }
        case MicInstrumentSelector::Kind::CatalogKind:
            return selector.instrumentType == slotType ? MicSelectorPriority::Kind : MicSelectorPriority::None;
        case MicInstrumentSelector::Kind::CommaList:
            if (vectorContains(selector.listTypes, slotType))
                return MicSelectorPriority::CommaList;
            if (vectorContains(selector.listFamilies, catalog.familyFor(slotType)))
                return MicSelectorPriority::CommaList;
            return MicSelectorPriority::None;
        case MicInstrumentSelector::Kind::CatalogFamily:
            return selector.family == catalog.familyFor(slotType) ? MicSelectorPriority::Family : MicSelectorPriority::None;
    }

    return MicSelectorPriority::None;
}

inline bool directMicSpecsMatchExactly(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic) {
    return sampleMic.type == KitLib::MicSpec::Type::Direct
        && outputMic.type == KitLib::MicSpec::Type::Direct
        && sampleMic.position == outputMic.position
        && sampleMic.format == outputMic.format
        && sampleMic.variant == outputMic.variant
        && sampleMic.channels == outputMic.channels;
}

inline bool directMicSpecsCanUseGroupOutput(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic) {
    if (sampleMic.type != KitLib::MicSpec::Type::Direct || outputMic.type != KitLib::MicSpec::Type::Direct)
        return false;

    if (sampleMic.variant != outputMic.variant)
        return false;

    if (sampleMic.channels == outputMic.channels && sampleMic.format == outputMic.format)
        return true;

    return sampleMic.format == KitLib::MicSpec::Format::Mono
        && sampleMic.channels == 1
        && outputMic.format == KitLib::MicSpec::Format::Stereo
        && outputMic.channels == 2;
}
