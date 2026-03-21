#pragma once

#include "InstrumentSynth.h"
#include "DrumKitEngine.h"
#include "MicRoutingBuilder.h"
#include "SampleLoader.h"

const MicLayout* InstrumentSynth::getMicLayout() const { return this->parent ? this->parent->getMicLayout() : nullptr; }
SampleLoader* InstrumentSynth::getSampleLoader() { return this->parent ? this->parent->getSampleLoader() : nullptr; }

void InstrumentSynth::unloadInstrument() {
    this->runtimeArticulations.clear();
    // stop and clear voices before releasing sounds
    this->clearVoices();
    this->clearSounds();
    this->noteToArticulation.fill(-1);
    this->instrumentSpecstPtr = nullptr;
}

void InstrumentSynth::loadInstrument(KitLib::InstrumentSpec* instSpec) {
    if (instSpec == nullptr) return;
    this->unloadInstrument();

    this->instrumentSpecstPtr = instSpec;
    this->type = instSpec->meta.type;

    this->processLayoutArticulations(instSpec);
}


void InstrumentSynth::processLayoutArticulations(KitLib::InstrumentSpec* instSpec) {
    LOG(Debug, "processLayoutArticulations: available articulationSpecs for inst=" + instSpec->meta.codeRaw + ":");
    for (const auto& artSpec : instSpec->articulationSpecs) {
        this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId); // discoverArticulations

        juce::String dims;
        for (const auto& dim : artSpec->expressionDimensions)
            dims += "[" + dim->typeToString(dim->type) + ", " + midiVectorToString(dim->values) + "]";
        LOG(Debug, "  nameId=" + juce::String((int)artSpec->nameId) + " exprId=" + juce::String((int)artSpec->exprId) + " exprDims=" + dims + " artSpec=" + artSpec->codeRaw);
    }
    CcDimensionsMap foundCcDimensions;
    for (const auto& layoutArt : this->instrumentSlot.articulations) {
        LOG(Info, "  Loading articulation: " + layoutArt.codeRaw + " (notes " + midiVectorToString(layoutArt.notes) + ")");

        KitLib::ArticulationSpec* resolvedSpec = nullptr;

        // 1. Find exact articulation for sample playback
        for (const auto& artSpec : instSpec->articulationSpecs) {
            if (artSpec->nameId == layoutArt.nameId && artSpec->exprId == layoutArt.exprId) {
                resolvedSpec = artSpec.get();
                break;
            }
        }

        // 2. Fallback if exact articulation not found
        if (!resolvedSpec) {
            if (this->getEngine() != nullptr) {
                const auto& fallbackIdMap = this->getEngine()->getArticulationFallbackIdMap();

                auto it = fallbackIdMap.find(layoutArt.nameId);
                if (it != fallbackIdMap.end()) {

                    for (auto& fallbackId : it->second) {
                        for (const auto& artSpec : instSpec->articulationSpecs) {
                            if (artSpec->nameId == fallbackId && artSpec->exprId == layoutArt.exprId) {
                                resolvedSpec = artSpec.get();
                                auto codeRaw = StringsDictSingleton::nameOf(artSpec->nameId);
                                if (layoutArt.exprId != 0) codeRaw += "[" + StringsDictSingleton::nameOf(artSpec->exprId) + "]";
                                LOG(Notice, instSpec->parent->meta.name + "/" + instSpec->meta.codeRaw + " has no articulation '" + layoutArt.codeRaw + "' for '" + this->instrumentSlot.id + "'. Using '" + codeRaw + "' instead");
                                break;
                            }
                        }
                        if (resolvedSpec) break;
                    }
                }
            }
        }


        // 3. Process articulation normally
        if (resolvedSpec != nullptr) {
            const auto artIdx = this->getOrAddRuntimeArticulationIdx(resolvedSpec->nameId, resolvedSpec->exprId);
            auto& artRt = this->runtimeArticulations[artIdx];
            resolvedSpec->sortVelocityLayers();
            this->processArticulationSpec(instSpec, layoutArt, resolvedSpec, artRt, artIdx, foundCcDimensions);
        } else {
            LOG(Warning, instSpec->parent->meta.name + "/" + instSpec->meta.codeRaw + " has no suitable articulations for '" + this->instrumentSlot.id + "' '" + layoutArt.codeRaw + "'");
        }

        // 4. ALSO collect CC thresholds from sibling exprIds
        
        for (const auto& artSpec : instSpec->articulationSpecs) {
            if (artSpec->nameId != layoutArt.nameId) continue;
            if (artSpec->exprId == layoutArt.exprId) continue; // already processed
            bool hasMatchingController = false;
            for (const auto& dim : artSpec->expressionDimensions) {
                for (const auto& controller : this->instrumentSlot.controllers) {
                    if (dim->type == controller.codeIdx && containsSameValue(controller.notes, layoutArt.notes)) {
                        hasMatchingController = true;
                        break;
                    }
                }
                if (hasMatchingController) break;
            }
            if (!hasMatchingController) continue;
            const auto artIdx = this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId);
            this->processArticulationSpec(instSpec, layoutArt, artSpec.get(), this->runtimeArticulations[artIdx], artIdx, foundCcDimensions);
        }
    }

    // Build final CC threshold maps
    this->processCcDimensions(foundCcDimensions);
    // this->buildCcDimensionTables(instSpec, foundCcDimensions);
}


