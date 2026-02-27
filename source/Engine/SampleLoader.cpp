#include "SampleLoader.h"
#include <juce_events/juce_events.h>

void SampleLoader::requestAsyncLoad(const juce::File& file, LoadCallback callback)
{
    if (!file.existsAsFile())
    {
        juce::MessageManager::callAsync([callback]()
        {
            callback(nullptr, 0.0);
        });
        return;
    }

    const auto cacheKey = makeCacheKey(file);

    // Check cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex);

        auto it = cache.find(cacheKey);
        if (it != cache.end())
        {
            auto buf = it->second;

            juce::MessageManager::callAsync([buf, callback]()
            {
                callback(buf, 0.0); // SR not cached separately (safe assumption)
            });

            return;
        }
    }

    threadPool.addJob(new LoadJob(*this, file, cacheKey, callback), true);
}


juce::ThreadPoolJob::JobStatus SampleLoader::LoadJob::runJob() {
    std::unique_ptr<juce::InputStream> in(file.createInputStream());
    if (!in) {
        juce::MessageManager::callAsync([cb = callback]() {
            cb(nullptr, 0.0);
        });
        return jobHasFinished;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(
        loader.formatManager.createReaderFor(std::move(in)));

    if (!reader) {
        juce::MessageManager::callAsync([cb = callback]() {
            cb(nullptr, 0.0);
        });
        return jobHasFinished;
    }

    const int channels = (int)reader->numChannels;
    const double sampleRate = reader->sampleRate;

    // Clamp length
    const int maxSamples = (int)(sampleRate * (loader.maxSampleLengthMs * 0.001));

    const int samplesToRead = (int)std::min<int64_t>(reader->lengthInSamples, maxSamples);

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(channels, samplesToRead);

    if (!reader->read(buffer.get(), 0, samplesToRead, 0, true, true)) {
        juce::MessageManager::callAsync([cb = callback]() {
            cb(nullptr, 0.0);
        });
        return jobHasFinished;
    }

    // Apply fade out
    const int fadeSamples = (int)(sampleRate * (loader.fadeOutMs * 0.001));

    const int fadeStart = std::max(0, samplesToRead - fadeSamples);

    for (int ch = 0; ch < channels; ++ch) {
        buffer->applyGainRamp(ch, fadeStart, samplesToRead - fadeStart, 1.0f, 0.0f);
    }

    // Cache insert
    {
        std::lock_guard<std::mutex> lock(loader.cacheMutex);

        const size_t bytes = (size_t)channels * (size_t)samplesToRead * sizeof(float);

        if (loader.currentCacheBytes + bytes <= loader.cacheLimitBytes) {
            loader.cache[cacheKey] = buffer;
            loader.currentCacheBytes += bytes;
        }
    }

    juce::MessageManager::callAsync([buf = buffer, sr = sampleRate, cb = callback]() {
        cb(buf, sr);
    });

    return jobHasFinished;
}