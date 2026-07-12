#include "SampleLoader.h"
#include <juce_events/juce_events.h>
#include <algorithm>
#include <chrono>

void SampleLoaderQueue::startProcessing() {
    if (this->isStarted()) { LOG(Warning, "SampleLoaderQueue: worker already started"); return; }
    this->stopFlag.store(false);
    this->workerThread = std::thread([this]() { this->workerLoop(); });
}

void SampleLoaderQueue::stopProcessing() {
    this->workerStarted.store(false);
    this->stopFlag.store(true);
    if (this->workerThread.joinable()) {
        try {
            this->workerThread.join();
        } catch (...) {}
    }
}

void SampleLoaderQueue::enqueueCandidate(const SampleLoadSpec& loadSpec, KitLib::InstrumentSpec::Type instType, StringsDictSingleton::Id artId, uint8_t velocity, uint8_t rrIndex, SampleLoaderQueue::LoadCallback cb) {
    const juce::String cacheKey = this->loader.makeCacheKey(loadSpec);
    std::lock_guard<std::mutex> g(lock);
    auto pit = this->pendingCallbacks.find(cacheKey);
    if (pit != this->pendingCallbacks.end()) {
        pit->second.push_back(cb);
        queuedCount.fetch_add(1);
        // LOG(Debug, "SampleLoaderQueue: attached additional callback for pending key " + cacheKey);
        return;
    }

    Candidate cand;
    cand.loadSpec = loadSpec;
    cand.cacheKey = cacheKey;
    cand.instType = instType;
    cand.artId = artId;
    cand.velocity = velocity;
    cand.rrIndex = rrIndex;
    cand.priority = computeInstrumentPriority(instType);
    const int stage = determineStage(cand);
    int idx = std::clamp(stage - 1, 0, 3);
    stageQueues[idx].push_back(std::move(cand));
    this->pendingCallbacks.emplace(cacheKey, std::vector<LoadCallback>{cb});
    queuedCount.fetch_add(1);
    // juce::String artName = StringsDictSingleton::nameOf(artId);
    // LOG(Verbose, "SampleLoaderQueue: enqueued varSpec file=" + loadSpec.file.getFullPathName() + " inst=" + KitLib::InstrumentSpec::typeToString(instType) + " art=" + artName + " vel=" + juce::String((int)vel) + " rr=" + juce::String((int)rrIndex));
}

int SampleLoaderQueue::computeInstrumentPriority(KitLib::InstrumentSpec::Type type) const {
    switch (type) {
        case KitLib::InstrumentSpec::Type::Kick: return 1;
        case KitLib::InstrumentSpec::Type::Snare: return 2;
        case KitLib::InstrumentSpec::Type::HiHat: return 3;
        case KitLib::InstrumentSpec::Type::Ride: return 4;
        case KitLib::InstrumentSpec::Type::Crash: return 5;
        case KitLib::InstrumentSpec::Type::Tom: return 6;
        case KitLib::InstrumentSpec::Type::Splash:
        case KitLib::InstrumentSpec::Type::China:
        case KitLib::InstrumentSpec::Type::Bell:
        case KitLib::InstrumentSpec::Type::Stack:
        case KitLib::InstrumentSpec::Type::Chopper:
        case KitLib::InstrumentSpec::Type::Gong:
        case KitLib::InstrumentSpec::Type::Karatala:
        case KitLib::InstrumentSpec::Type::Cymbal:
            return 7;
        case KitLib::InstrumentSpec::Type::Octoban:
        case KitLib::InstrumentSpec::Type::Rototom:
        case KitLib::InstrumentSpec::Type::Timbale:
        case KitLib::InstrumentSpec::Type::Timpani:
        case KitLib::InstrumentSpec::Type::Conga:
        case KitLib::InstrumentSpec::Type::Bongo:
        case KitLib::InstrumentSpec::Type::Djembe:
        case KitLib::InstrumentSpec::Type::Cajon:
        case KitLib::InstrumentSpec::Type::Taiko:
        case KitLib::InstrumentSpec::Type::Surdo:
        case KitLib::InstrumentSpec::Type::Mridangam:
        case KitLib::InstrumentSpec::Type::TalkingDrum:
        case KitLib::InstrumentSpec::Type::Drum:
            return 8;
        case KitLib::InstrumentSpec::Type::Cowbell:
        case KitLib::InstrumentSpec::Type::Tambourine:
        case KitLib::InstrumentSpec::Type::Shaker:
        case KitLib::InstrumentSpec::Type::Maracas:
        case KitLib::InstrumentSpec::Type::Cabasa:
        case KitLib::InstrumentSpec::Type::Guiro:
        case KitLib::InstrumentSpec::Type::Claves:
        case KitLib::InstrumentSpec::Type::Agogo:
        case KitLib::InstrumentSpec::Type::WoodBlock:
        case KitLib::InstrumentSpec::Type::TempleBlock:
        case KitLib::InstrumentSpec::Type::Triangle:
        case KitLib::InstrumentSpec::Type::Vibraslap:
        case KitLib::InstrumentSpec::Type::Castanets:
        case KitLib::InstrumentSpec::Type::SleighBells:
        case KitLib::InstrumentSpec::Type::Whip:
        case KitLib::InstrumentSpec::Type::Ratchet:
        case KitLib::InstrumentSpec::Type::Percussion:
        case KitLib::InstrumentSpec::Type::Other:
        case KitLib::InstrumentSpec::Type::Unknown:
            return 9;
    }
    return 9;
}

