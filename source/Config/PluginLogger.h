#pragma once

#include "LogLevel.h"

#include <atomic>
#include <cstddef>
#include <deque>
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>

#ifndef JUCE_DEBUG
#define JUCE_DEBUG 0
#endif

extern std::atomic<int> gAppLogLevel;

class GlobalLogger {
public:
    static GlobalLogger& instance();
    ~GlobalLogger();

    void shutdown();
    void setLogLevel(DebugLevel level);
    DebugLevel getLogLevel() const;
    void setWriteLogFilesEnabled(bool enabled);
    bool getWriteLogFilesEnabled() const;
    juce::File getCurrentLogFile() const;
    juce::File getLogsDirectory() const;
    juce::String getRecentImportantLines() const;
    void flush();
    juce::File createInstanceLogFile(const juce::File& dataPath);
    void setLogFile(const juce::File& file);
    void log(DebugLevel level, const juce::String& message);

private:
    class WriterThread;

    GlobalLogger();

    void updateWriterThreadState();
    void startWriterThread();
    void stopWriterThread();
    void rememberImportantLine(const juce::String& line);
    void enqueueLine(const juce::String& line);
    void flushQueuedLines();

    static constexpr std::size_t maxQueuedLines = 4096;
    static constexpr std::size_t maxRecentImportantLines = 500;

    std::atomic<int> logLevel { static_cast<int>(DebugLevel::Off) };
    std::atomic<bool> writeLogFilesEnabled { true };
    juce::File logFile;
    std::deque<juce::String> pendingLines;
    std::deque<juce::String> recentImportantLines;
    std::mutex queueMutex;
    mutable std::mutex recentLinesMutex;
    mutable std::mutex fileMutex;
    std::mutex threadMutex;
    juce::WaitableEvent queueEvent;
    std::unique_ptr<WriterThread> writerThread;
};

class PluginLogger {
public:
    void add(const juce::String& message);
    juce::StringArray getAll() const;
    void clear();

private:
    juce::StringArray messages;
    mutable juce::CriticalSection mutex;
};

#ifndef LOG
#define LOG(level, message) \
    do { if (gAppLogLevel.load(std::memory_order_relaxed) >= static_cast<int>(level)) \
        GlobalLogger::instance().log((level), (message)); } while (0)

#define LOG_ENABLED(level) (gAppLogLevel.load(std::memory_order_relaxed) >= static_cast<int>(DebugLevel::level))
#endif
