#pragma once
#include <atomic>
#include <deque>
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>

#ifndef JUCE_DEBUG
#define JUCE_DEBUG 0
#endif

enum DebugLevel {
    Off     = -1,
    Error   = 0,
    Warning = 1,
    Notice  = 2,
    Info    = 3,
    Debug   = 4,
    Verbose = 5
};

// Fast global log-level variable: checked by LOG before evaluating messages.
inline std::atomic<int> gAppLogLevel { static_cast<int>(DebugLevel::Off) };

static inline juce::String DebugLevelToString(DebugLevel level) {
    switch (level) {
        case DebugLevel::Off:     return "OFF";
        case DebugLevel::Error:   return "ERROR";
        case DebugLevel::Warning: return "WARNING";
        case DebugLevel::Notice:  return "NOTICE";
        case DebugLevel::Info:    return "INFO";
        case DebugLevel::Debug:   return "DEBUG";
        case DebugLevel::Verbose: return "VERBOSE";
        default:                  return "UNKNOWN";
    }
}

static inline juce::String DebugLevelToYaml(DebugLevel level) {
    switch (level) {
        case DebugLevel::Off:     return "off";
        case DebugLevel::Error:   return "error";
        case DebugLevel::Warning: return "warning";
        case DebugLevel::Notice:  return "notice";
        case DebugLevel::Info:    return "info";
        case DebugLevel::Debug:   return "debug";
        case DebugLevel::Verbose: return "verbose";
        default:                  return "off";
    }
}

static inline DebugLevel parseDebugLevelOrDefault(const juce::String& value, DebugLevel defaultLevel = DebugLevel::Off) {
    const auto normalized = value.trim().toLowerCase();
    if (normalized.isEmpty()) return defaultLevel;
    if (normalized == "off") return DebugLevel::Off;
    if (normalized == "error") return DebugLevel::Error;
    if (normalized == "warning" || normalized == "warn") return DebugLevel::Warning;
    if (normalized == "notice") return DebugLevel::Notice;
    if (normalized == "info") return DebugLevel::Info;
    if (normalized == "debug") return DebugLevel::Debug;
    if (normalized == "verbose") return DebugLevel::Verbose;
    return defaultLevel;
}

static inline bool isValidDebugLevelString(const juce::String& value) {
    const auto normalized = value.trim().toLowerCase();
    return normalized == "off"
        || normalized == "error"
        || normalized == "warning"
        || normalized == "warn"
        || normalized == "notice"
        || normalized == "info"
        || normalized == "debug"
        || normalized == "verbose";
}

class GlobalLogger {
public:
    static GlobalLogger& instance() {
        static GlobalLogger inst;
        return inst;
    }

    ~GlobalLogger() {
        this->stopWriterThread();
    }

    void shutdown() {
        this->stopWriterThread();
        this->flushQueuedLines();
    }

    void setLogLevel(DebugLevel level) {
        this->logLevel.store(static_cast<int>(level), std::memory_order_relaxed);
        gAppLogLevel.store(static_cast<int>(level), std::memory_order_relaxed);
        this->updateWriterThreadState();
    }

    DebugLevel getLogLevel() const {
        return static_cast<DebugLevel>(this->logLevel.load(std::memory_order_relaxed));
    }

    void setWriteLogFilesEnabled(bool enabled) {
        this->writeLogFilesEnabled.store(enabled, std::memory_order_relaxed);
        this->updateWriterThreadState();
    }

    bool getWriteLogFilesEnabled() const {
        return this->writeLogFilesEnabled.load(std::memory_order_relaxed);
    }

    juce::File getCurrentLogFile() const {
        const std::lock_guard<std::mutex> lock(this->fileMutex);
        return this->logFile;
    }

    juce::File getLogsDirectory() const {
        const auto file = this->getCurrentLogFile();
        return file.getFullPathName().isNotEmpty() ? file.getParentDirectory() : juce::File();
    }

    juce::String getRecentImportantLines() const {
        const std::lock_guard<std::mutex> lock(this->recentLinesMutex);
        juce::String result;
        for (const auto& line : this->recentImportantLines)
            result += line + "\n";
        return result.trimEnd();
    }

    void flush() {
        this->flushQueuedLines();
    }

