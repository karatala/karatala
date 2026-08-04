#include "InstrumentPlayer.h"
#include "../Config/PluginLogger.h"
#include "ArticulationNames.h"
#include "DrumKitEngine.h"
#include "MicRoutingBuilder.h"
#include "SampleLoader.h"
#include <cmath>

void InstrumentPlayer::clearParameterOverrides() {
    this->setTuningOffsetCents(0);
    this->setAttackOverrideMs(-1);
    this->setDecayOverrideMs(-1);
    this->setVolumeDb(0.0f);
    this->setPan(0);
    this->setFlipLR(false);
    this->setFlipPhase(false);
    this->micBleedLevels.clear();
    this->micBleedLevelsById.clear();
    this->refreshSampleMicBleedGains();
}

void InstrumentPlayer::setTuningOffsetCents(int cents) {
    this->tuningOffsetCents = cents;
    for (auto& art : this->runtimeArticulations) art.parameters.tuningCents = cents;
}

void InstrumentPlayer::setAttackOverrideMs(int ms) {
    if (ms < 0) this->attackOverrideMs.reset();
    else this->attackOverrideMs = ms;
    for (auto& art : this->runtimeArticulations) art.parameters.attackMs = ms;
}

void InstrumentPlayer::setDecayOverrideMs(int ms) {
    if (ms < 0) this->decayOverrideMs.reset();
    else this->decayOverrideMs = ms;
    for (auto& art : this->runtimeArticulations) art.parameters.decayMs = ms;
}

void InstrumentPlayer::setVolumeDb(float db) {
    this->volumeDb = db;
    for (auto& art : this->runtimeArticulations) art.parameters.volumeDb = db;
}

void InstrumentPlayer::configureSoundMicPlacement(
    SampledSound& sound, const KitLib::MicSpec& micSpec, int sourceChannelCount, int destinationChannelCount,
    bool routingAllowsPan, std::optional<int> placementBalance, int layerPan) {
    sound.placementBalance = placementBalance.value_or(0);
    if (destinationChannelCount < 2) {
        sound.micPlacementMode = MicPlacementMode::PreserveChannels;
    } else if (micSpec.type == KitLib::MicSpec::Type::Room) {
        sound.micPlacementMode = sourceChannelCount == 1 ? MicPlacementMode::MonoCenter : MicPlacementMode::PreserveChannels;
    } else if (sourceChannelCount == 1) {
        sound.micPlacementMode = MicPlacementMode::MonoSourceAndLayerPan;
    } else if (micSpec.type == KitLib::MicSpec::Type::Overhead || routingAllowsPan) {
        sound.micPlacementMode = MicPlacementMode::StereoLayerPan;
    } else {
        sound.micPlacementMode = MicPlacementMode::PreserveChannels;
    }
    InstrumentPlayer::updateSoundMicPlacementGains(sound, layerPan);
}

