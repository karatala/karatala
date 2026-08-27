#include "SampleLoader.h"
#include "../Config/PluginLogger.h"
#include <juce_events/juce_events.h>
#include <algorithm>
#include <chrono>
#include <cmath>

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

void SampleLoaderQueue::enqueueLogicalHit(std::vector<LogicalHitLoadEntry> entries, uint64_t targetId, KitLib::InstrumentSpec::Type instType, StringsDictSingleton::Id artId, uint8_t velocity, uint8_t roundRobinId, int stage) {
    this->enqueueLogicalHitWithPriority(std::move(entries), targetId, instType, artId, velocity, roundRobinId, std::clamp(stage, 1, 3) + staticPriorityClassOffset);
}

void SampleLoaderQueue::registerDemandTarget(std::shared_ptr<SampleLoadDemandState> state, std::vector<LogicalHitDemandWork> work, uint64_t targetId, KitLib::InstrumentSpec::Type instType, StringsDictSingleton::Id artId) {
    if (state == nullptr || work.empty() || targetId == 0) return;
    auto target = std::make_shared<DemandTarget>();
    target->state = std::move(state);
    target->work = std::move(work);
    target->targetId = targetId;
    target->instType = instType;
    target->artId = artId;
    std::lock_guard<std::mutex> queueLock(this->lock);
    this->demandTargets.push_back(std::move(target));
}

void SampleLoaderQueue::enqueueLogicalHitWithPriority(std::vector<LogicalHitLoadEntry> entries, uint64_t targetId, KitLib::InstrumentSpec::Type instType, StringsDictSingleton::Id artId, uint8_t velocity, uint8_t roundRobinId, int priorityClass) {
    if (entries.empty()) return;

    std::vector<ResolvedSampleLoadSpec> resolvedLoadSpecs;
    std::vector<juce::String> cacheKeys;
    resolvedLoadSpecs.reserve(entries.size());
    cacheKeys.reserve(entries.size());
    for (const auto& entry : entries) {
        auto resolvedLoadSpec = this->loader.resolveLoadSpec(entry.loadSpec);
        cacheKeys.push_back(resolvedLoadSpec.makeCacheKey());
        resolvedLoadSpecs.push_back(std::move(resolvedLoadSpec));
    }

    std::lock_guard<std::mutex> queueLock(this->lock);
    auto group = std::make_shared<LogicalHitLoadGroup>();
    group->targetId = targetId;
    group->sequence = this->nextGroupSequence++;
    group->instType = instType;
    group->artId = artId;
    group->velocity = velocity;
    group->roundRobinId = roundRobinId;
    group->priority = this->computeInstrumentPriority(instType);
    group->priorityClass = std::clamp(priorityClass, 0, priorityClassCount - 1);
    const size_t priorityIdx = static_cast<size_t>(group->priorityClass);

    for (size_t entryIdx = 0; entryIdx < entries.size(); ++entryIdx) {
        const auto& cacheKey = cacheKeys[entryIdx];
        auto pendingIt = this->pendingCallbacks.find(cacheKey);
        if (pendingIt != this->pendingCallbacks.end()) {
            pendingIt->second.push_back(std::move(entries[entryIdx].callback));
            this->promoteQueuedCandidateLocked(cacheKey, group);
        } else {
            Candidate candidate;
            candidate.loadSpec = std::move(resolvedLoadSpecs[entryIdx]);
            candidate.cacheKey = cacheKey;
            candidate.group = group;
            this->priorityQueues[priorityIdx].push_back(std::move(candidate));
            std::vector<LoadCallback> callbacks;
            callbacks.push_back(std::move(entries[entryIdx].callback));
            this->pendingCallbacks.emplace(cacheKey, std::move(callbacks));
        }
        this->queuedCount.fetch_add(1);
    }
}

bool SampleLoaderQueue::isHigherPriority(const LogicalHitLoadGroup& first, const LogicalHitLoadGroup& second) const {
    if (first.priorityClass != second.priorityClass) return first.priorityClass < second.priorityClass;
    if (first.priority != second.priority) return first.priority < second.priority;
    return first.sequence < second.sequence;
}

