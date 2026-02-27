#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <unordered_map>
#include <mutex>

class SampleLoader {
public:
    using BufferPtr = std::shared_ptr<juce::AudioBuffer<float>>;
    using LoadCallback = std::function<void(BufferPtr, double)>;

    SampleLoader() {
        formatManager.registerBasicFormats();
    }
    ~SampleLoader() {
        threadPool.removeAllJobs(true, 5000);
    }

    void requestAsyncLoad(const juce::File& file, LoadCallback callback);

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
        LoadJob(SampleLoader& owner, juce::File file, juce::String cacheKey, LoadCallback callback)
            : ThreadPoolJob("Sample Load Job")
            , loader(owner)
            , file(std::move(file))
            , cacheKey(std::move(cacheKey))
            , callback(std::move(callback))
        {}
        JobStatus runJob() override;

    private:
        SampleLoader& loader;
        juce::File file;
        juce::String cacheKey;
        LoadCallback callback;
    };

    friend class LoadJob;

    //========================================================
    juce::AudioFormatManager formatManager;
    juce::ThreadPool threadPool;

    std::mutex cacheMutex;
    std::unordered_map<juce::String, BufferPtr> cache;

    size_t currentCacheBytes = 0;
    size_t cacheLimitBytes;

    double maxSampleLengthMs = 5000.0;
    double fadeOutMs = 2000.0;

    juce::String makeCacheKey(const juce::File& file) const {
        return file.getFullPathName() + "_" + juce::String(this->maxSampleLengthMs) + "_" + juce::String(this->fadeOutMs);
    }
};
