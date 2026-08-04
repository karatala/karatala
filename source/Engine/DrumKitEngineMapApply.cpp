#include "DrumKitEngine.h"
#include "ArticulationNames.h"
#include "InstrumentMap/InstrumentMapOperations.h"
#include "InstrumentPlayer.h"
#include "SampleSelector.h"
#include "../Config/Helpers.h"
#include "../Config/PluginLogger.h"
#include "../Config/YamlHelper.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

struct DrumKitEngine::InstrumentMapApplyPlan {
    struct PreparedShadow {
        std::unique_ptr<InstrumentPlayer> instrumentPlayer;
        juce::String stateFingerprint;
        juce::String reloadFingerprint;
        std::vector<std::pair<int, InstrumentPlayer*>> noteRoutes;
    };

    struct PreparedShadowParameterUpdate {
        InstrumentPlayer* instrumentPlayer = nullptr;
        LayerParameterUpdate update;
        juce::String stateFingerprint;
    };

    struct PreparedSlot {
        juce::String slotId;
        int primaryIndex = -1;
        std::unique_ptr<InstrumentPlayer> primaryReplacement;
        std::optional<LayerParameterUpdate> primaryParameterUpdate;
        std::vector<std::pair<uint32_t, uint32_t>> primaryParameterRoutes;
        bool removeOldShadows = false;
        bool rebuildShadows = false;
        std::vector<PreparedShadow> shadows;
        std::vector<PreparedShadowParameterUpdate> shadowParameterUpdates;
    };

    std::vector<PreparedSlot> preparedSlots;
    std::unordered_map<juce::String, juce::String> nextSlotFingerprints;
    std::unordered_map<juce::String, juce::String> nextSlotReloadFingerprints;
    juce::String normalizedMapText;
    int preparedPlayerCount = 0;
};

struct DrumKitEngine::InstrumentMapCommitResult {
    std::vector<std::unique_ptr<InstrumentPlayer>> retiredPlayers;
    int appliedPlayerCount = 0;
    size_t totalPlayerCount = 0;
};

static juce::String firstCandidateOrRaw(const std::vector<juce::String>& candidates, const juce::String& raw) {
    for (const auto& candidate : candidates)
        if (candidate.trim().isNotEmpty()) return candidate.trim();
    return raw.trim();
}

static void appendUniqueId(std::vector<uint32_t>& values, uint32_t value) {
    if (!vectorContains(values, value)) values.push_back(value);
}

static juce::StringArray collectLibraryArticulationNames(const KitLib::InstrumentSpec& libraryInstrument) {
    juce::StringArray availableNames;
    for (const auto& articulationSpec : libraryInstrument.articulationSpecs) {
        if (articulationSpec == nullptr) continue;

        const auto libraryArticulationName = ArticulationNames::baseArticulationName(articulationSpec->codeRaw);
        if (libraryArticulationName.isEmpty()) continue;
        if (!ArticulationNames::stringArrayContainsIgnoreCase(availableNames, libraryArticulationName)) availableNames.add(libraryArticulationName);
    }
    availableNames.sort(true);
    return availableNames;
}

static int countKitIdMatches(const KitLib::LibraryManager& libraryManager, const juce::String& id) {
    int matchCount = 0;
    for (const auto& kitPtr : libraryManager.getKits()) {
        if (kitPtr == nullptr) continue;
        if (KitLib::matchesIdentityCandidate(KitLib::LibraryManager::getIdentityCandidates(kitPtr->meta), id)) ++matchCount;
    }
    return matchCount;
}

static int countInstrumentIdMatches(const KitLib::DrumKitSpec& kitSpec, const juce::String& id) {
    int matchCount = 0;
    for (const auto& [instrumentKey, instrumentPtr] : kitSpec.getInstrumentSpecsMap()) {
        juce::ignoreUnused(instrumentKey);
        if (instrumentPtr == nullptr) continue;
        if (KitLib::matchesIdentityCandidate(KitLib::InstrumentSpec::getIdentityCandidates(instrumentPtr->meta), id)) ++matchCount;
    }
    return matchCount;
}

static int countArticulationIdMatches(const KitLib::InstrumentSpec& instrumentSpec, const juce::String& id) {
    int matchCount = 0;
    for (const auto& articulationPtr : instrumentSpec.articulationSpecs) {
        if (articulationPtr == nullptr) continue;
        if (KitLib::matchesIdentityCandidate(articulationPtr->getIdentityCandidates(), id)) ++matchCount;
    }
    return matchCount;
}

static juce::String describeKitResolveFailure(const KitLib::LibraryManager& libraryManager, const juce::String& kitRef, const juce::String& slotId) {
    const auto matchCount = countKitIdMatches(libraryManager, kitRef);
    const auto reason = matchCount > 1 ? "Library kit ID is ambiguous" : "Library kit was not found";
    return juce::String(reason) + ": " + kitRef + " for slot '" + slotId + "'";
}

static juce::String describeInstrumentResolveFailure(const KitLib::DrumKitSpec& kitSpec, const juce::String& instCode, const juce::String& slotId) {
    const auto matchCount = countInstrumentIdMatches(kitSpec, instCode);
    const auto reason = matchCount > 1 ? "Library instrument ID is ambiguous" : "Library instrument was not found";
    return juce::String(reason) + ": " + kitSpec.meta.codeRaw + "/" + instCode + " for slot '" + slotId + "'";
}

static juce::String describeArticulationResolveFailure(const KitLib::InstrumentSpec& instrumentSpec, const juce::String& artCode, const juce::String& libraryInstrumentPath, const juce::String& slotId) {
    const auto matchCount = countArticulationIdMatches(instrumentSpec, artCode);
    const auto reason = matchCount > 1 ? "Library articulation ID is ambiguous" : "Unknown library articulation";
    return juce::String(reason) + ": " + artCode + " in library instrument '" + libraryInstrumentPath + "' for slot '" + slotId + "'";
}

static void normalizeLibraryArticulationFilters(DrumKitEngine::InstrumentMapLayer& mapLayer, const KitLib::InstrumentSpec& instrumentSpec) {
    for (auto& libraryArticulationName : mapLayer.articulationFilter) {
        auto* articulationSpec = instrumentSpec.findArticulationById(libraryArticulationName);
        if (articulationSpec != nullptr) libraryArticulationName = instrumentSpec.getPreferredArticulationId(*articulationSpec);
    }
}

static void normalizeInstrumentMapLayers(DrumKitEngine::InstrumentMap& instrumentMap, KitLib::LibraryManager& libraryManager) {
    for (auto& mapLayer : instrumentMap.layers) {
        auto* kitSpec = libraryManager.findKitById(mapLayer.kitRef);
        if (kitSpec == nullptr) continue;

        mapLayer.kitRef = libraryManager.getPreferredKitId(*kitSpec);
        if (mapLayer.instCode.isEmpty()) continue;

        auto* instSpec = kitSpec->findInstrumentById(mapLayer.instCode);
        if (instSpec == nullptr) continue;

        mapLayer.instCode = kitSpec->getPreferredInstrumentId(*instSpec);
        normalizeLibraryArticulationFilters(mapLayer, *instSpec);
    }
}

static juce::String buildSuggestedInstrumentMapText(const KitLayout* layout, const std::unordered_map<juce::String, InstrumentPlayer*>& instrumentPlayerById, const ArticulationSlotFallbackRules& fallbackRules, juce::StringArray* warnings) {
    if (layout == nullptr)
        return {};

    ::InstrumentMap::Map instrumentMap;
    for (const auto& slot : layout->instrumentSlots) {
        auto slotIt = instrumentPlayerById.find(slot.id);
        if (slotIt == instrumentPlayerById.end())
            continue;

        const auto* loadedInstrument = slotIt->second->getLoadedInstrumentSpec();
        if (loadedInstrument == nullptr || loadedInstrument->parent == nullptr)
            continue;

        auto mapLayers = ::InstrumentMap::buildLayers(slot, *loadedInstrument->parent, *loadedInstrument, loadedInstrument->parent->meta.codeRaw, fallbackRules);
        for (const auto& warning : mapLayers.errors) {
            LOG(Warning, warning);
            if (warnings != nullptr) warnings->add(warning);
        }
        for (auto& mapLayer : mapLayers.value) instrumentMap.layers.push_back(std::move(mapLayer));
    }

    return ::InstrumentMap::serialize(instrumentMap);
}

