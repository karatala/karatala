#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "InstrumentSynth.h"


class MicRoutingBuilder {
public:
    MicRoutingBuilder(InstrumentSynth* synth) : synth(synth) {}
    
    void build(KitLib::ArticulationSpec* artSpec, std::vector<int>& micBufStartChannels, std::vector<int>& micEntryChannelSizes, int& unmatchedStart) {
        this->collectMicEntries();
        this->assignVariants(artSpec);
        this->computeMicGroups(micBufStartChannels, micEntryChannelSizes, unmatchedStart);
    }

    int getVariant(size_t& index) {
        if (index >= this->allVariants.size()) return -1;
        return this->allVariants[index].second; 
    }
private:
    InstrumentSynth* synth = nullptr;
    std::vector<const MicLayout::Mic*> micEntriesForInst;
    std::vector<std::pair<const KitLib::SampleVariantSpec*, int>> allVariants; // keep order for assignment
    std::unordered_map<size_t, int> variantsPerMic; // index in micEntriesForInst -> count
    int unmatchedVariantCount = 0;

    void collectMicEntries() {
        this->micEntriesForInst.clear();

        const MicLayout* micLayout = this->synth->getMicLayout();

        if (micLayout == nullptr) return;

        std::vector<const MicLayout::Mic*> commonMics;
        std::vector<const MicLayout::Mic*> sameCodeMics;
        std::vector<const MicLayout::Mic*> sameTypeMics;

        for (const auto& mic : micLayout->mics) {
            if (mic.instrument.isEmpty()) {
                commonMics.push_back(&mic);
                continue;
            }
            if (mic.instrument == synth->getId()) {
                sameCodeMics.push_back(&mic);
                continue;
            }
            juce::String typeStr = mic.instrument;
            while (typeStr.length() > 0 && juce::CharacterFunctions::isDigit(typeStr.getLastCharacter())) {
                typeStr = typeStr.dropLastCharacters(1);
            }
            auto instType = KitLib::InstrumentSpec::typeFromString(typeStr);
            if (instType != KitLib::InstrumentSpec::Type::Unknown && instType == synth->getType()) {
                sameTypeMics.push_back(&mic);
            }
        }
        this->micEntriesForInst.insert(this->micEntriesForInst.end(), commonMics.begin(), commonMics.end());

        if (!sameCodeMics.empty()) {
            this->micEntriesForInst.insert(this->micEntriesForInst.end(), sameCodeMics.begin(), sameCodeMics.end());
        } else {
            this->micEntriesForInst.insert(this->micEntriesForInst.end(), sameTypeMics.begin(), sameTypeMics.end());
        }
        juce::String debug = "    MicLayout entries that apply to this instrument: ";
        for (size_t i = 0; i < this->micEntriesForInst.size(); ++i) {
            if (i > 0) debug += ", ";
            const auto* mic = this->micEntriesForInst[i];
            debug += "[" + juce::String(i) + "]: " + mic->name + " (" + juce::String(mic->specs.channels) + "ch)";
        }
        LOG(Debug, debug);
    }

    
    void assignVariants(KitLib::ArticulationSpec* artSpec) {
        this->variantsPerMic.clear();
        this->allVariants.clear();

        // Helper to compare mic specs (ignore variant suffix)
        auto micKeyEq = [](const KitLib::MicSpec& a, const KitLib::MicSpec& b) {
            return a.type == b.type && a.position == b.position && a.format == b.format;
        };

        // Count how many variants belong to each mic entry across all velocity layers
        for (const auto& velSpec : artSpec->velocityLayerSpecs) {
            for (const auto& varSpec : velSpec->sampleVariantSpecs) {
                this->allVariants.emplace_back(varSpec.get(), -1); //FIXME: 0?
            }
        }

        for (size_t vi = 0; vi < this->allVariants.size(); ++vi) {
            const KitLib::SampleVariantSpec* var = this->allVariants[vi].first;
            bool matched = false;

            for (size_t mi = 0; mi < micEntriesForInst.size(); ++mi) {
                const auto* mic = micEntriesForInst[mi];

                if (micKeyEq(var->micSpec, mic->specs)) {
                    this->variantsPerMic[mi] += 1;
                    this->allVariants[vi].second = static_cast<int>(mi);
                    matched = true;
                    break;
                }
            }

            if (!matched) {
                // assign fallback: try to match by mic type, else use first mic entry
                int fallback = -1;

                for (size_t mi = 0; mi < micEntriesForInst.size(); ++mi) {
                    if (micEntriesForInst[mi]->specs.type == var->micSpec.type) {
                        fallback = static_cast<int>(mi);
                        break;
                    }
                }

                if (fallback == -1 && !micEntriesForInst.empty()) {
                    fallback = 0;
                }
                if (fallback >= 0) {
                    this->variantsPerMic[fallback] += 1;
                    this->allVariants[vi].second = fallback;
                } else {
                    //FIXME: ? no mic entries at all: mark as unmatched
                    this->unmatchedVariantCount += 1;
                    this->allVariants[vi].second = -1;
                }
            }
        }

        juce::String debug = "    Variants per mic: ";
        for (size_t i = 0; i < this->micEntriesForInst.size(); ++i) {
            if (i > 0) debug += ", ";
            int cnt = this->variantsPerMic.count(i) ? this->variantsPerMic[i] : 0;
            debug += "[" + juce::String(i) + "]:" + juce::String(cnt);
        }
        debug += ". " + juce::String(this->unmatchedVariantCount) + " unmatched";
        LOG(Debug, debug);
    }

    void computeMicGroups(std::vector<int>& micBufStartChannels, std::vector<int>& micEntryChannelSizes, int& unmatchedStart) {
        micBufStartChannels.clear();
        micEntryChannelSizes.clear();

        // Build micBufStartChannels using the instrument's actual host mapping so that
        // mic-buffer indices align with the host-output mapping assigned earlier

        int mappingCursor = 0;

        for (size_t i = 0; i < micEntriesForInst.size(); ++i) {
            micBufStartChannels.push_back(mappingCursor);
            int ch = micEntriesForInst[i]->specs.channels;
            micEntryChannelSizes.push_back(ch);
            mappingCursor += ch;
        }

        juce::String debug = "    Mic Starts: ";
        for (size_t i = 0; i < micBufStartChannels.size(); ++i) {
            if (i > 0) debug += ", ";
            debug += "" + juce::String(micBufStartChannels[i]) + " (" + juce::String(micEntryChannelSizes[i]) + "ch)";
        }
        debug += ". Total: " + juce::String(mappingCursor);
        LOG(Debug, debug);

        // unmatched variants (if any) go after mapped region
        unmatchedStart = mappingCursor;
    }
};
