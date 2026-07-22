#include "../source/Engine/DrumKitEngine.h"
#include "../source/Engine/InstrumentSynth.h"
#include "catch2/catch_approx.hpp"
#include "catch2/catch_test_macros.hpp"

struct InstrumentSynthTestAccess {
    static std::vector<InstrumentSynth::ArticulationRuntime>& articulations(InstrumentSynth& synth) {
        return synth.runtimeArticulations;
    }

    static void configureController(InstrumentSynth& synth, int controllerNumber, uint8_t lowArticulationIdx, uint8_t highArticulationIdx) {
        synth.ccToArticulationMask[static_cast<size_t>(controllerNumber)] = true;
        auto& mapping = synth.ccToArticulationTable[static_cast<size_t>(controllerNumber)];
        for (size_t controllerValue = 0; controllerValue < mapping.size(); ++controllerValue)
            mapping[controllerValue] = controllerValue < 64 ? lowArticulationIdx : highArticulationIdx;
    }

    static void configureCachedController(InstrumentSynth& synth, int controllerNumber, int controllerValue, InstrumentSynth::ArtRtIdx articulationIdx) {
        synth.lastCcValue[static_cast<size_t>(controllerNumber)] = controllerValue;
        synth.lastCcArticulation[static_cast<size_t>(controllerNumber)] = articulationIdx;
    }

    static void mapRuntimeArticulation(InstrumentSynth& synth, const KitLib::ArticulationSpec& articulationSpec, InstrumentSynth::ArtRtIdx articulationIdx) {
        const uint64_t articulationKey = (static_cast<uint64_t>(articulationSpec.nameId) << 32)
            | static_cast<uint64_t>(articulationSpec.exprId);
        synth.artIdByNameExprId[articulationKey] = articulationIdx;
    }

    static void advanceRenderedSamples(InstrumentSynth& synth, uint64_t sampleCount) {
        synth.renderedSampleCount += sampleCount;
        synth.processPendingHiHatControllers();
    }

    static int cachedArticulation(const InstrumentSynth& synth, int controllerNumber) {
        return synth.lastCcArticulation[static_cast<size_t>(controllerNumber)];
    }
    static juce::String mappedArticulationName(const InstrumentSynth& synth, int controllerNumber, int controllerValue) {
        const auto artIdx = synth.ccToArticulationTable[static_cast<size_t>(controllerNumber)][static_cast<size_t>(controllerValue)];
        return synth.getArtName(artIdx);
    }

    static int runtimePedalOpenCloseValue(const InstrumentSynth& synth, const juce::String& articulationName) {
        return runtimeExpressionValue(synth, articulationName, [](const auto& art) { return art.pedalOpenCloseValue; });
    }

    static int runtimeZoneBowEdgeValue(const InstrumentSynth& synth, const juce::String& articulationName) {
        return runtimeExpressionValue(synth, articulationName, [](const auto& art) { return art.zoneBowEdgeValue; });
    }

    static size_t runtimeLayerCount(const InstrumentSynth& synth, const juce::String& articulationName) {
        for (const auto& art : synth.runtimeArticulations)
            if (art.getCodeRaw() == articulationName) return art.layers.size();
        return 0;
    }

    static void mapNote(InstrumentSynth& synth, int midiNoteNumber, InstrumentSynth::ArtRtIdx articulationIdx) {
        synth.noteToArticulation[static_cast<size_t>(midiNoteNumber)] = articulationIdx;
    }

    static void setVoiceArticulation(InstrumentSynthVoice& voice, const KitLib::ArticulationSpec& articulationSpec) {
        voice.artNameId = articulationSpec.nameId;
        voice.artExprId = articulationSpec.exprId;
    }

    static void setVoiceRuntimeArticulation(InstrumentSynthVoice& voice, InstrumentSynth::ArtRtIdx articulationIdx) {
        voice.artRuntimeIdx = articulationIdx;
    }

    static void setPedalNote(InstrumentSynth& synth, int midiNoteNumber) {
        synth.hihatPedalNote = midiNoteNumber;
    }

    static void startVoiceForRender(InstrumentSynthVoice& voice, InstrumentSynth& owner, SampledSound& sound) {
        voice.setOwner(&owner);
        voice.setCurrentPlaybackSampleRate(44100.0);
        voice.setAttackTimeSec(0.0f);
        voice.currentlyPlayingNote = 60;
        voice.currentPlayingMidiChannel = 1;
        voice.currentlyPlayingSound = &sound;
        voice.startNote(60, 1.0f, &sound);
    }

    static bool configureMicPlacement(
        InstrumentSynth& synth, SampledSound& sound, const juce::String& micCode, int sourceChannelCount,
        int destinationChannelCount, bool routingAllowsPan, std::optional<int> sourceBalance, int layerPan) {
        const auto parsedMic = KitLib::MicSpec::parseMicCode(micCode);
        if (!parsedMic.ok()) return false;
        synth.configureSoundMicPlacement(sound, parsedMic.value, sourceChannelCount, destinationChannelCount, routingAllowsPan, sourceBalance, layerPan);
        return true;
    }

private:
    template <typename Getter>
    static int runtimeExpressionValue(const InstrumentSynth& synth, const juce::String& articulationName, Getter getter) {
        for (const auto& art : synth.runtimeArticulations)
            if (art.getCodeRaw() == articulationName)
                return getter(art);
        return -1;
    }
};

