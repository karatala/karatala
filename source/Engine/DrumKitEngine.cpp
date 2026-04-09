#include "DrumKitEngine.h"
#include "DrumKitEngineMappingModel.h"
#include "DrumKitEngineTextView.h"
#include "InstrumentSynth.h"
#include "SampleSelector.h"
#include "../Config/Helpers.h"
#include "../Config/YamlHelper.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>
#include <unordered_set>

// tuning constants
static constexpr int NAME_IT_BETTER_MIN_VARIANTS = 3;
static constexpr int NAME_IT_BETTER_MAX_VELOCITY_DISTANCE_TO_USE_SAMPLES_FROM_NEARBY_VELOCITY_LAYERS = 32;
static constexpr int NAME_IT_BETTER_MAX_NEIGHBORS_TO_CONSIDER = 2;
// TOTAL_HOST_OUTPUT_CHANNELS is defined in DrumKitEngine.h

namespace {
static bool containsId(const std::vector<uint32_t>& values, uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

static void addUniqueId(std::vector<uint32_t>& values, uint32_t value) {
    if (!containsId(values, value))
        values.push_back(value);
}

static void eraseId(std::vector<uint32_t>& values, uint32_t value) {
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
}

static std::vector<uint32_t> toArticulationIdList(const std::vector<juce::String>& names) {
    std::vector<uint32_t> result;
    result.reserve(names.size());
    for (const auto& name : names) {
        if (name.isNotEmpty())
            addUniqueId(result, StringsDictSingleton::getId(name));
    }
    return result;
}

static std::vector<juce::String> effectiveArticulationFilter(const DrumKitEngine::InstrumentMapEntry& entry) {
    if (!entry.slotArticulationFilter.empty())
        return entry.slotArticulationFilter;
    return entry.articulationFilter;
}

static juce::String makeInstrumentMapEntryFingerprint(const DrumKitEngine::InstrumentMapEntry& entry) {
    auto joinValues = [](const std::vector<juce::String>& values) {
        juce::String out;
        for (size_t idx = 0; idx < values.size(); ++idx) {
            if (idx > 0) out += ",";
            out += values[idx].trim().toLowerCase();
        }
        return out;
    };

    juce::String fp;
    fp += entry.slotId.trim().toLowerCase() + "|";
    fp += entry.kitRef.trim().toLowerCase() + "|";
    fp += entry.instCode.trim().toLowerCase() + "|";
    fp += joinValues(entry.slotArticulationFilter) + "|";
    fp += joinValues(entry.articulationFilter) + "|";
    fp += entry.tuningCents.has_value() ? juce::String(*entry.tuningCents) : "_";
    fp += "|";
    fp += entry.attackTimeMs.has_value() ? juce::String(*entry.attackTimeMs) : "_";
    fp += "|";
    fp += entry.decayTimeMs.has_value() ? juce::String(*entry.decayTimeMs) : "_";
    return fp;
}

static std::unordered_map<juce::String, juce::String> buildSlotFingerprintMap(const DrumKitEngine::InstrumentMap& instrumentMap) {
    std::unordered_map<juce::String, juce::String> result;
    for (const auto& entry : instrumentMap.entries)
        result[entry.slotId] += makeInstrumentMapEntryFingerprint(entry) + "\n";
    return result;
}
}

MicLayout DrumKitEngine::resolveMicLayoutFromConfig() {
    const char* dataPathEnv = "EDRUMS_DATA_PATH";
    const auto defaultDataPath = DrumKitEngine().defaultDataPath;

    juce::File resolvedDataPath;
    auto envInfo = getEnvPath(dataPathEnv);
    if (envInfo.ok()) {
        resolvedDataPath = envInfo.value;
    } else {
        LOG(Notice, "" + envInfo.error());
        resolvedDataPath = defaultDataPath;
    }

    juce::String configuredMicLayout;
    const juce::File configFile = resolvedDataPath.getChildFile("config.yaml");
    if (configFile.existsAsFile()) {
        const auto root = YamlHelper::parse(configFile.loadFileAsString());
        if (!root.isNull()) {
            configuredMicLayout = root["micLayout"].asString().trim();
        } else {
            LOG(Warning, "Unable to parse config.yaml when resolving mic layout. Falling back to default.");
        }
    }

    if (configuredMicLayout.isNotEmpty() && configuredMicLayout != "default") {
        MicLayoutManager micLayoutManager;
        micLayoutManager.initDefault();
        auto scanResult = micLayoutManager.scanLayouts(resolvedDataPath.getChildFile("mic_layouts"));
        if (!scanResult.ok()) {
            LOG(Warning, "resolveMicLayoutFromConfig: " + scanResult.error());
        }

        if (const auto* resolved = micLayoutManager.getLayoutById(configuredMicLayout)) {
            return *resolved;
        }

        LOG(Warning, "resolveMicLayoutFromConfig: MicLayout not found: " + configuredMicLayout + ". Using default.");
    }

    return MicLayout::getDefault();
}


bool DrumKitEngine::useKitLayout(juce::String name) {
    const KitLayout* ptr = this->getKitLayoutManager()->getLayoutById(name);
    if (ptr != nullptr)
        return this->setKitLayout(*ptr);
    if (name != "default") LOG(Warning, "KitLayout not found: " + name);
    return false;
}

bool DrumKitEngine::useMicLayout(juce::String name) {
    const MicLayout* ptr = this->getMicLayoutManager()->getLayoutById(name);
    if (ptr != nullptr)
        return this->setMicLayout(*ptr);
    if (name != "default") LOG(Warning, "MicLayout not found: " + name);
    return false;
}

bool DrumKitEngine::setKitLayout(KitLayout layout) {
    this->activeKitLayoutPtr = std::make_unique<KitLayout>(layout);
    return this->applyKitLayout();
}

bool DrumKitEngine::setMicLayout(MicLayout layout) {
    this->activeMicLayoutPtr = std::make_unique<MicLayout>(layout);
    return this->applyMicLayout();
}

void DrumKitEngine::mapDefaultKit() {
    const auto& kits = this->getInstrumentManager()->getKits();
    const KitLib::DrumKitSpec* kit = kits.empty() ? nullptr : kits[0].get(); //MAYBE: later use global setting for default kit instead
    if (kit != nullptr) {
        this->mapKit(*kit);
        this->regenerateSuggestedInstrumentMapText();
        auto parsedSuggestedMap = DrumKitEngine::parseInstrumentMapText(this->suggestedInstrumentMapText);
        if (parsedSuggestedMap.ok())
            this->lastAppliedSlotFingerprints = buildSlotFingerprintMap(parsedSuggestedMap.value);
        else
            this->lastAppliedSlotFingerprints.clear();
        this->lastInstrumentMapStatus = "Mapped default kit and generated starter instrument map";//FIXME: refactor, ui relates shit out
    } else {
        this->suggestedInstrumentMapText.clear();
        this->lastAppliedSlotFingerprints.clear();
    }
}

void DrumKitEngine::regenerateSuggestedInstrumentMapText() { //TODO: refactor, move to ui related helper, or generate on demand in UI instead of storing in engine
    this->suggestedInstrumentMapText = DrumKitEngineTextView::buildSuggestedInstrumentMapText(this->activeKitLayoutPtr.get(), this->instrumentSynthById);
}

juce::String DrumKitEngine::buildReferenceText() const {
    return DrumKitEngineTextView::buildReferenceText(this->activeKitLayoutPtr.get(), this->getInstrumentManager());
}

void DrumKitEngine::mapKit(const KitLib::DrumKitSpec& kit) { //FIXME: review, probably rewrite, simplify
    if (this->activeKitLayoutPtr == nullptr) {
        LOG(Warning, "no active kit layout to map to");
        return;
    }

    // simple matching pass: by layout id -> kit instrument code, then fuzzy by name/code containment
    std::unordered_set<KitLib::InstrumentSpec*> usedInstSpecSet;

    std::map<KitLib::InstrumentSpec::Type, int> nextIndexByType; // for sequential assignment when layout uses numbering (kick1, kick2)
    

    for (const auto& layoutInstrument : this->activeKitLayoutPtr->instrumentSlots) { //FIXME: rewrite loop thru instrumentSynthById
        KitLib::InstrumentSpec* matched = nullptr;

        auto sit = this->instrumentSynthById.find(layoutInstrument.id);
        if (sit == this->instrumentSynthById.end()) {
            LOG(Warning, "layout instrument slot not present: " + layoutInstrument.id);
            continue;
        }
        InstrumentSynth* slot = sit->second;

        // resolve layout type to KitLib::InstrumentSpec::Type
        KitLib::InstrumentSpec::Type desiredType = layoutInstrument.type;
        
        // If we have a valid type, try type-based selection
        if (desiredType != KitLib::InstrumentSpec::Type::Unknown) {
            auto candidates = kit.getInstrumentsByType(desiredType);
        
            // parse numeric suffix from layout id (e.g., 'kick1' -> index 1)
            int requestedIdx = -1;
            {
                // find trailing digits
                int pos = static_cast<int>(layoutInstrument.id.length()) - 1;
                while (pos >= 0 && juce::CharacterFunctions::isDigit(layoutInstrument.id[pos]))
                    --pos;
                auto digits = layoutInstrument.id.substring(pos + 1);
                if (digits.isNotEmpty()) {
                    requestedIdx = digits.getIntValue() - 1; // convert to 0-based
                }
            }
        
            if (requestedIdx >= 0 && requestedIdx < static_cast<int>(candidates.size())) {
                // pick by explicit position if available and not used
                if (usedInstSpecSet.find(candidates[requestedIdx]) == usedInstSpecSet.end()) {
                    matched = candidates[requestedIdx];
                }
            }
        
            // otherwise pick next unused of that type
            if (matched == nullptr) {
                int& next = nextIndexByType[layoutInstrument.type];
                for (int i = next; i < static_cast<int>(candidates.size()); ++i) {
                    if (usedInstSpecSet.find(candidates[i]) == usedInstSpecSet.end()) {
                        matched = candidates[i];
                        next = i + 1;
                        break;
                    }
                }
                // wrap-around search from beginning if none found after 'next'
                if (matched == nullptr) {
                    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
                        if (usedInstSpecSet.find(candidates[i]) == usedInstSpecSet.end()) {
                            matched = candidates[i];
                            next = i + 1;
                            break;
                        }
                    }
                }
            }
        }
    
        // Fallback: attempt fuzzy match by name/code if still not matched
        if (matched == nullptr) {
            juce::String needle = layoutInstrument.id.trim().toLowerCase();
            for (const auto&[id, instPtr] : kit.getInstrumentSpecsMap()) {
                auto* candidate = instPtr.get();
                if (candidate == nullptr)
                    continue;
                if (usedInstSpecSet.find(candidate) != usedInstSpecSet.end())
                    continue;
                if (candidate->meta.codeRaw.trim().toLowerCase().contains(needle) || candidate->meta.name.trim().toLowerCase().contains(needle)) {
                    matched = candidate;
                    break;
                }
            }
        }

        if (matched == nullptr) {
            LOG(Warning, "no matching instrument found in kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");
            slot->unloadInstrument();
            continue;
        }

        LOG(Info, "Loading instrument '" + matched->meta.codeRaw + "' from kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");

        slot->loadInstrument(matched); //TODO: wrap in performance measuring block
        usedInstSpecSet.insert(matched);
    }

