#include "InstrumentPlayer.h"
#include "../Config/PluginLogger.h"
#include "ArticulationNames.h"
#include "DrumKitEngine.h"
#include "MicRoutingBuilder.h"
#include "SampleLoader.h"
#include <cmath>

void InstrumentPlayer::storeRoutingContext(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationLibraryMap& articulationLibraryMap, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, bool strictArticulationResolution, const std::unordered_map<juce::String, juce::String>& micRouteOverrides) {
    this->lastRoutingContext.defaultInstrument = defaultInstSpec;
    this->lastRoutingContext.articulationLibraryMap = articulationLibraryMap;
    this->lastRoutingContext.allowedArticulationNameIds = allowedArticulationNameIds;
    this->lastRoutingContext.allowedArticulationFilters = allowedArticulationFilters;
    this->lastRoutingContext.artNameRemap = artNameRemap;
    this->lastRoutingContext.artExactRemap = artExactRemap;
    this->lastRoutingContext.artAliases = artAliases;
    this->lastRoutingContext.setMicRouteOverrides(micRouteOverrides);
    this->lastRoutingContext.strictArticulationResolution = strictArticulationResolution;
}

void InstrumentPlayer::resetRoutingTables() {
    this->noteToArticulation.fill(-1);
    this->rebuildHiHatNoteChokeValues();
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

void InstrumentPlayer::rebuildArtLookupCache() {
    this->artIdByNameExprId.clear();
    for (int artIdx = 0; artIdx < static_cast<int>(this->runtimeArticulations.size()); ++artIdx) {
        const auto& art = this->runtimeArticulations[static_cast<size_t>(artIdx)];
        if (art.routeArticulationSlotNameId != 0 || art.routeArticulationSlotExpressionId != 0) continue;
        const uint64_t key = (static_cast<uint64_t>(art.nameId) << 32) | static_cast<uint64_t>(art.exprId);
        this->artIdByNameExprId.emplace(key, artIdx);
    }
    for (int artIdx = 0; artIdx < static_cast<int>(this->runtimeArticulations.size()); ++artIdx) {
        const auto& art = this->runtimeArticulations[static_cast<size_t>(artIdx)];
        const uint64_t key = (static_cast<uint64_t>(art.nameId) << 32) | static_cast<uint64_t>(art.exprId);
        this->artIdByNameExprId.emplace(key, artIdx);
    }
}

bool InstrumentPlayer::tryGetRuntimeArticulationIdx(const KitLib::ArticulationSpec* artSpec, bool createIfMissing, ArtRtIdx& artIdx) {
    if (artSpec == nullptr) return false;
    if (createIfMissing) {
        artIdx = this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId);
        return true;
    }
    artIdx = this->findArticulationIndex(artSpec->nameId, artSpec->exprId);
    return artIdx >= 0;
}

bool InstrumentPlayer::playbackEntryUsesArticulationSlotRuntimeKey(const ArticulationPlaybackEntry& playbackEntry) const {
    if (playbackEntry.isAliasEntry || playbackEntry.libraryArticulation == nullptr) return false;
    if (playbackEntry.isControllerSibling && !playbackEntry.usesExplicitMapLayer) return false;
    if (playbackEntry.usesExplicitMapLayer) return true;
    return playbackEntry.articulationSlotNameId != playbackEntry.libraryArticulation->nameId || playbackEntry.articulationSlotExpressionId != playbackEntry.libraryArticulation->exprId;
}

InstrumentPlayer::ArtRtIdx InstrumentPlayer::findRuntimeArticulationIndex(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId, StringsDictSingleton::Id articulationSlotNameId, StringsDictSingleton::Id articulationSlotExpressionId) const {
    for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx) {
        const auto& artRt = this->runtimeArticulations[static_cast<size_t>(artIdx)];
        if (artRt.nameId == nameId && artRt.exprId == exprId && artRt.routeArticulationSlotNameId == articulationSlotNameId && artRt.routeArticulationSlotExpressionId == articulationSlotExpressionId)
            return static_cast<ArtRtIdx>(artIdx);
    }
    return -1;
}