namespace {

KitLayout::InstrumentSlot makeHiHatSlot() {
    KitLayout::InstrumentSlot slot;
    slot.type = KitLib::InstrumentSpec::Type::HiHat;
    KitLayout::InstrumentSlot::ArticulationTrigger pedalTrigger { "pedal" };
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

    KitLayout::InstrumentSlot::ArticulationTrigger edgeOpen { "edge[open]" };
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

    KitLayout::InstrumentSlot::ArticulationTrigger bowOpen { "bow[open]" };
    bowOpen.notes.push_back(46);
    slot.articulations.push_back(std::move(bowOpen));

    KitLayout::InstrumentSlot::ArticulationTrigger bowTight { "bow[tight]" };
    bowTight.notes.push_back(42);
    slot.articulations.push_back(std::move(bowTight));

    KitLayout::InstrumentSlot::ContinuousController zoneController { 19, "zon_bow_edg" };
    zoneController.notes.push_back(42);
    zoneController.notes.push_back(46);
    slot.controllers.push_back(std::move(zoneController));
    return slot;
}

KitLayout::InstrumentSlot makeImplicitBowEdgeExpressionSlot() {
    KitLayout::InstrumentSlot slot;
    slot.id = "ride1";
    slot.type = KitLib::InstrumentSpec::Type::Ride;

    KitLayout::InstrumentSlot::ArticulationTrigger bow { "bow" };
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

    KitLayout::InstrumentSlot::ArticulationTrigger bowTrigger { "bow" };
    bowTrigger.notes.push_back(39);
    slot.articulations.push_back(std::move(bowTrigger));

    KitLayout::InstrumentSlot::ArticulationTrigger edgeTrigger { "edge" };
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

KitLib::InstrumentSpec::MetaInfo makeRouteInstrumentMeta(const juce::String& codeRaw, KitLib::InstrumentSpec::Type type) {
    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = codeRaw;
    meta.type = type;
    return meta;
}

void addSlotArticulation(KitLayout::InstrumentSlot& slot, const juce::String& codeRaw, uint8_t midiNote) {
    KitLayout::InstrumentSlot::ArticulationTrigger trigger { codeRaw };
    trigger.notes.push_back(midiNote);
    slot.articulations.push_back(std::move(trigger));
}

bool hasNormalRoute(const std::vector<InstrumentSynth::ResolvedArticulationRoute>& routes, const juce::String& destinationCodeRaw, const juce::String& sourceCodeRaw) {
    for (const auto& route : routes) {
        if (route.isAliasRoute || route.isControllerSibling) continue;
        if (route.sourceArticulation == nullptr) continue;
        if (route.destinationLayoutArt.codeRaw == destinationCodeRaw && route.sourceArticulation->codeRaw == sourceCodeRaw) return true;
    }
    return false;
}

bool ownsDestinationRoute(const std::vector<InstrumentSynth::ResolvedArticulationRoute>& routes, const juce::String& destinationCodeRaw) {
    for (const auto& route : routes) {
        if (route.isAliasRoute || route.isControllerSibling) continue;
        if (route.destinationLayoutArt.codeRaw == destinationCodeRaw) return true;
    }
    return false;
}

bool hasControllerSiblingRoute(const std::vector<InstrumentSynth::ResolvedArticulationRoute>& routes, const juce::String& destinationCodeRaw, const juce::String& sourceCodeRaw) {
    for (const auto& route : routes) {
        if (!route.isControllerSibling) continue;
        if (route.sourceArticulation == nullptr) continue;
        if (route.destinationLayoutArt.codeRaw == destinationCodeRaw && route.sourceArticulation->codeRaw == sourceCodeRaw) return true;
    }
    return false;
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
    InstrumentSynth synth { &this->engine, this->slot };
    KitLib::InstrumentSpec::MetaInfo instrumentMeta = makeInstrumentMeta();
    KitLib::InstrumentSpec instrumentSpec { nullptr, this->instrumentMeta };
    KitLib::ArticulationSpec tightSpec { &this->instrumentSpec, "edge[tight]" };
    KitLib::ArticulationSpec openSpec { &this->instrumentSpec, "edge[open]" };
    KitLib::ArticulationSpec pedalSpec { &this->instrumentSpec, "pedal" };
    KitLib::VelocityLayerSpec tightLayerSpec { &this->tightSpec, 127 };
    KitLib::VelocityLayerSpec openLayerSpec { &this->openSpec, 127 };
    KitLib::VelocityLayerSpec pedalLayerSpec { &this->pedalSpec, 127 };
    KitLib::MicSpec directMic = makeDirectMic();
    KitLib::SampleVariantSpec tightVariant { &this->tightLayerSpec, {}, 'a', this->directMic };
    KitLib::SampleVariantSpec openVariant { &this->openLayerSpec, {}, 'a', this->directMic };
    KitLib::SampleVariantSpec pedalVariant { &this->pedalLayerSpec, {}, 'a', this->directMic };
    SampledSound tightSound { nullptr, &this->tightVariant };
    SampledSound openSound { nullptr, &this->openVariant };
    SampledSound pedalSound { nullptr, &this->pedalVariant };
    InstrumentSynthVoice* tightVoice = nullptr;
    InstrumentSynthVoice* openVoice = nullptr;
    InstrumentSynthVoice* pedalVoice = nullptr;

    explicit ControllerTransitionFixture(bool includeOpenTarget = true) {
        this->engine.engineSettings.hihatControllerSmoothingSec.store(0.0f);
        this->engine.engineSettings.hihatOpenCrossfadeSec.store(0.080f);
        this->engine.engineSettings.hihatTransitionAttackGuardSec.store(0.0f);
        this->engine.engineSettings.hihatTransitionMinRemainingSec.store(0.01f);
        this->engine.engineSettings.hihatTransitionSilenceFloorDb.store(-90.0f);
        this->engine.engineSettings.hihatTailMatchAmount.store(0.0f);
        this->synth.setCurrentPlaybackSampleRate(44100.0);

        auto& articulations = InstrumentSynthTestAccess::articulations(this->synth);
        articulations.emplace_back(this->tightSpec.nameId, this->tightSpec.exprId);
        articulations.back().pedalOpenCloseValue = 90;
        articulations.back().layers.emplace_back();
        articulations.back().velocityToLayer.fill(0);
        articulations.emplace_back(this->openSpec.nameId, this->openSpec.exprId);
        articulations.back().pedalOpenCloseValue = 10;
        articulations.back().layers.emplace_back();
        articulations.back().velocityToLayer.fill(0);
        articulations.emplace_back(this->pedalSpec.nameId, this->pedalSpec.exprId);
        articulations.back().pedalOpenCloseValue = 90;
        articulations.back().layers.emplace_back();
        articulations.back().velocityToLayer.fill(0);

        InstrumentSynthTestAccess::mapRuntimeArticulation(this->synth, this->tightSpec, 0);
        InstrumentSynthTestAccess::mapRuntimeArticulation(this->synth, this->openSpec, 1);
        InstrumentSynthTestAccess::mapRuntimeArticulation(this->synth, this->pedalSpec, 2);
        InstrumentSynthTestAccess::mapNote(this->synth, 44, 2);
        InstrumentSynthTestAccess::setPedalNote(this->synth, 44);
        InstrumentSynthTestAccess::configureController(this->synth, 4, 1, 0);
        InstrumentSynthTestAccess::configureCachedController(this->synth, 4, 100, 0);

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
        articulations[0].layers[0].samples.push_back({ &this->tightSound, 1, {} });
        if (includeOpenTarget)
            articulations[1].layers[0].samples.push_back({ &this->openSound, 1, {} });
        articulations[2].layers[0].samples.push_back({ &this->pedalSound, 1, {} });
        for (auto& articulation : articulations) {
            auto& layer = articulation.layers[0];
            if (layer.samples.empty()) continue;
            InstrumentSynth::LogicalHitRuntime logicalHit;
            logicalHit.variantIdx = 1;
            logicalHit.sampleEntryIndexes.push_back(0);
            logicalHit.readiness->requiredEntryCount = 1;
            logicalHit.readiness->loadedEntryCount.store(1);
            logicalHit.readiness->ready.store(true);
            layer.logicalHits.push_back(std::move(logicalHit));
        }

        this->tightVoice = this->synth.addVoice(new InstrumentSynthVoice());
        this->openVoice = this->synth.addVoice(new InstrumentSynthVoice());
        this->pedalVoice = this->synth.addVoice(new InstrumentSynthVoice());
        this->synth.startVoice(this->tightVoice, &this->tightSound, 1, 42, 0.8f);
        InstrumentSynthTestAccess::setVoiceArticulation(*this->tightVoice, this->tightSpec);
        this->tightVoice->sourceSamplePosition = 4410.0;
        this->tightVoice->cachedEstimatedDb = -24.0f;
    }
};

} // namespace

TEST_CASE("InstrumentSynth - voice starts receive independent event ids", "[InstrumentSynth]") {
    InstrumentSynth synth(nullptr, KitLayout::InstrumentSlot {});
    synth.setCurrentPlaybackSampleRate(44100.0);

    auto* firstVoice = synth.addVoice(new InstrumentSynthVoice());
    auto* secondVoice = synth.addVoice(new InstrumentSynthVoice());
    SampledSound firstSound;
    SampledSound secondSound;

    synth.startVoice(firstVoice, &firstSound, 1, 42, 0.8f);
    synth.startVoice(secondVoice, &secondSound, 1, 42, 0.8f);

    REQUIRE(firstVoice->getEventId() != 0);
    REQUIRE(secondVoice->getEventId() != 0);
    REQUIRE(firstVoice->getEventId() != secondVoice->getEventId());
}

TEST_CASE("InstrumentSynth - simple empty articulation aliases to first mapped layer", "[InstrumentSynth][InstrumentMap]") {
    DrumKitEngine engine;
    const auto slot = makeSimpleStackSlot();
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeStackInstrumentMeta("stack-source") };
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentSynth::ArticulationNameRemap remap;
    remap[bowId] = edgeId;
    InstrumentSynth::ArticulationAliasMap aliases;
    aliases[edgeId] = { edgeId, 0 };

