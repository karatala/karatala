#include "SampleLoader.h"
#include <juce_gui_basics/juce_gui_basics.h>

#include <thread>

SampleLoader::SampleLoader() {
    formatManager.registerBasicFormats();
}

SampleLoader::~SampleLoader() {
}

void SampleLoader::requestAsyncLoad(const File& file, LoadCallback callback) { //FIXME: review wtf happens here
    if (!file.existsAsFile()) {
        MessageManager::callAsync([file, callback]() {
            callback(nullptr, 0.0);
        });
        return;
    }

    const auto path = file.getFullPathName().toStdString();

    // Check cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cache.find(path);
        if (it != cache.end()) {
            auto buf = it->second;
            MessageManager::callAsync([file, buf, this, callback]() {
                callback(buf, 0.0);
            });
            return;
        }
    }

    // Spawn a background thread to decode
    std::thread([this, file, path, callback]() {
        std::unique_ptr<InputStream> in(file.createInputStream());
        if (!in) {
            MessageManager::callAsync([file, callback]() {
                callback(nullptr, 0.0);
            });
            return;
        }

        std::unique_ptr<AudioFormatReader> reader(formatManager.createReaderFor(std::move(in)));
        if (!reader) {
            MessageManager::callAsync([file, callback]() {
                callback(nullptr, 0.0);
            });
            return;
        }

        int channels = jmin(2, (int) reader->numChannels);
        int samples = (int) reader->lengthInSamples;

        auto buffer = std::make_shared<AudioBuffer<float>>(channels, samples);
        reader->read(buffer.get(), 0, samples, 0, true, true);
        double sr = reader->sampleRate;

        // insert into cache
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            size_t bytes = (size_t) channels * (size_t) samples * sizeof(float);
            if (currentCacheBytes + bytes <= cacheLimitBytes) {
                cache[path] = buffer;
                currentCacheBytes += bytes;
            } else {
                // simplistic behavior: do not cache if over limit
            }
        }

        MessageManager::callAsync([file, buffer, sr, callback]() {
            callback(buffer, sr);
        });
    }).detach();
}
