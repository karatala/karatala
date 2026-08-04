#include "KitLibModel.h"
#include "InstrumentMap/InstrumentMap.h"
#include "../Config/PluginLogger.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace KitLib;

int DecayEnvelope::base62ToInt(const juce::String& s) {
    if (s.isEmpty())
        return -1;

    int result = 0;
    for (int i = 0; i < s.length(); ++i) {
        const juce::juce_wchar c = s[i];
        int value = -1;
        if (c >= '0' && c <= '9') value = static_cast<int>(c - '0');
        else if (c >= 'a' && c <= 'z') value = 10 + static_cast<int>(c - 'a');
        else if (c >= 'A' && c <= 'Z') value = 36 + static_cast<int>(c - 'A');

        if (value < 0)
            return -1;

        result = result * 62 + value;
    }
    return result;
}

DecayEnvelope DecayEnvelope::decode(const juce::String& decayCode, float peakDb, float peakMs) {
    DecayEnvelope env;
    env.peakDb = peakDb;
    env.peakMs = peakMs;

    if (decayCode.isEmpty())
        return env;

    auto parts = juce::StringArray::fromTokens(decayCode, "_", "");
    parts.removeEmptyStrings();

    float absoluteMs = 0.0f;
    for (int i = 0; i < parts.size() && i < NUM_POINTS; ++i) {
        const int deltaMs = base62ToInt(parts[i]);
        if (deltaMs < 0)
            break;

        if (i == 0)
            absoluteMs = static_cast<float>(deltaMs);
        else
            absoluteMs += static_cast<float>(deltaMs);

        env.points[static_cast<size_t>(i)] = absoluteMs;
        env.validPoints = i + 1;
    }

    return env;
}

DecayEnvelope DecayEnvelope::fromPoints(float peakDb, float peakMs, const std::array<std::optional<float>, NUM_POINTS>& pointsMs) {
    DecayEnvelope envelope;
    envelope.peakDb = peakDb;
    envelope.peakMs = peakMs;
    float previousMs = peakMs;
    for (int pointIdx = 0; pointIdx < NUM_POINTS; ++pointIdx) {
        const auto pointMs = pointsMs[static_cast<size_t>(pointIdx)];
        if (!pointMs.has_value() || *pointMs < previousMs) break;
        envelope.points[static_cast<size_t>(pointIdx)] = *pointMs;
        envelope.validPoints = pointIdx + 1;
        previousMs = *pointMs;
    }
    return envelope;
}

float DecayEnvelope::getAbsoluteDbAtMs(float elapsedMs) const {
    if (!this->isValid())
        return -100.0f;

    if (elapsedMs <= this->peakMs)
        return this->peakDb;

    for (int i = 0; i < this->validPoints; ++i) {
        const float pointMs = this->points[static_cast<size_t>(i)];
        if (pointMs < 0.0f)
            break;

        const float pointRelDb = static_cast<float>(i + 1) * DB_STEP;
        if (elapsedMs <= pointMs) {
            const float prevMs = (i == 0 ? this->peakMs : this->points[static_cast<size_t>(i - 1)]);
            const float prevRelDb = (i == 0 ? 0.0f : static_cast<float>(i) * DB_STEP);
            const float denom = juce::jmax(0.001f, pointMs - prevMs);
            const float t = juce::jlimit(0.0f, 1.0f, (elapsedMs - prevMs) / denom);
            const float relDb = prevRelDb + t * (pointRelDb - prevRelDb);
            return this->peakDb + relDb;
        }
    }

    const float lastRelDb = static_cast<float>(this->validPoints) * DB_STEP;
    return this->peakDb + lastRelDb;
}

