#include "../source/Engine/MidiCrosstalkFilter.h"
#include "catch2/catch_test_macros.hpp"

static std::vector<std::pair<int, int>> getNoteOns(const juce::MidiBuffer& midiMessages) {
    std::vector<std::pair<int, int>> notes;
    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            notes.emplace_back(message.getNoteNumber(), message.getVelocity());
    }
    return notes;
}
TEST_CASE("MidiCrosstalkProfile stores directional thresholds", "[MidiCrosstalk]") {
    MidiCrosstalkProfile profile;
    REQUIRE(profile.getEntryCount() == 0);

    profile.setThreshold(49, 48, 30);
    REQUIRE(profile.getThreshold(49, 48) == 30);
    REQUIRE(profile.getThreshold(48, 49) == 0);
    REQUIRE(profile.getEntryCount() == 1);

    profile.setThreshold(49, 48, 0);
    REQUIRE(profile.getEntryCount() == 0);
}

TEST_CASE("MidiCrosstalkFilter suppresses only learned low velocity companions", "[MidiCrosstalk]") {
    MidiCrosstalkProfile profile;
    profile.setThreshold(49, 48, 32);

    MidiCrosstalkFilter filter;
    filter.prepare(48000.0, 1024);

    juce::MidiBuffer midiMessages;
    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 49, static_cast<juce::uint8>(120)), 10);
    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(20)), 12);
    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 40, static_cast<juce::uint8>(15)), 13);
    filter.process(midiMessages, profile, 40, 256);

    const auto notes = getNoteOns(midiMessages);
    REQUIRE(notes.size() == 2);
    REQUIRE(notes[0] == std::pair<int, int>(49, 120));
    REQUIRE(notes[1] == std::pair<int, int>(40, 15));
}

TEST_CASE("MidiCrosstalkFilter preserves intentional strong simultaneous hits", "[MidiCrosstalk]") {
    MidiCrosstalkProfile profile;
    profile.setThreshold(49, 48, 32);

    MidiCrosstalkFilter filter;
    filter.prepare(48000.0, 1024);

    juce::MidiBuffer midiMessages;
    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 49, static_cast<juce::uint8>(120)), 10);
    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(80)), 12);
    filter.process(midiMessages, profile, 40, 256);

    REQUIRE(getNoteOns(midiMessages).size() == 2);
}

TEST_CASE("MidiCrosstalkFilter expires prior hits using audio block time", "[MidiCrosstalk]") {
    MidiCrosstalkProfile profile;
    profile.setThreshold(49, 48, 32);

    MidiCrosstalkFilter filter;
    filter.prepare(1000.0, 1024);

    juce::MidiBuffer primaryBlock;
    primaryBlock.addEvent(juce::MidiMessage::noteOn(1, 49, static_cast<juce::uint8>(120)), 0);
    filter.process(primaryBlock, profile, 40, 20);

    juce::MidiBuffer emptyBlock;
    filter.process(emptyBlock, profile, 40, 30);

    juce::MidiBuffer laterBlock;
    laterBlock.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(20)), 0);
    filter.process(laterBlock, profile, 40, 20);

    REQUIRE(getNoteOns(laterBlock).size() == 1);
}
