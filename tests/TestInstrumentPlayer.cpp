#include "../source/Engine/DrumKitEngine.h"
#include "../source/Engine/InstrumentPlayer.h"
#include "catch2/catch_approx.hpp"
#include "catch2/catch_test_macros.hpp"

struct InstrumentPlayerTestAccess {
    static std::vector<InstrumentPlayer::ArticulationRuntime>& articulations(InstrumentPlayer& player) {
        return player.runtimeArticulations;
    }

    static void configureController(InstrumentPlayer& player, int controllerNumber, uint8_t lowArticulationIdx, uint8_t highArticulationIdx) {
        player.ccToArticulationMask[static_cast<size_t>(controllerNumber)] = true;
        auto& mapping = player.ccToArticulationTable[static_cast<size_t>(controllerNumber)];
        for (size_t controllerValue = 0; controllerValue < mapping.size(); ++controllerValue)
            mapping[controllerValue] = controllerValue < 64 ? lowArticulationIdx : highArticulationIdx;
    }

    static void configureCachedController(InstrumentPlayer& player, int controllerNumber, int controllerValue, InstrumentPlayer::ArtRtIdx articulationIdx) {
        player.lastCcValue[static_cast<size_t>(controllerNumber)] = controllerValue;
        player.lastCcArticulation[static_cast<size_t>(controllerNumber)] = articulationIdx;
    }

    static void mapRuntimeArticulation(InstrumentPlayer& player, const KitLib::ArticulationSpec& articulationSpec, InstrumentPlayer::ArtRtIdx articulationIdx) {
        const uint64_t articulationKey = (static_cast<uint64_t>(articulationSpec.nameId) << 32)
            | static_cast<uint64_t>(articulationSpec.exprId);
        player.artIdByNameExprId[articulationKey] = articulationIdx;
    }

    static void advanceRenderedSamples(InstrumentPlayer& player, uint64_t sampleCount) {
        player.renderedSampleCount += sampleCount;
        player.processPendingHiHatControllers();
    }

    static int cachedArticulation(const InstrumentPlayer& player, int controllerNumber) {
        return player.lastCcArticulation[static_cast<size_t>(controllerNumber)];
    }
    static juce::String mappedArticulationName(const InstrumentPlayer& player, int controllerNumber, int controllerValue) {
        const auto artIdx = player.ccToArticulationTable[static_cast<size_t>(controllerNumber)][static_cast<size_t>(controllerValue)];
        return player.getArtName(artIdx);
    }

    static int runtimePedalOpenCloseValue(const InstrumentPlayer& player, const juce::String& articulationName) {
        return runtimeExpressionValue(player, articulationName, [](const auto& art) { return art.pedalOpenCloseValue; });
    }

    static int runtimeZoneBowEdgeValue(const InstrumentPlayer& player, const juce::String& articulationName) {
        return runtimeExpressionValue(player, articulationName, [](const auto& art) { return art.zoneBowEdgeValue; });
    }

    static size_t runtimeVelocityLevelCount(const InstrumentPlayer& player, const juce::String& articulationName) {
        for (const auto& art : player.runtimeArticulations)
            if (art.getCodeRaw() == articulationName) return art.velocityLevels.size();
        return 0;
    }

    static void mapNote(InstrumentPlayer& player, int midiNoteNumber, InstrumentPlayer::ArtRtIdx articulationIdx) {
        player.noteToArticulation[static_cast<size_t>(midiNoteNumber)] = articulationIdx;
    }

    static void setVoiceArticulation(InstrumentPlayerVoice& voice, const KitLib::ArticulationSpec& articulationSpec) {
        voice.artNameId = articulationSpec.nameId;
        voice.artExprId = articulationSpec.exprId;
    }

    static void setVoiceRuntimeArticulation(InstrumentPlayerVoice& voice, InstrumentPlayer::ArtRtIdx articulationIdx) {
        voice.artRuntimeIdx = articulationIdx;
    }

    static void setPedalNote(InstrumentPlayer& player, int midiNoteNumber) {
        player.hihatPedalNote = midiNoteNumber;
    }

    static void loadAftertouchChokeTargets(InstrumentPlayer& player, InstrumentPlayer::ArticulationPlaybackEntry playbackEntry) {
        player.articulationPlaybackEntries.clear();
        player.articulationPlaybackEntries.push_back(std::move(playbackEntry));
        InstrumentPlayer::CcDimensionsMap foundCcDimensions;
        std::set<juce::String> skippedMicCodes;
        player.loadAftertouchChokeTargets(foundCcDimensions, skippedMicCodes);
    }

    static void startVoiceForRender(InstrumentPlayerVoice& voice, InstrumentPlayer& owner, SampledSound& sound) {
        voice.setOwner(&owner);
        voice.setCurrentPlaybackSampleRate(44100.0);
        voice.setAttackTimeSec(0.0f);
        voice.currentlyPlayingNote = 60;
        voice.currentPlayingMidiChannel = 1;
        voice.currentlyPlayingSound = &sound;
        voice.startNote(60, 1.0f, &sound);
    }

    static bool configureMicPlacement(
        InstrumentPlayer& player, SampledSound& sound, const juce::String& micCode, int sourceChannelCount,
        int destinationChannelCount, bool routingAllowsPan, std::optional<int> placementBalance, int layerPan) {
        const auto parsedMic = KitLib::MicSpec::parseMicCode(micCode);
        if (!parsedMic.ok()) return false;
        player.configureSoundMicPlacement(sound, parsedMic.value, sourceChannelCount, destinationChannelCount, routingAllowsPan, placementBalance, layerPan);
        return true;
    }

private:
    template <typename Getter>
    static int runtimeExpressionValue(const InstrumentPlayer& player, const juce::String& articulationName, Getter getter) {
        for (const auto& art : player.runtimeArticulations)
            if (art.getCodeRaw() == articulationName)
                return getter(art);
        return -1;
    }
};

namespace InstrumentPlayerTests {

KitLayout::InstrumentSlot makeHiHatSlot() {
    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    KitLayout::InstrumentSlot::ArticulationSlot pedalTrigger { "pedal" };
    pedalTrigger.notes.push_back(44);
    slot.articulations.push_back(std::move(pedalTrigger));

    KitLayout::InstrumentSlot::ContinuousController pedalController { 4, "pdl_opn_cls" };
    pedalController.notes.push_back(42);
    slot.controllers.push_back(std::move(pedalController));
    return slot;
}

KitLayout::InstrumentSlot makeImplicitHiHatExpressionSlot() {
    KitLayout::InstrumentSlot slot;
    slot.id = "hihat1";
    slot.type = KitLib::InstrumentSpec::Type::HiHat;

    KitLayout::InstrumentSlot::ArticulationSlot edgeOpen { "edge[open]" };
    edgeOpen.notes.push_back(22);
    slot.articulations.push_back(std::move(edgeOpen));

    KitLayout::InstrumentSlot::ContinuousController pedalController { 4, "pdl_opn_cls" };
    pedalController.notes.push_back(22);
    slot.controllers.push_back(std::move(pedalController));
    return slot;
}

KitLayout::InstrumentSlot makeHiHatZoneRemapSlot() {
    KitLayout::InstrumentSlot slot;
    slot.id = "hihat1";
    slot.type = KitLib::InstrumentSpec::Type::HiHat;

    KitLayout::InstrumentSlot::ArticulationSlot bowOpen { "bow[open]" };
    bowOpen.notes.push_back(46);
    slot.articulations.push_back(std::move(bowOpen));

    KitLayout::InstrumentSlot::ArticulationSlot bowTight { "bow[tight]" };
    bowTight.notes.push_back(42);
    slot.articulations.push_back(std::move(bowTight));

    KitLayout::InstrumentSlot::ContinuousController zoneController { 19, "zon_bow_edg" };
    zoneController.notes.push_back(42);
    zoneController.notes.push_back(46);
    slot.controllers.push_back(std::move(zoneController));
    return slot;
}

KitLayout::InstrumentSlot makeCrossLayerHiHatSlot() {
    KitLayout::InstrumentSlot slot;
    slot.id = "hihat2";
    slot.type = KitLib::InstrumentSpec::Type::HiHat;

    KitLayout::InstrumentSlot::ArticulationSlot bowOpen { "bow[open]" };
    bowOpen.notes.push_back(10);
    slot.articulations.push_back(std::move(bowOpen));

    KitLayout::InstrumentSlot::ArticulationSlot bowTight { "bow[tight]" };
    bowTight.notes.push_back(6);
    slot.articulations.push_back(std::move(bowTight));

    KitLayout::InstrumentSlot::ArticulationSlot pedal { "pedal" };
    pedal.notes.push_back(8);
    slot.articulations.push_back(std::move(pedal));
    return slot;
}

KitLayout::InstrumentSlot makeImplicitBowEdgeExpressionSlot() {
    KitLayout::InstrumentSlot slot;
    slot.id = "ride1";
    slot.type = KitLib::InstrumentSpec::Type::Ride;

    KitLayout::InstrumentSlot::ArticulationSlot bow { "bow" };
    bow.notes.push_back(51);
    slot.articulations.push_back(std::move(bow));

    KitLayout::InstrumentSlot::ContinuousController zoneController { 17, "zon_bow_edg" };
    zoneController.notes.push_back(51);
    slot.controllers.push_back(std::move(zoneController));
    return slot;
}
KitLayout::InstrumentSlot makeSimpleStackSlot() {
    KitLayout::InstrumentSlot slot;
    slot.id = "stax1";
    slot.type = KitLib::InstrumentSpec::Type::Stack;

    KitLayout::InstrumentSlot::ArticulationSlot bowTrigger { "bow" };
    bowTrigger.notes.push_back(39);
    slot.articulations.push_back(std::move(bowTrigger));

    KitLayout::InstrumentSlot::ArticulationSlot edgeTrigger { "edge" };
    edgeTrigger.notes.push_back(15);
    slot.articulations.push_back(std::move(edgeTrigger));
    return slot;
}

KitLib::InstrumentSpec::MetaInfo makeStackInstrumentMeta(const juce::String& codeRaw) {
    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = codeRaw;
    meta.type = KitLib::InstrumentSpec::Type::Stack;
    return meta;
}

KitLib::InstrumentSpec::MetaInfo makePlaybackInstrumentMeta(const juce::String& codeRaw, KitLib::InstrumentSpec::Type type) {
    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = codeRaw;
    meta.type = type;
    return meta;
}

void addSlotArticulation(KitLayout::InstrumentSlot& slot, const juce::String& codeRaw, uint8_t midiNote) {
    KitLayout::InstrumentSlot::ArticulationSlot trigger { codeRaw };
    trigger.notes.push_back(midiNote);
    slot.articulations.push_back(std::move(trigger));
}

bool hasNormalPlaybackEntry(const std::vector<InstrumentPlayer::ArticulationPlaybackEntry>& playbackEntries, const juce::String& articulationSlotCodeRaw, const juce::String& libraryArticulationCodeRaw) {
    for (const auto& playbackEntry : playbackEntries) {
        if (playbackEntry.isAliasEntry || playbackEntry.isControllerSibling) continue;
        if (playbackEntry.libraryArticulation == nullptr) continue;
        if (playbackEntry.articulationSlot.codeRaw == articulationSlotCodeRaw && playbackEntry.libraryArticulation->codeRaw == libraryArticulationCodeRaw) return true;
    }
    return false;
}

bool ownsArticulationSlotPlaybackEntry(const std::vector<InstrumentPlayer::ArticulationPlaybackEntry>& playbackEntries, const juce::String& articulationSlotCodeRaw) {
    for (const auto& playbackEntry : playbackEntries) {
        if (playbackEntry.isAliasEntry || playbackEntry.isControllerSibling) continue;
        if (playbackEntry.articulationSlot.codeRaw == articulationSlotCodeRaw) return true;
    }
    return false;
}

bool hasControllerSiblingPlaybackEntry(const std::vector<InstrumentPlayer::ArticulationPlaybackEntry>& playbackEntries, const juce::String& articulationSlotCodeRaw, const juce::String& libraryArticulationCodeRaw) {
    for (const auto& playbackEntry : playbackEntries) {
        if (!playbackEntry.isControllerSibling) continue;
        if (playbackEntry.libraryArticulation == nullptr) continue;
        if (playbackEntry.articulationSlot.codeRaw == articulationSlotCodeRaw && playbackEntry.libraryArticulation->codeRaw == libraryArticulationCodeRaw) return true;
    }
    return false;
}

uint64_t makeArticulationSlotBindingKey(uint32_t articulationNameId, uint32_t expressionId) {
    return (static_cast<uint64_t>(articulationNameId) << 32) | static_cast<uint64_t>(expressionId);
}

KitLib::InstrumentSpec::MetaInfo makeInstrumentMeta() {
    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = "hihat-test";
    meta.type = KitLib::InstrumentSpec::Type::HiHat;
    return meta;
}

KitLib::MicSpec makeDirectMic() {
    KitLib::MicSpec micSpec;
    micSpec.type = KitLib::MicSpec::Type::Direct;
    micSpec.channels = 1;
    return micSpec;
}

KitLib::MicSpec makeOverheadMic() {
    KitLib::MicSpec micSpec;
    micSpec.type = KitLib::MicSpec::Type::Overhead;
    micSpec.channels = 2;
    return micSpec;
}

KitLib::MicSpec makeRoomMic() {
    KitLib::MicSpec micSpec;
    micSpec.type = KitLib::MicSpec::Type::Room;
    micSpec.channels = 2;
    return micSpec;
}

KitLib::DecayEnvelope makeTransitionEnvelope() {
    KitLib::DecayEnvelope envelope;
    envelope.peakDb = -20.0f;
    envelope.peakMs = 10.0f;
    envelope.points[0] = 100.0f;
    envelope.points[1] = 200.0f;
    envelope.validPoints = 2;
    return envelope;
}

struct ControllerTransitionFixture {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot = makeHiHatSlot();
    InstrumentPlayer player { &this->engine, this->slot };
    KitLib::InstrumentSpec::MetaInfo instrumentMeta = makeInstrumentMeta();
    KitLib::InstrumentSpec instrumentSpec { nullptr, this->instrumentMeta };
    KitLib::ArticulationSpec tightSpec { &this->instrumentSpec, "edge[tight]" };
    KitLib::ArticulationSpec openSpec { &this->instrumentSpec, "edge[open]" };
    KitLib::ArticulationSpec pedalSpec { &this->instrumentSpec, "pedal" };
    KitLib::VelocityLevelSpec tightVelocityLevelSpec { &this->tightSpec, 127 };
    KitLib::VelocityLevelSpec openVelocityLevelSpec { &this->openSpec, 127 };
    KitLib::VelocityLevelSpec pedalVelocityLevelSpec { &this->pedalSpec, 127 };
    KitLib::LogicalHitSpec tightLogicalHit { &this->tightVelocityLevelSpec, 1 };
    KitLib::LogicalHitSpec openLogicalHit { &this->openVelocityLevelSpec, 1 };
    KitLib::LogicalHitSpec pedalLogicalHit { &this->pedalVelocityLevelSpec, 1 };
    KitLib::MicSpec directMic = makeDirectMic();
    KitLib::SampleSpec tightSample { &this->tightLogicalHit, {}, this->directMic };
    KitLib::SampleSpec openSample { &this->openLogicalHit, {}, this->directMic };
    KitLib::SampleSpec pedalSample { &this->pedalLogicalHit, {}, this->directMic };
    SampledSound tightSound { nullptr, &this->tightSample };
    SampledSound openSound { nullptr, &this->openSample };
    SampledSound pedalSound { nullptr, &this->pedalSample };
    InstrumentPlayerVoice* tightVoice = nullptr;
    InstrumentPlayerVoice* openVoice = nullptr;
    InstrumentPlayerVoice* pedalVoice = nullptr;