InstrumentPlayer::ArtRtIdx InstrumentPlayer::getOrAddRuntimeArticulationIdx(StringsDictSingleton::Id nameId, StringsDictSingleton::Id exprId, StringsDictSingleton::Id articulationSlotNameId, StringsDictSingleton::Id articulationSlotExpressionId) {
    const auto existingArtIdx = this->findRuntimeArticulationIndex(nameId, exprId, articulationSlotNameId, articulationSlotExpressionId);
    if (existingArtIdx >= 0) return existingArtIdx;
    this->runtimeArticulations.emplace_back(nameId, exprId);
    auto& artRt = this->runtimeArticulations.back();
    artRt.routeArticulationSlotNameId = articulationSlotNameId;
    artRt.routeArticulationSlotExpressionId = articulationSlotExpressionId;
    return static_cast<ArtRtIdx>(this->runtimeArticulations.size() - 1);
}

bool InstrumentPlayer::tryGetRuntimeArticulationIdx(const ArticulationPlaybackEntry& playbackEntry, bool createIfMissing, ArtRtIdx& artIdx) {
    const auto* artSpec = playbackEntry.libraryArticulation;
    if (artSpec == nullptr) return false;
    if (!this->playbackEntryUsesArticulationSlotRuntimeKey(playbackEntry)) return this->tryGetRuntimeArticulationIdx(artSpec, createIfMissing, artIdx);

    if (createIfMissing) {
        artIdx = this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId, playbackEntry.articulationSlotNameId, playbackEntry.articulationSlotExpressionId);
        return true;
    }
    artIdx = this->findRuntimeArticulationIndex(artSpec->nameId, artSpec->exprId, playbackEntry.articulationSlotNameId, playbackEntry.articulationSlotExpressionId);
    return artIdx >= 0;
}


KitLib::ArticulationSpec* InstrumentPlayer::resolveArticulationSpec(KitLib::InstrumentSpec* instSpec, const ArticulationSlot& articulationSlot, bool allowFallback) {
    if (instSpec == nullptr) return nullptr;

    for (const auto& artSpec : instSpec->articulationSpecs) {
        if (artSpec->nameId == articulationSlot.nameId && artSpec->exprId == articulationSlot.exprId) return artSpec.get();
    }
    if (!allowFallback || this->getEngine() == nullptr) return nullptr;

    const auto& fallbackIdMap = this->getEngine()->getArticulationFallbackIdMap();
    auto it = fallbackIdMap.find(articulationSlot.nameId);
    if (it == fallbackIdMap.end()) return nullptr;

    for (const auto& fallbackId : it->second) {
        for (const auto& artSpec : instSpec->articulationSpecs) {
            if (artSpec->nameId == fallbackId && artSpec->exprId == articulationSlot.exprId) return artSpec.get();
        }
    }
    return nullptr;
}