    if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
        for (const auto& instrumentSynthPtr : this->instrumentSynthPtrs) {
            instrumentSynthPtr->setCurrentPlaybackSampleRate(this->lastSampleRate);
            instrumentSynthPtr->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
        }
    }

    // Start staged sample loading only after all instruments/articulations were enqueued
    if (this->sampleLoaderPtr != nullptr) {
        this->sampleLoaderPtr->getQueue().startProcessing();
    }
}

ParseResult<DrumKitEngine::Config> DrumKitEngine::parseConfigYaml(const juce::String yamlStr) {
    ParseResult<DrumKitEngine::Config> result;

    const auto root = YamlHelper::parse(yamlStr);
    if (root.isNull()) {
        return result.returnWithError("Failed to parse YAML: " + yamlStr);
    }

    DrumKitEngine::Config config;

    const auto kitsStr = root["libraryPath"].asString();
    if (kitsStr.isNotEmpty())
        config.libraryPath = resolvePathRelOrAbs(this->dataPath, kitsStr);

    const auto cacheLimit = root["sampleLoaderCacheLimitMegabytes"].asString();
    if (cacheLimit.isNotEmpty())
        config.sampleLoaderCacheLimitMegabytes = static_cast<uint64_t>(juce::uint64(cacheLimit.getLargeIntValue()));

    const auto kitLayout = root["kitLayout"].asString();
    if (kitLayout.isNotEmpty())
        config.kitLayout = kitLayout;

    const auto micLayout = root["micLayout"].asString();
    if (micLayout.isNotEmpty())
        config.micLayout = micLayout;

    result.value = config;
    return result;
}

