#pragma once

#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>
#include <unordered_map>

// Simple async sample loader with small in-memory cache.
// Not a production-grade LRU cache — keeps things simple for now.
class SampleLoader {
public:
    using LoadCallback = std::function<void(std::shared_ptr<juce::AudioBuffer<float>> audioBuffer, double sampleRate)>;

    SampleLoader();
    ~SampleLoader();

    // Request asynchronous load. If sample is cached, callback invoked synchronously on message thread.
    void requestAsyncLoad(const juce::File& file, LoadCallback callback);

    // Set rough cache size limit in bytes (not strict); default 512MB
    void setCacheLimitBytes(size_t bytes) {
        cacheLimitBytes = bytes;
    }

private:
    juce::AudioFormatManager formatManager;

    std::mutex cacheMutex;
    std::unordered_map<juce::String, std::shared_ptr<juce::AudioBuffer<float>>> cache;
    size_t currentCacheBytes = 0;
    size_t cacheLimitBytes = 512ull * 1024ull * 1024ull; // 512 MB
};
