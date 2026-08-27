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
#include <cstdint>
#include <vector>
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

struct SampleLoadDemandState {
    std::atomic<uint32_t> noteCount { 0 };
    std::atomic<int> requestedVelocityLevelIdx { -1 };
    std::atomic<uint32_t> revision { 0 };
    std::atomic<uint64_t> activeTargetId { 0 };

    void activate(uint64_t targetId) noexcept {
        this->noteCount.store(0, std::memory_order_relaxed);
        this->requestedVelocityLevelIdx.store(-1, std::memory_order_relaxed);
        this->revision.store(0, std::memory_order_relaxed);
        this->activeTargetId.store(targetId, std::memory_order_release);
    }

    void request(int velocityLevelIdx) noexcept {
        if (velocityLevelIdx < 0 || this->activeTargetId.load(std::memory_order_acquire) == 0) return;
        this->requestedVelocityLevelIdx.store(velocityLevelIdx, std::memory_order_relaxed);
        uint32_t currentNoteCount = this->noteCount.load(std::memory_order_relaxed);
        while (currentNoteCount < 3 && !this->noteCount.compare_exchange_weak(currentNoteCount, currentNoteCount + 1, std::memory_order_relaxed)) {}
        this->revision.fetch_add(1, std::memory_order_release);
    }

    void deactivate() noexcept { this->activeTargetId.store(0, std::memory_order_release); }
};

class SampleLoaderQueue {
public:
    using LoadCallback = std::function<void(std::shared_ptr<juce::AudioBuffer<float>>, double)>;

    struct LogicalHitLoadEntry {
        SampleLoadSpec loadSpec;
        LoadCallback callback;
    };

    struct LogicalHitDemandWork {
        std::vector<LogicalHitLoadEntry> loadEntries;
        std::function<bool(size_t)> registerProgress;
        std::function<bool()> isReady;
        int velocityLevelIdx = -1;
        uint8_t velocity = 0;
        uint8_t roundRobinId = 0;
        int initialLoadStage = 3;
        bool submitted = false;
    };

    explicit SampleLoaderQueue(SampleLoader& sampleLoader) : loader(sampleLoader) {
        this->stopFlag.store(false);
        for (auto& count : this->priorityInFlightCount) count.store(0);
        for (auto& count : this->priorityDispatchedCount) count.store(0);
        for (auto& timePoint : this->priorityFirstDispatchTime) timePoint = std::chrono::steady_clock::time_point();
    }
    ~SampleLoaderQueue() { this->stopProcessing(); }

    // Submit all active mic files for one logical hit. Stage 1 is highest-velocity minimal work, stage 2 is other-velocity minimal work, and stage 3 is Full-mode remainder.
    void enqueueLogicalHit(std::vector<LogicalHitLoadEntry> entries, uint64_t targetId, KitLib::InstrumentSpec::Type instType, StringsDictSingleton::Id artId, uint8_t velocity, uint8_t roundRobinId, int stage);
    void registerDemandTarget(std::shared_ptr<SampleLoadDemandState> state, std::vector<LogicalHitDemandWork> work, uint64_t targetId, KitLib::InstrumentSpec::Type instType, StringsDictSingleton::Id artId);
    uint64_t createLoadTargetId() { return this->nextLoadTargetId.fetch_add(1, std::memory_order_relaxed); }

    // Attempt to mark the worker as started. Returns true if the worker
    // was already started, false if we successfully marked it started.
    bool isStarted() {
        bool expected = false;
        return !this->workerStarted.compare_exchange_strong(expected, true);
    }
private:
    struct LogicalHitLoadGroup {
        uint64_t targetId = 0;
        uint64_t sequence = 0;
        KitLib::InstrumentSpec::Type instType = KitLib::InstrumentSpec::Type::Unknown;
        StringsDictSingleton::Id artId = 0;
        uint8_t velocity = 0;
        uint8_t roundRobinId = 0;
        int priorityClass = 5;
        int priority = 9; // lower is higher priority
    };

    struct Candidate {
        ResolvedSampleLoadSpec loadSpec;
        juce::String cacheKey;
        std::shared_ptr<const LogicalHitLoadGroup> group;
    };

