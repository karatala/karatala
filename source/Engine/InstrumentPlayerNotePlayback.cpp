#include "InstrumentPlayer.h"
#include "../Config/PluginLogger.h"
#include "DrumKitEngine.h"
#include "HiHatTransitionPolicy.h"
#include <cmath>

static uint32_t advanceFloatingRandomState(uint32_t randomState) {
    if (randomState == 0) randomState = 0x9e3779b9u;
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}

static float floatingRandomUnitValue(uint32_t randomState) {
    return static_cast<float>(randomState & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

bool InstrumentPlayer::playNote(const int midiChannel, const int midiNoteNumber, const juce::uint8 velocity) {
    const int articulationIdx = this->resolveArticulation(midiNoteNumber);
    if (articulationIdx >= 0 && articulationIdx < static_cast<int>(this->runtimeArticulations.size())) {
        if (LOG_ENABLED(Verbose))
            LOG(Verbose, "InstrumentPlayer::playNote: note " + juce::String(midiNoteNumber) + " maps to expression " + this->getArtName(articulationIdx) + " (artIdx=" + juce::String(articulationIdx) + ")");
    } else {
        // LOG(Warning, "InstrumentPlayer::playNote - no articulation available for note " + juce::String(midiNoteNumber));
        return false;
    }
    return this->playResolvedArticulation(midiChannel, midiNoteNumber, static_cast<ArtRtIdx>(articulationIdx), velocity);
}

bool InstrumentPlayer::playPreviewArticulation(const int midiChannel, const int midiNoteNumber, const juce::String& articulationSlotCode, const juce::uint8 velocity) {
    const ArticulationSlot previewArticulationSlot(articulationSlotCode);
    for (const auto& playbackEntry : this->articulationPlaybackEntries) {
        if (!playbackEntry.isControllerSibling || playbackEntry.libraryArticulation == nullptr) continue;
        if (playbackEntry.articulationSlotNameId != previewArticulationSlot.nameId || playbackEntry.libraryArticulation->exprId != previewArticulationSlot.exprId) continue;
        if (!vectorContains(playbackEntry.midiNotes, static_cast<uint8_t>(midiNoteNumber))) continue;

        ArtRtIdx articulationIdx = -1;
        if (!this->tryGetRuntimeArticulationIdx(playbackEntry, false, articulationIdx)) continue;
        if (this->playResolvedArticulation(midiChannel, midiNoteNumber, articulationIdx, velocity)) return true;
    }
    return false;
}

bool InstrumentPlayer::playLibraryLogicalHitPreview(const KitLib::ArticulationSpec& libraryArticulation, int namedVelocity, uint8_t roundRobinId, int midiChannel, int midiNoteNumber, juce::uint8 playbackVelocity) {
    for (size_t articulationIdx = 0; articulationIdx < this->runtimeArticulations.size(); ++articulationIdx) {
        auto& articulation = this->runtimeArticulations[articulationIdx];
        for (auto& velocityLevel : articulation.velocityLevels) {
            if (velocityLevel.velocityLevelSpec == nullptr || velocityLevel.velocityLevelSpec->parent != &libraryArticulation || velocityLevel.velocityLevelSpec->velocity != namedVelocity) continue;
            for (auto& logicalHit : velocityLevel.logicalHits) {
                if (logicalHit.roundRobinId != roundRobinId || !logicalHit.isReady()) continue;
                const float velocityGain = articulation.velocityGain[static_cast<size_t>(playbackVelocity)];
                if (!this->startVelocityLevelVoices(articulation, static_cast<ArtRtIdx>(articulationIdx), velocityLevel, logicalHit, midiChannel, midiNoteNumber, velocityGain, playbackVelocity)) continue;
                this->lastRequestedVelocity.store(velocityGain);
                this->applyAftertouch(midiChannel, midiNoteNumber);
                return true;
            }
        }
    }
    return false;
}

bool InstrumentPlayer::playResolvedArticulation(const int midiChannel, const int midiNoteNumber, ArtRtIdx articulationIdx, const juce::uint8 velocity) {
    if (articulationIdx < 0 || static_cast<size_t>(articulationIdx) >= this->runtimeArticulations.size()) return false;
    auto& art = this->runtimeArticulations[static_cast<size_t>(articulationIdx)];
    this->recordSampleLoadDemand(art, art.velocityToLevel[static_cast<size_t>(velocity)]);
    const auto floatingSelection = this->chooseFloatingLogicalHit(art, velocity);
    int velocityLevelIdx = floatingSelection.velocityLevelIdx;
    LogicalHitRuntime* logicalHit = nullptr;
    if (floatingSelection.isValid()) {
        auto& selectedVelocityLevel = art.velocityLevels[static_cast<size_t>(velocityLevelIdx)];
        if (floatingSelection.logicalHitIdx < selectedVelocityLevel.logicalHits.size()) logicalHit = &selectedVelocityLevel.logicalHits[floatingSelection.logicalHitIdx];
    } else {
        velocityLevelIdx = this->resolveVelocityLevel(art, velocity);
        if (velocityLevelIdx < 0) return false;
        logicalHit = this->chooseLogicalHit(art.velocityLevels[static_cast<size_t>(velocityLevelIdx)]);
    }
    if (logicalHit == nullptr) return false;

    auto& velocityLevel = art.velocityLevels[static_cast<size_t>(velocityLevelIdx)];
    const float velocityGain = art.velocityGain[static_cast<size_t>(velocity)];
    this->lastRequestedVelocity.store(velocityGain);

    bool anyStarted = this->startVelocityLevelVoices(art, articulationIdx, velocityLevel, *logicalHit, midiChannel, midiNoteNumber, velocityGain, velocity);
    if (!anyStarted) return false;
    if (floatingSelection.isValid()) this->commitFloatingLogicalHitSelection(art, floatingSelection);

    this->applyAftertouch(midiChannel, midiNoteNumber);

    return true;
}

int InstrumentPlayer::resolveArticulation(int midiNoteNumber) {
    if (this->runtimeArticulations.empty()) {
        if (!this->instrumentSlot.articulations.empty()) return 0;
        return -1;
    }
    if (midiNoteNumber < 0 || midiNoteNumber >= 128) return -1;
    int artIdx = this->noteToArticulation[static_cast<size_t>(midiNoteNumber)];
    if (parent != nullptr) {
        auto pair = this->noteToControllers.find(static_cast<uint8_t>(midiNoteNumber));
        if (pair != this->noteToControllers.end()) {
            for (auto ccNum : pair->second) {
                if (!this->ccToArticulationMask[ccNum]) continue;
                const auto ccVal = this->parent->getLatestContinuousControllerValue(ccNum);

                for (const auto& controller : this->instrumentSlot.controllers) {
                    if (controller.number != static_cast<int>(ccNum)) continue;
                    if (!vectorContains(controller.notes, static_cast<uint8_t>(midiNoteNumber))) continue;
                    artIdx = this->selectArticulationForController(static_cast<ArtRtIdx>(artIdx), controller.codeIdx, ccVal);
                    break;
                }
            }
        }
    }
    return artIdx;
}

int InstrumentPlayer::resolveVelocityLevel(const ArticulationRuntime& art, juce::uint8 velocity) {
    int velocityLevelIdx = art.velocityToLevel[static_cast<size_t>(velocity)];
    if (velocityLevelIdx >= 0 && velocityLevelIdx < (int)art.velocityLevels.size()) {
        if (this->hasReadyLogicalHit(art.velocityLevels[static_cast<size_t>(velocityLevelIdx)])) return velocityLevelIdx;
    }

    int bestIdx = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int idx = 0; idx < (int)art.velocityLevels.size(); ++idx) {
        const auto& velocityLevel = art.velocityLevels[static_cast<size_t>(idx)];
        if (!this->hasReadyLogicalHit(velocityLevel)) continue;
        const float distance = static_cast<float>(std::abs(static_cast<int>(velocityLevel.velocity) - static_cast<int>(velocity)));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIdx = idx;
        }
    }
    return bestIdx;
}

