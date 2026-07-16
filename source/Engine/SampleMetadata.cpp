#include "SampleMetadata.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KitLibSampleMetadataHelpers {

static constexpr const char* REQUIRED_COLUMN_NAMES[] = {
    "relative_path", "mic_code", "content_hash", "file_size_bytes", "onset_ms", "intensity_db", "true_peak_db", "peak_db", "peak_ms", "decay_end_ms", "stereo_balance",
    "-6db_ms", "-12db_ms", "-18db_ms", "-24db_ms", "-30db_ms", "-36db_ms", "-42db_ms", "-48db_ms", "-54db_ms", "-60db_ms"
};

static constexpr size_t REQUIRED_COLUMN_COUNT = sizeof(REQUIRED_COLUMN_NAMES) / sizeof(REQUIRED_COLUMN_NAMES[0]);
static constexpr size_t RELATIVE_PATH_COLUMN_IDX = 0;
static constexpr size_t MIC_CODE_COLUMN_IDX = 1;
static constexpr size_t CONTENT_HASH_COLUMN_IDX = 2;
static constexpr size_t FILE_SIZE_COLUMN_IDX = 3;
static constexpr size_t ONSET_COLUMN_IDX = 4;
static constexpr size_t INTENSITY_COLUMN_IDX = 5;
static constexpr size_t TRUE_PEAK_COLUMN_IDX = 6;
static constexpr size_t PEAK_COLUMN_IDX = 7;
static constexpr size_t PEAK_TIME_COLUMN_IDX = 8;
static constexpr size_t DECAY_END_COLUMN_IDX = 9;
static constexpr size_t STEREO_BALANCE_COLUMN_IDX = 10;
static constexpr size_t DECAY_COLUMN_START_IDX = 11;

struct CsvField {
    std::string_view rawText;
};

static bool isCsvWhitespace(char character) {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n' || character == '\f' || character == '\v';
}

static std::string_view trimCsvText(std::string_view text) {
    while (!text.empty() && isCsvWhitespace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isCsvWhitespace(text.back())) text.remove_suffix(1);
    return text;
}

static std::string decodeCsvField(std::string_view rawText) {
    std::string decodedText;
    decodedText.reserve(rawText.size());
    bool quoted = false;
    for (size_t charIdx = 0; charIdx < rawText.size(); ++charIdx) {
        const char character = rawText[charIdx];
        if (character == '"') {
            if (quoted && charIdx + 1 < rawText.size() && rawText[charIdx + 1] == '"') {
                decodedText += '"';
                ++charIdx;
            } else {
                quoted = !quoted;
            }
        } else {
            decodedText += character;
        }
    }
    return decodedText;
}

static std::string_view getFieldText(const CsvField& field, std::string& decodedStorage) {
    if (field.rawText.find('"') == std::string_view::npos) return trimCsvText(field.rawText);
    decodedStorage = decodeCsvField(field.rawText);
    return trimCsvText(std::string_view(decodedStorage));
}

