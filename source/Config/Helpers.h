#pragma once

#include <juce_core/juce_core.h>
#include "PluginLogger.h"

const int MIDI_MIN_VALUE = 0;
const int MIDI_MAX_VALUE = 127;

static inline bool isValidMidiValue(int value) {
    return value >= MIDI_MIN_VALUE && value <= MIDI_MAX_VALUE;
}

template <typename T>
static inline bool vectorContains(const std::vector<T>& vec, T value) { return std::find(vec.begin(), vec.end(), value) != vec.end(); }

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
        for (size_t errorIdx = 0; errorIdx < errors.size(); ++errorIdx) {
            out += errors[errorIdx];
            if (errorIdx + 1 < errors.size())
                out += separator;
        }
        return out;
    }
};

static inline ParseResult<juce::File> getEnvPath(const char* envName) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* envPath = std::getenv(envName);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
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
        const auto existingIt = this->nameIdMap.find(name);
        if (existingIt != this->nameIdMap.end()) return existingIt->second;

        const auto nameId = static_cast<Id>(this->namesById.size());
        this->nameIdMap.emplace(name, nameId);
        this->namesById.push_back(name);
        return nameId;
    }
    Name findName(Id id) const {
        std::lock_guard<std::mutex> lock(this->mutex);
        const auto nameIdx = static_cast<size_t>(id);
        if (id == StringsDictSingleton::EMPTY || nameIdx >= this->namesById.size()) return "";
        return this->namesById[nameIdx];
    }

private:
    std::unordered_map<Name, Id> nameIdMap;
    std::vector<Name> namesById;
    mutable std::mutex mutex;

    StringsDictSingleton() {
        this->nameIdMap.reserve(1024);
        this->namesById.reserve(1024);
        this->namesById.push_back("");
    }
    StringsDictSingleton(const StringsDictSingleton&) = delete;
    StringsDictSingleton& operator=(const StringsDictSingleton&) = delete;
};
