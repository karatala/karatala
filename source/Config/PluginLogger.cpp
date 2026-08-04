#include "PluginLogger.h"

#include <utility>

std::atomic<int> gAppLogLevel { static_cast<int>(DebugLevel::Off) };

static juce::String formatLogTimestamp(const juce::Time& timestamp) {
    return timestamp.formatted("%H:%M:%S") + "." + juce::String(timestamp.getMilliseconds()).paddedLeft('0', 3);
}

juce::String DebugLevelToString(DebugLevel level) {
    switch (level) {
        case DebugLevel::Off: return "OFF";
        case DebugLevel::Error: return "ERROR";
        case DebugLevel::Warning: return "WARNING";
        case DebugLevel::Notice: return "NOTICE";
        case DebugLevel::Info: return "INFO";
        case DebugLevel::Debug: return "DEBUG";
        case DebugLevel::Verbose: return "VERBOSE";
        default: return "UNKNOWN";
    }
}

juce::String DebugLevelToYaml(DebugLevel level) {
    switch (level) {
        case DebugLevel::Off: return "off";
        case DebugLevel::Error: return "error";
        case DebugLevel::Warning: return "warning";
        case DebugLevel::Notice: return "notice";
        case DebugLevel::Info: return "info";
        case DebugLevel::Debug: return "debug";
        case DebugLevel::Verbose: return "verbose";
        default: return "off";
    }
}

DebugLevel parseDebugLevelOrDefault(const juce::String& value, DebugLevel defaultLevel) {
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

bool isValidDebugLevelString(const juce::String& value) {
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

class GlobalLogger::WriterThread : public juce::Thread {
public:
    explicit WriterThread(GlobalLogger& logger)
        : juce::Thread("Karatala Log Writer"), owner(logger) {}

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

GlobalLogger& GlobalLogger::instance() {
    static GlobalLogger logger;
    return logger;
}

GlobalLogger::GlobalLogger() = default;

GlobalLogger::~GlobalLogger() {
    this->stopWriterThread();
}

void GlobalLogger::shutdown() {
    this->stopWriterThread();
    this->flushQueuedLines();
}

void GlobalLogger::setLogLevel(DebugLevel level) {
    this->logLevel.store(static_cast<int>(level), std::memory_order_relaxed);
    gAppLogLevel.store(static_cast<int>(level), std::memory_order_relaxed);
    this->updateWriterThreadState();
}

DebugLevel GlobalLogger::getLogLevel() const {
    return static_cast<DebugLevel>(this->logLevel.load(std::memory_order_relaxed));
}

void GlobalLogger::setWriteLogFilesEnabled(bool enabled) {
    this->writeLogFilesEnabled.store(enabled, std::memory_order_relaxed);
    this->updateWriterThreadState();
}

bool GlobalLogger::getWriteLogFilesEnabled() const {
    return this->writeLogFilesEnabled.load(std::memory_order_relaxed);
}

juce::File GlobalLogger::getCurrentLogFile() const {
    const std::lock_guard<std::mutex> lock(this->fileMutex);
    return this->logFile;
}

juce::File GlobalLogger::getLogsDirectory() const {
    const auto file = this->getCurrentLogFile();
    return file.getFullPathName().isNotEmpty() ? file.getParentDirectory() : juce::File();
}

juce::String GlobalLogger::getRecentImportantLines() const {
    const std::lock_guard<std::mutex> lock(this->recentLinesMutex);
    juce::String result;
    for (const auto& line : this->recentImportantLines) result += line + "\n";
    return result.trimEnd();
}

void GlobalLogger::flush() {
    this->flushQueuedLines();
}

juce::File GlobalLogger::createInstanceLogFile(const juce::File& dataPath) {
    auto logsDirectory = dataPath.getChildFile("logs");
    if (!logsDirectory.exists()) logsDirectory.createDirectory();

    const auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto baseName = "karatala-" + timestamp;
    auto candidate = logsDirectory.getChildFile(baseName + ".log");
    for (int duplicateIdx = 1; candidate.existsAsFile(); ++duplicateIdx) candidate = logsDirectory.getChildFile(baseName + "-" + juce::String(duplicateIdx) + ".log");
    candidate.create();
    return candidate;
}

void GlobalLogger::setLogFile(const juce::File& file) {
    {
        const std::lock_guard<std::mutex> lock(this->fileMutex);
        this->logFile = file;
        if (!this->logFile.getParentDirectory().exists()) this->logFile.getParentDirectory().createDirectory();
    }
    this->updateWriterThreadState();
}

void GlobalLogger::log(DebugLevel level, const juce::String& message) {
    if (this->logLevel.load(std::memory_order_relaxed) < static_cast<int>(level)) return;

    const juce::String line = DebugLevelToString(level) + ": " + message;

#if JUCE_DEBUG
    DBG(line);
#endif

    this->rememberImportantLine(line);
    this->enqueueLine(line);
}

void GlobalLogger::updateWriterThreadState() {
    if (this->getLogLevel() == DebugLevel::Off || !this->getWriteLogFilesEnabled()) this->stopWriterThread();
    else this->startWriterThread();
}

void GlobalLogger::startWriterThread() {
    const std::lock_guard<std::mutex> lock(this->threadMutex);
    if (this->writerThread != nullptr) return;
    this->writerThread = std::make_unique<WriterThread>(*this);
    this->writerThread->startThread();
}

void GlobalLogger::stopWriterThread() {
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

void GlobalLogger::rememberImportantLine(const juce::String& line) {
    if (!line.startsWith("ERROR:") && !line.startsWith("WARNING:") && !line.startsWith("NOTICE:")) return;
    const std::lock_guard<std::mutex> lock(this->recentLinesMutex);
    if (this->recentImportantLines.size() >= maxRecentImportantLines) this->recentImportantLines.pop_front();
    this->recentImportantLines.push_back(formatLogTimestamp(juce::Time::getCurrentTime()) + " " + line);
}

void GlobalLogger::enqueueLine(const juce::String& line) {
    if (!this->getWriteLogFilesEnabled()) return;
    std::unique_lock<std::mutex> lock(this->queueMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    if (this->pendingLines.size() >= maxQueuedLines) return;

    this->pendingLines.push_back(line);
    lock.unlock();
    this->queueEvent.signal();
}

void GlobalLogger::flushQueuedLines() {
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

    juce::String output;
    for (const auto& line : lines) output += formatLogTimestamp(juce::Time::getCurrentTime()) + " " + line + "\n";
    target.appendText(output);
}

void PluginLogger::add(const juce::String& message) {
    const juce::ScopedLock lock(this->mutex);
    this->messages.add(message);
}

juce::StringArray PluginLogger::getAll() const {
    const juce::ScopedLock lock(this->mutex);
    return this->messages;
}

void PluginLogger::clear() {
    const juce::ScopedLock lock(this->mutex);
    this->messages.clear();
}