void InstrumentSynth::processArticulationSpec(KitLib::InstrumentSpec* instSpec
  , const LayoutArticulation& layoutArt
  , KitLib::ArticulationSpec* artSpec
  , ArticulationRuntime& artRt
  , ArtRtIdx artRtIdx
  , CcDimensionsMap& foundCcDimensions
) {
    std::vector<int> micBufStartChannels; // start channel in micBuffer for each mic entry assigned to this instrument
    std::vector<int> micEntryChannelSizes; // number of channels for each mic entry
    std::vector<const MicLayout::Mic*> micEntriesForInst;
    int unmatchedStart = 0;

    MicRoutingBuilder micRouting(this);

    micRouting.build(artSpec, micBufStartChannels, micEntryChannelSizes, unmatchedStart);

    this->createVelocityLayers(instSpec, layoutArt.notes, artSpec, artRt, micBufStartChannels, micEntryChannelSizes, unmatchedStart, micRouting);
    artRt.buildVelocityMapping();

    for (uint8_t note : layoutArt.notes) this->noteToArticulation[note] = artRtIdx; // mapNotesToArticulation

    this->buildCcMappings(layoutArt, artSpec, foundCcDimensions);
    // this->runtimeArticulations.push_back(std::move(artRt));
}

void InstrumentSynth::createVelocityLayers(KitLib::InstrumentSpec* instSpec
    , const KitLayout::MidiNotes& notes
    , KitLib::ArticulationSpec* artSpec
    , ArticulationRuntime& artRt
    , const std::vector<int>& micBufStartChannels
    , const std::vector<int>& micEntryChannelSizes
    , int& unmatchedStart
    , MicRoutingBuilder& micRouting
) {
    size_t variantIdx = 0;

    for (const auto& velSpec : artSpec->velocityLayerSpecs) {
        VelocityLayerRuntime layer;
        layer.velocity = velSpec->velocity;
        layer.samples.reserve(velSpec->sampleVariantSpecs.size());

        for (const auto& varSpec : velSpec->sampleVariantSpecs) {
            auto* sound = new SampledSound(instSpec, varSpec.get(), varSpec->sampleFile, notes);
            sound->decayEnvelope = KitLib::DecayEnvelope::decode(varSpec->decayCode, varSpec->peakDb, varSpec->peakMs);
            if (!sound->decayEnvelope.isValid()) {
                LOG(Warning, "Missing or invalid decay metadata for sample: " + varSpec->sampleFile.getFileName() + ". Pedal articulation replacement will be skipped for this sample.");
            }
            int fileChannelCount = varSpec->fileChannelCount > 0
                ? varSpec->fileChannelCount
                : varSpec->micSpec.channels
            ;
            int assignedMicIndex = micRouting.getVariant(variantIdx);

            // if (variantIdx < micBufStartChannels.size()) //TODO: verify/test this is not needed
            //     assignedMicIndex = static_cast<int>(variantIdx);

            if (assignedMicIndex >= 0 && assignedMicIndex < static_cast<int>(micBufStartChannels.size())) {
                // All variants for the same mic map to the same mic buffer group
                sound->micBufferChannelStart = micBufStartChannels[assignedMicIndex];
                sound->micBufferChannelCount = micEntryChannelSizes[assignedMicIndex];
            } else {
                // unmatched: append after mic blocks, allocate sequentially using fileChannelCount
                sound->micBufferChannelStart = unmatchedStart;
                sound->micBufferChannelCount = fileChannelCount;
                unmatchedStart += fileChannelCount;
            }
            this->addSound(sound);
            SampleEntry se;
            se.sound = sound;
            // Store variant key (1..26) as returned by SampleVariantSpec::letterToIdx (0 if no letter)
            se.variantIdx = static_cast<uint8_t>(KitLib::SampleVariantSpec::letterToIdx(varSpec->letter));
            layer.samples.push_back(se);

            SampleLoader* loader = this->getSampleLoader();
            if (loader != nullptr) {
                loader->getQueue().enqueueCandidate(
                    varSpec->sampleFile,
                    instSpec->meta.type,
                    artSpec->nameId,
                    velSpec->velocity,
                    KitLib::SampleVariantSpec::letterToIdx(varSpec->letter),
                    [sound](std::shared_ptr<juce::AudioBuffer<float>> buf, double sr) {
                        sound->buffer = buf;
                        sound->sampleRate = sr;
                    }
                );
            }
            ++variantIdx;
        }
        artRt.layers.push_back(std::move(layer));
    }
    int computedMicChannels = unmatchedStart;
    const auto& hostMapForInst = this->getMicOutputMapping();
    int assignedHostChannels = static_cast<int>(hostMapForInst.size());
    if (assignedHostChannels > 0 && computedMicChannels > assignedHostChannels) { //TODO: give more channels?
        LOG(Warning, "Instrument '" + this->getId() + "' mic channels number (" + juce::String(computedMicChannels) + ") is greater than channel number we got from host mapping channels (" + juce::String(assignedHostChannels) + "). This may cause out-of-range writes.");
        // jassertfalse;
    }
}


