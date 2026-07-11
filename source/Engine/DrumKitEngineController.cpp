#include "DrumKitEngine.h"
#include "../Config/Helpers.h"

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
    auto envInfo = getEnvPath(DATA_PATH_ENV); //FIXME: reusable, see resolveMicLayoutFromConfig in DrumKitEngine
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
    const auto logFile = GlobalLogger::instance().createInstanceLogFile(this->dataPath);
    GlobalLogger::instance().setLogFile(logFile);

    auto yamlStr = configFile.loadFileAsString();
    auto conf = this->parseConfigYaml(yamlStr);
    if (conf.ok()) {
        this->globalConfig = conf.value;
        GlobalLogger::instance().setLogLevel(this->globalConfig.logLevel);
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
        LOG(Error, "Sample library not found under " + this->globalConfig.libraryPath.getFullPathName()); //FIXME: add in ui on Kit tab input  to set Library path
        jassertfalse;
        return;
    }

    this->previewResolveThreadPool.removeAllJobs(true, 2000);

    {
        const juce::ScopedLock resolutionLock(this->previewTargetResolutionLock);
        this->previewTargetCache.clear();
        this->pendingPreviewTargetKeys.clear();
    }

    auto libraryManager = std::make_shared<KitLib::LibraryManager>(this->globalConfig.libraryPath);
    libraryManager->scanAll();
    this->libraryManagerPtr = std::move(libraryManager);

    if (this->sampleLoaderPtr == nullptr)
        this->sampleLoaderPtr = std::make_unique<SampleLoader>();
    this->sampleLoaderPtr->setCacheLimitBytes(this->globalConfig.sampleLoaderCacheLimitMegabytes * 1024ull * 1024ull);
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

    auto micLayout = this->globalConfig.micLayout;
    if (micLayout.isEmpty() || !this->useMicLayout(micLayout)) {
        LOG(Notice, "Using default mic layout");
        auto src = this->getMicLayoutManager()->getDefault();
        this->setMicLayout(src);
    }

    auto instrumentMap = treeState.state["instrument_map"].toString();
    if (instrumentMap.isEmpty()) {
        LOG(Notice, "SKIP mapping default kit for now...");
        // this->mapDefaultKit();
    } else {
        if (!this->useInstrumentMap(instrumentMap))
            this->mapDefaultKit();
    }
}
