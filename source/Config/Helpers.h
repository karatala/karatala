#pragma once

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
        result.add (text);
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

struct WrappedValueResult {
    juce::String cleaned;
    juce::String wrapped;
};

static inline WrappedValueResult getWrappedValue(const juce::String& input, const juce::String& leftWrapper = "[", const juce::String& rightWrapper = "]") {
    WrappedValueResult result;
    juce::String processed = input.trim();

    int lIdx = processed.indexOf(leftWrapper);
    int rIdx = processed.indexOf(rightWrapper);

    if (lIdx >= 0 && rIdx > lIdx) {
        result.wrapped = processed.substring(lIdx + leftWrapper.length(),rIdx).trim();
        processed = (processed.substring(0, lIdx) + processed.substring(rIdx + rightWrapper.length())).trim();
    }
    result.cleaned = processed;
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

class StringIdRegistrySingleton {
public:
    using Id = uint32_t;
    using Name = juce::String;

    static StringIdRegistrySingleton& instance() {
        static StringIdRegistrySingleton inst;
        return inst;
    }

    static Id getId(const Name& s) { return instance().get(s); }

    Id get(const Name& s) {
        auto it = map.find(s);
        if (it != map.end()) return it->second;

        Id id = nextId++;
        map.emplace(Name(s), id);
        return id;
    }

private:
    StringIdRegistrySingleton() : nextId(1) {}
    std::unordered_map<Name, Id> map;
    Id nextId;
};

class GlobalLogger {
public:
    static GlobalLogger& instance() {
        static GlobalLogger inst;
        return inst;
    }

    void setLogLevel(DebugLevel level) {
        const juce::ScopedLock lock(mutex);
        logLevel = level;
    }

    DebugLevel getLogLevel() const {
        return logLevel;
    }

    void setLogFile(const juce::File& f) {
        const juce::ScopedLock lock(mutex);
        logFile = f;
        if (!logFile.getParentDirectory().exists())
            logFile.getParentDirectory().createDirectory();
    }

    void log(DebugLevel level, const juce::String& msg) {
        // Follow same semantics as other code: only log when logLevel >= level
        if (logLevel < level) return;

        juce::String line = DebugLevelToString(level) + ": " + msg;
        DBG(line);

        const juce::ScopedLock lock(mutex);
        if (logFile.getFullPathName().isNotEmpty()) {
            if (!logFile.getParentDirectory().exists())
                logFile.getParentDirectory().createDirectory();
            juce::String out = juce::Time::getCurrentTime().toISO8601(true) + " " + line + "\n";
            logFile.appendText(out);
        }
    }

private:
    GlobalLogger() {
        // Default log file under user's Documents/Karatala/karatala.log
        juce::File doc = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        juce::File dir = doc.getChildFile("Karatala");
        if (!dir.exists()) dir.createDirectory();
        logFile = dir.getChildFile("karatala.log");
    }

    juce::File logFile;
    mutable juce::CriticalSection mutex;
    DebugLevel logLevel = DebugLevel::Debug;
};

#ifndef LOG
#define LOG(level, msg) GlobalLogger::instance().log(level, msg)
#endif