void DrumKitEngine::mapDefaultKit() {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    this->lastInstrumentMapStatus.clear();
    const auto& kits = this->getInstrumentManager()->getKits();
    const KitLib::DrumKitSpec* kit = kits.empty() ? nullptr : kits[0].get(); //MAYBE: later use global setting for default kit instead
    if (kit != nullptr) {
        this->mapKit(*kit);
        this->regenerateSuggestedInstrumentMapText();
        auto parsedSuggestedMap = DrumKitEngine::parseInstrumentMapText(this->suggestedInstrumentMapText);
        if (parsedSuggestedMap.ok() && this->libraryManagerPtr != nullptr && this->activeKitLayoutPtr != nullptr) {
            ::InstrumentMap::applyAutomaticOrientation(parsedSuggestedMap.value, *this->activeKitLayoutPtr, *this->libraryManagerPtr);
            const auto normalizedSuggestedMapText = DrumKitEngine::serializeInstrumentMap(parsedSuggestedMap.value);
            this->lastAppliedSlotFingerprints = ::InstrumentMap::buildResolvedSlotFingerprintMap(parsedSuggestedMap.value, *this->libraryManagerPtr);
            this->lastAppliedSlotReloadFingerprints = ::InstrumentMap::buildResolvedSlotReloadFingerprintMap(parsedSuggestedMap.value, *this->libraryManagerPtr);
            this->lastAppliedShadowFingerprints.clear();
            this->lastAppliedShadowReloadFingerprints.clear();
            this->lastAppliedInstrumentMapText = normalizedSuggestedMapText;
        } else {
            this->lastAppliedSlotFingerprints.clear();
            this->lastAppliedSlotReloadFingerprints.clear();
            this->lastAppliedShadowFingerprints.clear();
            this->lastAppliedShadowReloadFingerprints.clear();
            this->lastAppliedInstrumentMapText.clear();
        }
        const auto fallbackWarnings = this->lastInstrumentMapStatus;
        this->lastInstrumentMapStatus = "Mapped default library kit and generated starter instrument map";//FIXME: refactor, ui relates shit out
        if (fallbackWarnings.isNotEmpty()) this->lastInstrumentMapStatus += "\n" + fallbackWarnings;
    } else {
        this->suggestedInstrumentMapText.clear();
        this->lastAppliedSlotFingerprints.clear();
        this->lastAppliedSlotReloadFingerprints.clear();
        this->lastAppliedShadowFingerprints.clear();
        this->lastAppliedShadowReloadFingerprints.clear();
        this->lastAppliedInstrumentMapText.clear();
    }
}

juce::String DrumKitEngine::buildInstrumentMapForKit(const KitLayout& layout, const KitLib::DrumKitSpec& libraryKit, juce::StringArray* warnings) const {
    InstrumentMap result;
    std::unordered_set<KitLib::InstrumentSpec*> usedLibraryInstruments;
    std::map<KitLib::InstrumentSpec::Type, int> nextIndexByType;

    for (const auto& slot : layout.instrumentSlots) {
        auto* matchedLibraryInstrument = ::InstrumentMap::findMatchingLibraryInstrument(slot, libraryKit, usedLibraryInstruments, nextIndexByType);
        if (matchedLibraryInstrument == nullptr) continue;
        const auto preferredLibraryKitId = this->libraryManagerPtr != nullptr
            ? this->libraryManagerPtr->getPreferredKitId(libraryKit)
            : firstCandidateOrRaw(KitLib::DrumKitSpec::getIdentityCandidates(libraryKit.meta), libraryKit.meta.codeRaw);
        auto mapLayers = ::InstrumentMap::buildLayers(slot, libraryKit, *matchedLibraryInstrument, preferredLibraryKitId, this->globalConfig.articulationSlotFallbackRules);
        for (const auto& warning : mapLayers.errors) {
            LOG(Warning, warning);
            if (warnings != nullptr) warnings->add(warning);
        }
        result.layers.insert(result.layers.end(), std::make_move_iterator(mapLayers.value.begin()), std::make_move_iterator(mapLayers.value.end()));
    }

    return serializeInstrumentMap(result);
}

ParseResult<juce::String> DrumKitEngine::buildInstrumentMapWithSlotInstrument(const juce::String& currentMapText, const juce::String& slotId, const juce::String& libraryKitId, const juce::String& libraryInstrumentId) const {
    ParseResult<juce::String> result;
    if (this->libraryManagerPtr == nullptr) return result.returnWithError("Sample library is not loaded");
    if (this->activeKitLayoutPtr == nullptr) return result.returnWithError("Kit layout is not loaded");

    const auto slotIterator = std::find_if(this->activeKitLayoutPtr->instrumentSlots.begin(), this->activeKitLayoutPtr->instrumentSlots.end(), [&slotId](const auto& slot) {
        return slot.id.compareIgnoreCase(slotId) == 0;
    });
    if (slotIterator == this->activeKitLayoutPtr->instrumentSlots.end()) return result.returnWithError("Instrument slot was not found: " + slotId);

    auto* libraryKit = this->libraryManagerPtr->findKitById(libraryKitId);
    if (libraryKit == nullptr) return result.returnWithError("Library kit was not found: " + libraryKitId);
    auto* libraryInstrument = libraryKit->findInstrumentById(libraryInstrumentId);
    if (libraryInstrument == nullptr) return result.returnWithError("Library instrument was not found: " + libraryInstrumentId);

    InstrumentMap map;
    if (currentMapText.trim().isNotEmpty()) {
        auto parsedMap = DrumKitEngine::parseInstrumentMapText(currentMapText);
        if (!parsedMap.ok()) return result.returnWithError(parsedMap.error());
        map = std::move(parsedMap.value);
    }

    const auto preferredLibraryKitId = this->libraryManagerPtr->getPreferredKitId(*libraryKit);
    auto replacementMapLayers = ::InstrumentMap::buildLayers(*slotIterator, *libraryKit, *libraryInstrument, preferredLibraryKitId, this->globalConfig.articulationSlotFallbackRules);
    for (const auto& warning : replacementMapLayers.errors) {
        LOG(Warning, warning);
        result.addWarning(warning);
    }
    if (replacementMapLayers.value.empty()) return result.returnWithError("Library instrument has no articulations that can be mapped to slot '" + slotIterator->id + "'");
    ::InstrumentMap::replaceSlotLayers(map, slotIterator->id, std::move(replacementMapLayers.value));
    result.value = DrumKitEngine::serializeInstrumentMap(map);
    return result;
}

/** Writes instrument map text in normalized slot/layer order with semicolon-separated params. */
juce::String DrumKitEngine::serializeInstrumentMap(const InstrumentMap& instrumentMap) {
    return ::InstrumentMap::serialize(instrumentMap);
}

void DrumKitEngine::regenerateSuggestedInstrumentMapText() { //TODO: refactor, move to ui related helper, or generate on demand in UI instead of storing in engine
    juce::StringArray warnings;
    this->suggestedInstrumentMapText = this->normalizeInstrumentMapText(buildSuggestedInstrumentMapText(this->activeKitLayoutPtr.get(), this->instrumentPlayerById, this->globalConfig.articulationSlotFallbackRules, &warnings));
    if (!warnings.isEmpty()) this->lastInstrumentMapStatus = warnings.joinIntoString("\n");
}

juce::String DrumKitEngine::normalizeInstrumentMapText(const juce::String& text) const {
    auto parsed = DrumKitEngine::parseInstrumentMapText(text);
    if (!parsed.ok()) return text;
    if (this->libraryManagerPtr != nullptr)
        normalizeInstrumentMapLayers(parsed.value, *this->libraryManagerPtr);
    return DrumKitEngine::serializeInstrumentMap(parsed.value);
}

void DrumKitEngine::mapKit(const KitLib::DrumKitSpec& kit) { //FIXME: review, probably rewrite, simplify
    if (this->activeKitLayoutPtr == nullptr) {
        LOG(Warning, "no active kit layout to map to");
        return;
    }

    auto librarySnapshot = this->libraryManagerPtr;
    std::unordered_set<KitLib::InstrumentSpec*> usedInstSpecSet;
    std::map<KitLib::InstrumentSpec::Type, int> nextIndexByType;


    for (const auto& layoutInstrument : this->activeKitLayoutPtr->instrumentSlots) { //FIXME: rewrite loop thru instrumentPlayerById
        auto sit = this->instrumentPlayerById.find(layoutInstrument.id);
        if (sit == this->instrumentPlayerById.end()) {
            LOG(Warning, "layout instrument slot not present: " + layoutInstrument.id);
            continue;
        }
        InstrumentPlayer* slot = sit->second;

        auto* matched = ::InstrumentMap::findMatchingLibraryInstrument(layoutInstrument, kit, usedInstSpecSet, nextIndexByType);

        if (matched == nullptr) {
            LOG(Warning, "no matching instrument found in kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");
            slot->unloadInstrument();
            continue;
        }

        LOG(Info, "Loading instrument '" + matched->meta.codeRaw + "' from kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");

        slot->loadInstrument(matched, librarySnapshot); //TODO: wrap in performance measuring block
    }

    if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
        for (const auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
            instrumentPlayerPtr->setCurrentPlaybackSampleRate(this->lastSampleRate);
            instrumentPlayerPtr->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
        }
    }

    // Start staged sample loading only after all instruments/articulations were enqueued
    if (this->sampleLoaderPtr != nullptr) {
        this->sampleLoaderPtr->getQueue().startProcessing();
    }
}

