#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "InstrumentSynth.h"

// Resolves the global host output channel assignment for each sample variant of an instrument.
//
// Built once per instrument load (constructor), then resolve() is called once per SampleVariantSpec
// to obtain its {globalStart, channels} directly — no intermediate index lists or out-params needed.
//
// Channel ordering contract: applyMicLayout (DrumKitEngine) fills micOutputMapping by iterating
// micLayout->mics in the same order this class uses (common mics first, then instrument-specific),
// so globalMapping[localCursor] correctly corresponds to each mic entry here.
class MicRoutingBuilder {
public:
    struct Assignment {
        int globalStart = 0;
        int channels    = 0;
    };

    explicit MicRoutingBuilder(InstrumentSynth* synth) : synth(synth) {
        this->buildMicEntries();
    }

    // Resolve the output channel assignment for a single sample variant spec.
    // Matching priority: exact (type+position+format) > type-only > first entry > fully unmatched.
    // For unmatched variants an offset beyond all real mic channels is allocated sequentially.
    Assignment resolve(const KitLib::MicSpec& micSpec, int fileChannelCount) {
        // 1. Exact match
        for (size_t i = 0; i < micEntries.size(); ++i)
            if (micKeyEq(micSpec, micEntries[i].mic->specs))
                return micEntries[i].assignment;

        // 2. Type-only match
        for (size_t i = 0; i < micEntries.size(); ++i)
            if (micEntries[i].mic->specs.type == micSpec.type)
                return micEntries[i].assignment;

        // 3. First entry fallback
        if (!micEntries.empty())
            return micEntries[0].assignment;

        // 4. Fully unmatched — allocate past the real mic region
        Assignment unmatched { unmatchedCursor, fileChannelCount };
        unmatchedCursor += fileChannelCount;
        return unmatched;
    }

private:
    struct MicEntryInfo {
        const MicLayout::Mic* mic       = nullptr;
        Assignment             assignment;
    };

    InstrumentSynth*           synth = nullptr;
    std::vector<MicEntryInfo>  micEntries;
    int                        unmatchedCursor = 0;

    static bool micKeyEq(const KitLib::MicSpec& a, const KitLib::MicSpec& b) {
        return a.type == b.type && a.position == b.position && a.format == b.format;
    }

    void buildMicEntries() {
        const MicLayout* micLayout = this->synth->getMicLayout();
        if (micLayout == nullptr) return;

        // Collect relevant mics in the same order applyMicLayout builds micOutputMapping:
        // common mics (instrument="") for all instruments first, then instrument-specific.
        std::vector<const MicLayout::Mic*> ordered;
        std::vector<const MicLayout::Mic*> sameCodeMics, sameTypeMics;

        for (const auto& mic : micLayout->mics) {
            if (mic.instrument.isEmpty()) {
                ordered.push_back(&mic);
                continue;
            }
            if (mic.instrument == synth->getId()) {
                sameCodeMics.push_back(&mic);
                continue;
            }
            juce::String typeStr = mic.instrument;
            while (typeStr.length() > 0 && juce::CharacterFunctions::isDigit(typeStr.getLastCharacter()))
                typeStr = typeStr.dropLastCharacters(1);
            auto instType = KitLib::InstrumentSpec::typeFromString(typeStr);
            if (instType != KitLib::InstrumentSpec::Type::Unknown && instType == synth->getType())
                sameTypeMics.push_back(&mic);
        }
        const auto& specific = sameCodeMics.empty() ? sameTypeMics : sameCodeMics;
        ordered.insert(ordered.end(), specific.begin(), specific.end());

        // Map each mic entry to its global host channel start via the instrument's micOutputMapping.
        const auto& globalMapping = synth->getMicOutputMapping();
        int localCursor = 0;

        juce::String debug = "    MicLayout entries for '" + synth->getId() + "': ";
        for (size_t i = 0; i < ordered.size(); ++i) {
            const auto* mic = ordered[i];
            const int ch = mic->specs.channels;
            const int globalStart = (localCursor < static_cast<int>(globalMapping.size()))
                ? globalMapping[localCursor]
                : localCursor; // fallback when micOutputMapping not yet set
            micEntries.push_back({ mic, { globalStart, ch } });
            if (i > 0) debug += ", ";
            debug += mic->name + " -> ch " + juce::String(globalStart) + " (" + juce::String(ch) + "ch)";
            localCursor += ch;
        }
        LOG(Debug, debug);

        // Unmatched variants land after all globally mapped channels
        unmatchedCursor = (micLayout != nullptr) ? micLayout->getTotalChannelCount() : localCursor;
    }
};
