#pragma once

#include "InstrumentSynth.h"
#include "DrumKitEngine.h"
#include "MicRoutingBuilder.h"
#include "SampleLoader.h"

const MicLayout* InstrumentSynth::getMicLayout() const { return this->parent ? this->parent->getMicLayout() : nullptr; }
const MicRoutingPlan* InstrumentSynth::getMicRoutingPlan() const { return this->parent ? this->parent->getMicRoutingPlan() : nullptr; }
SampleLoader* InstrumentSynth::getSampleLoader() { return this->parent ? this->parent->getSampleLoader() : nullptr; }

void InstrumentSynth::storeRoutingContext(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationSourceMap& artSources, const std::vector<uint32_t>& allowedArticulationNameIds, const ArticulationNameRemap& artNameRemap, const ArticulationAliasMap& artAliases) {
    this->lastRoutingContext.defaultInstrument = defaultInstSpec;
    this->lastRoutingContext.artSources = artSources;
    this->lastRoutingContext.allowedArticulationNameIds = allowedArticulationNameIds;
    this->lastRoutingContext.artNameRemap = artNameRemap;
    this->lastRoutingContext.artAliases = artAliases;
}

void InstrumentSynth::resetRoutingTables() {
    this->noteToArticulation.fill(-1);
    this->noteToControllers.clear();
    this->ccToArticulationMask.fill(false);
    this->lastCcArticulation.fill(-1);
    this->lastCcValue.fill(0);
    this->pendingCcValue.fill(0);
    this->pendingCcMidiChannel.fill(0);
    this->pendingCcTransition.fill(false);
    this->pendingCcArticulation.fill(-1);
    this->hihatPedalClosedState.fill(false);
    this->hihatImplicitPedalEligible.fill(false);
    this->lastCcProcessedSample.fill(0);
    this->hasProcessedCcTransition.fill(false);
    this->hihatPedalNote = -1;
    const auto pedalNameId = StringsDictSingleton::getId("pedal");
    for (const auto& articulation : this->instrumentSlot.articulations) {
        if (articulation.nameId == pedalNameId && !articulation.notes.empty()) {
            this->hihatPedalNote = articulation.notes.front();
            break;
        }
    }
    this->renderedSampleCount = 0;
    this->processedHiHatTransitions.store(0, std::memory_order_relaxed);
    this->coalescedHiHatTransitions.store(0, std::memory_order_relaxed);
    this->skippedHiHatTransitions.store(0, std::memory_order_relaxed);
    this->completedHiHatTransitions.store(0, std::memory_order_relaxed);
    for (auto& map : this->ccToArticulationTable) map.fill(0);
}

void InstrumentSynth::rebuildArtLookupCache() {
    this->artIdByNameExprId.clear();
    for (int artIdx = 0; artIdx < static_cast<int>(this->runtimeArticulations.size()); ++artIdx) {
        const auto& art = this->runtimeArticulations[artIdx];
        const uint64_t key = (static_cast<uint64_t>(art.nameId) << 32) | static_cast<uint64_t>(art.exprId);
        this->artIdByNameExprId.emplace(key, artIdx);
    }
}

bool InstrumentSynth::tryGetRuntimeArticulationIdx(const KitLib::ArticulationSpec* artSpec, bool createIfMissing, ArtRtIdx& artIdx) {
    if (artSpec == nullptr) return false;
    if (createIfMissing) {
        artIdx = this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId);
        return true;
    }
    artIdx = this->findArticulationIndex(artSpec->nameId, artSpec->exprId);
    return artIdx >= 0;
}

void InstrumentSynth::unloadInstrument() {
    this->runtimeArticulations.clear();
    this->resetRoutingTables();
    // stop and clear voices before releasing sounds
    this->clearVoices();
    this->clearSounds();
    this->instrumentSpecstPtr = nullptr;
    this->librarySnapshotPtr.reset();
    this->lastRoutingContext = {};
    this->clearParameterOverrides();
}