void DrumKitEngine::updateSlotParameters(const juce::String& slotId, const juce::String& artName, int layerIndex, std::optional<int> tuningCents, std::optional<int> attackMs, std::optional<int> decayMs) {
    LayerParameterUpdate update;
    update.tuningCents = tuningCents;
    update.attackMs = attackMs;
    update.decayMs = decayMs;
    this->updateSlotParameters(slotId, artName, layerIndex, update);
}

bool DrumKitEngine::updateMappedLibraryArticulationParameters(const juce::String& slotId, const juce::String& artName, const juce::String& qualifiedLibraryArticulationId, int libraryArticulationOccurrenceIndex, const LayerParameterUpdate& update) {
    if (libraryArticulationOccurrenceIndex < 0) return false;

    juce::StringArray idParts;
    idParts.addTokens(qualifiedLibraryArticulationId.trim(), "/", "");
    idParts.removeEmptyStrings();
    if (idParts.size() < 2) return false;

    const auto targetKit = idParts[0].trim();
    const auto targetInst = idParts[1].trim();
    const auto targetArt = idParts.size() >= 3 ? idParts[2].trim() : juce::String();

    const auto targetRoutes = this->getTargetRoutesForArticulationFilter(slotId, artName);
    auto librarySnapshot = this->libraryManagerPtr;
    if (librarySnapshot == nullptr) return false;
    auto* targetKitSpec = librarySnapshot->findKitById(targetKit);
    if (targetKitSpec == nullptr) return false;
    auto* targetInstrumentSpec = targetKitSpec->findInstrumentById(targetInst);
    if (targetInstrumentSpec == nullptr) return false;
    const auto* targetArticulationSpec = targetArt.isNotEmpty() ? targetInstrumentSpec->findArticulationById(targetArt) : nullptr;
    if (targetArt.isNotEmpty() && targetArticulationSpec == nullptr) return false;

    const juce::ScopedLock graphLock(this->engineGraphLock);

    int currentOccurrence = 0;
    for (auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
        if (instrumentPlayerPtr == nullptr) continue;
        if (instrumentPlayerPtr->getId() != slotId) continue;
        if (!DrumKitEngine::playerMatchesTargetRoutes(*instrumentPlayerPtr, targetRoutes) || !instrumentPlayerPtr->routesLibraryArticulation(*targetInstrumentSpec, targetArticulationSpec)) continue;

        if (currentOccurrence == libraryArticulationOccurrenceIndex) {
            return instrumentPlayerPtr->updateArticulationParameters(targetRoutes, update);
        }

        ++currentOccurrence;
    }

    return false;
}
void DrumKitEngine::updateSlotParameters(const juce::String& slotId, const juce::String& artName, int layerIndex, const LayerParameterUpdate& update) {
    if (layerIndex < 0)
        return;

    const auto targetRoutes = this->getTargetRoutesForArticulationFilter(slotId, artName);

    const juce::ScopedLock graphLock(this->engineGraphLock);

    int currentLayerIndex = 0;
    for (auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
        if (instrumentPlayerPtr == nullptr) continue;
        if (instrumentPlayerPtr->getId() != slotId) continue;
        if (!DrumKitEngine::playerMatchesTargetRoutes(*instrumentPlayerPtr, targetRoutes)) continue;

        if (currentLayerIndex == layerIndex) {
            instrumentPlayerPtr->updateArticulationParameters(targetRoutes, update);
            return;
        }

        ++currentLayerIndex;
    }
}

bool DrumKitEngine::updateSlotParametersBatch(const std::vector<LayerParameterTarget>& targets) {
    struct PreparedTarget {
        const LayerParameterTarget* target = nullptr;
        std::vector<std::pair<uint32_t, uint32_t>> routes;
    };

    std::vector<PreparedTarget> preparedTargets;
    preparedTargets.reserve(targets.size());
    for (const auto& target : targets) {
        if (target.layerIndex < 0 || target.slotId.trim().isEmpty()) continue;
        preparedTargets.push_back({ &target, this->getTargetRoutesForArticulationFilter(target.slotId, target.articulationName) });
    }
    if (preparedTargets.empty()) return false;

    const juce::ScopedLock graphLock(this->engineGraphLock);
    bool updated = false;
    for (const auto& preparedTarget : preparedTargets) {
        int currentLayerIndex = 0;
        for (auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
            if (instrumentPlayerPtr == nullptr || instrumentPlayerPtr->getId() != preparedTarget.target->slotId || !DrumKitEngine::playerMatchesTargetRoutes(*instrumentPlayerPtr, preparedTarget.routes)) continue;
            if (currentLayerIndex == preparedTarget.target->layerIndex) {
                updated = instrumentPlayerPtr->updateArticulationParameters(preparedTarget.routes, preparedTarget.target->update) || updated;
                break;
            }
            ++currentLayerIndex;
        }
    }
    return updated;
}

bool DrumKitEngine::updateSlotMicRoutesBatch(const std::vector<MicRouteTarget>& targets) {
    struct PreparedTarget {
        const MicRouteTarget* target = nullptr;
        std::vector<std::pair<uint32_t, uint32_t>> routes;
    };

    std::vector<PreparedTarget> preparedTargets;
    preparedTargets.reserve(targets.size());
    for (const auto& target : targets) {
        if (target.layerIndex < 0 || target.slotId.trim().isEmpty() || target.micCode.trim().isEmpty()) continue;
        preparedTargets.push_back({ &target, this->getTargetRoutesForArticulationFilter(target.slotId, target.articulationName) });
    }
    if (preparedTargets.empty()) return false;

    const juce::ScopedLock graphLock(this->engineGraphLock);
    bool updated = false;
    for (const auto& preparedTarget : preparedTargets) {
        int currentLayerIndex = 0;
        for (auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
            if (instrumentPlayerPtr == nullptr || instrumentPlayerPtr->getId() != preparedTarget.target->slotId || !DrumKitEngine::playerMatchesTargetRoutes(*instrumentPlayerPtr, preparedTarget.routes)) continue;
            if (currentLayerIndex == preparedTarget.target->layerIndex) {
                updated = instrumentPlayerPtr->updateMicRoute(preparedTarget.target->micCode, preparedTarget.target->outputId) || updated;
                break;
            }
            ++currentLayerIndex;
        }
    }
    return updated;
}

std::vector<std::pair<uint32_t, uint32_t>> DrumKitEngine::getTargetRoutesForArticulationFilter(const juce::String& slotId, const juce::String& artName) const {
    std::vector<std::pair<uint32_t, uint32_t>> targetRoutes;
    const bool hasArtFilter = artName.trim().isNotEmpty() && artName.trim() != "(default)";
    if (!hasArtFilter || this->activeKitLayoutPtr == nullptr) return targetRoutes;

    for (const auto& slot : this->activeKitLayoutPtr->instrumentSlots) {
        if (slot.id != slotId) continue;
        for (const auto& art : slot.articulations) {
            if (!ArticulationNames::mapFilterMatchesArticulation(artName, art.codeRaw)) continue;
            const auto route = std::make_pair(art.nameId, art.exprId);
            if (std::find(targetRoutes.begin(), targetRoutes.end(), route) == targetRoutes.end()) targetRoutes.push_back(route);
        }
        break;
    }
    return targetRoutes;
}

bool DrumKitEngine::playerMatchesTargetRoutes(const InstrumentPlayer& instrumentPlayer, const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes) {
    if (targetRoutes.empty()) return true;
    if (!instrumentPlayer.hasArticulationSlotRoutes()) return false;

    for (const auto& route : targetRoutes) {
        if (instrumentPlayer.routesArticulationSlot(route.first, route.second)) return true;
    }
    return false;
}

bool DrumKitEngine::playerRoutesArticulationSlot(const InstrumentPlayer& instrumentPlayer, const KitLayout::InstrumentSlot::ArticulationSlot& art) {
    return instrumentPlayer.routesArticulationSlot(art.nameId, art.exprId);
}

void DrumKitEngine::applyLayerParameterUpdate(InstrumentPlayer& instrumentPlayer, const LayerParameterUpdate& update) {
    if (update.tuningCents.has_value()) instrumentPlayer.setTuningOffsetCents(*update.tuningCents);
    if (update.attackMs.has_value()) instrumentPlayer.setAttackOverrideMs(*update.attackMs);
    if (update.decayMs.has_value()) instrumentPlayer.setDecayOverrideMs(*update.decayMs);
    if (update.volumeDb.has_value()) instrumentPlayer.setVolumeDb(*update.volumeDb);
    if (update.pan.has_value()) instrumentPlayer.setPan(*update.pan);
    if (update.flipLR.has_value()) instrumentPlayer.setFlipLR(*update.flipLR);
    if (update.flipPhase.has_value()) instrumentPlayer.setFlipPhase(*update.flipPhase);
    if (update.micBleedLevels.has_value()) instrumentPlayer.setMicBleedLevels(*update.micBleedLevels);
}

