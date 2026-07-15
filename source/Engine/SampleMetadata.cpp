#include "SampleMetadata.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <map>

namespace KitLibSampleMetadataHelpers {

static constexpr const char* REQUIRED_COLUMN_NAMES[] = {
    "relative_path", "mic_code", "content_hash", "file_size_bytes", "onset_ms", "intensity_db", "true_peak_db", "peak_db", "peak_ms", "decay_end_ms", "stereo_balance",
    "-6db_ms", "-12db_ms", "-18db_ms", "-24db_ms", "-30db_ms", "-36db_ms", "-42db_ms", "-48db_ms", "-54db_ms", "-60db_ms"
};

static constexpr const char* DECAY_COLUMN_NAMES[KitLib::SampleMetadataRow::DECAY_POINT_COUNT] = {
    "-6db_ms", "-12db_ms", "-18db_ms", "-24db_ms", "-30db_ms", "-36db_ms", "-42db_ms", "-48db_ms", "-54db_ms", "-60db_ms"
};

static juce::String normalizePath(const juce::String& path) {
    auto normalized = path.trim().replaceCharacter('\\', '/');
    while (normalized.startsWith("./")) normalized = normalized.substring(2);
    return normalized;
}

static juce::StringArray parseCsvLine(const juce::String& line) {
    juce::StringArray fields;
    juce::String field;
    bool quoted = false;
    for (int charIdx = 0; charIdx < line.length(); ++charIdx) {
        const auto character = line[charIdx];
        if (character == '"') {
            if (quoted && charIdx + 1 < line.length() && line[charIdx + 1] == '"') {
                field += '"';
                ++charIdx;
            } else {
                quoted = !quoted;
            }
        } else if (character == ',' && !quoted) {
            fields.add(field);
            field.clear();
        } else {
            field += character;
        }
    }
    fields.add(field);
    return fields;
}

static juce::String fieldValue(const juce::StringArray& fields, const std::map<juce::String, int>& columnIndexes, const juce::String& columnName) {
    const auto columnIt = columnIndexes.find(columnName);
    if (columnIt == columnIndexes.end() || columnIt->second < 0 || columnIt->second >= fields.size()) return {};
    return fields[columnIt->second].trim();
}

static std::optional<float> parseOptionalFloat(const juce::String& text) {
    if (text.isEmpty()) return std::nullopt;
    const auto utf8Text = text.toRawUTF8();
    char* endPtr = nullptr;
    errno = 0;
    const float value = std::strtof(utf8Text, &endPtr);
    if (errno != 0 || endPtr == utf8Text || *endPtr != '\0' || !std::isfinite(value)) return std::nullopt;
    return value;
}

static std::optional<juce::int64> parseOptionalInt64(const juce::String& text) {
    if (text.isEmpty()) return std::nullopt;
    const auto utf8Text = text.toRawUTF8();
    char* endPtr = nullptr;
    errno = 0;
    const auto value = std::strtoll(utf8Text, &endPtr, 10);
    if (errno != 0 || endPtr == utf8Text || *endPtr != '\0') return std::nullopt;
    return static_cast<juce::int64>(value);
}

} // namespace KitLibSampleMetadataHelpers