void InstrumentSynth::loadInstrument(KitLib::InstrumentSpec* instSpec) {
    this->loadInstrument(instSpec, nullptr);
}

void InstrumentSynth::loadInstrument(KitLib::InstrumentSpec* instSpec, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot) {
    if (instSpec == nullptr) return;
    this->unloadInstrument();

    this->librarySnapshotPtr = std::move(librarySnapshot);
    this->instrumentSpecstPtr = instSpec;
    this->type = instSpec->meta.type;

    this->processLayoutArticulations(instSpec, {});
}

void InstrumentSynth::loadInstrumentWithSources(KitLib::InstrumentSpec* defaultInstrument, const ArticulationSourceMap& artSources) {
    this->loadInstrumentWithSources(defaultInstrument, artSources, {});
}

void InstrumentSynth::loadInstrumentWithSources(KitLib::InstrumentSpec* defaultInstrument, const ArticulationSourceMap& artSources, const std::vector<uint32_t>& allowedArticulationNameIds, const ArticulationNameRemap& artNameRemap, const ArticulationAliasMap& artAliases) {
    this->loadInstrumentWithSources(defaultInstrument, artSources, allowedArticulationNameIds, artNameRemap, artAliases, nullptr);
}

void InstrumentSynth::loadInstrumentWithSources(KitLib::InstrumentSpec* defaultInstrument, const ArticulationSourceMap& artSources, const std::vector<uint32_t>& allowedArticulationNameIds, const ArticulationNameRemap& artNameRemap, const ArticulationAliasMap& artAliases, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot) {
    if (defaultInstrument == nullptr) return;
    this->unloadInstrument();

    this->librarySnapshotPtr = std::move(librarySnapshot);
    this->instrumentSpecstPtr = defaultInstrument;
    this->type = defaultInstrument->meta.type;

    this->processLayoutArticulations(defaultInstrument, artSources, allowedArticulationNameIds, artNameRemap, artAliases);
}

void InstrumentSynth::clearParameterOverrides() {
    this->tuningOffsetCents = 0;
    this->attackOverrideMs.reset();
    this->decayOverrideMs.reset();
    this->volumeDb = 0.0f;
    this->setPan(0);
    this->flipLR = false;
    this->flipPhase = false;
    this->micBleedLevels.clear();
}

void InstrumentSynth::setMicBleedLevels(std::unordered_map<juce::String, float> levels) {
    this->micBleedLevels.clear();
    for (auto& [micCode, level] : levels) {
        const float clamped = juce::jlimit(0.0f, 1.0f, level);
        if (clamped < 0.999f) this->micBleedLevels[micCode] = clamped;
    }
}

float InstrumentSynth::getMicBleedLevel(const juce::String& micCode) const {
    if (micCode.isEmpty()) return 1.0f;

    const auto it = this->micBleedLevels.find(micCode);
    if (it == this->micBleedLevels.end()) return 1.0f;

    return juce::jlimit(0.0f, 1.0f, it->second);
}

KitLib::ArticulationSpec* InstrumentSynth::resolveArticulationSpec(KitLib::InstrumentSpec* instSpec, const LayoutArticulation& layoutArt) {
    if (instSpec == nullptr) return nullptr;

    for (const auto& artSpec : instSpec->articulationSpecs) {
        if (artSpec->nameId == layoutArt.nameId && artSpec->exprId == layoutArt.exprId) return artSpec.get();
    }
    if (this->getEngine() == nullptr) return nullptr;

    const auto& fallbackIdMap = this->getEngine()->getArticulationFallbackIdMap();
    auto it = fallbackIdMap.find(layoutArt.nameId);
    if (it == fallbackIdMap.end()) return nullptr;

    for (const auto& fallbackId : it->second) {
        for (const auto& artSpec : instSpec->articulationSpecs) {
            if (artSpec->nameId == fallbackId && artSpec->exprId == layoutArt.exprId) return artSpec.get();
        }
    }
    return nullptr;
}