void SampleLoaderQueue::promoteQueuedCandidateLocked(const juce::String& cacheKey, const std::shared_ptr<const LogicalHitLoadGroup>& requestedGroup) {
    for (size_t priorityIdx = 0; priorityIdx < this->priorityQueues.size(); ++priorityIdx) {
        auto candidateIt = std::find_if(this->priorityQueues[priorityIdx].begin(), this->priorityQueues[priorityIdx].end(), [&cacheKey](const Candidate& candidate) { return candidate.cacheKey == cacheKey; });
        if (candidateIt == this->priorityQueues[priorityIdx].end()) continue;
        if (candidateIt->group != nullptr && this->isHigherPriority(*requestedGroup, *candidateIt->group)) {
            Candidate promotedCandidate = std::move(*candidateIt);
            if (this->activeGroupSequenceByPriority[priorityIdx] == promotedCandidate.group->sequence) this->activeGroupSequenceByPriority[priorityIdx] = 0;
            this->priorityQueues[priorityIdx].erase(candidateIt);
            promotedCandidate.group = requestedGroup;
            const size_t requestedPriorityIdx = static_cast<size_t>(std::clamp(requestedGroup->priorityClass, 0, priorityClassCount - 1));
            this->priorityQueues[requestedPriorityIdx].push_back(std::move(promotedCandidate));
        }
        return;
    }
}

bool SampleLoaderQueue::promoteLogicalHit(uint64_t targetId, uint8_t velocity, uint8_t roundRobinId, int priorityClass) {
    std::lock_guard<std::mutex> queueLock(this->lock);
    const int requestedPriorityClass = std::clamp(priorityClass, 0, priorityClassCount - 1);
    std::shared_ptr<LogicalHitLoadGroup> promotedGroup;
    bool promoted = false;
    for (size_t priorityIdx = 0; priorityIdx < this->priorityQueues.size(); ++priorityIdx) {
        auto candidateIt = this->priorityQueues[priorityIdx].begin();
        while (candidateIt != this->priorityQueues[priorityIdx].end()) {
            const auto& group = candidateIt->group;
            if (group == nullptr || group->targetId != targetId || group->velocity != velocity || group->roundRobinId != roundRobinId) {
                ++candidateIt;
                continue;
            }
            if (static_cast<int>(priorityIdx) <= requestedPriorityClass) {
                ++candidateIt;
                continue;
            }
            if (promotedGroup == nullptr) {
                promotedGroup = std::make_shared<LogicalHitLoadGroup>(*group);
                promotedGroup->priorityClass = requestedPriorityClass;
                promotedGroup->sequence = this->nextGroupSequence++;
            }
            if (this->activeGroupSequenceByPriority[priorityIdx] == group->sequence) this->activeGroupSequenceByPriority[priorityIdx] = 0;
            Candidate promotedCandidate = std::move(*candidateIt);
            candidateIt = this->priorityQueues[priorityIdx].erase(candidateIt);
            promotedCandidate.group = promotedGroup;
            this->priorityQueues[static_cast<size_t>(requestedPriorityClass)].push_back(std::move(promotedCandidate));
            promoted = true;
        }
    }
    return promoted;
}

std::shared_ptr<SampleLoaderQueue::DemandTarget> SampleLoaderQueue::takeNextDemandTarget() {
    std::lock_guard<std::mutex> queueLock(this->lock);
    this->demandTargets.erase(std::remove_if(this->demandTargets.begin(), this->demandTargets.end(), [](const std::shared_ptr<DemandTarget>& target) {
        return target == nullptr || target->state == nullptr || target->state->activeTargetId.load(std::memory_order_acquire) != target->targetId;
    }), this->demandTargets.end());
    if (this->demandTargets.empty()) {
        this->nextDemandTargetIdx = 0;
        return {};
    }
    if (this->nextDemandTargetIdx >= this->demandTargets.size()) this->nextDemandTargetIdx = 0;
    for (size_t checkedTargetCount = 0; checkedTargetCount < this->demandTargets.size(); ++checkedTargetCount) {
        const size_t targetIdx = (this->nextDemandTargetIdx + checkedTargetCount) % this->demandTargets.size();
        const auto& target = this->demandTargets[targetIdx];
        const uint32_t demandRevision = target->state->revision.load(std::memory_order_acquire);
        if (demandRevision == target->lastHandledRevision) continue;
        target->lastHandledRevision = demandRevision;
        this->nextDemandTargetIdx = (targetIdx + 1) % this->demandTargets.size();
        return target;
    }
    return {};
}

