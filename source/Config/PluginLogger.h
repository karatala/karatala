#pragma once
#include <array>
#include <atomic>
#include <juce_core/juce_core.h>

class PluginLogger {
public:
    void add(const juce::String& msg) {
        const juce::ScopedLock lock(mutex);
        messages.add(msg);
    }

    juce::StringArray getAll() const {
        const juce::ScopedLock lock(mutex);
        return messages;
    }

    void clear() {
        const juce::ScopedLock lock(mutex);
        messages.clear();
    }

private:
    juce::StringArray messages;
    mutable juce::CriticalSection mutex;
};