    explicit ControllerTransitionFixture(bool includeOpenTarget = true) {
        this->engine.engineSettings.hihatControllerSmoothingSec.store(0.0f);
        this->engine.engineSettings.hihatOpenCrossfadeSec.store(0.080f);
        this->engine.engineSettings.hihatTransitionAttackGuardSec.store(0.0f);
        this->engine.engineSettings.hihatTransitionMinRemainingSec.store(0.01f);
        this->engine.engineSettings.hihatTransitionSilenceFloorDb.store(-90.0f);
        this->engine.engineSettings.hihatTailMatchAmount.store(0.0f);
        this->player.setCurrentPlaybackSampleRate(44100.0);

        auto& articulations = InstrumentPlayerTestAccess::articulations(this->player);
        articulations.emplace_back(this->tightSpec.nameId, this->tightSpec.exprId);
        articulations.back().pedalOpenCloseValue = 90;
        articulations.back().velocityLevels.emplace_back();
        articulations.back().velocityToLevel.fill(0);
        articulations.emplace_back(this->openSpec.nameId, this->openSpec.exprId);
        articulations.back().pedalOpenCloseValue = 10;
        articulations.back().velocityLevels.emplace_back();
        articulations.back().velocityToLevel.fill(0);
        articulations.emplace_back(this->pedalSpec.nameId, this->pedalSpec.exprId);
        articulations.back().pedalOpenCloseValue = 90;
        articulations.back().velocityLevels.emplace_back();
        articulations.back().velocityToLevel.fill(0);

        InstrumentPlayerTestAccess::mapRuntimeArticulation(this->player, this->tightSpec, 0);
        InstrumentPlayerTestAccess::mapRuntimeArticulation(this->player, this->openSpec, 1);
        InstrumentPlayerTestAccess::mapRuntimeArticulation(this->player, this->pedalSpec, 2);
        InstrumentPlayerTestAccess::mapNote(this->player, 44, 2);
        InstrumentPlayerTestAccess::setPedalNote(this->player, 44);
        InstrumentPlayerTestAccess::configureController(this->player, 4, 1, 0);
        InstrumentPlayerTestAccess::configureCachedController(this->player, 4, 100, 0);

        this->tightSound.sampleRate = 44100.0;
        this->openSound.sampleRate = 44100.0;
        this->pedalSound.sampleRate = 44100.0;
        this->tightSound.decayEnvelope = makeTransitionEnvelope();
        this->openSound.decayEnvelope = makeTransitionEnvelope();
        this->pedalSound.decayEnvelope = makeTransitionEnvelope();
        this->tightSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
        this->openSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
        this->pedalSound.buffer = std::make_shared<juce::AudioBuffer<float>>(1, 88200);
        for (int sampleIdx = 0; sampleIdx < this->openSound.buffer->getNumSamples(); ++sampleIdx) {
            this->tightSound.buffer->setSample(0, sampleIdx, 0.5f);
            this->openSound.buffer->setSample(0, sampleIdx, 0.5f);
            this->pedalSound.buffer->setSample(0, sampleIdx, 0.5f);
        }
        this->tightSound.loaded.store(true);
        this->openSound.loaded.store(true);
        this->pedalSound.loaded.store(true);
        articulations[0].velocityLevels[0].samples.push_back({ &this->tightSound, 1, {} });
        if (includeOpenTarget)
            articulations[1].velocityLevels[0].samples.push_back({ &this->openSound, 1, {} });
        articulations[2].velocityLevels[0].samples.push_back({ &this->pedalSound, 1, {} });
        for (auto& articulation : articulations) {
            auto& velocityLevel = articulation.velocityLevels[0];
            if (velocityLevel.samples.empty()) continue;
            InstrumentPlayer::LogicalHitRuntime logicalHit;
            logicalHit.roundRobinId = 1;
            logicalHit.sampleEntryIndexes.push_back(0);
            logicalHit.readiness->requiredEntryCount = 1;
            logicalHit.readiness->loadedEntryCount.store(1);
            logicalHit.readiness->ready.store(true);
            velocityLevel.logicalHits.push_back(std::move(logicalHit));
        }

        this->tightVoice = this->player.addVoice(new InstrumentPlayerVoice());
        this->openVoice = this->player.addVoice(new InstrumentPlayerVoice());
        this->pedalVoice = this->player.addVoice(new InstrumentPlayerVoice());
        this->player.startVoice(this->tightVoice, &this->tightSound, 1, 42, 0.8f);
        InstrumentPlayerTestAccess::setVoiceArticulation(*this->tightVoice, this->tightSpec);
        this->tightVoice->sourceSamplePosition = 4410.0;
        this->tightVoice->cachedEstimatedDb = -24.0f;
    }
};

struct MultiMicClosingTransitionFixture {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot = makeHiHatSlot();
    InstrumentPlayer player { &this->engine, this->slot };
    KitLib::InstrumentSpec::MetaInfo instrumentMeta = makeInstrumentMeta();
    KitLib::InstrumentSpec instrumentSpec { nullptr, this->instrumentMeta };
    KitLib::ArticulationSpec tightSpec { &this->instrumentSpec, "bow[tight]" };
    KitLib::ArticulationSpec openSpec { &this->instrumentSpec, "bow[open]" };
    KitLib::VelocityLevelSpec tightVelocityLevelSpec { &this->tightSpec, 127 };
    KitLib::VelocityLevelSpec openVelocityLevelSpec { &this->openSpec, 127 };
    KitLib::LogicalHitSpec tightLogicalHit { &this->tightVelocityLevelSpec, 1 };
    KitLib::LogicalHitSpec openLogicalHit { &this->openVelocityLevelSpec, 1 };
    KitLib::MicSpec directMic = makeDirectMic();
    KitLib::MicSpec overheadMic = makeOverheadMic();
    KitLib::MicSpec roomMic = makeRoomMic();
    KitLib::SampleSpec tightDirectSample { &this->tightLogicalHit, {}, this->directMic };
    KitLib::SampleSpec openDirectSample { &this->openLogicalHit, {}, this->directMic };
    KitLib::SampleSpec tightOverheadSample { &this->tightLogicalHit, {}, this->overheadMic };
    KitLib::SampleSpec openOverheadSample { &this->openLogicalHit, {}, this->overheadMic };
    KitLib::SampleSpec tightRoomSample { &this->tightLogicalHit, {}, this->roomMic };
    KitLib::SampleSpec openRoomSample { &this->openLogicalHit, {}, this->roomMic };
    SampledSound tightDirectSound { nullptr, &this->tightDirectSample };
    SampledSound openDirectSound { nullptr, &this->openDirectSample };
    SampledSound tightOverheadSound { nullptr, &this->tightOverheadSample };
    SampledSound openOverheadSound { nullptr, &this->openOverheadSample };
    SampledSound tightRoomSound { nullptr, &this->tightRoomSample };
    SampledSound openRoomSound { nullptr, &this->openRoomSample };
    InstrumentPlayerVoice* sourceDirectVoice = nullptr;
    InstrumentPlayerVoice* sourceOverheadVoice = nullptr;
    InstrumentPlayerVoice* sourceRoomVoice = nullptr;
    std::vector<InstrumentPlayerVoice*> replacementVoices;