void SampleLoaderQueue::handleDemandTarget(DemandTarget& target) {
    if (target.state == nullptr || target.state->activeTargetId.load(std::memory_order_acquire) != target.targetId) return;
    const int requestedVelocityLevelIdx = target.state->requestedVelocityLevelIdx.load(std::memory_order_relaxed);
    if (requestedVelocityLevelIdx < 0) return;

    bool missingMinimalWork = false;
    for (auto& work : target.work) {
        if (!work.submitted || work.initialLoadStage > 2 || work.velocityLevelIdx != requestedVelocityLevelIdx || (work.isReady && work.isReady())) continue;
        missingMinimalWork = true;
        this->promoteLogicalHit(target.targetId, work.velocity, work.roundRobinId, demandMinimalPriorityClass);
    }
    if (missingMinimalWork) return;

    if (target.state->noteCount.load(std::memory_order_relaxed) >= 3 && !target.work.empty()) {
        for (size_t workOffset = 0; workOffset < target.work.size(); ++workOffset) {
            const size_t workIdx = (target.nextHotWorkIdx + workOffset) % target.work.size();
            auto& work = target.work[workIdx];
            if (!work.submitted || (work.isReady && work.isReady())) continue;
            if (!this->promoteLogicalHit(target.targetId, work.velocity, work.roundRobinId, hotArticulationPriorityClass)) continue;
            target.nextHotWorkIdx = (workIdx + 1) % target.work.size();
            break;
        }
    }

    for (auto& work : target.work) {
        if (work.submitted || work.velocityLevelIdx != requestedVelocityLevelIdx || work.loadEntries.empty()) continue;
        if (target.state->activeTargetId.load(std::memory_order_acquire) != target.targetId) return;
        if (!work.registerProgress || !work.registerProgress(work.loadEntries.size())) return;
        if (target.state->activeTargetId.load(std::memory_order_acquire) != target.targetId) return;
        work.submitted = true;
        this->enqueueLogicalHitWithPriority(std::move(work.loadEntries), target.targetId, target.instType, target.artId, work.velocity, work.roundRobinId, activatedDormantPriorityClass);
        return;
    }
}

int SampleLoaderQueue::computeInstrumentPriority(KitLib::InstrumentSpec::Type type) const {
    switch (type) {
        case KitLib::InstrumentSpec::Type::Kick: return 1;
        case KitLib::InstrumentSpec::Type::Snare: return 2;
        case KitLib::InstrumentSpec::Type::HiHat: return 3;
        case KitLib::InstrumentSpec::Type::Crash:
        case KitLib::InstrumentSpec::Type::Splash:
        case KitLib::InstrumentSpec::Type::China:
        case KitLib::InstrumentSpec::Type::Bell:
        case KitLib::InstrumentSpec::Type::Stack:
        case KitLib::InstrumentSpec::Type::Chopper:
        case KitLib::InstrumentSpec::Type::Gong:
        case KitLib::InstrumentSpec::Type::Karatala:
        case KitLib::InstrumentSpec::Type::Cymbal:
            return 4;
        case KitLib::InstrumentSpec::Type::Ride: return 5;
        case KitLib::InstrumentSpec::Type::Tom: return 6;
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
            return 7;
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
            return 8;
    }
    return 8;
}

