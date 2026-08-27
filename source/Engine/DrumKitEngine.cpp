#include "DrumKitEngine.h"
#include "ArticulationNames.h"
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

static void preparePlayerIfNeeded(InstrumentPlayer& player, double sampleRate, int samplesPerBlock) {
    if (sampleRate <= 0.0 || samplesPerBlock <= 0) return;
    player.setCurrentPlaybackSampleRate(sampleRate);
    player.prepareToPlay(sampleRate, samplesPerBlock);
}

static bool canReuseSlotWithoutReload(const KitLayout::InstrumentSlot& oldSlot, const KitLayout::InstrumentSlot& newSlot) {
    if (oldSlot.type != newSlot.type || oldSlot.articulations.size() != newSlot.articulations.size()) return false;

    for (size_t idx = 0; idx < oldSlot.articulations.size(); ++idx) {
        const auto& oldArticulation = oldSlot.articulations[idx];
        const auto& newArticulation = newSlot.articulations[idx];
        if (oldArticulation.nameId != newArticulation.nameId || oldArticulation.exprId != newArticulation.exprId) return false;
    }
    return true;
}

static void copyPlayerSettings(InstrumentPlayer& target, const InstrumentPlayer& source) {
    target.setTuningOffsetCents(source.getTuningOffsetCents());

    const auto attackOverride = source.getAttackOverrideMs();
    if (attackOverride.has_value()) target.setAttackOverrideMs(*attackOverride);

    const auto decayOverride = source.getDecayOverrideMs();
    if (decayOverride.has_value()) target.setDecayOverrideMs(*decayOverride);

    target.setVolumeDb(source.getVolumeDb());
    target.setPan(source.getPan());
    target.setFlipLR(source.getFlipLR());
    target.setFlipPhase(source.getFlipPhase());
    target.setMicBleedLevels(source.getMicBleedLevels());
}

static std::unique_ptr<InstrumentPlayer> createReplacementPlayer(DrumKitEngine* owner, const KitLayout::InstrumentSlot& layoutSlot, const InstrumentPlayer& player, double sampleRate, int samplesPerBlock) {
    auto replacement = std::make_unique<InstrumentPlayer>(owner, layoutSlot);
    replacement->setMicOutputMapping(player.getMicOutputMapping());
    replacement->setMicChannelCount(player.getMicChannelCount());

    const auto librarySnapshot = player.getLibrarySnapshot();
    const auto& routingContext = player.getLastRoutingContext();
    if (routingContext.isValid()) {
        replacement->loadInstrumentWithRouting(routingContext.defaultInstrument, routingContext.articulationLibraryMap, routingContext.allowedArticulationNameIds, routingContext.allowedArticulationFilters, routingContext.artNameRemap, routingContext.artExactRemap, routingContext.artAliases, librarySnapshot, routingContext.strictArticulationResolution, routingContext.micRouteOverrides);
    } else if (auto* loadedInstrument = player.getLoadedInstrumentSpec()) {
        replacement->loadInstrument(const_cast<KitLib::InstrumentSpec*>(loadedInstrument), librarySnapshot);
    }

    replacement->copyArticulationSlotRoutesFrom(player);
    copyPlayerSettings(*replacement, player);
    preparePlayerIfNeeded(*replacement, sampleRate, samplesPerBlock);
    return replacement;
}

DrumKitEngine::DrumKitEngine() {
    this->kitLayoutManagerPtr = std::make_unique<KitLayoutManager>();
    this->micLayoutManagerPtr = std::make_unique<MicLayoutManager>();
}

DrumKitEngine::~DrumKitEngine() {
    this->previewResolveThreadPool.removeAllJobs(true, 2000);
}

