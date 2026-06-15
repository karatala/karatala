#include "MidiCrosstalkFilter.h"

#include <algorithm>
#include <limits>

MidiCrosstalkProfile::MidiCrosstalkProfile() { this->clear(); }

MidiCrosstalkProfile::MidiCrosstalkProfile(const MidiCrosstalkProfile& other) {
    for (int idx = 0; idx < entryCount; ++idx)
        this->thresholds[static_cast<size_t>(idx)].store(other.thresholds[static_cast<size_t>(idx)].load(std::memory_order_relaxed), std::memory_order_relaxed);
    this->activeEntryCount.store(other.activeEntryCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

MidiCrosstalkProfile& MidiCrosstalkProfile::operator=(const MidiCrosstalkProfile& other) {
    if (this == &other) return *this;
    for (int idx = 0; idx < entryCount; ++idx)
        this->thresholds[static_cast<size_t>(idx)].store(other.thresholds[static_cast<size_t>(idx)].load(std::memory_order_relaxed), std::memory_order_relaxed);
    this->activeEntryCount.store(other.activeEntryCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
}

void MidiCrosstalkProfile::clear() noexcept {
    for (auto& threshold : this->thresholds)
        threshold.store(0, std::memory_order_relaxed);
    this->activeEntryCount.store(0, std::memory_order_relaxed);
}

void MidiCrosstalkProfile::setThreshold(int primaryNote, int secondaryNote, int velocityRatio) noexcept {
    if (!juce::isPositiveAndBelow(primaryNote, midiValueCount) || !juce::isPositiveAndBelow(secondaryNote, midiValueCount) || primaryNote == secondaryNote)
        return;
    auto& threshold = this->thresholds[static_cast<size_t>(getIndex(primaryNote, secondaryNote))];
    const uint8_t oldValue = threshold.load(std::memory_order_relaxed);
    const uint8_t newValue = static_cast<uint8_t>(juce::jlimit(0, 127, velocityRatio));
    threshold.store(newValue, std::memory_order_relaxed);
    if (oldValue == 0 && newValue > 0)
        this->activeEntryCount.fetch_add(1, std::memory_order_relaxed);
    else if (oldValue > 0 && newValue == 0)
        this->activeEntryCount.fetch_sub(1, std::memory_order_relaxed);
}

int MidiCrosstalkProfile::getThreshold(int primaryNote, int secondaryNote) const noexcept {
    if (!juce::isPositiveAndBelow(primaryNote, midiValueCount) || !juce::isPositiveAndBelow(secondaryNote, midiValueCount))
        return 0;
    return this->thresholds[static_cast<size_t>(getIndex(primaryNote, secondaryNote))].load(std::memory_order_relaxed);
}

int MidiCrosstalkProfile::getEntryCount() const noexcept { return this->activeEntryCount.load(std::memory_order_relaxed); }

int MidiCrosstalkProfile::getIndex(int primaryNote, int secondaryNote) noexcept { return primaryNote * midiValueCount + secondaryNote; }

void MidiCrosstalkFilter::prepare(double newSampleRate, int maximumMidiBytes) {
    this->sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    this->filteredMessages.ensureSize(static_cast<size_t>(juce::jmax(65536, maximumMidiBytes)));
    this->reset();
}

void MidiCrosstalkFilter::reset() noexcept {
    this->blockVelocities.fill(0);
    this->blockSamplePositions.fill(0);
    this->recentVelocities.fill(0);
    this->recentSamplePositions.fill(noSamplePosition);
    this->processedSamples = 0;
}

void MidiCrosstalkFilter::process(juce::MidiBuffer& midiMessages, const MidiCrosstalkProfile& profile, int windowMs, int blockNumSamples) {
    if (profile.getEntryCount() == 0) {
        this->processedSamples += juce::jmax(1, blockNumSamples);
        return;
    }

    this->blockVelocities.fill(0);
    this->blockSamplePositions.fill(0);
    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        if (!message.isNoteOn()) continue;
        const int note = message.getNoteNumber();
        const int velocity = message.getVelocity();
        if (velocity > this->blockVelocities[static_cast<size_t>(note)]) {
            this->blockVelocities[static_cast<size_t>(note)] = velocity;
            this->blockSamplePositions[static_cast<size_t>(note)] = metadata.samplePosition;
        }
    }

    const int windowSamples = juce::jmax(1, static_cast<int>(this->sampleRate * static_cast<double>(juce::jlimit(1, 100, windowMs)) / 1000.0));
    this->filteredMessages.clear();
    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        if (!message.isNoteOn() || !this->shouldSuppress(message.getNoteNumber(), message.getVelocity(), metadata.samplePosition, profile, windowSamples))
            this->filteredMessages.addEvent(message, metadata.samplePosition);
    }

    for (int note = 0; note < midiValueCount; ++note) {
        const int velocity = this->blockVelocities[static_cast<size_t>(note)];
        if (velocity <= 0) continue;
        this->recentVelocities[static_cast<size_t>(note)] = velocity;
        this->recentSamplePositions[static_cast<size_t>(note)] = this->processedSamples + this->blockSamplePositions[static_cast<size_t>(note)];
    }

    this->processedSamples += juce::jmax(1, blockNumSamples);
    midiMessages.clear();
    midiMessages.addEvents(this->filteredMessages, 0, std::numeric_limits<int>::max(), 0);
}

bool MidiCrosstalkFilter::shouldSuppress(int note, int velocity, int samplePosition, const MidiCrosstalkProfile& profile, int windowSamples) const noexcept {
    for (int primaryNote = 0; primaryNote < midiValueCount; ++primaryNote) {
        const int threshold = profile.getThreshold(primaryNote, note);
        if (threshold <= 0) continue;

        const int blockPrimaryVelocity = this->blockVelocities[static_cast<size_t>(primaryNote)];
        const int blockDistance = std::abs(this->blockSamplePositions[static_cast<size_t>(primaryNote)] - samplePosition);
        if (blockPrimaryVelocity > velocity && blockDistance <= windowSamples && velocity * 127 <= blockPrimaryVelocity * threshold)
            return true;

        const int64_t recentPosition = this->recentSamplePositions[static_cast<size_t>(primaryNote)];
        const int recentPrimaryVelocity = this->recentVelocities[static_cast<size_t>(primaryNote)];
        const int64_t absolutePosition = this->processedSamples + samplePosition;
        if (recentPosition >= 0 && absolutePosition >= recentPosition && absolutePosition - recentPosition <= windowSamples && recentPrimaryVelocity > velocity && velocity * 127 <= recentPrimaryVelocity * threshold)
            return true;
    }
    return false;
}