int SampleLoaderQueue::determineStage(const Candidate& cand) const {
    if (cand.rrIndex > 2) return 4;
    // Use articulation Ids for detection where possible (prefer StringsDictSingleton ids)
    bool isMain = false;
    const auto centerId = StringsDictSingleton::getId("center");
    const auto rimId = StringsDictSingleton::getId("rim");
    const auto bowId = StringsDictSingleton::getId("bow");
    const auto bellId = StringsDictSingleton::getId("bell");
    const auto edgeId = StringsDictSingleton::getId("edge");
    const auto pedalId = StringsDictSingleton::getId("pedal");

    switch (cand.instType) {
        case KitLib::InstrumentSpec::Type::Kick:
        case KitLib::InstrumentSpec::Type::Snare:
        case KitLib::InstrumentSpec::Type::Tom:
        case KitLib::InstrumentSpec::Type::Octoban:
        case KitLib::InstrumentSpec::Type::Rototom:
        case KitLib::InstrumentSpec::Type::Timbale:
        case KitLib::InstrumentSpec::Type::Timpani:
        case KitLib::InstrumentSpec::Type::Conga:
        case KitLib::InstrumentSpec::Type::Bongo:
        case KitLib::InstrumentSpec::Type::Djembe:
        case KitLib::InstrumentSpec::Type::Cajon:
        case KitLib::InstrumentSpec::Type::Taiko:
        case KitLib::InstrumentSpec::Type::Surdo:
        case KitLib::InstrumentSpec::Type::Mridangam:
        case KitLib::InstrumentSpec::Type::TalkingDrum:
        case KitLib::InstrumentSpec::Type::Drum:
            if (cand.artId == centerId || cand.artId == rimId) isMain = (cand.artId == centerId);
            break;
        case KitLib::InstrumentSpec::Type::Ride:
            if (cand.artId == bowId || cand.artId == bellId) isMain = true;
            break;
        case KitLib::InstrumentSpec::Type::HiHat:
            if (cand.artId == bowId || cand.artId == pedalId) isMain = true;
            break;
        case KitLib::InstrumentSpec::Type::Crash:
        case KitLib::InstrumentSpec::Type::Splash:
        case KitLib::InstrumentSpec::Type::China:
        case KitLib::InstrumentSpec::Type::Bell:
        case KitLib::InstrumentSpec::Type::Stack:
        case KitLib::InstrumentSpec::Type::Chopper:
        case KitLib::InstrumentSpec::Type::Gong:
        case KitLib::InstrumentSpec::Type::Karatala:
        case KitLib::InstrumentSpec::Type::Cymbal:
            if (cand.artId == edgeId || cand.artId == bowId || cand.artId == bellId) isMain = (cand.artId == edgeId || cand.artId == bowId);
            break;
        case KitLib::InstrumentSpec::Type::Cowbell:
        case KitLib::InstrumentSpec::Type::Tambourine:
        case KitLib::InstrumentSpec::Type::Shaker:
        case KitLib::InstrumentSpec::Type::Maracas:
        case KitLib::InstrumentSpec::Type::Cabasa:
        case KitLib::InstrumentSpec::Type::Guiro:
        case KitLib::InstrumentSpec::Type::Claves:
        case KitLib::InstrumentSpec::Type::Agogo:
        case KitLib::InstrumentSpec::Type::WoodBlock:
        case KitLib::InstrumentSpec::Type::TempleBlock:
        case KitLib::InstrumentSpec::Type::Triangle:
        case KitLib::InstrumentSpec::Type::Vibraslap:
        case KitLib::InstrumentSpec::Type::Castanets:
        case KitLib::InstrumentSpec::Type::SleighBells:
        case KitLib::InstrumentSpec::Type::Whip:
        case KitLib::InstrumentSpec::Type::Ratchet:
        case KitLib::InstrumentSpec::Type::Unknown:
        case KitLib::InstrumentSpec::Type::Percussion:
        case KitLib::InstrumentSpec::Type::Other:
            break;
    }

    // Representative perceptual velocities (example mapping)
    static const std::vector<int> perceptual = {10, 35, 65, 95, 120};

    // Determine if this candidate lies close to one of the representative velocities
    auto nearestIt = std::min_element(perceptual.begin(), perceptual.end(), [&](int a, int b){
        return std::abs(static_cast<int>(cand.velocity) - a) < std::abs(static_cast<int>(cand.velocity) - b);
    });
    int nearest = *nearestIt;
    int dist = std::abs(static_cast<int>(cand.velocity) - nearest);

    // If close enough to a representative (within 12), consider it part of the early subset
    bool isRepresentative = dist <= 12;

    if (isMain && isRepresentative) return 1; // stage1: main articulation representative velocities
    if (!isMain && isRepresentative) return 2; // stage2: other articulations representative velocities
    // stage3: remaining velocities
    return 3;
}

