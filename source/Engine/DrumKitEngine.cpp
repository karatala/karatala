#include "DrumKitEngine.h"
#include "SampleSelector.h"
#include "../Config/Helpers.h"
#include "../Config/YamlHelper.h"
#include <juce_core/juce_core.h>
#include <cmath>

DrumKitEngine::DrumKitEngine() {
    mKitLayoutManager = std::make_unique<KitLayoutManager>();
    mMicLayoutManager = std::make_unique<MicLayoutManager>();
}

DrumKitEngine::~DrumKitEngine() {
}

const juce::File DrumKitEngine::defaultDataPath {
    juce::File::getSpecialLocation(juce::File::SpecialLocationType::commonDocumentsDirectory)
        .getChildFile(PRODUCT_NAME_WITHOUT_VERSION)
};

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
    }
}

void DrumKitEngine::initDefaultLayouts() {
    mKitLayoutManager->initDefault();
    mMicLayoutManager->initDefault();
}

void DrumKitEngine::initKitLayouts() {
    const File layoutsDir = this->dataPath.getChildFile("kit_layouts");
    auto result = mKitLayoutManager->scanLayouts(layoutsDir);
    if (result.ok()) {
        DBG("INFO: found kit layouts: " + juce::String(result.value));
    } else {
        DBG("WARN: init kit layouts: " + result.error());
    }
}

void DrumKitEngine::initMicLayouts() {
    const File layoutsDir = this->dataPath.getChildFile("mic_layouts");
    auto result = mMicLayoutManager->scanLayouts(layoutsDir);
    if (result.ok()) {
        DBG("INFO: found mic layouts: " + juce::String(result.value));
    } else {
        DBG("WARN: init mic layouts: " + result.error());
    }
}

