#include "DrumKitEngine.h"
#include "SampleSelector.h"
#include "../Config/Helpers.h"
#include "../Config/YamlHelper.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>

// tuning constants
static constexpr int NAME_IT_BETTER_MIN_VARIANTS = 3;
static constexpr int NAME_IT_BETTER_MAX_VELOCITY_DISTANCE_TO_USE_SAMPLES_FROM_NEARBY_VELOCITY_LAYERS = 32;
static constexpr int NAME_IT_BETTER_MAX_NEIGHBORS_TO_CONSIDER = 2;

void DrumKitEngine::init(const juce::AudioProcessorValueTreeState& treeState) {
    this->initGlobalSettings();
    this->initDefaultLayouts();
    this->reloadAll();
    this->useState(treeState);
}

void DrumKitEngine::reloadAll() {
    this->initKitLayouts();
    this->initMicLayouts();
    this->initSampleLibrary();
}

void DrumKitEngine::initGlobalSettings() {
    auto envInfo = getEnvPath(DATA_PATH_ENV);
    if (envInfo.ok()) {
        this->dataPath = envInfo.value;
    } else {
        LOG(Warning, "" + envInfo.error());
        this->dataPath = defaultDataPath;
    }

    if (!this->dataPath.exists()) {
        const auto result = this->dataPath.createDirectory();
        if (result.failed()) {
            LOG(Warning, "Could not create data directory: " + result.getErrorMessage());
            jassertfalse;
        }
    }

    juce::File configFile = this->dataPath.getChildFile("config.yaml");
    if (!configFile.existsAsFile()) {
        auto result = configFile.create();
        if (result.failed()) {
            LOG(Warning, "Could not create config file: " + result.getErrorMessage());
            jassertfalse;
        } else {
            if (!configFile.replaceWithText(this->getDefaultYaml())) {
                LOG(Warning, "Could not write default config.yaml");
                jassertfalse;
            }
        }
    }

    auto yamlStr = configFile.loadFileAsString();
    auto conf = this->parseConfigYaml(yamlStr);
    if (conf.ok()) {
        this->globalConfig = conf.value;
    } else {
        LOG(Error, "engine config parse error: " + conf.error());
        jassertfalse;
    }
}

void DrumKitEngine::initDefaultLayouts() {
    this->kitLayoutManagerPtr->initDefault();
    this->micLayoutManagerPtr->initDefault();
}

void DrumKitEngine::initKitLayouts() {
    const juce::File layoutsDir = this->dataPath.getChildFile("kit_layouts");
    auto result = this->kitLayoutManagerPtr->scanLayouts(layoutsDir);
    if (result.ok()) {
        LOG(Info, "found kit layouts: " + juce::String(result.value));
    } else {
        LOG(Warning, "init kit layouts: " + result.error());
    }
}

void DrumKitEngine::initMicLayouts() {
    const juce::File layoutsDir = this->dataPath.getChildFile("mic_layouts");
    auto result = this->micLayoutManagerPtr->scanLayouts(layoutsDir);
    if (result.ok()) {
        LOG(Info, "found mic layouts: " + juce::String(result.value));
    } else {
        LOG(Warning, "init mic layouts: " + result.error());
    }
}

void DrumKitEngine::initSampleLibrary() {
    if (!this->globalConfig.libraryPath.isDirectory()) {
        LOG(Error, "Sample library not found under " + this->globalConfig.libraryPath.getFullPathName());
        jassertfalse;
        return;
    }

    this->libraryManagerPtr = std::make_unique<KitLib::LibraryManager>(this->globalConfig.libraryPath);
    this->libraryManagerPtr->scanAll();

    this->sampleLoaderPtr = std::make_unique<SampleLoader>();
    this->sampleLoaderPtr->setCacheLimitBytes(this->globalConfig.sampleLoaderCacheLimitMegabytes * 1024ull * 1024ull);
}

bool DrumKitEngine::useKitLayout(juce::String name) {
    const KitLayout* ptr = this->getKitLayoutManager()->getLayoutById(name);
    if (ptr != nullptr)
        this->setKitLayout(*ptr);
    if (name != "default") LOG(Warning, "KitLayout not found: " + name);
    return false;
}

bool DrumKitEngine::useMicLayout(juce::String name) {
    const MicLayout* ptr = this->getMicLayoutManager()->getLayoutById(name);
    if (ptr != nullptr)
        this->setMicLayout(*ptr);
    if (name != "default") LOG(Warning, "MicLayout not found: " + name);
    return false;
}

bool DrumKitEngine::useInstrumentMap(juce::String text) { // not working draft
    if (text.trim().isEmpty()) {
        LOG(Warning, "empty instrument map text");
        return false;
    }

    auto pairs = DrumKitEngine::parseInstrumentMapText(text); //TODO: plan implementation
    if (pairs.empty()) {
        LOG(Warning, "no valid mapping lines found");
        return false;
    }

    if (this->libraryManagerPtr == nullptr) {
        LOG(Error, "LibraryManager is not initialized");
        return false;
    }

    int applied = 0;
    for (const auto& p : pairs) {
        const auto slotId = p.first.trim();
        const auto rhs = p.second.trim();

        // find slot
        auto sit = this->instrumentSynthById.find(slotId);
        if (sit == this->instrumentSynthById.end()) {
            LOG(Warning, "instrument slot not found: " + slotId);
            continue;
        }
        InstrumentSynth* slot = sit->second;

        // parse kit and optional instrument code
        auto slashPos = rhs.indexOfChar('/');
        juce::String kitName = rhs;
        juce::String instCode;
        if (slashPos >= 0) {
            kitName = rhs.substring(0, slashPos).trim();
            instCode = rhs.substring(slashPos + 1).trim();
        }

        // find kit (by name or code)
        KitLib::DrumKitSpec* kitSpec = nullptr;
        for (auto& kptr : this->libraryManagerPtr->getKits()) {
            if (kptr->meta.name == kitName || kptr->meta.code == kitName) {
                kitSpec = kptr.get();
                break;
            }
        }

        if (kitSpec == nullptr) {
            LOG(Warning, "kit not found: " + kitName);
            continue;
        }

        if (instCode.isEmpty()) {
            this->mapKit(*kitSpec);
            applied++;
            continue;
        }

        KitLib::InstrumentSpec* instSpec = kitSpec->findInstrument(instCode);
        if (instSpec == nullptr) {
            LOG(Warning, "instrument not found in kit '" + kitSpec->meta.name + "': " + instCode);
            continue;
        }

        slot->loadInstrument(instSpec);
        applied++;

        // map midi notes for this slot based on kit layout definition (if present)
        if (this->activeKitLayoutPtr != nullptr) {
            auto it = this->activeKitLayoutPtr->instrumentsById.find(slotId);
            if (it != this->activeKitLayoutPtr->instrumentsById.end()) {
                const auto* layoutInst = it->second;
                for (const auto& art : layoutInst->articulations) {
                    for (int note : art.notes) {
                        auto& vec = this->instrumentSynthsByNote[note];
                        // avoid duplicates
                        if (std::find(vec.begin(), vec.end(), slot) == vec.end())
                            vec.push_back(slot);
                    }
                }
            }
        }
    }

    return applied > 0;
}