    synth.loadInstrumentWithSources(&instrument, {}, { bowId }, {}, remap, {}, aliases);

    const int bowArtIdx = synth.resolveArticulation(39);
    const int edgeArtIdx = synth.resolveArticulation(15);
    REQUIRE(bowArtIdx >= 0);
    REQUIRE(edgeArtIdx == bowArtIdx);
    CHECK(synth.getArtName(edgeArtIdx) == "edge");
}

TEST_CASE("InstrumentSynth - explicit articulation does not inherit simple fallback alias", "[InstrumentSynth][InstrumentMap]") {
    DrumKitEngine engine;
    const auto slot = makeSimpleStackSlot();
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeStackInstrumentMeta("stack-source") };
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentSynth::ArticulationNameRemap remap;
    remap[bowId] = edgeId;

    synth.loadInstrumentWithSources(&instrument, {}, { bowId }, {}, remap, {}, {});

    REQUIRE(synth.resolveArticulation(39) >= 0);
    CHECK(synth.resolveArticulation(15) < 0);
}

TEST_CASE("InstrumentSynth - simple articulation aliases survive routing rebuild", "[InstrumentSynth][InstrumentMap]") {
    DrumKitEngine engine;
    const auto slot = makeSimpleStackSlot();
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeStackInstrumentMeta("stack-source") };
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentSynth::ArticulationNameRemap remap;
    remap[bowId] = edgeId;
    InstrumentSynth::ArticulationAliasMap aliases;
    aliases[edgeId] = { edgeId, 0 };

    synth.loadInstrumentWithSources(&instrument, {}, { bowId }, {}, remap, {}, aliases);
    synth.rebuildRoutingFromLayout(slot);

    const int bowArtIdx = synth.resolveArticulation(39);
    const int edgeArtIdx = synth.resolveArticulation(15);
    REQUIRE(bowArtIdx >= 0);
    REQUIRE(edgeArtIdx == bowArtIdx);
}

TEST_CASE("InstrumentSynth - route list keeps all primary destination ids with combined filters", "[InstrumentSynth][InstrumentMap][CurrentRouting]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "crash1";
    slot.type = KitLib::InstrumentSpec::Type::Crash;
    addSlotArticulation(slot, "bow", 25);
    addSlotArticulation(slot, "edge", 49);
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeRouteInstrumentMeta("crash-source", KitLib::InstrumentSpec::Type::Crash) };
    instrument.getOrAddArticulation("bow");
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentSynth::RoutingContext routingContext;
    routingContext.defaultInstrument = &instrument;
    routingContext.allowedArticulationNameIds = { bowId, edgeId };
    routingContext.allowedArticulationFilters = { "bow", "edge" };
    routingContext.strictArticulationResolution = true;

    const auto routes = synth.buildResolvedRouteList(slot, routingContext);

    CHECK(hasNormalRoute(routes, "bow", "bow"));
    CHECK(hasNormalRoute(routes, "edge", "edge"));
}

TEST_CASE("InstrumentSynth - route list keeps destination rim while source is center", "[InstrumentSynth][InstrumentMap][CurrentRouting]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "snare1";
    slot.type = KitLib::InstrumentSpec::Type::Snare;
    addSlotArticulation(slot, "center", 38);
    addSlotArticulation(slot, "rim", 40);
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeRouteInstrumentMeta("snare-source", KitLib::InstrumentSpec::Type::Snare) };
    instrument.getOrAddArticulation("center");

    const auto centerId = StringsDictSingleton::getId("center");
    const auto rimId = StringsDictSingleton::getId("rim");
    InstrumentSynth::RoutingContext routingContext;
    routingContext.defaultInstrument = &instrument;
    routingContext.allowedArticulationNameIds = { centerId, rimId };
    routingContext.allowedArticulationFilters = { "center", "rim" };
    routingContext.artNameRemap[rimId] = centerId;
    routingContext.strictArticulationResolution = true;

    const auto routes = synth.buildResolvedRouteList(slot, routingContext);

    CHECK(hasNormalRoute(routes, "center", "center"));
    CHECK(hasNormalRoute(routes, "rim", "center"));
}

