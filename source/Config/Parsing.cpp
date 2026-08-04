#include "Parsing.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace Parsing {

std::optional<float> parseFiniteFloat(const juce::String& text) {
    const auto trimmed = text.trim();
    if (trimmed.isEmpty()) return std::nullopt;
    const auto utf8Text = trimmed.toRawUTF8();
    char* endPtr = nullptr;
    errno = 0;
    const float value = std::strtof(utf8Text, &endPtr);
    if (errno != 0 || endPtr == utf8Text || *endPtr != '\0' || !std::isfinite(value)) return std::nullopt;
    return value;
}

} // namespace Parsing
