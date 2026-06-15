#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>

class MidiCrosstalkProfile {
public:
    MidiCrosstalkProfile();
    MidiCrosstalkProfile(const MidiCrosstalkProfile& other);
    MidiCrosstalkProfile& operator=(const MidiCrosstalkProfile& other);

    void clear() noexcept;
    void setThreshold(int primaryNote, int secondaryNote, int velocityRatio) noexcept;
    int getThreshold(int primaryNote, int secondaryNote) const noexcept;
    int getEntryCount() const noexcept;

private:
    static constexpr int midiValueCount = 128;
    static constexpr int entryCount = midiValueCount * midiValueCount;
    std::array<std::atomic<uint8_t>, entryCount> thresholds;
    std::atomic<int> activeEntryCount { 0 };

    static int getIndex(int primaryNote, int secondaryNote) noexcept;
};

class MidiCrosstalkFilter {
public:
    void prepare(double sampleRate, int maximumMidiBytes);
    void reset() noexcept;
    void process(juce::MidiBuffer& midiMessages, const MidiCrosstalkProfile& profile, int windowMs, int blockNumSamples);

private:
    static constexpr int midiValueCount = 128;
    static constexpr int64_t noSamplePosition = -1;

    juce::MidiBuffer filteredMessages;
    std::array<int, midiValueCount> blockVelocities {};
    std::array<int, midiValueCount> blockSamplePositions {};
    std::array<int, midiValueCount> recentVelocities {};
    std::array<int64_t, midiValueCount> recentSamplePositions {};
    int64_t processedSamples = 0;
    double sampleRate = 44100.0;

    bool shouldSuppress(int note, int velocity, int samplePosition, const MidiCrosstalkProfile& profile, int windowSamples) const noexcept;
};