std::vector<uint8_t> InstrumentPlayer::getEffectiveExpressionValues(const KitLib::ArticulationSpec& artSpec, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) const {
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

bool InstrumentPlayer::hasEffectiveExpressionValues(const KitLib::ArticulationSpec& artSpec, KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) const {
    return !this->getEffectiveExpressionValues(artSpec, dimensionType).empty();
}

void InstrumentPlayer::refreshRuntimeExpressionValues(const KitLib::ArticulationSpec& artSpec, ArticulationRuntime& artRt) {
    auto maxExpressionValue = [this, &artSpec](KitLib::ArticulationSpec::ExpressionDimension::Type dimensionType) {
        const auto values = this->getEffectiveExpressionValues(artSpec, dimensionType);
        if (values.empty()) return -1;
        return static_cast<int>(*std::max_element(values.begin(), values.end()));
    };

    artRt.pedalOpenCloseValue = maxExpressionValue(KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose);
    artRt.zoneCenterRimValue = maxExpressionValue(KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneCenterRim);
    artRt.zoneBowEdgeValue = maxExpressionValue(KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneBowEdge);
}

std::vector<InstrumentPlayer::ResolvedArticulationSlotBinding> InstrumentPlayer::collectResolvedArticulationSlots(const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext) {
    std::vector<ResolvedArticulationSlotBinding> bindings;

    if (!routingContext.isValid()) return bindings;

    bindings.reserve(layoutSlot.articulations.size());

    for (const auto& articulationSlot : layoutSlot.articulations) {
        if (!routingContext.allowedArticulationFilters.empty()) {
            bool matchesFilter = false;
            for (const auto& artFilter : routingContext.allowedArticulationFilters) {
                if (ArticulationNames::mapFilterMatchesArticulation(artFilter, articulationSlot.codeRaw)) {
                    matchesFilter = true;
                    break;
                }
            }
            if (!matchesFilter) continue;
        } else if (!routingContext.allowedArticulationNameIds.empty() && !vectorContains(routingContext.allowedArticulationNameIds, articulationSlot.nameId)) continue;

        ResolvedArticulationSlotBinding binding(articulationSlot);
        binding.libraryInstrument = routingContext.defaultInstrument;

        auto libraryInstrumentIterator = routingContext.articulationLibraryMap.find(articulationSlot.nameId);
        if (libraryInstrumentIterator != routingContext.articulationLibraryMap.end() && libraryInstrumentIterator->second != nullptr) {
            binding.libraryInstrument = libraryInstrumentIterator->second;
            binding.strictArticulationResolution = true;
        }

        if (binding.libraryInstrument == nullptr) continue;

        const auto exactRemapIt = routingContext.artExactRemap.find(makeArticulationSlotRouteKey(articulationSlot.nameId, articulationSlot.exprId));
        if (exactRemapIt != routingContext.artExactRemap.end()) {
            binding.resolvedArticulationSlot.nameId = exactRemapIt->second.nameId;
            binding.resolvedArticulationSlot.exprId = exactRemapIt->second.exprId;
            binding.strictArticulationResolution = true;
        } else {
            auto remapIt = routingContext.artNameRemap.find(articulationSlot.nameId);
            if (remapIt != routingContext.artNameRemap.end()) {
                binding.resolvedArticulationSlot.nameId = remapIt->second;
                binding.strictArticulationResolution = true;
            }
        }
        if (routingContext.strictArticulationResolution) binding.strictArticulationResolution = true;

        binding.resolvedSpec = this->resolveArticulationSpec(binding.libraryInstrument, binding.resolvedArticulationSlot, !binding.strictArticulationResolution);
        if (binding.resolvedSpec == nullptr) {
            LOG(Warning, binding.libraryInstrument->parent->meta.name + "/" + binding.libraryInstrument->meta.codeRaw
                + " has no suitable articulations for '" + layoutSlot.id + "' '" + articulationSlot.codeRaw + "'");
            bindings.push_back(std::move(binding));
            continue;
        }

        // Controller-driven siblings: explicit library articulation dimensions or kit layout/default expression points.
        for (const auto& artSpec : binding.libraryInstrument->articulationSpecs) {
            if (artSpec == nullptr) continue;
            if (artSpec->nameId == binding.resolvedArticulationSlot.nameId && artSpec->exprId == binding.resolvedArticulationSlot.exprId) continue;

            bool hasMatchingController = false;
            for (const auto& controller : layoutSlot.controllers) {
                if (!containsSameValue(controller.notes, articulationSlot.notes)) continue;
                if (controller.codeIdx == KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose && artSpec->nameId != binding.resolvedArticulationSlot.nameId) {
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

void InstrumentPlayer::appendArticulationPlaybackEntries(std::vector<ArticulationPlaybackEntry>& playbackEntries, const std::vector<ResolvedArticulationSlotBinding>& bindings) {
    for (const auto& binding : bindings) {
        if (binding.libraryInstrument == nullptr || binding.resolvedSpec == nullptr) continue;

        ArticulationPlaybackEntry playbackEntry(binding.articulationSlot);
        playbackEntry.libraryInstrument = binding.libraryInstrument;
        playbackEntry.libraryArticulation = binding.resolvedSpec;
        playbackEntry.usesExplicitMapLayer = binding.strictArticulationResolution;
        playbackEntries.push_back(std::move(playbackEntry));

        for (auto* siblingSpec : binding.controllerSiblingSpecs) {
            if (siblingSpec == nullptr) continue;

            ArticulationPlaybackEntry siblingPlaybackEntry(binding.articulationSlot);
            siblingPlaybackEntry.libraryInstrument = binding.libraryInstrument;
            siblingPlaybackEntry.libraryArticulation = siblingSpec;
            siblingPlaybackEntry.usesExplicitMapLayer = binding.strictArticulationResolution;
            siblingPlaybackEntry.isControllerSibling = true;
            playbackEntries.push_back(std::move(siblingPlaybackEntry));
        }
    }
}

void InstrumentPlayer::applyArticulationPlaybackEntry(const ArticulationPlaybackEntry& playbackEntry, bool loadSamples, bool createIfMissing, CcDimensionsMap& foundCcDimensions, std::set<juce::String>& skippedMicCodes) {
    const auto& articulationSlot = playbackEntry.articulationSlot;
    if (playbackEntry.isAliasEntry) {
        auto findAliasLibraryArticulation = [this, &playbackEntry](StringsDictSingleton::Id exprId) {
            ArtRtIdx libraryArticulationIdx = -1;
            for (const auto& libraryPlaybackEntry : this->articulationPlaybackEntries) {
                if (libraryPlaybackEntry.isAliasEntry || libraryPlaybackEntry.isControllerSibling || libraryPlaybackEntry.libraryArticulation == nullptr) continue;
                if (libraryPlaybackEntry.libraryArticulation->nameId != playbackEntry.aliasTarget.nameId || libraryPlaybackEntry.libraryArticulation->exprId != exprId) continue;
                if (this->tryGetRuntimeArticulationIdx(libraryPlaybackEntry, false, libraryArticulationIdx)) return libraryArticulationIdx;
            }
            return this->findArticulationIndex(playbackEntry.aliasTarget.nameId, exprId);
        };

        ArtRtIdx libraryArticulationIdx = -1;
        if (playbackEntry.aliasTarget.exprId != 0) libraryArticulationIdx = findAliasLibraryArticulation(playbackEntry.aliasTarget.exprId);
        if (libraryArticulationIdx < 0) libraryArticulationIdx = findAliasLibraryArticulation(articulationSlot.exprId);
        if (libraryArticulationIdx < 0 && articulationSlot.exprId != 0) libraryArticulationIdx = findAliasLibraryArticulation(0);
        if (libraryArticulationIdx < 0) return;

        for (const auto note : playbackEntry.midiNotes) {
            if (note < 128) this->noteToArticulation[static_cast<size_t>(note)] = libraryArticulationIdx;
        }
        return;
    }

    auto* libraryInstrument = playbackEntry.libraryInstrument;
    auto* artSpec = playbackEntry.libraryArticulation;
    if (artSpec == nullptr || libraryInstrument == nullptr) return;

    ArtRtIdx artIdx = -1;
    if (!this->tryGetRuntimeArticulationIdx(playbackEntry, createIfMissing, artIdx)) {
        LOG(Warning, "Skipping routing rebuild for '" + articulationSlot.codeRaw + "' because runtime articulation '" + artSpec->codeRaw + "' is not loaded");
        return;
    }
    auto& artRt = this->runtimeArticulations[static_cast<size_t>(artIdx)];
    if (loadSamples) {
        artSpec->sortVelocityLevels();
        this->processArticulationSpec(libraryInstrument, articulationSlot, artSpec, artRt, artIdx, foundCcDimensions, skippedMicCodes);
    } else {
        this->refreshRuntimeExpressionValues(*artSpec, artRt);
        this->buildCcMappings(articulationSlot, artSpec, artIdx, foundCcDimensions);
    }
    if (!playbackEntry.isControllerSibling) {
        for (uint8_t note : playbackEntry.midiNotes) this->noteToArticulation[note] = artIdx;
    }
}

void InstrumentPlayer::applyArticulationPlaybackEntries(bool loadSamples, bool createIfMissing) {
    CcDimensionsMap foundCcDimensions;
    std::set<juce::String> skippedMicCodes;

    for (const auto& playbackEntry : this->articulationPlaybackEntries) {
        if (!playbackEntry.isControllerSibling && !playbackEntry.isAliasEntry) {
            LOG(Info, "  Loading articulation: " + playbackEntry.articulationSlot.codeRaw + " (notes " + midiVectorToString(playbackEntry.midiNotes) + ")");
            if (playbackEntry.libraryArticulation != nullptr) {
                LOG(Info, "    resolved: layout='" + playbackEntry.articulationSlot.codeRaw + "' -> artSpec='" + playbackEntry.libraryArticulation->codeRaw
                    + "' (nameId=" + juce::String((int)playbackEntry.libraryArticulation->nameId) + " exprId=" + juce::String((int)playbackEntry.libraryArticulation->exprId) + ")");
            }
        }
        this->applyArticulationPlaybackEntry(playbackEntry, loadSamples, createIfMissing, foundCcDimensions, skippedMicCodes);
    }

    if (loadSamples) this->loadAftertouchChokeTargets(foundCcDimensions, skippedMicCodes);

    if (loadSamples && !skippedMicCodes.empty() && LOG_ENABLED(Notice)) {
        juce::String codes;
        for (const auto& c : skippedMicCodes) {
            if (codes.isNotEmpty()) codes += ", ";
            codes += "'" + c + "'";
        }
        LOG(Notice, "    Skipping samples for mic(s) " + codes + " - not present in active mic layout"); //FIXME: missing context, line alone on 'notice' level not informative!
    }
    this->processCcDimensions(foundCcDimensions);
}

void InstrumentPlayer::loadAftertouchChokeTargets(CcDimensionsMap& foundCcDimensions, std::set<juce::String>& skippedMicCodes) {
    const auto chokeNameId = StringsDictSingleton::getId("choke");
    std::set<KitLib::InstrumentSpec*> libraryInstruments;
    for (const auto& playbackEntry : this->articulationPlaybackEntries) {
        if (playbackEntry.isAliasEntry || playbackEntry.libraryInstrument == nullptr || playbackEntry.libraryArticulation == nullptr) continue;
        if (playbackEntry.libraryArticulation->nameId == chokeNameId) continue;
        libraryInstruments.insert(playbackEntry.libraryInstrument);
    }

    std::set<ArtRtIdx> usedTargetArticulations;
    for (auto* libraryInstrument : libraryInstruments) {
        KitLib::ArticulationSpec* chokeSpec = nullptr;
        for (const auto& artSpec : libraryInstrument->articulationSpecs) {
            if (artSpec != nullptr && artSpec->nameId == chokeNameId && artSpec->exprId == 0) {
                chokeSpec = artSpec.get();
                break;
            }
        }
        if (chokeSpec == nullptr) continue;

        ArtRtIdx chokeArtIdx = -1;
        for (size_t artIdx = 0; artIdx < this->runtimeArticulations.size(); ++artIdx) {
            const auto candidateIdx = static_cast<ArtRtIdx>(artIdx);
            if (usedTargetArticulations.find(candidateIdx) != usedTargetArticulations.end()) continue;
            const auto& candidateArt = this->runtimeArticulations[artIdx];
            if (candidateArt.nameId != chokeSpec->nameId || candidateArt.exprId != chokeSpec->exprId) continue;

            bool candidateUsesLibraryInstrument = candidateArt.velocityLevels.empty();
            for (const auto& velocityLevel : candidateArt.velocityLevels) {
                for (const auto& sampleEntry : velocityLevel.samples) {
                    if (sampleEntry.sound != nullptr && sampleEntry.sound->instrumentSpec == libraryInstrument) candidateUsesLibraryInstrument = true;
                }
            }
            if (candidateUsesLibraryInstrument) {
                chokeArtIdx = candidateIdx;
                break;
            }
        }
        if (chokeArtIdx < 0) {
            this->runtimeArticulations.emplace_back(chokeSpec->nameId, chokeSpec->exprId);
            chokeArtIdx = static_cast<ArtRtIdx>(this->runtimeArticulations.size() - 1);
        }
        usedTargetArticulations.insert(chokeArtIdx);

        auto& chokeArt = this->runtimeArticulations[static_cast<size_t>(chokeArtIdx)];
        if (chokeArt.velocityLevels.empty()) {
            chokeSpec->sortVelocityLevels();
            ArticulationSlot hiddenChokeArticulation { chokeSpec->codeRaw };
            this->processArticulationSpec(libraryInstrument, hiddenChokeArticulation, chokeSpec, chokeArt, chokeArtIdx, foundCcDimensions, skippedMicCodes);
        }

        bool hasTargetSamples = false;
        for (auto& velocityLevel : chokeArt.velocityLevels) {
            for (auto& sampleEntry : velocityLevel.samples) {
                if (sampleEntry.sound == nullptr || sampleEntry.sound->instrumentSpec != libraryInstrument) continue;
                sampleEntry.sound->isAftertouchChokeTarget = true;
                hasTargetSamples = true;
            }
        }
        if (!hasTargetSamples) continue;

        for (const auto& sound : this->sampledSoundPtrs) {
            if (sound == nullptr || sound->instrumentSpec != libraryInstrument || sound->isAftertouchChokeTarget) continue;
            sound->aftertouchChokeArticulationIndex = chokeArtIdx;
        }
    }
}

void InstrumentPlayer::appendAliasPlaybackEntries(std::vector<ArticulationPlaybackEntry>& playbackEntries, const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext) {
    if (routingContext.artAliases.empty()) return;

    for (const auto& articulationSlot : layoutSlot.articulations) {
        const auto aliasIt = routingContext.artAliases.find(articulationSlot.nameId);
        if (aliasIt == routingContext.artAliases.end()) continue;

        bool hasNormalPlaybackEntry = false;
        for (const auto& playbackEntry : playbackEntries) {
            if (!playbackEntry.isAliasEntry && !playbackEntry.isControllerSibling && playbackEntry.articulationSlotNameId == articulationSlot.nameId && playbackEntry.articulationSlotExpressionId == articulationSlot.exprId) {
                hasNormalPlaybackEntry = true;
                break;
            }
        }
        if (hasNormalPlaybackEntry) continue;

        ArticulationPlaybackEntry aliasPlaybackEntry(articulationSlot);
        aliasPlaybackEntry.aliasTarget = aliasIt->second;
        aliasPlaybackEntry.isAliasEntry = true;
        playbackEntries.push_back(std::move(aliasPlaybackEntry));
    }
}

std::vector<InstrumentPlayer::ArticulationPlaybackEntry> InstrumentPlayer::buildArticulationPlaybackEntries(const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext) {
    const auto bindings = this->collectResolvedArticulationSlots(layoutSlot, routingContext);
    std::vector<ArticulationPlaybackEntry> playbackEntries;
    this->appendArticulationPlaybackEntries(playbackEntries, bindings);
    this->appendAliasPlaybackEntries(playbackEntries, layoutSlot, routingContext);
    return playbackEntries;
}

void InstrumentPlayer::applyResolvedRouting(const std::vector<ResolvedArticulationSlotBinding>& bindings, const KitLayout::InstrumentSlot& layoutSlot, const RoutingContext& routingContext, bool loadSamples, bool createIfMissing) {
    this->articulationPlaybackEntries.clear();
    this->appendArticulationPlaybackEntries(this->articulationPlaybackEntries, bindings);
    this->appendAliasPlaybackEntries(this->articulationPlaybackEntries, layoutSlot, routingContext);
    this->applyArticulationPlaybackEntries(loadSamples, createIfMissing);
}

void InstrumentPlayer::processArticulationSlots(KitLib::InstrumentSpec* defaultInstSpec, const ArticulationLibraryMap& articulationLibraryMap, const std::vector<uint32_t>& allowedArticulationNameIds, const std::vector<juce::String>& allowedArticulationFilters, const ArticulationNameRemap& artNameRemap, const ArticulationExactRemap& artExactRemap, const ArticulationAliasMap& artAliases, bool strictArticulationResolution) {
    if (defaultInstSpec == nullptr) return;

    this->storeRoutingContext(defaultInstSpec, articulationLibraryMap, allowedArticulationNameIds, allowedArticulationFilters, artNameRemap, artExactRemap, artAliases, strictArticulationResolution, this->lastRoutingContext.micRouteOverrides);

    LOG(Debug, "processArticulationSlots: available articulationSpecs for inst=" + defaultInstSpec->meta.codeRaw + ":");
    for (const auto& artSpec : defaultInstSpec->articulationSpecs) {
        this->getOrAddRuntimeArticulationIdx(artSpec->nameId, artSpec->exprId); // discoverArticulations

        if (LOG_ENABLED(Debug)) {
            juce::String dims;
            for (const auto& dim : artSpec->expressionDimensions)
                dims += "[" + dim->typeToString(dim->type) + ", " + midiVectorToString(dim->values) + "]";
            LOG(Debug, "  nameId=" + juce::String((int)artSpec->nameId) + " exprId=" + juce::String((int)artSpec->exprId) + " exprDims=" + dims + " artSpec=" + artSpec->codeRaw);
        }
    }
    const auto bindings = this->collectResolvedArticulationSlots(this->instrumentSlot, this->lastRoutingContext);
    this->applyResolvedRouting(bindings, this->instrumentSlot, this->lastRoutingContext, true, true);
    this->rebuildArtLookupCache();
}



void InstrumentPlayer::buildCcMappings(const ArticulationSlot& articulationSlot, KitLib::ArticulationSpec* artSpec, ArtRtIdx artIdx, CcDimensionsMap& foundCcDimensions) {

    { // Debug: report articulation and its expression dimensions
        if (LOG_ENABLED(Debug)) {
            auto code = artSpec->codeRaw;
            LOG(Debug, "buildCcMappings: processing artSpec=" + code + " nameId=" + juce::String((int)artSpec->nameId) + " exprId=" + juce::String((int)artSpec->exprId) + " articulationSlot=" + articulationSlot.codeRaw);
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
        if (containsSameValue(cc.notes, articulationSlot.notes)) {
            LOG(Debug, "buildCcMappings: controller " + juce::String((int)cc.number) + " {num=" + juce::String((int)cc.number) + ",codeIdx=" + juce::String((int)cc.codeIdx) + ", notes=" + midiVectorToString(cc.notes) + "} applies to articulation " + articulationSlot.codeRaw + " (articulationSlot.notes=" + midiVectorToString(articulationSlot.notes) + ")");
            const auto values = this->getEffectiveExpressionValues(*artSpec, cc.codeIdx);
            for (const auto& val : values) {
                const auto ccNum = static_cast<int8_t>(cc.number);
                const auto pair = std::make_pair(static_cast<int8_t>(val), artIdx);
                auto& vec = foundCcDimensions[ccNum];
                // avoid inserting identical (value,artIdx) duplicates
                if (!vectorContains(vec, pair)) {
                    vec.emplace_back(pair);
                    LOG(Debug, "buildCcMappings: cc=" + juce::String((int)ccNum) + " val=" + juce::String((int)val) + " (artIdx=" + juce::String(artIdx) + ") articulationSlot=" + articulationSlot.codeRaw);
                // } else {
                //     LOG(Warning, "buildCcMappings: skipping duplicate mapping cc=" + juce::String((int)ccNum) + " val=" + juce::String((int)val) + " (artIdx=" + juce::String(artIdx) + ") articulationSlot=" + articulationSlot.codeRaw);
                // }
                }
            }
        }
    }
}

void InstrumentPlayer::processCcDimensions(CcDimensionsMap& foundCcDimensions) {
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

void InstrumentPlayer::rebuildRoutingFromLayout(const KitLayout::InstrumentSlot& layoutSlot) {
    this->instrumentSlot = layoutSlot;
    this->resetRoutingTables();

    LOG(Info, "Rebuilding routing from layout for slot '" + layoutSlot.id + "' with " + juce::String((int)layoutSlot.articulations.size()) + " articulations");

    RoutingContext routingContext = this->lastRoutingContext;
    if (!routingContext.isValid())
        routingContext.defaultInstrument = this->instrumentSpecstPtr;

    const auto bindings = this->collectResolvedArticulationSlots(layoutSlot, routingContext);
    this->applyResolvedRouting(bindings, layoutSlot, routingContext, false, false);
}

bool InstrumentPlayer::updateMicRoute(const juce::String& micCode, const juce::String& outputId) {
    const auto normalizedMicCode = micCode.trim();
    if (normalizedMicCode.isEmpty()) return false;

    const MicLayout::Mic* matchingLayoutMic = nullptr;
    int bestMicPriority = -1;
    if (const auto* micLayout = this->getMicLayout()) {
        for (const auto& mic : micLayout->mics) {
            if (mic.code.compareIgnoreCase(normalizedMicCode) != 0) continue;
            const auto selector = parseMicInstrumentSelector(mic.instrument, this->getEngine()->getInstrumentCatalog());
            const int priority = static_cast<int>(matchMicInstrumentSelector(selector, this->getId(), this->getType(), this->getEngine()->getInstrumentCatalog()));
            if (priority > bestMicPriority) {
                matchingLayoutMic = &mic;
                bestMicPriority = priority;
            }
        }
    }

    std::vector<SampledSound*> matchingSounds;
    std::vector<juce::String> matchingMicCodes;
    for (const auto& sound : this->sampledSoundPtrs) {
        if (sound == nullptr || sound->sampleSpec == nullptr) continue;
        const auto& micSpec = sound->sampleSpec->micSpec;
        const bool codeMatches = micSpec.raw.compareIgnoreCase(normalizedMicCode) == 0;
        const bool specsMatch = matchingLayoutMic != nullptr && micSpec.type == matchingLayoutMic->specs.type && micSpec.position == matchingLayoutMic->specs.position && micSpec.format == matchingLayoutMic->specs.format;
        if (!codeMatches && !specsMatch) continue;
        matchingSounds.push_back(sound.get());
        bool codeAlreadyAdded = false;
        for (const auto& matchingCode : matchingMicCodes) {
            if (matchingCode.compareIgnoreCase(micSpec.raw) == 0) {
                codeAlreadyAdded = true;
                break;
            }
        }
        if (!codeAlreadyAdded) matchingMicCodes.push_back(micSpec.raw);
    }
    if (matchingSounds.empty()) return false;

    const auto oldOverrides = this->lastRoutingContext.micRouteOverrides;
    auto newOverrides = oldOverrides;
    std::erase_if(newOverrides, [&normalizedMicCode, &matchingMicCodes](const auto& route) {
        if (route.first.compareIgnoreCase(normalizedMicCode) == 0) return true;
        for (const auto& matchingCode : matchingMicCodes)
            if (route.first.compareIgnoreCase(matchingCode) == 0) return true;
        return false;
    });
    const auto normalizedOutputId = outputId.trim();
    if (normalizedOutputId.isNotEmpty()) {
        for (const auto& matchingCode : matchingMicCodes)
            newOverrides[matchingCode] = normalizedOutputId;
    }
    this->lastRoutingContext.setMicRouteOverrides(newOverrides);

    if (normalizedOutputId == "-") {
        for (auto* sound : matchingSounds) {
            sound->micBufferChannelStart = -1;
            sound->micBufferChannelCount = sound->sampleSpec->fileChannelCount > 0 ? sound->sampleSpec->fileChannelCount : sound->sampleSpec->micSpec.channels;
            sound->routedMicCode = sound->sampleSpec->micSpec.raw;
            sound->routedMicCodeId = StringsDictSingleton::getId(sound->routedMicCode);
        }
        return true;
    }

    MicRoutingBuilder micRouting(this);
    const auto* firstMatchingSound = matchingSounds.front();
    const int fileChannelCount = firstMatchingSound->sampleSpec->fileChannelCount > 0 ? firstMatchingSound->sampleSpec->fileChannelCount : firstMatchingSound->sampleSpec->micSpec.channels;
    const auto assignment = micRouting.resolve(firstMatchingSound->sampleSpec->micSpec, fileChannelCount);
    if (assignment.globalStart < 0) {
        this->lastRoutingContext.setMicRouteOverrides(oldOverrides);
        return false;
    }

    for (auto* sound : matchingSounds) {
        sound->micBufferChannelStart = assignment.globalStart;
        sound->micBufferChannelCount = assignment.channels;
        sound->routedMicCode = assignment.micCode;
        sound->routedMicCodeId = StringsDictSingleton::getId(assignment.micCode);
        const int sourceChannelCount = sound->sampleSpec->fileChannelCount > 0 ? sound->sampleSpec->fileChannelCount : sound->sampleSpec->micSpec.channels;
        const auto* layerParameters = this->getLayerParameters(static_cast<ArtRtIdx>(sound->runtimeArticulationIndex));
        const int layerPan = layerParameters != nullptr ? layerParameters->pan : 0;
        InstrumentPlayer::configureSoundMicPlacement(*sound, sound->sampleSpec->micSpec, sourceChannelCount, assignment.channels, assignment.applyBalance, sound->placementBalance, layerPan);
    }
    return true;
}