std::vector<uint8_t> InstrumentSynth::getEffectiveExpressionValues(const KitLib::ArticulationSpec& artSpec, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) const {
    for (const auto& dim : artSpec.expressionDimensions) {
        if (dim != nullptr && dim->type == dimensionType && !dim->values.empty())
            return dim->values;
    }

    const auto code = KitLib::ArticulationSpec::ExpressionDimension::typeToString(dimensionType);
    if (!KitLayout::isSupportedExpressionPointCode(code))
        return {};
        
    const auto label = KitLayout::expressionPointLabelForArticulation(artSpec, dimensionType);
    if (label.isEmpty())
        return {};

    if (const auto value = KitLayout::getExpressionPoint(this->instrumentSlot.expressionPoints, code, label); value.has_value())
        return { *value };

    if (this->parent != nullptr) {
        if (const auto* layout = this->parent->getKitLayout()) {
            if (const auto value = KitLayout::getExpressionPoint(layout->expressionPoints, code, label); value.has_value())
                return { *value };
        }
    }

    const auto defaults = KitLayout::getDefaultExpressionPoints();
    if (const auto value = KitLayout::getExpressionPoint(defaults, code, label); value.has_value())
        return { *value };

    return {};
}

bool InstrumentSynth::hasEffectiveExpressionValues(const KitLib::ArticulationSpec& artSpec, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) const {
    return !this->getEffectiveExpressionValues(artSpec, dimensionType).empty();
}

void InstrumentSynth::refreshRuntimeExpressionValues(const KitLib::ArticulationSpec& artSpec, ArticulationRuntime& artRt) {
    auto maxExpressionValue = [this, &artSpec](KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) {
        const auto values = this->getEffectiveExpressionValues(artSpec, dimensionType);
        if (values.empty()) return -1;
        return static_cast<int>(*std::max_element(values.begin(), values.end()));
    };

    artRt.pedalOpenCloseValue = maxExpressionValue(KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose);
    artRt.zoneCenterRimValue = maxExpressionValue(KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneCenterRim);
    artRt.zoneBowEdgeValue = maxExpressionValue(KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneBowEdge);
}

std::vector<InstrumentSynth::ResolvedArticulationBinding> InstrumentSynth::collectResolvedLayoutArticulations(const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext) {
    std::vector<ResolvedArticulationBinding> bindings;

    if (!routingContext.isValid()) return bindings;

    bindings.reserve(layoutSlot.articulations.size());

    for (const auto& layoutArt : layoutSlot.articulations) {
        if (!routingContext.allowedArticulationNameIds.empty() && !vectorContains(routingContext.allowedArticulationNameIds, layoutArt.nameId)) continue;

        ResolvedArticulationBinding binding(layoutArt);
        binding.sourceInstrument = routingContext.defaultInstrument;

        auto sourceIt = routingContext.artSources.find(layoutArt.nameId);
        if (sourceIt != routingContext.artSources.end() && sourceIt->second != nullptr)
            binding.sourceInstrument = sourceIt->second;

        if (binding.sourceInstrument == nullptr) continue;

        auto remapIt = routingContext.artNameRemap.find(layoutArt.nameId);
        if (remapIt != routingContext.artNameRemap.end())
            binding.resolvedLayoutArt.nameId = remapIt->second;

        binding.resolvedSpec = this->resolveArticulationSpec(binding.sourceInstrument, binding.resolvedLayoutArt);
        if (binding.resolvedSpec == nullptr) {
            LOG(Warning, binding.sourceInstrument->parent->meta.name + "/" + binding.sourceInstrument->meta.codeRaw
                + " has no suitable articulations for '" + layoutSlot.id + "' '" + layoutArt.codeRaw + "'");
            bindings.push_back(std::move(binding));
            continue;
        }

        // Controller-driven siblings: explicit source dimensions or layout/default expression points.
        for (const auto& artSpec : binding.sourceInstrument->articulationSpecs) {
            if (artSpec == nullptr) continue;
            if (artSpec->nameId == binding.resolvedLayoutArt.nameId && artSpec->exprId == binding.resolvedLayoutArt.exprId) continue;

            bool hasMatchingController = false;
            for (const auto& controller : layoutSlot.controllers) {
                if (!containsSameValue(controller.notes, layoutArt.notes)) continue;
                if (controller.codeIdx == KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose && artSpec->nameId != binding.resolvedLayoutArt.nameId) {
                    continue;
                }

                if (this->hasEffectiveExpressionValues(*artSpec, controller.codeIdx)) {
                    hasMatchingController = true;
                    break;
                }
            }

            if (hasMatchingController) binding.controllerSiblingSpecs.push_back(artSpec.get());
        }
        bindings.push_back(std::move(binding));
    }
    return bindings;
}