    explicit MultiMicClosingTransitionFixture(bool includeDirectReference = true, bool roomReferenceRegionAudible = true) {
        this->engine.engineSettings.hihatControllerSmoothingSec.store(0.0f);
        this->engine.engineSettings.hihatCloseCrossfadeSec.store(0.020f);
        this->engine.engineSettings.hihatTransitionAttackGuardSec.store(0.0f);
        this->engine.engineSettings.hihatTransitionMinRemainingSec.store(0.01f);
        this->engine.engineSettings.hihatTransitionSilenceFloorDb.store(-90.0f);
        this->engine.engineSettings.hihatTailMatchAmount.store(1.0f);
        this->engine.engineSettings.hihatTailMatchBiasDb.store(0.0f);
        this->engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
        this->player.setCurrentPlaybackSampleRate(44100.0);

        auto& articulations = InstrumentPlayerTestAccess::articulations(this->player);
        articulations.emplace_back(this->tightSpec.nameId, this->tightSpec.exprId);
        articulations.back().pedalOpenCloseValue = 90;
        articulations.back().velocityLevels.emplace_back();
        articulations.back().velocityToLevel.fill(0);
        articulations.emplace_back(this->openSpec.nameId, this->openSpec.exprId);
        articulations.back().pedalOpenCloseValue = 10;
        articulations.back().velocityLevels.emplace_back();
        articulations.back().velocityToLevel.fill(0);
        InstrumentPlayerTestAccess::mapRuntimeArticulation(this->player, this->tightSpec, 0);
        InstrumentPlayerTestAccess::mapRuntimeArticulation(this->player, this->openSpec, 1);
        InstrumentPlayerTestAccess::configureController(this->player, 4, 1, 0);
        InstrumentPlayerTestAccess::configureCachedController(this->player, 4, 10, 1);

        this->configureSound(this->tightDirectSound, 0, 1, makeTransitionEnvelope());
        this->configureSound(this->openDirectSound, 0, 1, makeTransitionEnvelope());
        this->configureSound(this->tightOverheadSound, 2, 2, makeTransitionEnvelope());
        this->configureSound(this->openOverheadSound, 2, 2, includeDirectReference ? this->makeLateMatchEnvelope() : makeTransitionEnvelope());
        this->configureSound(this->tightRoomSound, 14, 2, makeTransitionEnvelope());
        this->configureSound(this->openRoomSound, 14, 2, this->makeLateMatchEnvelope());

        this->fillWholeBuffer(this->openDirectSound, 0.5f);
        this->fillWholeBuffer(this->openOverheadSound, 0.5f);
        this->fillWholeBuffer(this->openRoomSound, 0.5f);
        this->fillWholeBuffer(this->tightDirectSound, 0.5f);
        this->fillBufferRange(this->tightOverheadSound, 4200, 5600, 0.5f);
        if (roomReferenceRegionAudible) this->fillBufferRange(this->tightRoomSound, 4200, 5600, 0.5f);

        auto& targetSamples = articulations[0].velocityLevels[0].samples;
        if (includeDirectReference) targetSamples.push_back({ &this->tightDirectSound, 1, {} });
        targetSamples.push_back({ &this->tightOverheadSound, 1, {} });
        targetSamples.push_back({ &this->tightRoomSound, 1, {} });

        if (includeDirectReference) this->sourceDirectVoice = this->addSourceVoice(this->openDirectSound);
        this->sourceOverheadVoice = this->addSourceVoice(this->openOverheadSound);
        this->sourceRoomVoice = this->addSourceVoice(this->openRoomSound);
        for (int voiceIdx = 0; voiceIdx < 3; ++voiceIdx) this->replacementVoices.push_back(this->player.addVoice(new InstrumentPlayerVoice()));
    }

    InstrumentPlayerVoice* findReplacementVoice(const SampledSound& sound) const {
        for (auto* voice : this->replacementVoices)
            if (voice != nullptr && voice->getCurrentlyPlayingSound() == &sound) return voice;
        return nullptr;
    }

private:
    static KitLib::DecayEnvelope makeLateMatchEnvelope() {
        KitLib::DecayEnvelope envelope;
        envelope.peakDb = -20.0f;
        envelope.peakMs = 10.0f;
        envelope.points[0] = 50.0f;
        envelope.points[1] = 100.0f;
        envelope.validPoints = 2;
        return envelope;
    }

    void configureSound(SampledSound& sound, int micBufferChannelStart, int micBufferChannelCount, const KitLib::DecayEnvelope& envelope) {
        sound.sampleRate = 44100.0;
        sound.micBufferChannelStart = micBufferChannelStart;
        sound.micBufferChannelCount = micBufferChannelCount;
        sound.decayEnvelope = envelope;
        sound.buffer = std::make_shared<juce::AudioBuffer<float>>(micBufferChannelCount, 88200);
        sound.buffer->clear();
        sound.loaded.store(true);
    }

    void fillWholeBuffer(SampledSound& sound, float value) {
        this->fillBufferRange(sound, 0, sound.buffer->getNumSamples(), value);
    }

    void fillBufferRange(SampledSound& sound, int startSample, int endSample, float value) {
        for (int channelIdx = 0; channelIdx < sound.buffer->getNumChannels(); ++channelIdx)
            for (int sampleIdx = startSample; sampleIdx < endSample; ++sampleIdx)
                sound.buffer->setSample(channelIdx, sampleIdx, value);
    }

    InstrumentPlayerVoice* addSourceVoice(SampledSound& sound) {
        auto* voice = this->player.addVoice(new InstrumentPlayerVoice());
        this->player.startVoice(voice, &sound, 1, 42, 102.0f / 127.0f);
        InstrumentPlayerTestAccess::setVoiceArticulation(*voice, this->openSpec);
        InstrumentPlayerTestAccess::setVoiceRuntimeArticulation(*voice, 1);
        voice->sourceSamplePosition = 4410.0;
        voice->cachedEstimatedDb = -26.0f;
        return voice;
    }
};

TEST_CASE("InstrumentPlayer - voice starts receive independent event ids", "[InstrumentPlayer]") {
    InstrumentPlayer player(nullptr, KitLayout::InstrumentSlot {});
    player.setCurrentPlaybackSampleRate(44100.0);

    auto* firstVoice = player.addVoice(new InstrumentPlayerVoice());
    auto* secondVoice = player.addVoice(new InstrumentPlayerVoice());
    SampledSound firstSound;
    SampledSound secondSound;

    player.startVoice(firstVoice, &firstSound, 1, 42, 0.8f);
    player.startVoice(secondVoice, &secondSound, 1, 42, 0.8f);

    REQUIRE(firstVoice->getEventId() != 0);
    REQUIRE(secondVoice->getEventId() != 0);
    REQUIRE(firstVoice->getEventId() != secondVoice->getEventId());
}

TEST_CASE("InstrumentPlayer - simple empty articulation aliases to first mapped layer", "[InstrumentPlayer][InstrumentMap]") {
    DrumKitEngine engine;
    const auto slot = makeSimpleStackSlot();
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeStackInstrumentMeta("stack-library") };
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentPlayer::ArticulationNameRemap remap;
    remap[bowId] = edgeId;
    InstrumentPlayer::ArticulationAliasMap aliases;
    aliases[edgeId] = { edgeId, 0 };

    player.loadInstrumentWithRouting(&instrument, {}, { bowId }, {}, remap, {}, aliases);

    const int bowArtIdx = player.resolveArticulation(39);
    const int edgeArtIdx = player.resolveArticulation(15);
    REQUIRE(bowArtIdx >= 0);
    REQUIRE(edgeArtIdx == bowArtIdx);
    CHECK(player.getArtName(edgeArtIdx) == "edge");
}

TEST_CASE("InstrumentPlayer - explicit articulation does not inherit simple fallback alias", "[InstrumentPlayer][InstrumentMap]") {
    DrumKitEngine engine;
    const auto slot = makeSimpleStackSlot();
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeStackInstrumentMeta("stack-library") };
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentPlayer::ArticulationNameRemap remap;
    remap[bowId] = edgeId;

    player.loadInstrumentWithRouting(&instrument, {}, { bowId }, {}, remap, {}, {});

    REQUIRE(player.resolveArticulation(39) >= 0);
    CHECK(player.resolveArticulation(15) < 0);
}

TEST_CASE("InstrumentPlayer - simple articulation aliases survive routing rebuild", "[InstrumentPlayer][InstrumentMap]") {
    DrumKitEngine engine;
    const auto slot = makeSimpleStackSlot();
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeStackInstrumentMeta("stack-library") };
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentPlayer::ArticulationNameRemap remap;
    remap[bowId] = edgeId;
    InstrumentPlayer::ArticulationAliasMap aliases;
    aliases[edgeId] = { edgeId, 0 };

    player.loadInstrumentWithRouting(&instrument, {}, { bowId }, {}, remap, {}, aliases);
    player.rebuildRoutingFromLayout(slot);

    const int bowArtIdx = player.resolveArticulation(39);
    const int edgeArtIdx = player.resolveArticulation(15);
    REQUIRE(bowArtIdx >= 0);
    REQUIRE(edgeArtIdx == bowArtIdx);
}

TEST_CASE("InstrumentPlayer - playback entries keep all primary articulation slot IDs with combined filters", "[InstrumentPlayer][InstrumentMap][CurrentRouting]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "crash1";
    slot.type = KitLib::InstrumentSpec::Type::Crash;
    addSlotArticulation(slot, "bow", 25);
    addSlotArticulation(slot, "edge", 49);
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makePlaybackInstrumentMeta("crash-library", KitLib::InstrumentSpec::Type::Crash) };
    instrument.getOrAddArticulation("bow");
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentPlayer::RoutingContext routingContext;
    routingContext.defaultInstrument = &instrument;
    routingContext.allowedArticulationNameIds = { bowId, edgeId };
    routingContext.allowedArticulationFilters = { "bow", "edge" };
    routingContext.strictArticulationResolution = true;

    const auto playbackEntries = player.buildArticulationPlaybackEntries(slot, routingContext);

    CHECK(hasNormalPlaybackEntry(playbackEntries, "bow", "bow"));
    CHECK(hasNormalPlaybackEntry(playbackEntries, "edge", "edge"));
}

TEST_CASE("InstrumentPlayer - playback entries keep the rim articulation slot when the library articulation is center", "[InstrumentPlayer][InstrumentMap][CurrentRouting]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "snare1";
    slot.type = KitLib::InstrumentSpec::Type::Snare;
    addSlotArticulation(slot, "center", 38);
    addSlotArticulation(slot, "rim", 40);
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makePlaybackInstrumentMeta("snare-library", KitLib::InstrumentSpec::Type::Snare) };
    instrument.getOrAddArticulation("center");

    const auto centerId = StringsDictSingleton::getId("center");
    const auto rimId = StringsDictSingleton::getId("rim");
    InstrumentPlayer::RoutingContext routingContext;
    routingContext.defaultInstrument = &instrument;
    routingContext.allowedArticulationNameIds = { centerId, rimId };
    routingContext.allowedArticulationFilters = { "center", "rim" };
    routingContext.artNameRemap[rimId] = centerId;
    routingContext.strictArticulationResolution = true;

    const auto playbackEntries = player.buildArticulationPlaybackEntries(slot, routingContext);

    CHECK(hasNormalPlaybackEntry(playbackEntries, "center", "center"));
    CHECK(hasNormalPlaybackEntry(playbackEntries, "rim", "center"));
}

