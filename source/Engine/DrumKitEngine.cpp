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
        DBG("WARN: " + envInfo.error());
        this->dataPath = defaultDataPath;
    }

    if (!this->dataPath.exists()) {
        const auto result = this->dataPath.createDirectory();
        if (result.failed()) {
            DBG("WARN: Could not create data directory: " + result.getErrorMessage());
            jassertfalse;
        }
    }

    File configFile = this->dataPath.getChildFile("config.yaml");
    if (!configFile.existsAsFile()) {
        auto result = configFile.create();
        if (result.failed()) {
            DBG("WARN: Could not create config file: " + result.getErrorMessage());
            jassertfalse;
        } else {
            if (!configFile.replaceWithText(this->getDefaultYaml())) {
                DBG("WARN: Could not write default config.yaml");
                jassertfalse;
            }
        }
    }

    auto yamlStr = configFile.loadFileAsString();
    auto conf = this->parseConfigYaml(yamlStr);
    if (conf.ok()) {
        this->mConfig = conf.value;
    } else {
        DBG("ERROR: engine config parse error: " + conf.error());
        jassertfalse;
    }
}

void DrumKitEngine::initDefaultLayouts() {
    this->mKitLayoutManager->initDefault();
    this->mMicLayoutManager->initDefault();
}

void DrumKitEngine::initKitLayouts() {
    const File layoutsDir = this->dataPath.getChildFile("kit_layouts");
    auto result = this->mKitLayoutManager->scanLayouts(layoutsDir);
    if (result.ok()) {
        DBG("INFO: found kit layouts: " + juce::String(result.value));
    } else {
        DBG("WARN: init kit layouts: " + result.error());
    }
}

void DrumKitEngine::initMicLayouts() {
    const File layoutsDir = this->dataPath.getChildFile("mic_layouts");
    auto result = this->mMicLayoutManager->scanLayouts(layoutsDir);
    if (result.ok()) {
        DBG("INFO: found mic layouts: " + juce::String(result.value));
    } else {
        DBG("WARN: init mic layouts: " + result.error());
    }
}

void DrumKitEngine::initSampleLibrary() {
    if (!mConfig.libraryPath.isDirectory()) {
        DBG("ERROR: Sample library not found under " + mConfig.libraryPath.getFullPathName());
        jassertfalse;
        return;
    }

    this->mLibraryManager = std::make_unique<KitLib::LibraryManager>(mConfig.libraryPath);
    this->mLibraryManager->scanAll();

    this->mSampleLoader = std::make_unique<SampleLoader>();
    this->mSampleLoader->setCacheLimitBytes(mConfig.sampleLoaderCacheLimitMegabytes * 1024ull * 1024ull);
}

bool DrumKitEngine::useKitLayout(juce::String name) {
    const KitLayout* ptr = this->getKitLayoutManager()->getLayoutById(name);
    if (ptr != nullptr)
        this->setKitLayout(*ptr);
    if (name != "default") DBG("WARN: KitLayout not found: " + name);
    return false;
}

bool DrumKitEngine::useMicLayout(juce::String name) {
    const MicLayout* ptr = this->getMicLayoutManager()->getLayoutById(name);
    if (ptr != nullptr)
        this->setMicLayout(*ptr);
    if (name != "default") DBG("WARN: MicLayout not found: " + name);
    return false;
}