void SampleLoaderQueue::workerLoop() {
    using namespace std::chrono_literals;
    while (!this->stopFlag.load()) {
        Candidate task;
        bool found = false;
        int taskStage = -1;

        // scan stages 1..4 and pick highest-priority candidate (lowest priority value)
        {
            std::lock_guard<std::mutex> g(this->lock);
            for (int s = 0; s < 4; ++s) {
                if (this->stageQueues[s].empty()) continue;
                // find best candidate in this stage by priority value
                auto it = std::min_element(this->stageQueues[s].begin(), this->stageQueues[s].end(), [](auto& a, auto& b){ return a.priority < b.priority; });
                if (it != this->stageQueues[s].end()) {
                    task = *it;
                    this->stageQueues[s].erase(it);
                    found = true;
                    taskStage = s + 1; // stages are 1..4
                    break;
                }
            }
        }

        if (!found) {
            std::this_thread::sleep_for(100ms);
            continue;
        }

        // wait until under maxInFlight
        while (this->inFlightCount.load() >= this->maxInFlight.load() && !stopFlag.load()) {
            std::this_thread::sleep_for(5ms);
        }

        this->dispatchedCount.fetch_add(1);
        this->inFlightCount.fetch_add(1);

        // juce::String artName = StringsDictSingleton::nameOf(task.artId);
        // LOG(Verbose, "SampleLoaderQueue: dispatching file=" + task.loadSpec.file.getFullPathName() + " inst=" + KitLib::InstrumentSpec::typeToString(task.instType) + " art=" + artName + " vel=" + juce::String((int)task.velocity) + " rr=" + juce::String((int)task.rrIndex));

        // Wrap callback to update stats and call all pending callbacks registered for this spec cache key
        juce::String pendingKey = task.cacheKey;
        const auto dispatchTime = std::chrono::steady_clock::now();
        const int stageIdx = std::clamp(taskStage - 1, 0, 3);

        // Update per-stage counters/timestamp for aggregated logging
        this->stageInFlightCount[static_cast<size_t>(stageIdx)].fetch_add(1);
        this->stageDispatchedCount[static_cast<size_t>(stageIdx)].fetch_add(1);
        {
            std::lock_guard<std::mutex> g(this->stageTimeMutex);
            if (this->stageFirstDispatchTime[static_cast<size_t>(stageIdx)] == std::chrono::steady_clock::time_point())
                this->stageFirstDispatchTime[static_cast<size_t>(stageIdx)] = dispatchTime;
        }

        loader.requestAsyncLoad(task.loadSpec, [this, pendingKey](std::shared_ptr<juce::AudioBuffer<float>> buf, double sr){
            // collect callbacks under lock then erase entry so future enqueues will create a new pending vector
            std::vector<LoadCallback> cbs;
            {
                std::lock_guard<std::mutex> g(lock);
                auto it = this->pendingCallbacks.find(pendingKey);
                if (it != this->pendingCallbacks.end()) {
                    cbs = std::move(it->second);
                    this->pendingCallbacks.erase(it);
                }
            }

            // invoke all callbacks (call on message thread via MessageManager if needed by caller)
            for (auto& cb : cbs) {
                try { cb(buf, sr); } catch (...) {}
            }

            this->loadedCount.fetch_add(1);
            this->inFlightCount.fetch_sub(1);

            // Decrement per-stage in-flight counter and if this was the last
            // in-flight task for the stage, emit a single aggregated log.
            // try {
            //     const int prev = this->stageInFlightCount[stageIdx].fetch_sub(1);
            //     if (prev == 1) {
            //         std::chrono::steady_clock::time_point start;
            //         int dispatched = 0;
            //         {
            //             std::lock_guard<std::mutex> g(this->stageTimeMutex);
            //             start = this->stageFirstDispatchTime[stageIdx];
            //             this->stageFirstDispatchTime[stageIdx] = std::chrono::steady_clock::time_point();
            //         }
            //         dispatched = this->stageDispatchedCount[stageIdx].exchange(0);
            //         if (start != std::chrono::steady_clock::time_point()) {
            //             const double secs = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - start).count();
            //             LOG(Info, "SampleLoaderQueue: stage " + juce::String((int)taskStage) + " finished " + juce::String(secs, 3) + "s for " + juce::String(dispatched) + " files");
            //         }
            //     }
            // } catch (...) {}
        });

        // small pause to avoid saturating threadpool instantly
        std::this_thread::sleep_for(10ms);
    }
}


