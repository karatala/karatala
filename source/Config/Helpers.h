#pragma once

#include <atomic>
#include <juce_core/juce_core.h>

const int MIDI_MIN_VALUE = 0;
const int MIDI_MAX_VALUE = 127;

static inline bool isValidMidiValue(int value) {
    return value >= MIDI_MIN_VALUE && value <= MIDI_MAX_VALUE;
}

template <typename T>
static inline bool vectorContains(const std::vector<T>& vec, T value) {
    for (T x : vec) if (x == value) return true;
    return false;
}

static inline bool containsSameValue(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::array<bool, 256> existing{};
    for (uint8_t x : a) existing[x] = true;
    for (uint8_t x : b) if (existing[x]) return true;
    return false;
}

static inline juce::StringArray splitByString(const juce::String& text, const juce::String& delimiter, bool keepEmptyTokens = false) {
    juce::StringArray result;
    if (delimiter.isEmpty()) {
        result.add(text);
        return result;
    }
    int start = 0;
    while (true) {
        int index = text.indexOf (start, delimiter);
        if (index < 0) break;
        auto token = text.substring (start, index);
        if (keepEmptyTokens || token.isNotEmpty())
            result.add (token);
        start = index + delimiter.length();
    }
    auto tail = text.substring (start);
    if (keepEmptyTokens || tail.isNotEmpty())
        result.add (tail);

    return result;
}

static inline juce::String midiVectorToString(const std::vector<uint8_t>& v) {
    juce::String s = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) s += ", ";
        s += juce::String((int)v[i]);
    }
    s += "]";
    return s;
}

template <typename T>
struct ParseResult {
    T value;
    bool isValid = true;
    std::vector<juce::String> errors;

    ParseResult& returnWithError(const juce::String& msg) {
        errors.push_back(msg);
        isValid = false;
        return *this;
    }
    void addWarning(const juce::String& msg) {
        errors.push_back(msg);
    }

    bool ok() const noexcept {
        return isValid;
    }

    juce::String error(const juce::String& separator = "\n") const {
        juce::String out;
        for (int i = 0; i < errors.size(); ++i) {
            out += errors[i];
            if (i + 1 < errors.size())
                out += separator;
        }
        return out;
    }
};

static inline ParseResult<juce::File> getEnvPath(const char* envName) {
#pragma warning(push)
#pragma warning(disable : 4996)
    const char* envPath = std::getenv(envName);
#pragma warning(pop)
    ParseResult<juce::File> result;

    if (envPath == nullptr)
        return result.returnWithError("Invalid env: '" + juce::String(envName) + "'");

    juce::File f(envPath);
    if (!f.isDirectory())
        return result.returnWithError("Path is not directory in env: '" + juce::String(envName) + "'");

    result.value = f;
    return result;
}

static inline juce::File resolvePathRelOrAbs(const juce::File& base, const juce::String& path) {
    if (path.isEmpty()) 
        return juce::File();
    if (juce::File::isAbsolutePath(path))
        return juce::File(path);
    return base.getChildFile(path);
}

struct WrappedSegment {
    juce::String prefix;
    juce::String wrapped;
    juce::String suffix;
};

static inline WrappedSegment extractWrappedSegment(const juce::String& input, const juce::String& leftWrapper, const juce::String& rightWrapper) {
    WrappedSegment result;
    juce::String processed = input.trim();

    const int lIdx = processed.indexOf(leftWrapper);
    const int rIdx = processed.indexOf(rightWrapper);

    if (lIdx >= 0 && rIdx > lIdx) {
        result.prefix  = processed.substring(0, lIdx).trim();
        result.wrapped = processed.substring(lIdx + leftWrapper.length(), rIdx).trim();
        result.suffix  = processed.substring(rIdx + rightWrapper.length()).trim();
    } else {
        result.prefix = processed;
    }

    return result;
}

enum DebugLevel {
    Error   = 0,
    Warning = 1,
    Notice  = 2,
    Info    = 3,
    Debug   = 4,
    Verbose = 5
};

// Fast global log-level variable: checked by the LOG macro before evaluating
// any message expression (string building etc.) — single atomic load, no lock.
inline std::atomic<int> gAppLogLevel { static_cast<int>(DebugLevel::Debug) };

