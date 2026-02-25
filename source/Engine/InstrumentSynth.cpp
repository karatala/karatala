#pragma once

#include "DrumKitEngine.h"
#include "InstrumentSynth.h"

void InstrumentSynth::unloadInstrument() {
    this->runtimeArticulations.clear();
    this->sampledSoundPtrs.clear();
    this->noteToArticulation.fill(-1);
    this->clearSounds();
    this->instrumentSpecstPtr = nullptr;
}

void InstrumentSynth::loadInstrument(KitLib::InstrumentSpec* instSpec) {
    if (instSpec == nullptr) return;
    this->unloadInstrument();

    this->instrumentSpecstPtr = instSpec;
    this->type = instSpec->meta.type;

    this->processLayoutArticulations(instSpec);
}

int8_t InstrumentSynth::findArticulationIndex(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId) const {
    const int8_t count = static_cast<int8_t>(this->runtimeArticulations.size());
    for (int8_t i = 0; i < count; ++i) {
        const auto& artRt = this->runtimeArticulations[i];
        if (artRt.nameId == nameId && artRt.exprId == exprId)
            return i;
    }
    return -1;
}

void InstrumentSynth::processLayoutArticulations(KitLib::InstrumentSpec* instSpec) {
    for (const auto& layoutArt : this->instrumentSlot.articulations) {
        LOG(Info, "  Loading articulation: " + layoutArt.codeRaw + " (notes " + midiVectorToString(layoutArt.notes) + ")");

        InstrumentSynth::ArticulationRuntime artRt(layoutArt.nameId, layoutArt.exprId);
        CcDimensionsMap foundCcDimensions;
        KitLib::ArticulationSpec* resolvedArtSpec = this->resolveArticulationSpec(instSpec, layoutArt);
        if (resolvedArtSpec != nullptr) {
            resolvedArtSpec->sortVelocityLayers();
            this->processArticulationSpec(instSpec, layoutArt, resolvedArtSpec, artRt, foundCcDimensions);
        } else {
            LOG(Warning, instSpec->parent->meta.name + "/" + instSpec->meta.codeRaw + " has no suitable articulations for '" + this->instrumentSlot.id + "' '" + layoutArt.codeRaw + "'");
        }
        this->processCcDimensions(foundCcDimensions);
    }
}

void InstrumentSynth::processArticulationSpec(KitLib::InstrumentSpec* instSpec, const LayoutArticulation& layoutArt, KitLib::ArticulationSpec* artSpec, ArticulationRuntime& artRt, CcDimensionsMap& foundCcDimensions) {
    std::vector<int> micBufStartChannels; // start channel in micBuffer for each mic entry assigned to this instrument
    std::vector<int> micEntryChannelSizes; // number of channels for each mic entry
    std::vector<const MicLayout::Mic*> micEntriesForInst;
    int unmatchedStart = 0;

    MicRoutingBuilder micRouting(this);

    micRouting.build(artSpec, micBufStartChannels, micEntryChannelSizes, unmatchedStart);

    this->createVelocityLayers(instSpec, layoutArt, artSpec, artRt, micBufStartChannels, micEntryChannelSizes, unmatchedStart, micRouting);
    this->buildVelocityMapping(artRt);
    this->mapNotesToArticulation(layoutArt);
    this->buildCcMappings(layoutArt, artSpec, foundCcDimensions);
    this->runtimeArticulations.push_back(std::move(artRt));
}

void MicRoutingBuilder::build(KitLib::ArticulationSpec* artSpec, std::vector<int>& micBufStartChannels, std::vector<int>& micEntryChannelSizes, int& unmatchedStart) {
    this->collectMicEntries();
    this->assignVariants(artSpec);
    this->computeMicGroups(micBufStartChannels, micEntryChannelSizes, unmatchedStart);
}