TEST_CASE("InstrumentSynth - hihat route list keeps bow edge and pedal rows with combined filters", "[InstrumentSynth][InstrumentMap][ExpressionPoints][CurrentRouting]") {
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
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeRouteInstrumentMeta("hihat-source", KitLib::InstrumentSpec::Type::HiHat) };
    instrument.getOrAddArticulation("edge[open]");
    instrument.getOrAddArticulation("edge[half]");
    instrument.getOrAddArticulation("edge[close]");
    instrument.getOrAddArticulation("edge[tight]");
    instrument.getOrAddArticulation("pedal");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    const auto pedalId = StringsDictSingleton::getId("pedal");
    InstrumentSynth::RoutingContext routingContext;
    routingContext.defaultInstrument = &instrument;
    routingContext.allowedArticulationNameIds = { bowId, edgeId, pedalId };
    routingContext.allowedArticulationFilters = { "bow", "edge", "pedal" };
    routingContext.artNameRemap[bowId] = edgeId;
    routingContext.strictArticulationResolution = true;

    const auto routes = synth.buildResolvedRouteList(slot, routingContext);

    CHECK(hasNormalRoute(routes, "bow[open]", "edge[open]"));
    CHECK(hasNormalRoute(routes, "edge[open]", "edge[open]"));
    CHECK(hasNormalRoute(routes, "pedal", "pedal"));
    CHECK(hasControllerSiblingRoute(routes, "bow[open]", "edge[half]"));
    CHECK(ownsDestinationRoute(routes, "edge[tight]"));
}

TEST_CASE("InstrumentSynth - explicit cymbal routes sharing edge source stay separate", "[InstrumentSynth][InstrumentMap][CurrentRouting]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "china1";
    slot.type = KitLib::InstrumentSpec::Type::China;
    addSlotArticulation(slot, "bow", 25);
    addSlotArticulation(slot, "edge", 49);
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeRouteInstrumentMeta("china-source", KitLib::InstrumentSpec::Type::China) };
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentSynth::RoutingContext routingContext;
    routingContext.defaultInstrument = &instrument;
    routingContext.allowedArticulationNameIds = { bowId, edgeId };
    routingContext.allowedArticulationFilters = { "bow", "edge" };
    routingContext.artNameRemap[bowId] = edgeId;
    routingContext.strictArticulationResolution = true;

    auto routes = synth.buildResolvedRouteList(slot, routingContext);
    synth.loadInstrumentWithRouteList(&instrument, routingContext, std::move(routes));

    const int bowArtIdx = synth.resolveArticulation(25);
    const int edgeArtIdx = synth.resolveArticulation(49);
    REQUIRE(bowArtIdx >= 0);
    REQUIRE(edgeArtIdx >= 0);
    CHECK(bowArtIdx != edgeArtIdx);
    CHECK(synth.getArtName(bowArtIdx) == "edge");
    CHECK(synth.getArtName(edgeArtIdx) == "edge");
}

TEST_CASE("InstrumentSynth - explicit rim route sharing center source stays separate", "[InstrumentSynth][InstrumentMap][CurrentRouting]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "tom2";
    slot.type = KitLib::InstrumentSpec::Type::Tom;
    addSlotArticulation(slot, "center", 45);
    addSlotArticulation(slot, "rim", 47);
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeRouteInstrumentMeta("tom-source", KitLib::InstrumentSpec::Type::Tom) };
    instrument.getOrAddArticulation("center");

    const auto centerId = StringsDictSingleton::getId("center");
    const auto rimId = StringsDictSingleton::getId("rim");
    InstrumentSynth::RoutingContext routingContext;
    routingContext.defaultInstrument = &instrument;
    routingContext.allowedArticulationNameIds = { centerId, rimId };
    routingContext.allowedArticulationFilters = { "center", "rim" };
    routingContext.artNameRemap[rimId] = centerId;
    routingContext.strictArticulationResolution = true;

    auto routes = synth.buildResolvedRouteList(slot, routingContext);
    synth.loadInstrumentWithRouteList(&instrument, routingContext, std::move(routes));

    const int centerArtIdx = synth.resolveArticulation(45);
    const int rimArtIdx = synth.resolveArticulation(47);
    REQUIRE(centerArtIdx >= 0);
    REQUIRE(rimArtIdx >= 0);
    CHECK(centerArtIdx != rimArtIdx);
    CHECK(synth.getArtName(centerArtIdx) == "center");
    CHECK(synth.getArtName(rimArtIdx) == "center");
}

TEST_CASE("InstrumentSynth - implicit pdl_opn_cls points route simplified hi-hat articulations", "[InstrumentSynth][ExpressionPoints]") {
    DrumKitEngine engine;
    const auto slot = makeImplicitHiHatExpressionSlot();
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    instrument.getOrAddArticulation("edge[open]");
    instrument.getOrAddArticulation("edge[half]");
    instrument.getOrAddArticulation("edge[close]");
    instrument.getOrAddArticulation("edge[tight]");

    const auto edgeId = StringsDictSingleton::getId("edge");
    synth.loadInstrumentWithSources(&instrument, {}, { edgeId });

    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 10) == "edge[open]");
    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 40) == "edge[half]");
    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 70) == "edge[close]");
    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 90) == "edge[tight]");
}

TEST_CASE("InstrumentSynth - slot expression points override implicit pdl_opn_cls defaults", "[InstrumentSynth][ExpressionPoints]") {
    DrumKitEngine engine;
    auto slot = makeImplicitHiHatExpressionSlot();
    slot.expressionPoints["pdl_opn_cls"]["open"] = 10;
    slot.expressionPoints["pdl_opn_cls"]["half"] = 20;
    slot.expressionPoints["pdl_opn_cls"]["close"] = 30;
    slot.expressionPoints["pdl_opn_cls"]["tight"] = 40;

    InstrumentSynth synth { &engine, slot };
    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    instrument.getOrAddArticulation("edge[open]");
    instrument.getOrAddArticulation("edge[half]");
    instrument.getOrAddArticulation("edge[close]");
    instrument.getOrAddArticulation("edge[tight]");

    const auto edgeId = StringsDictSingleton::getId("edge");
    synth.loadInstrumentWithSources(&instrument, {}, { edgeId });

    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 15) == "edge[half]");
    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 35) == "edge[tight]");
}