bool InstrumentPlayer::hasReadyLogicalHit(const VelocityLevelRuntime& velocityLevel) const {
    for (const auto& logicalHit : velocityLevel.logicalHits) {
        if (logicalHit.isReady()) return true;
    }
    return false;
}

InstrumentPlayer::LogicalHitSelection InstrumentPlayer::chooseFloatingLogicalHit(ArticulationRuntime& articulation, juce::uint8 velocity) const {
    LogicalHitSelection selection;
    if (this->parent == nullptr || !articulation.floatingSelectionAvailable) return selection;
    const auto& settings = this->parent->getEngineSettings();
    if (settings.roundRobinLock.load() > 0) return selection;
    const int minimumVelocityLevels = juce::jlimit(2, 5, settings.floatingSelectionMinimumVelocityLevels.load());
    if (articulation.velocityLevels.size() < static_cast<size_t>(minimumVelocityLevels)) return selection;

    const auto& window = articulation.floatingCandidateWindows[static_cast<size_t>(velocity)];
    float bestScore = std::numeric_limits<float>::max();
    uint32_t randomState = articulation.floatingRandomState;
    for (size_t candidateIdx = 0; candidateIdx < window.count; ++candidateIdx) {
        const auto& candidate = window.candidates[candidateIdx];
        if (candidate.velocityLevelIdx < 0 || static_cast<size_t>(candidate.velocityLevelIdx) >= articulation.velocityLevels.size()) continue;
        const auto& velocityLevel = articulation.velocityLevels[static_cast<size_t>(candidate.velocityLevelIdx)];
        if (candidate.logicalHitIdx >= velocityLevel.logicalHits.size()) continue;
        const auto& logicalHit = velocityLevel.logicalHits[candidate.logicalHitIdx];
        if (!logicalHit.isReady()) continue;

        randomState = advanceFloatingRandomState(randomState);
        const float normalizedDistance = candidate.intensityDistanceDb / window.intensityRangeDb;
        float recentUsePenalty = 0.0f;
        if (logicalHit.lastFloatingSelectionSample != std::numeric_limits<uint64_t>::max()) {
            const uint64_t elapsedSamples = this->renderedSampleCount >= logicalHit.lastFloatingSelectionSample ? this->renderedSampleCount - logicalHit.lastFloatingSelectionSample : 0;
            const double effectiveSampleRate = this->sampleRate > 0.0 ? this->sampleRate : 44100.0;
            recentUsePenalty = 1.0f / (1.0f + static_cast<float>(static_cast<double>(elapsedSamples) / effectiveSampleRate));
        }
        const float score = 4.0f * normalizedDistance * normalizedDistance + 0.5f * recentUsePenalty + (1.0f / 3.0f) * floatingRandomUnitValue(randomState);
        if (score >= bestScore) continue;
        bestScore = score;
        selection.velocityLevelIdx = candidate.velocityLevelIdx;
        selection.logicalHitIdx = candidate.logicalHitIdx;
    }
    if (selection.isValid()) selection.nextRandomState = randomState;
    return selection;
}