TEST_CASE("InstrumentPlayer - hi-hat playback entries keep bow edge and pedal rows with combined filters", "[InstrumentPlayer][InstrumentMap][ExpressionPoints][CurrentRouting]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "hihat1";
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    addSlotArticulation(slot, "bow[open]", 46);
    addSlotArticulation(slot, "bow[tight]", 42);
    addSlotArticulation(slot, "edge[open]", 22);
    addSlotArticulation(slot, "edge[tight]", 18);
    addSlotArticulation(slot, "pedal", 44);
    KitLayout::InstrumentSlot::ContinuousController pedalController { 1, "pdl_opn_cls" };
    pedalController.notes.push_back(22);
    pedalController.notes.push_back(46);
    slot.controllers.push_back(std::move(pedalController));
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makePlaybackInstrumentMeta("hihat-library", KitLib::InstrumentSpec::Type::HiHat) };
    instrument.getOrAddArticulation("edge[open]");
    instrument.getOrAddArticulation("edge[half]");
    instrument.getOrAddArticulation("edge[close]");
    instrument.getOrAddArticulation("edge[tight]");
    instrument.getOrAddArticulation("pedal");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    const auto pedalId = StringsDictSingleton::getId("pedal");
    InstrumentPlayer::RoutingContext routingContext;
    routingContext.defaultInstrument = &instrument;
    routingContext.allowedArticulationNameIds = { bowId, edgeId, pedalId };
    routingContext.allowedArticulationFilters = { "bow", "edge", "pedal" };
    routingContext.artNameRemap[bowId] = edgeId;
    routingContext.strictArticulationResolution = true;

    const auto playbackEntries = player.buildArticulationPlaybackEntries(slot, routingContext);

    CHECK(hasNormalPlaybackEntry(playbackEntries, "bow[open]", "edge[open]"));
    CHECK(hasNormalPlaybackEntry(playbackEntries, "edge[open]", "edge[open]"));
    CHECK(hasNormalPlaybackEntry(playbackEntries, "pedal", "pedal"));
    CHECK(hasControllerSiblingPlaybackEntry(playbackEntries, "bow[open]", "edge[half]"));
    CHECK(ownsArticulationSlotPlaybackEntry(playbackEntries, "edge[tight]"));
}

TEST_CASE("InstrumentPlayer - explicit hi-hat anchors keep articulation slot expressions and same-family CC siblings", "[InstrumentPlayer][InstrumentMap][ExpressionPoints][CurrentRouting]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "hihat1";
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    addSlotArticulation(slot, "bow[open]", 46);
    addSlotArticulation(slot, "bow[tight]", 42);
    KitLayout::InstrumentSlot::ContinuousController pedalController { 4, "pdl_opn_cls" };
    pedalController.notes.push_back(42);
    pedalController.notes.push_back(46);
    slot.controllers.push_back(std::move(pedalController));
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makePlaybackInstrumentMeta("hihat-library", KitLib::InstrumentSpec::Type::HiHat) };
    auto* edgeOpen = instrument.getOrAddArticulation("edge[open]");
    instrument.getOrAddArticulation("edge[half]");
    auto* edgeClose = instrument.getOrAddArticulation("edge[close]");
    auto* edgeTight = instrument.getOrAddArticulation("edge[tight]");
    instrument.getOrAddArticulation("bow[half]");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    const auto openId = StringsDictSingleton::getId("open");
    const auto closeId = StringsDictSingleton::getId("close");
    const auto tightId = StringsDictSingleton::getId("tight");
    InstrumentPlayer::RoutingContext routingContext;
    routingContext.defaultInstrument = &instrument;
    routingContext.allowedArticulationFilters = { "bow[open]", "bow[tight]" };
    routingContext.artExactRemap[makeArticulationSlotBindingKey(bowId, openId)] = { edgeId, openId };
    routingContext.artExactRemap[makeArticulationSlotBindingKey(bowId, tightId)] = { edgeId, tightId };
    routingContext.strictArticulationResolution = true;

    auto playbackEntries = player.buildArticulationPlaybackEntries(slot, routingContext);
    CHECK(hasNormalPlaybackEntry(playbackEntries, "bow[open]", "edge[open]"));
    CHECK(hasNormalPlaybackEntry(playbackEntries, "bow[tight]", "edge[tight]"));
    CHECK(hasControllerSiblingPlaybackEntry(playbackEntries, "bow[open]", "edge[half]"));
    CHECK(hasControllerSiblingPlaybackEntry(playbackEntries, "bow[open]", "edge[close]"));
    CHECK_FALSE(hasControllerSiblingPlaybackEntry(playbackEntries, "bow[open]", "bow[half]"));

    player.loadInstrumentWithPlaybackEntries(&instrument, routingContext, std::move(playbackEntries));
    CHECK(player.routesArticulationSlot(bowId, openId));
    CHECK(player.routesArticulationSlot(bowId, tightId));
    CHECK_FALSE(player.routesArticulationSlot(bowId, closeId));
    CHECK(player.routesLibraryArticulation(instrument, edgeOpen));
    CHECK(player.routesLibraryArticulation(instrument, edgeTight));
    CHECK_FALSE(player.routesLibraryArticulation(instrument, edgeClose));
}

TEST_CASE("InstrumentPlayer - explicit cymbal playback entries that share the edge library articulation stay separate", "[InstrumentPlayer][InstrumentMap][CurrentRouting]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "china1";
    slot.type = KitLib::InstrumentSpec::Type::China;
    addSlotArticulation(slot, "bow", 25);
    addSlotArticulation(slot, "edge", 49);
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makePlaybackInstrumentMeta("china-library", KitLib::InstrumentSpec::Type::China) };
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentPlayer::RoutingContext routingContext;
    routingContext.defaultInstrument = &instrument;
    routingContext.allowedArticulationNameIds = { bowId, edgeId };
    routingContext.allowedArticulationFilters = { "bow", "edge" };
    routingContext.artNameRemap[bowId] = edgeId;
    routingContext.strictArticulationResolution = true;

    auto playbackEntries = player.buildArticulationPlaybackEntries(slot, routingContext);
    player.loadInstrumentWithPlaybackEntries(&instrument, routingContext, std::move(playbackEntries));

    const int bowArtIdx = player.resolveArticulation(25);
    const int edgeArtIdx = player.resolveArticulation(49);
    REQUIRE(bowArtIdx >= 0);
    REQUIRE(edgeArtIdx >= 0);
    CHECK(bowArtIdx != edgeArtIdx);
    CHECK(player.getArtName(bowArtIdx) == "edge");
    CHECK(player.getArtName(edgeArtIdx) == "edge");
}

TEST_CASE("InstrumentPlayer - explicit rim playback entry that shares the center library articulation stays separate", "[InstrumentPlayer][InstrumentMap][CurrentRouting]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "tom2";
    slot.type = KitLib::InstrumentSpec::Type::Tom;
    addSlotArticulation(slot, "center", 45);
    addSlotArticulation(slot, "rim", 47);
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makePlaybackInstrumentMeta("tom-library", KitLib::InstrumentSpec::Type::Tom) };
    instrument.getOrAddArticulation("center");

    const auto centerId = StringsDictSingleton::getId("center");
    const auto rimId = StringsDictSingleton::getId("rim");
    InstrumentPlayer::RoutingContext routingContext;
    routingContext.defaultInstrument = &instrument;
    routingContext.allowedArticulationNameIds = { centerId, rimId };
    routingContext.allowedArticulationFilters = { "center", "rim" };
    routingContext.artNameRemap[rimId] = centerId;
    routingContext.strictArticulationResolution = true;

    auto playbackEntries = player.buildArticulationPlaybackEntries(slot, routingContext);
    player.loadInstrumentWithPlaybackEntries(&instrument, routingContext, std::move(playbackEntries));

    const int centerArtIdx = player.resolveArticulation(45);
    const int rimArtIdx = player.resolveArticulation(47);
    REQUIRE(centerArtIdx >= 0);
    REQUIRE(rimArtIdx >= 0);
    CHECK(centerArtIdx != rimArtIdx);
    CHECK(player.getArtName(centerArtIdx) == "center");
    CHECK(player.getArtName(rimArtIdx) == "center");
}

TEST_CASE("InstrumentPlayer - implicit pdl_opn_cls points route simplified hi-hat articulations", "[InstrumentPlayer][ExpressionPoints]") {
    DrumKitEngine engine;
    const auto slot = makeImplicitHiHatExpressionSlot();
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    instrument.getOrAddArticulation("edge[open]");
    instrument.getOrAddArticulation("edge[half]");
    instrument.getOrAddArticulation("edge[close]");
    instrument.getOrAddArticulation("edge[tight]");

    const auto edgeId = StringsDictSingleton::getId("edge");
    player.loadInstrumentWithRouting(&instrument, {}, { edgeId });

    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 10) == "edge[open]");
    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 40) == "edge[half]");
    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 70) == "edge[close]");
    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 90) == "edge[tight]");
}

TEST_CASE("InstrumentPlayer - slot expression points override implicit pdl_opn_cls defaults", "[InstrumentPlayer][ExpressionPoints]") {
    DrumKitEngine engine;
    auto slot = makeImplicitHiHatExpressionSlot();
    slot.expressionPoints["pdl_opn_cls"]["open"] = 10;
    slot.expressionPoints["pdl_opn_cls"]["half"] = 20;
    slot.expressionPoints["pdl_opn_cls"]["close"] = 30;
    slot.expressionPoints["pdl_opn_cls"]["tight"] = 40;

    InstrumentPlayer player { &engine, slot };
    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    instrument.getOrAddArticulation("edge[open]");
    instrument.getOrAddArticulation("edge[half]");
    instrument.getOrAddArticulation("edge[close]");
    instrument.getOrAddArticulation("edge[tight]");

    const auto edgeId = StringsDictSingleton::getId("edge");
    player.loadInstrumentWithRouting(&instrument, {}, { edgeId });

    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 15) == "edge[half]");
    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 35) == "edge[tight]");
}

TEST_CASE("InstrumentPlayer - routing rebuild refreshes implicit pdl_opn_cls runtime values", "[InstrumentPlayer][ExpressionPoints]") {
    DrumKitEngine engine;
    auto slot = makeImplicitHiHatExpressionSlot();
    InstrumentPlayer player { &engine, slot };
    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    auto* open = instrument.getOrAddArticulation("edge[open]");
    instrument.getOrAddArticulation("edge[half]");
    instrument.getOrAddArticulation("edge[close]");
    auto* tight = instrument.getOrAddArticulation("edge[tight]");
    open->getOrAddVelocityLevel(127);
    tight->getOrAddVelocityLevel(127);

    const auto edgeId = StringsDictSingleton::getId("edge");
    player.loadInstrumentWithRouting(&instrument, {}, { edgeId });
    REQUIRE(InstrumentPlayerTestAccess::runtimeVelocityLevelCount(player, "edge[open]") == 1);
    REQUIRE(InstrumentPlayerTestAccess::runtimeVelocityLevelCount(player, "edge[tight]") == 1);

    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 90) == "edge[tight]");
    CHECK(InstrumentPlayerTestAccess::runtimePedalOpenCloseValue(player, "edge[tight]") == 85);

    slot.expressionPoints["pdl_opn_cls"]["open"] = 10;
    slot.expressionPoints["pdl_opn_cls"]["half"] = 20;
    slot.expressionPoints["pdl_opn_cls"]["close"] = 30;
    slot.expressionPoints["pdl_opn_cls"]["tight"] = 40;
    player.rebuildRoutingFromLayout(slot);

    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 35) == "edge[tight]");
    CHECK(InstrumentPlayerTestAccess::runtimePedalOpenCloseValue(player, "edge[open]") == 10);
    CHECK(InstrumentPlayerTestAccess::runtimePedalOpenCloseValue(player, "edge[tight]") == 40);
    CHECK(InstrumentPlayerTestAccess::runtimeVelocityLevelCount(player, "edge[open]") == 1);
    CHECK(InstrumentPlayerTestAccess::runtimeVelocityLevelCount(player, "edge[tight]") == 1);
}
TEST_CASE("InstrumentPlayer - explicit expression folders override layout expression points", "[InstrumentPlayer][ExpressionPoints]") {
    DrumKitEngine engine;
    auto slot = makeImplicitHiHatExpressionSlot();
    slot.expressionPoints["pdl_opn_cls"]["open"] = 50;

    InstrumentPlayer player { &engine, slot };
    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    auto* open = instrument.getOrAddArticulation("edge[open]");
    open->addExpressionDimensionValue("pdl_opn_cls", 10);
    instrument.getOrAddArticulation("edge[half]");

    const auto edgeId = StringsDictSingleton::getId("edge");
    player.loadInstrumentWithRouting(&instrument, {}, { edgeId });

    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 10) == "edge[open]");
    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 11) == "edge[half]");
}