TEST_CASE("InstrumentSynth - routing rebuild refreshes implicit pdl_opn_cls runtime values", "[InstrumentSynth][ExpressionPoints]") {
    DrumKitEngine engine;
    auto slot = makeImplicitHiHatExpressionSlot();
    InstrumentSynth synth { &engine, slot };
    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    auto* open = instrument.getOrAddArticulation("edge[open]");
    instrument.getOrAddArticulation("edge[half]");
    instrument.getOrAddArticulation("edge[close]");
    auto* tight = instrument.getOrAddArticulation("edge[tight]");
    open->getOrAddVelocityLayer(127);
    tight->getOrAddVelocityLayer(127);

    const auto edgeId = StringsDictSingleton::getId("edge");
    synth.loadInstrumentWithSources(&instrument, {}, { edgeId });
    REQUIRE(InstrumentSynthTestAccess::runtimeLayerCount(synth, "edge[open]") == 1);
    REQUIRE(InstrumentSynthTestAccess::runtimeLayerCount(synth, "edge[tight]") == 1);

    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 90) == "edge[tight]");
    CHECK(InstrumentSynthTestAccess::runtimePedalOpenCloseValue(synth, "edge[tight]") == 85);

    slot.expressionPoints["pdl_opn_cls"]["open"] = 10;
    slot.expressionPoints["pdl_opn_cls"]["half"] = 20;
    slot.expressionPoints["pdl_opn_cls"]["close"] = 30;
    slot.expressionPoints["pdl_opn_cls"]["tight"] = 40;
    synth.rebuildRoutingFromLayout(slot);

    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 35) == "edge[tight]");
    CHECK(InstrumentSynthTestAccess::runtimePedalOpenCloseValue(synth, "edge[open]") == 10);
    CHECK(InstrumentSynthTestAccess::runtimePedalOpenCloseValue(synth, "edge[tight]") == 40);
    CHECK(InstrumentSynthTestAccess::runtimeLayerCount(synth, "edge[open]") == 1);
    CHECK(InstrumentSynthTestAccess::runtimeLayerCount(synth, "edge[tight]") == 1);
}
TEST_CASE("InstrumentSynth - explicit expression folders override layout expression points", "[InstrumentSynth][ExpressionPoints]") {
    DrumKitEngine engine;
    auto slot = makeImplicitHiHatExpressionSlot();
    slot.expressionPoints["pdl_opn_cls"]["open"] = 50;

    InstrumentSynth synth { &engine, slot };
    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    auto* open = instrument.getOrAddArticulation("edge[open]");
    open->addExpressionDimensionValue("pdl_opn_cls", 10);
    instrument.getOrAddArticulation("edge[half]");

    const auto edgeId = StringsDictSingleton::getId("edge");
    synth.loadInstrumentWithSources(&instrument, {}, { edgeId });

    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 10) == "edge[open]");
    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 11) == "edge[half]");
}

TEST_CASE("InstrumentSynth - explicit expression folders override layout expression points after rebuild", "[InstrumentSynth][ExpressionPoints]") {
    DrumKitEngine engine;
    auto slot = makeImplicitHiHatExpressionSlot();

    InstrumentSynth synth { &engine, slot };
    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    auto* open = instrument.getOrAddArticulation("edge[open]");
    open->addExpressionDimensionValue("pdl_opn_cls", 10);
    instrument.getOrAddArticulation("edge[half]");

    const auto edgeId = StringsDictSingleton::getId("edge");
    synth.loadInstrumentWithSources(&instrument, {}, { edgeId });

    slot.expressionPoints["pdl_opn_cls"]["open"] = 80;
    synth.rebuildRoutingFromLayout(slot);

    CHECK(InstrumentSynthTestAccess::runtimePedalOpenCloseValue(synth, "edge[open]") == 10);
    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 10) == "edge[open]");
    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 4, 11) == "edge[half]");
}
TEST_CASE("InstrumentSynth - implicit zon_bow_edg points route bow and edge on one cymbal note", "[InstrumentSynth][ExpressionPoints]") {
    DrumKitEngine engine;
    const auto slot = makeImplicitBowEdgeExpressionSlot();
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = "ride-test";
    meta.type = KitLib::InstrumentSpec::Type::Ride;
    KitLib::InstrumentSpec instrument { nullptr, meta };
    instrument.getOrAddArticulation("bow");
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    synth.loadInstrumentWithSources(&instrument, {}, { bowId });

    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 17, 10) == "bow");
    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 17, 90) == "edge");
}

TEST_CASE("InstrumentSynth - routing rebuild refreshes implicit zon_bow_edg runtime values", "[InstrumentSynth][ExpressionPoints]") {
    DrumKitEngine engine;
    auto slot = makeImplicitBowEdgeExpressionSlot();
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec::MetaInfo meta;
    meta.codeRaw = "ride-test";
    meta.type = KitLib::InstrumentSpec::Type::Ride;
    KitLib::InstrumentSpec instrument { nullptr, meta };
    instrument.getOrAddArticulation("bow");
    instrument.getOrAddArticulation("edge");

    const auto bowId = StringsDictSingleton::getId("bow");
    synth.loadInstrumentWithSources(&instrument, {}, { bowId });

    CHECK(InstrumentSynthTestAccess::runtimeZoneBowEdgeValue(synth, "bow") == 24);
    CHECK(InstrumentSynthTestAccess::runtimeZoneBowEdgeValue(synth, "edge") == 85);

    slot.expressionPoints["zon_bow_edg"]["bow"] = 5;
    slot.expressionPoints["zon_bow_edg"]["edge"] = 100;
    synth.rebuildRoutingFromLayout(slot);

    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 17, 4) == "bow");
    CHECK(InstrumentSynthTestAccess::mappedArticulationName(synth, 17, 90) == "edge");
    CHECK(InstrumentSynthTestAccess::runtimeZoneBowEdgeValue(synth, "bow") == 5);
    CHECK(InstrumentSynthTestAccess::runtimeZoneBowEdgeValue(synth, "edge") == 100);
}
TEST_CASE("InstrumentSynth - zone controller siblings preserve hihat note expression", "[InstrumentSynth][ExpressionPoints][InstrumentMap]") {
    DrumKitEngine engine;
    const auto slot = makeHiHatZoneRemapSlot();
    InstrumentSynth synth { &engine, slot };

    KitLib::InstrumentSpec instrument { nullptr, makeInstrumentMeta() };
    instrument.getOrAddArticulation("edge[open]");
    instrument.getOrAddArticulation("edge[tight]");

    const auto bowId = StringsDictSingleton::getId("bow");
    const auto edgeId = StringsDictSingleton::getId("edge");
    InstrumentSynth::ArticulationNameRemap remap;
    remap[bowId] = edgeId;
    synth.loadInstrumentWithSources(&instrument, {}, { bowId }, {}, remap);

    const int openIdx = synth.resolveArticulation(46);
    const int tightIdx = synth.resolveArticulation(42);
    REQUIRE(openIdx >= 0);
    REQUIRE(tightIdx >= 0);
    CHECK(synth.getArtName(openIdx) == "edge[open]");
    CHECK(synth.getArtName(tightIdx) == "edge[tight]");
}