void InstrumentPlayer::commitFloatingLogicalHitSelection(ArticulationRuntime& articulation, const LogicalHitSelection& selection) {
    if (!selection.isValid() || static_cast<size_t>(selection.velocityLevelIdx) >= articulation.velocityLevels.size()) return;
    auto& velocityLevel = articulation.velocityLevels[static_cast<size_t>(selection.velocityLevelIdx)];
    if (selection.logicalHitIdx >= velocityLevel.logicalHits.size()) return;
    velocityLevel.logicalHits[selection.logicalHitIdx].lastFloatingSelectionSample = this->renderedSampleCount;
    articulation.floatingRandomState = selection.nextRandomState;
}

InstrumentPlayer::LogicalHitRuntime* InstrumentPlayer::chooseLogicalHit(VelocityLevelRuntime& velocityLevel) {
    if (velocityLevel.logicalHits.empty()) return nullptr;

    const auto& settings = this->parent->getEngineSettings();
    const int roundRobinLock = settings.roundRobinLock.load();
    if (roundRobinLock > 0) {
        const size_t lockedIdx = static_cast<size_t>(roundRobinLock - 1) % velocityLevel.logicalHits.size();
        return velocityLevel.logicalHits[lockedIdx].isReady() ? &velocityLevel.logicalHits[lockedIdx] : nullptr;
    }

    size_t readyCount = 0;
    for (const auto& logicalHit : velocityLevel.logicalHits) {
        if (logicalHit.isReady()) ++readyCount;
    }
    if (readyCount == 0) return nullptr;

    const uint32_t roundRobinValue = velocityLevel.roundRobinCounter->fetch_add(1u, std::memory_order_relaxed);
    size_t readyTargetIdx = static_cast<size_t>(roundRobinValue) % readyCount;
    for (auto& logicalHit : velocityLevel.logicalHits) {
        if (!logicalHit.isReady()) continue;
        if (readyTargetIdx == 0) return &logicalHit;
        --readyTargetIdx;
    }
    return nullptr;
}

