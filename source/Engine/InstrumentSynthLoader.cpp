#include "InstrumentSynth.h"
#include "ArticulationNameHelpers.h"
#include "DrumKitEngine.h"
#include "MicRoutingBuilder.h"
#include "SampleLoader.h"

const MicLayout* InstrumentSynth::getMicLayout() const { return this->parent ? this->parent->getMicLayout() : nullptr; }
const MicRoutingPlan* InstrumentSynth::getMicRoutingPlan() const { return this->parent ? this->parent->getMicRoutingPlan() : nullptr; }
SampleLoader* InstrumentSynth::getSampleLoader() { return this->parent ? this->parent->getSampleLoader() : nullptr; }

void InstrumentSynth::storeRoutingContext(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationSourceMap& artSources, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, bool strictArticulationResolution, const std::unordered_map<juce::String, juce::String>& micRouteOverrides) {
    this->lastRoutingContext.defaultInstrument = defaultInstSpec;
    this->lastRoutingContext.artSources = artSources;
    this->lastRoutingContext.allowedArticulationNameIds = allowedArticulationNameIds;
    this->lastRoutingContext.allowedArticulationFilters = allowedArticulationFilters;
    this->lastRoutingContext.artNameRemap = artNameRemap;
    this->lastRoutingContext.artExactRemap = artExactRemap;
    this->lastRoutingContext.artAliases = artAliases;
    this->lastRoutingContext.setMicRouteOverrides(micRouteOverrides);
    this->lastRoutingContext.strictArticulationResolution = strictArticulationResolution;
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
        const auto& art = this->runtimeArticulations[static_cast<size_t>(artIdx)];
        if (art.routeDestinationNameId != 0 || art.routeDestinationExprId != 0) continue;
        const uint64_t key = (static_cast<uint64_t>(art.nameId) << 32) | static_cast<uint64_t>(art.exprId);
        this->artIdByNameExprId.emplace(key, artIdx);
    }
    for (int artIdx = 0; artIdx < static_cast<int>(this->runtimeArticulations.size()); ++artIdx) {
        const auto& art = this->runtimeArticulations[static_cast<size_t>(artIdx)];
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

bool InstrumentSynth::routeUsesDestinationRuntimeKey(const ResolvedArticulationRoute& route) const {
    if (route.isAliasRoute || route.sourceArticulation == nullptr) return false;
    if (route.isControllerSibling && !route.isExplicitMapRoute) return false;
    if (route.isExplicitMapRoute) return true;
    return route.destinationArtNameId != route.sourceArticulation->nameId || route.destinationExprId != route.sourceArticulation->exprId;
}

InstrumentSynth::ArtRtIdx InstrumentSynth::findRuntimeArticulationIndex(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId, StringsDictSingleton::Id destinationNameId, StringsDictSingleton::Id destinationExprId) const {
    for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx) {
        const auto& artRt = this->runtimeArticulations[static_cast<size_t>(artIdx)];
        if (artRt.nameId == nameId && artRt.exprId == exprId && artRt.routeDestinationNameId == destinationNameId && artRt.routeDestinationExprId == destinationExprId)
            return static_cast<ArtRtIdx>(artIdx);
    }
    return -1;
}

InstrumentSynth::ArtRtIdx InstrumentSynth::getOrAddRuntimeArticulationIdx(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId, StringsDictSingleton::Id destinationNameId, StringsDictSingleton::Id destinationExprId) {
    const auto existingArtIdx = this->findRuntimeArticulationIndex(nameId, exprId, destinationNameId, destinationExprId);
    if (existingArtIdx >= 0) return existingArtIdx;
    this->runtimeArticulations.emplace_back(nameId, exprId);
    auto& artRt = this->runtimeArticulations.back();
    artRt.routeDestinationNameId = destinationNameId;
    artRt.routeDestinationExprId = destinationExprId;
    return static_cast<ArtRtIdx>(this->runtimeArticulations.size() - 1);
}

bool InstrumentSynth::tryGetRuntimeArticulationIdx(const ResolvedArticulationRoute& route, bool createIfMissing, ArtRtIdx& artIdx) {
    const auto* artSpec = route.sourceArticulation;
    if (artSpec == nullptr) return false;
    if (!this->routeUsesDestinationRuntimeKey(route)) return this->tryGetRuntimeArticulationIdx(artSpec, createIfMissing, artIdx);

    if (createIfMissing) {
        artIdx = this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId, route.destinationArtNameId, route.destinationExprId);
        return true;
    }
    artIdx = this->findRuntimeArticulationIndex(artSpec->nameId, artSpec->exprId, route.destinationArtNameId, route.destinationExprId);
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
    this->clearDestinationRoutes();
    this->resolvedArticulationRoutes.clear();
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