void SampleLoader::requestAsyncLoad(const SampleLoadSpec& loadSpec, LoadCallback callback) {
    if (!loadSpec.file.existsAsFile()) {
        juce::MessageManager::callAsync([callback]() {
            callback(nullptr, 0.0);
        });
        return;
    }

    const auto cacheKey = this->makeCacheKey(loadSpec);
    { // Check cache
        std::lock_guard<std::mutex> lock(cacheMutex);

        auto it = cache.find(cacheKey);
        if (it != cache.end()) {
            auto buf = it->second.buffer;
            auto cachedSr = it->second.sampleRate;
            juce::MessageManager::callAsync([buf, cachedSr, callback, loadSpec]() {
                // LOG(Debug, "SampleLoader: cache hit for " + loadSpec.file.getFullPathName());
                callback(buf, cachedSr);
            });
            return;
        }
    }

    threadPool.addJob(new LoadJob(*this, loadSpec, cacheKey, callback), true);
}


juce::ThreadPoolJob::JobStatus SampleLoader::LoadJob::runJob() {
    std::unique_ptr<juce::InputStream> in(this->loadSpec.file.createInputStream());
    if (!in) {
        juce::MessageManager::callAsync([cb = callback]() {
            cb(nullptr, 0.0);
        });
        return jobHasFinished;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(loader.formatManager.createReaderFor(std::move(in)));
    if (!reader) {
        juce::MessageManager::callAsync([cb = callback]() {
            cb(nullptr, 0.0);
        });
        return jobHasFinished;
    }

    const int channels = (int)reader->numChannels;
    const double sampleRate = reader->sampleRate;

    const double resolvedMaxReadLengthMs = this->loadSpec.maxReadLengthMs > 0.0
        ? this->loadSpec.maxReadLengthMs
        : loader.maxSampleLengthMs;

    const double resolvedFadeOutMs = this->loadSpec.fadeOutMs > 0.0
        ? this->loadSpec.fadeOutMs
        : loader.fadeOutMs;

    // Clamp length
    const int maxSamples = juce::jmax(1, (int)(sampleRate * (resolvedMaxReadLengthMs * 0.001)));

    const int samplesToRead = (int)std::min<int64_t>(reader->lengthInSamples, maxSamples);

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(channels, samplesToRead);

    if (!reader->read(buffer.get(), 0, samplesToRead, 0, true, true)) {
        juce::MessageManager::callAsync([cb = callback]() {
            cb(nullptr, 0.0);
        });
        return jobHasFinished;
    }

    if (this->loadSpec.volumeTrimDb != 0.0f) {
        const float gainLinear = juce::Decibels::decibelsToGain(this->loadSpec.volumeTrimDb);
        for (int channelIdx = 0; channelIdx < channels; ++channelIdx)
            buffer->applyGain(channelIdx, 0, samplesToRead, gainLinear);
    }

    // Apply fade out
    const int fadeSamples = (int)(sampleRate * (resolvedFadeOutMs * 0.001));
    const int fadeStart = std::max(0, samplesToRead - fadeSamples);
    for (int ch = 0; ch < channels; ++ch) {
        buffer->applyGainRamp(ch, fadeStart, samplesToRead - fadeStart, 1.0f, 0.0f);
    }

    { // Cache insert
        std::lock_guard<std::mutex> lock(loader.cacheMutex);

        const size_t bytes = (size_t)channels * (size_t)samplesToRead * sizeof(float);

        if (loader.currentCacheBytes + bytes <= loader.cacheLimitBytes) {
            loader.cache[cacheKey] = { buffer, sampleRate };
            loader.currentCacheBytes += bytes;
            // LOG(Debug, "SampleLoader::LoadJob: cached " + file.getFullPathName() + " (" + juce::String(bytes) + " bytes)");
        }
    }

    juce::MessageManager::callAsync([buf = buffer, sr = sampleRate, cb = callback]() {
        cb(buf, sr);
    });

    return jobHasFinished;
}
