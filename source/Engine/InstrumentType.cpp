#include "InstrumentType.h"

namespace KitLib {

struct TypeEntry {
    const char* name;
    InstrumentType type;
};

static constexpr TypeEntry typeTable[] = {
    { "percussion", KitLib::InstrumentType::Percussion },
    { "other", KitLib::InstrumentType::Other },
    { "drum", KitLib::InstrumentType::Drum },
    { "cymbal", KitLib::InstrumentType::Cymbal },
    { "kick", KitLib::InstrumentType::Kick },
    { "tom", KitLib::InstrumentType::Tom },
    { "octoban", KitLib::InstrumentType::Octoban },
    { "rototom", KitLib::InstrumentType::Rototom },
    { "timbale", KitLib::InstrumentType::Timbale },
    { "timpani", KitLib::InstrumentType::Timpani },
    { "conga", KitLib::InstrumentType::Conga },
    { "bongo", KitLib::InstrumentType::Bongo },
    { "djembe", KitLib::InstrumentType::Djembe },
    { "cajon", KitLib::InstrumentType::Cajon },
    { "taiko", KitLib::InstrumentType::Taiko },
    { "surdo", KitLib::InstrumentType::Surdo },
    { "mridangam", KitLib::InstrumentType::Mridangam },
    { "talking_drum", KitLib::InstrumentType::TalkingDrum },
    { "snare", KitLib::InstrumentType::Snare },
    { "ride", KitLib::InstrumentType::Ride },
    { "hihat", KitLib::InstrumentType::HiHat },
    { "hi_hat", KitLib::InstrumentType::HiHat },
    { "crash", KitLib::InstrumentType::Crash },
    { "splash", KitLib::InstrumentType::Splash },
    { "china", KitLib::InstrumentType::China },
    { "bell", KitLib::InstrumentType::Bell },
    { "stack", KitLib::InstrumentType::Stack },
    { "stax", KitLib::InstrumentType::Stack },
    { "chopper", KitLib::InstrumentType::Chopper },
    { "gong", KitLib::InstrumentType::Gong },
    { "karatala", KitLib::InstrumentType::Karatala },
    { "cowbell", KitLib::InstrumentType::Cowbell },
    { "cowbel", KitLib::InstrumentType::Cowbell },
    { "tambourine", KitLib::InstrumentType::Tambourine },
    { "shaker", KitLib::InstrumentType::Shaker },
    { "maracas", KitLib::InstrumentType::Maracas },
    { "cabasa", KitLib::InstrumentType::Cabasa },
    { "guiro", KitLib::InstrumentType::Guiro },
    { "claves", KitLib::InstrumentType::Claves },
    { "agogo", KitLib::InstrumentType::Agogo },
    { "wood_block", KitLib::InstrumentType::WoodBlock },
    { "woodblock", KitLib::InstrumentType::WoodBlock },
    { "temple_block", KitLib::InstrumentType::TempleBlock },
    { "triangle", KitLib::InstrumentType::Triangle },
    { "vibraslap", KitLib::InstrumentType::Vibraslap },
    { "castanets", KitLib::InstrumentType::Castanets },
    { "sleigh_bells", KitLib::InstrumentType::SleighBells },
    { "whip", KitLib::InstrumentType::Whip },
    { "ratchet", KitLib::InstrumentType::Ratchet },
};

juce::String instrumentTypeToString(InstrumentType type) {
    for (const auto& entry : typeTable)
        if (entry.type == type) return entry.name;
    return "unknown";
}

InstrumentType instrumentTypeFromString(const juce::String& name) {
    const auto normalized = name.toLowerCase();
    for (const auto& entry : typeTable)
        if (normalized == entry.name) return entry.type;
    return InstrumentType::Unknown;
}

} // namespace KitLib