void InstrumentSynth::loadInstrumentWithSources(KitLib::InstrumentSpec* defaultInstrument, const ArticulationSourceMap& artSources, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, bool strictArticulationResolution, const std::unordered_map<juce::String, juce::String>& micRouteOverrides) {
    this->loadInstrumentWithSources(defaultInstrument, artSources, allowedArticulationNameIds, allowedArticulationFilters, artNameRemap, artExactRemap, artAliases, nullptr, strictArticulationResolution, micRouteOverrides);
}

void InstrumentSynth::loadInstrumentWithSources(KitLib::InstrumentSpec* defaultInstrument, const ArticulationSourceMap& artSources, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot, bool strictArticulationResolution, const std::unordered_map<juce::String, juce::String>& micRouteOverrides) {
    if (defaultInstrument == nullptr) return;
    this->unloadInstrument();

    this->librarySnapshotPtr = std::move(librarySnapshot);
    this->instrumentSpecstPtr = defaultInstrument;
    this->type = defaultInstrument->meta.type;

    this->storeRoutingContext(defaultInstrument, artSources, allowedArticulationNameIds, allowedArticulationFilters, artNameRemap, artExactRemap, artAliases, strictArticulationResolution, micRouteOverrides);
    this->processLayoutArticulations(defaultInstrument, artSources, allowedArticulationNameIds, allowedArticulationFilters, artNameRemap, artExactRemap, artAliases, strictArticulationResolution);
}

void InstrumentSynth::loadInstrumentWithRouteList(KitLib::InstrumentSpec* defaultInstrument, const RoutingContext& routingContext, std::vector<ResolvedArticulationRoute> routes, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot) {
    if (defaultInstrument == nullptr) return;
    this->unloadInstrument();

    this->librarySnapshotPtr = std::move(librarySnapshot);
    this->instrumentSpecstPtr = defaultInstrument;
    this->type = defaultInstrument->meta.type;
    this->lastRoutingContext = routingContext;
    this->lastRoutingContext.rebuildMicRouteOverrideIds();
    if (!this->lastRoutingContext.isValid()) this->lastRoutingContext.defaultInstrument = defaultInstrument;

    LOG(Debug, "loadInstrumentWithRouteList: available articulationSpecs for inst=" + defaultInstrument->meta.codeRaw + ":");
    for (const auto& artSpec : defaultInstrument->articulationSpecs) {
        this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId);
    }

    this->resolvedArticulationRoutes = std::move(routes);
    this->applyResolvedRouteList(true, true);
    this->rebuildArtLookupCache();
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
    this->micBleedLevelsById.clear();
    this->refreshSampleMicBleedGains();
}