TEST_CASE("InstrumentPlayer - explicit expression folders override layout expression points after rebuild", "[InstrumentPlayer][ExpressionPoints]") {
    DrumKitEngine engine;
    auto slot = makeImplicitHiHatExpressionSlot();

    InstrumentPlayer player { &engine, slot };
    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    auto* open = instrument.getOrAddArticulation("edge[open]");
    open->addExpressionDimensionValue("pdl_opn_cls", 10);
    instrument.getOrAddArticulation("edge[half]");

    const auto edgeId = StringsDictSingleton::getId("edge");
    player.loadInstrumentWithRouting(&instrument, {}, { edgeId });

    slot.expressionPoints["pdl_opn_cls"]["open"] = 80;
    player.rebuildRoutingFromLayout(slot);

    CHECK(InstrumentPlayerTestAccess::runtimePedalOpenCloseValue(player, "edge[open]") == 10);
    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 10) == "edge[open]");
    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 4, 11) == "edge[half]");
}
TEST_CASE("InstrumentPlayer - implicit zon_bow_edg points route bow and edge on one cymbal note", "[InstrumentPlayer][ExpressionPoints]") {
    DrumKitEngine engine;
    const auto slot = makeImplicitBowEdgeExpressionSlot();
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = "ride-test";
    meta.type = KitLib::InstrumentSpec::Type::Ride;
    KitLib::InstrumentSpec instrument { nullptr, meta };
    instrument.getOrAddArticulation("bow");
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    player.loadInstrumentWithRouting(&instrument, {}, { bowId });

    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 17, 10) == "bow");
    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 17, 90) == "edge");
}

TEST_CASE("InstrumentPlayer - routing rebuild refreshes implicit zon_bow_edg runtime values", "[InstrumentPlayer][ExpressionPoints]") {
    DrumKitEngine engine;
    auto slot = makeImplicitBowEdgeExpressionSlot();
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = "ride-test";
    meta.type = KitLib::InstrumentSpec::Type::Ride;
    KitLib::InstrumentSpec instrument { nullptr, meta };
    instrument.getOrAddArticulation("bow");
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    player.loadInstrumentWithRouting(&instrument, {}, { bowId });

    CHECK(InstrumentPlayerTestAccess::runtimeZoneBowEdgeValue(player, "bow") == 24);
    CHECK(InstrumentPlayerTestAccess::runtimeZoneBowEdgeValue(player, "edge") == 85);

    slot.expressionPoints["zon_bow_edg"]["bow"] = 5;
    slot.expressionPoints["zon_bow_edg"]["edge"] = 100;
    player.rebuildRoutingFromLayout(slot);

    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 17, 4) == "bow");
    CHECK(InstrumentPlayerTestAccess::mappedArticulationName(player, 17, 90) == "edge");
    CHECK(InstrumentPlayerTestAccess::runtimeZoneBowEdgeValue(player, "bow") == 5);
    CHECK(InstrumentPlayerTestAccess::runtimeZoneBowEdgeValue(player, "edge") == 100);
}
TEST_CASE("InstrumentPlayer - zone controller siblings preserve hihat note expression", "[InstrumentPlayer][ExpressionPoints][InstrumentMap]") {
    DrumKitEngine engine;
    const auto slot = makeHiHatZoneRemapSlot();
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    instrument.getOrAddArticulation("edge[open]");
    instrument.getOrAddArticulation("edge[tight]");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentPlayer::ArticulationNameRemap remap;
    remap[bowId] = edgeId;
    player.loadInstrumentWithRouting(&instrument, {}, { bowId }, {}, remap);

    const int openIdx = player.resolveArticulation(46);
    const int tightIdx = player.resolveArticulation(42);
    REQUIRE(openIdx >= 0);
    REQUIRE(tightIdx >= 0);
    CHECK(player.getArtName(openIdx) == "edge[open]");
    CHECK(player.getArtName(tightIdx) == "edge[tight]");
}

TEST_CASE("InstrumentPlayer - non-pedal hihat controller does not trigger live transition", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    InstrumentPlayerTestAccess::configureController(fixture.player, 19, 1, 0);

    fixture.player.handleController(1, 19, 10);

    const auto counters = fixture.player.getHiHatTransitionCounters();
    CHECK(counters.processed == 0);
    CHECK(fixture.tightVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
}
TEST_CASE("InstrumentPlayer - controller uses accepted per-voice tail replacement", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;

    fixture.player.handleController(1, 4, 10);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.tightVoice->adsr.getParameters().release == Catch::Approx(0.080f));
    REQUIRE(fixture.openVoice->getCurrentlyPlayingSound() == &fixture.openSound);
    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
    REQUIRE(fixture.openVoice->sourceSamplePosition == Catch::Approx(4410.0));
    const auto counters = fixture.player.getHiHatTransitionCounters();
    REQUIRE(counters.completed == 1);
    REQUIRE(counters.skipped == 0);
}

TEST_CASE("InstrumentPlayer - unavailable target leaves the ringing voice unchanged", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture(false);

    fixture.player.handleController(1, 4, 10);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
    REQUIRE_FALSE(fixture.openVoice->isVoiceActive());
    REQUIRE(InstrumentPlayerTestAccess::cachedArticulation(fixture.player, 4) == 0);
    const auto counters = fixture.player.getHiHatTransitionCounters();
    REQUIRE(counters.completed == 0);
    REQUIRE(counters.skipped == 1);
}

TEST_CASE("InstrumentPlayer - silent overhead and room targets reuse the direct transition offset", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    MultiMicClosingTransitionFixture fixture;

    fixture.player.handleController(1, 4, 100);

    REQUIRE(fixture.sourceDirectVoice != nullptr);
    CHECK(fixture.sourceDirectVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    CHECK(fixture.sourceOverheadVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    CHECK(fixture.sourceRoomVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    auto* directReplacement = fixture.findReplacementVoice(fixture.tightDirectSound);
    auto* overheadReplacement = fixture.findReplacementVoice(fixture.tightOverheadSound);
    auto* roomReplacement = fixture.findReplacementVoice(fixture.tightRoomSound);
    REQUIRE(directReplacement != nullptr);
    REQUIRE(overheadReplacement != nullptr);
    REQUIRE(roomReplacement != nullptr);
    CHECK(directReplacement->sourceSamplePosition == Catch::Approx(4410.0).margin(24.0));
    CHECK(overheadReplacement->sourceSamplePosition == Catch::Approx(directReplacement->sourceSamplePosition).margin(1.0));
    CHECK(roomReplacement->sourceSamplePosition == Catch::Approx(directReplacement->sourceSamplePosition).margin(1.0));
    const auto counters = fixture.player.getHiHatTransitionCounters();
    CHECK(counters.completed == 1);
    CHECK(counters.skipped == 0);
}

TEST_CASE("InstrumentPlayer - room target reuses the overhead offset when direct is absent", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    MultiMicClosingTransitionFixture fixture(false);

    fixture.player.handleController(1, 4, 100);

    auto* overheadReplacement = fixture.findReplacementVoice(fixture.tightOverheadSound);
    auto* roomReplacement = fixture.findReplacementVoice(fixture.tightRoomSound);
    REQUIRE(overheadReplacement != nullptr);
    REQUIRE(roomReplacement != nullptr);
    CHECK(overheadReplacement->sourceSamplePosition == Catch::Approx(4410.0).margin(24.0));
    CHECK(roomReplacement->sourceSamplePosition == Catch::Approx(overheadReplacement->sourceSamplePosition).margin(1.0));
    CHECK(fixture.sourceRoomVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
}

TEST_CASE("InstrumentPlayer - silent closing target fades the old mic when reference offsets are also silent", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    MultiMicClosingTransitionFixture fixture(true, false);

    fixture.player.handleController(1, 4, 100);

    CHECK(fixture.findReplacementVoice(fixture.tightRoomSound) == nullptr);
    REQUIRE(fixture.sourceRoomVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    CHECK(fixture.sourceRoomVoice->adsr.getParameters().release == Catch::Approx(0.020f));
    CHECK(InstrumentPlayerTestAccess::cachedArticulation(fixture.player, 4) == 0);
    const auto counters = fixture.player.getHiHatTransitionCounters();
    CHECK(counters.completed == 1);
    CHECK(counters.skipped == 0);
}

TEST_CASE("InstrumentPlayer - controller smoothing retains the latest value", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatControllerSmoothingSec.store(0.010f);

    const auto slot = makeHiHatSlot();
    InstrumentPlayer player(&engine, slot);
    player.setCurrentPlaybackSampleRate(1000.0);
    auto& articulations = InstrumentPlayerTestAccess::articulations(player);
    articulations.emplace_back(1, 1);
    articulations.back().pedalOpenCloseValue = 10;
    articulations.emplace_back(1, 2);
    articulations.back().pedalOpenCloseValue = 90;
    InstrumentPlayerTestAccess::configureController(player, 4, 0, 1);

    player.handleController(1, 4, 10);
    player.handleController(1, 4, 80);
    player.handleController(1, 4, 100);

    auto counters = player.getHiHatTransitionCounters();
    REQUIRE(counters.processed == 1);
    REQUIRE(counters.coalesced == 2);

    InstrumentPlayerTestAccess::advanceRenderedSamples(player, 10);

    counters = player.getHiHatTransitionCounters();
    REQUIRE(counters.processed == 2);
    REQUIRE(counters.coalesced == 2);
}

TEST_CASE("InstrumentPlayer - attack guard retries a stopped slow-close controller move", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
    fixture.engine.engineSettings.hihatControllerSmoothingSec.store(0.020f);
    fixture.engine.engineSettings.hihatTransitionAttackGuardSec.store(0.094f);
    fixture.tightVoice->killImmediately();
    fixture.player.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.8f);
    fixture.openVoice->sourceSamplePosition = 0.0;
    fixture.openVoice->cachedEstimatedDb = -24.0f;
    InstrumentPlayerTestAccess::configureCachedController(fixture.player, 4, 10, 1);

    fixture.player.handleController(1, 4, 100);

    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
    REQUIRE_FALSE(fixture.tightVoice->isVoiceActive());
    auto counters = fixture.player.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 0);

    fixture.openVoice->sourceSamplePosition = 4410.0;
    InstrumentPlayerTestAccess::advanceRenderedSamples(fixture.player, 882);

    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.tightVoice->getCurrentlyPlayingSound() == &fixture.tightSound);
    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
    counters = fixture.player.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 1);
}

TEST_CASE("InstrumentPlayer - voice pressure retries after a replacement voice becomes free", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
    fixture.engine.engineSettings.hihatControllerSmoothingSec.store(0.020f);
    fixture.player.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.5f);
    fixture.player.startVoice(fixture.pedalVoice, &fixture.openSound, 1, 42, 0.5f);

    fixture.player.handleController(1, 4, 10);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
    auto counters = fixture.player.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 0);

    fixture.openVoice->killImmediately();
    InstrumentPlayerTestAccess::advanceRenderedSamples(fixture.player, 882);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.openVoice->getCurrentlyPlayingSound() == &fixture.openSound);
    counters = fixture.player.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 1);
}