bool DrumKitEngine::useInstrumentMap(juce::String text) { // not working draft
    if (text.trim().isEmpty()) {
        DBG("WARN: empty instrument map text");
        return false;
    }

    auto pairs = DrumKitEngine::parseInstrumentMapText(text); //TODO: plan implementation
    if (pairs.empty()) {
        DBG("WARN: no valid mapping lines found");
        return false;
    }

    if (this->mLibraryManager == nullptr) {
        DBG("ERROR: LibraryManager is not initialized");
        return false;
    }

    int applied = 0;
    for (const auto& p : pairs) {
        const auto slotId = p.first.trim();
        const auto rhs = p.second.trim();

        // find slot
        auto sit = mInstrumentSynthById.find(slotId);
        if (sit == mInstrumentSynthById.end()) {
            DBG("WARN: instrument slot not found: " + slotId);
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
        KitLib::DrumKitSpecs* kit = nullptr;
        for (auto& kptr : this->mLibraryManager->getKits()) {
            if (kptr->meta.name == kitName || kptr->meta.code == kitName) {
                kit = kptr.get();
                break;
            }
        }

        if (kit == nullptr) {
            DBG("WARN: kit not found: " + kitName);
            continue;
        }

        if (instCode.isEmpty()) {
            this->mapKit(*kit);
            applied++;
            continue;
        }

        KitLib::InstrumentSpecs* inst = kit->findInstrument(instCode);
        if (inst == nullptr) {
            DBG("WARN: instrument not found in kit '" + kit->meta.name + "': " + instCode);
            continue;
        }

        slot->loadInstrument(inst);
        applied++;

        // map midi notes for this slot based on kit layout definition (if present)
        if (this->mKitLayout != nullptr) {
            auto it = this->mKitLayout->instrumentsById.find(slotId);
            if (it != this->mKitLayout->instrumentsById.end()) {
                const auto* layoutInst = it->second;
                for (const auto& art : layoutInst->articulations) {
                    for (int note : art.notes) {
                        auto& vec = mInstrumentSynthsByNote[note];
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
    mKitLayout = std::make_unique<KitLayout>(layout);
    return this->applyKitLayout();
}

bool DrumKitEngine::setMicLayout(MicLayout layout) {
    mMicLayout = std::make_unique<MicLayout>(layout);
    return this->applyMicLayout();
}

void DrumKitEngine::useState(const juce::AudioProcessorValueTreeState& treeState) {
    auto kitLayout = treeState.state["kit_layout"].toString();
    if (kitLayout.isEmpty() || !this->useKitLayout(kitLayout)) {
        if (this->mKitLayout == nullptr) {
            kitLayout = this->mConfig.kitLayout;
            if (kitLayout.isEmpty() || !this->useKitLayout(kitLayout)) {
                DBG("WARN: Using default kit layout");
                auto src = this->getKitLayoutManager()->getDefault();
                this->setKitLayout(src);
            }
        }
    }

    auto micLayout = treeState.state["mic_layout"].toString();
    if (micLayout.isEmpty() || !this->useMicLayout(micLayout)) {
        if (this->mMicLayout == nullptr) {
            micLayout = this->mConfig.micLayout;
            if (micLayout.isEmpty() || !this->useMicLayout(micLayout)) {
                DBG("WARN: Using default mic layout");
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
    KitLib::DrumKitSpecs* kit = this->getInstrumentManager()->getKit(0); //MAYBE: later use global setting for default kit instead
    if (kit != nullptr)
        this->mapKit(*kit);
}

void DrumKitEngine::mapKit(KitLib::DrumKitSpecs& kit) { //FIXME: review, probably rewrite, simplify
    if (this->mKitLayout == nullptr) {
        DBG("WARN: no active kit layout to map to");
        return;
    }

    // simple matching pass: by layout id -> kit instrument code, then fuzzy by name/code containment
    std::unordered_set<KitLib::InstrumentSpecs*> usedInstruments;

    std::map<KitLib::InstrumentSpecs::Type, int> nextIndexByType; // for sequential assignment when layout uses numbering (kick1, kick2)
    

    for (const auto& layoutInstrument : this->mKitLayout->instrumentSlots) { //FIXME: rewrite loop thru mInstrumentSynthById
        KitLib::InstrumentSpecs* matched = nullptr;

        auto sit = mInstrumentSynthById.find(layoutInstrument.id);
        if (sit == mInstrumentSynthById.end()) {
            DBG("WARN: layout instrument slot not present: " + layoutInstrument.id);
            continue;
        }
        InstrumentSynth* slot = sit->second;

        // resolve layout type to KitLib::InstrumentSpecs::Type
        KitLib::InstrumentSpecs::Type desiredType = layoutInstrument.type;
        
        // If we have a valid type, try type-based selection
        if (desiredType != KitLib::InstrumentSpecs::Type::Unknown) {
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
                if (usedInstruments.find(candidates[requestedIdx]) == usedInstruments.end()) {
                    matched = candidates[requestedIdx];
                }
            }
        
            // otherwise pick next unused of that type
            if (matched == nullptr) {
                int& next = nextIndexByType[layoutInstrument.type];
                for (int i = next; i < static_cast<int>(candidates.size()); ++i) {
                    if (usedInstruments.find(candidates[i]) == usedInstruments.end()) {
                        matched = candidates[i];
                        next = i + 1;
                        break;
                    }
                }
                // wrap-around search from beginning if none found after 'next'
                if (matched == nullptr) {
                    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
                        if (usedInstruments.find(candidates[i]) == usedInstruments.end()) {
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
            String needle = layoutInstrument.id.trim().toLowerCase();
            for (const auto& pair : kit.instruments) {
                auto* cand = pair.second.get();
                if (cand == nullptr)
                    continue;
                if (usedInstruments.find(cand) != usedInstruments.end())
                    continue;
                if (cand->meta.code.trim().toLowerCase().contains(needle) || cand->meta.name.trim().toLowerCase().contains(needle)) {
                    matched = cand;
                            break;
                        }
                    }
                }

        if (matched == nullptr) {
            DBG("WARN: no matching instrument found in kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");
            slot->unloadInstrument();
            continue;
        }
        
        DBG("INFO: loading instrument '" + matched->meta.code + "' from kit '" + kit.meta.name + "' for layout slot '" + layoutInstrument.id + "'");
        
        slot->loadInstrument(matched); //TODO: wrap in performance measuring block
        usedInstruments.insert(matched);

        //FIXME: map MIDI notes for articulations?? WHY? isnt it already done
        // for (const auto& art : layoutInstrument.articulations) {
        //     for (int note : art.notes) {
        //         auto& vec = mInstrumentSynthsByNote[note];
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
            DBG("WARN: invalid mapping line (no colon): " + line);
            continue;
        }
        auto left = line.substring(0, colon).trim();
        auto right = line.substring(colon + 1).trim();
        if (left.isEmpty() || right.isEmpty()) {
            DBG("WARN: invalid mapping line (empty slot or target): " + line);
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
    const File libDir = this->dataPath.getChildFile("sample_library");
    if (libDir.exists())
        result += "libraryPath: " + libDir.getFullPathName() + "\n";
    return result;
}

void DrumKitEngine::saveSettings() {
    File configFile = dataPath.getChildFile("config.yaml");
    //TODO: write yaml
    jassertfalse;
}

bool DrumKitEngine::applyKitLayout() {
    if (this->mKitLayout == nullptr) {
        DBG("WARN: kit layout was not set");
        return false;
    }
    this->mInstrumentSynthPtrs.clear();
    this->mInstrumentSynthById.clear();
    this->mInstrumentSynthsByNote.clear();
    this->mContinuousControllerValues.clear();

    for (const auto& instrumentSlot : this->getKitLayout()->instrumentSlots) {
        auto slot = std::make_unique<InstrumentSynth>(this, instrumentSlot);
        auto ptr = slot.get();

        this->mInstrumentSynthById[slot->getId()] = ptr;
        this->mInstrumentSynthPtrs.push_back(std::move(slot));

        for (const auto& cc : instrumentSlot.controllers) {
            this->mContinuousControllerValues[cc.number] = 0;
        }
        for (const auto& art : instrumentSlot.articulations) {
            for (const int note : art.notes) {
                this->mInstrumentSynthsByNote[note].push_back(ptr);
            }
        }
    }

    DBG("INFO: Applied kit layout: " + this->mKitLayout->id);
    return true;
}

bool DrumKitEngine::applyMicLayout() { //TODO: review
    if (this->mMicLayout == nullptr) {
        DBG("WARN: mic layout was not set");
        return false;
    }

    if (this->mInstrumentSynthPtrs.size() == 0) {
        DBG("WARN: unable to apply mic layout: no instrument slots was defined in kit layout");
        return false;
    }

    // For each instrument slot, calculate how many mic channels it should have
    for (size_t idx = 0; idx < this->mInstrumentSynthPtrs.size(); ++idx) {
        InstrumentSynth* slotPtr = this->mInstrumentSynthPtrs[idx].get();
        int totalChannels = 0;
        const auto slotId = slotPtr->getId();
        for (const MicLayout::Mic& mic : this->mMicLayout->mics) {
            // match mic entries that list this slot id in their instrument field
            if (mic.instrument == slotId) {
                // parse mic code; try to infer channel count
                auto prRes = KitLib::MicSpecs::parseMicCode(mic.code);
                if (prRes.ok()) {
                    totalChannels += prRes.value.channels;
                } else {
                    totalChannels += 1; //FIXME: fallback mono? review, maybe take something from file
                }
            }
        }

        if (totalChannels <= 0)
            totalChannels = 2; //FIXME: default to stereo? review, maybe take something from file

        slotPtr->setMicChannelCount(totalChannels);
    }

    DBG("INFO: Applied mic layout: " + this->mMicLayout->id);
    return true;
}

void DrumKitEngine::prepareToPlay(double sampleRate, int samplesPerBlock) {
    for (const auto& instrumentSynthPtr : this->mInstrumentSynthPtrs) {
        instrumentSynthPtr->setCurrentPlaybackSampleRate(sampleRate);
        instrumentSynthPtr->prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void DrumKitEngine::processBlock(juce::AudioBuffer<float>& outputBuffer, juce::MidiBuffer& midiMessages) { //TODO: review
    const auto outputBufferStartSample = 0;
    const auto outputBufferNumSamples = outputBuffer.getNumSamples();
    
    this->saveLatestContinuousControllerValues(midiMessages);

    for (const auto& instrumentSynthPtr : this->mInstrumentSynthPtrs) {
        auto& micBuffer = instrumentSynthPtr->getMicBuffer();
        micBuffer.clear();

        // Render into own mic buffer
        instrumentSynthPtr->renderNextBlock(micBuffer, midiMessages, outputBufferStartSample, outputBufferNumSamples);

        // Simple temporary mixing: add each mic channel into output channels
        const int outCh = outputBuffer.getNumChannels();
        const int micCh = micBuffer.getNumChannels();
        for (int c = 0; c < micCh; ++c) {
            int dest = c % outCh;
            outputBuffer.addFrom(dest, outputBufferStartSample, micBuffer, c, outputBufferStartSample, outputBufferNumSamples, 1.0f);
        }
    }
}

void DrumKitEngine::saveLatestContinuousControllerValues(const juce::MidiBuffer& midiMessages) {
    for (const auto& midiMess : midiMessages) {
        const juce::MidiMessage msg = midiMess.getMessage();
        if (!msg.isController()) continue;
        const auto ccNum = msg.getControllerNumber();
        if (this->mContinuousControllerValues.find(ccNum) == mContinuousControllerValues.end()) continue;
        
        this->mContinuousControllerValues[ccNum] = msg.getControllerValue();
    }
}

void InstrumentSynth::unloadInstrument() {
    this->runtimeArticulations.clear();
    this->sampledSoundPtrs.clear();
    this->noteToArticulation.fill(-1);
    this->clearSounds();
    this->instrumentSpecstPtr = nullptr;
}

void InstrumentSynth::loadInstrument(KitLib::InstrumentSpecs* instrumentSpecs) {
    if (instrumentSpecs == nullptr) return;
    this->unloadInstrument();
    this->instrumentSpecstPtr = instrumentSpecs;
    this->type = instrumentSpecs->meta.type;

    auto loader = this->getSampleLoader();

    // For each layout articulation create runtime entry
    for (const KitLayout::InstrumentSlot::ArticulationTrigger& layoutArt : this->instrumentDef.articulations) {
        InstrumentSynth::ArticulationRuntime artRt;
        artRt.codeRaw = layoutArt.codeRaw;

        std::unordered_map<int, std::vector<std::pair<int8_t, int>>> foundCcDimensions;

        bool found = false;
        for (const auto& instArticulation : instrumentSpecs->articulations) {
            if (instArticulation->codeIdx != layoutArt.codeIdx) continue;

            found = true;

            // ensure velocity layers are sorted
            instArticulation->sortVelocityLayers();

            // Build velocity layers and create SampledSound objects
            for (const auto& velPtr : instArticulation->velocityLayers) {
                VelocityLayerRuntime layer;
                layer.velocity = velPtr->velocity;
                layer.samples.reserve(velPtr->sampleVariants.size()); //FIXME: sampleVariants here have all posible sample variants for all microphones, need to group them somewhere by letter, so same letter variants play same time from all coresponding mic channels
                for (const auto& variantPtr : velPtr->sampleVariants) {
                    auto* sound = new SampledSound(instrumentSpecs, variantPtr.get(), variantPtr->sampleFile, layoutArt.notes); //FIXME: need to thing what params to have in SampledSound constructor, maybe add some and maybe remove
                    this->addSound(sound);
                    this->sampledSoundPtrs.push_back(sound);
                    SampleEntry se;
                    se.sound = sound;
                    se.variantIdx = 0;
                    layer.samples.push_back(se);

                    if (loader != nullptr) {
                        loader->requestAsyncLoad(variantPtr->sampleFile, [sound](std::shared_ptr<juce::AudioBuffer<float>> buf, double sr) {
                            sound->buffer = buf;
                            sound->sampleRate = sr;
                        });
                    }
                }
                artRt.layers.push_back(std::move(layer));
            }

            // Map velocities to layers using upper-bound semantics:
            if (!artRt.layers.empty()) {
                // choose the first layer whose configured velocity is >= current velocity.
                // Example: layers {27,52,77,102,127} -> 0..27->0, 28..52->1, 53..77->2, 78..102->3, 103..127->4
                const int8_t count = static_cast<int8_t>(artRt.layers.size());
                for (int value = 0; value <= MIDI_MAX_VALUE; ++value) {
                    int chosen = count - 1; // default to last layer
                    for (int idx = 0; idx < count; ++idx) {
                        if (value <= static_cast<int>(artRt.layers[idx].velocity)) {
                            chosen = idx;
                            break;
                        }
                    }
                    artRt.velocityToLayer[value] = static_cast<int8_t>(chosen);
                }
            }
            

            // Map layout notes to this articulation index
            int artIdx = static_cast<int>(runtimeArticulations.size());
            for (uint8_t note : layoutArt.notes) {
                this->noteToArticulation[note] = static_cast<int8_t>(artIdx);
            }



            // Prepare note -> controller and controllerValue -> articulation mappings per subscribed CC
            for (const auto& cc : this->instrumentDef.controllers) {
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
                    for (const auto& dim : instArticulation->dimensions) {
                        if (dim->type == cc.codeIdx) {
                            for (const auto& val : dim->values) {
                                auto ccNum = static_cast<uint8_t>(cc.number);
                                const auto pair = std::make_pair(static_cast<int8_t>(val), artIdx);
                                auto& vec = foundCcDimensions[ccNum];
                                vec.emplace_back(pair);
                            }
                        }
                    }
                }
            }

            runtimeArticulations.push_back(std::move(artRt));
        }


        

        for (auto& pair : foundCcDimensions) {
            auto& ccNum = pair.first;
            auto& valArtPairVec = pair.second;
            
            std::sort(valArtPairVec.begin(), valArtPairVec.end(), [](auto& a, auto& b){
                return a.first < b.first;
            });

            std::array<uint8_t, 128> map;
            const int8_t count = static_cast<uint8_t>(valArtPairVec.size());
    
            for (int value = 0; value <= MIDI_MAX_VALUE; ++value) {
                uint8_t chosen = count - 1; // default to last
                for (uint8_t idx = 0; idx < count; ++idx) {
                    if (value <= static_cast<int>(valArtPairVec[idx].first)) {
                        chosen = valArtPairVec[idx].second;
                        break;
                    }
                }
                map[value] = chosen;
            }
            this->ccToArticulation[ccNum] = map;
        }
        
        if (!found) {
            DBG("WARN: no kit articulation found for layout art: " + layoutArt.codeRaw);
            // push empty runtime articulation to preserve indices
            // this->runtimeArticulations.push_back(std::move(artRt));
            // continue;
        }

    }
}

void InstrumentSynth::handleMidiEvent(const MidiMessage& midiMessage) {




    //TODO: from KitLayout::InstrumentSlot instrumentDef get CC and notes to subscribe to

    
    if (midiMessage.isNoteOn()) {
        if (noteToArticulation[midiMessage.getNoteNumber()] == -1) return;


        // if (! 
            this->playNote(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getVelocity(), 1);
        // )
            // this->noteOn(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getFloatVelocity());
    }
    
    //TODO: common realistic choke (volume change) on aftertouch: with some custom decay timeout, if aftertouch value became less in that timeout, volume should return back by some percent
    else if (midiMessage.isAftertouch())
        this->handleAftertouch(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getAfterTouchValue());
    
        
    else if (midiMessage.isAllNotesOff() || midiMessage.isAllSoundOff()) this->allNotesOff(midiMessage.getChannel(), true);
        
        
        
    // else if (midiMessage.isController()) this->handleController(channel, midiMessage.getControllerNumber(), midiMessage.getControllerValue());

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
    //     DBG("Nothing to play for " + targetArt);
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
        if (!this->instrumentDef.articulations.empty()) {
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
                        DBG("DEBUG: CC " + juce::String(ccNum) + " value " + juce::String(ccVal) + " maps to expression " + juce::String(artIdx) + " for note " + juce::String(midiNoteNumber) );
                    }
                }
            }
        }
    }

    if (artIdx < 0) {
        // DBG("WARN: InstrumentSynth::playNote - no articulation available for note " + juce::String(midiNoteNumber));
        return false;
    }

    // Select a sample using runtime tables
    SampledSound* chosen = this->selectSampleFor(artIdx, static_cast<int>(velocity));
    if (chosen == nullptr) {
        DBG("INFO: Nothing to play for note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)));
        return false;
    } else {
        //  DBG("INFO: play note " + juce::String(midiNoteNumber) + " vel " + juce::String(static_cast<int>(velocity)));
    }

    const ScopedLock sl (this->lock);

    const bool stealNotes = true;
    juce::SynthesiserVoice* voice = findFreeVoice(chosen, midiChannel, midiNoteNumber, stealNotes);
    if (voice == nullptr) {
        DBG("WARN: InstrumentSynth::playNote - no free voice available");
        return false;
    }
    float velf = juce::jlimit(0.0f, 1.0f, static_cast<float>(velocity) / 127.0f);
    this->startVoice(voice, chosen, midiChannel, midiNoteNumber, velf);
    return true;
}

void InstrumentSynth::noteOn (const int midiChannel, const int midiNoteNumber, const float velocity) {
    const ScopedLock sl (this->lock);
    bool reallyShouldStealNotes = true;
    for (auto* sound : this->sounds) {
        if (auto* sampledSound = dynamic_cast<SampledSound*>(sound)) {
            if (sampledSound->appliesToNote(midiNoteNumber) && sampledSound->appliesToChannel(midiChannel)) {
                
                DBG("DEBUG: NoteOn " + juce::String(midiNoteNumber) + " vel " + juce::String(velocity) + " " + sampledSound->sampleVariantSpec->sampleFileName);
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
    const int numVoices = 6; // allow polyphony for round-robins
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

    // DBG("DEBUG: selectSampleFor " + art.codeRaw + " artIdx=" + juce::String(articulationIdx) + " vel=" + juce::String(midiVel) + " micIdx=" + juce::String(micIdx));

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

    for (int i = 0; i < numSamples; ++i) {
        const double pos = mSourceSamplePosition;
        const int idx = static_cast<int>(pos);
        const double frac = pos - idx;

        float outSample = 0.0f;
        if (idx >= 0 && idx < srcLen) {
            float v0 = buf.getSample(0, idx);
            float v1 = (idx + 1 < srcLen) ? buf.getSample(0, idx + 1) : 0.0f;
            outSample = static_cast<float>(v0 + (v1 - v0) * frac);
            if (srcChannels > 1) {
                float v0r = buf.getSample(1, idx);
                float v1r = (idx + 1 < srcLen) ? buf.getSample(1, idx + 1) : 0.0f;
                float r = static_cast<float>(v0r + (v1r - v0r) * frac);
                outSample = (outSample + r) * 0.5f;
            }
        }

        const float env = mAdsr.getNextSample();
        outSample *= env * mVelocityGain;

        const int outCh = outputBuffer.getNumChannels();
        for (int ch = 0; ch < outCh; ++ch) {
            outputBuffer.addSample(ch, startSample + i, outSample);
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