void InstrumentSynth::setMicBleedLevels(std::unordered_map<juce::String, float> levels) {
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

float InstrumentSynth::getMicBleedLevel(const juce::String& micCode) const {
    if (micCode.isEmpty()) return 1.0f;
    return this->getMicBleedLevel(StringsDictSingleton::getId(micCode));
}

float InstrumentSynth::getMicBleedLevel(StringsDictSingleton::Id micCodeId) const {
    if (micCodeId == StringsDictSingleton::EMPTY) return 1.0f;

    const auto it = this->micBleedLevelsById.find(micCodeId);
    if (it == this->micBleedLevelsById.end()) return 1.0f;

    return juce::jlimit(0.0f, 1.0f, it->second);
}

void InstrumentSynth::refreshSampleMicBleedGains() {
    for (const auto& soundPtr : this->sampledSoundPtrs) {
        if (!soundPtr) continue;
        soundPtr->cachedMicBleedGain.store(this->getMicBleedLevel(soundPtr->routedMicCodeId), std::memory_order_relaxed);
    }
}

KitLib::ArticulationSpec* InstrumentSynth::resolveArticulationSpec(KitLib::InstrumentSpec* instSpec, const LayoutArticulation& layoutArt, bool allowFallback) {
    if (instSpec == nullptr) return nullptr;

    for (const auto& artSpec : instSpec->articulationSpecs) {
        if (artSpec->nameId == layoutArt.nameId && artSpec->exprId == layoutArt.exprId) return artSpec.get();
    }
    if (!allowFallback || this->getEngine() == nullptr) return nullptr;

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
        if (!routingContext.allowedArticulationFilters.empty()) {
            bool matchesFilter = false;
            for (const auto& artFilter : routingContext.allowedArticulationFilters) {
                if (ArticulationNameHelpers::mapFilterMatchesArticulation(artFilter, layoutArt.codeRaw)) {
                    matchesFilter = true;
                    break;
                }
            }
            if (!matchesFilter) continue;
        } else if (!routingContext.allowedArticulationNameIds.empty() && !vectorContains(routingContext.allowedArticulationNameIds, layoutArt.nameId)) continue;

        ResolvedArticulationBinding binding(layoutArt);
        binding.sourceInstrument = routingContext.defaultInstrument;

        auto sourceIt = routingContext.artSources.find(layoutArt.nameId);
        if (sourceIt != routingContext.artSources.end() && sourceIt->second != nullptr) {
            binding.sourceInstrument = sourceIt->second;
            binding.strictArticulationResolution = true;
        }

        if (binding.sourceInstrument == nullptr) continue;

        const auto exactRemapIt = routingContext.artExactRemap.find(makeDestinationRouteKey(layoutArt.nameId, layoutArt.exprId));
        if (exactRemapIt != routingContext.artExactRemap.end()) {
            binding.resolvedLayoutArt.nameId = exactRemapIt->second.nameId;
            binding.resolvedLayoutArt.exprId = exactRemapIt->second.exprId;
            binding.strictArticulationResolution = true;
        } else {
            auto remapIt = routingContext.artNameRemap.find(layoutArt.nameId);
            if (remapIt != routingContext.artNameRemap.end()) {
                binding.resolvedLayoutArt.nameId = remapIt->second;
                binding.strictArticulationResolution = true;
            }
        }
        if (routingContext.strictArticulationResolution) binding.strictArticulationResolution = true;

        binding.resolvedSpec = this->resolveArticulationSpec(binding.sourceInstrument, binding.resolvedLayoutArt, !binding.strictArticulationResolution);
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

void InstrumentSynth::appendResolvedArticulationRoutes(std::vector<ResolvedArticulationRoute>& routes, const std::vector<ResolvedArticulationBinding>& bindings) {
    for (const auto& binding : bindings) {
        if (binding.sourceInstrument == nullptr || binding.resolvedSpec == nullptr) continue;

        ResolvedArticulationRoute route(binding.layoutArt);
        route.sourceInstrument = binding.sourceInstrument;
        route.sourceArticulation = binding.resolvedSpec;
        route.isExplicitMapRoute = binding.strictArticulationResolution;
        routes.push_back(std::move(route));

        for (auto* siblingSpec : binding.controllerSiblingSpecs) {
            if (siblingSpec == nullptr) continue;

            ResolvedArticulationRoute siblingRoute(binding.layoutArt);
            siblingRoute.sourceInstrument = binding.sourceInstrument;
            siblingRoute.sourceArticulation = siblingSpec;
            siblingRoute.isExplicitMapRoute = binding.strictArticulationResolution;
            siblingRoute.isControllerSibling = true;
            routes.push_back(std::move(siblingRoute));
        }
    }
}

void InstrumentSynth::applyResolvedArticulationRoute(const ResolvedArticulationRoute& route, bool loadSamples, bool createIfMissing, CcDimensionsMap& foundCcDimensions, std::set<juce::String>& skippedMicCodes) {
    const auto& layoutArt = route.destinationLayoutArt;
    if (route.isAliasRoute) {
        auto findAliasSourceArticulation = [this, &route](StringsDictSingleton::Id exprId) {
            ArtRtIdx sourceArtIdx = -1;
            for (const auto& sourceRoute : this->resolvedArticulationRoutes) {
                if (sourceRoute.isAliasRoute || sourceRoute.isControllerSibling || sourceRoute.sourceArticulation == nullptr) continue;
                if (sourceRoute.sourceArticulation->nameId != route.aliasTarget.nameId || sourceRoute.sourceArticulation->exprId != exprId) continue;
                if (this->tryGetRuntimeArticulationIdx(sourceRoute, false, sourceArtIdx)) return sourceArtIdx;
            }
            return this->findArticulationIndex(route.aliasTarget.nameId, exprId);
        };

        ArtRtIdx sourceArtIdx = -1;
        if (route.aliasTarget.exprId != 0) sourceArtIdx = findAliasSourceArticulation(route.aliasTarget.exprId);
        if (sourceArtIdx < 0) sourceArtIdx = findAliasSourceArticulation(layoutArt.exprId);
        if (sourceArtIdx < 0 && layoutArt.exprId != 0) sourceArtIdx = findAliasSourceArticulation(0);
        if (sourceArtIdx < 0) return;

        for (const auto note : route.midiNotes) {
            if (note < 128) this->noteToArticulation[static_cast<size_t>(note)] = sourceArtIdx;
        }
        return;
    }

    auto* sourceInstrument = route.sourceInstrument;
    auto* artSpec = route.sourceArticulation;
    if (artSpec == nullptr || sourceInstrument == nullptr) return;

    ArtRtIdx artIdx = -1;
    if (!this->tryGetRuntimeArticulationIdx(route, createIfMissing, artIdx)) {
        LOG(Warning, "Skipping routing rebuild for '" + layoutArt.codeRaw + "' because runtime articulation '" + artSpec->codeRaw + "' is not loaded");
        return;
    }
    auto& artRt = this->runtimeArticulations[static_cast<size_t>(artIdx)];
    if (loadSamples) {
        artSpec->sortVelocityLayers();
        this->processArticulationSpec(sourceInstrument, layoutArt, artSpec, artRt, artIdx, foundCcDimensions, skippedMicCodes);
    } else {
        this->refreshRuntimeExpressionValues(*artSpec, artRt);
        this->buildCcMappings(layoutArt, artSpec, artIdx, foundCcDimensions);
    }
    if (!route.isControllerSibling) {
        for (uint8_t note : route.midiNotes) this->noteToArticulation[note] = artIdx;
    }
}

void InstrumentSynth::applyResolvedRouteList(bool loadSamples, bool createIfMissing) {
    CcDimensionsMap foundCcDimensions;
    std::set<juce::String> skippedMicCodes;

    for (const auto& route : this->resolvedArticulationRoutes) {
        if (!route.isControllerSibling && !route.isAliasRoute) {
            LOG(Info, "  Loading articulation: " + route.destinationLayoutArt.codeRaw + " (notes " + midiVectorToString(route.midiNotes) + ")");
            if (route.sourceArticulation != nullptr) {
                LOG(Info, "    resolved: layout='" + route.destinationLayoutArt.codeRaw + "' -> artSpec='" + route.sourceArticulation->codeRaw
                    + "' (nameId=" + juce::String((int)route.sourceArticulation->nameId) + " exprId=" + juce::String((int)route.sourceArticulation->exprId) + ")");
            }
        }
        this->applyResolvedArticulationRoute(route, loadSamples, createIfMissing, foundCcDimensions, skippedMicCodes);
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

void InstrumentSynth::appendAliasArticulationRoutes(std::vector<ResolvedArticulationRoute>& routes, const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext) {
    if (routingContext.artAliases.empty()) return;

    for (const auto& layoutArt : layoutSlot.articulations) {
        const auto aliasIt = routingContext.artAliases.find(layoutArt.nameId);
        if (aliasIt == routingContext.artAliases.end()) continue;

        bool hasNormalRoute = false;
        for (const auto& route : routes) {
            if (!route.isAliasRoute && !route.isControllerSibling && route.destinationArtNameId == layoutArt.nameId && route.destinationExprId == layoutArt.exprId) {
                hasNormalRoute = true;
                break;
            }
        }
        if (hasNormalRoute) continue;

        ResolvedArticulationRoute aliasRoute(layoutArt);
        aliasRoute.aliasTarget = aliasIt->second;
        aliasRoute.isAliasRoute = true;
        routes.push_back(std::move(aliasRoute));
    }
}

std::vector<InstrumentSynth::ResolvedArticulationRoute> InstrumentSynth::buildResolvedRouteList(const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext) {
    const auto bindings = this->collectResolvedLayoutArticulations(layoutSlot, routingContext);
    std::vector<ResolvedArticulationRoute> routes;
    this->appendResolvedArticulationRoutes(routes, bindings);
    this->appendAliasArticulationRoutes(routes, layoutSlot, routingContext);
    return routes;
}

void InstrumentSynth::applyResolvedRouting(const std::vector<ResolvedArticulationBinding>& bindings, const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext, bool loadSamples, bool createIfMissing) {
    this->resolvedArticulationRoutes.clear();
    this->appendResolvedArticulationRoutes(this->resolvedArticulationRoutes, bindings);
    this->appendAliasArticulationRoutes(this->resolvedArticulationRoutes, layoutSlot, routingContext);
    this->applyResolvedRouteList(loadSamples, createIfMissing);
}

void InstrumentSynth::processLayoutArticulations(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationSourceMap& artSources, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, bool strictArticulationResolution) {
    if (defaultInstSpec == nullptr) return;

    this->storeRoutingContext(defaultInstSpec, artSources, allowedArticulationNameIds, allowedArticulationFilters, artNameRemap, artExactRemap, artAliases, strictArticulationResolution, this->lastRoutingContext.micRouteOverrides);

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
    this->applyResolvedRouting(bindings, this->instrumentSlot, this->lastRoutingContext, true, true);
    this->rebuildArtLookupCache();
}


void InstrumentSynth::processArticulationSpec(KitLib::InstrumentSpec* instSpec
  , const LayoutArticulation& layoutArt
  , KitLib::ArticulationSpec* artSpec
  , ArticulationRuntime& artRt
  , ArtRtIdx artIdx
  , CcDimensionsMap& foundCcDimensions
  , std::set<juce::String>& skippedMicCodes
) {
    MicRoutingBuilder micRouting(this);

    this->createVelocityLayers(instSpec, layoutArt.notes, artSpec, artRt, micRouting);
    micRouting.mergeSkippedInto(skippedMicCodes);
    artRt.buildVelocityMapping();

    this->refreshRuntimeExpressionValues(*artSpec, artRt);
    this->buildCcMappings(layoutArt, artSpec, artIdx, foundCcDimensions);
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
            sound->routedMicCodeId = StringsDictSingleton::getId(assignment.micCode);
            sound->cachedMicBleedGain.store(this->getMicBleedLevel(sound->routedMicCodeId), std::memory_order_relaxed);
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


void InstrumentSynth::buildCcMappings(const LayoutArticulation& layoutArt, KitLib::ArticulationSpec* artSpec, ArtRtIdx artIdx, CcDimensionsMap& foundCcDimensions) {

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
            const auto ccNum = static_cast<uint8_t>(cc.number);
            auto& vec = this->noteToControllers[note];
            if (!vectorContains(vec, ccNum))
                vec.emplace_back(ccNum);
        }

        // only controllers with notes that are linked to this articulation
        if (containsSameValue(cc.notes, layoutArt.notes)) {
            LOG(Debug, "buildCcMappings: controller " + juce::String((int)cc.number) + " {num=" + juce::String((int)cc.number) + ",codeIdx=" + juce::String((int)cc.codeIdx) + ", notes=" + midiVectorToString(cc.notes) + "} applies to articulation " + layoutArt.codeRaw + " (layoutArt.notes=" + midiVectorToString(layoutArt.notes) + ")");
            const auto values = this->getEffectiveExpressionValues(*artSpec, cc.codeIdx);
            for (const auto& val : values) {
                const auto ccNum = static_cast<int8_t>(cc.number);
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
        const auto count = valArtPairs.size();

        for (size_t valueIdx = 0; valueIdx <= static_cast<size_t>(MIDI_MAX_VALUE); ++valueIdx) {
            int8_t chosen = valArtPairs[count - 1].second; // default to last
            for (size_t pairIdx = 0; pairIdx < count; ++pairIdx) {
                if (static_cast<int>(valueIdx) <= static_cast<int>(valArtPairs[pairIdx].first)) {
                    chosen = valArtPairs[pairIdx].second;
                    break;
                }
            }
            map[valueIdx] = static_cast<uint8_t>(chosen);
        }
        this->ccToArticulationMask[static_cast<size_t>(ccNum)] = true;
        this->ccToArticulationTable[static_cast<size_t>(ccNum)] = map;
        
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
    this->applyResolvedRouting(bindings, layoutSlot, routingContext, false, false);
}

bool InstrumentSynth::updateMicRoute(const juce::String& sourceMicCode, const juce::String& outputMicCode) {
    const auto normalizedSourceMicCode = sourceMicCode.trim();
    if (normalizedSourceMicCode.isEmpty()) return false;

    const MicLayout::Mic* sourceLayoutMic = nullptr;
    int bestSourcePriority = -1;
    if (const auto* micLayout = this->getMicLayout()) {
        for (const auto& mic : micLayout->mics) {
            if (mic.code.compareIgnoreCase(normalizedSourceMicCode) != 0) continue;
            const auto selector = parseMicInstrumentSelector(mic.instrument, this->getEngine()->getInstrumentCatalog());
            const int priority = static_cast<int>(matchMicInstrumentSelector(selector, this->getId(), this->getType(), this->getEngine()->getInstrumentCatalog()));
            if (priority > bestSourcePriority) {
                sourceLayoutMic = &mic;
                bestSourcePriority = priority;
            }
        }
    }

    std::vector<SampledSound*> matchingSounds;
    std::vector<juce::String> matchingSourceMicCodes;
    for (const auto& sound : this->sampledSoundPtrs) {
        if (sound == nullptr || sound->sampleVariantSpec == nullptr) continue;
        const auto& micSpec = sound->sampleVariantSpec->micSpec;
        const bool codeMatches = micSpec.raw.compareIgnoreCase(normalizedSourceMicCode) == 0;
        const bool specsMatch = sourceLayoutMic != nullptr && micSpec.type == sourceLayoutMic->specs.type && micSpec.position == sourceLayoutMic->specs.position && micSpec.format == sourceLayoutMic->specs.format;
        if (!codeMatches && !specsMatch) continue;
        matchingSounds.push_back(sound.get());
        bool codeAlreadyAdded = false;
        for (const auto& matchingCode : matchingSourceMicCodes) {
            if (matchingCode.compareIgnoreCase(micSpec.raw) == 0) {
                codeAlreadyAdded = true;
                break;
            }
        }
        if (!codeAlreadyAdded) matchingSourceMicCodes.push_back(micSpec.raw);
    }
    if (matchingSounds.empty()) return false;

    const auto oldOverrides = this->lastRoutingContext.micRouteOverrides;
    auto newOverrides = oldOverrides;
    std::erase_if(newOverrides, [&normalizedSourceMicCode, &matchingSourceMicCodes](const auto& route) {
        if (route.first.compareIgnoreCase(normalizedSourceMicCode) == 0) return true;
        for (const auto& matchingCode : matchingSourceMicCodes)
            if (route.first.compareIgnoreCase(matchingCode) == 0) return true;
        return false;
    });
    const auto normalizedOutputMicCode = outputMicCode.trim();
    if (normalizedOutputMicCode.isNotEmpty()) {
        for (const auto& matchingCode : matchingSourceMicCodes)
            newOverrides[matchingCode] = normalizedOutputMicCode;
    }
    this->lastRoutingContext.setMicRouteOverrides(newOverrides);

    if (normalizedOutputMicCode == "-") {
        for (auto* sound : matchingSounds) {
            sound->micBufferChannelStart = -1;
            sound->micBufferChannelCount = sound->sampleVariantSpec->fileChannelCount > 0 ? sound->sampleVariantSpec->fileChannelCount : sound->sampleVariantSpec->micSpec.channels;
            sound->routedMicCode = sound->sampleVariantSpec->micSpec.raw;
            sound->routedMicCodeId = StringsDictSingleton::getId(sound->routedMicCode);
        }
        return true;
    }

    MicRoutingBuilder micRouting(this);
    const auto* firstMatchingSound = matchingSounds.front();
    const int fileChannelCount = firstMatchingSound->sampleVariantSpec->fileChannelCount > 0 ? firstMatchingSound->sampleVariantSpec->fileChannelCount : firstMatchingSound->sampleVariantSpec->micSpec.channels;
    const auto assignment = micRouting.resolve(firstMatchingSound->sampleVariantSpec->micSpec, fileChannelCount);
    if (assignment.globalStart < 0) {
        this->lastRoutingContext.setMicRouteOverrides(oldOverrides);
        return false;
    }

    for (auto* sound : matchingSounds) {
        sound->micBufferChannelStart = assignment.globalStart;
        sound->micBufferChannelCount = assignment.channels;
        sound->routedMicCode = assignment.micCode;
        sound->routedMicCodeId = StringsDictSingleton::getId(assignment.micCode);
        sound->cachedMicBleedGain.store(this->getMicBleedLevel(sound->routedMicCodeId), std::memory_order_relaxed);
        sound->useDirectBalance = assignment.applyBalance && assignment.channels == 2 && sound->sampleVariantSpec->micSpec.type == KitLib::MicSpec::Type::Direct;
    }
    return true;
}