void DrumKitEngine::applyPreparedMapLayerUpdate(InstrumentPlayer& instrumentPlayer, const LayerParameterUpdate& update, const std::vector<std::pair<uint32_t, uint32_t>>& targetRoutes) {
    if (!targetRoutes.empty()) {
        instrumentPlayer.updateArticulationParameters(targetRoutes, update);
        return;
    }

    DrumKitEngine::applyLayerParameterUpdate(instrumentPlayer, update);
    LayerParameterUpdate velocityResponseUpdate;
    velocityResponseUpdate.updateVelocityResponse = update.updateVelocityResponse;
    velocityResponseUpdate.velocityResponse = update.velocityResponse;
    instrumentPlayer.updateArticulationParameters({}, velocityResponseUpdate);
}

bool DrumKitEngine::useInstrumentMap(juce::String text) {
    return this->useInstrumentMapInternal(std::move(text), {});
}

bool DrumKitEngine::applyInstrumentMapSlotChange(const juce::String& fullMapText, const juce::String& changedSlotId) {
    return this->useInstrumentMapInternal(fullMapText, changedSlotId);
}

bool DrumKitEngine::useInstrumentMapInternal(juce::String text, const juce::String& changedSlotIdHint) {
    // ======================== PHASE 1: VALIDATION (no lock) ========================
    // Safe to read libraryManagerPtr/activeKitLayoutPtr without lock - only mutated on
    // the message thread, which is our current thread.
    this->lastInstrumentMapStatus.clear();

    if (text.trim().isEmpty()) {
        this->lastInstrumentMapStatus = "Instrument map is empty";
        LOG(Warning, this->lastInstrumentMapStatus);
        return false;
    }

    auto librarySnapshot = this->libraryManagerPtr;
    if (librarySnapshot == nullptr) {
        this->lastInstrumentMapStatus = "LibraryManager is not initialized";
        LOG(Error, this->lastInstrumentMapStatus);
        return false;
    }

    if (this->activeKitLayoutPtr == nullptr) {
        this->lastInstrumentMapStatus = "Cannot apply instrument map without an active kit layout";
        LOG(Error, this->lastInstrumentMapStatus);
        return false;
    }

    const auto changedSlotKey = changedSlotIdHint.trim().toLowerCase();
    const bool hasChangedSlotHint = changedSlotKey.isNotEmpty();

    // ======================== PHASE 2: PARSE & RESOLVE (no lock) ========================
    // All parsing, kit/instrument resolution, and instrument player creation happens here.
    // The audio thread continues to process the old instrument player graph.

    auto parsed = DrumKitEngine::parseInstrumentMapText(text);
    for (const auto& warning : parsed.errors)
        LOG(parsed.ok() ? Warning : Error, warning);

    if (!parsed.ok()) {
        this->lastInstrumentMapStatus = parsed.error();
        return false;
    }

    normalizeInstrumentMapLayers(parsed.value, *librarySnapshot);
    ::InstrumentMap::applyAutomaticOrientation(parsed.value, *this->activeKitLayoutPtr, *librarySnapshot);
    const auto normalizedMapText = DrumKitEngine::serializeInstrumentMap(parsed.value);

    LOG(Info, "=== useInstrumentMap: " + juce::String(parsed.value.layers.size()) + " map layers ===");
    for (size_t idx = 0; idx < parsed.value.layers.size(); ++idx) {
        const auto& mapLayer = parsed.value.layers[idx];
        juce::String articulationSlotText, libraryArticulationText;
        for (const auto& articulationSlotName : mapLayer.slotArticulationFilter) articulationSlotText += (articulationSlotText.isEmpty() ? "" : ",") + articulationSlotName;
        for (const auto& libraryArticulationName : mapLayer.articulationFilter) libraryArticulationText += (libraryArticulationText.isEmpty() ? "" : ",") + libraryArticulationName;
        juce::String routeStr;
        for (const auto& [micCode, outputId] : mapLayer.micRouteOverrides)
            routeStr += (routeStr.isEmpty() ? "" : ",") + micCode + "->" + outputId;
        LOG(Info, "  mapLayer[" + juce::String(idx) + "]: slot=" + mapLayer.slotId
            + (articulationSlotText.isNotEmpty() ? "/" + articulationSlotText : "")
            + " -> kit=" + mapLayer.kitRef + " inst=" + mapLayer.instCode
            + (libraryArticulationText.isNotEmpty() ? " art=" + libraryArticulationText : "")
            + (mapLayer.tuningCents.has_value() ? " tuning=" + juce::String(*mapLayer.tuningCents) : "")
            + (mapLayer.attackTimeMs.has_value() ? " attack=" + juce::String(*mapLayer.attackTimeMs) : "")
            + (mapLayer.decayTimeMs.has_value() ? " decay=" + juce::String(*mapLayer.decayTimeMs) : "")
            + (routeStr.isNotEmpty() ? " micRoute=[" + routeStr + "]" : ""));
    }

    struct ShadowPlan {
        const InstrumentMapLayer* mapLayer = nullptr;
        KitLib::InstrumentSpec* instSpec = nullptr;
        std::vector<juce::String> articulationSlots;
        InstrumentPlayer::ArticulationNameRemap artNameRemap;
        InstrumentPlayer::ArticulationExactRemap artExactRemap;
    };

    // Build a name remap from the articulation slot and library articulation filters.
    auto buildArtNameRemap = [](const InstrumentMapLayer& mapLayer) -> InstrumentPlayer::ArticulationNameRemap {
        InstrumentPlayer::ArticulationNameRemap remap;
        const auto& articulationSlots = mapLayer.slotArticulationFilter;
        const auto& libraryArticulations = mapLayer.articulationFilter;
        if (articulationSlots.empty() || libraryArticulations.empty())
            return remap;
        for (size_t idx = 0; idx < articulationSlots.size() && idx < libraryArticulations.size(); ++idx) {
            auto articulationSlotId = StringsDictSingleton::getId(ArticulationNames::normalizeArticulationName(articulationSlots[idx]));
            auto libraryArticulationId = StringsDictSingleton::getId(ArticulationNames::normalizeArticulationName(libraryArticulations[idx]));
            if (articulationSlotId != libraryArticulationId)
                remap[articulationSlotId] = libraryArticulationId;
        }
        return remap;
    };

    auto makeArticulationRouteKey = [](uint32_t nameId, uint32_t exprId) {
        return (static_cast<uint64_t>(nameId) << 32) | static_cast<uint64_t>(exprId);
    };

    auto buildArtExactRemap = [&makeArticulationRouteKey](const InstrumentMapLayer& mapLayer) -> InstrumentPlayer::ArticulationExactRemap {
        InstrumentPlayer::ArticulationExactRemap remap;
        const auto& articulationSlots = mapLayer.slotArticulationFilter;
        const auto& libraryArticulations = mapLayer.articulationFilter;
        if (articulationSlots.empty() || libraryArticulations.empty()) return remap;
        for (size_t idx = 0; idx < articulationSlots.size() && idx < libraryArticulations.size(); ++idx) {
            if (articulationSlots[idx].isEmpty() || libraryArticulations[idx].isEmpty()) continue;
            const KitLayout::InstrumentSlot::ArticulationSlot articulationSlot(articulationSlots[idx]);
            const KitLayout::InstrumentSlot::ArticulationSlot libraryArticulation(libraryArticulations[idx]);
            if (articulationSlot.nameId == libraryArticulation.nameId && articulationSlot.exprId == libraryArticulation.exprId) continue;
            remap[makeArticulationRouteKey(articulationSlot.nameId, articulationSlot.exprId)] = { libraryArticulation.nameId, libraryArticulation.exprId };
        }
        return remap;
    };
    auto resolveVelocityResponseOverride = [](const InstrumentMapLayer& mapLayer) -> std::optional<VelocityResponse> {
        if (!mapLayer.velocityResponse.has_value()) return std::nullopt;
        auto parsedResponse = VelocityResponse::fromDefinition(*mapLayer.velocityResponse);
        return parsedResponse.ok() ? std::optional<VelocityResponse>(std::move(parsedResponse.value)) : std::nullopt;
    };
    auto makeParameterUpdate = [&resolveVelocityResponseOverride](const InstrumentMapLayer& mapLayer) {
        LayerParameterUpdate update;
        update.tuningCents = mapLayer.tuningCents.value_or(0);
        update.attackMs = mapLayer.attackTimeMs.value_or(-1);
        update.decayMs = mapLayer.decayTimeMs.value_or(-1);
        update.volumeDb = mapLayer.volumeDb.value_or(0.0f);
        update.pan = mapLayer.pan.value_or(0);
        update.flipLR = mapLayer.flipLR;
        update.flipPhase = mapLayer.flipPhase;
        update.micBleedLevels = mapLayer.micBleedLevels;
        update.updateVelocityResponse = true;
        update.velocityResponse = resolveVelocityResponseOverride(mapLayer);
        return update;
    };

    auto buildTargetRoutes = [](const KitLayout::InstrumentSlot& slotLayout, const std::vector<juce::String>& articulationSlotFilters) {
        std::vector<std::pair<uint32_t, uint32_t>> targetRoutes;
        if (articulationSlotFilters.empty()) return targetRoutes;
        for (const auto& articulationSlot : slotLayout.articulations) {
            for (const auto& articulationSlotFilter : articulationSlotFilters) {
                if (!ArticulationNames::mapFilterMatchesArticulation(articulationSlotFilter, articulationSlot.codeRaw)) continue;
                const auto route = std::make_pair(articulationSlot.nameId, articulationSlot.exprId);
                if (std::find(targetRoutes.begin(), targetRoutes.end(), route) == targetRoutes.end()) targetRoutes.push_back(route);
            }
        }
        return targetRoutes;
    };

    auto makeResolvedMapLayerFingerprint = [](const InstrumentMapLayer& mapLayer, const KitLib::InstrumentSpec& instSpec) {
        return ::InstrumentMap::makeLayerFingerprint(mapLayer) + "\nsource\n" + instSpec.getContentFingerprint();
    };

    auto makeResolvedMapLayerReloadFingerprint = [](const InstrumentMapLayer& mapLayer, const KitLib::InstrumentSpec& instSpec) {
        return ::InstrumentMap::makeLayerReloadFingerprint(mapLayer) + "\nsource\n" + instSpec.getContentFingerprint();
    };

    std::vector<juce::String> slotOrder;
    std::unordered_map<juce::String, std::vector<const InstrumentMapLayer*>> mapLayersBySlotId;
    for (const auto& mapLayer : parsed.value.layers) {
        if (mapLayersBySlotId.find(mapLayer.slotId) == mapLayersBySlotId.end()) slotOrder.push_back(mapLayer.slotId);
        mapLayersBySlotId[mapLayer.slotId].push_back(&mapLayer);
    }

    std::unordered_map<juce::String, juce::String> nextSlotFingerprints = ::InstrumentMap::buildResolvedSlotFingerprintMap(parsed.value, *librarySnapshot);
    std::unordered_map<juce::String, juce::String> nextSlotReloadFingerprints = ::InstrumentMap::buildResolvedSlotReloadFingerprintMap(parsed.value, *librarySnapshot);
    struct ExistingShadow {
        InstrumentPlayer* instrumentPlayer = nullptr;
        juce::String stateFingerprint;
        juce::String reloadFingerprint;
    };

    // Build index maps for fast slot lookup (safe to read on message thread)
    std::unordered_map<juce::String, int> primarySlotIndex;
    std::unordered_map<juce::String, std::vector<ExistingShadow>> existingShadowsBySlotId;
    for (int idx = 0; idx < static_cast<int>(this->instrumentPlayerPtrs.size()); ++idx) {
        auto* instrumentPlayer = this->instrumentPlayerPtrs[static_cast<size_t>(idx)].get();
        if (instrumentPlayer == nullptr) continue;
        if (idx < this->primaryInstrumentPlayerCount) {
            primarySlotIndex[instrumentPlayer->getId()] = idx;
            continue;
        }

        ExistingShadow existingShadow;
        existingShadow.instrumentPlayer = instrumentPlayer;
        auto stateIt = this->lastAppliedShadowFingerprints.find(instrumentPlayer);
        if (stateIt != this->lastAppliedShadowFingerprints.end()) existingShadow.stateFingerprint = stateIt->second;
        auto reloadIt = this->lastAppliedShadowReloadFingerprints.find(instrumentPlayer);
        if (reloadIt != this->lastAppliedShadowReloadFingerprints.end()) existingShadow.reloadFingerprint = reloadIt->second;
        existingShadowsBySlotId[instrumentPlayer->getId()].push_back(std::move(existingShadow));
    }

    juce::StringArray statusLines;
    for (const auto& warning : parsed.errors)
        statusLines.add(warning);

    InstrumentMapApplyPlan applyPlan;

    for (const auto& slotId : slotOrder) {
        const auto& slotMapLayers = mapLayersBySlotId[slotId];
        if (slotMapLayers.empty()) continue;

        auto slotIt = this->instrumentPlayerById.find(slotId);
        if (slotIt == this->instrumentPlayerById.end()) {
            statusLines.add("Instrument slot not found: " + slotId);
            continue;
        }
        auto* primarySlot = slotIt->second;
        const auto& slotLayout = primarySlot->getInstrumentSlot();
        bool slotChanged = !primarySlot->isLoaded();
        const bool slotStateChanged = this->lastAppliedSlotFingerprints[slotId] != nextSlotFingerprints[slotId];
        const bool slotReloadChanged = this->lastAppliedSlotReloadFingerprints[slotId] != nextSlotReloadFingerprints[slotId];
        const bool slotMatchesHint = !hasChangedSlotHint || slotId.trim().toLowerCase() == changedSlotKey;
        if (hasChangedSlotHint && !slotMatchesHint && !slotChanged && !slotStateChanged && !slotReloadChanged) continue;

        struct ResolvedMapLayer {
            const InstrumentMapLayer* mapLayer = nullptr;
            KitLib::DrumKitSpec* kitSpec = nullptr;
            KitLib::InstrumentSpec* instSpec = nullptr;
            std::vector<juce::String> effectiveArticulationSlotFilters;
        };

        std::vector<ResolvedMapLayer> resolvedMapLayers;
        bool slotResolveFailed = false;
        for (const auto* mapLayer : slotMapLayers) {
            const auto articulationSlotFilter = mapLayer->slotArticulationFilter.front();
            const bool articulationSlotExists = std::any_of(slotLayout.articulations.begin(), slotLayout.articulations.end(), [&articulationSlotFilter](const auto& articulationSlot) {
                return ArticulationNames::mapFilterMatchesArticulation(articulationSlotFilter, articulationSlot.codeRaw);
            });
            if (!articulationSlotExists) {
                statusLines.add("Articulation slot '" + articulationSlotFilter + "' was not found in instrument slot '" + slotId + "'");
                slotResolveFailed = true;
                continue;
            }

            auto* kitSpec = librarySnapshot->findKitById(mapLayer->kitRef);
            if (kitSpec == nullptr) {
                statusLines.add(describeKitResolveFailure(*librarySnapshot, mapLayer->kitRef, mapLayer->slotId));
                slotResolveFailed = true;
                continue;
            }

            if (mapLayer->instCode.isEmpty()) {
                statusLines.add("Library instrument ID is required for slot '" + mapLayer->slotId + "'");
                slotResolveFailed = true;
                continue;
            }

            auto* instSpec = kitSpec->findInstrumentById(mapLayer->instCode);
            if (instSpec == nullptr) {
                statusLines.add(describeInstrumentResolveFailure(*kitSpec, mapLayer->instCode, mapLayer->slotId));
                slotResolveFailed = true;
                continue;
            }

            if (!mapLayer->articulationFilter.empty()) {
                const auto availableLibraryArticulations = collectLibraryArticulationNames(*instSpec);
                juce::StringArray unknownLibraryArticulations;
                juce::StringArray unknownLibraryArticulationKeys;

                for (const auto& libraryArticulationName : mapLayer->articulationFilter) {
                    const auto normalizedLibraryArticulationName = ArticulationNames::normalizeArticulationName(libraryArticulationName);
                    if (normalizedLibraryArticulationName.isEmpty()) continue;
                    const auto libraryArticulationMatchCount = countArticulationIdMatches(*instSpec, libraryArticulationName);
                    if (libraryArticulationMatchCount == 1) continue;
                    if (libraryArticulationMatchCount == 0 && ArticulationNames::stringArrayContainsIgnoreCase(availableLibraryArticulations, normalizedLibraryArticulationName)) continue;
                    if (ArticulationNames::stringArrayContainsIgnoreCase(unknownLibraryArticulationKeys, normalizedLibraryArticulationName)) continue;
                    auto libraryArticulationToken = describeArticulationResolveFailure(*instSpec, libraryArticulationName, kitSpec->meta.codeRaw + "/" + instSpec->meta.codeRaw, mapLayer->slotId);
                    const auto suggestedLibraryArticulation = ArticulationNames::findClosestArticulationName(normalizedLibraryArticulationName, availableLibraryArticulations);
                    if (suggestedLibraryArticulation.isNotEmpty()) libraryArticulationToken += " (did you mean '" + suggestedLibraryArticulation + "'?)";
                    unknownLibraryArticulationKeys.add(normalizedLibraryArticulationName);
                    unknownLibraryArticulations.add(libraryArticulationToken);
                }

                if (!unknownLibraryArticulations.isEmpty()) {
                    const auto unknownLibraryArticulationsText = unknownLibraryArticulations.joinIntoString(", ");
                    const auto availableLibraryArticulationsText = availableLibraryArticulations.isEmpty()
                        ? juce::String("none")
                        : availableLibraryArticulations.joinIntoString(", ");

                    statusLines.add("Library articulation errors: " + unknownLibraryArticulationsText + ". Available library articulations: " + availableLibraryArticulationsText);
                    slotResolveFailed = true;
                    continue;
                }
            }

            const auto& articulationSlotFilters = mapLayer->slotArticulationFilter;
            juce::String articulationSlotFilterText;
            for (const auto& slotFilter : articulationSlotFilters) articulationSlotFilterText += (articulationSlotFilterText.isEmpty() ? "" : ",") + slotFilter;
            LOG(Info, "  resolved: slot=" + mapLayer->slotId + " -> " + kitSpec->meta.name + "/" + mapLayer->instCode
                + (articulationSlotFilterText.isNotEmpty() ? " articulationSlots=[" + articulationSlotFilterText + "]" : " articulationSlots=[ALL]"));
            resolvedMapLayers.push_back({ mapLayer, kitSpec, instSpec, std::move(articulationSlotFilters) });
        }

        if (slotResolveFailed || resolvedMapLayers.empty()) {
            statusLines.add("Slot '" + slotId + "' was not applied; old route kept");
            nextSlotFingerprints[slotId] = this->lastAppliedSlotFingerprints[slotId];
            nextSlotReloadFingerprints[slotId] = this->lastAppliedSlotReloadFingerprints[slotId];
            continue;
        }

        slotChanged = slotChanged || slotReloadChanged;

        const auto& primary = resolvedMapLayers.front();
        InstrumentPlayer::ArticulationLibraryMap articulationLibraryMap;
        InstrumentPlayer::ArticulationNameRemap primaryRemap = buildArtNameRemap(*primary.mapLayer);
        InstrumentPlayer::ArticulationExactRemap primaryExactRemap = buildArtExactRemap(*primary.mapLayer);
        std::vector<ShadowPlan> shadowPlans;
        std::vector<uint32_t> claimedArticulationSlotNameIds;
        std::vector<uint32_t> primaryAllowedArticulationSlotNameIds;
        std::vector<juce::String> primaryAllowedArticulationSlotFilters;
        auto appendPrimaryAllowedArticulationSlotFilter = [&primaryAllowedArticulationSlotFilters](const juce::String& articulationSlotName) {
            const auto normalizedArticulationSlotName = articulationSlotName.trim();
            if (normalizedArticulationSlotName.isEmpty()) return;
            for (const auto& existingArticulationSlotName : primaryAllowedArticulationSlotFilters)
                if (existingArticulationSlotName.compareIgnoreCase(normalizedArticulationSlotName) == 0) return;
            primaryAllowedArticulationSlotFilters.push_back(normalizedArticulationSlotName);
        };
        auto removePrimaryAllowedArticulationSlotFilter = [&primaryAllowedArticulationSlotFilters](uint32_t articulationSlotNameId) {
            std::erase_if(primaryAllowedArticulationSlotFilters, [articulationSlotNameId](const juce::String& articulationSlotName) {
                return StringsDictSingleton::getId(ArticulationNames::normalizeArticulationName(articulationSlotName)) == articulationSlotNameId;
            });
        };

        if (primary.effectiveArticulationSlotFilters.empty()) {
            for (const auto& articulationSlot : slotLayout.articulations)
                appendUniqueId(primaryAllowedArticulationSlotNameIds, articulationSlot.nameId);
        } else {
            for (const auto& articulationSlotName : primary.effectiveArticulationSlotFilters) {
                if (articulationSlotName.isNotEmpty()) {
                    appendUniqueId(primaryAllowedArticulationSlotNameIds, StringsDictSingleton::getId(ArticulationNames::normalizeArticulationName(articulationSlotName)));
                    appendPrimaryAllowedArticulationSlotFilter(articulationSlotName);
                }
            }
        }

        if (LOG_ENABLED(Info)) {
            juce::String allowedStr;
            for (const auto& articulationSlotNameId : primaryAllowedArticulationSlotNameIds)
                allowedStr += (allowedStr.isEmpty() ? "" : ",") + StringsDictSingleton::nameOf(articulationSlotNameId);
            LOG(Info, "  slot=" + slotId + " primaryAllowed(initial)=[" + allowedStr + "] slotChanged=" + juce::String(slotChanged ? "yes" : "no"));
        }

        for (const auto& articulationSlotName : primary.effectiveArticulationSlotFilters) {
            if (articulationSlotName.isNotEmpty())
                appendUniqueId(claimedArticulationSlotNameIds, StringsDictSingleton::getId(ArticulationNames::normalizeArticulationName(articulationSlotName)));
        }

        for (size_t idx = 1; idx < resolvedMapLayers.size(); ++idx) {
            const auto& resolved = resolvedMapLayers[idx];
            if (resolved.effectiveArticulationSlotFilters.empty()) {
                shadowPlans.push_back({ resolved.mapLayer, resolved.instSpec, {}, buildArtNameRemap(*resolved.mapLayer), buildArtExactRemap(*resolved.mapLayer) });
                continue;
            }

            std::vector<juce::String> layeredArticulationSlots;
            const bool hasParameterOverrides = resolved.mapLayer->tuningCents.has_value()
                                            || resolved.mapLayer->attackTimeMs.has_value()
                                            || resolved.mapLayer->decayTimeMs.has_value()
                                            || resolved.mapLayer->volumeDb.has_value()
                                            || resolved.mapLayer->pan.has_value()
                                            || resolved.mapLayer->velocityResponse.has_value()
                                            || resolved.mapLayer->flipLR
                                            || resolved.mapLayer->flipPhase
                                            || !resolved.mapLayer->micBleedLevels.empty()
                                            || !resolved.mapLayer->micRouteOverrides.empty();
            for (const auto& articulationSlotName : resolved.effectiveArticulationSlotFilters) {
                if (articulationSlotName.isEmpty()) continue;

                const auto articulationSlotNameId = StringsDictSingleton::getId(ArticulationNames::normalizeArticulationName(articulationSlotName));
                const bool alreadyClaimed = vectorContains(claimedArticulationSlotNameIds, articulationSlotNameId);
                if (hasParameterOverrides) {
                    layeredArticulationSlots.push_back(articulationSlotName);
                    if (!alreadyClaimed) {
                        appendUniqueId(claimedArticulationSlotNameIds, articulationSlotNameId);
                        std::erase(primaryAllowedArticulationSlotNameIds, articulationSlotNameId);
                        removePrimaryAllowedArticulationSlotFilter(articulationSlotNameId);
                    }
                } else if (!alreadyClaimed) {
                    appendUniqueId(claimedArticulationSlotNameIds, articulationSlotNameId);
                    articulationLibraryMap[articulationSlotNameId] = resolved.instSpec;
                    appendUniqueId(primaryAllowedArticulationSlotNameIds, articulationSlotNameId);
                    if (!primary.effectiveArticulationSlotFilters.empty()) appendPrimaryAllowedArticulationSlotFilter(articulationSlotName);
                    // Propagate the cross-articulation remap to the primary instrument player.
                    auto mapLayerRemap = buildArtNameRemap(*resolved.mapLayer);
                    auto remapIt = mapLayerRemap.find(articulationSlotNameId);
                    if (remapIt != mapLayerRemap.end()) primaryRemap[articulationSlotNameId] = remapIt->second;
                    auto mapLayerExactRemap = buildArtExactRemap(*resolved.mapLayer);
                    primaryExactRemap.insert(mapLayerExactRemap.begin(), mapLayerExactRemap.end());
                } else {
                    layeredArticulationSlots.push_back(articulationSlotName);
                }
            }

            if (!layeredArticulationSlots.empty())
                shadowPlans.push_back({ resolved.mapLayer, resolved.instSpec, layeredArticulationSlots, buildArtNameRemap(*resolved.mapLayer), buildArtExactRemap(*resolved.mapLayer) });
        }

        if (LOG_ENABLED(Info)) {
            juce::String allowedStr;
            for (const auto& articulationSlotNameId : primaryAllowedArticulationSlotNameIds)
                allowedStr += (allowedStr.isEmpty() ? "" : ",") + StringsDictSingleton::nameOf(articulationSlotNameId);
            LOG(Info, "  slot=" + slotId + " primaryAllowed(final)=[" + allowedStr + "]");
            if (!primaryRemap.empty()) {
                juce::String remapStr;
                for (const auto& [fromId, toId] : primaryRemap)
                    remapStr += (remapStr.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(fromId) + "->" + StringsDictSingleton::nameOf(toId);
                LOG(Info, "  primaryRemap: [" + remapStr + "]");
            }
            if (!articulationLibraryMap.empty()) {
                juce::String libraryMapText;
                for (const auto& [artId, instPtr] : articulationLibraryMap)
                    libraryMapText += (libraryMapText.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(artId) + "->" + instPtr->meta.codeRaw;
                LOG(Info, "  articulationLibraryMap: [" + libraryMapText + "]");
            }
        }

        InstrumentMapApplyPlan::PreparedSlot prep;
        prep.slotId = slotId;
        auto pIdxIt = primarySlotIndex.find(slotId);
        prep.primaryIndex = (pIdxIt != primarySlotIndex.end()) ? pIdxIt->second : -1;
        if (!slotChanged && slotStateChanged && prep.primaryIndex >= 0) {
            prep.primaryParameterUpdate = makeParameterUpdate(*primary.mapLayer);
            prep.primaryParameterRoutes = buildTargetRoutes(slotLayout, primary.effectiveArticulationSlotFilters);
        }

        const auto existingShadowIt = existingShadowsBySlotId.find(slotId);
        const bool slotHasExistingShadows = existingShadowIt != existingShadowsBySlotId.end() && !existingShadowIt->second.empty();
        const bool slotHasShadowPlans = !shadowPlans.empty();
        bool canReuseExistingShadows = slotHasExistingShadows && slotHasShadowPlans && !slotChanged && existingShadowIt->second.size() == shadowPlans.size();
        std::vector<bool> matchedExistingShadows(slotHasExistingShadows ? existingShadowIt->second.size() : 0, false);
        std::vector<InstrumentMapApplyPlan::PreparedShadowParameterUpdate> shadowParameterUpdates;

        if (canReuseExistingShadows) {
            for (const auto& shadowPlan : shadowPlans) {
                const auto reloadFingerprint = makeResolvedMapLayerReloadFingerprint(*shadowPlan.mapLayer, *shadowPlan.instSpec);
                int matchedShadowIndex = -1;
                for (int idx = 0; idx < static_cast<int>(existingShadowIt->second.size()); ++idx) {
                    if (matchedExistingShadows[static_cast<size_t>(idx)] || existingShadowIt->second[static_cast<size_t>(idx)].reloadFingerprint != reloadFingerprint) continue;
                    matchedShadowIndex = idx;
                    break;
                }

                if (matchedShadowIndex < 0) {
                    canReuseExistingShadows = false;
                    break;
                }

                matchedExistingShadows[static_cast<size_t>(matchedShadowIndex)] = true;
                const auto stateFingerprint = makeResolvedMapLayerFingerprint(*shadowPlan.mapLayer, *shadowPlan.instSpec);
                const auto& existingShadow = existingShadowIt->second[static_cast<size_t>(matchedShadowIndex)];
                if (existingShadow.stateFingerprint != stateFingerprint)
                    shadowParameterUpdates.push_back({ existingShadow.instrumentPlayer, makeParameterUpdate(*shadowPlan.mapLayer), stateFingerprint });
            }
        }

        prep.rebuildShadows = slotHasShadowPlans && (!canReuseExistingShadows || slotChanged);
        prep.removeOldShadows = slotHasExistingShadows && (prep.rebuildShadows || (!slotHasShadowPlans && (slotChanged || slotStateChanged)));
        if (canReuseExistingShadows) prep.shadowParameterUpdates = std::move(shadowParameterUpdates);

        // Create a replacement instrument player for the primary slot outside the lock.
        if (slotChanged && prep.primaryIndex >= 0) {
            auto replacement = std::make_unique<InstrumentPlayer>(this, slotLayout);
            replacement->setMicOutputMapping(primarySlot->getMicOutputMapping());
            replacement->setMicChannelCount(primarySlot->getMicChannelCount());
            const bool primaryStrictArticulationResolution = !primaryAllowedArticulationSlotFilters.empty() || !primaryRemap.empty() || !primaryExactRemap.empty();
            InstrumentPlayer::RoutingContext primaryRoutingContext;
            primaryRoutingContext.defaultInstrument = primary.instSpec;
            primaryRoutingContext.articulationLibraryMap = articulationLibraryMap;
            primaryRoutingContext.allowedArticulationNameIds = primaryAllowedArticulationSlotNameIds;
            primaryRoutingContext.allowedArticulationFilters = primaryAllowedArticulationSlotFilters;
            primaryRoutingContext.artNameRemap = primaryRemap;
            primaryRoutingContext.artExactRemap = primaryExactRemap;
            primaryRoutingContext.micRouteOverrides = primary.mapLayer->micRouteOverrides;
            primaryRoutingContext.strictArticulationResolution = primaryStrictArticulationResolution;
            auto primaryPlaybackEntries = replacement->buildArticulationPlaybackEntries(slotLayout, primaryRoutingContext);
            replacement->loadInstrumentWithPlaybackEntries(primary.instSpec, primaryRoutingContext, std::move(primaryPlaybackEntries), librarySnapshot);
            DrumKitEngine::applyPreparedMapLayerUpdate(*replacement, makeParameterUpdate(*primary.mapLayer), buildTargetRoutes(slotLayout, primary.effectiveArticulationSlotFilters));
            LOG(Info, "  PRIMARY loaded: slot=" + slotId + " inst=" + primary.instSpec->meta.codeRaw
                + " tuning=" + juce::String(primary.mapLayer->tuningCents.value_or(0))
                + (primary.mapLayer->attackTimeMs.has_value() ? " attack=" + juce::String(*primary.mapLayer->attackTimeMs) : "")
                + (primary.mapLayer->decayTimeMs.has_value() ? " decay=" + juce::String(*primary.mapLayer->decayTimeMs) : ""));
            if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
                replacement->setCurrentPlaybackSampleRate(this->lastSampleRate);
                replacement->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
            }
            prep.primaryReplacement = std::move(replacement);
            ++applyPlan.preparedPlayerCount;
        }

        // Create shadow instrument players outside the lock.
        if (prep.rebuildShadows) {
            for (const auto& shadowPlan : shadowPlans) {
                auto shadow = std::make_unique<InstrumentPlayer>(this, slotLayout);
                auto* shadowRaw = shadow.get();
                const auto shadowArticulationSlotNameIds = ArticulationNames::toArticulationIdList(shadowPlan.articulationSlots);
                shadowRaw->setMicOutputMapping(primarySlot->getMicOutputMapping());
                shadowRaw->setMicChannelCount(primarySlot->getMicChannelCount());
                const bool shadowStrictArticulationResolution = !shadowPlan.articulationSlots.empty() || !shadowPlan.artNameRemap.empty() || !shadowPlan.artExactRemap.empty();
                InstrumentPlayer::RoutingContext shadowRoutingContext;
                shadowRoutingContext.defaultInstrument = shadowPlan.instSpec;
                shadowRoutingContext.allowedArticulationNameIds = shadowArticulationSlotNameIds;
                shadowRoutingContext.allowedArticulationFilters = shadowPlan.articulationSlots;
                shadowRoutingContext.artNameRemap = shadowPlan.artNameRemap;
                shadowRoutingContext.artExactRemap = shadowPlan.artExactRemap;
                shadowRoutingContext.micRouteOverrides = shadowPlan.mapLayer->micRouteOverrides;
                shadowRoutingContext.strictArticulationResolution = shadowStrictArticulationResolution;
                auto shadowPlaybackEntries = shadowRaw->buildArticulationPlaybackEntries(slotLayout, shadowRoutingContext);
                shadowRaw->loadInstrumentWithPlaybackEntries(shadowPlan.instSpec, shadowRoutingContext, std::move(shadowPlaybackEntries), librarySnapshot);
                DrumKitEngine::applyPreparedMapLayerUpdate(*shadowRaw, makeParameterUpdate(*shadowPlan.mapLayer), {});
                if (LOG_ENABLED(Info)) {
                    juce::String shadowArtsStr;
                    for (const auto& articulationSlotName : shadowPlan.articulationSlots) shadowArtsStr += (shadowArtsStr.isEmpty() ? "" : ",") + articulationSlotName;
                    juce::String remapStr;
                    for (const auto& [fromId, toId] : shadowPlan.artNameRemap) remapStr += (remapStr.isEmpty() ? "" : ", ") + StringsDictSingleton::nameOf(fromId) + "->" + StringsDictSingleton::nameOf(toId);
                    LOG(Info, "  SHADOW loaded: slot=" + slotId + " inst=" + shadowPlan.instSpec->meta.codeRaw
                        + " arts=[" + shadowArtsStr + "]"
                        + (remapStr.isNotEmpty() ? " remap=[" + remapStr + "]" : "")
                        + " tuning=" + juce::String(shadowPlan.mapLayer->tuningCents.value_or(0)));
                }

                if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
                    shadowRaw->setCurrentPlaybackSampleRate(this->lastSampleRate);
                    shadowRaw->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
                }

                InstrumentMapApplyPlan::PreparedShadow preparedShadow;
                preparedShadow.stateFingerprint = makeResolvedMapLayerFingerprint(*shadowPlan.mapLayer, *shadowPlan.instSpec);
                preparedShadow.reloadFingerprint = makeResolvedMapLayerReloadFingerprint(*shadowPlan.mapLayer, *shadowPlan.instSpec);
                for (const auto& articulationSlot : slotLayout.articulations) {
                    if (!shadowRaw->routesArticulationSlot(articulationSlot.nameId, articulationSlot.exprId)) continue;
                    for (const int note : articulationSlot.notes)
                        preparedShadow.noteRoutes.push_back({note, shadowRaw});
                }
                preparedShadow.instrumentPlayer = std::move(shadow);
                prep.shadows.push_back(std::move(preparedShadow));
                ++applyPlan.preparedPlayerCount;
            }
        }

        applyPlan.preparedSlots.push_back(std::move(prep));
    }

    applyPlan.nextSlotFingerprints = std::move(nextSlotFingerprints);
    applyPlan.nextSlotReloadFingerprints = std::move(nextSlotReloadFingerprints);
    applyPlan.normalizedMapText = normalizedMapText;
    auto commitResult = this->commitInstrumentMapApply(applyPlan);
    const int applied = commitResult.appliedPlayerCount;

    if (applied > 0 && this->sampleLoaderPtr != nullptr)
        this->sampleLoaderPtr->getQueue().startProcessing();

    if (applied <= 0 && statusLines.isEmpty())
        statusLines.add("Instrument map unchanged - no reload needed");

    if (statusLines.isEmpty())
        statusLines.add("Instrument map applied successfully");

    this->lastInstrumentMapStatus = statusLines.joinIntoString("\n");
    LOG(Info, "=== useInstrumentMap done: applied=" + juce::String(applied) + " oldPlayers=" + juce::String(commitResult.retiredPlayers.size())
        + " totalPlayers=" + juce::String(commitResult.totalPlayerCount) + " ===");
    return applied > 0;
}