bool DrumKitEngine::applyKitLayout() {
    if (this->activeKitLayoutPtr == nullptr) {
        LOG(Warning, "kit layout was not set");
        return false;
    }

    struct ExistingPlayerState {
        std::vector<InstrumentPlayer*> rawPlayers;
        std::unordered_map<juce::String, int> primaryIndexById;
        std::unordered_map<juce::String, std::vector<int>> shadowIndicesBySlotId;
    } existingState;

    {
        const juce::ScopedLock graphLock(this->engineGraphLock);
        existingState.rawPlayers.reserve(this->instrumentPlayerPtrs.size());
        for (int idx = 0; idx < static_cast<int>(this->instrumentPlayerPtrs.size()); ++idx) {
            auto* instrumentPlayer = this->instrumentPlayerPtrs[static_cast<size_t>(idx)].get();
            existingState.rawPlayers.push_back(instrumentPlayer);
            if (idx < this->primaryInstrumentPlayerCount)
                existingState.primaryIndexById[instrumentPlayer->getId()] = idx;
            else
                existingState.shadowIndicesBySlotId[instrumentPlayer->getId()].push_back(idx);
        }
    }

    struct PreparedShadow {
        int oldIndex = -1;
        std::unique_ptr<InstrumentPlayer> replacement;
    };

    struct PreparedSlot {
        KitLayout::InstrumentSlot layoutSlot;
        juce::String slotId;
        int oldPrimaryIndex = -1;
        std::unique_ptr<InstrumentPlayer> primaryReplacement;
        std::vector<PreparedShadow> shadows;
    };

    std::vector<PreparedSlot> preparedSlots;
    preparedSlots.reserve(this->activeKitLayoutPtr->instrumentSlots.size());

    for (const auto& layoutSlot : this->activeKitLayoutPtr->instrumentSlots) {
        PreparedSlot prepared;
        prepared.layoutSlot = layoutSlot;
        prepared.slotId = layoutSlot.id;

        auto primaryIt = existingState.primaryIndexById.find(layoutSlot.id);
        if (primaryIt != existingState.primaryIndexById.end()) {
            prepared.oldPrimaryIndex = primaryIt->second;
            auto* oldPrimary = existingState.rawPlayers[static_cast<size_t>(prepared.oldPrimaryIndex)];
            if (!canReuseSlotWithoutReload(oldPrimary->getInstrumentSlot(), layoutSlot))
                prepared.primaryReplacement = createReplacementPlayer(this, layoutSlot, *oldPrimary, this->lastSampleRate, this->lastSamplesPerBlock);

            auto shadowIt = existingState.shadowIndicesBySlotId.find(layoutSlot.id);
            if (shadowIt != existingState.shadowIndicesBySlotId.end()) {
                prepared.shadows.reserve(shadowIt->second.size());
                for (const int shadowIndex : shadowIt->second) {
                    PreparedShadow shadow;
                    shadow.oldIndex = shadowIndex;

                    auto* oldShadow = existingState.rawPlayers[static_cast<size_t>(shadowIndex)];
                    if (!canReuseSlotWithoutReload(oldShadow->getInstrumentSlot(), layoutSlot))
                        shadow.replacement = createReplacementPlayer(this, layoutSlot, *oldShadow, this->lastSampleRate, this->lastSamplesPerBlock);

                    prepared.shadows.push_back(std::move(shadow));
                }
            }
        } else {
            prepared.primaryReplacement = std::make_unique<InstrumentPlayer>(this, layoutSlot);
            preparePlayerIfNeeded(*prepared.primaryReplacement, this->lastSampleRate, this->lastSamplesPerBlock);
        }

        preparedSlots.push_back(std::move(prepared));
    }

    std::vector<std::unique_ptr<InstrumentPlayer>> oldPlayers;
    {
        const juce::ScopedLock graphLock(this->engineGraphLock);
        auto previousPlayers = std::move(this->instrumentPlayerPtrs);

        this->instrumentPlayerPtrs.clear();
        this->instrumentPlayerById.clear();
        this->primaryInstrumentPlayerCount = 0;

        for (auto& prepared : preparedSlots) {
            if (prepared.oldPrimaryIndex >= 0 && !prepared.primaryReplacement) {
                this->instrumentPlayerPtrs.push_back(std::move(previousPlayers[static_cast<size_t>(prepared.oldPrimaryIndex)]));
            } else {
                this->instrumentPlayerPtrs.push_back(std::move(prepared.primaryReplacement));
            }

            auto* inserted = this->instrumentPlayerPtrs.back().get();
            this->instrumentPlayerById[prepared.slotId] = inserted;
        }

        this->primaryInstrumentPlayerCount = static_cast<int>(this->instrumentPlayerPtrs.size());
        this->clearShadowPlayers();

        for (auto& prepared : preparedSlots) {
            for (auto& shadow : prepared.shadows) {
                if (shadow.oldIndex >= 0 && !shadow.replacement) {
                    this->instrumentPlayerPtrs.push_back(std::move(previousPlayers[static_cast<size_t>(shadow.oldIndex)]));
                } else if (shadow.replacement) {
                    this->instrumentPlayerPtrs.push_back(std::move(shadow.replacement));
                }

                auto* shadowPlayer = this->instrumentPlayerPtrs.back().get();

                this->addShadowPlayerNoteRoutes(*shadowPlayer, prepared.layoutSlot);
            }
        }

        std::unordered_set<juce::String> activeSlotIds;
        activeSlotIds.reserve(preparedSlots.size());
        for (const auto& prepared : preparedSlots)
            activeSlotIds.insert(prepared.slotId);

        for (auto it = this->lastAppliedSlotFingerprints.begin(); it != this->lastAppliedSlotFingerprints.end();) {
            if (it->first != "*" && activeSlotIds.find(it->first) == activeSlotIds.end())
                it = this->lastAppliedSlotFingerprints.erase(it);
            else
                ++it;
        }

        for (auto reloadIt = this->lastAppliedSlotReloadFingerprints.begin(); reloadIt != this->lastAppliedSlotReloadFingerprints.end();) {
            if (reloadIt->first != "*" && activeSlotIds.find(reloadIt->first) == activeSlotIds.end())
                reloadIt = this->lastAppliedSlotReloadFingerprints.erase(reloadIt);
            else
                ++reloadIt;
        }

        if (this->activeMicLayoutPtr != nullptr)
            this->applyMicLayoutUnlocked();

        for (auto& instrumentPlayer : previousPlayers) {
            if (instrumentPlayer)
                oldPlayers.push_back(std::move(instrumentPlayer));
        }
    }

    LOG(Info, "Applied kit layout: " + this->activeKitLayoutPtr->id
        + " (primary slots: " + juce::String(this->primaryInstrumentPlayerCount)
        + ", recycled instrument players: " + juce::String(static_cast<int>(existingState.rawPlayers.size()) - static_cast<int>(oldPlayers.size()))
        + ", retired instrument players: " + juce::String(static_cast<int>(oldPlayers.size())) + ")");

    this->buildNoteNameCache();
    return true;
}

