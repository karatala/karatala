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
    juce::String contentHash;
    juce::int64 metadataFileSizeBytes = 0;

    // Pre-processing options that affect cached audio identity.
    float fixedGainDb = 0.0f;
    double maxReadLengthMs = 0.0; // <=0: use loader default
    double fadeOutMs = 0.0;       // <=0: use loader default
};

struct SamplePreprocessingRecipe {
    static constexpr int ALGORITHM_VERSION = 2;

    int gainMilliDb = 0;
    juce::int64 maxReadMicroseconds = 0;
    juce::int64 fadeOutMicroseconds = 0;

    float getGainDb() const { return static_cast<float>(this->gainMilliDb) / 1000.0f; }
    double getMaxReadLengthMs() const { return static_cast<double>(this->maxReadMicroseconds) / 1000.0; }
    double getFadeOutMs() const { return static_cast<double>(this->fadeOutMicroseconds) / 1000.0; }
    juce::String makeKey() const {
        return "recipe=v" + juce::String(ALGORITHM_VERSION)
            + "|gain_mdb=" + juce::String(this->gainMilliDb)
            + "|max_read_us=" + juce::String(this->maxReadMicroseconds)
            + "|fade_us=" + juce::String(this->fadeOutMicroseconds);
    }
};

struct ResolvedSampleLoadSpec {
    juce::File file;
    juce::String sourceIdentity;
    SamplePreprocessingRecipe recipe;

    juce::String makeCacheKey() const { return this->sourceIdentity + "|" + this->recipe.makeKey(); }
};

class SampleLoaderQueue {
public:
    using LoadCallback = std::function<void(std::shared_ptr<juce::AudioBuffer<float>>, double)>;

    explicit SampleLoaderQueue(SampleLoader& sampleLoader) : loader(sampleLoader) {
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
    void enqueueCandidate(const SampleLoadSpec& loadSpec, KitLib::InstrumentSpec::Type instType, StringsDictSingleton::Id artId, uint8_t velocity, uint8_t rrIndex, bool earlyLoad, LoadCallback cb);

    // Attempt to mark the worker as started. Returns true if the worker
    // was already started, false if we successfully marked it started.
    bool isStarted() {
        bool expected = false;
        return !this->workerStarted.compare_exchange_strong(expected, true);
    }
private:
    struct Candidate {
        ResolvedSampleLoadSpec loadSpec;
        juce::String cacheKey;
        KitLib::InstrumentSpec::Type instType;
        StringsDictSingleton::Id artId;
        uint8_t velocity;
        uint8_t rrIndex;
        bool earlyLoad = false;
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
    int computeInstrumentPriority(KitLib::InstrumentSpec::Type type) const;
    int determineStage(const Candidate& candidate) const;
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
        if (this->queuePtr) this->queuePtr->stopProcessing();
        this->threadPool.removeAllJobs(true, 5000);
    }

    void requestAsyncLoad(const SampleLoadSpec& loadSpec, LoadCallback callback);
    void requestAsyncLoad(const ResolvedSampleLoadSpec& loadSpec, LoadCallback callback);

    ResolvedSampleLoadSpec resolveLoadSpec(const SampleLoadSpec& loadSpec) const;
    juce::String makeCacheKey(const SampleLoadSpec& loadSpec) const { return this->resolveLoadSpec(loadSpec).makeCacheKey(); }

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
        LoadJob(SampleLoader& owner, ResolvedSampleLoadSpec spec, juce::String key, LoadCallback fn)
            : ThreadPoolJob("Sample Load Job")
            , loader(owner)
            , loadSpec(std::move(spec))
            , cacheKey(std::move(key))
            , callback(std::move(fn))
        {}
        JobStatus runJob() override;

    private:
        SampleLoader& loader;
        ResolvedSampleLoadSpec loadSpec;
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
    std::unordered_map<juce::String, std::vector<LoadCallback>> pendingLoads;

    size_t currentCacheBytes = 0;
    size_t cacheLimitBytes;

    double maxSampleLengthMs = 5000.0;
    double fadeOutMs = 2000.0;

};