void InstrumentSynth::buildCcMappings(const LayoutArticulation& layoutArt, KitLib::ArticulationSpec* artSpec, CcDimensionsMap& foundCcDimensions) {

    { // Debug: report articulation and its expression dimensions
        auto code = artSpec->codeRaw;
        LOG(Debug, "buildCcMappings: processing artSpec=" + code + " nameId=" + juce::String((int)artSpec->nameId) + " exprId=" + juce::String((int)artSpec->exprId) + " layoutArt=" + layoutArt.codeRaw);
        if (artSpec->expressionDimensions.empty()) {
            LOG(Debug, "buildCcMappings: artSpec has no expressionDimensions");
        } else {
            juce::String dims;
            for (const auto& dim : artSpec->expressionDimensions)
                dims += "[" + dim->typeToString(dim->type) + ", " + midiVectorToString(dim->values) + "]";
            LOG(Debug, "buildCcMappings: expressionDimensions=" + dims);
        }
    }

    for (const auto& cc : this->instrumentSlot.controllers) {
        for (uint8_t note : cc.notes) {
            auto ccNum = static_cast<uint8_t>(cc.number);
            auto& vec = this->noteToControllers[note];
            if (std::find(vec.begin(), vec.end(), ccNum) == vec.end())
                vec.emplace_back(ccNum);
        }

        // only controllers with notes that are linked to this articulation
        if (containsSameValue(cc.notes, layoutArt.notes)) {
            LOG(Debug, "buildCcMappings: controller " + juce::String((int)cc.number) + " {num=" + juce::String((int)cc.number) + ",codeIdx=" + juce::String((int)cc.codeIdx) + ", notes=" + midiVectorToString(cc.notes) + "} applies to articulation " + layoutArt.codeRaw + " (layoutArt.notes=" + midiVectorToString(layoutArt.notes) + ")");
            // store refs for dimensions to process later
            for (const auto& expDim : artSpec->expressionDimensions) {
                if (expDim->type == cc.codeIdx) {
                    for (const auto& val : expDim->values) {
                        auto ccNum = static_cast<uint8_t>(cc.number);
                        const auto artIdx = this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId);
                        const auto pair = std::make_pair(static_cast<int8_t>(val), artIdx);
                        auto& vec = foundCcDimensions[ccNum];
                        // avoid inserting identical (value,artIdx) duplicates
                        // if (std::find(vec.begin(), vec.end(), pair) == vec.end()) {
                            vec.emplace_back(pair);
                            LOG(Debug, "buildCcMappings: cc=" + juce::String((int)ccNum) + " val=" + juce::String((int)val) + " (artIdx=" + juce::String(artIdx) + ") layoutArt=" + layoutArt.codeRaw);
                        // } else {
                        //     LOG(Warning, "buildCcMappings: skipping duplicate mapping cc=" + juce::String((int)ccNum) + " val=" + juce::String((int)val) + " (artIdx=" + juce::String(artIdx) + ") layoutArt=" + layoutArt.codeRaw);
                        // }
                    }
                }
            }
        }
    }
}