namespace KitLib {

juce::String SampleMetadataTable::makeKey(const juce::String& relativePath, const juce::String& micCode) {
    return KitLibSampleMetadataHelpers::normalizePath(relativePath) + "\n" + micCode.trim().toLowerCase();
}

ParseResult<int> SampleMetadataTable::loadFromKitRoot(const juce::File& kitRoot) {
    ParseResult<int> result;
    this->active = false;
    this->rowsByKey.clear();
    const auto metadataFile = kitRoot.getChildFile("metadata.csv");
    if (!metadataFile.existsAsFile()) return result;

    const auto text = metadataFile.loadFileAsString();
    auto lines = juce::StringArray::fromLines(text);
    while (!lines.isEmpty() && lines[lines.size() - 1].trim().isEmpty()) lines.remove(lines.size() - 1);
    if (lines.isEmpty()) return result.returnWithError("metadata.csv is empty: " + metadataFile.getFullPathName());

    const auto headerFields = KitLibSampleMetadataHelpers::parseCsvLine(lines[0]);
    std::map<juce::String, int> columnIndexes;
    for (int columnIdx = 0; columnIdx < headerFields.size(); ++columnIdx) columnIndexes[headerFields[columnIdx].trim().toLowerCase()] = columnIdx;
    juce::StringArray missingColumns;
    for (const auto* columnName : KitLibSampleMetadataHelpers::REQUIRED_COLUMN_NAMES) {
        if (columnIndexes.find(columnName) == columnIndexes.end()) missingColumns.add(columnName);
    }
    if (!missingColumns.isEmpty()) return result.returnWithError("metadata.csv is missing required columns " + missingColumns.joinIntoString(", ") + ": " + metadataFile.getFullPathName());
    for (int lineIdx = 1; lineIdx < lines.size(); ++lineIdx) {
        if (lines[lineIdx].trim().isEmpty()) continue;
        const auto fields = KitLibSampleMetadataHelpers::parseCsvLine(lines[lineIdx]);
        SampleMetadataRow row;
        row.relativePath = KitLibSampleMetadataHelpers::normalizePath(KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, "relative_path"));
        row.micCode = KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, "mic_code").toLowerCase();
        if (row.relativePath.isEmpty() || row.micCode.isEmpty()) {
            result.addWarning("metadata.csv row " + juce::String(lineIdx + 1) + " has no relative_path or mic_code and was ignored");
            continue;
        }

        row.contentHash = KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, "content_hash").toLowerCase();
        const auto fileSize = KitLibSampleMetadataHelpers::parseOptionalInt64(KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, "file_size_bytes"));
        row.fileSizeBytes = fileSize.value_or(0);
        row.onsetMs = KitLibSampleMetadataHelpers::parseOptionalFloat(KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, "onset_ms"));
        row.intensityDb = KitLibSampleMetadataHelpers::parseOptionalFloat(KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, "intensity_db"));
        row.truePeakDb = KitLibSampleMetadataHelpers::parseOptionalFloat(KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, "true_peak_db"));
        row.peakDb = KitLibSampleMetadataHelpers::parseOptionalFloat(KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, "peak_db"));
        row.peakMs = KitLibSampleMetadataHelpers::parseOptionalFloat(KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, "peak_ms"));
        row.decayEndMs = KitLibSampleMetadataHelpers::parseOptionalFloat(KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, "decay_end_ms"));
        row.stereoBalance = KitLibSampleMetadataHelpers::parseOptionalFloat(KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, "stereo_balance"));
        for (int pointIdx = 0; pointIdx < SampleMetadataRow::DECAY_POINT_COUNT; ++pointIdx) {
            row.decayPointsMs[static_cast<size_t>(pointIdx)] = KitLibSampleMetadataHelpers::parseOptionalFloat(KitLibSampleMetadataHelpers::fieldValue(fields, columnIndexes, KitLibSampleMetadataHelpers::DECAY_COLUMN_NAMES[pointIdx]));
        }

        const auto key = SampleMetadataTable::makeKey(row.relativePath, row.micCode);
        if (this->rowsByKey.find(key) != this->rowsByKey.end()) return result.returnWithError("Duplicate metadata.csv row for " + row.relativePath + " and mic " + row.micCode);
        this->rowsByKey.emplace(key, std::move(row));
    }

    if (this->rowsByKey.empty()) return result.returnWithError("metadata.csv contains no usable rows: " + metadataFile.getFullPathName());
    this->active = true;
    result.value = this->size();
    return result;
}

const SampleMetadataRow* SampleMetadataTable::find(const juce::String& relativePath, const juce::String& micCode) const {
    const auto rowIt = this->rowsByKey.find(SampleMetadataTable::makeKey(relativePath, micCode));
    return rowIt == this->rowsByKey.end() ? nullptr : &rowIt->second;
}

} // namespace KitLib