TEST_CASE("InstrumentSynth - non-pedal hihat controller does not trigger live transition", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    InstrumentSynthTestAccess::configureController(fixture.synth, 19, 1, 0);

    fixture.synth.handleController(1, 19, 10);

    const auto counters = fixture.synth.getHiHatTransitionCounters();
    CHECK(counters.processed == 0);
    CHECK(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
}
TEST_CASE("InstrumentSynth - controller uses accepted per-voice tail replacement", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;

    fixture.synth.handleController(1, 4, 10);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.tightVoice->adsr.getParameters().release == Catch::Approx(0.080f));
    REQUIRE(fixture.openVoice->getCurrentlyPlayingSound() == &fixture.openSound);
    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE(fixture.openVoice->sourceSamplePosition == Catch::Approx(4410.0));
    const auto counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.completed == 1);
    REQUIRE(counters.skipped == 0);
}

TEST_CASE("InstrumentSynth - unavailable target leaves the ringing voice unchanged", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture(false);

    fixture.synth.handleController(1, 4, 10);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE_FALSE(fixture.openVoice->isVoiceActive());
    REQUIRE(InstrumentSynthTestAccess::cachedArticulation(fixture.synth, 4) == 0);
    const auto counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.completed == 0);
    REQUIRE(counters.skipped == 1);
}

TEST_CASE("InstrumentSynth - controller smoothing retains the latest value", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    DrumKitEngine engine;
    engine.engineSettings.hihatControllerSmoothingSec.store(0.010f);

    const auto slot = makeHiHatSlot();
    InstrumentSynth synth(&engine, slot);
    synth.setCurrentPlaybackSampleRate(1000.0);
    auto& articulations = InstrumentSynthTestAccess::articulations(synth);
    articulations.emplace_back(1, 1);
    articulations.back().pedalOpenCloseValue = 10;
    articulations.emplace_back(1, 2);
    articulations.back().pedalOpenCloseValue = 90;
    InstrumentSynthTestAccess::configureController(synth, 4, 0, 1);

    synth.handleController(1, 4, 10);
    synth.handleController(1, 4, 80);
    synth.handleController(1, 4, 100);

    auto counters = synth.getHiHatTransitionCounters();
    REQUIRE(counters.processed == 1);
    REQUIRE(counters.coalesced == 2);

    InstrumentSynthTestAccess::advanceRenderedSamples(synth, 10);

    counters = synth.getHiHatTransitionCounters();
    REQUIRE(counters.processed == 2);
    REQUIRE(counters.coalesced == 2);
}

TEST_CASE("InstrumentSynth - attack guard retries a stopped slow-close controller move", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
    fixture.engine.engineSettings.hihatControllerSmoothingSec.store(0.020f);
    fixture.engine.engineSettings.hihatTransitionAttackGuardSec.store(0.094f);
    fixture.tightVoice->killImmediately();
    fixture.synth.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.8f);
    fixture.openVoice->sourceSamplePosition = 0.0;
    fixture.openVoice->cachedEstimatedDb = -24.0f;
    InstrumentSynthTestAccess::configureCachedController(fixture.synth, 4, 10, 1);

    fixture.synth.handleController(1, 4, 100);

    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE_FALSE(fixture.tightVoice->isVoiceActive());
    auto counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 0);

    fixture.openVoice->sourceSamplePosition = 4410.0;
    InstrumentSynthTestAccess::advanceRenderedSamples(fixture.synth, 882);

    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.tightVoice->getCurrentlyPlayingSound() == &fixture.tightSound);
    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 1);
}

TEST_CASE("InstrumentSynth - voice pressure retries after a replacement voice becomes free", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
    fixture.engine.engineSettings.hihatControllerSmoothingSec.store(0.020f);
    fixture.synth.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.5f);
    fixture.synth.startVoice(fixture.pedalVoice, &fixture.openSound, 1, 42, 0.5f);

    fixture.synth.handleController(1, 4, 10);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    auto counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 0);

    fixture.openVoice->killImmediately();
    InstrumentSynthTestAccess::advanceRenderedSamples(fixture.synth, 882);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.openVoice->getCurrentlyPlayingSound() == &fixture.openSound);
    counters = fixture.synth.getHiHatTransitionCounters();
    REQUIRE(counters.skipped == 1);
    REQUIRE(counters.completed == 1);
}