    juce::File createInstanceLogFile(const juce::File& dataPath) {
        auto logsDir = dataPath.getChildFile("logs");
        if (!logsDir.exists()) logsDir.createDirectory();

        const auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
        const auto baseName = "karatala-" + timestamp;
        auto candidate = logsDir.getChildFile(baseName + ".log");
        for (int duplicateIndex = 1; candidate.existsAsFile(); ++duplicateIndex) candidate = logsDir.getChildFile(baseName + "-" + juce::String(duplicateIndex) + ".log");
        candidate.create();
        return candidate;
    }

    void setLogFile(const juce::File& file) {
        {
            const std::lock_guard<std::mutex> lock(this->fileMutex);
            this->logFile = file;
            if (!this->logFile.getParentDirectory().exists()) this->logFile.getParentDirectory().createDirectory();
        }
        this->updateWriterThreadState();
    }

    void log(DebugLevel level, const juce::String& msg) {
        if (this->logLevel.load(std::memory_order_relaxed) < static_cast<int>(level)) return;

        const juce::String line = DebugLevelToString(level) + ": " + msg;

      #if JUCE_DEBUG
        DBG(line);
      #endif

        this->rememberImportantLine(line);
        this->enqueueLine(line);
    }

private:
    class WriterThread : public juce::Thread {
    public:
        explicit WriterThread(GlobalLogger& logger): juce::Thread("Karatala Log Writer"), owner(logger) {}

        void run() override {
            while (!this->threadShouldExit()) {
                this->owner.queueEvent.wait(250);
                if (this->threadShouldExit()) break;
                this->owner.flushQueuedLines();
            }
        }

    private:
        GlobalLogger& owner;
    };

    GlobalLogger() = default;

    void updateWriterThreadState() {
        if (this->getLogLevel() == DebugLevel::Off || !this->getWriteLogFilesEnabled()) this->stopWriterThread();
        else this->startWriterThread();
    }

    void startWriterThread() {
        const std::lock_guard<std::mutex> lock(this->threadMutex);
        if (this->writerThread != nullptr) return;
        this->writerThread = std::make_unique<WriterThread>(*this);
        this->writerThread->startThread();
    }

    void stopWriterThread() {
        std::unique_ptr<WriterThread> thread;
        {
            const std::lock_guard<std::mutex> lock(this->threadMutex);
            thread = std::move(this->writerThread);
        }

        if (thread == nullptr) return;

        thread->signalThreadShouldExit();
        thread->notify();
        this->queueEvent.signal();
        thread->waitForThreadToExit(1000);
    }

    void rememberImportantLine(const juce::String& line) {
        if (!line.startsWith("ERROR:") && !line.startsWith("WARNING:") && !line.startsWith("NOTICE:")) return;
        const std::lock_guard<std::mutex> lock(this->recentLinesMutex);
        if (this->recentImportantLines.size() >= maxRecentImportantLines)
            this->recentImportantLines.pop_front();
        this->recentImportantLines.push_back(juce::Time::getCurrentTime().toISO8601(true) + " " + line);
    }

    void enqueueLine(const juce::String& line) {
        if (!this->getWriteLogFilesEnabled()) return;
        std::unique_lock<std::mutex> lock(this->queueMutex, std::try_to_lock);
        if (!lock.owns_lock()) return;
        if (this->pendingLines.size() >= maxQueuedLines) return;

        this->pendingLines.push_back(line);
        lock.unlock();
        this->queueEvent.signal();
    }

    void flushQueuedLines() {
        std::deque<juce::String> lines;
        {
            const std::lock_guard<std::mutex> lock(this->queueMutex);
            lines.swap(this->pendingLines);
        }
        if (lines.empty()) return;

        juce::File target;
        {
            const std::lock_guard<std::mutex> lock(this->fileMutex);
            target = this->logFile;
        }
        if (target.getFullPathName().isEmpty() || !this->getWriteLogFilesEnabled()) return;

        juce::String out;
        for (const auto& line : lines)
            out += juce::Time::getCurrentTime().toISO8601(true) + " " + line + "\n";
        target.appendText(out);
    }

    static constexpr size_t maxQueuedLines = 4096;
    static constexpr size_t maxRecentImportantLines = 500;

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

#ifndef LOG
// The if-guard ensures msg is never evaluated when its level is disabled.
#define LOG(level, msg) \
    do { if (gAppLogLevel.load(std::memory_order_relaxed) >= static_cast<int>(level)) \
        GlobalLogger::instance().log((level), (msg)); } while (0)

// Use LOG_ENABLED(level) to gate expensive prep work before a LOG call.
#define LOG_ENABLED(level) (gAppLogLevel.load(std::memory_order_relaxed) >= static_cast<int>(DebugLevel::level))
#endif