juce::String DrumKitEngine::getDefaultYaml() {
    juce::String result = "";
    result += "kitLayout: default\n";
    result += "micLayout: default\n";
    result += "sampleLoaderCacheLimitMegabytes: 1024\n";
    const juce::File libDir = this->dataPath.getChildFile("sample_library");
    if (libDir.exists())
        result += "libraryPath: " + libDir.getFullPathName() + "\n";
    return result;
}

void DrumKitEngine::saveSettings() {
    juce::File configFile = dataPath.getChildFile("config.yaml");
    //TODO: write yaml
    jassertfalse;
}

bool DrumKitEngine::applyKitLayout() {
    if (this->activeKitLayoutPtr == nullptr) {
        LOG(Warning, "kit layout was not set");
        return false;
    }
    this->instrumentSynthPtrs.clear();
    this->instrumentSynthById.clear();
    this->instrumentSynthsByNote.clear();
    this->latestCCValues.fill(0);
    this->trackedCCMask.fill(false);
    this->latestPAValues.fill(0);
    this->trackedPAMask.fill(false);

    for (const auto& instrumentSlot : this->getKitLayout()->instrumentSlots) {
        auto slot = std::make_unique<InstrumentSynth>(this, instrumentSlot);
        auto ptr = slot.get();

        this->instrumentSynthById[slot->getId()] = ptr;
        this->instrumentSynthPtrs.push_back(std::move(slot));

        for (const auto& cc : instrumentSlot.controllers) {
            if (cc.number >= 0 && cc.number < 128)
                this->trackedCCMask[cc.number] = true;
        }
        for (const auto& art : instrumentSlot.articulations) {
            for (const int note : art.notes) {
                if (note >= 0 && note < 128)
                    this->trackedPAMask[note] = true;
                this->instrumentSynthsByNote[note].push_back(ptr); //TODO: check ptr safety
            }
        }
    }

    this->primaryInstrumentSynthCount = static_cast<int>(this->instrumentSynthPtrs.size());

    LOG(Info, "Applied kit layout: " + this->activeKitLayoutPtr->id);

    this->buildNoteNameCache();
    return true;
}

