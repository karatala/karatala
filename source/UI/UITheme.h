#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/KitLibModel.h"

namespace UITheme {
namespace Colours {
    inline const juce::Colour bgApp = juce::Colours::black;
    inline const juce::Colour bgEditor = juce::Colour(0xff1e1e1e);
    inline const juce::Colour bgPanel = juce::Colour(0xff1a1a1a);
    inline const juce::Colour bgPanelRaised = juce::Colour(0xff222222);
    inline const juce::Colour bgControl = juce::Colour(0xff333333);
    inline const juce::Colour bgControlAlt = juce::Colour(0xff2a2a2a);
    inline const juce::Colour bgControlRaised = juce::Colour(0xff3a3a3a);
    inline const juce::Colour borderSubtle = juce::Colour(0xff333333);
    inline const juce::Colour borderMid = juce::Colour(0xff444444);
    inline const juce::Colour borderStrong = juce::Colour(0xff555555);
    inline const juce::Colour textPrimary = juce::Colours::white;
    inline const juce::Colour textSecondary = juce::Colour(0xffcccccc);
    inline const juce::Colour textInfo = juce::Colour(0xffaaaaaa);
    inline const juce::Colour textMuted = juce::Colour(0xff888888);
    inline const juce::Colour textSubtle = juce::Colour(0xff777777);
    inline const juce::Colour textAccent = juce::Colour(0xff99bbdd);
    inline const juce::Colour textAccentSoft = juce::Colour(0xff88aacc);
    inline const juce::Colour textSuccess = juce::Colours::lightgreen;
    inline const juce::Colour accent = juce::Colour(0xffcc4444);
    inline const juce::Colour accentSoft = juce::Colour(0xff88aacc);
    inline const juce::Colour accentWarning = juce::Colour(0xff883333);
    inline const juce::Colour midiLearnIdle = juce::Colours::red;
    inline const juce::Colour midiLearnActive = juce::Colours::yellow;
    inline const juce::Colour separator = juce::Colour(0xff111111);
    inline const juce::Colour padEmpty = juce::Colour(0xff333333);
    inline const juce::Colour listRowOdd = juce::Colour(0xff2e2e2e);
    inline const juce::Colour listSelection = juce::Colour(0xff3a5a8a);
    inline const juce::Colour selectionStrong = juce::Colour(0xff3399FF);
    inline const juce::Colour keyWhite = juce::Colour(0xffF4F4F4);
    inline const juce::Colour keyWhiteText = juce::Colour(0xff222222);
    inline const juce::Colour keyBlack = juce::Colour(0xff222222);
    inline const juce::Colour keyBlackText = juce::Colour(0xffEEEEEE);
    inline const juce::Colour transparent = juce::Colours::transparentBlack;

    inline juce::Colour instrumentType(KitLib::InstrumentSpec::Type type) {
        switch (type) {
            case KitLib::InstrumentSpec::Type::Kick:       return juce::Colour(0xffcc3333);
            case KitLib::InstrumentSpec::Type::Snare:      return juce::Colour(0xffcc8833);
            case KitLib::InstrumentSpec::Type::HiHat:      return juce::Colour(0xffcccc33);
            case KitLib::InstrumentSpec::Type::Tom:        return juce::Colour(0xff3366cc);
            case KitLib::InstrumentSpec::Type::Ride:       return juce::Colour(0xff33cccc);
            case KitLib::InstrumentSpec::Type::Crash:      return juce::Colour(0xff9933cc);
            case KitLib::InstrumentSpec::Type::Splash:     return juce::Colour(0xff33cc99);
            case KitLib::InstrumentSpec::Type::China:      return juce::Colour(0xffcc3399);
            case KitLib::InstrumentSpec::Type::Bell:
            case KitLib::InstrumentSpec::Type::Stack:
            case KitLib::InstrumentSpec::Type::Chopper:
            case KitLib::InstrumentSpec::Type::Gong:
            case KitLib::InstrumentSpec::Type::Karatala:
            case KitLib::InstrumentSpec::Type::Cymbal:
                return juce::Colour(0xff6699cc);
            case KitLib::InstrumentSpec::Type::Octoban:
            case KitLib::InstrumentSpec::Type::Rototom:
            case KitLib::InstrumentSpec::Type::Timbale:
            case KitLib::InstrumentSpec::Type::Timpani:
            case KitLib::InstrumentSpec::Type::Conga:
            case KitLib::InstrumentSpec::Type::Bongo:
            case KitLib::InstrumentSpec::Type::Djembe:
            case KitLib::InstrumentSpec::Type::Cajon:
            case KitLib::InstrumentSpec::Type::Taiko:
            case KitLib::InstrumentSpec::Type::Surdo:
            case KitLib::InstrumentSpec::Type::Mridangam:
            case KitLib::InstrumentSpec::Type::TalkingDrum:
            case KitLib::InstrumentSpec::Type::Drum:
                return juce::Colour(0xff6666cc);
            case KitLib::InstrumentSpec::Type::Cowbell:
            case KitLib::InstrumentSpec::Type::Tambourine:
            case KitLib::InstrumentSpec::Type::Shaker:
            case KitLib::InstrumentSpec::Type::Maracas:
            case KitLib::InstrumentSpec::Type::Cabasa:
            case KitLib::InstrumentSpec::Type::Guiro:
            case KitLib::InstrumentSpec::Type::Claves:
            case KitLib::InstrumentSpec::Type::Agogo:
            case KitLib::InstrumentSpec::Type::WoodBlock:
            case KitLib::InstrumentSpec::Type::TempleBlock:
            case KitLib::InstrumentSpec::Type::Triangle:
            case KitLib::InstrumentSpec::Type::Vibraslap:
            case KitLib::InstrumentSpec::Type::Castanets:
            case KitLib::InstrumentSpec::Type::SleighBells:
            case KitLib::InstrumentSpec::Type::Whip:
            case KitLib::InstrumentSpec::Type::Ratchet:
            case KitLib::InstrumentSpec::Type::Percussion:
                return juce::Colour(0xff999966);
            case KitLib::InstrumentSpec::Type::Unknown:
            case KitLib::InstrumentSpec::Type::Other:
                return juce::Colour(0xff555555);
        }
    }
}

namespace Fonts {
    inline juce::Font label() { return juce::Font(juce::FontOptions(10.0f)); }
    inline juce::Font labelBold() { return juce::Font(juce::FontOptions(10.0f).withStyle("Bold")); }
    inline juce::Font labelBoldMedium() { return labelBold().withHeight(12.0f); }
    inline juce::Font small() { return juce::Font(juce::FontOptions(9.0f)); }
    inline juce::Font table() { return juce::Font(juce::FontOptions(10.0f)); }
    inline juce::Font tableSmall() { return table().withHeight(11.0f); }
    inline juce::Font comboBox() { return juce::Font(juce::FontOptions(12.0f)); }
    inline juce::Font popupMenu() { return comboBox(); }
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
    constexpr int modeButton = 44;
    constexpr int sidebarKnob = 40;
    constexpr int midiLearnButton = 22;
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
    constexpr int sourceCardWidth = 170;
    constexpr int layerRowHeight = 64;
    constexpr int midiMapInstCol = 90;
    constexpr int midiMapArtCol = 100;
}
}