bool DrumKitEngine::setKitLayout(KitLayout layout) {
    this->activeKitLayoutPtr = std::make_unique<KitLayout>(layout);
    return this->applyKitLayout();
}

bool DrumKitEngine::setMicLayout(MicLayout layout) {
    this->activeMicLayoutPtr = std::make_unique<MicLayout>(layout);
    return this->applyMicLayout();
}

void DrumKitEngine::useState(const juce::AudioProcessorValueTreeState& treeState) {
    auto kitLayout = treeState.state["kit_layout"].toString();
    if (kitLayout.isEmpty() || !this->useKitLayout(kitLayout)) {
        if (this->activeKitLayoutPtr == nullptr) {
            kitLayout = this->globalConfig.kitLayout;
            if (kitLayout.isEmpty() || !this->useKitLayout(kitLayout)) {
                LOG(Warning, "Using default kit layout");
                auto src = this->getKitLayoutManager()->getDefault();
                this->setKitLayout(src);
            }
        }
    }

    auto micLayout = treeState.state["mic_layout"].toString();
    if (micLayout.isEmpty() || !this->useMicLayout(micLayout)) {
        if (this->activeMicLayoutPtr == nullptr) {
            micLayout = this->globalConfig.micLayout;
            if (micLayout.isEmpty() || !this->useMicLayout(micLayout)) {
                LOG(Warning, "Using default mic layout");
                auto src = this->getMicLayoutManager()->getDefault();
                this->setMicLayout(src);
            }
        }
    }

    auto instrumentMap = treeState.state["instrument_map"].toString();
    if (instrumentMap.isEmpty()) {
        this->mapDefaultKit();
    } else {
        this->useInstrumentMap(instrumentMap);
    }
}

void DrumKitEngine::mapDefaultKit() {
    KitLib::DrumKitSpec* kit = this->getInstrumentManager()->getKit(0); //MAYBE: later use global setting for default kit instead
    if (kit != nullptr)
        this->mapKit(*kit);
}

void DrumKitEngine::mapKit(KitLib::DrumKitSpec& kit) { //FIXME: review, probably rewrite, simplify
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
            for (const auto&[id, instPtr] : kit.instrumentSpecsMap) {
                auto* candidate = instPtr.get();
                if (candidate == nullptr)
                    continue;
                if (usedInstSpecSet.find(candidate) != usedInstSpecSet.end())
                    continue;
                if (candidate->meta.code.trim().toLowerCase().contains(needle) || candidate->meta.name.trim().toLowerCase().contains(needle)) {
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
        
        LOG(Info, "loading instrument '" + matched->meta.code + "' from kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");
        
        slot->loadInstrument(matched); //TODO: wrap in performance measuring block
        usedInstSpecSet.insert(matched);

        //FIXME: map MIDI notes for articulations?? WHY? isnt it already done
        // for (const auto& art : layoutInstrument.articulations) {
        //     for (int note : art.notes) {
        //         auto& vec = instrumentSynthsByNote[note];
        //         if (std::find(vec.begin(), vec.end(), slot) == vec.end())
        //             vec.push_back(slot);
        //     }
        // }
    }
}

//TODO: Each non-empty, non-comment line should be: <slotId>:<KitName>/<InstrumentCode>:<params? like tuning, fx chain, etc> //FIXME: make parser return structured ParseResult<DataObjectToBeDefined> 
std::vector<std::pair<juce::String, juce::String>> DrumKitEngine::parseInstrumentMapText(const juce::String& text) { 
    std::vector<std::pair<juce::String, juce::String>> result;
    const juce::StringArray lines = juce::StringArray::fromLines(text);
    for (auto l : lines) {
        auto line = l.trim();
        if (line.isEmpty())
            continue;
        if (line.startsWithChar('#'))
            continue;

        auto colon = line.indexOfChar(':');
        if (colon <= 0) {
            LOG(Warning, "invalid mapping line (no colon): " + line);
            continue;
        }
        auto left = line.substring(0, colon).trim();
        auto right = line.substring(colon + 1).trim();
        if (left.isEmpty() || right.isEmpty()) {
            LOG(Warning, "invalid mapping line (empty slot or target): " + line);
            continue;
        }
        result.emplace_back(left, right);
    }
    return result;
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
    this->latestCcValueMap.clear();

    for (const auto& instrumentSlot : this->getKitLayout()->instrumentSlots) {
        auto slot = std::make_unique<InstrumentSynth>(this, instrumentSlot);
        auto ptr = slot.get();

        this->instrumentSynthById[slot->getId()] = ptr;
        this->instrumentSynthPtrs.push_back(std::move(slot));

        for (const auto& cc : instrumentSlot.controllers) {
            this->latestCcValueMap[cc.number] = 0;
        }
        for (const auto& art : instrumentSlot.articulations) {
            for (const int note : art.notes) {
                this->instrumentSynthsByNote[note].push_back(ptr);
            }
        }
    }

    LOG(Info, "Applied kit layout: " + this->activeKitLayoutPtr->id);
    return true;
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
    const int expectedHostChannels = 32; // PluginProcessor defines 16 stereo outputs -> 32 channels

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

        // Determine target instrument synths //FIXME: move to loadInstrument
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
            LOG(Warning, "Mic '" + mic.name + "' with instrument '" + mic.instrument + "' could not be assigned to any instrument slot");
            outCursor += channelCount; // still reserve host channels
            continue;
        }

        // Host channel indices for this mic
        std::vector<int> hostIdx;
        for (int i = 0; i < channelCount; ++i) hostIdx.push_back(outCursor + i);

        // Append host indices to each target instrument's mapping
        for (auto* t : targets) {
            auto existing = t->getMicOutputMapping();
            std::vector<int> merged = existing;
            merged.insert(merged.end(), hostIdx.begin(), hostIdx.end());
            t->setMicOutputMapping(merged);
        }

        outCursor += channelCount;
    }

    // Apply mapped mic channel counts to synths and size mic buffers if we know the block size
    for (const auto& instPtr : this->instrumentSynthPtrs) {
        const auto& map = instPtr->getMicOutputMapping();
        int totalAssigned = static_cast<int>(map.size());
        if (totalAssigned <= 0) {
            // Fallback default to stereo
            totalAssigned = 2;
        }
        instPtr->setMicChannelCount(totalAssigned);
        if (this->lastSamplesPerBlock > 0) {
            instPtr->getMicBuffer().setSize(totalAssigned, this->lastSamplesPerBlock);
        }
    }

    // Detailed mapping log per instrument
    for (const auto& instPtr : this->instrumentSynthPtrs) {
        juce::String mapStr = "";
        const auto& map = instPtr->getMicOutputMapping();
        for (size_t i = 0; i < map.size(); ++i) {
            if (i) mapStr += ",";
            mapStr += juce::String(map[i]);
        }
        if (mapStr.isEmpty()) mapStr = "(none)";
        LOG(Info, "Instrument '" + instPtr->getId() + "' mapping -> " + mapStr + " (mic channels: " + juce::String(instPtr->getMicChannelCount()) + ")");
    }

    LOG(Info, "Applied mic layout: " + this->activeMicLayoutPtr->id + " (assigned host channels: " + juce::String(outCursor) + ")");
    return true;
}