TEST_CASE("InstrumentPlayer - audible open tail creates a level-matched pedal event and preserves tight voices", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);
    fixture.engine.engineSettings.hihatTransitionAttackGuardSec.store(0.0f);
    auto& articulations = InstrumentPlayerTestAccess::articulations(fixture.player);
    articulations[0].pedalOpenCloseValue = 85;
    articulations[2].pedalOpenCloseValue = 85;
    articulations[0].velocityLevels[0].samples.clear();
    fixture.player.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.8f);
    InstrumentPlayerTestAccess::setVoiceArticulation(*fixture.openVoice, fixture.openSpec);
    fixture.openVoice->sourceSamplePosition = 4410.0;
    fixture.openVoice->cachedEstimatedDb = -24.0f;
    InstrumentPlayerTestAccess::configureCachedController(fixture.player, 4, 10, 1);

    fixture.player.handleController(1, 4, 100);

    REQUIRE(fixture.pedalVoice->getCurrentlyPlayingSound() == &fixture.pedalSound);
    REQUIRE(fixture.pedalVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
    REQUIRE(fixture.pedalVoice->getVelocityGain() == Catch::Approx(80.0f / 127.0f));

    const auto pedalEventId = fixture.pedalVoice->getEventId();
    fixture.player.handleController(1, 4, 100);
    REQUIRE(fixture.pedalVoice->getEventId() == pedalEventId);
}

TEST_CASE("InstrumentPlayer - hi-hat note choke uses the exact runtime articulation when library articulation names repeat", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.tightVoice->killImmediately();

    auto& articulations = InstrumentPlayerTestAccess::articulations(fixture.player);
    InstrumentPlayer::ArticulationRuntime plainOpen { fixture.openSpec.nameId, fixture.openSpec.exprId };
    plainOpen.pedalOpenCloseValue = -1;
    articulations.insert(articulations.begin(), std::move(plainOpen));
    InstrumentPlayerTestAccess::mapNote(fixture.player, 44, 3);

    fixture.player.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.8f);
    InstrumentPlayerTestAccess::setVoiceArticulation(*fixture.openVoice, fixture.openSpec);
    InstrumentPlayerTestAccess::setVoiceRuntimeArticulation(*fixture.openVoice, 2);
    fixture.openVoice->sourceSamplePosition = 4410.0;
    fixture.openVoice->cachedEstimatedDb = -24.0f;

    REQUIRE(fixture.player.playNote(1, 44, 100));

    const bool startedPedalSound = fixture.tightVoice->getCurrentlyPlayingSound() == &fixture.pedalSound
        || fixture.pedalVoice->getCurrentlyPlayingSound() == &fixture.pedalSound;
    CHECK(fixture.openVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    CHECK(startedPedalSound);
}

TEST_CASE("InstrumentPlayer - unrouted tight and pedal notes choke open voices in a sibling hi-hat map layer", "[InstrumentPlayer][HiHatTransition][ControllerPath][LayeredMap]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatCloseCrossfadeSec.store(0.020f);
    const auto slot = makeCrossLayerHiHatSlot();
    InstrumentPlayer openLayerPlayer { &engine, slot };
    openLayerPlayer.setCurrentPlaybackSampleRate(44100.0);
    openLayerPlayer.rebuildRoutingFromLayout(slot);

    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    KitLib::ArticulationSpec openSpec { &instrument, "bow[open]" };
    KitLib::VelocityLevelSpec openVelocityLevelSpec { &openSpec, 127 };
    KitLib::LogicalHitSpec openLogicalHit { &openVelocityLevelSpec, 1 };
    const auto directMic = makeDirectMic();
    KitLib::SampleSpec openSample { &openLogicalHit, {}, directMic };
    SampledSound openSound { nullptr, &openSample };

    auto& articulations = InstrumentPlayerTestAccess::articulations(openLayerPlayer);
    articulations.emplace_back(openSpec.nameId, openSpec.exprId);
    articulations.back().pedalOpenCloseValue = 24;
    InstrumentPlayerTestAccess::mapRuntimeArticulation(openLayerPlayer, openSpec, 0);
    InstrumentPlayerTestAccess::mapNote(openLayerPlayer, 10, 0);

    auto* openVoice = openLayerPlayer.addVoice(new InstrumentPlayerVoice());
    openLayerPlayer.startVoice(openVoice, &openSound, 1, 10, 0.8f);
    InstrumentPlayerTestAccess::setVoiceArticulation(*openVoice, openSpec);
    InstrumentPlayerTestAccess::setVoiceRuntimeArticulation(*openVoice, 0);

    SECTION("tight note") {
        openLayerPlayer.handleMidiEvent(juce::MidiMessage::noteOn(1, 6, static_cast<juce::uint8>(100)));
        CHECK(openVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
        CHECK(openVoice->adsr.getParameters().release == Catch::Approx(0.020f));
    }

    SECTION("pedal note") {
        openLayerPlayer.handleMidiEvent(juce::MidiMessage::noteOn(1, 8, static_cast<juce::uint8>(100)));
        CHECK(openVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
        CHECK(openVoice->adsr.getParameters().release == Catch::Approx(0.020f));
    }

    SECTION("note outside this hihat slot") {
        openLayerPlayer.handleMidiEvent(juce::MidiMessage::noteOn(1, 18, static_cast<juce::uint8>(100)));
        CHECK(openVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
    }
}

TEST_CASE("InstrumentPlayer - hi-hat controller close uses the exact runtime articulation when library articulation names repeat", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
    fixture.tightVoice->killImmediately();

    auto& articulations = InstrumentPlayerTestAccess::articulations(fixture.player);
    InstrumentPlayer::ArticulationRuntime plainOpen { fixture.openSpec.nameId, fixture.openSpec.exprId };
    plainOpen.pedalOpenCloseValue = -1;
    articulations.insert(articulations.begin(), std::move(plainOpen));
    InstrumentPlayerTestAccess::configureController(fixture.player, 4, 2, 1);
    InstrumentPlayerTestAccess::configureCachedController(fixture.player, 4, 10, 2);

    fixture.player.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.8f);
    InstrumentPlayerTestAccess::setVoiceArticulation(*fixture.openVoice, fixture.openSpec);
    InstrumentPlayerTestAccess::setVoiceRuntimeArticulation(*fixture.openVoice, 2);
    fixture.openVoice->sourceSamplePosition = 4410.0;
    fixture.openVoice->cachedEstimatedDb = -24.0f;

    fixture.player.handleController(1, 4, 100);

    CHECK(fixture.openVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    CHECK(fixture.tightVoice->getCurrentlyPlayingSound() == &fixture.tightSound);
}

TEST_CASE("InstrumentPlayer - closing the pedal in silence does not create a pedal event", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);
    fixture.tightVoice->killImmediately();
    InstrumentPlayerTestAccess::configureCachedController(fixture.player, 4, 10, 1);

    fixture.player.handleController(1, 4, 100);

    REQUIRE_FALSE(fixture.openVoice->isVoiceActive());
    REQUIRE_FALSE(fixture.pedalVoice->isVoiceActive());
}

TEST_CASE("InstrumentPlayer - tight-only playback does not create an implicit pedal event", "[InstrumentPlayer][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);
    auto& articulations = InstrumentPlayerTestAccess::articulations(fixture.player);
    articulations[0].pedalOpenCloseValue = 85;
    articulations[2].pedalOpenCloseValue = 85;
    InstrumentPlayerTestAccess::configureCachedController(fixture.player, 4, 10, 1);

    fixture.player.handleController(1, 4, 100);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
    REQUIRE_FALSE(fixture.pedalVoice->isVoiceActive());
}

TEST_CASE("InstrumentPlayerVoice - pans mono direct mic into stereo output", "[InstrumentPlayer][routing]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "cow1";
    slot.type = KitLib::InstrumentSpec::Type::Cowbell;
    InstrumentPlayer player { &engine, slot };

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(1, 2);
    buffer->setSample(0, 0, 1.0f);
    buffer->setSample(0, 1, 1.0f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.micBufferChannelStart = 0;
    sound.micBufferChannelCount = 2;
    REQUIRE(InstrumentPlayerTestAccess::configureMicPlacement(player, sound, "dtm", 1, 2, true, 0, 0));

    InstrumentPlayerVoice voice;
    InstrumentPlayerTestAccess::startVoiceForRender(voice, player, sound);

    juce::AudioBuffer<float> output(2, 2);
    output.clear();
    voice.renderNextBlock(output, 0, 2);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.7071067f).margin(0.0001f));
    REQUIRE(output.getSample(1, 0) == Catch::Approx(0.7071067f).margin(0.0001f));
}

TEST_CASE("InstrumentPlayerVoice - negative playback offset advances the source", "[InstrumentPlayer][timing]") {
    InstrumentPlayer player(nullptr, KitLayout::InstrumentSlot {});
    auto buffer = std::make_shared<juce::AudioBuffer<float>>(1, 3);
    buffer->setSample(0, 0, 0.25f);
    buffer->setSample(0, 1, 0.5f);
    buffer->setSample(0, 2, 0.75f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 1000.0;
    sound.sampleLengthSamples = buffer->getNumSamples();
    sound.playbackOffsetMs = -1.0f;

    InstrumentPlayerVoice voice;
    InstrumentPlayerTestAccess::startVoiceForRender(voice, player, sound);

    juce::AudioBuffer<float> output(1, 1);
    output.clear();
    voice.renderNextBlock(output, 0, 1);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.5f).margin(0.0001f));
}

TEST_CASE("InstrumentPlayerVoice - positive playback offset delays the source", "[InstrumentPlayer][timing]") {
    InstrumentPlayer player(nullptr, KitLayout::InstrumentSlot {});
    auto buffer = std::make_shared<juce::AudioBuffer<float>>(1, 3);
    buffer->clear();
    buffer->setSample(0, 0, 1.0f);
    buffer->setSample(0, 1, 1.0f);
    buffer->setSample(0, 2, 1.0f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.sampleLengthSamples = buffer->getNumSamples();
    sound.playbackOffsetMs = 2000.0f / 44100.0f;

    InstrumentPlayerVoice voice;
    InstrumentPlayerTestAccess::startVoiceForRender(voice, player, sound);

    juce::AudioBuffer<float> output(1, 3);
    output.clear();
    voice.renderNextBlock(output, 0, 3);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.0f));
    REQUIRE(output.getSample(0, 1) == Catch::Approx(0.0f));
    REQUIRE(output.getSample(0, 2) == Catch::Approx(1.0f).margin(0.0001f));
}

