#pragma once

#include <juce_core/juce_core.h>

namespace KitLib {

enum class InstrumentType {
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

juce::String instrumentTypeToString(InstrumentType type);
InstrumentType instrumentTypeFromString(const juce::String& name);

} // namespace KitLib
