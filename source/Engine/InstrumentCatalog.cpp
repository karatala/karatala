#include "InstrumentCatalog.h"

#include "../Config/Helpers.h"

std::optional<InstrumentFamily> instrumentFamilyFromString(const juce::String& raw) {
    const auto value = raw.trim().toLowerCase();
    if (value == "drum") return InstrumentFamily::Drum;
    if (value == "cymbal") return InstrumentFamily::Cymbal;
    if (value == "other") return InstrumentFamily::Other;
    return std::nullopt;
}

juce::String instrumentFamilyToString(InstrumentFamily family) {
    switch (family) {
        case InstrumentFamily::Drum:   return "drum";
        case InstrumentFamily::Cymbal: return "cymbal";
        case InstrumentFamily::Other:  return "other";
        case InstrumentFamily::Unknown: break;
    }
    return "unknown";
}

InstrumentCatalog InstrumentCatalog::makeDefault() {
    InstrumentCatalog catalog;

    auto add = [&catalog](KitLib::InstrumentType type, InstrumentFamily family, std::optional<DirectMicOutPolicy> directMicOuts = std::nullopt) {
        catalog.kinds[type] = { family, directMicOuts };
    };

    add(KitLib::InstrumentType::Kick, InstrumentFamily::Drum, DirectMicOutPolicy::Own);
    add(KitLib::InstrumentType::Tom, InstrumentFamily::Drum, DirectMicOutPolicy::Own);
    add(KitLib::InstrumentType::Snare, InstrumentFamily::Drum, DirectMicOutPolicy::Own);
    add(KitLib::InstrumentType::Octoban, InstrumentFamily::Drum, DirectMicOutPolicy::Auto);
    add(KitLib::InstrumentType::Rototom, InstrumentFamily::Drum, DirectMicOutPolicy::Auto);
    add(KitLib::InstrumentType::Timbale, InstrumentFamily::Drum, DirectMicOutPolicy::Auto);
    add(KitLib::InstrumentType::Timpani, InstrumentFamily::Drum);
    add(KitLib::InstrumentType::Conga, InstrumentFamily::Drum);
    add(KitLib::InstrumentType::Bongo, InstrumentFamily::Drum);
    add(KitLib::InstrumentType::Djembe, InstrumentFamily::Drum);
    add(KitLib::InstrumentType::Cajon, InstrumentFamily::Drum);
    add(KitLib::InstrumentType::Taiko, InstrumentFamily::Drum);
    add(KitLib::InstrumentType::Surdo, InstrumentFamily::Drum);
    add(KitLib::InstrumentType::Mridangam, InstrumentFamily::Drum);
    add(KitLib::InstrumentType::TalkingDrum, InstrumentFamily::Drum);

    add(KitLib::InstrumentType::HiHat, InstrumentFamily::Cymbal, DirectMicOutPolicy::Own);
    add(KitLib::InstrumentType::Ride, InstrumentFamily::Cymbal, DirectMicOutPolicy::Own);
    add(KitLib::InstrumentType::Crash, InstrumentFamily::Cymbal);
    add(KitLib::InstrumentType::Splash, InstrumentFamily::Cymbal);
    add(KitLib::InstrumentType::China, InstrumentFamily::Cymbal);
    add(KitLib::InstrumentType::Bell, InstrumentFamily::Cymbal);
    add(KitLib::InstrumentType::Stack, InstrumentFamily::Cymbal);
    add(KitLib::InstrumentType::Chopper, InstrumentFamily::Cymbal);
    add(KitLib::InstrumentType::Gong, InstrumentFamily::Cymbal);
    add(KitLib::InstrumentType::Karatala, InstrumentFamily::Cymbal);

    add(KitLib::InstrumentType::Cowbell, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Tambourine, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Shaker, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Maracas, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Cabasa, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Guiro, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Claves, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Agogo, InstrumentFamily::Other);
    add(KitLib::InstrumentType::WoodBlock, InstrumentFamily::Other);
    add(KitLib::InstrumentType::TempleBlock, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Triangle, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Vibraslap, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Castanets, InstrumentFamily::Other);
    add(KitLib::InstrumentType::SleighBells, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Whip, InstrumentFamily::Other);
    add(KitLib::InstrumentType::Ratchet, InstrumentFamily::Other);

    catalog.aliases["cowbel"] = KitLib::InstrumentType::Cowbell;
    catalog.aliases["stax"] = KitLib::InstrumentType::Stack;
    catalog.aliases["hi_hat"] = KitLib::InstrumentType::HiHat;
    catalog.aliases["woodblock"] = KitLib::InstrumentType::WoodBlock;
    return catalog;
}

