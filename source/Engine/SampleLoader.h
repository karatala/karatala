#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <deque>
#include <atomic>
#include <array>
#include <chrono>
#include "KitLibModel.h"

class SampleLoader;

struct SampleLoadSpec {
    juce::File file;

    // Pre-processing options that affect cached audio identity.
    float volumeTrimDb = 0.0f;
    double maxReadLengthMs = 0.0; // <=0: use loader default
    double fadeOutMs = 0.0;       // <=0: use loader default

    // Future-proof extension marker for additional transforms.
    juce::String preprocessTag;
};

class SampleLoaderQueue {
public:
    using LoadCallback = std::function<void(std::shared_ptr<juce::AudioBuffer<float>>, double)>;

    explicit SampleLoaderQueue(SampleLoader& loader) : loader(loader) {
        this->stopFlag.store(false);
        for (auto &a : stageInFlightCount) a.store(0);
        for (auto &a : stageDispatchedCount) a.store(0);
        for (auto &t : stageFirstDispatchTime) t = std::chrono::steady_clock::time_point();
    }
    ~SampleLoaderQueue() { this->stopProcessing(); }

    // Enqueue a sample candidate for progressive staged loading.
    // - `instType` is the instrument type (Kick, Snare, HiHat, ...)
    // - `artId` is the articulation id (StringsDictSingleton::Id)
    // - `velocity` is the velocity layer value (1..127)
    // - `rrIndex` is the round-robin index (1..26)
    void enqueueCandidate(const SampleLoadSpec& loadSpec, KitLib::InstrumentSpec::Type instType, StringsDictSingleton::Id artId, uint8_t velocity, uint8_t rrIndex, LoadCallback cb);

    // Attempt to mark the worker as started. Returns true if the worker
    // was already started, false if we successfully marked it started.
    bool isStarted() {
        bool expected = false;
        return !this->workerStarted.compare_exchange_strong(expected, true);
    }
private:
    struct Candidate {
        SampleLoadSpec loadSpec;
        juce::String cacheKey;
        KitLib::InstrumentSpec::Type instType;
        StringsDictSingleton::Id artId;
        uint8_t velocity;
        uint8_t rrIndex;
        int priority; // lower is higher priority
    };

    SampleLoader& loader;
    std::mutex lock;
    std::deque<Candidate> stageQueues[4]; // index 0->stage1, 1->stage2, ...
    // map cache key -> pending callbacks (used to attach callbacks for equivalent load specs)
    std::unordered_map<juce::String, std::vector<LoadCallback>> pendingCallbacks;

    std::thread workerThread;
    std::atomic<bool> stopFlag{false};
    std::atomic<bool> workerStarted{false};

    // Stats
    std::atomic<uint32_t> queuedCount{0};
    std::atomic<uint32_t> dispatchedCount{0};
    std::atomic<uint32_t> loadedCount{0};
    std::atomic<uint32_t> inFlightCount{0};

    // concurrency
    std::atomic<uint32_t> maxInFlight{3};

    // Per-stage in-flight counters and first-dispatch timestamps used to
    // aggregate logs so we only emit one message per stage (not per file).
    std::array<std::atomic<int>, 4> stageInFlightCount;
    std::array<std::atomic<int>, 4> stageDispatchedCount;
    std::array<std::chrono::steady_clock::time_point, 4> stageFirstDispatchTime;
    std::mutex stageTimeMutex;

public:
    void startProcessing();
    void stopProcessing();

    void setMaxInFlight(uint32_t n) { this->maxInFlight.store(n); }
    uint32_t getQueuedCount() const { return this->queuedCount.load(); }
    uint32_t getLoadedCount() const { return this->loadedCount.load(); }

    void workerLoop();
    int computeInstrumentPriority(KitLib::InstrumentSpec::Type t) const;
    int determineStage(const Candidate& c) const;
};

class SampleLoader {
public:
    using BufferPtr = std::shared_ptr<juce::AudioBuffer<float>>;
    using LoadCallback = std::function<void(BufferPtr, double)>;

    SampleLoader() {
        formatManager.registerBasicFormats();
        this->queuePtr = std::make_unique<SampleLoaderQueue>(*this);
    }
    ~SampleLoader() {
        threadPool.removeAllJobs(true, 5000);
    }

    void requestAsyncLoad(const SampleLoadSpec& loadSpec, LoadCallback callback);

    juce::String makeCacheKey(const SampleLoadSpec& loadSpec) const {
        // const double resolvedMaxReadLengthMs = loadSpec.maxReadLengthMs > 0.0 ? loadSpec.maxReadLengthMs : this->maxSampleLengthMs;
        // const double resolvedFadeOutMs = loadSpec.fadeOutMs > 0.0 ? loadSpec.fadeOutMs : this->fadeOutMs;
        // const int volumeMilliDb = juce::roundToInt(loadSpec.volumeTrimDb * 1000.0f);
        // const int maxReadUs = juce::roundToInt(static_cast<float>(resolvedMaxReadLengthMs * 1000.0));
        // const int fadeOutUs = juce::roundToInt(static_cast<float>(resolvedFadeOutMs * 1000.0));
        return loadSpec.file.getFullPathName()
            // + "|v=" + juce::String(volumeMilliDb)
            // + "|len=" + juce::String(maxReadUs)
            // + "|fade=" + juce::String(fadeOutUs)
            + "|tag=" + loadSpec.preprocessTag
        ;
    }

    SampleLoaderQueue& getQueue() { return *this->queuePtr; }

    void setCacheLimitBytes(size_t bytes) {
        std::lock_guard<std::mutex> lock(this->cacheMutex);
        this->cacheLimitBytes = bytes;
    }
    void setMaxSampleLengthMs(double ms) { this->maxSampleLengthMs = ms; }
    void setFadeOutMs(double ms) { this->fadeOutMs = ms; }

private:
    //========================================================
    class LoadJob : public juce::ThreadPoolJob {
    public:
        LoadJob(SampleLoader& owner, SampleLoadSpec loadSpec, juce::String cacheKey, LoadCallback callback)
            : ThreadPoolJob("Sample Load Job")
            , loader(owner)
            , loadSpec(std::move(loadSpec))
            , cacheKey(std::move(cacheKey))
            , callback(std::move(callback))
        {}
        JobStatus runJob() override;

    private:
        SampleLoader& loader;
        SampleLoadSpec loadSpec;
        juce::String cacheKey;
        LoadCallback callback;
    };

    friend class LoadJob;

    //========================================================
    juce::AudioFormatManager formatManager;
    juce::ThreadPool threadPool;

    std::unique_ptr<SampleLoaderQueue> queuePtr;

    std::mutex cacheMutex;
    struct CacheEntry {
        BufferPtr buffer;
        double sampleRate = 0.0;
    };
    std::unordered_map<juce::String, CacheEntry> cache;

    size_t currentCacheBytes = 0;
    size_t cacheLimitBytes;

    double maxSampleLengthMs = 5000.0;
    double fadeOutMs = 2000.0;

};