TEST_CASE("InstrumentSynth - audible open tail creates a level-matched pedal event and preserves tight voices", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);
    fixture.engine.engineSettings.hihatTransitionAttackGuardSec.store(0.0f);
    auto& articulations = InstrumentSynthTestAccess::articulations(fixture.synth);
    articulations[0].pedalOpenCloseValue = 85;
    articulations[2].pedalOpenCloseValue = 85;
    articulations[0].layers[0].samples.clear();
    fixture.synth.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.8f);
    InstrumentSynthTestAccess::setVoiceArticulation(*fixture.openVoice, fixture.openSpec);
    fixture.openVoice->sourceSamplePosition = 4410.0;
    fixture.openVoice->cachedEstimatedDb = -24.0f;
    InstrumentSynthTestAccess::configureCachedController(fixture.synth, 4, 10, 1);

    fixture.synth.handleController(1, 4, 100);

    REQUIRE(fixture.pedalVoice->getCurrentlyPlayingSound() == &fixture.pedalSound);
    REQUIRE(fixture.pedalVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE(fixture.openVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE(fixture.pedalVoice->getVelocityGain() == Catch::Approx(80.0f / 127.0f));

    const auto pedalEventId = fixture.pedalVoice->getEventId();
    fixture.synth.handleController(1, 4, 100);
    REQUIRE(fixture.pedalVoice->getEventId() == pedalEventId);
}

TEST_CASE("InstrumentSynth - hihat note choke uses exact runtime articulation when source names repeat", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.tightVoice->killImmediately();

    auto& articulations = InstrumentSynthTestAccess::articulations(fixture.synth);
    InstrumentSynth::ArticulationRuntime plainOpen { fixture.openSpec.nameId, fixture.openSpec.exprId };
    plainOpen.pedalOpenCloseValue = -1;
    articulations.insert(articulations.begin(), std::move(plainOpen));
    InstrumentSynthTestAccess::mapNote(fixture.synth, 44, 3);

    fixture.synth.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.8f);
    InstrumentSynthTestAccess::setVoiceArticulation(*fixture.openVoice, fixture.openSpec);
    InstrumentSynthTestAccess::setVoiceRuntimeArticulation(*fixture.openVoice, 2);
    fixture.openVoice->sourceSamplePosition = 4410.0;
    fixture.openVoice->cachedEstimatedDb = -24.0f;

    REQUIRE(fixture.synth.playNote(1, 44, 100));

    const bool startedPedalSound = fixture.tightVoice->getCurrentlyPlayingSound() == &fixture.pedalSound
        || fixture.pedalVoice->getCurrentlyPlayingSound() == &fixture.pedalSound;
    CHECK(fixture.openVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    CHECK(startedPedalSound);
}

TEST_CASE("InstrumentSynth - hihat controller close uses exact runtime articulation when source names repeat", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::Off);
    fixture.tightVoice->killImmediately();

    auto& articulations = InstrumentSynthTestAccess::articulations(fixture.synth);
    InstrumentSynth::ArticulationRuntime plainOpen { fixture.openSpec.nameId, fixture.openSpec.exprId };
    plainOpen.pedalOpenCloseValue = -1;
    articulations.insert(articulations.begin(), std::move(plainOpen));
    InstrumentSynthTestAccess::configureController(fixture.synth, 4, 2, 1);
    InstrumentSynthTestAccess::configureCachedController(fixture.synth, 4, 10, 2);

    fixture.synth.startVoice(fixture.openVoice, &fixture.openSound, 1, 42, 0.8f);
    InstrumentSynthTestAccess::setVoiceArticulation(*fixture.openVoice, fixture.openSpec);
    InstrumentSynthTestAccess::setVoiceRuntimeArticulation(*fixture.openVoice, 2);
    fixture.openVoice->sourceSamplePosition = 4410.0;
    fixture.openVoice->cachedEstimatedDb = -24.0f;

    fixture.synth.handleController(1, 4, 100);

    CHECK(fixture.openVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::FadingOut);
    CHECK(fixture.tightVoice->getCurrentlyPlayingSound() == &fixture.tightSound);
}

TEST_CASE("InstrumentSynth - closing the pedal in silence does not create a pedal event", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);
    fixture.tightVoice->killImmediately();
    InstrumentSynthTestAccess::configureCachedController(fixture.synth, 4, 10, 1);

    fixture.synth.handleController(1, 4, 100);

    REQUIRE_FALSE(fixture.openVoice->isVoiceActive());
    REQUIRE_FALSE(fixture.pedalVoice->isVoiceActive());
}

TEST_CASE("InstrumentSynth - tight-only playback does not create an implicit pedal event", "[InstrumentSynth][HiHatTransition][ControllerPath]") {
    ControllerTransitionFixture fixture;
    fixture.engine.engineSettings.hihatFootSampleMode.store(HiHatFootSampleMode::CloseOnly);
    auto& articulations = InstrumentSynthTestAccess::articulations(fixture.synth);
    articulations[0].pedalOpenCloseValue = 85;
    articulations[2].pedalOpenCloseValue = 85;
    InstrumentSynthTestAccess::configureCachedController(fixture.synth, 4, 10, 1);

    fixture.synth.handleController(1, 4, 100);

    REQUIRE(fixture.tightVoice->getEnvelopeState() == InstrumentSynthVoice::EnvelopeState::Playing);
    REQUIRE_FALSE(fixture.pedalVoice->isVoiceActive());
}

TEST_CASE("InstrumentSynthVoice - pans mono direct mic into stereo output", "[InstrumentSynth][routing]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "cow1";
    slot.type = KitLib::InstrumentSpec::Type::Cowbell;
    InstrumentSynth synth { &engine, slot };

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(1, 2);
    buffer->setSample(0, 0, 1.0f);
    buffer->setSample(0, 1, 1.0f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.micBufferChannelStart = 0;
    sound.micBufferChannelCount = 2;
    REQUIRE(InstrumentSynthTestAccess::configureMicPlacement(synth, sound, "dtm", 1, 2, true, 0, 0));

    InstrumentSynthVoice voice;
    InstrumentSynthTestAccess::startVoiceForRender(voice, synth, sound);

    juce::AudioBuffer<float> output(2, 2);
    output.clear();
    voice.renderNextBlock(output, 0, 2);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.7071067f).margin(0.0001f));
    REQUIRE(output.getSample(1, 0) == Catch::Approx(0.7071067f).margin(0.0001f));
}

TEST_CASE("InstrumentSynthVoice - negative playback offset advances the source", "[InstrumentSynth][timing]") {
    InstrumentSynth synth(nullptr, KitLayout::InstrumentSlot {});
    auto buffer = std::make_shared<juce::AudioBuffer<float>>(1, 3);
    buffer->setSample(0, 0, 0.25f);
    buffer->setSample(0, 1, 0.5f);
    buffer->setSample(0, 2, 0.75f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 1000.0;
    sound.sampleLengthSamples = buffer->getNumSamples();
    sound.playbackOffsetMs = -1.0f;

    InstrumentSynthVoice voice;
    InstrumentSynthTestAccess::startVoiceForRender(voice, synth, sound);

    juce::AudioBuffer<float> output(1, 1);
    output.clear();
    voice.renderNextBlock(output, 0, 1);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.5f).margin(0.0001f));
}

TEST_CASE("InstrumentSynthVoice - positive playback offset delays the source", "[InstrumentSynth][timing]") {
    InstrumentSynth synth(nullptr, KitLayout::InstrumentSlot {});
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

    InstrumentSynthVoice voice;
    InstrumentSynthTestAccess::startVoiceForRender(voice, synth, sound);

    juce::AudioBuffer<float> output(1, 3);
    output.clear();
    voice.renderNextBlock(output, 0, 3);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.0f));
    REQUIRE(output.getSample(0, 1) == Catch::Approx(0.0f));
    REQUIRE(output.getSample(0, 2) == Catch::Approx(1.0f).margin(0.0001f));
}

