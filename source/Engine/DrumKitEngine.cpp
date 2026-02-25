#include "DrumKitEngine.h"
#include "InstrumentSynth.h"
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
    this->initArticulationFallbackMap();
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
        LOG(Notice, "" + envInfo.error());
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

void DrumKitEngine::initArticulationFallbackMap() {
    this->articulationFallbackIdMap.clear();

    auto& dict = StringsDictSingleton::instance();
    //TODO: move fallback map to global config and fill dynamically from it
    this->articulationFallbackIdMap[dict.getId("bell")] = { dict.getId("bow"), dict.getId("edge") };
    this->articulationFallbackIdMap[dict.getId("bow")] = { dict.getId("edge"), dict.getId("bell") };
    this->articulationFallbackIdMap[dict.getId("edge")] = { dict.getId("bow"), dict.getId("bell") };
    this->articulationFallbackIdMap[dict.getId("rim")] = { dict.getId("center") };
    this->articulationFallbackIdMap[dict.getId("center")] = { dict.getId("rim") };
    this->articulationFallbackIdMap[dict.getId("sidestick")] = { dict.getId("rim"), dict.getId("center") };
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
            if (kptr->meta.name == kitName || kptr->meta.codeRaw == kitName) {
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
                LOG(Notice, "Using default kit layout");
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
                LOG(Notice, "Using default mic layout");
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
    this->latestCCValueMap.clear();
    this->latestPAValueMap.clear();

    for (const auto& instrumentSlot : this->getKitLayout()->instrumentSlots) {
        auto slot = std::make_unique<InstrumentSynth>(this, instrumentSlot);
        auto ptr = slot.get();

        this->instrumentSynthById[slot->getId()] = ptr;
        this->instrumentSynthPtrs.push_back(std::move(slot));

        for (const auto& cc : instrumentSlot.controllers) {
            this->latestCCValueMap[cc.number] = 0;
        }
        for (const auto& art : instrumentSlot.articulations) {
            for (const int note : art.notes) {
                this->latestPAValueMap[note] = 0;
                this->instrumentSynthsByNote[note].push_back(ptr); //TODO: check ptr safety
            }
        }
    }

    LOG(Info, "Applied kit layout: " + this->activeKitLayoutPtr->id);

    this->buildNoteNameCache();
    return true;
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
        LOG(Debug, "Mic mapping " + mapStr + " (" + juce::String(instPtr->getMicChannelCount()) + " ch) used for instrument '" + instPtr->getId() + "'");
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
    this->saveLatestPolyphonicAftertouchValues(midiMessages);

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
                // FIXME: remove logs from loops and audio thread
                if (mappingUsed && !juce::JUCEApplicationBase::isStandaloneApp())
                    LOG(Warning, "Instrument '" + instrumentSynthPtr->getId() + "' mapping for mic channel " + juce::String(c) + " -> host index " + juce::String(dest) + " is out of range (host channels=" + juce::String(outCh) + "). Falling back to wrap index.");

                // Fallback: wrap to available outputs
                dest = c % outCh;
            }
            outputBuffer.addFrom(dest, outputBufferStartSample, micBuffer, c, outputBufferStartSample, outputBufferNumSamples, 1.0f);
        }
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
        if (this->latestCCValueMap.find(ccNum) == this->latestCCValueMap.end()) continue;
        this->latestCCValueMap[ccNum] = msg.getControllerValue();
    }
}
void DrumKitEngine::saveLatestPolyphonicAftertouchValues(const juce::MidiBuffer& midiMessages) {
    for (const auto& midiMess : midiMessages) {
        const juce::MidiMessage msg = midiMess.getMessage();
        if (!msg.isAftertouch()) continue;
        const auto note = msg.getNoteNumber();
        if (this->latestPAValueMap.find(note) == this->latestPAValueMap.end()) continue;
        this->latestPAValueMap[note] = msg.getAfterTouchValue();
    }
}