void InstrumentSynth::applyResolvedArticulationBinding(const LayoutArticulation& layoutArt, KitLib::InstrumentSpec* sourceInstrument, KitLib::ArticulationSpec* artSpec, bool loadSamples, bool createIfMissing, bool updateNoteRoutes, CcDimensionsMap& foundCcDimensions, std::set<juce::String>& skippedMicCodes) {
    if (artSpec == nullptr || sourceInstrument == nullptr) return;

    ArtRtIdx artIdx = -1;
    if (!this->tryGetRuntimeArticulationIdx(artSpec, createIfMissing, artIdx)) {
        LOG(Warning, "Skipping routing rebuild for '" + layoutArt.codeRaw + "' because runtime articulation '" + artSpec->codeRaw + "' is not loaded");
        return;
    }
    auto& artRt = this->runtimeArticulations[artIdx];
    if (loadSamples) {
        artSpec->sortVelocityLayers();
        this->processArticulationSpec(sourceInstrument, layoutArt, artSpec, artRt, foundCcDimensions, skippedMicCodes);
    } else {
        this->refreshRuntimeExpressionValues(*artSpec, artRt);
        this->buildCcMappings(layoutArt, artSpec, foundCcDimensions);
    }
    if (updateNoteRoutes) {
        for (uint8_t note : layoutArt.notes) this->noteToArticulation[note] = artIdx;
    }
}

void InstrumentSynth::applyResolvedRouting(const std::vector<ResolvedArticulationBinding>& bindings, bool loadSamples, bool createIfMissing) {
    CcDimensionsMap foundCcDimensions;
    std::set<juce::String> skippedMicCodes;

    for (const auto& binding : bindings) {
        LOG(Info, "  Loading articulation: " + binding.layoutArt.codeRaw + " (notes " + midiVectorToString(binding.layoutArt.notes) + ")");

        if (binding.layoutArt.nameId != binding.resolvedLayoutArt.nameId) {
            LOG(Info, "    remap: layout art '" + binding.layoutArt.codeRaw + "' (nameId=" + juce::String((int)binding.layoutArt.nameId)
                + ") -> resolve as nameId=" + juce::String((int)binding.resolvedLayoutArt.nameId)
                + " ('" + StringsDictSingleton::nameOf(binding.resolvedLayoutArt.nameId) + "')");
        }
        if (binding.resolvedSpec != nullptr) {
            LOG(Info, "    resolved: layout='" + binding.layoutArt.codeRaw + "' -> artSpec='" + binding.resolvedSpec->codeRaw
                + "' (nameId=" + juce::String((int)binding.resolvedSpec->nameId) + " exprId=" + juce::String((int)binding.resolvedSpec->exprId) + ")");
        }
        this->applyResolvedArticulationBinding(binding.layoutArt, binding.sourceInstrument, binding.resolvedSpec, loadSamples, createIfMissing, true, foundCcDimensions, skippedMicCodes);

        for (auto* siblingSpec : binding.controllerSiblingSpecs)
            this->applyResolvedArticulationBinding(binding.layoutArt, binding.sourceInstrument, siblingSpec, loadSamples, createIfMissing, false, foundCcDimensions, skippedMicCodes);
    }

    if (loadSamples && !skippedMicCodes.empty() && LOG_ENABLED(Notice)) {
        juce::String codes;
        for (const auto& c : skippedMicCodes) {
            if (codes.isNotEmpty()) codes += ", ";
            codes += "'" + c + "'";
        }
        LOG(Notice, "    Skipping samples for mic(s) " + codes + " - not present in active mic layout");
    }
    this->processCcDimensions(foundCcDimensions);
}