DrumKitEngine::InstrumentMapCommitResult DrumKitEngine::commitInstrumentMapApply(InstrumentMapApplyPlan& plan) {
    InstrumentMapCommitResult result;
    result.appliedPlayerCount = plan.preparedPlayerCount;
    result.retiredPlayers.reserve(this->instrumentPlayerPtrs.size());

    std::unordered_set<juce::String> shadowSlotIdsToRemove;
    shadowSlotIdsToRemove.reserve(plan.preparedSlots.size());
    for (const auto& preparedSlot : plan.preparedSlots) {
        if (preparedSlot.removeOldShadows) shadowSlotIdsToRemove.insert(preparedSlot.slotId);
    }

    std::unordered_set<InstrumentPlayer*> removedShadowPlayers;
    removedShadowPlayers.reserve(this->instrumentPlayerPtrs.size());

    const juce::ScopedLock graphLock(this->engineGraphLock);

    if (!shadowSlotIdsToRemove.empty()) {
        for (int idx = static_cast<int>(this->instrumentPlayerPtrs.size()) - 1; idx >= this->primaryInstrumentPlayerCount; --idx) {
            auto* shadowPlayer = this->instrumentPlayerPtrs[static_cast<size_t>(idx)].get();
            if (shadowPlayer == nullptr || shadowSlotIdsToRemove.find(shadowPlayer->getId()) == shadowSlotIdsToRemove.end()) continue;
            removedShadowPlayers.insert(shadowPlayer);
            this->lastAppliedShadowFingerprints.erase(shadowPlayer);
            this->lastAppliedShadowReloadFingerprints.erase(shadowPlayer);
            result.retiredPlayers.push_back(std::move(this->instrumentPlayerPtrs[static_cast<size_t>(idx)]));
            this->instrumentPlayerPtrs.erase(this->instrumentPlayerPtrs.begin() + idx);
        }
    }

    bool anyPrimarySwapped = false;
    for (auto& preparedSlot : plan.preparedSlots) {
        if (preparedSlot.primaryReplacement && preparedSlot.primaryIndex >= 0 && preparedSlot.primaryIndex < static_cast<int>(this->instrumentPlayerPtrs.size())) {
            const auto primaryIndex = static_cast<size_t>(preparedSlot.primaryIndex);
            result.retiredPlayers.push_back(std::move(this->instrumentPlayerPtrs[primaryIndex]));
            this->instrumentPlayerPtrs[primaryIndex] = std::move(preparedSlot.primaryReplacement);
            this->instrumentPlayerById[preparedSlot.slotId] = this->instrumentPlayerPtrs[primaryIndex].get();
            anyPrimarySwapped = true;
        } else if (preparedSlot.primaryParameterUpdate.has_value() && preparedSlot.primaryIndex >= 0 && preparedSlot.primaryIndex < static_cast<int>(this->instrumentPlayerPtrs.size())) {
            DrumKitEngine::applyPreparedMapLayerUpdate(*this->instrumentPlayerPtrs[static_cast<size_t>(preparedSlot.primaryIndex)], *preparedSlot.primaryParameterUpdate, preparedSlot.primaryParameterRoutes);
            ++result.appliedPlayerCount;
        }

        for (const auto& shadowUpdate : preparedSlot.shadowParameterUpdates) {
            if (shadowUpdate.instrumentPlayer == nullptr) continue;
            DrumKitEngine::applyPreparedMapLayerUpdate(*shadowUpdate.instrumentPlayer, shadowUpdate.update, {});
            this->lastAppliedShadowFingerprints[shadowUpdate.instrumentPlayer] = shadowUpdate.stateFingerprint;
            ++result.appliedPlayerCount;
        }
    }

    if (anyPrimarySwapped) {
        this->rebuildPrimaryInstrumentNoteRouting();
        if (this->activeKitLayoutPtr != nullptr) {
            for (int idx = this->primaryInstrumentPlayerCount; idx < static_cast<int>(this->instrumentPlayerPtrs.size()); ++idx) {
                auto* shadowPlayer = this->instrumentPlayerPtrs[static_cast<size_t>(idx)].get();
                if (shadowPlayer == nullptr) continue;
                const auto layoutIt = std::find_if(this->activeKitLayoutPtr->instrumentSlots.begin(), this->activeKitLayoutPtr->instrumentSlots.end(), [&shadowPlayer](const KitLayout::InstrumentSlot& slot) { return slot.id == shadowPlayer->getId(); });
                if (layoutIt != this->activeKitLayoutPtr->instrumentSlots.end()) this->addShadowPlayerNoteRoutes(*shadowPlayer, *layoutIt);
            }
        }
    } else if (!removedShadowPlayers.empty()) {
        for (auto& [note, instrumentPlayers] : this->instrumentPlayersByNote) {
            juce::ignoreUnused(note);
            std::erase_if(instrumentPlayers, [&removedShadowPlayers](InstrumentPlayer* instrumentPlayer) { return removedShadowPlayers.find(instrumentPlayer) != removedShadowPlayers.end(); });
        }
    }

    for (auto& preparedSlot : plan.preparedSlots) {
        for (auto& preparedShadow : preparedSlot.shadows) {
            for (const auto& [note, instrumentPlayerPtr] : preparedShadow.noteRoutes) this->instrumentPlayersByNote[note].push_back(instrumentPlayerPtr);
            auto* shadowPlayer = preparedShadow.instrumentPlayer.get();
            this->lastAppliedShadowFingerprints[shadowPlayer] = preparedShadow.stateFingerprint;
            this->lastAppliedShadowReloadFingerprints[shadowPlayer] = preparedShadow.reloadFingerprint;
            this->instrumentPlayerPtrs.push_back(std::move(preparedShadow.instrumentPlayer));
        }
    }

    this->lastAppliedSlotFingerprints = std::move(plan.nextSlotFingerprints);
    this->lastAppliedSlotReloadFingerprints = std::move(plan.nextSlotReloadFingerprints);
    this->lastAppliedInstrumentMapText = std::move(plan.normalizedMapText);
    result.totalPlayerCount = this->instrumentPlayerPtrs.size();
    return result;
}

ParseResult<DrumKitEngine::InstrumentMap> DrumKitEngine::parseInstrumentMapText(const juce::String& text) {
    return ::InstrumentMap::parseText(text);
}