void MicRoutingBuilder::collectMicEntries() {
    this->micEntriesForInst.clear();

    const MicLayout* micLayout = this->synth->getEngine()->getMicLayout();

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

void MicRoutingBuilder::assignVariants(KitLib::ArticulationSpec* artSpec) {
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
    debug += "unmatched=" + juce::String(this->unmatchedVariantCount);
    LOG(Debug, debug);
}

void MicRoutingBuilder::computeMicGroups(std::vector<int>& micBufStartChannels, std::vector<int>& micEntryChannelSizes, int& unmatchedStart) {
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

void InstrumentSynth::createVelocityLayers(KitLib::InstrumentSpec* instSpec
, const LayoutArticulation& layoutArt
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
            auto* sound = new SampledSound(instSpec, varSpec.get(), varSpec->sampleFile, layoutArt.notes);
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
            this->sampledSoundPtrs.push_back(sound);

            SampleEntry se;
            se.sound = sound;

            // Store variant key (1..26) as returned by SampleVariantSpec::letterToIdx (0 if no letter)
            se.variantIdx = static_cast<uint8_t>(KitLib::SampleVariantSpec::letterToIdx(varSpec->letter));

            layer.samples.push_back(se);

            const auto loader = this->getEngine()->getSampleLoader();
            if (loader != nullptr) {
                loader->requestAsyncLoad(
                    varSpec->sampleFile, 
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
    if (assignedHostChannels > 0 && computedMicChannels > assignedHostChannels) {//TODO: give more channels?
        LOG(Warning, "Instrument '" + this->getId() + "' mic channels number (" + juce::String(computedMicChannels) + ") is greater than channel number we got from host mapping channels (" + juce::String(assignedHostChannels) + "). This may cause out-of-range writes.");
        // jassertfalse;
    }
}

void InstrumentSynth::buildVelocityMapping(ArticulationRuntime& artRt) {
    if (artRt.layers.empty()) return;
    // choose the first layer whose configured velocity is >= current velocity.
    // Example: layers {27,52,77,102,127} -> 0..27->0, 28..52->1, 53..77->2, 78..102->3, 103..127->4
    const int8_t count = static_cast<int8_t>(artRt.layers.size());
    for (int value = 0; value <= MIDI_MAX_VALUE; ++value) {
        int8_t chosen = count - 1; // default to last layer
        for (int8_t idx = 0; idx < count; ++idx) {
            if (value <= static_cast<int>(artRt.layers[idx].velocity)) {
                chosen = idx;
                break;
            }
        }
        artRt.velocityToLayer[value] = chosen;
    }
}

void InstrumentSynth::mapNotesToArticulation(const LayoutArticulation& layoutArt) {
    int8_t artIdx = static_cast<int8_t>(this->runtimeArticulations.size());
    for (uint8_t note : layoutArt.notes)
        this->noteToArticulation[note] = artIdx;
}

void InstrumentSynth::buildCcMappings(const LayoutArticulation& layoutArt, KitLib::ArticulationSpec* artSpec, CcDimensionsMap& foundCcDimensions) {
    for (const auto& cc : this->instrumentSlot.controllers) {
        for (uint8_t note : cc.notes) {
            auto ccNum = static_cast<uint8_t>(cc.number);
            auto& vec = this->noteToControllers[note];

            if (std::find(vec.begin(), vec.end(), ccNum) == vec.end())
                vec.emplace_back(ccNum);
        }

        // only controllers with notes that are linked to this articulation
        if (containsSameValue(cc.notes, layoutArt.notes)) {
            // store refs for dimensions to process later
            for (const auto& expDim : artSpec->expressionDimensions) {
                if (expDim->type == cc.codeIdx) {
                    for (const auto& val : expDim->values) {
                        auto ccNum = static_cast<uint8_t>(cc.number);
                        const auto pair = std::make_pair(
                            static_cast<int8_t>(val),
                            static_cast<int8_t>(this->runtimeArticulations.size())
                        );
                        auto& vec = foundCcDimensions[ccNum];
                        vec.emplace_back(pair);
                    }
                }
            }
        }
    }
}

void InstrumentSynth::processCcDimensions(CcDimensionsMap& foundCcDimensions) {
    for (auto&[ccNum, valArtPairs] : foundCcDimensions) {            
        std::sort(valArtPairs.begin(), valArtPairs.end(), [](auto& a, auto& b){
            return a.first < b.first;
        });
        std::array<uint8_t, 128> map;
        const int8_t count = static_cast<uint8_t>(valArtPairs.size());

        for (int value = 0; value <= MIDI_MAX_VALUE; ++value) {
            uint8_t chosen = count - 1; // default to last
            for (uint8_t idx = 0; idx < count; ++idx) {
                if (value <= static_cast<int>(valArtPairs[idx].first)) {
                    chosen = valArtPairs[idx].second;
                    break;
                }
            }
            map[value] = chosen;
        }
        this->ccToArticulation[ccNum] = map;
    }
}


KitLib::ArticulationSpec* InstrumentSynth::resolveArticulationSpec(KitLib::InstrumentSpec* instSpec, const LayoutArticulation& layoutArt) {
    // Exact match
    for (const auto& artSpec : instSpec->articulationSpecs) {
        if (artSpec->nameId == layoutArt.nameId && artSpec->exprId == layoutArt.exprId) {
            return artSpec.get();
        }
    }

    if (this->getEngine() == nullptr) return nullptr;
    const auto fallbackIdMap = this->getEngine()->getArticulationFallbackIdMap();

    // fallback
    auto it = fallbackIdMap.find(layoutArt.nameId);
    if (it == fallbackIdMap.end())
        return nullptr;
    for (auto& fallbackId : it->second) {
        for (const auto& artSpec : instSpec->articulationSpecs) {
            if (artSpec->nameId != fallbackId || artSpec->exprId != layoutArt.exprId)
                continue;
            auto codeRaw = StringsDictSingleton::nameOf(artSpec->nameId);
            if (layoutArt.exprId != 0)
                codeRaw += "[" + StringsDictSingleton::nameOf(artSpec->exprId) + "]";
            LOG(Notice, instSpec->parent->meta.name + "/" + instSpec->meta.codeRaw + " has no articulation '" + layoutArt.codeRaw + "' for '" + this->instrumentSlot.id + "'. Using '" + codeRaw + "' instead");
            return artSpec.get();
        }
    }
    return nullptr;
}

void InstrumentSynth::handleMidiEvent(const juce::MidiMessage& midiMessage) {
    
    if (midiMessage.isNoteOn()) {
        const int note = midiMessage.getNoteNumber();
        if (noteToArticulation[note] == -1) return;

        this->playNote(midiMessage.getChannel(), note, midiMessage.getVelocity(), 1);
        // || this->noteOn(midiMessage.getChannel(), note, midiMessage.getFloatVelocity());
    }
    else if (midiMessage.isController()) {
        auto ccNum = static_cast<uint8_t>(midiMessage.getControllerNumber());
        if (noteToControllers.find(ccNum) == noteToControllers.end()) return;
        // this->handleController(midiMessage.getChannel(), ccNum, midiMessage.getControllerValue());
    }
    
    else if (midiMessage.isAftertouch())
        this->handleAftertouch(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getAfterTouchValue());
    
        
    else if (midiMessage.isAllNotesOff() || midiMessage.isAllSoundOff()) this->allNotesOff(midiMessage.getChannel(), true);
        
    // else if (midiMessage.isNoteOff()) this->noteOff (channel, midiMessage.getNoteNumber(), midiMessage.getFloatVelocity(), true);
    // else if (midiMessage.isChannelPressure()) this->handleChannelPressure(channel, midiMessage.getChannelPressureValue());
    // else if (midiMessage.isProgramChange()) this->handleProgramChange(channel, midiMessage.getProgramChangeNumber());
    // else if (midiMessage.isPitchWheel()) {
    //     const int wheelPos = midiMessage.getPitchWheelValue();
    //     lastPitchWheelValues[channel - 1] = wheelPos;
    //     this->handlePitchWheel(channel, wheelPos);
    // } 
    
}

bool InstrumentSynth::playNote(const int midiChannel, const int midiNoteNumber, const juce::uint8 velocity, int expressions) {
    /* TODO: need to plan a fix for next problems:
        1. currently if playing cymbal loudly and fast (swell many times) it launches many voices, and then if suddenly start doing it very quietly, it will mute loud voices at some point (when ran out of voices) and play only quiet samples, which sounds unrealistic.
        2. if playing more closed hihat articulation right after more opened, opened keep sounding.
        3. when loud notes played fast, sometimes waveforms can counterphase/double volume of each other.

        if new note louder then ol -> fast fade out and mark old note's voice to this->stopNote, or maybe even better crossfade old and new notes by POLYPHONY_CROSFADE_TIMEOUT_MS=20
        according some dynamic hardcoded mapping that shows which articulation can mute another, like cymbal bow/edge can mute each other and self (if last is louder)
                
        For hihat pedal state without note should crossfade changes, like if hitted closed hat and start opening pedal it should crossfade to more opened sound (new more open sample plays from small offset so it is without attack) so more opened starts from time offset when pedal changestarted with HIHAT_PEDAL_CROSSFADE_TIMEOUT_MS=50
        Maybe we have to use juces shouldStealNotes and override findVoiceToSteal - idk.

        ??? do we need to make InstrumentSynth abstract and extend with several classes like InstrumentSynthCymbal, InstrumentSynthDrum and for special cases InstrumentSynthHiHat, 
    */


    //     if (voice->getCurrentlyPlayingNote() == midiNoteNumber && voice->isPlayingChannel (midiChannel))
    //         stopVoice (voice, 1.0f, true);

    // startVoice (findFreeVoice (sound, midiChannel, midiNoteNumber, shouldStealNotes), sound, midiChannel, midiNoteNumber, velocity);
    
    juce::ignoreUnused(expressions);

    // Choose articulation based on MIDI note mapping
    int artIdx = -1;
    if (this->runtimeArticulations.empty()) {
        // fallback: try to use first layout articulation if any
        if (!this->instrumentSlot.articulations.empty()) {
            // find index 0
            artIdx = 0;
        }
    } else {
        if (midiNoteNumber >= 0 && midiNoteNumber < 128) {
            artIdx = this->noteToArticulation[midiNoteNumber];
            
            if (!this->ccToArticulation.empty() && this->parent != nullptr) {
                auto pair = this->noteToControllers.find(static_cast<uint8_t>(midiNoteNumber));
                if (pair != this->noteToControllers.end()) {
                    for (auto ccNum : pair->second) {
                        auto artIt = this->ccToArticulation.find(ccNum);
                        if (artIt == this->ccToArticulation.end()) continue;
                        const auto ccVal = this->parent->getLatestContinuousControllerValue(ccNum);
                        const auto& mapArr = artIt->second;

                        artIdx = static_cast<int>(mapArr[ccVal]);
                        // LOG(Verbose, "CC " + juce::String(ccNum) + " value " + juce::String(ccVal) + " maps to expression " + juce::String(artIdx) + " for note " + juce::String(midiNoteNumber) );
                    }
                }
            }
        }
    }

    if (artIdx < 0) {
        // LOG(Warning, "InstrumentSynth::playNote - no articulation available for note " + juce::String(midiNoteNumber));
        return false;
    }

    // Play all samples from the chosen velocity layer (one voice per sample/mic variant)
    auto& art = this->runtimeArticulations[artIdx];
    int layerIdx = art.velocityToLayer[static_cast<int>(velocity)];
    if (layerIdx < 0 || layerIdx >= static_cast<int>(art.layers.size())) return false;

    auto& layer = art.layers[layerIdx];
    if (layer.samples.empty()) {
        LOG(Info, "Nothing to play for note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)));
        return false;
    }

    const juce::ScopedLock sl (this->lock);
    const bool stealNotes = true;
    float velf = juce::jlimit(0.0f, 1.0f, static_cast<float>(velocity) / 127.0f);
    bool anyStarted = false;
    // Build list of unique variant keys present in this layer (preserve order of first appearance)
    std::vector<uint8_t> uniqueVariants;
    uniqueVariants.reserve(layer.samples.size());
    for (const auto& se : layer.samples) {
        uint8_t key = se.variantIdx;
        bool found = false;
        for (auto k : uniqueVariants) { if (k == key) { found = true; break; } }
        if (!found) uniqueVariants.push_back(key);
    }

    // If there are multiple variant groups, pick one using round-robin counter; otherwise use the single group
    uint8_t chosenKey = 0;
    if (!uniqueVariants.empty()) {
        if (uniqueVariants.size() == 1) {
            chosenKey = uniqueVariants[0];
        } else {
            uint32_t rr = layer.roundRobinCounter->fetch_add(1u);
            size_t idx = static_cast<size_t>(rr % static_cast<uint32_t>(uniqueVariants.size()));
            chosenKey = uniqueVariants[idx];
        }
    }

    // Start voices only for samples that belong to the chosen variant group
    for (const auto& se : layer.samples) {
        if (se.sound == nullptr) continue;
        if (se.variantIdx != chosenKey) continue;
        juce::SynthesiserVoice* voice = findFreeVoice(se.sound, midiChannel, midiNoteNumber, stealNotes);
        if (voice == nullptr) continue;
        auto& instCode = se.sound->sampleVariantSpec->parent->parent->parent->meta.codeRaw;
        auto& artCode = se.sound->sampleVariantSpec->parent->parent->codeRaw;
        auto& filePath = se.sound->sampleVariantSpec->sampleFile.getFullPathName();
        LOG(Debug, "startVoice note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)) + " inst " + instCode + " art " + artCode + " micBuf(" + juce::String(se.sound->micBufferChannelStart) + "," + juce::String(se.sound->micBufferChannelCount) + ") file=" + filePath);
        this->startVoice(voice, se.sound, midiChannel, midiNoteNumber, velf);
        anyStarted = true;
    }
    if (!anyStarted) {
        LOG(Warning, "InstrumentSynth::playNote - no free voice available for any sample in layer");
        return false;
    }
    const auto& paVal = this->parent->getLatestPolyphonicAftertouchValue(midiNoteNumber);
    if (paVal > 0)
        this->handleAftertouch(midiChannel, midiNoteNumber, paVal);       
            
    return true;
}

void InstrumentSynth::noteOn (const int midiChannel, const int midiNoteNumber, const float velocity) {
    juce::ignoreUnused(midiChannel, midiNoteNumber, velocity);
    jassertfalse;
}

void InstrumentSynth::prepareToPlay(double newSampleRate, int samplesPerBlock) {
    // Ensure mic buffer has correct size
    if (mMicChannelCount <= 0)
        mMicChannelCount = 2;
    micBuffer.setSize(mMicChannelCount, samplesPerBlock);

    // Configure voices for this synth
    this->clearVoices();

    // Dynamically determine required voices based on the maximum number of simultaneous
    // sample entries that may be started for a single note. This counts how many
    // samples belong to the same round-robin variant group (variantIdx) per layer,
    // and uses the largest such group across all articulations and layers.
    int maxSimultaneousSamples = 0;
    for (const auto& art : this->runtimeArticulations) {
        for (const auto& layer : art.layers) {
            std::unordered_map<uint8_t, int> groupCounts;
            for (const auto& se : layer.samples) {
                uint8_t key = se.variantIdx; // 0 means no-letter variants
                groupCounts[key]++;
            }
            for (const auto& kv : groupCounts) {
                if (kv.second > maxSimultaneousSamples) maxSimultaneousSamples = kv.second;
            }
        }
    }

    // Minimum voices to keep behavior stable
    const int minVoices = 4;
    const int numVoices = std::max(minVoices, maxSimultaneousSamples * minVoices);
    for (int i = 0; i < numVoices; ++i)
        this->addVoice(new InstrumentSynthVoice());
    LOG(Debug, "prepareToPlay - instrument: " + this->instrumentSlot.id + " sampleRate=" + juce::String(newSampleRate) + " samplesPerBlock=" + juce::String(samplesPerBlock) + " numVoices=" + juce::String(numVoices));
    
    // set playback sample rate for voices
    this->setCurrentPlaybackSampleRate(newSampleRate);

    for (auto* voice : this->voices) //TODO: maybe refactor to separate method
        if (auto* v = dynamic_cast<InstrumentSynthVoice*>(voice))
            v->chokeProcessor.prepare(newSampleRate);
}

InstrumentSynthVoice::InstrumentSynthVoice() : gainParam(nullptr), panParam(nullptr), invertPhaseParam(nullptr) {
    this->pitchRatio = 1.0;
    this->velocityGain = 1.0f;
    this->sourceSamplePosition = 0.0;
}

void InstrumentSynthVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* s, int /*pitchWheel*/) {
    juce::ignoreUnused(midiNoteNumber);
    this->velocityGain = velocity;
    this->sourceSamplePosition = 0.0;
    this->adsr.setParameters({0.005f, 0.05f, 0.8f, 0.08f});
    this->adsr.setSampleRate(getSampleRate());
    this->adsr.noteOn();

    this->chokeProcessor.noteStarted();
    // LOG(Debug, "InstrumentSynthVoice: startNote velocity=" + juce::String(velocity) + " pitchRatio=" + juce::String(this->pitchRatio));

    if (auto* sampled = dynamic_cast<SampledSound*>(s)) {
        if (sampled->sampleRate > 0 && getSampleRate() > 0)
            this->pitchRatio = sampled->sampleRate / getSampleRate();
        else
            this->pitchRatio = 1.0;
    } else {
        this->pitchRatio = 1.0;
    }
}

void InstrumentSynthVoice::stopNote(float velocity, bool allowTailOff) {
    juce::ignoreUnused(velocity);
    if (allowTailOff) {
        this->adsr.noteOff();
    } else {
        this->adsr.reset();
        this->clearCurrentNote();
    }
}

void InstrumentSynthVoice::pitchWheelMoved(int /*newValue*/) {}
void InstrumentSynthVoice::controllerMoved(int /*controllerNumber*/, int /*newValue*/) {}

void InstrumentSynthVoice::aftertouchChanged (int newValue) {
    LOG(Debug, "aftertouchChanged " + juce::String(newValue));
    this->chokeProcessor.handlePolyAftertouch(newValue);
}

void InstrumentSynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) {
    if (!isVoiceActive())
        return;

    auto snd = getCurrentlyPlayingSound();
    auto* s = dynamic_cast<SampledSound*>(snd.get());
    if (s == nullptr || !s->buffer)
        return;

    auto& buf = *s->buffer;
    const int srcChannels = buf.getNumChannels();
    const int srcLen = buf.getNumSamples();
    // Determine source/destination channel mapping based on SampleVariantSpec metadata.
    int srcStart = 0;
    int srcCount = srcChannels;
    if (s->sampleVariantSpec) {
        srcStart = s->sampleVariantSpec->fileChannelStart;
        srcCount = s->sampleVariantSpec->fileChannelCount > 0 ? s->sampleVariantSpec->fileChannelCount : srcChannels;
    }

    const int destChCount = outputBuffer.getNumChannels();

    if (this->chokeProcessor.isMuted()) {
        this->stopNote(0, false);
        return;
    }
    for (int i = 0; i < numSamples; ++i) {
        
        //TODO: use peak offset for mics (found minimal for example di and take it as 0, based on that calculate for all other mics of that sample variant relatively, for example far room = 10ms etc...) first makke script to smart-parse from samples and save to filename
        // Process per-voice ducking for this block and get block gain
        const float chokeGain = this->chokeProcessor.getNextBlockGain();
 
        const double pos = this->sourceSamplePosition;
        const int idx = static_cast<int>(pos);
        const double frac = pos - idx;

        // For each source channel in this sample variant, fetch interpolated sample
        // and write to corresponding destination channel (offset by srcStart).
        if (idx >= 0 && idx < srcLen) {
            const float env = this->adsr.getNextSample();

            for (int sc = 0; sc < srcCount; ++sc) {
                int actualSrcCh = juce::jlimit(0, srcChannels - 1, srcStart + sc);
                float v0 = buf.getSample(actualSrcCh, idx);
                float v1 = (idx + 1 < srcLen) ? buf.getSample(actualSrcCh, idx + 1) : 0.0f;
                float sample = static_cast<float>(v0 + (v1 - v0) * frac);
                sample *= env * this->velocityGain * chokeGain;

                int destCh = s->micBufferChannelStart + sc;
                if (destCh >= 0 && destCh < destChCount) {
                    outputBuffer.addSample(destCh, startSample + i, sample);
                } else {
                    // Attempt to write outside of provided mic buffer — warn once per occurrence
                    // if (s->sampleVariantSpec) {
                    //     LOG(Warning, "Render: attempted write to micBuffer destCh=" + juce::String(destCh) + " but mic buffer has " + juce::String(destChCount) + " channels. Sample=" + s->sampleVariantSpec->sampleFileName);
                    // } else {
                    //     LOG(Warning, "Render: attempted write to micBuffer destCh=" + juce::String(destCh) + " but mic buffer has " + juce::String(destChCount) + " channels.");
                    // }
                }
            }
        } else {
            // still advance envelope even if out of source range
            (void) this->adsr.getNextSample();
        }

        this->sourceSamplePosition += this->pitchRatio;
        if (this->sourceSamplePosition >= srcLen) {
            // reached end
            this->adsr.reset();
            this->clearCurrentNote();
            break;
        }
    }
}