void InstrumentSynth::applyArticulationAliases(const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext) {
    if (routingContext.artAliases.empty())
        return;

    for (const auto& layoutArt : layoutSlot.articulations) {
        const auto aliasIt = routingContext.artAliases.find(layoutArt.nameId);
        if (aliasIt == routingContext.artAliases.end())
            continue;

        ArtRtIdx sourceArtIdx = this->findArticulationIndex(aliasIt->second, layoutArt.exprId);
        if (sourceArtIdx < 0 && layoutArt.exprId != 0)
            sourceArtIdx = this->findArticulationIndex(aliasIt->second, 0);
        if (sourceArtIdx < 0)
            continue;

        for (const auto note : layoutArt.notes) {
            if (note < 128)
                this->noteToArticulation[static_cast<size_t>(note)] = sourceArtIdx;
        }
    }
}

void InstrumentSynth::processLayoutArticulations(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationSourceMap& artSources, const std::vector<uint32_t>& allowedArticulationNameIds, const ArticulationNameRemap& artNameRemap, const ArticulationAliasMap& artAliases) {
    if (defaultInstSpec == nullptr) return;

    this->storeRoutingContext(defaultInstSpec, artSources, allowedArticulationNameIds, artNameRemap, artAliases);

    LOG(Debug, "processLayoutArticulations: available articulationSpecs for inst=" + defaultInstSpec->meta.codeRaw + ":");
    for (const auto& artSpec : defaultInstSpec->articulationSpecs) {
        this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId); // discoverArticulations

        if (LOG_ENABLED(Debug)) {
            juce::String dims;
            for (const auto& dim : artSpec->expressionDimensions)
                dims += "[" + dim->typeToString(dim->type) + ", " + midiVectorToString(dim->values) + "]";
            LOG(Debug, "  nameId=" + juce::String((int)artSpec->nameId) + " exprId=" + juce::String((int)artSpec->exprId) + " exprDims=" + dims + " artSpec=" + artSpec->codeRaw);
        }
    }
    const auto bindings = this->collectResolvedLayoutArticulations(this->instrumentSlot, this->lastRoutingContext);
    this->applyResolvedRouting(bindings, true, true);
    this->applyArticulationAliases(this->instrumentSlot, this->lastRoutingContext);
    this->rebuildArtLookupCache();
}


void InstrumentSynth::processArticulationSpec(KitLib::InstrumentSpec* instSpec
  , const LayoutArticulation& layoutArt
  , KitLib::ArticulationSpec* artSpec
  , ArticulationRuntime& artRt
  , CcDimensionsMap& foundCcDimensions
  , std::set<juce::String>& skippedMicCodes
) {
    MicRoutingBuilder micRouting(this);

    this->createVelocityLayers(instSpec, layoutArt.notes, artSpec, artRt, micRouting);
    micRouting.mergeSkippedInto(skippedMicCodes);
    artRt.buildVelocityMapping();

    this->refreshRuntimeExpressionValues(*artSpec, artRt);
    this->buildCcMappings(layoutArt, artSpec, foundCcDimensions);
}