    struct DemandTarget {
        std::shared_ptr<SampleLoadDemandState> state;
        std::vector<LogicalHitDemandWork> work;
        uint64_t targetId = 0;
        KitLib::InstrumentSpec::Type instType = KitLib::InstrumentSpec::Type::Unknown;
        StringsDictSingleton::Id artId = 0;
        uint32_t lastHandledRevision = 0;
        size_t nextHotWorkIdx = 0;
    };

    static constexpr int priorityClassCount = 6;
    static constexpr int demandMinimalPriorityClass = 0;
    static constexpr int activatedDormantPriorityClass = 1;
    static constexpr int hotArticulationPriorityClass = 2;
    static constexpr int staticPriorityClassOffset = 2;

    SampleLoader& loader;
    std::mutex lock;
    std::array<std::deque<Candidate>, priorityClassCount> priorityQueues;
    // map cache key -> pending callbacks (used to attach callbacks for equivalent load specs)
    std::unordered_map<juce::String, std::vector<LoadCallback>> pendingCallbacks;
    std::vector<std::shared_ptr<DemandTarget>> demandTargets;
    size_t nextDemandTargetIdx = 0;
    std::array<uint64_t, priorityClassCount> activeGroupSequenceByPriority {};
    std::array<uint64_t, priorityClassCount> lastDispatchedTargetByPriority {};

    std::thread workerThread;
    std::atomic<bool> stopFlag{false};
    std::atomic<bool> workerStarted{false};
    std::atomic<uint64_t> nextLoadTargetId{1};
    uint64_t nextGroupSequence = 1;

    // Stats
    std::atomic<uint32_t> queuedCount{0};
    std::atomic<uint32_t> dispatchedCount{0};
    std::atomic<uint32_t> loadedCount{0};
    std::atomic<uint32_t> inFlightCount{0};

    // concurrency
    std::atomic<uint32_t> maxInFlight{1};

    // Per-priority in-flight counters and first-dispatch timestamps used to aggregate logs so we only emit one message per priority class.
    std::array<std::atomic<int>, priorityClassCount> priorityInFlightCount;
    std::array<std::atomic<int>, priorityClassCount> priorityDispatchedCount;
    std::array<std::chrono::steady_clock::time_point, priorityClassCount> priorityFirstDispatchTime;
    std::mutex priorityTimeMutex;

public:
    void startProcessing();
    void stopProcessing();

    void setMaxInFlight(uint32_t n) { this->maxInFlight.store(n); }
    uint32_t getQueuedCount() const { return this->queuedCount.load(); }
    uint32_t getLoadedCount() const { return this->loadedCount.load(); }

    void workerLoop();
    int computeInstrumentPriority(KitLib::InstrumentSpec::Type type) const;

private:
    bool isHigherPriority(const LogicalHitLoadGroup& first, const LogicalHitLoadGroup& second) const;
    void promoteQueuedCandidateLocked(const juce::String& cacheKey, const std::shared_ptr<const LogicalHitLoadGroup>& requestedGroup);
    void enqueueLogicalHitWithPriority(std::vector<LogicalHitLoadEntry> entries, uint64_t targetId, KitLib::InstrumentSpec::Type instType, StringsDictSingleton::Id artId, uint8_t velocity, uint8_t roundRobinId, int priorityClass);
    bool promoteLogicalHit(uint64_t targetId, uint8_t velocity, uint8_t roundRobinId, int priorityClass);
    std::shared_ptr<DemandTarget> takeNextDemandTarget();
    void handleDemandTarget(DemandTarget& target);
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
    size_t getCurrentCacheBytes() const { return this->currentCacheBytes.load(std::memory_order_acquire); }
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
    juce::ThreadPool threadPool { juce::ThreadPool::Options().withNumberOfThreads(1).withDesiredThreadPriority(juce::Thread::Priority::low) };

    std::unique_ptr<SampleLoaderQueue> queuePtr;

    std::mutex cacheMutex;
    struct CacheEntry {
        BufferPtr buffer;
        double sampleRate = 0.0;
    };
    std::unordered_map<juce::String, CacheEntry> cache;
    std::unordered_map<juce::String, std::vector<LoadCallback>> pendingLoads;

    std::atomic<size_t> currentCacheBytes { 0 };
    size_t cacheLimitBytes;

    double maxSampleLengthMs = 5000.0;
    double fadeOutMs = 2000.0;

};