// void InstrumentSynth::buildCcDimensionTables(KitLib::InstrumentSpec* instSpec, CcDimensionsMap& foundCcDimensions) {
//     for (const auto& artSpec : instSpec->articulationSpecs) {
//         int8_t artIdx = this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId);

//         for (const auto& dim : artSpec->expressionDimensions) {
//             uint8_t ccNum = this->controllerNumberFromType(dim->type);

//             for (auto val : dim->values) {
//                 foundCcDimensions[ccNum].push_back({ static_cast<int8_t>(val), artIdx });
//             }
//         }
//     }

//     this->processCcDimensions(foundCcDimensions);
// }
void InstrumentSynth::processCcDimensions(CcDimensionsMap& foundCcDimensions) {
    for (auto&[ccNum, valArtPairs] : foundCcDimensions) {
        LOG(Debug, "processCcDimensions: CC " + juce::String((int)ccNum) + " has " + juce::String((int)valArtPairs.size()) + " mappings");
        std::sort(valArtPairs.begin(), valArtPairs.end(), [](auto& a, auto& b){
            return a.first < b.first;
        });

        std::array<uint8_t, 128> map;
        const int8_t count = static_cast<uint8_t>(valArtPairs.size());

        for (int value = 0; value <= MIDI_MAX_VALUE; ++value) {
            uint8_t chosen = valArtPairs[count - 1].second; // default to last
            for (uint8_t idx = 0; idx < count; ++idx) {
                if (value <= static_cast<int>(valArtPairs[idx].first)) {
                    chosen = valArtPairs[idx].second;
                    break;
                }
            }
            map[value] = chosen;
        }
        this->ccToArticulation[ccNum] = map;
        // log condensed mapping: thresholds -> articulation index
        juce::String summary;
        for (size_t i = 0; i < valArtPairs.size(); ++i) {
            if (i) summary += ",";
            summary += "(" + juce::String((int)valArtPairs[i].first) + ":" + this->getArtName(valArtPairs[i].second) + " (artIdx=" + juce::String(valArtPairs[i].second) + "))";
        }
        LOG(Debug, "processCcDimensions: CC " + juce::String((int)ccNum) + " thresholds: " + summary);
    }
}