void InstrumentSynth::createVelocityLayers(KitLib::InstrumentSpec* instSpec
    , const KitLayout::MidiNotes& notes
    , KitLib::ArticulationSpec* artSpec
    , ArticulationRuntime& artRt
    , MicRoutingBuilder& micRouting
) {
    for (const auto& velSpec : artSpec->velocityLayerSpecs) {
        VelocityLayerRuntime layer;
        layer.velocity = velSpec->velocity;
        layer.samples.reserve(velSpec->sampleVariantSpecs.size());

        for (const auto& varSpec : velSpec->sampleVariantSpecs) {
            const int fileChannelCount = varSpec->fileChannelCount > 0
                ? varSpec->fileChannelCount
                : varSpec->micSpec.channels;

            const auto assignment = micRouting.resolve(varSpec->micSpec, fileChannelCount);
            // Skip samples whose mic is not present in the active mic layout.
            // The unmatched code is recorded in the builder for a single summary log later.
            if (assignment.globalStart < 0)
                continue;

            auto sound = std::make_shared<SampledSound>(instSpec, varSpec.get(), varSpec->sampleFile, notes);
            const std::optional<int> effectiveBalance = this->instrumentSlot.balance.has_value()
                ? this->instrumentSlot.balance
                : (artSpec->balanceConflict ? std::optional<int> {} : artSpec->balance);
            sound->decayEnvelope = KitLib::DecayEnvelope::decode(varSpec->decayCode, varSpec->peakDb, varSpec->peakMs);
            if (!varSpec->decayCode.isEmpty() && !sound->decayEnvelope.isValid()) {
                LOG(Warning, "Invalid decay metadata for sample: " + varSpec->sampleFile.getFileName() + ". Pedal articulation replacement will be skipped for this sample.");
            }
            sound->micBufferChannelStart = assignment.globalStart;
            sound->micBufferChannelCount = assignment.channels;
            sound->routedMicCode = assignment.micCode;
            sound->balance = effectiveBalance.value_or(0);
            sound->useDirectBalance = assignment.applyBalance && effectiveBalance.has_value() && assignment.channels == 2 && varSpec->micSpec.type == KitLib::MicSpec::Type::Direct;

            this->addSound(sound);
            SampleEntry se;
            se.sound = sound.get();
            // Store variant key (1..26) as returned by SampleVariantSpec::letterToIdx (0 if no letter)
            se.variantIdx = static_cast<uint8_t>(KitLib::SampleVariantSpec::letterToIdx(varSpec->letter));
            layer.samples.push_back(se);

            SampleLoader* loader = this->getSampleLoader();
            if (loader != nullptr) {
                std::weak_ptr<SampledSound> weakSound = sound;

                SampleLoadSpec loadSpec;
                loadSpec.file = varSpec->sampleFile;
                loadSpec.volumeTrimDb = varSpec->volumeTrimDb;

                loader->getQueue().enqueueCandidate(
                    loadSpec,
                    instSpec->meta.type,
                    artSpec->nameId,
                    velSpec->velocity,
                    KitLib::SampleVariantSpec::letterToIdx(varSpec->letter),
                    [weakSound](std::shared_ptr<juce::AudioBuffer<float>> buf, double sr) {
                        auto soundLocked = weakSound.lock();
                        if (!soundLocked) return;
                        soundLocked->buffer = buf;
                        soundLocked->sampleRate = sr;
                        soundLocked->sampleLengthSamples = buf != nullptr ? buf->getNumSamples() : 0;
                        soundLocked->sampleLengthMs = buf != nullptr && sr > 0.0
                            ? static_cast<float>((static_cast<double>(soundLocked->sampleLengthSamples) / sr) * 1000.0)
                            : 0.0f;
                        soundLocked->transitionAudioValid = false;
                        if (buf != nullptr && buf->getNumChannels() > 0 && buf->getNumSamples() > 0) {
                            int sourceChannelStart = 0;
                            int sourceChannelCount = buf->getNumChannels();
                            if (soundLocked->sampleVariantSpec != nullptr) {
                                sourceChannelStart = soundLocked->sampleVariantSpec->fileChannelStart;
                                if (soundLocked->sampleVariantSpec->fileChannelCount > 0)
                                    sourceChannelCount = soundLocked->sampleVariantSpec->fileChannelCount;
                            }
                            const int sourceChannelEnd = juce::jmin(
                                buf->getNumChannels(),
                                sourceChannelStart + sourceChannelCount);
                            for (int sourceChannel = juce::jmax(0, sourceChannelStart); sourceChannel < sourceChannelEnd; ++sourceChannel) {
                                if (buf->getMagnitude(sourceChannel, 0, buf->getNumSamples()) > 0.000001f) {
                                    soundLocked->transitionAudioValid = true;
                                    break;
                                }
                            }
                        }
                        soundLocked->loaded.store(buf != nullptr, std::memory_order_release);
                    }
                );
            }
        }
        artRt.layers.push_back(std::move(layer));
    }
}