TEST_CASE("InstrumentSynthVoice - flips stereo direct mic before manual pan", "[InstrumentSynth][routing]") {
    DrumKitEngine engine;
    KitLayout::InstrumentSlot slot;
    slot.id = "perc1";
    slot.type = KitLib::InstrumentSpec::Type::Cowbell;
    InstrumentSynth synth { &engine, slot };
    synth.setFlipLR(true);

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
    REQUIRE(InstrumentSynthTestAccess::configureMicPlacement(synth, sound, "dts", 2, 2, true, -127, -127));

    InstrumentSynthVoice voice;
    InstrumentSynthTestAccess::startVoiceForRender(voice, synth, sound);

    juce::AudioBuffer<float> output(2, 2);
    output.clear();
    voice.renderNextBlock(output, 0, 2);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(1.0f).margin(0.0001f));
    REQUIRE(output.getSample(1, 0) == Catch::Approx(0.0f).margin(0.0001f));
}

TEST_CASE("InstrumentSynthVoice - room stereo ignores layer pan", "[InstrumentSynth][routing]") {
    InstrumentSynth synth(nullptr, KitLayout::InstrumentSlot {});
    auto buffer = std::make_shared<juce::AudioBuffer<float>>(2, 1);
    buffer->setSample(0, 0, 0.25f);
    buffer->setSample(1, 0, 0.75f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.micBufferChannelStart = 0;
    sound.micBufferChannelCount = 2;
    REQUIRE(InstrumentSynthTestAccess::configureMicPlacement(synth, sound, "rfs", 2, 2, false, 96, 127));

    InstrumentSynthVoice voice;
    InstrumentSynthTestAccess::startVoiceForRender(voice, synth, sound);

    juce::AudioBuffer<float> output(2, 1);
    output.clear();
    voice.renderNextBlock(output, 0, 1);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.25f).margin(0.0001f));
    REQUIRE(output.getSample(1, 0) == Catch::Approx(0.75f).margin(0.0001f));
}

TEST_CASE("InstrumentSynthVoice - overhead stereo uses only manual pan", "[InstrumentSynth][routing]") {
    InstrumentSynth synth(nullptr, KitLayout::InstrumentSlot {});
    auto buffer = std::make_shared<juce::AudioBuffer<float>>(2, 1);
    buffer->setSample(0, 0, 0.25f);
    buffer->setSample(1, 0, 0.75f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.micBufferChannelStart = 0;
    sound.micBufferChannelCount = 2;
    REQUIRE(InstrumentSynthTestAccess::configureMicPlacement(synth, sound, "ons", 2, 2, false, -127, 127));

    InstrumentSynthVoice voice;
    InstrumentSynthTestAccess::startVoiceForRender(voice, synth, sound);

    juce::AudioBuffer<float> output(2, 1);
    output.clear();
    voice.renderNextBlock(output, 0, 1);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.0f).margin(0.0001f));
    REQUIRE(output.getSample(1, 0) == Catch::Approx(0.75f).margin(0.0001f));
}

TEST_CASE("InstrumentSynthVoice - direct mono output ignores layer pan", "[InstrumentSynth][routing]") {
    InstrumentSynth synth(nullptr, KitLayout::InstrumentSlot {});
    auto buffer = std::make_shared<juce::AudioBuffer<float>>(1, 1);
    buffer->setSample(0, 0, 0.5f);

    SampledSound sound;
    sound.buffer = buffer;
    sound.sampleRate = 44100.0;
    sound.micBufferChannelStart = 0;
    sound.micBufferChannelCount = 1;
    REQUIRE(InstrumentSynthTestAccess::configureMicPlacement(synth, sound, "dtm", 1, 1, false, -96, 127));

    InstrumentSynthVoice voice;
    InstrumentSynthTestAccess::startVoiceForRender(voice, synth, sound);

    juce::AudioBuffer<float> output(1, 1);
    output.clear();
    voice.renderNextBlock(output, 0, 1);

    REQUIRE(output.getSample(0, 0) == Catch::Approx(0.5f).margin(0.0001f));
}

TEST_CASE("InstrumentSynth - bleed follows source mic after routing to generic output code", "[InstrumentSynth][routing]") {
    InstrumentSynth synth(nullptr, KitLayout::InstrumentSlot {});
    const auto sourceMic = KitLib::MicSpec::parseMicCode("dtm1");
    REQUIRE(sourceMic.ok());

    KitLib::SampleVariantSpec variant(nullptr, {}, 'a', sourceMic.value);
    auto sound = std::make_shared<SampledSound>(nullptr, &variant);
    sound->routedMicCode = "dtm";
    sound->routedMicCodeId = StringsDictSingleton::getId(sound->routedMicCode);
    synth.addSound(sound);

    synth.setMicBleedLevels({ { "dtm1", 0.25f } });

    REQUIRE(sound->cachedMicBleedGain.load(std::memory_order_relaxed) == Catch::Approx(0.25f));
}

TEST_CASE("InstrumentSynth - layer parameters update only the target articulation", "[InstrumentSynth][InstrumentMap][routing]") {
    InstrumentSynth synth(nullptr, KitLayout::InstrumentSlot {});
    const auto bellId = StringsDictSingleton::getId("bell");
    const auto bowId = StringsDictSingleton::getId("bow");
    auto& articulations = InstrumentSynthTestAccess::articulations(synth);
    articulations.emplace_back(bellId, 0);
    articulations.back().routeDestinationNameId = bellId;
    articulations.back().layers.emplace_back();
    articulations.emplace_back(bowId, 0);
    articulations.back().routeDestinationNameId = bowId;
    articulations.back().layers.emplace_back();

    const auto sourceMic = KitLib::MicSpec::parseMicCode("dtm1");
    REQUIRE(sourceMic.ok());
    KitLib::SampleVariantSpec variant(nullptr, {}, 'a', sourceMic.value);
    SampledSound bellSound(nullptr, &variant);
    SampledSound bowSound(nullptr, &variant);
    articulations[0].layers[0].samples.push_back({ &bellSound, 1, {} });
    articulations[1].layers[0].samples.push_back({ &bowSound, 1, {} });

    InstrumentSynth::LayerParameterUpdate update;
    update.tuningCents = 120;
    update.volumeDb = -6.0f;
    update.pan = -40;
    update.flipLR = true;
    update.flipPhase = true;
    update.micBleedLevels = std::unordered_map<juce::String, float> { { "dtm1", 0.25f } };

    REQUIRE(synth.updateArticulationParameters({ { bellId, 0 } }, update));
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
