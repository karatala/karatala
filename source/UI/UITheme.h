#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/InstrumentType.h"

namespace UITheme {
namespace Symbols {
    inline const juce::String times = juce::String::charToString(0x00d7);
    inline const juce::String phaseInvert = juce::String::charToString(0x00d8);
    inline const juce::String filledCircle = juce::String::charToString(0x25cf);
    inline const juce::String hollowCircle = juce::String::charToString(0x25cb);
    inline const juce::String checkedBox = juce::String::charToString(0x2611) + juce::String::charToString(0xfe0f);
    inline const juce::String dice = juce::String::charToString(0x1f3b2);
    inline const juce::String signalBars = juce::String::charToString(0x1f4f6);
    inline const juce::String user = juce::String::charToString(0x1f464);
    inline const juce::String package = juce::String::charToString(0x1f4e6);
}

namespace Colours {
    inline const juce::Colour bgApp = juce::Colour(0xff080808);
    inline const juce::Colour bgEditor = juce::Colour(0xff1e1e1e);
    inline const juce::Colour bgPanel = juce::Colour(0xff1a1a1a);
    inline const juce::Colour bgPanelRaised = juce::Colour(0xff222222);
    inline const juce::Colour bgControl = juce::Colour(0xff333333);
    inline const juce::Colour bgControlAlt = juce::Colour(0xff2a2a2a);
    inline const juce::Colour bgControlRaised = juce::Colour(0xff3a3a3a);
    inline const juce::Colour borderSubtle = juce::Colour(0xff333333);
    inline const juce::Colour borderMid = juce::Colour(0xff444444);
    inline const juce::Colour borderStrong = juce::Colour(0xff555555);
    inline const juce::Colour textPrimary = juce::Colour(0xfffafafa);
    inline const juce::Colour textSecondary = juce::Colour(0xffcccccc);
    inline const juce::Colour textInfo = juce::Colour(0xffaaaaaa);
    inline const juce::Colour textMuted = juce::Colour(0xff888888);
    inline const juce::Colour textSubtle = juce::Colour(0xff777777);
    inline const juce::Colour textAccent = juce::Colour(0xff99bbdd);
    inline const juce::Colour textAccentSoft = juce::Colour(0xff88aacc);
    inline const juce::Colour textSuccess = juce::Colour(0xff00EE00);
    inline const juce::Colour accent = juce::Colour(0xffcc4444);
    inline const juce::Colour accentSoft = juce::Colour(0xff88aacc);
    inline const juce::Colour accentWarning = juce::Colour(0xff883333);
    inline const juce::Colour broadEditDanger = juce::Colour(0xffff2020);
    inline const juce::Colour midiLearnIdle = juce::Colour(0xffEEEE00);
    inline const juce::Colour midiLearnActive = juce::Colour(0xffFFFF00);
    inline const juce::Colour separator = juce::Colour(0xff111111);
    inline const juce::Colour padEmpty = juce::Colour(0xff333333);
    inline const juce::Colour listRowOdd = juce::Colour(0xff2e2e2e);
    inline const juce::Colour listSelection = juce::Colour(0xff3a5a8a);
    inline const juce::Colour selectionStrong = juce::Colour(0xff3399FF);
    inline const juce::Colour libraryBadgeDirect = juce::Colour(0xffa34b44);
    inline const juce::Colour libraryBadgeOverhead = juce::Colour(0xff347fa3);
    inline const juce::Colour libraryBadgeRoom = juce::Colour(0xff7656a0);
    inline const juce::Colour libraryBadgeFormat = juce::Colour(0xff35887a);
    inline const juce::Colour libraryBadgeRoundRobin = juce::Colour(0xff9a6a30);
    inline const juce::Colour libraryBadgeVelocity = juce::Colour(0xff4e8a48);
    inline const juce::Colour keyWhite = juce::Colour(0xffF4F4F4);
    inline const juce::Colour keyWhiteText = juce::Colour(0xff222222);
    inline const juce::Colour keyBlack = juce::Colour(0xff222222);
    inline const juce::Colour keyBlackText = juce::Colour(0xffEEEEEE);
    inline const juce::Colour transparent = juce::Colour(0x00000000);