bool InstrumentPlayer::startVelocityLevelVoices(ArticulationRuntime& art, ArtRtIdx artIdx, VelocityLevelRuntime& velocityLevel, LogicalHitRuntime& logicalHit, int midiChannel, int midiNoteNumber, float velocityGain, juce::uint8 midiVelocity) {
    const juce::ScopedLock sl(this->lock);
    const auto& settings = this->parent->getEngineSettings();
    const float closeCrossfadeSec = settings.hihatCloseCrossfadeSec.load();
    static constexpr size_t MAX_LOGICAL_HIT_VOICES = 64;
    if (!logicalHit.isReady() || logicalHit.sampleEntryIndexes.empty() || logicalHit.sampleEntryIndexes.size() > MAX_LOGICAL_HIT_VOICES) return false;

    std::array<InstrumentPlayerVoice*, MAX_LOGICAL_HIT_VOICES> reservedVoices {};
    size_t reservedVoiceCount = 0;
    for (const size_t sampleEntryIdx : logicalHit.sampleEntryIndexes) {
        if (sampleEntryIdx >= velocityLevel.samples.size()) return false;
        const auto& sampleEntry = velocityLevel.samples[sampleEntryIdx];
        if (sampleEntry.sound == nullptr || !sampleEntry.sound->loaded.load(std::memory_order_acquire) || sampleEntry.sound->buffer == nullptr) return false;

        InstrumentPlayerVoice* reservedVoice = nullptr;
        for (auto* candidateVoice : this->voices) {
            if (candidateVoice == nullptr || candidateVoice->isVoiceActive()) continue;
            bool alreadyReserved = false;
            for (size_t reservedIdx = 0; reservedIdx < reservedVoiceCount; ++reservedIdx) {
                if (reservedVoices[reservedIdx] == candidateVoice) {
                    alreadyReserved = true;
                    break;
                }
            }
            if (!alreadyReserved) {
                reservedVoice = candidateVoice;
                break;
            }
        }
        if (reservedVoice == nullptr) reservedVoice = this->findVoiceToSteal(midiChannel, midiNoteNumber, sampleEntry.sound->micBufferChannelStart, reservedVoices.data(), reservedVoiceCount);
        if (reservedVoice == nullptr) return false;
        reservedVoices[reservedVoiceCount++] = reservedVoice;
    }

    this->cullMaskedVoices(velocityLevel, logicalHit.roundRobinId, midiChannel, midiNoteNumber, velocityGain);

    // Hi-hat articulation-order muting: a tighter/more-closed hit (higher poc rank)
    // fades out ringing voices from more-open articulations.
    if (this->type == KitLib::InstrumentSpec::Type::HiHat)
        this->chokeHiHatVoicesByPoc(art.pedalOpenCloseValue, closeCrossfadeSec);

    for (size_t logicalEntryIdx = 0; logicalEntryIdx < logicalHit.sampleEntryIndexes.size(); ++logicalEntryIdx) {
        const auto& sampleEntry = velocityLevel.samples[logicalHit.sampleEntryIndexes[logicalEntryIdx]];
        this->startSampleVoice(*reservedVoices[logicalEntryIdx], art, artIdx, sampleEntry, midiChannel, midiNoteNumber, velocityGain, midiVelocity);
    }
    return true;
}

void InstrumentPlayer::fadeOutExistingVoices(const ArticulationRuntime& art, int midiChannel, int midiNoteNumber, float newVelocity) {
    const auto& settings = this->parent->getEngineSettings();
    const float polyphonyCrossfadeSec = settings.polyphonyCrossfadeTimeoutSec.load();
    const float crossArtChokeDbMargin = settings.hihatHitChokeDbMargin.load();
    const float newVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, newVelocity), -100.0f);

    for (auto* voice : voices) {
        if (!voice || !voice->isVoiceActive()) continue;
        if (voice->getCurrentlyPlayingNote() != midiNoteNumber) continue;
        if (!voice->isPlayingChannel(midiChannel)) continue;

        if (voice->artNameId == art.nameId && voice->artExprId == art.exprId) {
            if (newVelocity >= voice->getVelocityGain() - 0.05f) voice->requestFadeOut(polyphonyCrossfadeSec);
            continue;
        }

        // Cross-articulation choke for hi-hat: a new hit should choke old articulations,
        // but only if the new hit is loud enough relative to the old voice.
        // A quiet ghost note should not fully choke a loud ringing cymbal.
        if (this->type == KitLib::InstrumentSpec::Type::HiHat) {
            float newHitPeakDbEstimate = newVelocityDb;
            if (auto* currentSound = voice->getCurrentlyPlayingSound(); currentSound != nullptr && currentSound->decayEnvelope.isValid()) {
                newHitPeakDbEstimate = currentSound->decayEnvelope.peakDb + newVelocityDb;
            }

            const float oldCurrentDb = voice->getEstimatedCurrentDb();
            if (newHitPeakDbEstimate >= oldCurrentDb - crossArtChokeDbMargin) {
                voice->requestFadeOut(polyphonyCrossfadeSec);
            }
        }
    }
}

