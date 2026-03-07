#pragma once

#include "InstrumentSynth.h"
#include "DrumKitEngine.h"
#include "MicRoutingBuilder.h"
#include "SampleLoader.h"

const MicLayout* InstrumentSynth::getMicLayout() const { return this->parent ? this->parent->getMicLayout() : nullptr; }
const SampleLoader* InstrumentSynth::getSampleLoader() const { return this->parent ? this->parent->getSampleLoader() : nullptr; }

static constexpr float POLYPHONY_CROSFADE_TIMEOUT_SEC = 0.100f;
static constexpr float HIHAT_PEDAL_CROSSFADE_TIMEOUT_SEC = 0.050f;

/*================================================================
   instrument loading / assigning
================================================================*/

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

            SampleEntry se;
            se.sound = sound;

            // Store variant key (1..26) as returned by SampleVariantSpec::letterToIdx (0 if no letter)
            se.variantIdx = static_cast<uint8_t>(KitLib::SampleVariantSpec::letterToIdx(varSpec->letter));

            layer.samples.push_back(se);

            const SampleLoader* loader = this->getSampleLoader();
            if (loader != nullptr) {
                auto* q = loader->getQueue();
                if (q != nullptr) {
                    q->enqueueCandidate(varSpec->sampleFile, instSpec->meta.type, artSpec->nameId, velSpec->velocity, KitLib::SampleVariantSpec::letterToIdx(varSpec->letter),
                    [sound](std::shared_ptr<juce::AudioBuffer<float>> buf, double sr) {
                        sound->buffer = buf;
                        sound->sampleRate = sr;
                    });
                }
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


/*================================================================
   instrument runtime
================================================================*/


void InstrumentSynth::handleMidiEvent(const juce::MidiMessage& midiMessage) {
    
    if (midiMessage.isNoteOn()) {
        const int note = midiMessage.getNoteNumber();
        if (noteToArticulation[note] == -1) return;

        this->playNote(midiMessage.getChannel(), note, midiMessage.getVelocity());
    }
    else if (midiMessage.isController()) {
        auto ccNum = static_cast<uint8_t>(midiMessage.getControllerNumber());
        if (noteToControllers.find(ccNum) == noteToControllers.end()) return;
        this->handleController(midiMessage.getChannel(), ccNum, midiMessage.getControllerValue());
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

bool InstrumentSynth::playNote(const int midiChannel, const int midiNoteNumber, const juce::uint8 velocity) {
    /* TODO: need to plan a fix for next problems:
        1. currently if playing cymbal loudly and fast (swell many times) it launches many voices, and then if suddenly start doing it very quietly, it will mute loud voices at some point (when ran out of voices) and play only quiet samples, which sounds unrealistic.
        2. if playing more closed hihat articulation right after more opened, opened keep sounding.
        3. when loud notes played fast, sometimes waveforms can counterphase/double volume of each other.

        if new note louder then ol -> fast fade out and mark old note's voice to this->stopNote, or maybe even better crossfade old and new notes by POLYPHONY_CROSFADE_TIMEOUT_SEC=20
        according some dynamic hardcoded mapping that shows which articulation can mute another, like cymbal bow/edge can mute each other and self (if last is louder)
                
        For hihat pedal state without note should crossfade changes, like if hitted closed hat and start opening pedal it should crossfade to more opened sound (new more open sample plays from small offset so it is without attack) so more opened starts from time offset when pedal changestarted with HIHAT_PEDAL_CROSSFADE_TIMEOUT_SEC=50
        Maybe we have to use juces shouldStealNotes and override findVoiceToSteal - idk.

        ??? do we need to make InstrumentSynth abstract and extend with several classes like InstrumentSynthCymbal, InstrumentSynthDrum and for special cases InstrumentSynthHiHat, 
    */

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

    if (artIdx < 0 || artIdx >= static_cast<int>(runtimeArticulations.size())) {
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

    float velf = juce::jlimit(0.0f, 1.0f, static_cast<float>(velocity) / 127.0f);
    // publish currently requested velocity for stealing logic
    this->lastRequestedVelocity.store(velf);
    bool anyStarted = false;
    // Crossfade older voices of the same articulation if the new note is louder.
    // This reduces unrealistic sudden muting when many loud voices were previously started.
    for (auto* voice : this->voices) {
        if (voice == nullptr) continue;
        if (!voice->isVoiceActive()) continue;
        if (voice->getCurrentlyPlayingNote() == midiNoteNumber && voice->isPlayingChannel(midiChannel)) {
            // this->stopVoice(voice, true);
            voice->crossfadeOutSec(POLYPHONY_CROSFADE_TIMEOUT_SEC);
        }

        auto* ss = voice->getCurrentlyPlayingSound();
        if (ss == nullptr || ss->sampleVariantSpec == nullptr) continue;

        // Compare articulation identity: SampleVariantSpec->parent->parent is ArticulationSpec*
        auto* artSpecPtr = ss->sampleVariantSpec->parent ? ss->sampleVariantSpec->parent->parent : nullptr;
        if (artSpecPtr == nullptr) continue;

        // If this voice belongs to the same articulation we're about to play
        if (artSpecPtr->nameId == art.nameId && artSpecPtr->exprId == art.exprId) {
            // If new note is significantly louder than existing voice, fade the old one
            const float existingGain = voice->getVelocityGain();
            if (velf > existingGain + 0.05f) {
                voice->crossfadeOutSec(POLYPHONY_CROSFADE_TIMEOUT_SEC);
            }
        }
    }

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
        // warning if buffer is empty
        if (se.sound->buffer == nullptr) {
            LOG(Warning, "Sample buffer not loaded for note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)) + " file=" + se.sound->sampleVariantSpec->sampleFile.getFullPathName());
            continue;
        }
        const auto groupId = se.sound->micBufferChannelStart; // use mic start channel as group id for stealing logic
        auto* voice = this->findFreeVoice(se.sound, midiChannel, midiNoteNumber, groupId);
        if (voice == nullptr) continue;

        // this->stopVoice(voice, false);

        if (false) {
            auto& instCode = se.sound->sampleVariantSpec->parent->parent->parent->meta.codeRaw;
            auto& artCode = se.sound->sampleVariantSpec->parent->parent->codeRaw;
            auto& filePath = se.sound->sampleVariantSpec->sampleFile.getFullPathName();
            LOG(Debug, "startVoice note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)) + " inst " + instCode + " art " + artCode + " micBuf(" + juce::String(se.sound->micBufferChannelStart) + "," + juce::String(se.sound->micBufferChannelCount) + ") file=" + filePath);
        }
        // use samples mic index as voice's hitGroupId so we can use it in stealing logic to prefer stealing voices with same mic
        voice->hitGroupId = groupId;
        this->startVoice(voice, se.sound, midiChannel, midiNoteNumber, velf);
        voice->artNameId = art.nameId;
        voice->artExprId = art.exprId;
        voice->setVelocityGain(velf);
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


void InstrumentSynth::renderNextBlock(juce::AudioBuffer<float>& outputAudio, const juce::MidiBuffer& inputMidi, int startSample, int numSamples) {
    jassert(!juce::exactlyEqual(sampleRate, 0.0));
    const int targetChannels = outputAudio.getNumChannels();

    auto midiIterator = inputMidi.findNextSamplePosition(startSample);

    bool firstEvent = true;

    const juce::ScopedLock sl(this->lock);

    for (; numSamples > 0; ++midiIterator) {
        if (midiIterator == inputMidi.cend()) {
            if (targetChannels > 0)
                this->renderVoices(outputAudio, startSample, numSamples);
            return;
        }

        const auto metadata = *midiIterator;
        const int samplesToNextMidiMessage = metadata.samplePosition - startSample;

        if (samplesToNextMidiMessage >= numSamples) {
            if (targetChannels > 0)
                this->renderVoices(outputAudio, startSample, numSamples);

            this->handleMidiEvent(metadata.getMessage());
            break;
        }

        if (samplesToNextMidiMessage < ((firstEvent && !subBlockSubdivisionIsStrict) ? 1 : minimumSubBlockSize)) {
            this->handleMidiEvent(metadata.getMessage());
            continue;
        }

        firstEvent = false;

        if (targetChannels > 0)
            this->renderVoices(outputAudio, startSample, samplesToNextMidiMessage);

        this->handleMidiEvent(metadata.getMessage());
        startSample += samplesToNextMidiMessage;
        numSamples -= samplesToNextMidiMessage;
    }

    std::for_each(midiIterator, inputMidi.cend(), [&](const auto& meta) { handleMidiEvent(meta.getMessage()); });
}

void InstrumentSynth::renderVoices(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) {
    this->applyAdaptiveTailCompression(); //FIXME: wtf this even doing?
    for (auto* voice : this->voices) voice->renderNextBlock(buffer, startSample, numSamples);
}

InstrumentSynthVoice* InstrumentSynth::findVoiceToSteal(SampledSound* soundToPlay, int midiNoteNumber) const {
    // Adapted stealing policy:
    // - Prefer stealing voices that are released
    // - Prefer voices that are quieter than the currently requested velocity (avoid muting loud voices when playing quietly)
    // - Fall back to the default heuristic (oldest non-protected voice)

    // gather usable voices
    std::vector<InstrumentSynthVoice*> usable;
    for (auto* v : this->voices) {
        if (v->canPlaySound(soundToPlay) && v->isVoiceActive())
            usable.push_back(v);
    }

    if (usable.empty()) return nullptr;

    // sort by start time (oldest first)
    std::sort(usable.begin(), usable.end(), [](const InstrumentSynthVoice* a, const InstrumentSynthVoice* b) { return a->wasStartedBefore(*b); });

    // find low/top protected voices (same logic as JUCE default)
    InstrumentSynthVoice* low = nullptr;
    InstrumentSynthVoice* top = nullptr;
    for (auto* voice : usable) {
        if (!voice->isPlayingButReleased()) {
            auto note = voice->getCurrentlyPlayingNote();
            if (low == nullptr || note < low->getCurrentlyPlayingNote()) low = voice;
            if (top == nullptr || note > top->getCurrentlyPlayingNote()) top = voice;
        }
    }
    if (top == low) top = nullptr;

    // requested velocity
    const float reqVel = this->lastRequestedVelocity.load();
    const float threshold = 0.05f; // allow small margin

    // Tiered selection: prefer released & quiet non-protected voices
    InstrumentSynthVoice* candidate = nullptr;

    // 1) released & not protected & quiet
    for (auto* voice : usable) {
        if (voice == low || voice == top) continue;
        if (!voice->isPlayingButReleased()) continue;
        // try to inspect velocity -- downcast to our voice type
        if (voice->getVelocityGain() <= reqVel + threshold) {
            candidate = voice; break;
        }
    }

    // 2) not key down & not protected & quiet
    if (candidate == nullptr) {
        for (auto* voice : usable) {
            if (voice == low || voice == top) continue;
            if (voice->isAttackHold()) continue;
            if (voice->getVelocityGain() <= reqVel + threshold) { candidate = voice; break; }
        }
    }

    // 3) any not-protected & quiet
    if (candidate == nullptr) {
        for (auto* voice : usable) {
            if (voice == low || voice == top) continue;
            if (voice->getVelocityGain() <= reqVel + threshold) { candidate = voice; break; }
        }
    }

    if (candidate != nullptr)
        return candidate;

    // fallback to heuristics similar to JUCE's default (oldest released, oldest not keydown, etc.)
    // oldest voice with same pitch
    for (auto* voice : usable) if (voice->getCurrentlyPlayingNote() == midiNoteNumber) return voice;
    for (auto* voice : usable) if (voice != low && voice != top && voice->isPlayingButReleased()) return voice;
    for (auto* voice : usable) if (voice != low && voice != top && !voice->isAttackHold()) return voice;
    for (auto* voice : usable) if (voice != low && voice != top) return voice;

    // last resort
    if (top != nullptr) return top;
    return low;
}

InstrumentSynthVoice* InstrumentSynth::findVoiceToSteal(SampledSound* soundToPlay, int /*midiChannel*/, int /*midiNoteNumber*/, int groupId) const {
    std::vector<InstrumentSynthVoice*> voiceCandidates;

    for (auto* voice : this->voices) {
        if (!voice->isVoiceActive()) continue;
        if (!voice->canPlaySound(soundToPlay)) continue;
        if (voice->hitGroupId != groupId) continue;

        voiceCandidates.push_back(voice);
    }

    if (voiceCandidates.empty()) return nullptr;

    // Determine protected voice: loudest amplitude, tie-breaker newest noteOnTime
    InstrumentSynthVoice* protectedVoice = nullptr;
    float protectedAmp = -1.0f;
    uint32_t protectedTime = 0;

    for (auto* candidate : voiceCandidates) {
        const float amp = candidate->getCurrentAmplitude();
        const uint32_t started = candidate->noteOnTime;
        if (protectedVoice == nullptr || amp > protectedAmp || (amp == protectedAmp && started > protectedTime)) {
            protectedVoice = candidate;
            protectedAmp = amp;
            protectedTime = started;
        }
    }

    // Choose best non-protected voice by score = amplitude + ageFactor
    InstrumentSynthVoice* best = nullptr;
    float bestScore = std::numeric_limits<float>::max();

    for (auto* candidate : voiceCandidates) {
        if (candidate == protectedVoice) continue;

        const float ageFactor = static_cast<float>(lastNoteOnCounter - candidate->noteOnTime) * 0.0001f;
        const float score = candidate->getCurrentAmplitude() + ageFactor;

        if (score < bestScore) {
            bestScore = score;
            best = candidate;
        }
    }

    if (best != nullptr) {
        // LOG(Debug, "Stealing voice (non-protected) in group " + juce::String(groupId) + " amp=" + juce::String(best->getCurrentAmplitude()));
        return best;
    }

    // No non-protected candidate found. Return protected voice as last resort,
    // but prefer the oldest candidate if multiple voices share the protected identity.
    if (protectedVoice != nullptr)
        return protectedVoice;

    // Fallback - return oldest candidate
    auto* oldest = voiceCandidates.front();
    for (auto* candidate : voiceCandidates) if (candidate->wasStartedBefore(*oldest)) oldest = candidate;
    return oldest;
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
    const int minVoices = 10;
    const int numVoices = std::max(minVoices, maxSimultaneousSamples * minVoices);
    for (int i = 0; i < numVoices; ++i)
        this->addVoice(new InstrumentSynthVoice());
    LOG(Debug, "prepareToPlay - instrument: " + this->instrumentSlot.id + " sampleRate=" + juce::String(newSampleRate) + " samplesPerBlock=" + juce::String(samplesPerBlock) + " numVoices=" + juce::String(numVoices));

    // set playback sample rate for voices
    this->setCurrentPlaybackSampleRate(newSampleRate);

    for (auto* voice : this->voices) //TODO: maybe refactor to separate method
        voice->chokeProcessor.prepare(newSampleRate);
}

void InstrumentSynthVoice::startNote(int midiNoteNumber, float velocity, SampledSound* sound) {
    juce::ignoreUnused(midiNoteNumber);
    this->velocityGain = velocity;
    this->sourceSamplePosition = 0.0;
    // Apply ADSR using configured attack/release
    this->adsr.setParameters({ this->attackTimeSec, 0.05f, 0.8f, this->releaseTimeSec }); //FIXME
    this->adsr.setSampleRate(this->getSampleRate());
    this->adsr.noteOn();

    this->noteOnTimeMs = juce::Time::getMillisecondCounterHiRes();

    this->tailShortenFactor = 1.0f;

    this->chokeProcessor.noteStarted();
    // LOG(Debug, "InstrumentSynthVoice: startNote velocity=" + juce::String(velocity) + " pitchRatio=" + juce::String(this->pitchRatio));

    if (sound->sampleRate > 0 && getSampleRate() > 0)
        this->pitchRatio = sound->sampleRate / getSampleRate();
    else
        this->pitchRatio = 1.0;

}

void InstrumentSynthVoice::crossfadeOutSec(float sec) {
    // set a very short release and trigger noteOff so the ADSR will fade quickly
    const float releaseSec = juce::jmax(0.001f, sec);
    // keep existing attack/decay/sustain aggressive for quick fade
    this->adsr.setParameters({ 0.001f, 0.01f, 0.0f, releaseSec });
    this->adsr.noteOff();
}

void InstrumentSynthVoice::stopNote(bool allowTailOff) {
    if (allowTailOff) {
        // keep decay/sustain as-is-ish, but ensure release matches configured value
        this->adsr.setParameters({ this->attackTimeSec, 0.05f, 0.8f, this->releaseTimeSec });
        this->adsr.noteOff();
        this->releaseStartTimeMs = juce::Time::getMillisecondCounterHiRes(); //unfinished? and what to do with it?
    } else {
        this->adsr.reset();
        this->clearCurrentNote();
    }
}

void InstrumentSynthVoice::aftertouchChanged(int newValue) {
    // LOG(Debug, "aftertouchChanged " + juce::String(newValue));
    this->chokeProcessor.handlePolyAftertouch(newValue);
}

void InstrumentSynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) {
    if (!this->isVoiceActive())
        return;

    SampledSound* s = this->getCurrentlyPlayingSound();
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
        this->stopNote(false);
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
            const auto env = this->adsr.getNextSample();
            this->lastEnvelopeValue = env;

            for (int sc = 0; sc < srcCount; ++sc) {
                int actualSrcCh = juce::jlimit(0, srcChannels - 1, srcStart + sc);
                float v0 = buf.getSample(actualSrcCh, idx);
                float v1 = (idx + 1 < srcLen) ? buf.getSample(actualSrcCh, idx + 1) : 0.0f;
                auto sample = static_cast<float>(v0 + (v1 - v0) * frac) * env * this->velocityGain * chokeGain;

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
            // LOG(Debug, "reached end of sample " + (s->sampleVariantSpec ? s->sampleVariantSpec->sampleFile.getFileName() : "unknown") + " at pos " + juce::String(this->sourceSamplePosition) + " srcLen=" + juce::String(srcLen));
            break;
        }
    }
    if (!this->adsr.isActive()) {
        // LOG(Debug, "adsr ended for sample " + (s->sampleVariantSpec ? s->sampleVariantSpec->sampleFile.getFileName() : "unknown") + " at pos " + juce::String(this->sourceSamplePosition) + " srcLen=" + juce::String(srcLen));
        this->clearCurrentNote();
    }
}


void InstrumentSynth::handleController(int midiChannel, int controllerNumber, int controllerValue) { //FIXME: this does not work
    // Only handle hi-hat pedal crossfades here for now
    if (this->getType() != KitLib::InstrumentSpec::Type::HiHat) return;

    auto it = this->ccToArticulation.find(static_cast<int>(controllerNumber));
    if (it == this->ccToArticulation.end()) return;

    const auto& mapArr = it->second;
    uint8_t newArtIdx = mapArr[juce::jlimit(0, 127, controllerValue)];

    // For each active voice, if its articulation differs from newArtIdx, crossfade
    for (auto* voice : this->voices) {
        if (voice == nullptr) continue;
        if (!voice->isVoiceActive()) continue;

        auto* ss = voice->getCurrentlyPlayingSound();
        if (ss == nullptr || ss->sampleVariantSpec == nullptr) continue;

        // Find articulation index for this voice by matching nameId/exprId
        int currentArtIdx = -1;
        for (size_t ai = 0; ai < this->runtimeArticulations.size(); ++ai) {
            const auto& art = this->runtimeArticulations[ai];
            if (art.nameId == ss->sampleVariantSpec->parent->parent->nameId && art.exprId == ss->sampleVariantSpec->parent->parent->exprId) {
                currentArtIdx = static_cast<int>(ai);
                break;
            }
        }
        if (currentArtIdx == -1) continue;

        if (currentArtIdx == static_cast<int>(newArtIdx)) continue; // already correct

        // Now prepare to start a replacement voice from new articulation
        const auto& newArt = this->runtimeArticulations[newArtIdx];
        // choose layer by current voice velocity
        int midiVel = static_cast<int>(juce::jlimit(0.0f, 1.0f, voice->getVelocityGain()) * 127.0f);
        int layerIdx = newArt.velocityToLayer[midiVel];
        if (layerIdx < 0 || layerIdx >= static_cast<int>(newArt.layers.size())) continue;
        const auto& layer = newArt.layers[layerIdx];

        // find variant index of current voice
        uint8_t curVariant = 0;
        if (ss->sampleVariantSpec) curVariant = KitLib::SampleVariantSpec::letterToIdx(ss->sampleVariantSpec->letter);

        // find sample entry in target layer matching variant or fallback to first
        const InstrumentSynth::SampleEntry* targetSe = nullptr;
        for (const auto& se : layer.samples) {
            if (se.variantIdx == curVariant) { targetSe = &se; break; }
        }
        if (targetSe == nullptr && !layer.samples.empty()) targetSe = &layer.samples.front();
        if (targetSe == nullptr || targetSe->sound == nullptr) continue;

        // Fade out the old voice
        voice->crossfadeOutSec(HIHAT_PEDAL_CROSSFADE_TIMEOUT_SEC);

        // Start replacement voice with small offset to avoid attack transient
        auto* newVoice = this->findFreeVoice(targetSe->sound, midiChannel, voice->getCurrentlyPlayingNote(), voice->hitGroupId);
        if (newVoice == nullptr) continue;
        float vel = voice->getVelocityGain();
        // Start voice; this will call startNote and set ADSR
        this->startVoice(newVoice, targetSe->sound, midiChannel, voice->getCurrentlyPlayingNote(), vel);

        // skip initial attack portion of sample (start from small offset)
        const double offsetSec = 0.02; // 20 ms offset to skip attack
        double srcRate = targetSe->sound->sampleRate > 0.0 ? targetSe->sound->sampleRate : 44100.0;
        const double offsetSamples = offsetSec * srcRate;
        newVoice->setSourceSamplePosition(offsetSamples);
        // set ADSR to minimal attack so it sounds continuous
        newVoice->setAdsrParameters(0.001f, 0.01f, 0.9f, 0.05f);

    }
}