void DrumKitEngine::rebuildPrimaryInstrumentNoteRouting() {
    this->instrumentPlayersByNote.clear();
    this->trackedCCMask.fill(false);
    this->latestPAValues.fill(0);
    this->trackedPAMask.fill(false);

    if (this->activeKitLayoutPtr == nullptr)
        return;

    for (const auto& instrumentSlot : this->activeKitLayoutPtr->instrumentSlots) {
        auto it = this->instrumentPlayerById.find(instrumentSlot.id);
        if (it == this->instrumentPlayerById.end())
            continue;

        auto* instrumentPlayer = it->second;

        // Rebuild the instrument player's internal note/CC routing from current layout data.
        instrumentPlayer->rebuildRoutingFromLayout(instrumentSlot);

        for (const auto& cc : instrumentSlot.controllers) {
            if (cc.number >= 0 && cc.number < 128)
                this->trackedCCMask[static_cast<size_t>(cc.number)] = true;
        }

        for (const auto& art : instrumentSlot.articulations) {
            const bool routesArt = DrumKitEngine::playerRoutesArticulationSlot(*instrumentPlayer, art);
            for (const int note : art.notes) {
                if (note < 0 || note >= 128)
                    continue;
                this->trackedPAMask[static_cast<size_t>(note)] = true;
                if (routesArt) this->instrumentPlayersByNote[note].push_back(instrumentPlayer);
            }
        }
    }
}