ParseResult<InstrumentCatalog> InstrumentCatalog::parseYaml(const YamlHelper::YamlNode& node, InstrumentCatalog base) {
    ParseResult<InstrumentCatalog> result;

    if (!node.isMap()) {
        result.value = base;
        return result;
    }

    const auto& kindsNode = node["kinds"];
    if (kindsNode.isMap()) {
        for (const auto& kindName : kindsNode.keys()) {
            auto kindType = base.resolveKind(kindName);
            if (kindType == KitLib::InstrumentType::Unknown) {
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
            if (targetType == KitLib::InstrumentType::Unknown) {
                result.addWarning("instrumentCatalog: alias '" + alias + "' targets unknown kind '" + targetText + "'");
                continue;
            }
            base.aliases[alias.trim().toLowerCase()] = targetType;
        }
    }

    result.value = base;
    return result;
}

KitLib::InstrumentType InstrumentCatalog::resolveKind(const juce::String& raw) const {
    const auto key = raw.trim().toLowerCase();
    auto aliasIt = this->aliases.find(key);
    if (aliasIt != this->aliases.end()) return aliasIt->second;
    return KitLib::instrumentTypeFromString(key);
}

std::optional<InstrumentCatalog::KindInfo> InstrumentCatalog::kindInfo(KitLib::InstrumentType type) const {
    auto it = this->kinds.find(type);
    if (it == this->kinds.end()) return std::nullopt;
    return it->second;
}

InstrumentFamily InstrumentCatalog::familyFor(KitLib::InstrumentType type) const {
    auto info = this->kindInfo(type);
    return info.has_value() ? info->family : InstrumentFamily::Unknown;
}

std::optional<DirectMicOutPolicy> InstrumentCatalog::defaultDirectMicOutsFor(KitLib::InstrumentType type) const {
    auto info = this->kindInfo(type);
    if (!info.has_value()) return std::nullopt;
    return info->directMicOuts;
}

MicInstrumentSelector parseMicInstrumentSelector(const juce::String& raw, const InstrumentCatalog& catalog) {
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
            if (type != KitLib::InstrumentType::Unknown) {
                selector.listTypes.push_back(type);
                continue;
            }
            auto family = instrumentFamilyFromString(token);
            if (family.has_value()) selector.listFamilies.push_back(*family);
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
    if (type != KitLib::InstrumentType::Unknown) {
        selector.kind = MicInstrumentSelector::Kind::CatalogKind;
        selector.instrumentType = type;
        return selector;
    }

    selector.kind = MicInstrumentSelector::Kind::ExactSlotId;
    return selector;
}

MicSelectorPriority matchMicInstrumentSelector(const MicInstrumentSelector& selector, const juce::String& slotId, KitLib::InstrumentType slotType, const InstrumentCatalog& catalog) {
    switch (selector.kind) {
        case MicInstrumentSelector::Kind::EmptyCommon: return MicSelectorPriority::EmptyCommon;
        case MicInstrumentSelector::Kind::ExactSlotId: return selector.text == slotId.trim().toLowerCase() ? MicSelectorPriority::ExactSlotId : MicSelectorPriority::None;
        case MicInstrumentSelector::Kind::WildcardPrefix: {
            const auto normalizedSlotId = slotId.trim().toLowerCase();
            const auto kindName = KitLib::instrumentTypeToString(slotType).toLowerCase();
            return (normalizedSlotId.startsWith(selector.text) || kindName.startsWith(selector.text)) ? MicSelectorPriority::WildcardPrefix : MicSelectorPriority::None;
        }
        case MicInstrumentSelector::Kind::CatalogKind: return selector.instrumentType == slotType ? MicSelectorPriority::Kind : MicSelectorPriority::None;
        case MicInstrumentSelector::Kind::CommaList:
            if (vectorContains(selector.listTypes, slotType)) return MicSelectorPriority::CommaList;
            if (vectorContains(selector.listFamilies, catalog.familyFor(slotType))) return MicSelectorPriority::CommaList;
            return MicSelectorPriority::None;
        case MicInstrumentSelector::Kind::CatalogFamily:
            return selector.family == catalog.familyFor(slotType) ? MicSelectorPriority::Family : MicSelectorPriority::None;
    }

    return MicSelectorPriority::None;
}

bool micSpecSelectorsMatchOutput(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic) {
    return (outputMic.type == KitLib::MicSpec::Type::Any || sampleMic.type == outputMic.type)
        && (outputMic.position == KitLib::MicSpec::Position::Any || sampleMic.position == outputMic.position)
        && (outputMic.variant.isEmpty() || sampleMic.variant == outputMic.variant);
}

bool micSpecsMatchOutput(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic) {
    return micSpecSelectorsMatchOutput(sampleMic, outputMic) && sampleMic.format == outputMic.format && sampleMic.channels == outputMic.channels;
}

bool directMicSpecsMatchExactly(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic) {
    return sampleMic.type == KitLib::MicSpec::Type::Direct && micSpecsMatchOutput(sampleMic, outputMic);
}

bool directMicSpecsCanUseGroupOutput(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic) {
    if (sampleMic.type != KitLib::MicSpec::Type::Direct || !micSpecSelectorsMatchOutput(sampleMic, outputMic)) return false;
    if (outputMic.type == KitLib::MicSpec::Type::Any || outputMic.position == KitLib::MicSpec::Position::Any) return micSpecsMatchOutput(sampleMic, outputMic);
    if (sampleMic.channels == outputMic.channels && sampleMic.format == outputMic.format) return true;

    return sampleMic.format == KitLib::MicSpec::Format::Mono && sampleMic.channels == 1
        && outputMic.format == KitLib::MicSpec::Format::Stereo && outputMic.channels == 2;
}