    inline juce::Colour instrumentType(KitLib::InstrumentType type) {
        switch (type) {
            case KitLib::InstrumentType::Kick:       return juce::Colour(0xffcc3333);
            case KitLib::InstrumentType::Snare:      return juce::Colour(0xffcc8833);
            case KitLib::InstrumentType::HiHat:      return juce::Colour(0xffcccc33);
            case KitLib::InstrumentType::Tom:        return juce::Colour(0xff3366cc);
            case KitLib::InstrumentType::Ride:       return juce::Colour(0xff33cccc);
            case KitLib::InstrumentType::Crash:      return juce::Colour(0xff9933cc);
            case KitLib::InstrumentType::Splash:     return juce::Colour(0xff33cc99);
            case KitLib::InstrumentType::China:      return juce::Colour(0xffcc3399);
            case KitLib::InstrumentType::Bell:
            case KitLib::InstrumentType::Stack:
            case KitLib::InstrumentType::Chopper:
            case KitLib::InstrumentType::Gong:
            case KitLib::InstrumentType::Karatala:
            case KitLib::InstrumentType::Cymbal:
                return juce::Colour(0xff6699cc);
            case KitLib::InstrumentType::Octoban:
            case KitLib::InstrumentType::Rototom:
            case KitLib::InstrumentType::Timbale:
            case KitLib::InstrumentType::Timpani:
            case KitLib::InstrumentType::Conga:
            case KitLib::InstrumentType::Bongo:
            case KitLib::InstrumentType::Djembe:
            case KitLib::InstrumentType::Cajon:
            case KitLib::InstrumentType::Taiko:
            case KitLib::InstrumentType::Surdo:
            case KitLib::InstrumentType::Mridangam:
            case KitLib::InstrumentType::TalkingDrum:
            case KitLib::InstrumentType::Drum:
                return juce::Colour(0xff6666cc);
            case KitLib::InstrumentType::Cowbell:
            case KitLib::InstrumentType::Tambourine:
            case KitLib::InstrumentType::Shaker:
            case KitLib::InstrumentType::Maracas:
            case KitLib::InstrumentType::Cabasa:
            case KitLib::InstrumentType::Guiro:
            case KitLib::InstrumentType::Claves:
            case KitLib::InstrumentType::Agogo:
            case KitLib::InstrumentType::WoodBlock:
            case KitLib::InstrumentType::TempleBlock:
            case KitLib::InstrumentType::Triangle:
            case KitLib::InstrumentType::Vibraslap:
            case KitLib::InstrumentType::Castanets:
            case KitLib::InstrumentType::SleighBells:
            case KitLib::InstrumentType::Whip:
            case KitLib::InstrumentType::Ratchet:
            case KitLib::InstrumentType::Percussion:
                return juce::Colour(0xff999966);
            case KitLib::InstrumentType::Unknown:
            case KitLib::InstrumentType::Other:
                return juce::Colour(0xff555555);
        }
        return juce::Colour(0xff555555);
    }
}

namespace Fonts {
    inline juce::Font normal() { return juce::Font(juce::FontOptions(12.0f)); }
    inline juce::Font label() { return juce::Font(juce::FontOptions(10.0f)); }
    inline juce::Font labelBold() { return juce::Font(juce::FontOptions(10.0f).withStyle("Bold")); }
    inline juce::Font labelBoldMedium() { return labelBold().withHeight(12.0f); }
    inline juce::Font small() { return juce::Font(juce::FontOptions(10.0f)); }
    inline juce::Font table() { return juce::Font(juce::FontOptions(10.0f)); }
    inline juce::Font tableSmall() { return table().withHeight(11.0f); }
    inline juce::Font mono() { return juce::Font(juce::FontOptions(11.0f).withName(juce::Font::getDefaultMonospacedFontName())); }
    inline juce::Font sectionTitle() { return juce::Font(juce::FontOptions(13.0f).withStyle("Bold")); }
}

namespace Spacing {
    constexpr int xs = 2;
    constexpr int sm = 4;
    constexpr int md = 8;
    constexpr int rowCompact = 18;
    constexpr int rowNormal = 22;
    constexpr int rowLarge = 26;
}

namespace Sizes {
    constexpr int sidebarWidth = 56;
    constexpr int statusBarHeight = 24;
    constexpr int pageButton = 44;
    constexpr int sidebarKnob = 40;
    constexpr int standardControlHeight = 20;
    constexpr int standardControlTextHeight = 14;
    constexpr int midiLearnButton = standardControlHeight;
    constexpr int compactButton = standardControlHeight;
    constexpr int sliderTextBoxW = 40;
    constexpr int sliderTextBoxH = 20;
    constexpr int compactPotTextBoxW = 38;
    constexpr int compactPotTextBoxH = 12;
    constexpr int settingsTextBoxW = 50;
    constexpr int settingsIntTextBoxW = 40;
    constexpr int settingsTextBoxH = 14;
    constexpr int popupPadding = 8;
    constexpr int potWidth = 50;
    constexpr int potHeight = 56;
    constexpr int libraryArticulationCardWidth = 170;
    constexpr int layerRowHeight = 64;
    constexpr int padTitleRailWidth = 18;
    constexpr int tableInstrumentColumnWidth = 90;
    constexpr int tableArticulationColumnWidth = 100;
}
}