void DrumKitEngine::initSampleLibrary() {
    if (!mConfig.libraryPath.isDirectory()) {
        DBG("ERROR: Sample library not found under " + mConfig.libraryPath.getFullPathName());
        return;
    }

    mInstrumentManager = std::make_unique<InstrumentManager>(mConfig.libraryPath);
    mInstrumentManager->scanAll();

    mSampleLoader = std::make_unique<SampleLoader>();
    mSampleLoader->setCacheLimitBytes(mConfig.sampleLoaderCacheLimitMegabytes * 1024ull * 1024ull);
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

bool DrumKitEngine::useInstrumentMap(juce::String text) {
    if (text.trim().isEmpty()) {
        DBG("WARN: empty instrument map text");
        return false;
    }

    auto pairs = DrumKitEngine::parseInstrumentMapText(text);
    if (pairs.empty()) {
        DBG("WARN: no valid mapping lines found");
        return false;
    }

    if (this->mInstrumentManager == nullptr) {
        DBG("ERROR: InstrumentManager is not initialized");
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
        DrumKit* kit = nullptr;
        for (auto& kptr : this->mInstrumentManager->getAllKits()) {
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
            // map whole kit to layout (best-effort)
            this->mapKit(*kit);
            applied++;
            continue;
        }

        DrumKitInstrument* inst = kit->findInstrument(instCode);
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
    DrumKit* kit = this->getInstrumentManager()->getKit(0); //MAYBE: later use global setting for default kit instead
    if (kit != nullptr)
        this->mapKit(*kit);
}

void DrumKitEngine::mapKit(DrumKit& kit) {
    if (this->mKitLayout == nullptr) {
        DBG("WARN: no active kit layout to map to");
        return;
    }

    // simple matching pass: by layout id -> kit instrument code, then fuzzy by name/code containment
    std::unordered_set<DrumKitInstrument*> usedInstruments;

    std::map<DrumKitInstrument::Type, int> nextIndexByType; // for sequential assignment when layout uses numbering (kick1, kick2)
    
    //FIXME: rewrite loop thru mInstrumentSynthById
    for (const auto& layoutInstrument : this->mKitLayout->instruments) {
        DrumKitInstrument* matched = nullptr;

        auto sit = mInstrumentSynthById.find(layoutInstrument.id);
        if (sit == mInstrumentSynthById.end()) {
            DBG("WARN: layout instrument slot not present: " + layoutInstrument.id);
            continue;
        }
        InstrumentSynth* slot = sit->second;

        // resolve layout type to DrumKitInstrument::Type
        DrumKitInstrument::Type desiredType = layoutInstrument.type;
        
        // If we have a valid type, try type-based selection
        if (desiredType != DrumKitInstrument::Type::Unknown) {
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
            DBG("WARN: no matching instrument found in kit '" + kit.meta.name + "' for layout slot: " + layoutInstrument.id);
            slot->unloadInstrument();
            continue;
        }
        
        DBG("INFO: loading instrument '" + matched->meta.code + "' from kit '" + kit.meta.name + "' for layout slot: " + layoutInstrument.id);
        // load instrument into slot
        slot->loadInstrument(matched);
        usedInstruments.insert(matched);

        // map MIDI notes for articulations?? WHY? already done
        // for (const auto& art : layoutInstrument.articulations) {
        //     for (int note : art.notes) {
        //         auto& vec = mInstrumentSynthsByNote[note];
        //         if (std::find(vec.begin(), vec.end(), slot) == vec.end())
        //             vec.push_back(slot);
        //     }
        // }
    }
}


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
    //LATER: write yaml
    jassertfalse;
}

bool DrumKitEngine::applyKitLayout() {
    if (this->mKitLayout == nullptr) {
        DBG("WARN: kit layout was not set");
        return false;
    }
    // Clear previous slots and indices
    mInstrumentSynthPtrs.clear();
    mInstrumentSynthById.clear();
    mInstrumentSynthsByNote.clear();

    for (const auto& instrumentDef : this->getKitLayout()->instruments) {
        auto slot = std::make_unique<InstrumentSynth>(this, instrumentDef);
        auto ptr = slot.get();

        // insert into ownership vector and lookup map
        mInstrumentSynthById[slot->getId()] = ptr;
        mInstrumentSynthPtrs.push_back(std::move(slot));

        // build midi note mapping from articulations
        for (const auto& art : instrumentDef.articulations) {
            for (const int note : art.notes) {
                mInstrumentSynthsByNote[note].push_back(ptr);
            }
        }
    }

    DBG("INFO: Applied kit layout: " + this->mKitLayout->id);
    return true;
}

bool DrumKitEngine::applyMicLayout() {
    if (this->mMicLayout == nullptr) {
        DBG("WARN: mic layout was not set");
        return false;
    }

    if (mInstrumentSynthPtrs.size() == 0) {
        DBG("WARN: unable to apply mic layout: no instrument slots was defined in kit layout");
        return false;
    }

    // For each instrument slot, calculate how many mic channels it should have
    for (size_t idx = 0; idx < mInstrumentSynthPtrs.size(); ++idx) {
        InstrumentSynth* slotPtr = mInstrumentSynthPtrs[idx].get();
        int totalChannels = 0;
        const auto slotId = slotPtr->getId();
        for (const MicLayout::Mic& mic : this->mMicLayout->mics) {
            // match mic entries that list this slot id in their instrument field
            if (mic.instrument == slotId) {
                // parse mic code; try to infer channel count
                auto prRes = MicDescriptor::tryCreateFromCode(mic.code);
                if (prRes.ok()) {
                    totalChannels += prRes.value.getChannelCount();
                } else {
                    totalChannels += 1; // fallback mono
                }
            }
        }

        if (totalChannels <= 0)
            totalChannels = 2; // default to stereo

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

void DrumKitEngine::processBlock(juce::AudioBuffer<float>& outputBuffer, juce::MidiBuffer& midiMessages) {
    const auto outputBufferStartSample = 0;
    const auto outputBufferNumSamples = outputBuffer.getNumSamples();
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


void InstrumentSynth::unloadInstrument() {
    this->clearSounds();
    mArticulations.clear();
    mDrumKitInstrumentPtr = nullptr;
}

void InstrumentSynth::loadInstrument(DrumKitInstrument* instrument) {
    if (instrument == nullptr) return;
    
    this->unloadInstrument();
    this->mDrumKitInstrumentPtr = instrument;
    this->type = instrument->meta.type;

    auto loader = this->getSampleLoader();

    for (const auto& artPtr : instrument->articulations) {
        for (const auto& velPtr : artPtr->velocityLayers) {
            for (const auto& variantPtr : velPtr->sampleVariants) {
                auto* sound = new SampledSound(instrument, variantPtr->sampleFile, artPtr->name, velPtr->velocityLayer, variantPtr->variantLetter);
                
                this->addSound(sound); // add to synth immediately; buffer will be filled asynchronously
                
                
                
     
                
                // this->mArticulations ?

                if (loader != nullptr) {
                    loader->requestAsyncLoad(variantPtr->sampleFile, [sound](const juce::File& /*file*/, std::shared_ptr<juce::AudioBuffer<float>> buf, double sr) {
                        sound->buffer = buf;
                        sound->sampleRate = sr;
                    });
                }
            }
        }
    }
}

void InstrumentSynth::handleMidiEvent(const MidiMessage& midiMessage) {
    const int channel = midiMessage.getChannel();

    //TODO: common realistic choke (volume change) on aftertouch: with some custom decay timeout, if aftertouch value became less in that timeout, volume should return back by some percent
    
    //TODO: from KitLayout::Instrument instrumentDef get CC and notes to subscribe to
    /** 
     * 
    */
    
    
    if (midiMessage.isNoteOn()) {
        if (! this->playNote(channel, midiMessage.getNoteNumber(), midiMessage.getVelocity(), 1))
            this->noteOn(channel, midiMessage.getNoteNumber(), midiMessage.getFloatVelocity());
    }
    else if (midiMessage.isAllNotesOff() || midiMessage.isAllSoundOff()) this->allNotesOff(channel, true);
    else if (midiMessage.isAftertouch()) this->handleAftertouch(channel, midiMessage.getNoteNumber(), midiMessage.getAfterTouchValue());
    else if (midiMessage.isController()) this->handleController(channel, midiMessage.getControllerNumber(), midiMessage.getControllerValue());

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
    juce::ignoreUnused(midiChannel, midiNoteNumber, velocity, expressions);
    
    // SampledSound* chosen = this->selectSampleFor(targetArt, midiVel);
    // if (chosen == nullptr) {
    //     DBG("Nothing to play for " + targetArt);
    //     return;
    // }
    
    // const ScopedLock sl (lock);

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
    
    return false;
}

SampleLoader* InstrumentSynth::getSampleLoader() {
    return (this->parent) ? this->parent->getSampleLoader() : nullptr;
}

void InstrumentSynth::prepareToPlay(double newSampleRate, int samplesPerBlock) {
    // Ensure mic buffer has correct size
    if (mMicChannelCount <= 0)
        mMicChannelCount = 2;
    mMicBuffer.setSize(mMicChannelCount, samplesPerBlock);

    // Configure voices for this synth
    this->clearVoices();
    const int numVoices = 6; // allow polyphony for round-robins
    for (int i = 0; i < numVoices; ++i)
        this->addVoice(new InstrumentSynthVoice());

    // set playback sample rate for voices
    this->setCurrentPlaybackSampleRate(newSampleRate);
}

SampledSound* InstrumentSynth::selectSampleFor(const juce::String& articulation, int midiVel) {
    std::array<uint8_t, 1> queryDims{ static_cast<uint8_t>(midiVel) };
    
    auto it = mArticulations.find(articulation);
    if (it == mArticulations.end()) return nullptr;
    auto& inst = it->second;

    const void* zonePtr = nullptr;
    std::visit([&](auto&& sel) {
        using T = std::decay_t<decltype(sel)>;
        if constexpr (!std::is_same_v<T, std::monostate>) {
            if constexpr (T::DIMENSION_COUNT == std::tuple_size_v<std::remove_reference_t<decltype(queryDims)>>) {
                const auto* node = sel.findZone(queryDims);
                zonePtr = static_cast<const void*>(node);
            }
        }
    }, inst.selector);

    if (zonePtr == nullptr) return nullptr;

    auto zit = inst.zoneToIndex.find(zonePtr);
    if (zit == inst.zoneToIndex.end()) return nullptr;

    size_t idx = zit->second;
    auto& zoneSamples = inst.samplesByZone[idx];
    if (zoneSamples.empty()) return nullptr;

    std::atomic_ref<uint32_t> rrRef(inst.roundRobinCounters[idx]);
    uint32_t rr = rrRef.fetch_add(1, std::memory_order_relaxed);
    return zoneSamples[ rr % zoneSamples.size() ];
}

InstrumentSynthVoice::InstrumentSynthVoice()
    : mGainParameter(nullptr), mPanParameter(nullptr), mInvertPhaseParameter(nullptr) {
    mPitchRatio = 1.0;
    mVelocityGain = 1.0f;
    mSourceSamplePosition = 0.0;
}

InstrumentSynthVoice::~InstrumentSynthVoice() {}

bool InstrumentSynthVoice::canPlaySound(juce::SynthesiserSound* sound) {
    return dynamic_cast<SampledSound*>(sound) != nullptr;
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