void InstrumentSynth::buildCcMappings(const LayoutArticulation& layoutArt, KitLib::ArticulationSpec* artSpec, CcDimensionsMap& foundCcDimensions) {

    { // Debug: report articulation and its expression dimensions
        if (LOG_ENABLED(Debug)) {
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
    }

    for (const auto& cc : this->instrumentSlot.controllers) {
        for (uint8_t note : cc.notes) {
            auto ccNum = static_cast<uint8_t>(cc.number);
            auto& vec = this->noteToControllers[note];
            if (!vectorContains(vec, ccNum))
                vec.emplace_back(ccNum);
        }

        // only controllers with notes that are linked to this articulation
        if (containsSameValue(cc.notes, layoutArt.notes)) {
            LOG(Debug, "buildCcMappings: controller " + juce::String((int)cc.number) + " {num=" + juce::String((int)cc.number) + ",codeIdx=" + juce::String((int)cc.codeIdx) + ", notes=" + midiVectorToString(cc.notes) + "} applies to articulation " + layoutArt.codeRaw + " (layoutArt.notes=" + midiVectorToString(layoutArt.notes) + ")");
            const auto values = this->getEffectiveExpressionValues(*artSpec, cc.codeIdx);
            for (const auto& val : values) {
                auto ccNum = static_cast<uint8_t>(cc.number);
                const auto artIdx = this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId);
                const auto pair = std::make_pair(static_cast<int8_t>(val), artIdx);
                auto& vec = foundCcDimensions[ccNum];
                // avoid inserting identical (value,artIdx) duplicates
                if (!vectorContains(vec, pair)) {
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
        this->ccToArticulationMask[ccNum] = true;
        this->ccToArticulationTable[ccNum] = map;
        
        if (LOG_ENABLED(Debug)) { // log condensed mapping: thresholds -> articulation index
            juce::String summary;
            for (size_t i = 0; i < valArtPairs.size(); ++i) {
                if (i) summary += ",";
                summary += "(" + juce::String((int)valArtPairs[i].first) + ":" + this->getArtName(valArtPairs[i].second) + " (artIdx=" + juce::String(valArtPairs[i].second) + "))";
            }
            LOG(Debug, "processCcDimensions: CC " + juce::String((int)ccNum) + " thresholds: " + summary);
        }
    }
}

void InstrumentSynth::rebuildRoutingFromLayout(const KitLayout::InstrumentSlot& layoutSlot) {
    this->instrumentSlot = layoutSlot;
    this->resetRoutingTables();

    LOG(Info, "Rebuilding routing from layout for slot '" + layoutSlot.id + "' with " + juce::String((int)layoutSlot.articulations.size()) + " articulations");

    RoutingContext routingContext = this->lastRoutingContext;
    if (!routingContext.isValid())
        routingContext.defaultInstrument = this->instrumentSpecstPtr;

    const auto bindings = this->collectResolvedLayoutArticulations(layoutSlot, routingContext);
    this->applyResolvedRouting(bindings, false, false);
    this->applyArticulationAliases(layoutSlot, routingContext);
}