TEST_CASE("InstrumentPlayerVoice - flips stereo direct mic before manual pan", "[InstrumentPlayer][routing]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "perc1";
    slot.type = KitLib::InstrumentSpec::Type::Cowbell;
    InstrumentPlayer player { &engine, slot };
    player.setFlipLR(true);

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(2, 2);
    buffer->setSample(0, 0, 0.25f);
    buffer->setSample(0, 1, 0.25f);
    buffer->setSample(1, 0, 1.0f);
    buffer->setSample(1, 1, 1.0f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.micBufferChannelStart = 0;
    sound.micBufferChannelCount = 2;
    REQUIRE(InstrumentPlayerTestAccess::configureMicPlacement(player, sound, "dts", 2, 2, true, -127, -127));

    InstrumentPlayerVoice voice;
    InstrumentPlayerTestAccess::startVoiceForRender(voice, player, sound);

    juce::AudioBuffer<float> output(2, 2);
    output.clear();
    voice.renderNextBlock(output, 0, 2);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(1.0f).margin(0.0001f));
    REQUIRE(output.getSample(1, 0) == Catch::Approx(0.0f).margin(0.0001f));
}

TEST_CASE("InstrumentPlayerVoice - room stereo ignores layer pan", "[InstrumentPlayer][routing]") {
    InstrumentPlayer player(nullptr, KitLayout::InstrumentSlot {});
    auto buffer = std::make_shared<juce::AudioBuffer<float>>(2, 1);
    buffer->setSample(0, 0, 0.25f);
    buffer->setSample(1, 0, 0.75f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.micBufferChannelStart = 0;
    sound.micBufferChannelCount = 2;
    REQUIRE(InstrumentPlayerTestAccess::configureMicPlacement(player, sound, "rfs", 2, 2, false, 96, 127));

    InstrumentPlayerVoice voice;
    InstrumentPlayerTestAccess::startVoiceForRender(voice, player, sound);

    juce::AudioBuffer<float> output(2, 1);
    output.clear();
    voice.renderNextBlock(output, 0, 1);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.25f).margin(0.0001f));
    REQUIRE(output.getSample(1, 0) == Catch::Approx(0.75f).margin(0.0001f));
}

TEST_CASE("InstrumentPlayerVoice - overhead stereo uses only manual pan", "[InstrumentPlayer][routing]") {
    InstrumentPlayer player(nullptr, KitLayout::InstrumentSlot {});
    auto buffer = std::make_shared<juce::AudioBuffer<float>>(2, 1);
    buffer->setSample(0, 0, 0.25f);
    buffer->setSample(1, 0, 0.75f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.micBufferChannelStart = 0;
    sound.micBufferChannelCount = 2;
    REQUIRE(InstrumentPlayerTestAccess::configureMicPlacement(player, sound, "ons", 2, 2, false, -127, 127));

    InstrumentPlayerVoice voice;
    InstrumentPlayerTestAccess::startVoiceForRender(voice, player, sound);

    juce::AudioBuffer<float> output(2, 1);
    output.clear();
    voice.renderNextBlock(output, 0, 1);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.0f).margin(0.0001f));
    REQUIRE(output.getSample(1, 0) == Catch::Approx(0.75f).margin(0.0001f));
}

TEST_CASE("InstrumentPlayerVoice - direct mono output ignores layer pan", "[InstrumentPlayer][routing]") {
    InstrumentPlayer player(nullptr, KitLayout::InstrumentSlot {});
    auto buffer = std::make_shared<juce::AudioBuffer<float>>(1, 1);
    buffer->setSample(0, 0, 0.5f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.micBufferChannelStart = 0;
    sound.micBufferChannelCount = 1;
    REQUIRE(InstrumentPlayerTestAccess::configureMicPlacement(player, sound, "dtm", 1, 1, false, -96, 127));

    InstrumentPlayerVoice voice;
    InstrumentPlayerTestAccess::startVoiceForRender(voice, player, sound);

    juce::AudioBuffer<float> output(1, 1);
    output.clear();
    voice.renderNextBlock(output, 0, 1);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.5f).margin(0.0001f));
}

TEST_CASE("InstrumentPlayer - bleed follows the sample mic after routing to a generic output code", "[InstrumentPlayer][routing]") {
    InstrumentPlayer player(nullptr, KitLayout::InstrumentSlot {});
    const auto micSpec = KitLib::MicSpec::parseMicCode("dtm1");
    REQUIRE(micSpec.ok());

    KitLib::SampleSpec sample(nullptr, {}, micSpec.value);
    auto sound = std::make_shared<SampledSound>(nullptr, &sample);
    sound->routedMicCode = "dtm";
    sound->routedMicCodeId = StringsDictSingleton::getId(sound->routedMicCode);
    player.addSound(sound);

    player.setMicBleedLevels({ { "dtm1", 0.25f } });

    REQUIRE(sound->cachedMicBleedGain.load(std::memory_order_relaxed) == Catch::Approx(0.25f));
}

TEST_CASE("InstrumentPlayer - map-layer parameters update only the target articulation", "[InstrumentPlayer][InstrumentMap][routing]") {
    InstrumentPlayer player(nullptr, KitLayout::InstrumentSlot {});
    const auto bellId = StringsDictSingleton::getId("bell");
    const auto bowId = StringsDictSingleton::getId("bow");
    auto& articulations = InstrumentPlayerTestAccess::articulations(player);
    articulations.emplace_back(bellId, 0);
    articulations.back().routeArticulationSlotNameId = bellId;
    articulations.back().velocityLevels.emplace_back();
    articulations.emplace_back(bowId, 0);
    articulations.back().routeArticulationSlotNameId = bowId;
    articulations.back().velocityLevels.emplace_back();

    const auto micSpec = KitLib::MicSpec::parseMicCode("dtm1");
    REQUIRE(micSpec.ok());
    KitLib::SampleSpec sample(nullptr, {}, micSpec.value);
    SampledSound bellSound(nullptr, &sample);
    SampledSound bowSound(nullptr, &sample);
    articulations[0].velocityLevels[0].samples.push_back({ &bellSound, 1, {} });
    articulations[1].velocityLevels[0].samples.push_back({ &bowSound, 1, {} });

    InstrumentPlayer::LayerParameterUpdate update;
    update.tuningCents = 120;
    update.volumeDb = -6.0f;
    update.pan = -40;
    update.flipLR = true;
    update.flipPhase = true;
    update.micBleedLevels = std::unordered_map<juce::String, float> { { "dtm1", 0.25f } };

    REQUIRE(player.updateArticulationParameters({ { bellId, 0 } }, update));
    CHECK(articulations[0].parameters.tuningCents == 120);
    CHECK(articulations[0].parameters.volumeDb == Catch::Approx(-6.0f));
    CHECK(articulations[0].parameters.pan == -40);
    CHECK(articulations[0].parameters.flipLR);
    CHECK(articulations[0].parameters.flipPhase);
    CHECK(bellSound.cachedMicBleedGain.load(std::memory_order_relaxed) == Catch::Approx(0.25f));
    CHECK(articulations[1].parameters.tuningCents == 0);
    CHECK(articulations[1].parameters.volumeDb == Catch::Approx(0.0f));
    CHECK(articulations[1].parameters.pan == 0);
    CHECK_FALSE(articulations[1].parameters.flipLR);
    CHECK_FALSE(articulations[1].parameters.flipPhase);
    CHECK(bowSound.cachedMicBleedGain.load(std::memory_order_relaxed) == Catch::Approx(1.0f));
}

TEST_CASE("InstrumentPlayer - one bleed mic update preserves other mic levels", "[InstrumentPlayer][InstrumentMap][linked-edit]") {
    InstrumentPlayer player(nullptr, KitLayout::InstrumentSlot {});
    const auto centerId = StringsDictSingleton::getId("center");
    auto& articulations = InstrumentPlayerTestAccess::articulations(player);
    articulations.emplace_back(centerId, 0);
    articulations.back().routeArticulationSlotNameId = centerId;
    articulations.back().velocityLevels.emplace_back();

    const auto directMic = KitLib::MicSpec::parseMicCode("dtm");
    const auto overheadMic = KitLib::MicSpec::parseMicCode("ons");
    REQUIRE(directMic.ok());
    REQUIRE(overheadMic.ok());
    KitLib::SampleSpec directSample(nullptr, {}, directMic.value);
    KitLib::SampleSpec overheadSample(nullptr, {}, overheadMic.value);
    SampledSound directSound(nullptr, &directSample);
    SampledSound overheadSound(nullptr, &overheadSample);
    directSound.cachedMicBleedGain.store(0.4f, std::memory_order_relaxed);
    overheadSound.cachedMicBleedGain.store(0.6f, std::memory_order_relaxed);
    articulations[0].velocityLevels[0].samples.push_back({ &directSound, 1, {} });
    articulations[0].velocityLevels[0].samples.push_back({ &overheadSound, 1, {} });

    InstrumentPlayer::LayerParameterUpdate update;
    update.micBleedLevel = std::make_pair(juce::String("dtm"), 0.2f);

    REQUIRE(player.updateArticulationParameters({ { centerId, 0 } }, update));
    CHECK(directSound.cachedMicBleedGain.load(std::memory_order_relaxed) == Catch::Approx(0.2f));
    CHECK(overheadSound.cachedMicBleedGain.load(std::memory_order_relaxed) == Catch::Approx(0.6f));
}

TEST_CASE("InstrumentPlayer - recorded choke articulation becomes a hidden aftertouch target", "[InstrumentPlayer][AftertouchChoke][Loader]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "crash1";
    slot.type = KitLib::InstrumentSpec::Type::Crash;
    InstrumentPlayer player { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makePlaybackInstrumentMeta("crash-library", KitLib::InstrumentSpec::Type::Crash) };
    auto* bowSpec = instrument.getOrAddArticulation("bow");
    auto* chokeSpec = instrument.getOrAddArticulation("choke");
    REQUIRE(bowSpec != nullptr);
    REQUIRE(chokeSpec != nullptr);

    auto sourceSound = std::make_shared<SampledSound>(&instrument, nullptr);
    auto chokeSound = std::make_shared<SampledSound>(&instrument, nullptr);
    player.addSound(sourceSound);
    player.addSound(chokeSound);
    auto& articulations = InstrumentPlayerTestAccess::articulations(player);
    articulations.emplace_back(bowSpec->nameId, bowSpec->exprId);
    articulations.emplace_back(chokeSpec->nameId, chokeSpec->exprId);
    articulations[1].velocityLevels.emplace_back();
    articulations[1].velocityLevels[0].samples.push_back({ chokeSound.get(), 1, chokeSound });

    KitLayout::InstrumentSlot::ArticulationSlot bowArticulationSlot { "bow" };
    bowArticulationSlot.notes.push_back(49);
    InstrumentPlayer::ArticulationPlaybackEntry bowPlaybackEntry { bowArticulationSlot };
    bowPlaybackEntry.libraryInstrument = &instrument;
    bowPlaybackEntry.libraryArticulation = bowSpec;
    InstrumentPlayerTestAccess::loadAftertouchChokeTargets(player, std::move(bowPlaybackEntry));

    CHECK(sourceSound->aftertouchChokeArticulationIndex == 1);
    CHECK_FALSE(sourceSound->isAftertouchChokeTarget);
    CHECK(chokeSound->isAftertouchChokeTarget);
    CHECK(chokeSound->aftertouchChokeArticulationIndex < 0);
}