void DrumKitEngine::rebuildPrimaryInstrumentNoteRouting() {
    this->instrumentSynthsByNote.clear();
    this->latestPAValues.fill(0);
    this->trackedPAMask.fill(false);

    if (this->activeKitLayoutPtr == nullptr)
        return;

    for (const auto& instrumentSlot : this->activeKitLayoutPtr->instrumentSlots) {
        auto it = this->instrumentSynthById.find(instrumentSlot.id);
        if (it == this->instrumentSynthById.end())
            continue;

        auto* slot = it->second;
        for (const auto& art : instrumentSlot.articulations) {
            for (const int note : art.notes) {
                if (note >= 0 && note < 128)
                    this->trackedPAMask[note] = true;
                this->instrumentSynthsByNote[note].push_back(slot);
            }
        }
    }
}

void DrumKitEngine::clearShadowSynths() {
    if (static_cast<int>(this->instrumentSynthPtrs.size()) > this->primaryInstrumentSynthCount)
        this->instrumentSynthPtrs.erase(this->instrumentSynthPtrs.begin() + this->primaryInstrumentSynthCount, this->instrumentSynthPtrs.end());

    this->rebuildPrimaryInstrumentNoteRouting();
}

void DrumKitEngine::buildNoteNameCache() {
    for (int i = 0; i < 128; ++i) this->noteNameCache[i].clear();

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
    if (this->activeMicLayoutPtr == nullptr) {
        LOG(Warning, "mic layout was not set");
        return false;
    }

    if (this->instrumentSynthPtrs.size() == 0) {
        LOG(Warning, "unable to apply mic layout: no instrument slots was defined in kit layout");
        return false;
    }
    // mic layout is for a mapping of each SampleVariantSpec mic (MicSpec) of InstrumentSynth to coresponding output channel of the plugin
    // the map (sequence and other params defined in MicSpec) of channels is defined in micLayout.
    //  MicSpec to determine which of plugin and routing 
    


    // For each instrument slot, calculate how many mic channels it should have
    // for (size_t idx = 0; idx < this->instrumentSynthPtrs.size(); ++idx) {
    //     InstrumentSynth* slotPtr = this->instrumentSynthPtrs[idx].get();
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

    // Build per-instrument mapping using expected host channel count (defined by plugin outputs)
    // All output buses are mono; stereo mics occupy 2 consecutive mono channels
    const int expectedHostChannels = TOTAL_HOST_OUTPUT_CHANNELS;

    // Initialize empty mapping for each synth
    for (const auto& instPtr : this->instrumentSynthPtrs) {
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

        // Determine target instrument synths //FIXME: move to loadInstrument?
        std::vector<InstrumentSynth*> targets;
        if (mic.instrument.isEmpty()) {
            // assign to all instruments
            for (const auto& isp : this->instrumentSynthPtrs) targets.push_back(isp.get());
        } else {
            auto it = this->instrumentSynthById.find(mic.instrument);
            if (it != this->instrumentSynthById.end()) {
                targets.push_back(it->second);
            } else {
                // fallback: try by instrument type (strip trailing digits)
                juce::String typeStr = mic.instrument;
                // remove trailing digits
                while (typeStr.length() > 0 && juce::CharacterFunctions::isDigit(typeStr.getLastCharacter()))
                    typeStr = typeStr.dropLastCharacters(1);
                auto type = KitLib::InstrumentSpec::typeFromString(typeStr);
                if (type != KitLib::InstrumentSpec::Type::Unknown) {
                    for (const auto& isp : this->instrumentSynthPtrs) {
                        if (isp->getType() == type) targets.push_back(isp.get());
                    }
                }
            }
        }

        if (targets.empty()) {
            LOG(Warning,  "Assigning mic " + mic.name + " (" + juce::String(mic.specs.channels) + "ch)" + " for instrument '" + mic.instrument + "' FAILED: no coresponding instrument slots found in your kit layout");
            outCursor += channelCount; // still reserve host channels
            continue;
        } else {
            LOG(Debug, "Assigning mic " + mic.name + " (" + juce::String(mic.specs.channels) + "ch)" + " for instrument '" + mic.instrument + "'");
        }

        // Host channel indices for this mic — directly maps to mono bus indices
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

    // Apply mapped mic channel counts to synths (used for informational purposes)
    for (const auto& instPtr : this->instrumentSynthPtrs) {
        const auto& map = instPtr->getMicOutputMapping();
        int totalAssigned = static_cast<int>(map.size());
        if (totalAssigned <= 0) totalAssigned = 2;
        instPtr->setMicChannelCount(totalAssigned);
    }

    for (const auto& instPtr : this->instrumentSynthPtrs) {
        auto primaryIt = this->instrumentSynthById.find(instPtr->getId());
        if (primaryIt == this->instrumentSynthById.end())
            continue;

        auto* primary = primaryIt->second;
        if (primary == instPtr.get())
            continue;

        instPtr->setMicOutputMapping(primary->getMicOutputMapping());
        instPtr->setMicChannelCount(primary->getMicChannelCount());
    }

    // Detailed mapping log per instrument
    for (const auto& instPtr : this->instrumentSynthPtrs) {
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
    this->lastSampleRate = sampleRate;
    this->lastSamplesPerBlock = samplesPerBlock;
    for (const auto& instrumentSynthPtr : this->instrumentSynthPtrs) {
        instrumentSynthPtr->setCurrentPlaybackSampleRate(sampleRate);
        instrumentSynthPtr->prepareToPlay(sampleRate, samplesPerBlock);
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
    
    this->saveLatestContinuousControllerValues(midiMessages);
    this->saveLatestPolyphonicAftertouchValues(midiMessages);

    // Each instrument's SampledSound now stores *global* host channel indices in
    // micBufferChannelStart, so voices can render directly into outputBuffer.
    // The per-instrument micBuffer intermediate and local-to-global copy are no longer needed.
    for (const auto& instrumentSynthPtr : this->instrumentSynthPtrs) {
        instrumentSynthPtr->renderNextBlock(outputBuffer, midiMessages, outputBufferStartSample, outputBufferNumSamples);
    }
}

std::optional<juce::String> DrumKitEngine::getNameForMidiNoteNumber(int note, int /* midiChannel */) {
    if (note < 0 || note > 127) return {};

    const auto& name = this->noteNameCache[note];
    if (name.isNotEmpty()) return name;
    return {};
}


void DrumKitEngine::saveLatestContinuousControllerValues(const juce::MidiBuffer& midiMessages) {
    for (const auto& midiMess : midiMessages) {
        const juce::MidiMessage msg = midiMess.getMessage();
        if (!msg.isController()) continue;
        const auto ccNum = msg.getControllerNumber();
        if (ccNum < 0 || ccNum > 127 || !this->trackedCCMask[ccNum]) continue;
        this->latestCCValues[ccNum] = msg.getControllerValue();
    }
}
void DrumKitEngine::saveLatestPolyphonicAftertouchValues(const juce::MidiBuffer& midiMessages) {
    for (const auto& midiMess : midiMessages) {
        const juce::MidiMessage msg = midiMess.getMessage();
        if (!msg.isAftertouch()) continue;
        const auto note = msg.getNoteNumber();
        if (note < 0 || note > 127 || !this->trackedPAMask[note]) continue;
        this->latestPAValues[note] = msg.getAfterTouchValue();
    }
}

bool DrumKitEngine::useInstrumentMap(juce::String text) {
    const juce::ScopedLock graphLock(this->engineGraphLock);
    this->lastInstrumentMapStatus.clear();

    if (text.trim().isEmpty()) {
        this->lastInstrumentMapStatus = "Instrument map is empty";
        LOG(Warning, this->lastInstrumentMapStatus);
        return false;
    }

    if (this->libraryManagerPtr == nullptr) {
        this->lastInstrumentMapStatus = "LibraryManager is not initialized";
        LOG(Error, this->lastInstrumentMapStatus);
        return false;
    }

    if (this->activeKitLayoutPtr == nullptr) {
        this->lastInstrumentMapStatus = "Cannot apply instrument map without an active kit layout";
        LOG(Error, this->lastInstrumentMapStatus);
        return false;
    }

    auto parsed = DrumKitEngine::parseInstrumentMapText(text);
    for (const auto& warning : parsed.errors)
        LOG(parsed.ok() ? Warning : Error, warning);

    if (!parsed.ok()) {
        this->lastInstrumentMapStatus = parsed.error();
        return false;
    }

    struct ShadowPlan {
        const InstrumentMapEntry* entry = nullptr;
        KitLib::InstrumentSpec* instSpec = nullptr;
        std::vector<juce::String> articulations;
    };

    auto applyOverrides = [](InstrumentSynth& synth, const InstrumentMapEntry& entry) {
        synth.setTuningOffsetCents(entry.tuningCents.value_or(0));
        if (entry.attackTimeMs.has_value())
            synth.setAttackOverrideMs(*entry.attackTimeMs);
        if (entry.decayTimeMs.has_value())
            synth.setDecayOverrideMs(*entry.decayTimeMs);
    };

    std::vector<juce::String> slotOrder;
    std::unordered_map<juce::String, std::vector<const InstrumentMapEntry*>> entriesBySlotId;
    for (const auto& entry : parsed.value.entries) {
        if (entriesBySlotId.find(entry.slotId) == entriesBySlotId.end())
            slotOrder.push_back(entry.slotId);
        entriesBySlotId[entry.slotId].push_back(&entry);
    }

    std::unordered_map<juce::String, juce::String> nextSlotFingerprints = buildSlotFingerprintMap(parsed.value);
    bool anyGroupHasMultipleEntries = false;
    for (const auto& slotId : slotOrder) {
        const auto& group = entriesBySlotId[slotId];
        if (group.size() > 1)
            anyGroupHasMultipleEntries = true;
    }

    const bool currentlyHasShadowSynths = static_cast<int>(this->instrumentSynthPtrs.size()) > this->primaryInstrumentSynthCount;
    const bool rebuildAllShadows = currentlyHasShadowSynths || anyGroupHasMultipleEntries;
    if (rebuildAllShadows)
        this->clearShadowSynths();

    juce::StringArray statusLines;
    for (const auto& warning : parsed.errors)
        statusLines.add(warning);

    int applied = 0;
    bool forceApplyAllSlots = false;

    for (const auto& slotId : slotOrder) {
        const auto& group = entriesBySlotId[slotId];
        if (group.empty())
            continue;

        if (slotId == "*") {
            const auto& wildcardEntry = *group.front();
            auto* kitSpec = this->libraryManagerPtr->findKit(wildcardEntry.kitRef);
            if (kitSpec == nullptr) {
                statusLines.add("Kit not found for wildcard entry: " + wildcardEntry.kitRef);
                continue;
            }

            const bool wildcardChanged = this->lastAppliedSlotFingerprints[slotId] != nextSlotFingerprints[slotId];
            if (wildcardChanged) {
                this->mapKit(*kitSpec);
                forceApplyAllSlots = true;
                ++applied;
            }
            continue;
        }

        const bool slotChanged = forceApplyAllSlots || (this->lastAppliedSlotFingerprints[slotId] != nextSlotFingerprints[slotId]);

        auto slotIt = this->instrumentSynthById.find(slotId);
        if (slotIt == this->instrumentSynthById.end()) {
            statusLines.add("Instrument slot not found: " + slotId);
            continue;
        }

        struct ResolvedEntry {
            const InstrumentMapEntry* entry = nullptr;
            KitLib::DrumKitSpec* kitSpec = nullptr;
            KitLib::InstrumentSpec* instSpec = nullptr;
            std::vector<juce::String> effectiveFilter;
        };

        std::vector<ResolvedEntry> resolvedEntries;
        for (const auto* entry : group) {
            auto* kitSpec = this->libraryManagerPtr->findKit(entry->kitRef);
            if (kitSpec == nullptr) {
                statusLines.add("Kit not found: " + entry->kitRef + " for slot '" + entry->slotId + "'");
                continue;
            }

            if (entry->instCode.isEmpty()) {
                statusLines.add("Instrument code is required for slot '" + entry->slotId + "'");
                continue;
            }

            auto* instSpec = kitSpec->findInstrument(entry->instCode);
            if (instSpec == nullptr) {
                statusLines.add("Instrument not found: " + kitSpec->meta.name + "/" + entry->instCode + " for slot '" + entry->slotId + "'");
                continue;
            }

            resolvedEntries.push_back({ entry, kitSpec, instSpec, effectiveArticulationFilter(*entry) });
        }

        if (resolvedEntries.empty())
            continue;

        auto* primarySlot = slotIt->second;
        const auto& slotLayout = primarySlot->getInstrumentSlot();
        const auto& primary = resolvedEntries.front();
        InstrumentSynth::ArticulationSourceMap artSources;
        std::vector<ShadowPlan> shadowPlans;
        std::vector<uint32_t> claimedArticulations;
        std::vector<uint32_t> primaryAllowedArticulationIds;

        if (primary.effectiveFilter.empty()) {
            for (const auto& layoutArt : slotLayout.articulations)
                addUniqueId(primaryAllowedArticulationIds, layoutArt.nameId);
        } else {
            for (const auto& artName : primary.effectiveFilter) {
                if (artName.isNotEmpty())
                    addUniqueId(primaryAllowedArticulationIds, StringsDictSingleton::getId(artName));
            }
        }

        for (const auto& artName : primary.effectiveFilter) {
            if (artName.isNotEmpty())
                addUniqueId(claimedArticulations, StringsDictSingleton::getId(artName));
        }

        for (size_t idx = 1; idx < resolvedEntries.size(); ++idx) {
            const auto& resolved = resolvedEntries[idx];
            if (resolved.effectiveFilter.empty()) {
                shadowPlans.push_back({ resolved.entry, resolved.instSpec, {} });
                continue;
            }

            std::vector<juce::String> layeredArts;
            const bool hasParameterOverrides = resolved.entry->tuningCents.has_value() || resolved.entry->attackTimeMs.has_value() || resolved.entry->decayTimeMs.has_value();
            for (const auto& artName : resolved.effectiveFilter) {
                if (artName.isEmpty())
                    continue;

                const auto artId = StringsDictSingleton::getId(artName);
                if (hasParameterOverrides) {
                    layeredArts.push_back(artName);
                    addUniqueId(claimedArticulations, artId);
                    eraseId(primaryAllowedArticulationIds, artId);
                } else if (!containsId(claimedArticulations, artId)) {
                    addUniqueId(claimedArticulations, artId);
                    artSources[artId] = resolved.instSpec;
                } else {
                    layeredArts.push_back(artName);
                }
            }

            if (!layeredArts.empty())
                shadowPlans.push_back({ resolved.entry, resolved.instSpec, layeredArts });
        }

        if (slotChanged) {
            primarySlot->loadInstrumentWithSources(primary.instSpec, artSources, primaryAllowedArticulationIds);
            applyOverrides(*primarySlot, *primary.entry);
            if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
                primarySlot->setCurrentPlaybackSampleRate(this->lastSampleRate);
                primarySlot->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
            }
            ++applied;
        }

        if (rebuildAllShadows) for (const auto& shadowPlan : shadowPlans) {
            auto shadow = std::make_unique<InstrumentSynth>(this, slotLayout);
            auto* shadowPtr = shadow.get();
            shadowPtr->setMicOutputMapping(primarySlot->getMicOutputMapping());
            shadowPtr->setMicChannelCount(primarySlot->getMicChannelCount());
            shadowPtr->loadInstrumentWithSources(shadowPlan.instSpec, {}, toArticulationIdList(shadowPlan.articulations));
            applyOverrides(*shadowPtr, *shadowPlan.entry);

            if (this->lastSampleRate > 0.0 && this->lastSamplesPerBlock > 0) {
                shadowPtr->setCurrentPlaybackSampleRate(this->lastSampleRate);
                shadowPtr->prepareToPlay(this->lastSampleRate, this->lastSamplesPerBlock);
            }

            for (const auto& art : slotLayout.articulations) {
                if (!shadowPlan.articulations.empty()) {
                    const auto artName = StringsDictSingleton::nameOf(art.nameId);
                    if (std::find(shadowPlan.articulations.begin(), shadowPlan.articulations.end(), artName) == shadowPlan.articulations.end())
                        continue;
                }

                for (const int note : art.notes)
                    this->instrumentSynthsByNote[note].push_back(shadowPtr);
            }

            this->instrumentSynthPtrs.push_back(std::move(shadow));
            ++applied;
        }
    }

    this->lastAppliedSlotFingerprints = std::move(nextSlotFingerprints);

    if (this->sampleLoaderPtr != nullptr)
        this->sampleLoaderPtr->getQueue().startProcessing();

    if (applied <= 0 && statusLines.isEmpty())
        statusLines.add("Instrument map unchanged - no reload needed");

    if (statusLines.isEmpty())
        statusLines.add("Instrument map applied successfully");

    this->lastInstrumentMapStatus = statusLines.joinIntoString("\n");
    return applied > 0;
}

ParseResult<DrumKitEngine::InstrumentMap> DrumKitEngine::parseInstrumentMapText(const juce::String& text) {
    return DrumKitEngineMappingModel::parseInstrumentMapText(text);
}