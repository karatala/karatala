#include "KitHomePanelHelpers.h"

namespace KitHomePanelHelpers {

juce::String safePresetFileName(juce::String name) {
    return name.trim().replaceCharacters("\\/:|*?<>\"", "_________");
}

const std::vector<TypeChoice>& editableTypeChoices() {
    static const std::vector<TypeChoice> choices {
        { KitLib::InstrumentSpec::Type::Unknown, "auto" },
        { KitLib::InstrumentSpec::Type::Kick, "kick" },
        { KitLib::InstrumentSpec::Type::Snare, "snare" },
        { KitLib::InstrumentSpec::Type::Tom, "tom" },
        { KitLib::InstrumentSpec::Type::Octoban, "octoban" },
        { KitLib::InstrumentSpec::Type::Rototom, "rototom" },
        { KitLib::InstrumentSpec::Type::Timbale, "timbale" },
        { KitLib::InstrumentSpec::Type::Timpani, "timpani" },
        { KitLib::InstrumentSpec::Type::Conga, "conga" },
        { KitLib::InstrumentSpec::Type::Bongo, "bongo" },
        { KitLib::InstrumentSpec::Type::Djembe, "djembe" },
        { KitLib::InstrumentSpec::Type::Cajon, "cajon" },
        { KitLib::InstrumentSpec::Type::Taiko, "taiko" },
        { KitLib::InstrumentSpec::Type::Surdo, "surdo" },
        { KitLib::InstrumentSpec::Type::Mridangam, "mridangam" },
        { KitLib::InstrumentSpec::Type::TalkingDrum, "talking_drum" },
        { KitLib::InstrumentSpec::Type::HiHat, "hihat" },
        { KitLib::InstrumentSpec::Type::Ride, "ride" },
        { KitLib::InstrumentSpec::Type::Crash, "crash" },
        { KitLib::InstrumentSpec::Type::Splash, "splash" },
        { KitLib::InstrumentSpec::Type::China, "china" },
        { KitLib::InstrumentSpec::Type::Bell, "bell" },
        { KitLib::InstrumentSpec::Type::Stack, "stack" },
        { KitLib::InstrumentSpec::Type::Chopper, "chopper" },
        { KitLib::InstrumentSpec::Type::Gong, "gong" },
        { KitLib::InstrumentSpec::Type::Karatala, "karatala" },
        { KitLib::InstrumentSpec::Type::Cowbell, "cowbell" },
        { KitLib::InstrumentSpec::Type::Tambourine, "tambourine" },
        { KitLib::InstrumentSpec::Type::Shaker, "shaker" },
        { KitLib::InstrumentSpec::Type::Maracas, "maracas" },
        { KitLib::InstrumentSpec::Type::Cabasa, "cabasa" },
        { KitLib::InstrumentSpec::Type::Guiro, "guiro" },
        { KitLib::InstrumentSpec::Type::Claves, "claves" },
        { KitLib::InstrumentSpec::Type::Agogo, "agogo" },
        { KitLib::InstrumentSpec::Type::WoodBlock, "wood_block" },
        { KitLib::InstrumentSpec::Type::TempleBlock, "temple_block" },
        { KitLib::InstrumentSpec::Type::Triangle, "triangle" },
        { KitLib::InstrumentSpec::Type::Vibraslap, "vibraslap" },
        { KitLib::InstrumentSpec::Type::Castanets, "castanets" },
        { KitLib::InstrumentSpec::Type::SleighBells, "sleigh_bells" },
        { KitLib::InstrumentSpec::Type::Whip, "whip" },
        { KitLib::InstrumentSpec::Type::Ratchet, "ratchet" },
    };
    return choices;
}

int typeComboIdFor(KitLib::InstrumentSpec::Type type) {
    const auto& choices = editableTypeChoices();
    for (size_t idx = 0; idx < choices.size(); ++idx) {
        if (choices[idx].type == type)
            return static_cast<int>(idx + 1);
    }

    return 1;
}

KitLib::InstrumentSpec::Type typeFromComboId(int selectedId) {
    const auto& choices = editableTypeChoices();
    const int idx = selectedId - 1;
    if (idx < 0 || idx >= static_cast<int>(choices.size()))
        return KitLib::InstrumentSpec::Type::Unknown;

    return choices[static_cast<size_t>(idx)].type;
}

std::optional<DirectMicOutPolicy> policyFromComboId(int selectedId) {
    switch (selectedId) {
        case 2: return DirectMicOutPolicy::Own;
        case 3: return DirectMicOutPolicy::Auto;
        case 4: return DirectMicOutPolicy::Shared;
        default: return std::nullopt;
    }
}

int comboIdForPolicy(std::optional<DirectMicOutPolicy> policy) {
    if (!policy.has_value())
        return 1;

    switch (*policy) {
        case DirectMicOutPolicy::Own: return 2;
        case DirectMicOutPolicy::Auto: return 3;
        case DirectMicOutPolicy::Shared: return 4;
    }

    return 1;
}

} // namespace KitHomePanelHelpers