void DrumKitEngine::clearShadowPlayers() {
    if (static_cast<int>(this->instrumentPlayerPtrs.size()) > this->primaryInstrumentPlayerCount)
        this->instrumentPlayerPtrs.erase(this->instrumentPlayerPtrs.begin() + this->primaryInstrumentPlayerCount, this->instrumentPlayerPtrs.end());

    this->lastAppliedShadowFingerprints.clear();
    this->lastAppliedShadowReloadFingerprints.clear();
    this->rebuildPrimaryInstrumentNoteRouting();
}

void DrumKitEngine::rebuildMidiNoteRoutingFromLayout() {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    this->rebuildPrimaryInstrumentNoteRouting();

    if (this->activeKitLayoutPtr != nullptr) {
        for (int idx = this->primaryInstrumentPlayerCount; idx < static_cast<int>(this->instrumentPlayerPtrs.size()); ++idx) {
            auto* shadowPlayer = this->instrumentPlayerPtrs[static_cast<size_t>(idx)].get();
            if (shadowPlayer == nullptr) continue;

            const KitLayout::InstrumentSlot* layoutSlot = nullptr;
            for (const auto& slot : this->activeKitLayoutPtr->instrumentSlots) {
                if (slot.id == shadowPlayer->getId()) {
                    layoutSlot = &slot;
                    break;
                }
            }
            if (layoutSlot == nullptr) continue;

            this->addShadowPlayerNoteRoutes(*shadowPlayer, *layoutSlot);
        }
    }

    this->buildNoteNameCache();
}

void DrumKitEngine::addShadowPlayerNoteRoutes(InstrumentPlayer& shadowPlayer, const KitLayout::InstrumentSlot& layoutSlot) {
    shadowPlayer.rebuildRoutingFromLayout(layoutSlot);
    for (const auto& art : layoutSlot.articulations) {
        if (!DrumKitEngine::playerRoutesArticulationSlot(shadowPlayer, art)) continue;
        for (const int note : art.notes) {
            if (note >= 0 && note < 128) this->instrumentPlayersByNote[note].push_back(&shadowPlayer);
        }
    }
}

void DrumKitEngine::buildNoteNameCache() {
    for (size_t noteIdx = 0; noteIdx < this->noteNameCache.size(); ++noteIdx) this->noteNameCache[noteIdx].clear();

    if (this->activeKitLayoutPtr == nullptr) return;

    auto& layout = *this->activeKitLayoutPtr;
    auto& dict = StringsDictSingleton::instance();

    for (const auto& inst : layout.instrumentSlots) {
        juce::String instName = inst.display.isNotEmpty() ? inst.display : inst.id;
        for (const auto& art : inst.articulations) {

            juce::String artName = dict.findName(art.nameId);
            if (artName.isEmpty()) artName = art.codeRaw;

            juce::String finalName = instName;
            if (artName.isNotEmpty()) finalName += " " + artName;

            for (uint8_t note : art.notes) {
                if (note < 0 || note > 127) continue;

                if (this->noteNameCache[note].isEmpty())
                    this->noteNameCache[note] = finalName + " (" + juce::String(note) + ")";
            }
        }
    }
}

bool DrumKitEngine::applyMicLayout() {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    return this->applyMicLayoutUnlocked();
}

std::vector<InstrumentLoadProgressSource> DrumKitEngine::getInstrumentLoadProgressSources() const {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    std::vector<InstrumentLoadProgressSource> result;
    result.reserve(this->instrumentPlayerPtrs.size());
    for (const auto& instrumentPlayer : this->instrumentPlayerPtrs) {
        if (instrumentPlayer == nullptr) continue;
        result.push_back({ instrumentPlayer->getId(), instrumentPlayer->getLoadProgress(), instrumentPlayer->getArticulationLogicalHitProgressSources() });
    }
    return result;
}

