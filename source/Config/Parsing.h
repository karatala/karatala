#pragma once

#include <juce_core/juce_core.h>
#include <cstddef>
#include <optional>
#include <vector>

template <typename ValueType>
struct ParseResult {
    ValueType value;
    bool isValid = true;
    std::vector<juce::String> errors;

    ParseResult& returnWithError(const juce::String& message) {
        this->errors.push_back(message);
        this->isValid = false;
        return *this;
    }

    void addWarning(const juce::String& message) {
        this->errors.push_back(message);
    }

    bool ok() const noexcept {
        return this->isValid;
    }

    juce::String error(const juce::String& separator = "\n") const {
        juce::String output;
        for (std::size_t errorIdx = 0; errorIdx < this->errors.size(); ++errorIdx) {
            output += this->errors[errorIdx];
            if (errorIdx + 1 < this->errors.size()) output += separator;
        }
        return output;
    }
};

namespace Parsing {

struct WrappedSegment {
    juce::String prefix;
    juce::String wrapped;
    juce::String suffix;
};

std::optional<float> parseFiniteFloat(const juce::String& text);

inline WrappedSegment extractWrappedSegment(const juce::String& input, const juce::String& leftWrapper, const juce::String& rightWrapper) {
    WrappedSegment result;
    const juce::String processed = input.trim();
    const int leftIdx = processed.indexOf(leftWrapper);
    const int rightIdx = processed.indexOf(rightWrapper);

    if (leftIdx >= 0 && rightIdx > leftIdx) {
        result.prefix = processed.substring(0, leftIdx).trim();
        result.wrapped = processed.substring(leftIdx + leftWrapper.length(), rightIdx).trim();
        result.suffix = processed.substring(rightIdx + rightWrapper.length()).trim();
    } else {
        result.prefix = processed;
    }

    return result;
}

} // namespace Parsing