void InstrumentPlayer::cullMaskedVoices(const VelocityLevelRuntime& velocityLevel, uint8_t chosenRoundRobinId, int midiChannel, int midiNoteNumber, float newVelocity) {
    const auto& settings = this->parent->getEngineSettings();
    const float voiceCullThresholdDb = settings.voiceCullThresholdDb.load();
    float newPeakDb = -100.0f;
    bool hasPeak = false;
    bool hasDirectPeak = false;
    const float newVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, newVelocity), -100.0f);

    for (const auto& se : velocityLevel.samples) {
        if (!se.sound || se.roundRobinId != chosenRoundRobinId) continue;

        const auto& env = se.sound->decayEnvelope;
        if (!env.isValid()) continue;

        const bool isDirect = se.sound->sampleSpec
            && se.sound->sampleSpec->micSpec.type == KitLib::MicSpec::Type::Direct;
        const float candidatePeakDb = env.peakDb + newVelocityDb;

        if (!hasPeak || (isDirect && !hasDirectPeak) || (isDirect == hasDirectPeak && candidatePeakDb > newPeakDb)) {
            newPeakDb = candidatePeakDb;
            hasPeak = true;
            hasDirectPeak = isDirect;
        }
    }

    if (!hasPeak) return;

    // Stack-allocated flat arrays instead of std::unordered_map/set — avoids
    // heap allocation on every note-on. 128 entries covers the maximum voice
    // count per instrument (typically 10-100).
    static constexpr int MAX_GROUPS = 128;
    struct NoteOnGroup { juce::uint32 noteOnTime; float peakDb; };
    NoteOnGroup groups[MAX_GROUPS];
    int groupCount = 0;

    for (auto* voice : this->voices) {
        if (!voice || !voice->isVoiceActive()) continue;
        if (voice->getCurrentlyPlayingNote() != midiNoteNumber) continue;
        if (!(midiChannel <= 0 || voice->isPlayingChannel(midiChannel))) continue;

        auto* oldSound = voice->getCurrentlyPlayingSound();
        if (oldSound == nullptr) continue;

        const auto& oldEnv = oldSound->decayEnvelope;
        if (!oldEnv.isValid()) continue;

        const double oldSrcRate = oldSound->sampleRate > 0.0 ? oldSound->sampleRate : 44100.0;
        const float elapsedMs = static_cast<float>((voice->sourceSamplePosition / oldSrcRate) * 1000.0);
        const float oldVelocityDb = juce::Decibels::gainToDecibels(juce::jmax(0.001f, voice->velocityGain), -100.0f);
        const float oldAbsDb = oldEnv.getAbsoluteDbAtMs(elapsedMs) + oldVelocityDb;

        // Linear scan to find or insert group by noteOnTime
        int found = -1;
        for (int idx = 0; idx < groupCount; ++idx) {
            if (groups[idx].noteOnTime == voice->noteOnTime) { found = idx; break; }
        }
        if (found >= 0) {
            groups[found].peakDb = juce::jmax(groups[found].peakDb, oldAbsDb);
        } else if (groupCount < MAX_GROUPS) {
            groups[groupCount++] = { voice->noteOnTime, oldAbsDb };
        }
    }

    // Mark groups whose peak is below threshold for culling (reuse the array in-place)
    juce::uint32 cullTimes[MAX_GROUPS];
    int cullCount = 0;
    for (int idx = 0; idx < groupCount; ++idx) {
        if (groups[idx].peakDb < (newPeakDb - voiceCullThresholdDb))
            cullTimes[cullCount++] = groups[idx].noteOnTime;
    }

    if (cullCount == 0) return;

    for (auto* voice : this->voices) {
        if (!voice || !voice->isVoiceActive()) continue;

        for (int idx = 0; idx < cullCount; ++idx) {
            if (voice->noteOnTime == cullTimes[idx]) {
                voice->envelopeState = InstrumentPlayerVoice::EnvelopeState::Dying;
                voice->killImmediately();
                break;
            }
        }
    }
}