bool DrumKitEngine::applyMicLayoutUnlocked() {
    if (this->activeMicLayoutPtr == nullptr) {
        LOG(Warning, "mic layout was not set");
        return false;
    }

    if (this->instrumentPlayerPtrs.size() == 0) {
        LOG(Warning, "unable to apply mic layout: no instrument slots was defined in kit layout");
        return false;
    }
    // mic layout is for a mapping of each SampleSpec mic (MicSpec) of InstrumentPlayer to coresponding output channel of the plugin
    // the map (sequence and other params defined in MicSpec) of channels is defined in micLayout.
    //  MicSpec to determine which of plugin and routing



    // For each instrument slot, calculate how many mic channels it should have
    // for (size_t idx = 0; idx < this->instrumentPlayerPtrs.size(); ++idx) {
    //     InstrumentPlayer* slotPtr = this->instrumentPlayerPtrs[idx].get();
    //     int totalChannels = 0;
    //     const auto slotId = slotPtr->getId();
    //     for (const MicLayout::Mic& mic : this->activeMicLayoutPtr->mics) {
    //         // match mic entries that list this slot id in their instrument field
    //         if (mic.instrument == slotId) {
    //             // parse mic code; try to infer channel count
    //             auto prRes = KitLib::MicSpec::parseMicCode(mic.code);
    //             if (prRes.ok()) {
    //                 totalChannels += prRes.value.channels;
    //             } else {
    //                 totalChannels += 1; //FIXME: fallback mono? review, maybe take something from file
    //             }
    //         }
    //     }

    //     if (totalChannels <= 0)
    //         totalChannels = 2; //FIXME: default to stereo? review, maybe take something from file

    //     slotPtr->setMicChannelCount(totalChannels);
    // }

    // Build an immutable routing-plan snapshot for this kit/mic layout. Stage 3 direct
    // allocations are populated by the pure builder when library direct mic requests are
    // provided; this layout-level snapshot still defines normal mic order.
    std::vector<MicRoutingPlan::SlotRequest> planSlotRequests;
    if (this->activeKitLayoutPtr != nullptr) {
        planSlotRequests.reserve(this->activeKitLayoutPtr->instrumentSlots.size());
        for (const auto& slot : this->activeKitLayoutPtr->instrumentSlots) {
            MicRoutingPlan::SlotRequest request;
            request.slotId = slot.id;
            request.slotType = slot.type;
            request.directPolicy = slot.directMicOuts.value_or(
                this->globalConfig.instrumentCatalog.defaultDirectMicOutsFor(slot.type).value_or(DirectMicOutPolicy::Shared));
            planSlotRequests.push_back(std::move(request));
        }
    }
    this->activeMicRoutingPlanPtr = std::make_unique<MicRoutingPlan>(
        MicRoutingPlan::build(*this->activeMicLayoutPtr, planSlotRequests, this->globalConfig.instrumentCatalog));

    // Build per-instrument mapping using expected host channel count (defined by plugin outputs)
    // All output buses are mono; stereo mics occupy 2 consecutive mono channels
    const int expectedHostChannels = TOTAL_HOST_OUTPUT_CHANNELS;

    // Initialize an empty mapping for each instrument player.
    for (const auto& instPtr : this->instrumentPlayerPtrs) {
        instPtr->setMicOutputMapping({});
    }

    int outCursor = 0;
    for (const auto& mic : this->activeMicLayoutPtr->mics) {
        int channelCount = mic.specs.channels;
        if (channelCount <= 0) channelCount = 1;

        if (outCursor + channelCount > expectedHostChannels) {
            LOG(Warning, "Not enough host output channels to assign mic '" + mic.name + "' with " + juce::String(channelCount) + " channels. Skipping remaining mics.");
            break;
        }

        // Determine target instrument players. //FIXME: move to loadInstrument?
        std::vector<InstrumentPlayer*> targets;
        const auto selector = parseMicInstrumentSelector(mic.instrument, this->globalConfig.instrumentCatalog);
        for (const auto& isp : this->instrumentPlayerPtrs) {
            if (matchMicInstrumentSelector(selector, isp->getId(), isp->getType(), this->globalConfig.instrumentCatalog) != MicSelectorPriority::None)
                targets.push_back(isp.get());
        }

        if (targets.empty()) {
            LOG(Warning,  "Assigning mic " + mic.name + " (" + juce::String(mic.specs.channels) + "ch)" + " for instrument '" + mic.instrument + "' FAILED: no coresponding instrument slots found in your kit layout");
            outCursor += channelCount; // still reserve host channels
            continue;
        } else {
            juce::StringArray targetSlotIds;
            for (const auto* targetPlayer : targets) targetSlotIds.addIfNotAlreadyThere(targetPlayer->getId());
            LOG(Info, "Mic output available: name='" + mic.name + "' route=" + mic.id + " channels=" + juce::String(channelCount) + " slots=[" + targetSlotIds.joinIntoString(",") + "]");
        }

        // Host channel indices for this mic - directly maps to mono bus indices
        std::vector<int> hostIdx;
        for (int i = 0; i < channelCount; ++i) hostIdx.push_back(outCursor + i);

        // Append host indices to each target instrument's mapping
        for (auto* t : targets) {
            auto existing = t->getMicOutputMapping();
            std::vector<int> merged = existing;
            merged.insert(merged.end(), hostIdx.begin(), hostIdx.end());
            t->setMicOutputMapping(merged);
        }

        outCursor += channelCount; // advance by actual mic channel count
    }

    // Apply mapped mic channel counts to instrument players (used for informational purposes).
    for (const auto& instPtr : this->instrumentPlayerPtrs) {
        const auto& map = instPtr->getMicOutputMapping();
        int totalAssigned = static_cast<int>(map.size());
        if (totalAssigned <= 0) totalAssigned = 2;
        instPtr->setMicChannelCount(totalAssigned);
    }

    for (const auto& instPtr : this->instrumentPlayerPtrs) {
        auto primaryIt = this->instrumentPlayerById.find(instPtr->getId());
        if (primaryIt == this->instrumentPlayerById.end())
            continue;

        auto* primary = primaryIt->second;
        if (primary == instPtr.get())
            continue;

        instPtr->setMicOutputMapping(primary->getMicOutputMapping());
        instPtr->setMicChannelCount(primary->getMicChannelCount());
    }

    // Detailed mapping log per instrument
    for (const auto& instPtr : this->instrumentPlayerPtrs) {
        juce::String mapStr = "";
        const auto& map = instPtr->getMicOutputMapping();
        for (size_t i = 0; i < map.size(); ++i) {
            if (i) mapStr += ",";
            mapStr += juce::String(map[i] + 1); // convert to 1-based for user-friendly display
        }
        if (mapStr.isEmpty()) mapStr = "(none)";
        LOG(Debug, "Mic mapping " + mapStr + " (" + juce::String(instPtr->getMicChannelCount()) + " ch) used for instrument '" + instPtr->getId() + "'");
    }

    LOG(Info, "Applied mic layout: " + this->activeMicLayoutPtr->id + " (assigned host channels: " + juce::String(outCursor) + ")");
    return true;
}