void DrumKitEngine::prepareToPlay(double sampleRate, int samplesPerBlock) {
    this->lastSamplesPerBlock = samplesPerBlock;
    for (const auto& instrumentSynthPtr : this->instrumentSynthPtrs) {
        instrumentSynthPtr->setCurrentPlaybackSampleRate(sampleRate);
        instrumentSynthPtr->prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void DrumKitEngine::processBlock(juce::AudioBuffer<float>& outputBuffer, juce::MidiBuffer& midiMessages) { //TODO: review
    const auto outputBufferStartSample = 0;
    const auto outputBufferNumSamples = outputBuffer.getNumSamples();
    
    this->saveLatestContinuousControllerValues(midiMessages);

    for (const auto& instrumentSynthPtr : this->instrumentSynthPtrs) {
        auto& micBuffer = instrumentSynthPtr->getMicBuffer();
        micBuffer.clear();

        // Render into own mic buffer
        instrumentSynthPtr->renderNextBlock(micBuffer, midiMessages, outputBufferStartSample, outputBufferNumSamples);

        // Mix mic buffer channels into host output using per-instrument mapping (if present)
        const int outCh = outputBuffer.getNumChannels();
        const int micCh = micBuffer.getNumChannels();
        const auto& mapping = instrumentSynthPtr->getMicOutputMapping();

        for (int c = 0; c < micCh; ++c) {
            int dest = -1;
            bool mappingUsed = false;
            if (c < static_cast<int>(mapping.size())) {
                dest = mapping[c];
                mappingUsed = true;
            }
            if (dest < 0 || dest >= outCh) {
                // Log when mapping refers to non-existing host channels
                if (mappingUsed) {
                    LOG(Warning, "Instrument '" + instrumentSynthPtr->getId() + "' mapping for mic channel " + juce::String(c) + " -> host index " + juce::String(dest) + " is out of range (host channels=" + juce::String(outCh) + "). Falling back to wrap index.");
                }
                // Fallback: wrap to available outputs
                dest = c % outCh;
            }
            outputBuffer.addFrom(dest, outputBufferStartSample, micBuffer, c, outputBufferStartSample, outputBufferNumSamples, 1.0f);
        }
    }
}

void DrumKitEngine::saveLatestContinuousControllerValues(const juce::MidiBuffer& midiMessages) {
    for (const auto& midiMess : midiMessages) {
        const juce::MidiMessage msg = midiMess.getMessage();
        if (!msg.isController()) continue;
        const auto ccNum = msg.getControllerNumber();
        if (this->latestCcValueMap.find(ccNum) == this->latestCcValueMap.end()) continue;
        this->latestCcValueMap[ccNum] = msg.getControllerValue();
    }
}

void InstrumentSynth::unloadInstrument() {
    this->runtimeArticulations.clear();
    this->sampledSoundPtrs.clear();
    this->noteToArticulation.fill(-1);
    this->clearSounds();
    this->instrumentSpecstPtr = nullptr;
}

void InstrumentSynth::loadInstrument(KitLib::InstrumentSpec* instSpec) {
    if (instSpec == nullptr) return;
    this->unloadInstrument();
    this->instrumentSpecstPtr = instSpec;
    this->type = instSpec->meta.type;

    auto loader = this->getSampleLoader();

    // For each layout articulation create runtime entry
    for (const KitLayout::InstrumentSlot::ArticulationTrigger& layoutArt : this->instrumentSlot.articulations) {
        InstrumentSynth::ArticulationRuntime artRt;
        artRt.codeRaw = layoutArt.codeRaw;

        LOG(Debug, "Processing articulation: " + layoutArt.codeRaw);
        std::unordered_map<int8_t, std::vector<std::pair<int8_t, int8_t>>> foundCcDimensions;

        bool found = false;
        int unmatchedStart = 0;
        for (const auto& artSpec : instSpec->articulationSpecs) {
            if (artSpec->codeIdx != layoutArt.codeIdx) continue;

            found = true;

            // ensure velocity layers are sorted
            artSpec->sortVelocityLayers();

            // Build velocity layers and create SampledSound objects
            // We want each mic (as defined in active mic layout) to occupy a contiguous block
            // in the synth's micBuffer. We'll group sampleVariantSpecs by matching mic entry
            // (type+position+format) and assign starts accordingly.
            std::vector<int> micStarts; // start channel in micBuffer for each mic entry assigned to this instrument
            std::vector<int> micChannels; // number of channels for each mic entry
            std::vector<const MicLayout::Mic*> micEntriesForInst;

            // Gather mic layout entries that apply to this instrument in the same order as applyMicLayout()
            if (this->parent && this->parent->getMicLayout() != nullptr) {
                const MicLayout* ml = this->parent->getMicLayout();
                for (const auto& mic : ml->mics) {
                    bool applies = false;
                    if (mic.instrument.isEmpty()) applies = true;
                    if (mic.instrument == this->getId()) applies = true;
                    if (!applies) {
                        // try fallback by type (strip digits)
                        juce::String typeStr = mic.instrument;
                        while (typeStr.length() > 0 && juce::CharacterFunctions::isDigit(typeStr.getLastCharacter()))
                            typeStr = typeStr.dropLastCharacters(1);
                        auto instType = KitLib::InstrumentSpec::typeFromString(typeStr);
                        if (instType != KitLib::InstrumentSpec::Type::Unknown && instType == this->getType()) applies = true;
                    }

                    if (applies) {
                        micEntriesForInst.push_back(&mic);
                    }
                }
            }

            // Debug: list mic entries that apply to this instrument
            {
                juce::String s = "Mic entries for inst '" + this->getId() + "': ";
                for (size_t i = 0; i < micEntriesForInst.size(); ++i) {
                    const auto* m = micEntriesForInst[i];
                    s += "[" + juce::String(i) + ":" + m->name + " (" + juce::String(m->specs.channels) + "ch)] ";
                }
                LOG(Debug, s);
            }

            // Helper to compare mic specs (ignore variant suffix)
            auto micKeyEq = [](const KitLib::MicSpec& a, const KitLib::MicSpec& b) {
                return a.type == b.type && a.position == b.position && a.format == b.format; // ignore variant
            };

            // Count how many variants belong to each mic entry across all velocity layers
            std::unordered_map<size_t, int> variantsPerMic; // index in micEntriesForInst -> count
            std::vector<std::pair<const KitLib::SampleVariantSpec*, int>> allVariants; // keep order for assignment
            for (const auto& velSpec : artSpec->velocityLayerSpecs) {
                for (const auto& varSpec : velSpec->sampleVariantSpecs) {
                    allVariants.emplace_back(varSpec.get(), 0);
                }
            }

            for (size_t vi = 0; vi < allVariants.size(); ++vi) {
                const KitLib::SampleVariantSpec* var = allVariants[vi].first;
                bool matched = false;
                for (size_t mi = 0; mi < micEntriesForInst.size(); ++mi) {
                    const auto* mic = micEntriesForInst[mi];
                    if (micKeyEq(var->micSpec, mic->specs)) {
                        variantsPerMic[mi] += 1;
                        allVariants[vi].second = static_cast<int>(mi);
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    // assign fallback: try to match by mic type, else use first mic entry
                    int fallback = -1;
                    for (size_t mi = 0; mi < micEntriesForInst.size(); ++mi) {
                        if (micEntriesForInst[mi]->specs.type == var->micSpec.type) {
                            fallback = static_cast<int>(mi);
                            break;
                        }
                    }
                    if (fallback == -1 && !micEntriesForInst.empty()) fallback = 0;
                    if (fallback >= 0) {
                        variantsPerMic[fallback] += 1;
                        allVariants[vi].second = fallback;
                    } else {
                        // no mic entries at all: mark as unmatched
                        variantsPerMic[SIZE_MAX] += 1;
                        allVariants[vi].second = -1;
                    }
                }
            }

            // Debug: show how many variants matched each mic and unmatched count
            {
                juce::String s = "Variant counts per mic for inst '" + this->getId() + "': ";
                for (size_t mi = 0; mi < micEntriesForInst.size(); ++mi) {
                    int cnt = variantsPerMic.count(mi) ? variantsPerMic[mi] : 0;
                    s += "[" + juce::String(mi) + ":" + juce::String(cnt) + "] ";
                }
                int unmatched = variantsPerMic.count(SIZE_MAX) ? variantsPerMic[SIZE_MAX] : 0;
                s += " unmatched=" + juce::String(unmatched);
                LOG(Debug, s);
            }

            // Build micStarts using the instrument's actual host mapping so that
            // mic-buffer indices align with the host-output mapping assigned earlier

            int mappingCursor = 0;
            for (size_t mi = 0; mi < micEntriesForInst.size(); ++mi) {
                micStarts.push_back(mappingCursor);
                int ch = micEntriesForInst[mi]->specs.channels;
                micChannels.push_back(ch);
                mappingCursor += ch;
            }

            // Debug: show computed micStarts/micChannels
            {
                juce::String s2 = "Computed micStarts/ch for inst '" + this->getId() + "': ";
                for (size_t i = 0; i < micStarts.size(); ++i) {
                    if (i) s2 += ",";
                    s2 += "[" + juce::String(micStarts[i]) + ", " + juce::String(micChannels[i]) + "ch]";
                }
                s2 += " mappingCursor=" + juce::String(mappingCursor);
                LOG(Debug, s2);
            }
            // unmatched variants (if any) go after mapped region
            unmatchedStart = mappingCursor;
            int unmatchedCount = variantsPerMic.count(SIZE_MAX) ? variantsPerMic[SIZE_MAX] : 0;
            // We'll use each variant's own channel count for unmatched variants.

            // Now create sounds and assign micBufferChannelStart according to grouping
            size_t variantIdx = 0;
            for (const auto& velSpec : artSpec->velocityLayerSpecs) {
                VelocityLayerRuntime layer;
                layer.velocity = velSpec->velocity;
                layer.samples.reserve(velSpec->sampleVariantSpecs.size());
                for (const auto& varSpec : velSpec->sampleVariantSpecs) {
                    auto* sound = new SampledSound(instSpec, varSpec.get(), varSpec->sampleFile, layoutArt.notes);
                    int fileChannelCount = varSpec->fileChannelCount > 0 ? varSpec->fileChannelCount : varSpec->micSpec.channels;

                    int assignedMicIndex = allVariants[variantIdx].second;
                    if (assignedMicIndex >= 0) {
                        int start = micStarts[assignedMicIndex];
                        int ch = micChannels[assignedMicIndex];
                        // All variants for the same mic map to the same mic buffer group
                        sound->micBufferChannelStart = start;
                        sound->micBufferChannelCount = ch;
                    } else {
                        // unmatched: append after mic blocks, allocate sequentially using fileChannelCount
                        sound->micBufferChannelStart = unmatchedStart;
                        sound->micBufferChannelCount = fileChannelCount;
                        unmatchedStart += fileChannelCount;
                    }

                    // Debug log: show mapping of this SampledSound -> mic buffer channels and file channel ranges
                    // if (sound->sampleVariantSpec) {
                    //     juce::String sname = sound->sampleVariantSpec->sampleFileName;
                    //     int fileStart = sound->sampleVariantSpec->fileChannelStart;
                    //     int fileCount = sound->sampleVariantSpec->fileChannelCount > 0 ? sound->sampleVariantSpec->fileChannelCount : sound->sampleVariantSpec->micSpec.channels;
                    //     LOG(Debug, "channels for " + sname + " -> micBuf: start=" + juce::String(sound->micBufferChannelStart) + " count=" + juce::String(sound->micBufferChannelCount) 
                    //         + ", file: start=" + juce::String(fileStart) + ", count=" + juce::String(fileCount));
                    // }

                    this->addSound(sound);
                    this->sampledSoundPtrs.push_back(sound);
                    SampleEntry se;
                    se.sound = sound;
                    // Store variant key (1..26) as returned by SampleVariantSpec::letterToIdx
                    // If no letter, value will be 0.
                    se.variantIdx = static_cast<uint8_t>(KitLib::SampleVariantSpec::letterToIdx(varSpec->letter));
                    layer.samples.push_back(se);

                    if (loader != nullptr) {
                        loader->requestAsyncLoad(varSpec->sampleFile, [sound](std::shared_ptr<juce::AudioBuffer<float>> buf, double sr) {
                            sound->buffer = buf;
                            sound->sampleRate = sr;
                        });
                    }
                    ++variantIdx;
                }
                artRt.layers.push_back(std::move(layer));
            }

            // Before mapping velocities, log computed mic buffer requirements vs host mapping
            int computedMicChannels = unmatchedStart;
            const auto& hostMapForInst = this->getMicOutputMapping();
            int assignedHostChannels = static_cast<int>(hostMapForInst.size());
            LOG(Debug, "Instrument '" + this->getId() + "' computed mic channels=" + juce::String(computedMicChannels) + ", assigned host mapping channels=" + juce::String(assignedHostChannels));
            if (assignedHostChannels > 0 && computedMicChannels != assignedHostChannels) {
                LOG(Warning, "Instrument '" + this->getId() + "' computed mic channels (" + juce::String(computedMicChannels) + ") differs from host mapping channels (" + juce::String(assignedHostChannels) + "). This may cause out-of-range writes.");
            }

            // Map velocities to layers using upper-bound semantics:
            if (!artRt.layers.empty()) {
                // choose the first layer whose configured velocity is >= current velocity.
                // Example: layers {27,52,77,102,127} -> 0..27->0, 28..52->1, 53..77->2, 78..102->3, 103..127->4
                const int8_t count = static_cast<int8_t>(artRt.layers.size());
                for (int value = 0; value <= MIDI_MAX_VALUE; ++value) {
                    int8_t chosen = count - 1; // default to last layer
                    for (int8_t idx = 0; idx < count; ++idx) {
                        if (value <= static_cast<int>(artRt.layers[idx].velocity)) {
                            chosen = idx;
                            break;
                        }
                    }
                    artRt.velocityToLayer[value] = chosen;
                }
            }
            

            // Map layout notes to this articulation index
            int8_t artIdx = static_cast<int8_t>(this->runtimeArticulations.size());
            for (uint8_t note : layoutArt.notes) {
                this->noteToArticulation[note] = artIdx;
            }



            // Prepare note -> controller and controllerValue -> articulation mappings per subscribed CC
            for (const auto& cc : this->instrumentSlot.controllers) {
                // build reverse map of note -> controllers
                for (uint8_t note : cc.notes) {
                    auto ccNum = static_cast<uint8_t>(cc.number);
                    auto& vec = this->noteToControllers[note];
                    if (std::find(vec.begin(), vec.end(), ccNum) == vec.end()) {
                        vec.emplace_back(ccNum);
                    }
                }
                
                // only controllers with notes that are linked to this articulation
                if (containsSameValue(cc.notes, layoutArt.notes)) {
                    // store refs for dimensions to process later
                    for (const auto& expDim : artSpec->expressionDimensions) {
                        if (expDim->type == cc.codeIdx) {
                            for (const auto& val : expDim->values) {
                                auto ccNum = static_cast<uint8_t>(cc.number);
                                const auto pair = std::make_pair(static_cast<int8_t>(val), artIdx);
                                auto& vec = foundCcDimensions[ccNum];
                                vec.emplace_back(pair);
                            }
                        }
                    }
                }
            }
            this->runtimeArticulations.push_back(std::move(artRt));
        } 

        for (auto&[ccNum, valArtPairs] : foundCcDimensions) {            
            std::sort(valArtPairs.begin(), valArtPairs.end(), [](auto& a, auto& b){
                return a.first < b.first;
            });

            std::array<uint8_t, 128> map;
            const int8_t count = static_cast<uint8_t>(valArtPairs.size());
    
            for (int value = 0; value <= MIDI_MAX_VALUE; ++value) {
                uint8_t chosen = count - 1; // default to last
                for (uint8_t idx = 0; idx < count; ++idx) {
                    if (value <= static_cast<int>(valArtPairs[idx].first)) {
                        chosen = valArtPairs[idx].second;
                        break;
                    }
                }
                map[value] = chosen;
            }
            this->ccToArticulation[ccNum] = map;
        }
        
        if (!found) {
            LOG(Warning, "no kit articulation found for layout art: " + layoutArt.codeRaw);
            // push empty runtime articulation to preserve indices
            // this->runtimeArticulations.push_back(std::move(artRt));
            // continue;
        }

    }
}

void InstrumentSynth::handleMidiEvent(const juce::MidiMessage& midiMessage) {
    
    

    if (midiMessage.isNoteOn()) {
        if (noteToArticulation[midiMessage.getNoteNumber()] == -1) return;

        this->playNote(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getVelocity(), 1);
        // || this->noteOn(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getFloatVelocity());
    }
    else if (midiMessage.isController()) {
        auto ccNum = static_cast<uint8_t>(midiMessage.getControllerNumber());
        if (noteToControllers.find(ccNum) == noteToControllers.end()) return;


        // this->handleController(midiMessage.getChannel(), ccNum, midiMessage.getControllerValue());
    }
    
    //TODO: common realistic choke (volume change) on aftertouch: with some custom decay timeout, if aftertouch value became less in that timeout, volume should return back by some percent
    else if (midiMessage.isAftertouch())
        this->handleAftertouch(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getAfterTouchValue());
    
        
    else if (midiMessage.isAllNotesOff() || midiMessage.isAllSoundOff()) this->allNotesOff(midiMessage.getChannel(), true);
        
        

    // else if (midiMessage.isNoteOff()) this->noteOff (channel, midiMessage.getNoteNumber(), midiMessage.getFloatVelocity(), true);
    // else if (midiMessage.isChannelPressure()) this->handleChannelPressure(channel, midiMessage.getChannelPressureValue());
    // else if (midiMessage.isProgramChange()) this->handleProgramChange(channel, midiMessage.getProgramChangeNumber());
    // else if (midiMessage.isPitchWheel()) {
    //     const int wheelPos = midiMessage.getPitchWheelValue();
    //     lastPitchWheelValues[channel - 1] = wheelPos;
    //     this->handlePitchWheel(channel, wheelPos);
    // } 
    
}

bool InstrumentSynth::playNote(const int midiChannel, const int midiNoteNumber, const juce::uint8 velocity, int expressions) {
    
    // SampledSound* chosen = this->selectSampleFor(targetArt, midiVel);
    // if (chosen == nullptr) {
    //     LOG(Info, "Nothing to play for " + targetArt);
    //     return;
    // }


    /* TODO: 
        if note louder then last > choke (kill or fast fade? using NAME_THIS_VAR_RIGHT_CHOKED_BY_LOUDER_TIMEOUT_MS) playing notes of this instrument's articulations that are chokable for this articulation. 
        create and define logic for what instruments articulation can choke other articulation, like cymbal bow/edge can choke each other and self (if last is louder)
        for hihat pedal state without note should crossfade changes, like if hitted closed hat and start opening pedal it should crossfade to more opened sound (without attack of course) so more opened starts from time offset when pedal changestarted with NAME_THIS_VAR_RIGHT_HIHAT_PEDAL_CHANGE_CROSSFADE_MS
        probably have to use juces shouldStealNotes and override findVoiceToSteal 
        need to make InstrumentSynth abstract and extend with several classes like InstrumentSynthCymbal, InstrumentSynthDrum and for special cases InstrumentSynthHiHat, 
    */


    //     if (voice->getCurrentlyPlayingNote() == midiNoteNumber && voice->isPlayingChannel (midiChannel))
    //         stopVoice (voice, 1.0f, true);

    // startVoice (findFreeVoice (sound, midiChannel, midiNoteNumber, shouldStealNotes), sound, midiChannel, midiNoteNumber, velocity);
    
    juce::ignoreUnused(expressions);

    // Choose articulation based on MIDI note mapping
    int artIdx = -1;
    if (this->runtimeArticulations.empty()) {
        // fallback: try to use first layout articulation if any
        if (!this->instrumentSlot.articulations.empty()) {
            // find index 0
            artIdx = 0;
        }
    } else {
        if (midiNoteNumber >= 0 && midiNoteNumber < 128) {
            artIdx = this->noteToArticulation[midiNoteNumber];
            
            if (!this->ccToArticulation.empty() && this->parent != nullptr) {
                // const auto& ccValues = this->parent->getLatestContinuousControllerValuesRef();
                auto pair = this->noteToControllers.find(static_cast<uint8_t>(midiNoteNumber));
                if (pair != this->noteToControllers.end()) {
                    for (auto ccNum : pair->second) {
                        const auto& ccLatestValues = this->parent->getLatestContinuousControllerValuesRef();
                        auto it = ccLatestValues.find(ccNum);
                        if (it == ccLatestValues.end()) continue; //wtf

                        int ccVal = it->second;
                        if (ccVal < 0) ccVal = 0;
                        if (ccVal > 127) ccVal = 127;

                        auto artIt = this->ccToArticulation.find(ccNum);
                        if (artIt == this->ccToArticulation.end()) continue;
                        const auto& mapArr = artIt->second;

                        artIdx = static_cast<int>(mapArr[ccVal]);
                        LOG(Debug, "CC " + juce::String(ccNum) + " value " + juce::String(ccVal) + " maps to expression " + juce::String(artIdx) + " for note " + juce::String(midiNoteNumber) );
                    }
                }
            }
        }
    }

    if (artIdx < 0) {
        // LOG(Warning, "InstrumentSynth::playNote - no articulation available for note " + juce::String(midiNoteNumber));
        return false;
    }

    // Play all samples from the chosen velocity layer (one voice per sample/mic variant)
    auto& art = this->runtimeArticulations[artIdx];
    int layerIdx = art.velocityToLayer[static_cast<int>(velocity)];
    if (layerIdx < 0 || layerIdx >= static_cast<int>(art.layers.size())) return false;

    auto& layer = art.layers[layerIdx];
    if (layer.samples.empty()) {
        LOG(Info, "Nothing to play for note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)));
        return false;
    }

    const juce::ScopedLock sl (this->lock);
    const bool stealNotes = true;
    float velf = juce::jlimit(0.0f, 1.0f, static_cast<float>(velocity) / 127.0f);
    bool anyStarted = false;
    // Build list of unique variant keys present in this layer (preserve order of first appearance)
    std::vector<uint8_t> uniqueVariants;
    uniqueVariants.reserve(layer.samples.size());
    for (const auto& se : layer.samples) {
        uint8_t key = se.variantIdx;
        bool found = false;
        for (auto k : uniqueVariants) { if (k == key) { found = true; break; } }
        if (!found) uniqueVariants.push_back(key);
    }

    // If there are multiple variant groups, pick one using round-robin counter; otherwise use the single group
    uint8_t chosenKey = 0;
    if (!uniqueVariants.empty()) {
        if (uniqueVariants.size() == 1) {
            chosenKey = uniqueVariants[0];
        } else {
            uint32_t rr = layer.roundRobinCounter->fetch_add(1u);
            size_t idx = static_cast<size_t>(rr % static_cast<uint32_t>(uniqueVariants.size()));
            chosenKey = uniqueVariants[idx];
        }
    }

    // Start voices only for samples that belong to the chosen variant group
    for (const auto& se : layer.samples) {
        if (se.sound == nullptr) continue;
        if (se.variantIdx != chosenKey) continue;
        juce::SynthesiserVoice* voice = findFreeVoice(se.sound, midiChannel, midiNoteNumber, stealNotes);
        if (voice == nullptr) continue;
        LOG(Debug, "Starting voice for note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)) + " sample " + (se.sound->sampleVariantSpec ? se.sound->sampleVariantSpec->sampleFileName : "null") + " micBufChStart=" + juce::String(se.sound->micBufferChannelStart) + " micBufChCount=" + juce::String(se.sound->micBufferChannelCount));
        this->startVoice(voice, se.sound, midiChannel, midiNoteNumber, velf);
        anyStarted = true;
    }
    if (!anyStarted) {
        LOG(Warning, "InstrumentSynth::playNote - no free voice available for any sample in layer");
        return false;
    }
    return true;
}

void InstrumentSynth::noteOn (const int midiChannel, const int midiNoteNumber, const float velocity) {
    const juce::ScopedLock sl (this->lock);
    bool reallyShouldStealNotes = true;
    for (auto* sound : this->sounds) {
        if (auto* sampledSound = dynamic_cast<SampledSound*>(sound)) {
            if (sampledSound->appliesToNote(midiNoteNumber) && sampledSound->appliesToChannel(midiChannel)) {
                
                LOG(Debug, "NoteOn " + juce::String(midiNoteNumber) + " vel " + juce::String(velocity) + " " + sampledSound->sampleVariantSpec->sampleFileName);
                this->startVoice(this->findFreeVoice (sampledSound, midiChannel, midiNoteNumber, reallyShouldStealNotes), sampledSound, midiChannel, midiNoteNumber, velocity);
            }
        }
    }
}

SampleLoader* InstrumentSynth::getSampleLoader() {
    return (this->parent) ? this->parent->getSampleLoader() : nullptr;
}

void InstrumentSynth::prepareToPlay(double newSampleRate, int samplesPerBlock) {
    // Ensure mic buffer has correct size
    if (mMicChannelCount <= 0)
        mMicChannelCount = 2;
    micBuffer.setSize(mMicChannelCount, samplesPerBlock);

    // Configure voices for this synth
    this->clearVoices();
    const int numVoices = 16; //FIXME: make configurable, maybe per articulation/layer based on number of variants, also consider dynamic voice allocation based on load
    for (int i = 0; i < numVoices; ++i)
        this->addVoice(new InstrumentSynthVoice());

    // set playback sample rate for voices
    this->setCurrentPlaybackSampleRate(newSampleRate);
}

SampledSound* InstrumentSynth::selectSampleFor(int articulationIdx, int midiVel, int micIdx) {
    if (articulationIdx < 0 || articulationIdx >= static_cast<int>(this->runtimeArticulations.size()))
        return nullptr;

    auto& art = this->runtimeArticulations[articulationIdx];
    if (art.layers.empty()) return nullptr;

    // LOG(Debug, "selectSampleFor " + art.codeRaw + " artIdx=" + juce::String(articulationIdx) + " vel=" + juce::String(midiVel) + " micIdx=" + juce::String(micIdx));

    // clamp midiVel
    if (midiVel < 0) midiVel = 0;
    if (midiVel > 127) midiVel = 127;

    int layerIdx = art.velocityToLayer[midiVel];
    
    if (layerIdx < 0 || layerIdx >= static_cast<int>(art.layers.size())) return nullptr;
    // If there are enough variants, do round-robin here
    auto& layer = art.layers[layerIdx];
    int sampleCount = static_cast<int>(layer.samples.size());
    if (sampleCount >= NAME_IT_BETTER_MIN_VARIANTS) {
        uint32_t rr = layer.roundRobinCounter->fetch_add(1u);
        int idx = static_cast<int>(rr % static_cast<uint32_t>(sampleCount));
        return layer.samples[idx].sound;
    }

    // Otherwise try to find nearby layers within reasonable velocity distance that have samples
    int bestLayer = -1;
    int bestDist = 10000;
    for (int i = 0; i < static_cast<int>(art.layers.size()); ++i) {
        if (static_cast<int>(art.layers[i].samples.size()) == 0) continue;
        int dist = std::abs(static_cast<int>(art.layers[i].velocity) - midiVel);
        if (dist <= NAME_IT_BETTER_MAX_VELOCITY_DISTANCE_TO_USE_SAMPLES_FROM_NEARBY_VELOCITY_LAYERS && dist < bestDist) {
            bestDist = dist;
            bestLayer = i;
        }
    }

    if (bestLayer >= 0) {
        auto& best = art.layers[bestLayer];
        int sc = static_cast<int>(best.samples.size());
        if (sc > 0) {
            uint32_t rr = best.roundRobinCounter->fetch_add(1u);
            int idx = static_cast<int>(rr % static_cast<uint32_t>(sc));
            return best.samples[idx].sound;
        }
    }

    // fallback: try primary layer even if empty -> try any sample in any layer
    for (auto& l : art.layers) {
        if (!l.samples.empty()) {
            uint32_t rr = l.roundRobinCounter->fetch_add(1u);
            int idx = static_cast<int>(rr % static_cast<uint32_t>(l.samples.size()));
            return l.samples[idx].sound;
        }
    }

    return nullptr;
}

InstrumentSynthVoice::InstrumentSynthVoice()
    : mGainParameter(nullptr), mPanParameter(nullptr), mInvertPhaseParameter(nullptr) {
    mPitchRatio = 1.0;
    mVelocityGain = 1.0f;
    mSourceSamplePosition = 0.0;
}

void InstrumentSynthVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* s, int /*pitchWheel*/) {
    juce::ignoreUnused(midiNoteNumber);
    mVelocityGain = velocity;
    mSourceSamplePosition = 0.0;
    mAdsr.setParameters({0.005f, 0.05f, 0.8f, 0.08f});
    mAdsr.setSampleRate(getSampleRate());
    mAdsr.noteOn();

    if (auto* sampled = dynamic_cast<SampledSound*>(s)) {
        if (sampled->sampleRate > 0 && getSampleRate() > 0)
            mPitchRatio = sampled->sampleRate / getSampleRate();
        else
            mPitchRatio = 1.0;
    } else {
        mPitchRatio = 1.0;
    }
}

void InstrumentSynthVoice::stopNote(float velocity, bool allowTailOff) {
    juce::ignoreUnused(velocity);
    if (allowTailOff) {
        mAdsr.noteOff();
    } else {
        mAdsr.reset();
        clearCurrentNote();
    }
}

void InstrumentSynthVoice::pitchWheelMoved(int /*newValue*/) {}
void InstrumentSynthVoice::controllerMoved(int /*controllerNumber*/, int /*newValue*/) {}

void InstrumentSynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) {
    if (!isVoiceActive())
        return;

    auto snd = getCurrentlyPlayingSound();
    auto* s = dynamic_cast<SampledSound*>(snd.get());
    if (s == nullptr || !s->buffer)
        return;

    auto& buf = *s->buffer;
    const int srcChannels = buf.getNumChannels();
    const int srcLen = buf.getNumSamples();
    // Determine source/destination channel mapping based on SampleVariantSpec metadata.
    int srcStart = 0;
    int srcCount = srcChannels;
    if (s->sampleVariantSpec) {
        srcStart = s->sampleVariantSpec->fileChannelStart;
        srcCount = s->sampleVariantSpec->fileChannelCount > 0 ? s->sampleVariantSpec->fileChannelCount : srcChannels;
    }

    const int destChCount = outputBuffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i) {
        const double pos = mSourceSamplePosition;
        const int idx = static_cast<int>(pos);
        const double frac = pos - idx;

        // For each source channel in this sample variant, fetch interpolated sample
        // and write to corresponding destination channel (offset by srcStart).
        if (idx >= 0 && idx < srcLen) {
            const float env = mAdsr.getNextSample();

            for (int sc = 0; sc < srcCount; ++sc) {
                int actualSrcCh = juce::jlimit(0, srcChannels - 1, srcStart + sc);
                float v0 = buf.getSample(actualSrcCh, idx);
                float v1 = (idx + 1 < srcLen) ? buf.getSample(actualSrcCh, idx + 1) : 0.0f;
                float sample = static_cast<float>(v0 + (v1 - v0) * frac);
                sample *= env * mVelocityGain;

                int destCh = s->micBufferChannelStart + sc;
                if (destCh >= 0 && destCh < destChCount) {
                    outputBuffer.addSample(destCh, startSample + i, sample);
                } else {
                    // Attempt to write outside of provided mic buffer — warn once per occurrence
                    // if (s->sampleVariantSpec) {
                    //     LOG(Warning, "Render: attempted write to micBuffer destCh=" + juce::String(destCh) + " but mic buffer has " + juce::String(destChCount) + " channels. Sample=" + s->sampleVariantSpec->sampleFileName);
                    // } else {
                    //     LOG(Warning, "Render: attempted write to micBuffer destCh=" + juce::String(destCh) + " but mic buffer has " + juce::String(destChCount) + " channels.");
                    // }
                }
            }
        } else {
            // still advance envelope even if out of source range
            (void)mAdsr.getNextSample();
        }

        mSourceSamplePosition += mPitchRatio;
        if (mSourceSamplePosition >= srcLen) {
            // reached end
            mAdsr.reset();
            clearCurrentNote();
            break;
        }
    }
}