static juce::String getFieldString(const CsvField& field) {
    std::string decodedStorage;
    const auto text = getFieldText(field, decodedStorage);
    if (text.empty()) return {};
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

static std::optional<float> parseOptionalFloat(const CsvField& field) {
    std::string decodedStorage;
    const auto text = getFieldText(field, decodedStorage);
    if (text.empty()) return std::nullopt;
    std::array<char, 64> textBuffer {};
    if (text.size() >= textBuffer.size()) return std::nullopt;
    std::copy(text.begin(), text.end(), textBuffer.begin());
    char* endPtr = nullptr;
    errno = 0;
    const float value = std::strtof(textBuffer.data(), &endPtr);
    if (errno != 0 || endPtr != textBuffer.data() + text.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

static std::optional<juce::int64> parseOptionalInt64(const CsvField& field) {
    std::string decodedStorage;
    auto text = getFieldText(field, decodedStorage);
    if (text.empty()) return std::nullopt;
    if (text.front() == '+') text.remove_prefix(1);
    if (text.empty()) return std::nullopt;
    juce::int64 value = 0;
    const auto parseResult = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parseResult.ec != std::errc {} || parseResult.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

template <typename FieldCallback>
static void forEachCsvField(std::string_view line, FieldCallback&& callback) {
    size_t fieldStartIdx = 0;
    int columnIdx = 0;
    bool quoted = false;
    for (size_t charIdx = 0; charIdx < line.size(); ++charIdx) {
        const char character = line[charIdx];
        if (character == '"') {
            if (quoted && charIdx + 1 < line.size() && line[charIdx + 1] == '"') {
                ++charIdx;
            } else {
                quoted = !quoted;
            }
        } else if (character == ',' && !quoted) {
            callback(columnIdx, CsvField { line.substr(fieldStartIdx, charIdx - fieldStartIdx) });
            fieldStartIdx = charIdx + 1;
            ++columnIdx;
        }
    }
    callback(columnIdx, CsvField { line.substr(fieldStartIdx) });
}

static size_t findLineEnd(std::string_view text, size_t lineStartIdx) {
    return text.find_first_of("\r\n", lineStartIdx);
}

static size_t findNextLineStart(std::string_view text, size_t lineEndIdx) {
    if (lineEndIdx == std::string_view::npos) return text.size();
    size_t nextLineStartIdx = lineEndIdx + 1;
    if (text[lineEndIdx] == '\r' && nextLineStartIdx < text.size() && text[nextLineStartIdx] == '\n') ++nextLineStartIdx;
    return nextLineStartIdx;
}

static juce::String normalizePath(const juce::String& path) {
    auto normalized = path.trim().replaceCharacter('\\', '/');
    while (normalized.startsWith("./")) normalized = normalized.substring(2);
    return normalized;
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

    juce::MemoryBlock csvData;
    if (!metadataFile.loadFileAsData(csvData) || csvData.getSize() == 0) return result.returnWithError("metadata.csv is empty: " + metadataFile.getFullPathName());
    std::string_view csvText(static_cast<const char*>(csvData.getData()), csvData.getSize());
    if (csvText.size() >= 3 && static_cast<unsigned char>(csvText[0]) == 0xef && static_cast<unsigned char>(csvText[1]) == 0xbb && static_cast<unsigned char>(csvText[2]) == 0xbf) csvText.remove_prefix(3);
    if (csvText.empty()) return result.returnWithError("metadata.csv is empty: " + metadataFile.getFullPathName());

    const size_t headerEndIdx = KitLibSampleMetadataHelpers::findLineEnd(csvText, 0);
    const auto headerLine = csvText.substr(0, headerEndIdx);
    std::vector<juce::String> headerFields;
    headerFields.reserve(KitLibSampleMetadataHelpers::REQUIRED_COLUMN_COUNT);
    KitLibSampleMetadataHelpers::forEachCsvField(headerLine, [&headerFields](int, const KitLibSampleMetadataHelpers::CsvField& field) {
        headerFields.push_back(KitLibSampleMetadataHelpers::getFieldString(field).toLowerCase());
    });

    std::array<int, KitLibSampleMetadataHelpers::REQUIRED_COLUMN_COUNT> columnIndexes;
    columnIndexes.fill(-1);
    for (int columnIdx = 0; columnIdx < static_cast<int>(headerFields.size()); ++columnIdx) {
        for (size_t requiredIdx = 0; requiredIdx < KitLibSampleMetadataHelpers::REQUIRED_COLUMN_COUNT; ++requiredIdx) {
            if (headerFields[static_cast<size_t>(columnIdx)] == KitLibSampleMetadataHelpers::REQUIRED_COLUMN_NAMES[requiredIdx]) columnIndexes[requiredIdx] = columnIdx;
        }
    }

    juce::StringArray missingColumns;
    for (size_t requiredIdx = 0; requiredIdx < KitLibSampleMetadataHelpers::REQUIRED_COLUMN_COUNT; ++requiredIdx) {
        if (columnIndexes[requiredIdx] < 0) missingColumns.add(KitLibSampleMetadataHelpers::REQUIRED_COLUMN_NAMES[requiredIdx]);
    }
    if (!missingColumns.isEmpty()) return result.returnWithError("metadata.csv is missing required columns " + missingColumns.joinIntoString(", ") + ": " + metadataFile.getFullPathName());

    std::vector<int> requiredIndexByColumn(headerFields.size(), -1);
    for (size_t requiredIdx = 0; requiredIdx < KitLibSampleMetadataHelpers::REQUIRED_COLUMN_COUNT; ++requiredIdx) requiredIndexByColumn[static_cast<size_t>(columnIndexes[requiredIdx])] = static_cast<int>(requiredIdx);
    this->rowsByKey.reserve(static_cast<size_t>(std::count(csvText.begin(), csvText.end(), '\n')));

    size_t lineStartIdx = KitLibSampleMetadataHelpers::findNextLineStart(csvText, headerEndIdx);
    int lineNumber = 2;
    while (lineStartIdx < csvText.size()) {
        const size_t lineEndIdx = KitLibSampleMetadataHelpers::findLineEnd(csvText, lineStartIdx);
        const auto line = csvText.substr(lineStartIdx, lineEndIdx == std::string_view::npos ? std::string_view::npos : lineEndIdx - lineStartIdx);
        if (!KitLibSampleMetadataHelpers::trimCsvText(line).empty()) {
            std::array<KitLibSampleMetadataHelpers::CsvField, KitLibSampleMetadataHelpers::REQUIRED_COLUMN_COUNT> fields {};
            KitLibSampleMetadataHelpers::forEachCsvField(line, [&fields, &requiredIndexByColumn](int columnIdx, const KitLibSampleMetadataHelpers::CsvField& field) {
                if (columnIdx < 0 || columnIdx >= static_cast<int>(requiredIndexByColumn.size())) return;
                const int requiredIdx = requiredIndexByColumn[static_cast<size_t>(columnIdx)];
                if (requiredIdx >= 0) fields[static_cast<size_t>(requiredIdx)] = field;
            });

            SampleMetadataRow row;
            row.relativePath = KitLibSampleMetadataHelpers::normalizePath(KitLibSampleMetadataHelpers::getFieldString(fields[KitLibSampleMetadataHelpers::RELATIVE_PATH_COLUMN_IDX]));
            row.micCode = KitLibSampleMetadataHelpers::getFieldString(fields[KitLibSampleMetadataHelpers::MIC_CODE_COLUMN_IDX]).toLowerCase();
            if (row.relativePath.isEmpty() || row.micCode.isEmpty()) {
                result.addWarning("metadata.csv row " + juce::String(lineNumber) + " has no relative_path or mic_code and was ignored");
            } else {
                row.contentHash = KitLibSampleMetadataHelpers::getFieldString(fields[KitLibSampleMetadataHelpers::CONTENT_HASH_COLUMN_IDX]).toLowerCase();
                row.fileSizeBytes = KitLibSampleMetadataHelpers::parseOptionalInt64(fields[KitLibSampleMetadataHelpers::FILE_SIZE_COLUMN_IDX]).value_or(0);
                row.onsetMs = KitLibSampleMetadataHelpers::parseOptionalFloat(fields[KitLibSampleMetadataHelpers::ONSET_COLUMN_IDX]);
                row.intensityDb = KitLibSampleMetadataHelpers::parseOptionalFloat(fields[KitLibSampleMetadataHelpers::INTENSITY_COLUMN_IDX]);
                row.truePeakDb = KitLibSampleMetadataHelpers::parseOptionalFloat(fields[KitLibSampleMetadataHelpers::TRUE_PEAK_COLUMN_IDX]);
                row.peakDb = KitLibSampleMetadataHelpers::parseOptionalFloat(fields[KitLibSampleMetadataHelpers::PEAK_COLUMN_IDX]);
                row.peakMs = KitLibSampleMetadataHelpers::parseOptionalFloat(fields[KitLibSampleMetadataHelpers::PEAK_TIME_COLUMN_IDX]);
                row.decayEndMs = KitLibSampleMetadataHelpers::parseOptionalFloat(fields[KitLibSampleMetadataHelpers::DECAY_END_COLUMN_IDX]);
                row.stereoBalance = KitLibSampleMetadataHelpers::parseOptionalFloat(fields[KitLibSampleMetadataHelpers::STEREO_BALANCE_COLUMN_IDX]);
                for (int pointIdx = 0; pointIdx < SampleMetadataRow::DECAY_POINT_COUNT; ++pointIdx) row.decayPointsMs[static_cast<size_t>(pointIdx)] = KitLibSampleMetadataHelpers::parseOptionalFloat(fields[KitLibSampleMetadataHelpers::DECAY_COLUMN_START_IDX + static_cast<size_t>(pointIdx)]);

                const auto key = row.relativePath + "\n" + row.micCode;
                const bool inserted = this->rowsByKey.try_emplace(key, std::move(row)).second;
                if (!inserted) return result.returnWithError("Duplicate metadata.csv row for " + row.relativePath + " and mic " + row.micCode);
            }
        }

        if (lineEndIdx == std::string_view::npos) break;
        lineStartIdx = KitLibSampleMetadataHelpers::findNextLineStart(csvText, lineEndIdx);
        ++lineNumber;
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