void InstrumentPlayer::updateSoundMicPlacementGains(SampledSound& sound, int layerPan) {
    int effectivePan = 0;
    if (sound.micPlacementMode == MicPlacementMode::MonoSourceAndLayerPan) effectivePan = juce::jlimit(-127, 127, sound.placementBalance + layerPan);
    else if (sound.micPlacementMode == MicPlacementMode::StereoLayerPan) effectivePan = juce::jlimit(-127, 127, layerPan);

    const float normalizedPan = static_cast<float>(effectivePan) / 127.0f;
    float leftGain = 1.0f;
    float rightGain = 1.0f;
    if (sound.micPlacementMode == MicPlacementMode::MonoCenter || sound.micPlacementMode == MicPlacementMode::MonoSourceAndLayerPan) {
        const float panAngle = (normalizedPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        leftGain = std::cos(panAngle);
        rightGain = std::sin(panAngle);
    } else if (sound.micPlacementMode == MicPlacementMode::StereoLayerPan) {
        leftGain = normalizedPan > 0.0f ? 1.0f - normalizedPan : 1.0f;
        rightGain = normalizedPan < 0.0f ? 1.0f + normalizedPan : 1.0f;
    }
    sound.cachedPlacementLeftGain.store(leftGain, std::memory_order_relaxed);
    sound.cachedPlacementRightGain.store(rightGain, std::memory_order_relaxed);
}

void InstrumentPlayer::refreshArticulationMicPlacementGains(ArticulationRuntime& art) {
    for (auto& velocityLevel : art.velocityLevels)
        for (auto& sample : velocityLevel.samples)
            if (sample.sound != nullptr) InstrumentPlayer::updateSoundMicPlacementGains(*sample.sound, art.parameters.pan);
}

void InstrumentPlayer::setPan(int newPan) {
    const int clampedPan = juce::jlimit(-127, 127, newPan);
    this->pan.store(clampedPan, std::memory_order_relaxed);
    for (auto& art : this->runtimeArticulations) {
        art.parameters.pan = clampedPan;
        this->refreshArticulationMicPlacementGains(art);
    }
}

void InstrumentPlayer::setFlipLR(bool shouldFlip) {
    this->flipLR = shouldFlip;
    for (auto& art : this->runtimeArticulations) art.parameters.flipLR = shouldFlip;
}

void InstrumentPlayer::setFlipPhase(bool shouldFlip) {
    this->flipPhase = shouldFlip;
    for (auto& art : this->runtimeArticulations) art.parameters.flipPhase = shouldFlip;
}

void InstrumentPlayer::setMicBleedLevels(std::unordered_map<juce::String, float> levels) {
    this->micBleedLevels.clear();
    this->micBleedLevelsById.clear();
    for (auto& [micCode, level] : levels) {
        const float clamped = juce::jlimit(0.0f, 1.0f, level);
        if (clamped < 0.999f) {
            this->micBleedLevels[micCode] = clamped;
            const auto micCodeId = StringsDictSingleton::getId(micCode);
            if (micCodeId != StringsDictSingleton::EMPTY) this->micBleedLevelsById[micCodeId] = clamped;
        }
    }
    this->refreshSampleMicBleedGains();
}

const InstrumentPlayer::LayerParameters* InstrumentPlayer::getLayerParameters(ArtRtIdx artIdx) const {
    if (artIdx < 0 || static_cast<size_t>(artIdx) >= this->runtimeArticulations.size()) return nullptr;
    return &this->runtimeArticulations[static_cast<size_t>(artIdx)].parameters;
}

void InstrumentPlayer::applyParameterUpdate(ArticulationRuntime& art, const LayerParameterUpdate& update) {
    if (update.tuningCents.has_value()) art.parameters.tuningCents = *update.tuningCents;
    if (update.attackMs.has_value()) art.parameters.attackMs = *update.attackMs;
    if (update.decayMs.has_value()) art.parameters.decayMs = *update.decayMs;
    if (update.volumeDb.has_value()) art.parameters.volumeDb = *update.volumeDb;
    if (update.pan.has_value()) {
        art.parameters.pan = juce::jlimit(-127, 127, *update.pan);
        this->refreshArticulationMicPlacementGains(art);
    }
    if (update.flipLR.has_value()) art.parameters.flipLR = *update.flipLR;
    if (update.flipPhase.has_value()) art.parameters.flipPhase = *update.flipPhase;
    if (update.updateVelocityResponse) {
        art.parameters.velocityResponse = update.velocityResponse;
        if (this->parent != nullptr) art.buildVelocityMapping(this->parent->getVelocityResponse());
    }
    if (update.micBleedLevel.has_value()) {
        const auto targetMicCodeId = StringsDictSingleton::getId(update.micBleedLevel->first.trim().toLowerCase());
        const float targetLevel = juce::jlimit(0.0f, 1.0f, update.micBleedLevel->second);
        for (const auto& velocityLevel : art.velocityLevels) {
            for (const auto& sample : velocityLevel.samples) {
                if (sample.sound == nullptr || sample.sound->sampleSpec == nullptr) continue;
                const auto micCodeId = StringsDictSingleton::getId(sample.sound->sampleSpec->micSpec.raw.trim().toLowerCase());
                if (micCodeId == targetMicCodeId) sample.sound->cachedMicBleedGain.store(targetLevel, std::memory_order_relaxed);
            }
        }
    }
    if (!update.micBleedLevels.has_value()) return;

    std::unordered_map<StringsDictSingleton::Id, float> levelsByMicId;
    for (const auto& [micCode, level] : *update.micBleedLevels) {
        const auto micCodeId = StringsDictSingleton::getId(micCode.trim().toLowerCase());
        if (micCodeId != StringsDictSingleton::EMPTY) levelsByMicId[micCodeId] = juce::jlimit(0.0f, 1.0f, level);
    }
    for (const auto& velocityLevel : art.velocityLevels) {
        for (const auto& sample : velocityLevel.samples) {
            if (sample.sound == nullptr || sample.sound->sampleSpec == nullptr) continue;
            const auto micCodeId = StringsDictSingleton::getId(sample.sound->sampleSpec->micSpec.raw.trim().toLowerCase());
            const auto levelIt = levelsByMicId.find(micCodeId);
            const float level = levelIt != levelsByMicId.end() ? levelIt->second : 1.0f;
            sample.sound->cachedMicBleedGain.store(level, std::memory_order_relaxed);
        }
    }
}

bool InstrumentPlayer::updateArticulationParameters(const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes, const LayerParameterUpdate& update) {
    std::array<bool, 128> matchedArticulations {};
    if (targetRoutes.empty()) {
        for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx)
            if (!this->runtimeArticulations[artIdx].velocityLevels.empty()) matchedArticulations[artIdx] = true;
    } else {
        for (const auto& playbackEntry : this->articulationPlaybackEntries) {
            bool targetMatches = false;
            for (const auto& targetRoute : targetRoutes) {
                if (playbackEntry.articulationSlotNameId == targetRoute.first && playbackEntry.articulationSlotExpressionId == targetRoute.second) {
                    targetMatches = true;
                    break;
                }
            }
            if (!targetMatches) continue;
            ArtRtIdx artIdx = -1;
            if (this->tryGetRuntimeArticulationIdx(playbackEntry, false, artIdx) && artIdx >= 0) matchedArticulations[static_cast<size_t>(artIdx)] = true;
            for (const auto note : playbackEntry.midiNotes) {
                if (note >= this->noteToArticulation.size()) continue;
                const auto mappedArtIdx = this->noteToArticulation[static_cast<size_t>(note)];
                if (mappedArtIdx >= 0) matchedArticulations[static_cast<size_t>(mappedArtIdx)] = true;
            }
        }
        for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx) {
            const auto& art = this->runtimeArticulations[artIdx];
            const auto articulationSlotNameId = art.routeArticulationSlotNameId != 0 ? art.routeArticulationSlotNameId : art.nameId;
            const auto articulationSlotExpressionId = art.routeArticulationSlotNameId != 0 || art.routeArticulationSlotExpressionId != 0 ? art.routeArticulationSlotExpressionId : art.exprId;
            for (const auto& targetRoute : targetRoutes) {
                if (articulationSlotNameId == targetRoute.first && articulationSlotExpressionId == targetRoute.second) matchedArticulations[artIdx] = true;
            }
        }
    }

    bool updated = false;
    for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx) {
        if (!matchedArticulations[artIdx]) continue;
        this->applyParameterUpdate(this->runtimeArticulations[artIdx], update);
        updated = true;
    }
    return updated;
}

float InstrumentPlayer::getMicBleedLevel(const juce::String& micCode) const {
    if (micCode.isEmpty()) return 1.0f;
    return this->getMicBleedLevel(StringsDictSingleton::getId(micCode));
}

float InstrumentPlayer::getMicBleedLevel(StringsDictSingleton::Id micCodeId) const {
    if (micCodeId == StringsDictSingleton::EMPTY) return 1.0f;

    const auto it = this->micBleedLevelsById.find(micCodeId);
    if (it == this->micBleedLevelsById.end()) return 1.0f;

    return juce::jlimit(0.0f, 1.0f, it->second);
}

void InstrumentPlayer::refreshSampleMicBleedGains() {
    for (const auto& soundPtr : this->sampledSoundPtrs) {
        if (!soundPtr || soundPtr->sampleSpec == nullptr) continue;
        soundPtr->cachedMicBleedGain.store(this->getMicBleedLevel(soundPtr->sampleSpec->micSpec.raw), std::memory_order_relaxed);
    }
}