void SampleLoaderQueue::workerLoop() {
    using namespace std::chrono_literals;
    while (!this->stopFlag.load()) {
        if (auto demandTarget = this->takeNextDemandTarget()) this->handleDemandTarget(*demandTarget);

        Candidate task;
        bool found = false;
        int taskPriorityClass = -1;

        // Scan priority classes in order, keep one logical hit together, and alternate demand targets between logical hits.
        {
            std::lock_guard<std::mutex> queueLock(this->lock);
            for (size_t priorityIdx = 0; priorityIdx < this->priorityQueues.size(); ++priorityIdx) {
                auto& priorityQueue = this->priorityQueues[priorityIdx];
                if (priorityQueue.empty()) {
                    this->activeGroupSequenceByPriority[priorityIdx] = 0;
                    continue;
                }

                auto candidateIt = priorityQueue.end();
                const uint64_t activeGroupSequence = this->activeGroupSequenceByPriority[priorityIdx];
                if (activeGroupSequence != 0) {
                    candidateIt = std::find_if(priorityQueue.begin(), priorityQueue.end(), [activeGroupSequence](const Candidate& candidate) { return candidate.group != nullptr && candidate.group->sequence == activeGroupSequence; });
                }
                if (candidateIt == priorityQueue.end()) {
                    const uint64_t lastTargetId = this->lastDispatchedTargetByPriority[priorityIdx];
                    candidateIt = std::min_element(priorityQueue.begin(), priorityQueue.end(), [lastTargetId](const Candidate& first, const Candidate& second) {
                        const bool firstAlternatesTarget = lastTargetId == 0 || first.group->targetId != lastTargetId;
                        const bool secondAlternatesTarget = lastTargetId == 0 || second.group->targetId != lastTargetId;
                        if (firstAlternatesTarget != secondAlternatesTarget) return firstAlternatesTarget;
                        if (first.group->priority != second.group->priority) return first.group->priority < second.group->priority;
                        return first.group->sequence < second.group->sequence;
                    });
                }
                if (candidateIt != priorityQueue.end()) {
                    task = std::move(*candidateIt);
                    priorityQueue.erase(candidateIt);
                    found = true;
                    taskPriorityClass = static_cast<int>(priorityIdx);
                    const uint64_t groupSequence = task.group != nullptr ? task.group->sequence : 0;
                    const bool groupHasQueuedFiles = groupSequence != 0 && std::any_of(priorityQueue.begin(), priorityQueue.end(), [groupSequence](const Candidate& candidate) { return candidate.group != nullptr && candidate.group->sequence == groupSequence; });
                    if (groupHasQueuedFiles) {
                        this->activeGroupSequenceByPriority[priorityIdx] = groupSequence;
                    } else {
                        this->activeGroupSequenceByPriority[priorityIdx] = 0;
                        this->lastDispatchedTargetByPriority[priorityIdx] = task.group != nullptr ? task.group->targetId : 0;
                    }
                    break;
                }
            }
        }

        if (!found) {
            std::this_thread::sleep_for(5ms);
            continue;
        }

        // wait until under maxInFlight
        while (this->inFlightCount.load() >= this->maxInFlight.load() && !stopFlag.load()) {
            std::this_thread::sleep_for(5ms);
        }

        this->dispatchedCount.fetch_add(1);
        this->inFlightCount.fetch_add(1);

        // juce::String artName = StringsDictSingleton::nameOf(task.group->artId);
        // LOG(Verbose, "SampleLoaderQueue: dispatching file=" + task.loadSpec.file.getFullPathName() + " inst=" + KitLib::InstrumentSpec::typeToString(task.group->instType) + " art=" + artName + " vel=" + juce::String((int)task.group->velocity) + " rr=" + juce::String((int)task.group->roundRobinId));

        // Wrap callback to update stats and call all pending callbacks registered for this spec cache key
        juce::String pendingKey = task.cacheKey;
        const auto dispatchTime = std::chrono::steady_clock::now();
        const int priorityIdx = std::clamp(taskPriorityClass, 0, priorityClassCount - 1);

        // Update per-priority counters/timestamp for aggregated logging
        this->priorityInFlightCount[static_cast<size_t>(priorityIdx)].fetch_add(1);
        this->priorityDispatchedCount[static_cast<size_t>(priorityIdx)].fetch_add(1);
        {
            std::lock_guard<std::mutex> priorityTimeLock(this->priorityTimeMutex);
            if (this->priorityFirstDispatchTime[static_cast<size_t>(priorityIdx)] == std::chrono::steady_clock::time_point()) this->priorityFirstDispatchTime[static_cast<size_t>(priorityIdx)] = dispatchTime;
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

            // Per-priority aggregated logging stays disabled until load status reporting is implemented.
            // try {
            //     const int prev = this->priorityInFlightCount[priorityIdx].fetch_sub(1);
            //     if (prev == 1) {
            //         std::chrono::steady_clock::time_point start;
            //         int dispatched = 0;
            //         {
            //             std::lock_guard<std::mutex> g(this->priorityTimeMutex);
            //             start = this->priorityFirstDispatchTime[priorityIdx];
            //             this->priorityFirstDispatchTime[priorityIdx] = std::chrono::steady_clock::time_point();
            //         }
            //         dispatched = this->priorityDispatchedCount[priorityIdx].exchange(0);
            //         if (start != std::chrono::steady_clock::time_point()) {
            //             const double secs = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - start).count();
            //             LOG(Info, "SampleLoaderQueue: priority " + juce::String(priorityIdx) + " finished " + juce::String(secs, 3) + "s for " + juce::String(dispatched) + " files");
            //         }
            //     }
            // } catch (...) {}
        });

        // small pause to avoid saturating threadpool instantly
        std::this_thread::sleep_for(10ms);
    }
}


void SampleLoader::requestAsyncLoad(const SampleLoadSpec& loadSpec, LoadCallback callback) {
    this->requestAsyncLoad(this->resolveLoadSpec(loadSpec), std::move(callback));
}