void DrumKitEngine::prepareToPlay(double sampleRate, int samplesPerBlock) {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    this->lastSampleRate = sampleRate;
    this->lastSamplesPerBlock = samplesPerBlock;
    this->crosstalkFilter.prepare(sampleRate, juce::jmax(1024, samplesPerBlock * 8));
    for (const auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
        instrumentPlayerPtr->setCurrentPlaybackSampleRate(sampleRate);
        instrumentPlayerPtr->prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void DrumKitEngine::processBlock(juce::AudioBuffer<float>& outputBuffer, juce::MidiBuffer& midiMessages) { //TODO: review
    const juce::ScopedTryLock graphLock(this->engineGraphLock);
    if (!graphLock.isLocked()) {
        outputBuffer.clear();
        return;
    }

    const auto outputBufferStartSample = 0;
    const auto outputBufferNumSamples = outputBuffer.getNumSamples();

    this->saveLatestControllerValues(midiMessages);
    this->crosstalkFilter.process(midiMessages, this->crosstalkProfile, this->engineSettings.crosstalkWindowMs.load(), outputBufferNumSamples);

    // Each instrument's SampledSound now stores *global* host channel indices in
    // micBufferChannelStart, so voices can render directly into outputBuffer.
    // The per-instrument micBuffer intermediate and local-to-global copy are no longer needed.
    for (const auto& instrumentPlayerPtr : this->instrumentPlayerPtrs) {
        instrumentPlayerPtr->renderNextBlock(outputBuffer, midiMessages, outputBufferStartSample, outputBufferNumSamples);
    }

    // Mix one-shot preview playback into the first two output channels
    int readPos = this->previewReadPos.load();
    if (readPos >= 0) {
        const juce::SpinLock::ScopedLockType previewScopedLock(this->previewLock);
        auto buf = this->previewBuffer;
        if (buf != nullptr && readPos < buf->getNumSamples()) {
            int samplesToMix = juce::jmin(outputBufferNumSamples, buf->getNumSamples() - readPos);
            int srcChannels = buf->getNumChannels();
            int dstChannels = outputBuffer.getNumChannels();
            for (int ch = 0; ch < juce::jmin(srcChannels, dstChannels, 2); ++ch) {
                outputBuffer.addFrom(ch, 0, *buf, ch, readPos, samplesToMix, this->previewGain);
            }
            readPos += samplesToMix;
            if (readPos >= buf->getNumSamples())
                this->previewReadPos.store(-1);
            else
                this->previewReadPos.store(readPos);
        } else {
            this->previewReadPos.store(-1);
        }
    }
}

void DrumKitEngine::clearCrosstalkProfile() {
    this->crosstalkProfile.clear();
}

void DrumKitEngine::beginCrosstalkCalibration() {
    this->crosstalkProfileBeforeCalibration = this->crosstalkProfile;
    this->crosstalkProfile.clear();
}

void DrumKitEngine::cancelCrosstalkCalibration() {
    if (this->crosstalkProfileBeforeCalibration.has_value())
        this->crosstalkProfile = *this->crosstalkProfileBeforeCalibration;
    this->crosstalkProfileBeforeCalibration.reset();
}

void DrumKitEngine::finishCrosstalkCalibration() {
    this->crosstalkProfileBeforeCalibration.reset();
}

void DrumKitEngine::setCrosstalkThreshold(int primaryNote, int secondaryNote, int velocityRatio) {
    this->crosstalkProfile.setThreshold(primaryNote, secondaryNote, velocityRatio);
}

juce::String DrumKitEngine::buildCrosstalkProfileText() const {
    juce::String text;
    for (int primaryNote = 0; primaryNote < 128; ++primaryNote) {
        for (int secondaryNote = 0; secondaryNote < 128; ++secondaryNote) {
            const int ratio = this->crosstalkProfile.getThreshold(primaryNote, secondaryNote);
            if (ratio <= 0)
                continue;
            const auto primaryName = juce::MidiMessage::getMidiNoteName(primaryNote, true, true, 3);
            const auto secondaryName = juce::MidiMessage::getMidiNoteName(secondaryNote, true, true, 3);
            const int percentage = static_cast<int>(std::round(static_cast<double>(ratio) * 100.0 / 127.0));
            text += juce::String(primaryNote) + " " + primaryName + " -> " + juce::String(secondaryNote) + " " + secondaryName + ": suppress <= " + juce::String(percentage) + "% of primary velocity\n";
        }
    }
    return text.isNotEmpty() ? text : "No learned crosstalk pairs.";
}

std::optional<juce::String> DrumKitEngine::getNameForMidiNoteNumber(int note, int /* midiChannel */) {
    if (note < 0 || note > 127) return {};

    const auto& name = this->noteNameCache[static_cast<size_t>(note)];
    if (name.isNotEmpty()) return name;
    return {};
}

void DrumKitEngine::saveLatestControllerValues(const juce::MidiBuffer& midiMessages) {
    for (const auto& midiMess : midiMessages) {
        const juce::MidiMessage msg = midiMess.getMessage();
        if (msg.isController()) {
            const auto ccNum = msg.getControllerNumber();
            if (ccNum >= 0 && ccNum <= 127 && this->trackedCCMask[static_cast<size_t>(ccNum)])
                this->latestCCValues[static_cast<size_t>(ccNum)] = msg.getControllerValue();
        } else if (msg.isAftertouch()) {
            const auto note = msg.getNoteNumber();
            if (note >= 0 && note <= 127 && this->trackedPAMask[static_cast<size_t>(note)])
                this->latestPAValues[static_cast<size_t>(note)] = msg.getAfterTouchValue();
        }
    }
}