TEST_CASE("InstrumentPlayer - poly aftertouch starts one coherent multi-mic choke sample transition", "[InstrumentPlayer][AftertouchChoke][ControllerPath]") {
    DrumKitEngine engine;
    engine.engineSettings.chokeMuteTailSec.store(0.015f);
    engine.engineSettings.hihatTransitionSilenceFloorDb.store(-90.0f);
    engine.engineSettings.hihatTailMatchAmount.store(1.0f);
    engine.engineSettings.hihatTailMatchBiasDb.store(-3.0f);
    engine.engineSettings.hihatTransitionMinRemainingSec.store(0.010f);

    KitLayout::InstrumentSlot slot;
    slot.id = "crash1";
    slot.type = KitLib::InstrumentSpec::Type::Crash;
    InstrumentPlayer player { &engine, slot };
    player.setCurrentPlaybackSampleRate(44100.0);

    KitLib::InstrumentSpec instrument { nullptr, makePlaybackInstrumentMeta("crash-library", KitLib::InstrumentSpec::Type::Crash) };
    KitLib::ArticulationSpec bowSpec { &instrument, "bow" };
    KitLib::ArticulationSpec chokeSpec { &instrument, "choke" };
    KitLib::VelocityLevelSpec bowVelocityLevelSpec { &bowSpec, 127 };
    KitLib::VelocityLevelSpec chokeVelocityLevelSpec { &chokeSpec, 127 };
    KitLib::LogicalHitSpec bowLogicalHit { &bowVelocityLevelSpec, 1 };
    KitLib::LogicalHitSpec chokeLogicalHit { &chokeVelocityLevelSpec, 1 };
    const auto directMic = makeDirectMic();
    const auto overheadMic = makeOverheadMic();
    KitLib::SampleSpec bowDirectSample { &bowLogicalHit, {}, directMic };
    KitLib::SampleSpec bowOverheadSample { &bowLogicalHit, {}, overheadMic };
    KitLib::SampleSpec chokeDirectSample { &chokeLogicalHit, {}, directMic };
    KitLib::SampleSpec chokeOverheadSample { &chokeLogicalHit, {}, overheadMic };
    SampledSound bowDirectSound { &instrument, &bowDirectSample };
    SampledSound bowOverheadSound { &instrument, &bowOverheadSample };
    SampledSound chokeDirectSound { &instrument, &chokeDirectSample };
    SampledSound chokeOverheadSound { &instrument, &chokeOverheadSample };

    auto prepareSound = [](SampledSound& sound, int channels, int micBufferChannelStart) {
        sound.sampleRate = 44100.0;
        sound.decayEnvelope = makeTransitionEnvelope();
        sound.buffer = std::make_shared<juce::AudioBuffer<float>>(channels, 88200);
        for (int channelIdx = 0; channelIdx < channels; ++channelIdx)
            for (int sampleIdx = 0; sampleIdx < sound.buffer->getNumSamples(); ++sampleIdx) sound.buffer->setSample(channelIdx, sampleIdx, 0.5f);
        sound.micBufferChannelStart = micBufferChannelStart;
        sound.micBufferChannelCount = channels;
        sound.loaded.store(true);
    };
    prepareSound(bowDirectSound, 1, 0);
    prepareSound(bowOverheadSound, 2, 2);
    prepareSound(chokeDirectSound, 1, 0);
    prepareSound(chokeOverheadSound, 2, 2);
    bowDirectSound.aftertouchChokeArticulationIndex = 1;
    bowOverheadSound.aftertouchChokeArticulationIndex = 1;
    chokeDirectSound.isAftertouchChokeTarget = true;
    chokeOverheadSound.isAftertouchChokeTarget = true;

    auto& articulations = InstrumentPlayerTestAccess::articulations(player);
    articulations.emplace_back(bowSpec.nameId, bowSpec.exprId);
    articulations.emplace_back(chokeSpec.nameId, chokeSpec.exprId);
    articulations[1].velocityLevels.emplace_back();
    articulations[1].velocityToLevel.fill(0);
    articulations[1].velocityLevels[0].samples.push_back({ &chokeDirectSound, 1, {} });
    articulations[1].velocityLevels[0].samples.push_back({ &chokeOverheadSound, 1, {} });
    InstrumentPlayer::LogicalHitRuntime logicalHit;
    logicalHit.roundRobinId = 1;
    logicalHit.sampleEntryIndexes = { 0, 1 };
    logicalHit.readiness->requiredEntryCount = 2;
    logicalHit.readiness->loadedEntryCount.store(2);
    logicalHit.readiness->ready.store(true);
    articulations[1].velocityLevels[0].logicalHits.push_back(std::move(logicalHit));

    auto* bowDirectVoice = player.addVoice(new InstrumentPlayerVoice());
    auto* bowOverheadVoice = player.addVoice(new InstrumentPlayerVoice());
    auto* chokeDirectVoice = player.addVoice(new InstrumentPlayerVoice());
    auto* chokeOverheadVoice = player.addVoice(new InstrumentPlayerVoice());
    player.startVoice(bowDirectVoice, &bowDirectSound, 1, 49, 100.0f / 127.0f);
    player.startVoice(bowOverheadVoice, &bowOverheadSound, 1, 49, 100.0f / 127.0f);
    InstrumentPlayerTestAccess::setVoiceArticulation(*bowDirectVoice, bowSpec);
    InstrumentPlayerTestAccess::setVoiceArticulation(*bowOverheadVoice, bowSpec);
    InstrumentPlayerTestAccess::setVoiceRuntimeArticulation(*bowDirectVoice, 0);
    InstrumentPlayerTestAccess::setVoiceRuntimeArticulation(*bowOverheadVoice, 0);
    bowDirectVoice->setMidiVelocity(100);
    bowOverheadVoice->setMidiVelocity(100);
    bowDirectVoice->sourceSamplePosition = 4410.0;
    bowOverheadVoice->sourceSamplePosition = 4410.0;
    bowDirectVoice->cachedEstimatedDb = -28.0f;
    bowOverheadVoice->cachedEstimatedDb = -28.0f;

    player.handleAftertouch(1, 49, 100);

    CHECK(bowDirectVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    CHECK(bowOverheadVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::FadingOut);
    REQUIRE(chokeDirectVoice->getCurrentlyPlayingSound() == &chokeDirectSound);
    REQUIRE(chokeOverheadVoice->getCurrentlyPlayingSound() == &chokeOverheadSound);
    CHECK(chokeDirectVoice->sourceSamplePosition > chokeDirectSound.decayEnvelope.peakMs * 44.1);
    CHECK(chokeOverheadVoice->sourceSamplePosition > chokeOverheadSound.decayEnvelope.peakMs * 44.1);

    const auto directEventId = chokeDirectVoice->getEventId();
    const auto overheadEventId = chokeOverheadVoice->getEventId();
    player.handleAftertouch(1, 49, 110);
    CHECK(chokeDirectVoice->getEventId() == directEventId);
    CHECK(chokeOverheadVoice->getEventId() == overheadEventId);
    CHECK(chokeDirectVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
    CHECK(chokeOverheadVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
}

TEST_CASE("InstrumentPlayer - unusable choke sample keeps the existing aftertouch fade fallback", "[InstrumentPlayer][AftertouchChoke][ControllerPath]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "crash1";
    slot.type = KitLib::InstrumentSpec::Type::Crash;
    InstrumentPlayer player { &engine, slot };
    player.setCurrentPlaybackSampleRate(44100.0);

    SampledSound sourceSound;
    sourceSound.aftertouchChokeArticulationIndex = 0;
    auto& articulations = InstrumentPlayerTestAccess::articulations(player);
    articulations.emplace_back(StringsDictSingleton::getId("choke"), 0);
    auto* sourceVoice = player.addVoice(new InstrumentPlayerVoice());
    sourceVoice->chokeProcessor.engineSettings = &engine.getEngineSettings();
    sourceVoice->chokeProcessor.prepare(44100.0);
    player.startVoice(sourceVoice, &sourceSound, 1, 49, 0.8f);

    player.handleAftertouch(1, 49, 100);

    CHECK(sourceVoice->getEnvelopeState() == InstrumentPlayerVoice::EnvelopeState::Playing);
    CHECK(sourceVoice->isVoiceActive());
}

TEST_CASE("InstrumentPlayer - library logical hit preview starts all linked mics without advancing round-robin selection", "[InstrumentPlayer][LibraryKitOverridePreview]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "snare1";
    slot.type = KitLib::InstrumentSpec::Type::Snare;
    InstrumentPlayer player { &engine, slot };
    player.setCurrentPlaybackSampleRate(44100.0);

    KitLib::InstrumentSpec instrument { nullptr, makePlaybackInstrumentMeta("snare-library", KitLib::InstrumentSpec::Type::Snare) };
    KitLib::ArticulationSpec libraryArticulation { &instrument, "center" };
    KitLib::VelocityLevelSpec libraryVelocityLevel { &libraryArticulation, 96 };
    KitLib::LogicalHitSpec libraryLogicalHit { &libraryVelocityLevel, 1 };
    auto directMic = makeDirectMic();
    directMic.raw = "dtm";
    auto overheadMic = makeOverheadMic();
    overheadMic.raw = "ons";
    KitLib::SampleSpec directSample { &libraryLogicalHit, {}, directMic };
    KitLib::SampleSpec overheadSample { &libraryLogicalHit, {}, overheadMic };
    auto directSound = std::make_shared<SampledSound>(&instrument, &directSample);
    auto overheadSound = std::make_shared<SampledSound>(&instrument, &overheadSample);
    directSound->sampleRate = 44100.0;
    directSound->buffer = std::make_shared<juce::AudioBuffer<float>>(1, 128);
    directSound->micBufferChannelStart = 0;
    directSound->micBufferChannelCount = 1;
    directSound->loaded.store(true);
    overheadSound->sampleRate = 44100.0;
    overheadSound->buffer = std::make_shared<juce::AudioBuffer<float>>(2, 128);
    overheadSound->micBufferChannelStart = 2;
    overheadSound->micBufferChannelCount = 2;
    overheadSound->loaded.store(true);
    player.addSound(directSound);
    player.addSound(overheadSound);

    auto& articulations = InstrumentPlayerTestAccess::articulations(player);
    articulations.emplace_back(libraryArticulation.nameId, libraryArticulation.exprId);
    articulations[0].velocityLevels.emplace_back();
    auto& runtimeVelocityLevel = articulations[0].velocityLevels[0];
    runtimeVelocityLevel.velocity = 96;
    runtimeVelocityLevel.velocityLevelSpec = &libraryVelocityLevel;
    runtimeVelocityLevel.samples.push_back({ directSound.get(), 1, directSound });
    runtimeVelocityLevel.samples.push_back({ overheadSound.get(), 1, overheadSound });
    InstrumentPlayer::LogicalHitRuntime logicalHit;
    logicalHit.roundRobinId = 1;
    logicalHit.sampleEntryIndexes = { 0, 1 };
    logicalHit.readiness->requiredEntryCount = 2;
    logicalHit.readiness->loadedEntryCount.store(2);
    logicalHit.readiness->ready.store(true);
    runtimeVelocityLevel.logicalHits.push_back(std::move(logicalHit));
    runtimeVelocityLevel.roundRobinCounter->store(7);

    auto* directVoice = player.addVoice(new InstrumentPlayerVoice());
    auto* overheadVoice = player.addVoice(new InstrumentPlayerVoice());

    REQUIRE(player.playLibraryLogicalHitPreview(libraryArticulation, 96, 1, 1, 38, 100));
    CHECK(directVoice->getCurrentlyPlayingSound() == directSound.get());
    CHECK(overheadVoice->getCurrentlyPlayingSound() == overheadSound.get());
    CHECK(runtimeVelocityLevel.roundRobinCounter->load() == 7);
}

} // namespace InstrumentPlayerTests