static inline const juce::String DebugLevelToString(DebugLevel level) {
    switch (level) {
        case DebugLevel::Error:   return "ERROR";
        case DebugLevel::Warning: return "WARNING";
        case DebugLevel::Notice:  return "NOTICE";
        case DebugLevel::Info:    return "INFO";
        case DebugLevel::Debug:   return "DEBUG";
        case DebugLevel::Verbose: return "VERBOSE";
        default:                  return "UNKNOWN";
    }
}

class StringsDictSingleton {
public:
    using Id   = std::uint32_t;
    using Name = juce::String;
    
    static const Id EMPTY = 0;

    static StringsDictSingleton& instance() noexcept {
        static StringsDictSingleton inst;
        return inst;
    }

    static Id getId(const Name& name) {
        if (name == "") return StringsDictSingleton::EMPTY;
        return instance().findOrCreateId(name);
    }
    static Name nameOf(Id id) { return instance().findName(id); }

    Id findOrCreateId(const Name& name) {
        std::lock_guard<std::mutex> lock(this->mutex);
        auto [it, inserted] = this->nameIdMap.try_emplace(name, nextId);
        if (inserted) ++nextId;
        return it->second;
    }
    Name findName(Id id) const {
        if (id == StringsDictSingleton::EMPTY) return "";
        for (const auto& [name, nameId] : this->nameIdMap)
            if (nameId == id) return name;
        return "";
    }

private:
    std::unordered_map<Name, Id> nameIdMap;
    Id nextId { 1 };
    std::mutex mutex;

    StringsDictSingleton() { this->nameIdMap.reserve(1024); }
    StringsDictSingleton(const StringsDictSingleton&) = delete;
    StringsDictSingleton& operator=(const StringsDictSingleton&) = delete;
};


class GlobalLogger {
public:
    static GlobalLogger& instance() {
        static GlobalLogger inst;
        return inst;
    }

    void setLogLevel(DebugLevel level) {
        const juce::ScopedLock lock(mutex);
        this->logLevel = level;
        gAppLogLevel.store(static_cast<int>(level), std::memory_order_relaxed);
    }

    DebugLevel getLogLevel() const {
        return this->logLevel;
    }

    void setLogFile(const juce::File& f) {
        const juce::ScopedLock lock(mutex);
        this->logFile = f;
        if (!this->logFile.getParentDirectory().exists())
            this->logFile.getParentDirectory().createDirectory();
    }

    void log(DebugLevel level, const juce::String& msg) {
        // Follow same semantics as other code: only log when logLevel >= level
        if (this->logLevel < level) return;

        juce::String line = DebugLevelToString(level) + ": " + msg;
        DBG(line);

        // const juce::ScopedLock lock(mutex);
        // if (this->logFile.getFullPathName().isNotEmpty()) {
        //     if (!this->logFile.getParentDirectory().exists())
        //         this->logFile.getParentDirectory().createDirectory();
        //     juce::String out = juce::Time::getCurrentTime().toISO8601(true) + " " + line + "\n";
        //     this->logFile.appendText(out);
        // }
    }

private:
    GlobalLogger() {
        // Default log file under user's Documents/Karatala/karatala.log
        juce::File doc = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        juce::File dir = doc.getChildFile("Karatala");
        if (!dir.exists()) dir.createDirectory();
        this->logFile = dir.getChildFile("karatala.log");
    }

    juce::File logFile;
    mutable juce::CriticalSection mutex;
    DebugLevel logLevel = DebugLevel::Debug;
};

#ifndef LOG
// The if-guard ensures msg (string building, formatted(), etc.) is never
// evaluated when the current log level is below the requested level.
// gAppLogLevel is a cheap atomic read — essentially one instruction.
#define LOG(level, msg) \
    do { if (gAppLogLevel.load(std::memory_order_relaxed) >= static_cast<int>(level)) \
        GlobalLogger::instance().log((level), (msg)); } while (0)

// Use LOG_ENABLED(level) to gate expensive prep work before a LOG call:
//   if (LOG_ENABLED(Debug)) { auto s = buildExpensiveString(); LOG(Debug, s); }
#define LOG_ENABLED(level) (gAppLogLevel.load(std::memory_order_relaxed) >= static_cast<int>(DebugLevel::level))
#endif