ResolvedSampleLoadSpec SampleLoader::resolveLoadSpec(const SampleLoadSpec& loadSpec) const {
    ResolvedSampleLoadSpec resolved;
    resolved.file = loadSpec.file;
    const auto currentFileSize = loadSpec.file.getSize();
    if (loadSpec.contentHash.isNotEmpty() && loadSpec.metadataFileSizeBytes > 0 && loadSpec.metadataFileSizeBytes == currentFileSize) {
        resolved.sourceIdentity = "blake2b128=" + loadSpec.contentHash.toLowerCase() + "|size=" + juce::String(currentFileSize);
    } else {
        resolved.sourceIdentity = "path=" + loadSpec.file.getFullPathName()
            + "|size=" + juce::String(currentFileSize)
            + "|mtime=" + juce::String(loadSpec.file.getLastModificationTime().toMilliseconds());
    }
    const double resolvedMaxReadLengthMs = loadSpec.maxReadLengthMs > 0.0 ? loadSpec.maxReadLengthMs : this->maxSampleLengthMs;
    const double resolvedFadeOutMs = loadSpec.fadeOutMs > 0.0 ? loadSpec.fadeOutMs : this->fadeOutMs;
    resolved.recipe.gainMilliDb = juce::roundToInt(loadSpec.fixedGainDb * 1000.0f);
    resolved.recipe.maxReadMicroseconds = static_cast<juce::int64>(std::llround(resolvedMaxReadLengthMs * 1000.0));
    resolved.recipe.fadeOutMicroseconds = static_cast<juce::int64>(std::llround(resolvedFadeOutMs * 1000.0));
    return resolved;
}

void SampleLoader::requestAsyncLoad(const ResolvedSampleLoadSpec& loadSpec, LoadCallback callback) {
    if (!loadSpec.file.existsAsFile()) {
        juce::MessageManager::callAsync([callback]() {
            callback(nullptr, 0.0);
        });
        return;
    }

    const auto cacheKey = loadSpec.makeCacheKey();
    BufferPtr cachedBuffer;
    double cachedSampleRate = 0.0;
    {
        std::lock_guard<std::mutex> lock(this->cacheMutex);
        auto cacheIt = this->cache.find(cacheKey);
        if (cacheIt != this->cache.end()) {
            cachedBuffer = cacheIt->second.buffer;
            cachedSampleRate = cacheIt->second.sampleRate;
        } else {
            auto pendingIt = this->pendingLoads.find(cacheKey);
            if (pendingIt != this->pendingLoads.end()) {
                pendingIt->second.push_back(std::move(callback));
                return;
            }
            this->pendingLoads.emplace(cacheKey, std::vector<LoadCallback> { std::move(callback) });
        }
    }
    if (cachedBuffer != nullptr) {
        juce::MessageManager::callAsync([cachedBuffer, cachedSampleRate, callback = std::move(callback)]() { callback(cachedBuffer, cachedSampleRate); });
        return;
    }

    this->threadPool.addJob(new LoadJob(*this, loadSpec, cacheKey, [this, cacheKey](BufferPtr buffer, double sampleRate) {
        std::vector<LoadCallback> callbacks;
        {
            std::lock_guard<std::mutex> lock(this->cacheMutex);
            auto pendingIt = this->pendingLoads.find(cacheKey);
            if (pendingIt != this->pendingLoads.end()) {
                callbacks = std::move(pendingIt->second);
                this->pendingLoads.erase(pendingIt);
            }
        }
        for (auto& pendingCallback : callbacks) pendingCallback(buffer, sampleRate);
    }), true);
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

    const double resolvedMaxReadLengthMs = this->loadSpec.recipe.getMaxReadLengthMs();
    const double resolvedFadeOutMs = this->loadSpec.recipe.getFadeOutMs();

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

    const float fixedGain = this->loadSpec.recipe.gainMilliDb != 0 ? juce::Decibels::decibelsToGain(this->loadSpec.recipe.getGainDb()) : 1.0f;
    const int fadeSamples = (int)(sampleRate * (resolvedFadeOutMs * 0.001));
    const int fadeStart = std::max(0, samplesToRead - fadeSamples);
    for (int channelIdx = 0; channelIdx < channels; ++channelIdx) {
        if (fixedGain != 1.0f && fadeStart > 0) buffer->applyGain(channelIdx, 0, fadeStart, fixedGain);
        buffer->applyGainRamp(channelIdx, fadeStart, samplesToRead - fadeStart, fixedGain, 0.0f);
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