float DecayEnvelope::getMsAtAbsoluteDb(float targetAbsDb) const {
    if (!this->isValid())
        return -1.0f;

    const float targetRelDb = targetAbsDb - this->peakDb;
    if (targetRelDb >= 0.0f)
        return this->peakMs;

    for (int i = 0; i < this->validPoints; ++i) {
        const float pointRelDb = static_cast<float>(i + 1) * DB_STEP;
        if (targetRelDb >= pointRelDb) {
            const float prevMs = (i == 0 ? this->peakMs : this->points[static_cast<size_t>(i - 1)]);
            const float prevRelDb = (i == 0 ? 0.0f : static_cast<float>(i) * DB_STEP);
            const float dbInterval = pointRelDb - prevRelDb;
            if (std::abs(dbInterval) < 0.001f)
                return prevMs;

            const float interpolation = juce::jlimit(0.0f, 1.0f, (targetRelDb - prevRelDb) / dbInterval);
            const float pointMs = this->points[static_cast<size_t>(i)];
            return prevMs + interpolation * (pointMs - prevMs);
        }
    }

    return -1.0f;
}

/** Parses a concrete source mic code or a mic-layout target code. Wildcards are used only by target outputs. */
ParseResult<MicSpec> MicSpec::parseMicCode(const juce::String& code) {
    ParseResult<MicSpec> result;
    const int codelength = code.length();
    if (codelength < 3 || codelength > 4) {
        return result.returnWithError("MIC code must be 3 or 4 characters long: '" + code + "'");
    }

    const juce::juce_wchar type = code[0];
    const juce::juce_wchar pos = code[1];
    const juce::juce_wchar format = code[2];
    const juce::String variant = (codelength == 4 ? code.substring(3) : "");

    if (!juce::String("dor*").containsChar(type)) {
        return result.returnWithError("Invalid mic type: '" + juce::String::charToString(type) + "' in '" + code + "'. Expected one of: d (direct), o (overhead), r (room), * (any)");
    }

    if (pos != '*' && type == '*' && !juce::String("ustbionwcf").containsChar(pos)) {
        return result.returnWithError("Invalid mic position: '" + juce::String::charToString(pos) + "' in '" + code + "'. Expected a known position or * (any)");
    }
    if (pos != '*' && type == 'd' && !juce::String("ustbio").containsChar(pos)) {
        return result.returnWithError("Invalid position for direct mic: '" + juce::String::charToString(pos) + "' in '" + code + "'. Expected: u (sub), s (side), t (top), b (bottom), i (in), o (out), * (any)");
    }
    if (pos != '*' && type == 'o' && !juce::String("nw").containsChar(pos)) {
        return result.returnWithError("Invalid position for overhead mic: '" + juce::String::charToString(pos) + "' in '" + code + "'. Expected: n (normal), w (wide), * (any)");
    }
    if (pos != '*' && type == 'r' && !juce::String("cf").containsChar(pos)) {
        return result.returnWithError("Invalid position for room mic: '" + juce::String::charToString(pos) + "' in '" + code + "'. Expected: c (close), f (far), * (any)");
    }

    if (!juce::String("msi").containsChar(format)) {
        return result.returnWithError("Invalid mic format: '" + juce::String::charToString(format) + "' in '" + code + "'. Expected: m (mono), s (stereo), i (mid-side)");
    }

    result.value.raw = code;
    result.value.type = typeFromChar(type);
    result.value.position = positionFromChar(pos);
    result.value.format = formatFromChar(format);
    result.value.variant = variant;
    result.value.channels = result.value.format == MicSpec::Format::Mono ? 1 : 2;

    return result;
}

/** Parses a mic chain like dtm_dbm_ons */
ParseResult<std::vector<MicSpec>> MicSpec::parseMicCodesChain(const juce::String& codeChain) {
    ParseResult<std::vector<MicSpec>> result;

    if (codeChain.isEmpty())
        return result.returnWithError("Mic chain code is empty");

    auto parts = juce::StringArray::fromTokens(codeChain, "_", "");
    parts.removeEmptyStrings();

    for (const auto& part : parts) {
        auto item = MicSpec::parseMicCode(part);
        if (item.ok())
            result.value.push_back(item.value);
        else
            result.addWarning("Skipping invalid mic code '" + part + "' in chain '" + codeChain + "': '" + item.error() + "'");
    }
    return result;
}
