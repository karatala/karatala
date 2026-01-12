#pragma once

#include <juce_core/juce_core.h>

static inline juce::String stringToTitleCase(const juce::String& str) {
    return str;
}

static inline juce::String stringsJoinAndSnakeCase(const juce::StringArray& strings) {
    return strings.joinIntoString("_").toLowerCase().replace(" ", "_").replace("-", "_").replace(".", "_").replace(",", "_");
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
        return result.returnWithError("E: Invalid env: '" + juce::String(envName) + "'");

    juce::File f(envPath);
    if (!f.isDirectory())
        return result.returnWithError("E: Path is not directory in env: '" + juce::String(envName) + "'");

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