void InstrumentPlayer::chokeHiHatVoicesByPoc(int newArtPedalOpenCloseValue, float fadeTimeSec) {
    // newArtPedalOpenCloseValue < 0 means the triggering articulation carries no poc rank
    // (e.g. a non-hi-hat-openness hit) — nothing to do.
    if (newArtPedalOpenCloseValue < 0) return;

    for (auto* voice : this->voices) {
        if (!voice || !voice->isVoiceActive()) continue;
        if (voice->envelopeState == InstrumentPlayerVoice::EnvelopeState::FadingOut
            || voice->envelopeState == InstrumentPlayerVoice::EnvelopeState::Dying)
            continue;

        // Look up the poc rank stored in the matching ArticulationRuntime.
        // artNameId / artExprId are stamped onto the voice when it starts.
        const int voicePedalOpenCloseValue = this->getVoicePedalOpenCloseValue(*voice);

        if (voicePedalOpenCloseValue < 0) continue; // voice's articulation has no poc rank — skip

        // A strictly more-closed new hit (higher poc) chokes softer-ringing voices.
        // Equal-poc same-articulation retriggers are handled by existing cullMaskedVoices.
        if (newArtPedalOpenCloseValue > voicePedalOpenCloseValue) {
            if (LOG_ENABLED(Verbose))
                LOG(Verbose, "chokeHiHatVoicesByPoc: inst=" + this->getId() + " choking voice artPoc=" + juce::String(voicePedalOpenCloseValue)
                    + " newPoc=" + juce::String(newArtPedalOpenCloseValue)
                    + " note=" + juce::String(voice->getCurrentlyPlayingNote()));
            voice->requestFadeOut(fadeTimeSec);
        }
    }
}

void InstrumentPlayer::startSampleVoice(InstrumentPlayerVoice& voice, ArticulationRuntime& art, ArtRtIdx artIdx, const SampleEntry& se, int midiChannel, int midiNoteNumber, float velocityGain, juce::uint8 midiVelocity) {
    if (LOG_ENABLED(Verbose)) {
        auto& instCode = se.sound->sampleSpec->parent->parent->parent->parent->meta.codeRaw;
        auto& artCode = se.sound->sampleSpec->parent->parent->parent->codeRaw;
        auto& filePath = se.sound->sampleSpec->sampleFile.getFullPathName();
        LOG(Verbose, "startVoice note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(midiVelocity)) + " inst " + instCode + " art " + artCode + " micBuf(" + juce::String(se.sound->micBufferChannelStart) + "," + juce::String(se.sound->micBufferChannelCount) + ") file=" + filePath);
    }
    // use samples mic index as voice's hitGroupId so we can use it in stealing logic to prefer stealing voices with same mic
    voice.hitGroupId = se.sound->micBufferChannelStart;

    // Apply overrides BEFORE startVoice so startNote() reads the correct values when
    // baking pitchRatio and ADSR parameters. Setting these after startVoice means the
    // first (and every) note-on uses stale defaults.
    voice.setPitchRatioMultiplier(std::pow(2.0, static_cast<double>(art.parameters.tuningCents) / 1200.0));
    voice.setAttackTimeSec(art.parameters.attackMs >= 0 ? static_cast<float>(art.parameters.attackMs) / 1000.0f : 0.001f);
    voice.setReleaseTimeSec(art.parameters.decayMs >= 0 ? static_cast<float>(art.parameters.decayMs) / 1000.0f : 0.1f);
    voice.setUseOneShotDecayEnvelope(art.parameters.decayMs >= 0);

    this->startVoice(&voice, se.sound, midiChannel, midiNoteNumber, velocityGain);

    voice.artNameId = art.nameId;
    voice.artExprId = art.exprId;
    voice.artRuntimeIdx = artIdx;
    voice.setVelocityGain(velocityGain);
    voice.setMidiVelocity(midiVelocity);
}
