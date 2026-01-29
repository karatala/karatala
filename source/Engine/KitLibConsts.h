#pragma once

#include <juce_core/juce_core.h>
#include <regex>
#include <vector>
#include "../Config/Helpers.h"


namespace KitLib {

/**
 * MicSpecs
 * Represents a parsed mic token from the micChain.
 * Example: "ons" → {type=Overhead, position=Normak, format=Stereo, variant=""}
 * Example: "dtm1" → {type=Direct, position=Top, format=mono, variant="1"}
 * Example: "rfi" → {type=Room, position=Far, format=Mid-Side, variant=""}
 */
class MicSpecs {
public:
    enum class Type : char { Unknown = 0,
        Direct = 'd',
        Overhead = 'o',
        Room = 'r'
    };
    enum class Position : char { Unknown = 0,
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
    enum class Format : char { Unknown = 0,
        Mono = 'm',
        Stereo = 's',
        MidSide = 'i'
    };

    Type type = Type::Unknown;
    Position position = Position::Unknown;
    Format format = Format::Unknown;
    juce::String variant, raw;
    int channels = 0;

    bool isValid() const { return this->type != Type::Unknown && this->position != Position::Unknown && this->format != Format::Unknown; }

    static ParseResult<MicSpecs> parseMicCode(const juce::String& code);
    static ParseResult<std::vector<MicSpecs>> parseMicCodesChain(const juce::String& codeChain);

    juce::String getDescription() const {
        juce::StringArray parts;
        parts.add(enumName(type));
        parts.add(enumName(position));
        parts.add(enumName(format));
        if (!variant.isEmpty())
            parts.add(variant);
        return parts.joinIntoString(" ");
    }
    
private:
    template <typename E>
    struct EnumEntry { E value; juce::juce_wchar code; const char* name; };

    static constexpr EnumEntry<Type> typeTable[] {
        { Type::Direct, 'd', "Direct" },
        { Type::Overhead, 'o', "Overhead" },
        { Type::Room, 'r', "Room" }
    };

    static constexpr EnumEntry<Position> positionTable[] {
        { Position::Sub, 'u', "Sub" },
        { Position::Side, 's', "Side" },
        { Position::Top, 't', "Top" },
        { Position::Bottom, 'b', "Bottom" },
        { Position::In, 'i', "In" },
        { Position::Out, 'o', "Out" },
        { Position::Normal, 'n', "Normal" },
        { Position::Wide, 'w', "Wide" },
        { Position::Close, 'c', "Close" },
        { Position::Far, 'f', "Far" }
    };

    static constexpr EnumEntry<Format> formatTable[] {
        { Format::Mono, 'm', "Mono" },
        { Format::Stereo, 's', "Stereo" },
        { Format::MidSide, 'i', "MidSide" }
    };

    template <typename E, size_t N>
    static E enumFromChar(juce::juce_wchar c, const EnumEntry<E>(&table)[N]) noexcept {
        for (const auto& e : table)
            if (e.code == c) return e.value;
        return E::Unknown;
    }
    
    template <typename E, size_t N>
    static const char* enumName(E value, const EnumEntry<E>(&table)[N]) noexcept {
        for (const auto& e : table)
            if (e.value == value) return e.name;
        return "?";
    }

    static Type typeFromChar(juce::juce_wchar c) noexcept { return enumFromChar(c, typeTable); }
    static Position positionFromChar(juce::juce_wchar c) noexcept { return enumFromChar(c, positionTable); }
    static Format formatFromChar(juce::juce_wchar c) noexcept { return enumFromChar(c, formatTable); }
    static const char* enumName(Type v) noexcept { return enumName(v, typeTable); }
    static const char* enumName(Position v) noexcept { return enumName(v, positionTable); }
    static const char* enumName(Format v) noexcept { return enumName(v, formatTable); }
};

}