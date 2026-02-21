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
    LOG(Debug, "INSTRUMENT: " + instSpec->meta.code + " (" + instSpec->meta.name + ")");
    this->instrumentSpecstPtr = instSpec;
    this->type = instSpec->meta.type;

    auto loader = this->getEngine()->getSampleLoader();

    // For each layout articulation create runtime entry
    struct MissingArt { juce::String codeRaw; std::vector<uint8_t> notes; };
    std::vector<MissingArt> missingArts;

    for (const auto& layoutArt : this->instrumentSlot.articulations) {
        InstrumentSynth::ArticulationRuntime artRt(layoutArt.codeId, layoutArt.exprId);

        LOG(Debug, "  ARTICULATION: " + layoutArt.codeRaw);
        std::unordered_map<int8_t, std::vector<std::pair<int8_t, int8_t>>> foundCcDimensions;

        bool found = false;
        int unmatchedStart = 0;
        for (const auto& artSpec : instSpec->articulationSpecs) {
            if (artSpec->codeId != layoutArt.codeId) continue;

            found = true;

            // ensure velocity layers are sorted
            artSpec->sortVelocityLayers();

            // Build velocity layers and create SampledSound objects
            // We want each mic (as defined in active mic layout) to occupy a contiguous block
            // in the synth's micBuffer. We'll group sampleVariantSpecs by matching mic entry
            // (type+position+format) and assign starts accordingly.
            std::vector<int> micStarts; // start channel in micBuffer for each mic entry assigned to this instrument
            std::vector<int> micChannels; // number of channels for each mic entry
            std::vector<const MicLayout::Mic*> micEntriesForInst;

            // Gather mic layout entries that apply to this instrument in the same order as applyMicLayout()
            if (this->parent && this->parent->getMicLayout() != nullptr) {
                const MicLayout* ml = this->parent->getMicLayout();

                std::vector<const MicLayout::Mic*> globals;
                std::vector<const MicLayout::Mic*> exactMatches;
                std::vector<const MicLayout::Mic*> typeMatches;

                for (const auto& mic : ml->mics) {
                    // 1. Global mics
                    if (mic.instrument.isEmpty()) {
                        globals.push_back(&mic);
                        continue;
                    }

                    // 2. Exact match (tom1 == tom1)
                    if (mic.instrument == this->getId()) {
                        exactMatches.push_back(&mic);
                        continue;
                    }

                    // 3. Type fallback candidate
                    juce::String typeStr = mic.instrument;

                    while (typeStr.length() > 0 && juce::CharacterFunctions::isDigit(typeStr.getLastCharacter())) {
                        typeStr = typeStr.dropLastCharacters(1);
                    }

                    auto instType = KitLib::InstrumentSpec::typeFromString(typeStr);

                    if (instType != KitLib::InstrumentSpec::Type::Unknown && instType == this->getType()) {
                        typeMatches.push_back(&mic);
                    }
                }

                // Always include globals
                micEntriesForInst.insert(micEntriesForInst.end(), globals.begin(), globals.end());

                // Prefer exact matches
                if (!exactMatches.empty()) {
                    micEntriesForInst.insert(micEntriesForInst.end(), exactMatches.begin(), exactMatches.end());
                } else {
                    micEntriesForInst.insert(micEntriesForInst.end(), typeMatches.begin(), typeMatches.end());
                }
            }


            // Debug: list mic entries that apply to this instrument
            {
                juce::String s = "    MicLayout entries: ";
                for (size_t i = 0; i < micEntriesForInst.size(); ++i) {
                    const auto* m = micEntriesForInst[i];
                    s += "[" + juce::String(i) + ":" + m->name + " (" + juce::String(m->specs.channels) + "ch)] ";
                }
                LOG(Debug, s);
            }

            // Helper to compare mic specs (ignore variant suffix)
            auto micKeyEq = [](const KitLib::MicSpec& a, const KitLib::MicSpec& b) {
                return a.type == b.type && a.position == b.position && a.format == b.format; // ignore variant
            };

            // Count how many variants belong to each mic entry across all velocity layers
            std::unordered_map<size_t, int> variantsPerMic; // index in micEntriesForInst -> count
            std::vector<std::pair<const KitLib::SampleVariantSpec*, int>> allVariants; // keep order for assignment
            for (const auto& velSpec : artSpec->velocityLayerSpecs) {
                for (const auto& varSpec : velSpec->sampleVariantSpecs) {
                    allVariants.emplace_back(varSpec.get(), 0);
                }
            }

            for (size_t vi = 0; vi < allVariants.size(); ++vi) {
                const KitLib::SampleVariantSpec* var = allVariants[vi].first;
                bool matched = false;
                for (size_t mi = 0; mi < micEntriesForInst.size(); ++mi) {
                    const auto* mic = micEntriesForInst[mi];
                    if (micKeyEq(var->micSpec, mic->specs)) {
                        variantsPerMic[mi] += 1;
                        allVariants[vi].second = static_cast<int>(mi);
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
                    if (fallback == -1 && !micEntriesForInst.empty()) fallback = 0;
                    if (fallback >= 0) {
                        variantsPerMic[fallback] += 1;
                        allVariants[vi].second = fallback;
                    } else {
                        // no mic entries at all: mark as unmatched
                        variantsPerMic[SIZE_MAX] += 1;
                        allVariants[vi].second = -1;
                    }
                }
            }

            // Debug: show how many variants matched each mic and unmatched count
            {
                juce::String s = "    Variants per mic: ";
                for (size_t mi = 0; mi < micEntriesForInst.size(); ++mi) {
                    int cnt = variantsPerMic.count(mi) ? variantsPerMic[mi] : 0;
                    s += "[" + juce::String(mi) + ":" + juce::String(cnt) + "] ";
                }
                int unmatched = variantsPerMic.count(SIZE_MAX) ? variantsPerMic[SIZE_MAX] : 0;
                s += "unmatched=" + juce::String(unmatched);
                LOG(Debug, s);
            }

            // Build micStarts using the instrument's actual host mapping so that
            // mic-buffer indices align with the host-output mapping assigned earlier

            int mappingCursor = 0;
            for (size_t mi = 0; mi < micEntriesForInst.size(); ++mi) {
                micStarts.push_back(mappingCursor);
                int ch = micEntriesForInst[mi]->specs.channels;
                micChannels.push_back(ch);
                mappingCursor += ch;
            }

            // Debug: show computed micStarts/micChannels
            {
                juce::String s2 = "    micStarts: ";
                for (size_t i = 0; i < micStarts.size(); ++i) {
                    if (i) s2 += ",";
                    s2 += "[" + juce::String(micStarts[i]) + ", " + juce::String(micChannels[i]) + "ch] ";
                }
                s2 += " mappingCursor=" + juce::String(mappingCursor);
                LOG(Debug, s2);
            }
            // unmatched variants (if any) go after mapped region
            unmatchedStart = mappingCursor;
            // int unmatchedCount = variantsPerMic.count(SIZE_MAX) ? variantsPerMic[SIZE_MAX] : 0;
            // We'll use each variant's own channel count for unmatched variants.

            // Now create sounds and assign micBufferChannelStart according to grouping
            size_t variantIdx = 0;
            for (const auto& velSpec : artSpec->velocityLayerSpecs) {
                VelocityLayerRuntime layer;
                layer.velocity = velSpec->velocity;
                layer.samples.reserve(velSpec->sampleVariantSpecs.size());
                for (const auto& varSpec : velSpec->sampleVariantSpecs) {
                    auto* sound = new SampledSound(instSpec, varSpec.get(), varSpec->sampleFile, layoutArt.notes);
                    int fileChannelCount = varSpec->fileChannelCount > 0 ? varSpec->fileChannelCount : varSpec->micSpec.channels;

                    int assignedMicIndex = allVariants[variantIdx].second;
                    if (assignedMicIndex >= 0) {
                        int start = micStarts[assignedMicIndex];
                        int ch = micChannels[assignedMicIndex];
                        // All variants for the same mic map to the same mic buffer group
                        sound->micBufferChannelStart = start;
                        sound->micBufferChannelCount = ch;
                    } else {
                        // unmatched: append after mic blocks, allocate sequentially using fileChannelCount
                        sound->micBufferChannelStart = unmatchedStart;
                        sound->micBufferChannelCount = fileChannelCount;
                        unmatchedStart += fileChannelCount;
                    }

                    // Debug log: show mapping of this SampledSound -> mic buffer channels and file channel ranges
                    // if (sound->sampleVariantSpec) {
                    //     juce::String sname = sound->sampleVariantSpec->sampleFileName;
                    //     int fileStart = sound->sampleVariantSpec->fileChannelStart;
                    //     int fileCount = sound->sampleVariantSpec->fileChannelCount > 0 ? sound->sampleVariantSpec->fileChannelCount : sound->sampleVariantSpec->micSpec.channels;
                    //     LOG(Debug, "channels for " + sname + " -> micBuf: start=" + juce::String(sound->micBufferChannelStart) + " count=" + juce::String(sound->micBufferChannelCount) 
                    //         + ", file: start=" + juce::String(fileStart) + ", count=" + juce::String(fileCount));
                    // }

                    this->addSound(sound);
                    this->sampledSoundPtrs.push_back(sound);
                    SampleEntry se;
                    se.sound = sound;
                    // Store variant key (1..26) as returned by SampleVariantSpec::letterToIdx
                    // If no letter, value will be 0.
                    se.variantIdx = static_cast<uint8_t>(KitLib::SampleVariantSpec::letterToIdx(varSpec->letter));
                    layer.samples.push_back(se);

                    if (loader != nullptr) {
                        loader->requestAsyncLoad(varSpec->sampleFile, [sound](std::shared_ptr<juce::AudioBuffer<float>> buf, double sr) {
                            sound->buffer = buf;
                            sound->sampleRate = sr;
                        });
                    }
                    ++variantIdx;
                }
                artRt.layers.push_back(std::move(layer));
            }

            // Before mapping velocities, log computed mic buffer requirements vs host mapping
            int computedMicChannels = unmatchedStart;
            const auto& hostMapForInst = this->getMicOutputMapping();
            int assignedHostChannels = static_cast<int>(hostMapForInst.size());
            LOG(Debug, "    computed mics/assigned host mapping channels=" + juce::String(computedMicChannels) + "/" + juce::String(assignedHostChannels));
            if (assignedHostChannels > 0 && computedMicChannels != assignedHostChannels) {
                LOG(Warning, "Instrument '" + this->getId() + "' computed mic channels (" + juce::String(computedMicChannels) + ") differs from host mapping channels (" + juce::String(assignedHostChannels) + "). This may cause out-of-range writes.");
            }

            // Map velocities to layers using upper-bound semantics:
            if (!artRt.layers.empty()) {
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
            

            // Map layout notes to this articulation index
            int8_t artIdx = static_cast<int8_t>(this->runtimeArticulations.size());
            for (uint8_t note : layoutArt.notes) {
                this->noteToArticulation[note] = artIdx;
            }



            // Prepare note -> controller and controllerValue -> articulation mappings per subscribed CC
            for (const auto& cc : this->instrumentSlot.controllers) {
                // build reverse map of note -> controllers
                for (uint8_t note : cc.notes) {
                    auto ccNum = static_cast<uint8_t>(cc.number);
                    auto& vec = this->noteToControllers[note];
                    if (std::find(vec.begin(), vec.end(), ccNum) == vec.end()) {
                        vec.emplace_back(ccNum);
                    }
                }
                
                // only controllers with notes that are linked to this articulation
                if (containsSameValue(cc.notes, layoutArt.notes)) {
                    // store refs for dimensions to process later
                    for (const auto& expDim : artSpec->expressionDimensions) {
                        if (expDim->type == cc.codeIdx) {
                            for (const auto& val : expDim->values) {
                                auto ccNum = static_cast<uint8_t>(cc.number);
                                const auto pair = std::make_pair(static_cast<int8_t>(val), artIdx);
                                auto& vec = foundCcDimensions[ccNum];
                                vec.emplace_back(pair);
                            }
                        }
                    }
                }
            }
            this->runtimeArticulations.push_back(std::move(artRt));
        } 

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

        if (!found) {
            // Defer fallback handling until we've processed all layout articulations so
            // that a later layout articulation can serve as a fallback for an earlier one.
            MissingArt ma;
            ma.codeRaw = layoutArt.codeRaw;
            for (int note : layoutArt.notes) ma.notes.push_back(static_cast<uint8_t>(note));
            missingArts.push_back(std::move(ma));
        }
    }

    // Second pass: attempt fallbacks for any layout articulations that remained unresolved.
    if (!missingArts.empty()) {

        static const std::unordered_map<std::string, std::vector<std::string>> fallbackMap = {
            {"bell", {"bow", "edge"}},
            {"edge", {"bow"}},
            {"bow", {"edge"}},
            {"rim", {"center"}},
            {"sidestick", {"rim"}},
        };

        for (const auto& ma : missingArts) {
            const auto wrapSegm = extractWrappedSegment(ma.codeRaw, "[", "]");
            const std::string missing = wrapSegm.prefix.toStdString();
            const auto& exprId = StringsDictSingleton::getId(wrapSegm.wrapped);
            int8_t artIdx = -1;

            auto it = fallbackMap.find(missing);
            if (it != fallbackMap.end()) {
                const auto& replacements = it->second;
                const int8_t count = static_cast<int8_t>(this->runtimeArticulations.size());
                for (int8_t i = 0; i < count && artIdx == -1; ++i) {
                    const auto& artRt = this->runtimeArticulations[i];
                    if (artRt.exprId != exprId) continue;

                    for (const auto& replacement : replacements) {
                        const auto codeId = StringsDictSingleton::getId(replacement);
                        if (artRt.codeId == codeId) {
                            LOG(Info, "Using fallback articulation '" + juce::String(replacement) + "' for '" + ma.codeRaw + "'");
                            artIdx = i;
                            break;
                        }
                    }
                }
            }

            if (artIdx < 0) {
                LOG(Warning, "kit instrument " + instSpec->meta.code + " (" + instSpec->meta.name + ") has no articulations for '" + ma.codeRaw + "'");
            } else {
                for (uint8_t note : ma.notes) this->noteToArticulation[note] = artIdx;
            }
        }
    }

    
}

void InstrumentSynth::handleMidiEvent(const juce::MidiMessage& midiMessage) {
    
    

    if (midiMessage.isNoteOn()) {
        if (noteToArticulation[midiMessage.getNoteNumber()] == -1) return;

        this->playNote(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getVelocity(), 1);
        // || this->noteOn(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getFloatVelocity());
    }
    else if (midiMessage.isController()) {
        auto ccNum = static_cast<uint8_t>(midiMessage.getControllerNumber());
        if (noteToControllers.find(ccNum) == noteToControllers.end()) return;


        // this->handleController(midiMessage.getChannel(), ccNum, midiMessage.getControllerValue());
    }
    
    //TODO: common realistic choke (volume change) on aftertouch: with some custom decay timeout, if aftertouch value became less in that timeout, volume should return back by some percent
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
    
    // SampledSound* chosen = this->selectSampleFor(targetArt, midiVel);
    // if (chosen == nullptr) {
    //     LOG(Info, "Nothing to play for " + targetArt);
    //     return;
    // }


    /* TODO: 
        if note louder then last > choke (kill or fast fade? using NAME_THIS_VAR_RIGHT_CHOKED_BY_LOUDER_TIMEOUT_MS) playing notes of this instrument's articulations that are chokable for this articulation. 
        create and define logic for what instruments articulation can choke other articulation, like cymbal bow/edge can choke each other and self (if last is louder)
        for hihat pedal state without note should crossfade changes, like if hitted closed hat and start opening pedal it should crossfade to more opened sound (without attack of course) so more opened starts from time offset when pedal changestarted with NAME_THIS_VAR_RIGHT_HIHAT_PEDAL_CHANGE_CROSSFADE_MS
        probably have to use juces shouldStealNotes and override findVoiceToSteal 
        need to make InstrumentSynth abstract and extend with several classes like InstrumentSynthCymbal, InstrumentSynthDrum and for special cases InstrumentSynthHiHat, 
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
                // const auto& ccValues = this->parent->getLatestContinuousControllerValuesRef();
                auto pair = this->noteToControllers.find(static_cast<uint8_t>(midiNoteNumber));
                if (pair != this->noteToControllers.end()) {
                    for (auto ccNum : pair->second) {
                        const auto& ccLatestValues = this->parent->getLatestContinuousControllerValuesRef();
                        auto it = ccLatestValues.find(ccNum);
                        if (it == ccLatestValues.end()) continue; //wtf

                        int ccVal = it->second;
                        if (ccVal < 0) ccVal = 0;
                        if (ccVal > 127) ccVal = 127;

                        auto artIt = this->ccToArticulation.find(ccNum);
                        if (artIt == this->ccToArticulation.end()) continue;
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
        // auto& instCode = se.sound->sampleVariantSpec->parent->parent->parent->meta.code;
        // auto& artCode = se.sound->sampleVariantSpec->parent->parent->name;
        // auto& filePath = se.sound->sampleVariantSpec->sampleFile.getFullPathName();
        // LOG(Verbose, "startVoice note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)) + " inst " + instCode + " art " + artCode + " micBufCh start=" + juce::String(se.sound->micBufferChannelStart) + "x" + juce::String(se.sound->micBufferChannelCount) + " file=" + filePath);
        this->startVoice(voice, se.sound, midiChannel, midiNoteNumber, velf);
        anyStarted = true;
    }
    if (!anyStarted) {
        LOG(Warning, "InstrumentSynth::playNote - no free voice available for any sample in layer");
        return false;
    }
    return true;
}

void InstrumentSynth::noteOn (const int midiChannel, const int midiNoteNumber, const float velocity) {
    const juce::ScopedLock sl (this->lock);
    bool reallyShouldStealNotes = true;
    for (auto* sound : this->sounds) {
        if (auto* sampledSound = dynamic_cast<SampledSound*>(sound)) {
            if (sampledSound->appliesToNote(midiNoteNumber) && sampledSound->appliesToChannel(midiChannel)) {
                
                LOG(Debug, "NoteOn " + juce::String(midiNoteNumber) + " vel " + juce::String(velocity) + " " + sampledSound->sampleVariantSpec->sampleFileName);
                this->startVoice(this->findFreeVoice (sampledSound, midiChannel, midiNoteNumber, reallyShouldStealNotes), sampledSound, midiChannel, midiNoteNumber, velocity);
            }
        }
    }
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

    for (auto* voice : this->voices) //TODO: refactor to separate method
        if (auto* v = dynamic_cast<InstrumentSynthVoice*>(voice))
            v->chokeProcessor.prepare(newSampleRate);
}

// SampledSound* InstrumentSynth::selectSampleFor(int articulationIdx, int midiVel, int micIdx) {
//     if (articulationIdx < 0 || articulationIdx >= static_cast<int>(this->runtimeArticulations.size()))
//         return nullptr;

//     auto& art = this->runtimeArticulations[articulationIdx];
//     if (art.layers.empty()) return nullptr;

//     // LOG(Debug, "selectSampleFor " + art.codeRaw + " artIdx=" + juce::String(articulationIdx) + " vel=" + juce::String(midiVel) + " micIdx=" + juce::String(micIdx));

//     // clamp midiVel
//     if (midiVel < 0) midiVel = 0;
//     if (midiVel > 127) midiVel = 127;

//     int layerIdx = art.velocityToLayer[midiVel];
    
//     if (layerIdx < 0 || layerIdx >= static_cast<int>(art.layers.size())) return nullptr;
//     // If there are enough variants, do round-robin here
//     auto& layer = art.layers[layerIdx];
//     int sampleCount = static_cast<int>(layer.samples.size());
//     if (sampleCount >= NAME_IT_BETTER_MIN_VARIANTS) {
//         uint32_t rr = layer.roundRobinCounter->fetch_add(1u);
//         int idx = static_cast<int>(rr % static_cast<uint32_t>(sampleCount));
//         return layer.samples[idx].sound;
//     }

//     // Otherwise try to find nearby layers within reasonable velocity distance that have samples
//     int bestLayer = -1;
//     int bestDist = 10000;
//     for (int i = 0; i < static_cast<int>(art.layers.size()); ++i) {
//         if (static_cast<int>(art.layers[i].samples.size()) == 0) continue;
//         int dist = std::abs(static_cast<int>(art.layers[i].velocity) - midiVel);
//         if (dist <= NAME_IT_BETTER_MAX_VELOCITY_DISTANCE_TO_USE_SAMPLES_FROM_NEARBY_VELOCITY_LAYERS && dist < bestDist) {
//             bestDist = dist;
//             bestLayer = i;
//         }
//     }

//     if (bestLayer >= 0) {
//         auto& best = art.layers[bestLayer];
//         int sc = static_cast<int>(best.samples.size());
//         if (sc > 0) {
//             uint32_t rr = best.roundRobinCounter->fetch_add(1u);
//             int idx = static_cast<int>(rr % static_cast<uint32_t>(sc));
//             return best.samples[idx].sound;
//         }
//     }

//     // fallback: try primary layer even if empty -> try any sample in any layer
//     for (auto& l : art.layers) {
//         if (!l.samples.empty()) {
//             uint32_t rr = l.roundRobinCounter->fetch_add(1u);
//             int idx = static_cast<int>(rr % static_cast<uint32_t>(l.samples.size()));
//             return l.samples[idx].sound;
//         }
//     }

//     return nullptr;
// }

InstrumentSynthVoice::InstrumentSynthVoice()
    : mGainParameter(nullptr), mPanParameter(nullptr), mInvertPhaseParameter(nullptr) {
    mPitchRatio = 1.0;
    mVelocityGain = 1.0f;
    mSourceSamplePosition = 0.0;
}

void InstrumentSynthVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* s, int /*pitchWheel*/) {
    juce::ignoreUnused(midiNoteNumber);
    mVelocityGain = velocity;
    mSourceSamplePosition = 0.0;
    mAdsr.setParameters({0.005f, 0.05f, 0.8f, 0.08f});
    mAdsr.setSampleRate(getSampleRate());
    mAdsr.noteOn();

    this->chokeProcessor.noteStarted();
    // LOG(Debug, "InstrumentSynthVoice: startNote velocity=" + juce::String(velocity) + " pitchRatio=" + juce::String(mPitchRatio));

    if (auto* sampled = dynamic_cast<SampledSound*>(s)) {
        if (sampled->sampleRate > 0 && getSampleRate() > 0)
            mPitchRatio = sampled->sampleRate / getSampleRate();
        else
            mPitchRatio = 1.0;
    } else {
        mPitchRatio = 1.0;
    }
}

void InstrumentSynthVoice::stopNote(float velocity, bool allowTailOff) {
    juce::ignoreUnused(velocity);
    if (allowTailOff) {
        mAdsr.noteOff();
    } else {
        mAdsr.reset();
        clearCurrentNote();
    }
}

void InstrumentSynthVoice::pitchWheelMoved(int /*newValue*/) {}
void InstrumentSynthVoice::controllerMoved(int /*controllerNumber*/, int /*newValue*/) {}

void InstrumentSynthVoice::aftertouchChanged (int newValue) {
    // LOG(Verbose, "aftertouchChanged value=" + juce::String(newValue));
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
        this->clearCurrentNote();
        return;
    }
    for (int i = 0; i < numSamples; ++i) {
        
        //TODO: use peak offset for mics (found minimal for example di and take it as 0, based on that calculate for all other mics of that sample variant relatively, for example far room = 10ms etc...) first makke script to smart-parse from samples and save to filename
        // Process per-voice ducking for this block and get block gain
        const float chokeGain = this->chokeProcessor.getNextBlockGain();
 
        const double pos = mSourceSamplePosition;
        const int idx = static_cast<int>(pos);
        const double frac = pos - idx;

        // For each source channel in this sample variant, fetch interpolated sample
        // and write to corresponding destination channel (offset by srcStart).
        if (idx >= 0 && idx < srcLen) {
            const float env = mAdsr.getNextSample();

            for (int sc = 0; sc < srcCount; ++sc) {
                int actualSrcCh = juce::jlimit(0, srcChannels - 1, srcStart + sc);
                float v0 = buf.getSample(actualSrcCh, idx);
                float v1 = (idx + 1 < srcLen) ? buf.getSample(actualSrcCh, idx + 1) : 0.0f;
                float sample = static_cast<float>(v0 + (v1 - v0) * frac);
                sample *= env * mVelocityGain * chokeGain;

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
            (void)mAdsr.getNextSample();
        }

        mSourceSamplePosition += mPitchRatio;
        if (mSourceSamplePosition >= srcLen) {
            // reached end
            mAdsr.reset();
            clearCurrentNote();
            break;
        }
    }
}
