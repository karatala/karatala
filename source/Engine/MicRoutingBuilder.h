#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <set>
#include "InstrumentSynth.h"
#include "DrumKitEngine.h"
#include "MicRoutingPlan.h"

// Resolves the global host output channel assignment for each sample variant of an instrument.
//
// Built once per instrument load (constructor), then resolve() is called once per SampleVariantSpec
// to obtain its {globalStart, channels} directly.
//
// Channel ordering contract: applyMicLayout (DrumKitEngine) builds micOutputMapping by iterating
// micLayout->mics in order. This class must iterate in the exact same order and apply the same
// slot-resolution logic so that localCursor always aligns with globalMapping.
class MicRoutingBuilder {
public:
    struct Assignment {
        int globalStart = -1; // -1 means unmatched — voice renders silently (destCh guard in render loop)
        int channels    = 0;
        juce::String micCode;
        bool applyBalance = false;
    };

    explicit MicRoutingBuilder(InstrumentSynth* synth) : synth(synth) {
        this->buildMicEntries();
    }

    // Resolve the output channel assignment for a single sample variant spec.
    // Only exact (type+position+format) matches are accepted.
    // Unmatched specs return {-1, fileChannelCount}; the caller must skip those samples.
    // All unmatched raw codes are accumulated in skippedRawCodes for a single summary log.
    Assignment resolve(const KitLib::MicSpec& micSpec, int fileChannelCount) {
        if (const auto* plan = this->synth != nullptr ? this->synth->getMicRoutingPlan() : nullptr) {
            const auto& routeOverrides = this->synth->getLastRoutingContext().micRouteOverrides;
            auto overrideIt = routeOverrides.end();
            for (auto routeIt = routeOverrides.begin(); routeIt != routeOverrides.end(); ++routeIt) {
                if (routeIt->first.compareIgnoreCase(micSpec.raw) == 0) {
                    overrideIt = routeIt;
                    break;
                }
            }
            if (overrideIt != routeOverrides.end()) {
                const auto overrideAssignment = plan->resolveOverride(this->synth->getId(), micSpec, overrideIt->second);
                if (overrideAssignment.matched()) {
                    LOG(Debug, "Mic route override: slot=" + this->synth->getId()
                        + " source=" + micSpec.raw
                        + " output=" + overrideIt->second
                        + " hostStart=" + juce::String(overrideAssignment.globalStart)
                        + " channels=" + juce::String(overrideAssignment.channels));
                    return { overrideAssignment.globalStart, overrideAssignment.channels, overrideAssignment.micCode, overrideAssignment.applyBalance };
                }
                LOG(Warning, overrideAssignment.warning);
            }

            const auto planned = plan->resolve(this->synth->getId(), this->synth->getType(), micSpec);
            if (planned.matched())
                return { planned.globalStart, planned.channels, planned.micCode, planned.applyBalance };

            // Source direct-code collection is wired into the pure Stage 3 plan builder,
            // but legacy synth loading still needs direct mics until useInstrumentMap
            // publishes source-aware requests for every slot.
            if (micSpec.type != KitLib::MicSpec::Type::Direct) {
                skippedRawCodes.insert(micSpec.raw);
                return { -1, fileChannelCount };
            }
        }

        for (const auto& e : micEntries)
            if (micKeyEq(micSpec, e.mic->specs)) {
                auto assignment = e.assignment;
                assignment.micCode = e.mic->code;
                return assignment;
            }

        skippedRawCodes.insert(micSpec.raw);
        return { -1, fileChannelCount };
    }
    // Merge skipped raw codes accumulated so far into an external set.
    void mergeSkippedInto(std::set<juce::String>& out) const {
        out.insert(skippedRawCodes.begin(), skippedRawCodes.end());
    }

private:
    struct MicEntryInfo {
        const MicLayout::Mic* mic       = nullptr;
        Assignment             assignment;
    };

    InstrumentSynth*           synth = nullptr;
    std::vector<MicEntryInfo>  micEntries;
    std::set<juce::String>     skippedRawCodes;

    static bool micKeyEq(const KitLib::MicSpec& a, const KitLib::MicSpec& b) {
        return a.type == b.type && a.position == b.position && a.format == b.format;
    }

    void buildMicEntries() {
        const MicLayout* micLayout = this->synth->getMicLayout();
        if (micLayout == nullptr) return;

        const DrumKitEngine* engine = this->synth->getEngine();

        // Iterate micLayout->mics in the same order applyMicLayout does, collecting only the
        // mics whose channels were assigned to this instrument. The ordering must match exactly
        // so that localCursor aligns with globalMapping.
        //
        // applyMicLayout target resolution per mic:
        //   instrument==""        → common mic, goes to ALL instruments
        //   instrument found in slots → exclusive to that slot only
        //   instrument NOT in slots → type fallback, goes to all instruments of that type
        //
        // We mirror that here. The sameCode/sameType exclusivity rule:
        //   if ANY mic in the layout exactly names this synth's id, type-fallback mics are
        //   NOT included (they would go to other slots that matched exactly by id).
        bool hasExactCodeMatch = false;
        for (const auto& mic : micLayout->mics)
            if (mic.instrument == this->synth->getId()) { hasExactCodeMatch = true; break; }

        const auto& globalMapping = this->synth->getMicOutputMapping();
        int localCursor = 0;

        juce::String debug = "    MicLayout entries for '" + this->synth->getId() + "': ";
        bool firstEntry = true;

        for (const auto& mic : micLayout->mics) {
            const int ch = juce::jmax(1, mic.specs.channels);
            bool includeForThisInst = false;

            if (mic.instrument.isEmpty()) {
                includeForThisInst = true;  // common mic
            } else if (mic.instrument == this->synth->getId()) {
                includeForThisInst = true;  // exact id match
            } else if (!hasExactCodeMatch) {
                // Type fallback — only when this mic's instrument field does NOT resolve
                // to any known layout slot (mirrors applyMicLayout's fallback branch).
                bool isKnownSlot = (engine != nullptr) && engine->hasLayoutSlotId(mic.instrument);
                if (!isKnownSlot) {
                    juce::String typeStr = mic.instrument;
                    while (typeStr.length() > 0 && juce::CharacterFunctions::isDigit(typeStr.getLastCharacter()))
                        typeStr = typeStr.dropLastCharacters(1);
                    auto instType = KitLib::InstrumentSpec::typeFromString(typeStr);
                    if (instType != KitLib::InstrumentSpec::Type::Unknown && instType == this->synth->getType())
                        includeForThisInst = true;
                }
            }

            if (includeForThisInst) {
                const int globalStart = (localCursor < static_cast<int>(globalMapping.size()))
                    ? globalMapping[localCursor]
                    : localCursor;
                MicEntryInfo info;
                info.mic = &mic;
                info.assignment.globalStart = globalStart;
                info.assignment.channels = ch;
                info.assignment.micCode = mic.code;
                micEntries.push_back(std::move(info));
                if (!firstEntry) debug += ", ";
                debug += mic.name + " -> ch " + juce::String(globalStart) + " (" + juce::String(ch) + "ch)";
                firstEntry = false;
                localCursor += ch;
            }
        }
        LOG(Debug, debug);

        // Sanity check: localCursor should equal the instrument's assigned channel count.
        const int assigned = static_cast<int>(globalMapping.size());
        if (assigned > 0 && localCursor != assigned)
            LOG(Warning, "MicRoutingBuilder: instrument '" + this->synth->getId()
                + "' cursor mismatch: walked " + juce::String(localCursor)
                + " channels but micOutputMapping has " + juce::String(assigned)
                + " — check mic layout and slot id matching.");
    }
};

