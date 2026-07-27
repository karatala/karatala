#include "KitSourceOverrides.h"
#include "../Config/YamlHelper.h"
#include "RoutingTypes.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>

namespace KitSourceOverrideHelpers {

static juce::String decodeYamlKey(const juce::String& rawKey) {
    const auto trimmed = rawKey.trim();
    if (trimmed.length() < 2) return trimmed;
    const auto firstCharacter = trimmed[0];
    const auto lastCharacter = trimmed[trimmed.length() - 1];
    if ((firstCharacter != '"' || lastCharacter != '"') && (firstCharacter != '\'' || lastCharacter != '\'')) return trimmed;
    auto decoded = trimmed.substring(1, trimmed.length() - 1);
    if (firstCharacter == '"') decoded = decoded.replace("\\\"", "\"").replace("\\\\", "\\");
    return decoded;
}

static juce::String quoteYaml(const juce::String& value) {
    return "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
}

static juce::String normalizeRelativePath(const juce::String& path) {
    auto normalized = path.trim().replaceCharacter('\\', '/');
    while (normalized.startsWith("./")) normalized = normalized.substring(2);
    while (normalized.contains("//")) normalized = normalized.replace("//", "/");
    return normalized;
}

static bool isSafeRelativePath(const juce::String& path) {
    if (path.isEmpty() || path.startsWithChar('/') || path.startsWithChar('\\') || path.endsWithChar('/')) return false;
    if (path.length() > 1 && path[1] == ':') return false;
    juce::StringArray parts;
    parts.addTokens(path, "/", "");
    parts.removeEmptyStrings();
    if (parts.isEmpty()) return false;
    for (const auto& part : parts)
        if (part == "." || part == "..") return false;
    return true;
}

static bool isValidContentHash(const juce::String& contentHash) {
    return contentHash.isEmpty() || (contentHash.length() == 32 && contentHash.containsOnly("0123456789abcdefABCDEF"));
}

static std::optional<int> parseStrictInt(const YamlHelper::YamlNode& node) {
    if (!node.isScalar()) return std::nullopt;
    auto text = node.asString().trim().toStdString();
    if (text.empty()) return std::nullopt;
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

static std::optional<float> parseStrictFloat(const YamlHelper::YamlNode& node) {
    if (!node.isScalar()) return std::nullopt;
    const auto text = node.asString().trim().toStdString();
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

static juce::String formatFloat(float value) {
    if (std::abs(value) < 0.00005f) value = 0.0f;
    auto text = juce::String(value, 4);
    while (text.containsChar('.') && text.endsWithChar('0')) text = text.dropLastCharacters(1);
    if (text.endsWithChar('.')) text = text.dropLastCharacters(1);
    return text;
}

static void addUnknownKeyWarnings(const YamlHelper::YamlNode& node, const std::vector<juce::String>& knownKeys, const juce::String& context, ParseResult<int>& result) {
    if (!node.isMap()) return;
    for (const auto& rawKey : node.keys()) {
        const auto key = decodeYamlKey(rawKey);
        if (std::find(knownKeys.begin(), knownKeys.end(), key) == knownKeys.end()) result.addWarning(context + ": unknown key '" + key + "' was ignored");
    }
}

static void parseBalance(const YamlHelper::YamlNode& node, const juce::String& context, std::optional<int>& target, ParseResult<int>& result) {
    if (node.isNull()) return;
    const auto parsed = parseBalanceValue(node.asString());
    if (parsed.has_value()) target = *parsed;
    else result.addWarning(context + ": invalid balance '" + node.asString() + "' was ignored");
}

static void parseVelocityUpperBounds(const YamlHelper::YamlNode& node, const juce::String& context, KitLib::ArticulationSourceOverride& target, ParseResult<int>& result) {
    if (node.isNull()) return;
    if (!node.isMap()) {
        result.addWarning(context + ": velocityUpperBounds must be a map");
        return;
    }

    for (const auto& [rawNamedVelocity, upperBoundaryNode] : node.mapValue) {
        const auto namedVelocityText = decodeYamlKey(rawNamedVelocity).toStdString();
        int namedVelocity = 0;
        const auto parsedNamedVelocity = std::from_chars(namedVelocityText.data(), namedVelocityText.data() + namedVelocityText.size(), namedVelocity);
        const auto upperBoundary = parseStrictInt(upperBoundaryNode);
        if (parsedNamedVelocity.ec != std::errc {} || parsedNamedVelocity.ptr != namedVelocityText.data() + namedVelocityText.size() || namedVelocity < 1 || namedVelocity > 127) {
            result.addWarning(context + ": invalid named velocity '" + decodeYamlKey(rawNamedVelocity) + "' was ignored");
        } else if (!upperBoundary.has_value() || *upperBoundary < 1 || *upperBoundary > 127) {
            result.addWarning(context + ": invalid upper boundary for velocity " + juce::String(namedVelocity) + " was ignored");
        } else {
            target.velocityUpperBounds[static_cast<uint8_t>(namedVelocity)] = static_cast<uint8_t>(*upperBoundary);
        }
    }
}

static void parseArticulations(const YamlHelper::YamlNode& node, const juce::String& instrumentCodeRaw, KitLib::InstrumentSourceOverride& target, ParseResult<int>& result) {
    if (node.isNull()) return;
    if (!node.isMap()) {
        result.addWarning("Instrument '" + instrumentCodeRaw + "': articulations must be a map");
        return;
    }

    for (const auto& [rawArticulationCode, articulationNode] : node.mapValue) {
        const auto articulationCodeRaw = decodeYamlKey(rawArticulationCode);
        const auto context = "Instrument '" + instrumentCodeRaw + "', articulation '" + articulationCodeRaw + "'";
        if (articulationCodeRaw.isEmpty() || !articulationNode.isMap()) {
            result.addWarning(context + ": override must be a map");
            continue;
        }

        KitLib::ArticulationSourceOverride articulationOverride;
        parseBalance(articulationNode["balance"], context, articulationOverride.balance, result);
        parseVelocityUpperBounds(articulationNode["velocityUpperBounds"], context, articulationOverride, result);
        addUnknownKeyWarnings(articulationNode, { "balance", "velocityUpperBounds" }, context, result);
        if (articulationOverride.balance.has_value() || !articulationOverride.velocityUpperBounds.empty()) target.articulationOverrides[articulationCodeRaw] = std::move(articulationOverride);
    }
}

static void parseInstruments(const YamlHelper::YamlNode& node, KitLib::KitSourceOverrides& target, ParseResult<int>& result) {
    if (node.isNull()) return;
    if (!node.isMap()) {
        result.addWarning("instruments must be a map");
        return;
    }

    for (const auto& [rawInstrumentCode, instrumentNode] : node.mapValue) {
        const auto instrumentCodeRaw = decodeYamlKey(rawInstrumentCode);
        const auto context = "Instrument '" + instrumentCodeRaw + "'";
        if (instrumentCodeRaw.isEmpty() || !instrumentNode.isMap()) {
            result.addWarning(context + ": override must be a map");
            continue;
        }

        KitLib::InstrumentSourceOverride instrumentOverride;
        parseBalance(instrumentNode["balance"], context, instrumentOverride.balance, result);
        parseArticulations(instrumentNode["articulations"], instrumentCodeRaw, instrumentOverride, result);
        addUnknownKeyWarnings(instrumentNode, { "balance", "articulations" }, context, result);
        if (instrumentOverride.balance.has_value() || !instrumentOverride.articulationOverrides.empty()) target.instrumentOverrides[instrumentCodeRaw] = std::move(instrumentOverride);
    }
}

static void parseMicOverrides(const YamlHelper::YamlNode& node, const juce::String& samplePath, KitLib::SampleSourceOverride& target, ParseResult<int>& result) {
    if (node.isNull()) return;
    if (!node.isMap()) {
        result.addWarning("Sample '" + samplePath + "': mics must be a map");
        return;
    }

    for (const auto& [rawMicCode, micNode] : node.mapValue) {
        const auto micCode = decodeYamlKey(rawMicCode).trim().toLowerCase();
        const auto context = "Sample '" + samplePath + "', mic '" + micCode + "'";
        if (micCode.isEmpty() || !micNode.isMap()) {
            result.addWarning(context + ": override must be a map");
            continue;
        }

        KitLib::MicSourceOverride micOverride;
        if (!micNode["playbackOffsetMs"].isNull()) {
            const auto parsedOffset = parseStrictFloat(micNode["playbackOffsetMs"]);
            if (parsedOffset.has_value()) micOverride.playbackOffsetMs = *parsedOffset;
            else result.addWarning(context + ": invalid playbackOffsetMs was ignored");
        }
        addUnknownKeyWarnings(micNode, { "playbackOffsetMs" }, context, result);
        if (micOverride.playbackOffsetMs.has_value()) target.micOverrides[micCode] = micOverride;
    }
}

static void parseSamples(const YamlHelper::YamlNode& node, KitLib::KitSourceOverrides& target, ParseResult<int>& result) {
    if (node.isNull()) return;
    if (!node.isMap()) {
        result.addWarning("samples must be a map");
        return;
    }

    for (const auto& [rawSamplePath, sampleNode] : node.mapValue) {
        const auto samplePath = normalizeRelativePath(decodeYamlKey(rawSamplePath));
        const auto context = "Sample '" + samplePath + "'";
        if (!isSafeRelativePath(samplePath) || !sampleNode.isMap()) {
            result.addWarning(context + ": override must have a safe relative path and map value");
            continue;
        }

        KitLib::SampleSourceOverride sampleOverride;
        if (!sampleNode["contentHash"].isNull()) {
            const auto contentHash = sampleNode["contentHash"].asString().trim().toLowerCase();
            if (isValidContentHash(contentHash)) sampleOverride.contentHash = contentHash;
            else result.addWarning(context + ": invalid contentHash was ignored");
        }
        if (!sampleNode["manualGainDb"].isNull()) {
            const auto parsedGain = parseStrictFloat(sampleNode["manualGainDb"]);
            if (parsedGain.has_value()) sampleOverride.manualGainDb = *parsedGain;
            else result.addWarning(context + ": invalid manualGainDb was ignored");
        }
        if (!sampleNode["playbackOffsetMs"].isNull()) {
            const auto parsedOffset = parseStrictFloat(sampleNode["playbackOffsetMs"]);
            if (parsedOffset.has_value()) sampleOverride.playbackOffsetMs = *parsedOffset;
            else result.addWarning(context + ": invalid playbackOffsetMs was ignored");
        }
        parseMicOverrides(sampleNode["mics"], samplePath, sampleOverride, result);
        addUnknownKeyWarnings(sampleNode, { "contentHash", "manualGainDb", "playbackOffsetMs", "mics" }, context, result);
        const bool hasOverride = sampleOverride.contentHash.isNotEmpty() || sampleOverride.manualGainDb.has_value() || sampleOverride.playbackOffsetMs.has_value() || !sampleOverride.micOverrides.empty();
        if (hasOverride) target.sampleOverrides[samplePath] = std::move(sampleOverride);
    }
}

static void appendBalance(juce::String& yaml, const juce::String& indent, const std::optional<int>& balance) {
    if (balance.has_value()) yaml += indent + "balance: " + balanceValueToString(*balance) + "\n";
}

} // namespace KitSourceOverrideHelpers

namespace KitLib {

ParseResult<int> KitSourceOverrides::loadFromKitRoot(const juce::File& kitRoot) {
    this->instrumentOverrides.clear();
    this->sampleOverrides.clear();
    const auto file = kitRoot.getChildFile(KitSourceOverrides::FILE_NAME);
    this->loadedFileExisted = file.existsAsFile();
    this->loadedFileValid = true;
    this->loadedFileContent = this->loadedFileExisted ? file.loadFileAsString() : juce::String();
    if (!this->loadedFileExisted) return {};
    if (file.getSize() <= 0) {
        ParseResult<int> result;
        this->loadedFileValid = false;
        return result.returnWithError("Empty " + juce::String(KitSourceOverrides::FILE_NAME) + ": " + file.getFullPathName());
    }
    auto result = this->parseYaml(this->loadedFileContent);
    this->loadedFileValid = result.ok();
    return result;
}

ParseResult<int> KitSourceOverrides::parseYaml(const juce::String& yaml) {
    ParseResult<int> result;
    KitSourceOverrides parsedOverrides;
    const auto root = YamlHelper::parse(yaml);
    if (!root.isMap()) return result.returnWithError("Unable to parse " + juce::String(KitSourceOverrides::FILE_NAME));

    const auto version = KitSourceOverrideHelpers::parseStrictInt(root["version"]);
    if (!version.has_value()) return result.returnWithError("kit-overrides.yml requires integer version");
    if (*version != KitSourceOverrides::FORMAT_VERSION) return result.returnWithError("Unsupported kit-overrides.yml version " + juce::String(*version));

    KitSourceOverrideHelpers::parseInstruments(root["instruments"], parsedOverrides, result);
    KitSourceOverrideHelpers::parseSamples(root["samples"], parsedOverrides, result);
    KitSourceOverrideHelpers::addUnknownKeyWarnings(root, { "version", "instruments", "samples" }, "kit-overrides.yml", result);

    this->instrumentOverrides = std::move(parsedOverrides.instrumentOverrides);
    this->sampleOverrides = std::move(parsedOverrides.sampleOverrides);
    result.value = this->getExplicitValueCount();
    return result;
}

ParseResult<bool> KitSourceOverrides::saveToKitRoot(const juce::File& kitRoot) {
    ParseResult<bool> result;
    if (!kitRoot.isDirectory()) return result.returnWithError("Kit root is not a directory: " + kitRoot.getFullPathName());

    const auto targetFile = kitRoot.getChildFile(KitSourceOverrides::FILE_NAME);
    const auto backupFile = kitRoot.getChildFile(juce::String(KitSourceOverrides::FILE_NAME) + ".bak");
    if (!this->loadedFileValid) return result.returnWithError("The loaded kit-overrides.yml is invalid. Fix or reload it before saving.");
    const bool targetExists = targetFile.existsAsFile();
    if (targetExists != this->loadedFileExisted || (targetExists && targetFile.loadFileAsString() != this->loadedFileContent))
        return result.returnWithError("kit-overrides.yml changed outside this editor. Reload the kit before saving.");

    const auto yaml = this->toYaml();
    KitSourceOverrides validationCopy;
    const auto validation = validationCopy.parseYaml(yaml);
    if (!validation.ok()) return result.returnWithError("Generated kit-overrides.yml is invalid: " + validation.error());
    if (validationCopy.toYaml() != yaml) return result.returnWithError("Generated kit-overrides.yml did not round-trip without data loss");

    if (targetFile.existsAsFile()) {
        juce::TemporaryFile temporaryBackup(backupFile);
        if (!targetFile.copyFileTo(temporaryBackup.getFile()) || !temporaryBackup.overwriteTargetFileWithTemporary()) return result.returnWithError("Could not update kit-overrides.yml backup");
    }

    juce::TemporaryFile temporaryTarget(targetFile);
    if (!temporaryTarget.getFile().replaceWithText(yaml) || !temporaryTarget.overwriteTargetFileWithTemporary()) return result.returnWithError("Could not save kit-overrides.yml");
    this->loadedFileExisted = true;
    this->loadedFileValid = true;
    this->loadedFileContent = targetFile.loadFileAsString();
    result.value = true;
    return result;
}

juce::String KitSourceOverrides::toYaml() const {
    juce::String yaml;
    yaml += "version: " + juce::String(KitSourceOverrides::FORMAT_VERSION) + "\n";

    if (!this->instrumentOverrides.empty()) {
        yaml += "\ninstruments:\n";
        for (const auto& [instrumentCodeRaw, instrumentOverride] : this->instrumentOverrides) {
            yaml += "  " + KitSourceOverrideHelpers::quoteYaml(instrumentCodeRaw) + ":\n";
            KitSourceOverrideHelpers::appendBalance(yaml, "    ", instrumentOverride.balance);
            if (!instrumentOverride.articulationOverrides.empty()) {
                yaml += "    articulations:\n";
                for (const auto& [articulationCodeRaw, articulationOverride] : instrumentOverride.articulationOverrides) {
                    yaml += "      " + KitSourceOverrideHelpers::quoteYaml(articulationCodeRaw) + ":\n";
                    KitSourceOverrideHelpers::appendBalance(yaml, "        ", articulationOverride.balance);
                    if (!articulationOverride.velocityUpperBounds.empty()) {
                        yaml += "        velocityUpperBounds:\n";
                        for (const auto& [namedVelocity, upperBoundary] : articulationOverride.velocityUpperBounds) {
                            const auto namedVelocityText = juce::String(static_cast<int>(namedVelocity)).paddedLeft('0', 3);
                            yaml += "          " + KitSourceOverrideHelpers::quoteYaml(namedVelocityText) + ": " + juce::String(static_cast<int>(upperBoundary)) + "\n";
                        }
                    }
                }
            }
        }
    }

    if (!this->sampleOverrides.empty()) {
        yaml += "\nsamples:\n";
        for (const auto& [relativePath, sampleOverride] : this->sampleOverrides) {
            yaml += "  " + KitSourceOverrideHelpers::quoteYaml(KitSourceOverrideHelpers::normalizeRelativePath(relativePath)) + ":\n";
            if (sampleOverride.contentHash.isNotEmpty()) yaml += "    contentHash: " + KitSourceOverrideHelpers::quoteYaml(sampleOverride.contentHash.toLowerCase()) + "\n";
            if (sampleOverride.manualGainDb.has_value()) yaml += "    manualGainDb: " + KitSourceOverrideHelpers::formatFloat(*sampleOverride.manualGainDb) + "\n";
            if (sampleOverride.playbackOffsetMs.has_value()) yaml += "    playbackOffsetMs: " + KitSourceOverrideHelpers::formatFloat(*sampleOverride.playbackOffsetMs) + "\n";
            if (!sampleOverride.micOverrides.empty()) {
                yaml += "    mics:\n";
                for (const auto& [micCode, micOverride] : sampleOverride.micOverrides) {
                    yaml += "      " + KitSourceOverrideHelpers::quoteYaml(micCode.trim().toLowerCase()) + ":\n";
                    if (micOverride.playbackOffsetMs.has_value()) yaml += "        playbackOffsetMs: " + KitSourceOverrideHelpers::formatFloat(*micOverride.playbackOffsetMs) + "\n";
                }
            }
        }
    }
    return yaml;
}

const InstrumentSourceOverride* KitSourceOverrides::findInstrumentOverride(const juce::String& instrumentCodeRaw) const {
    const auto overrideIt = this->instrumentOverrides.find(instrumentCodeRaw);
    return overrideIt == this->instrumentOverrides.end() ? nullptr : &overrideIt->second;
}

const ArticulationSourceOverride* KitSourceOverrides::findArticulationOverride(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw) const {
    const auto* instrumentOverride = this->findInstrumentOverride(instrumentCodeRaw);
    if (instrumentOverride == nullptr) return nullptr;
    const auto overrideIt = instrumentOverride->articulationOverrides.find(articulationCodeRaw);
    return overrideIt == instrumentOverride->articulationOverrides.end() ? nullptr : &overrideIt->second;
}

const SampleSourceOverride* KitSourceOverrides::findSampleOverride(const juce::String& relativePath) const {
    const auto normalizedPath = KitSourceOverrideHelpers::normalizeRelativePath(relativePath);
    const auto overrideIt = this->sampleOverrides.find(normalizedPath);
    return overrideIt == this->sampleOverrides.end() ? nullptr : &overrideIt->second;
}

SampleSourceOverrideMatch KitSourceOverrides::findSampleOverride(const juce::String& relativePath, const juce::String& contentHash) const {
    SampleSourceOverrideMatch result;
    const auto normalizedPath = KitSourceOverrideHelpers::normalizeRelativePath(relativePath);
    const auto exactIt = this->sampleOverrides.find(normalizedPath);
    if (exactIt != this->sampleOverrides.end()) {
        result.value = &exactIt->second;
        result.storedRelativePath = exactIt->first;
        return result;
    }

    const auto normalizedHash = contentHash.trim().toLowerCase();
    if (!KitSourceOverrideHelpers::isValidContentHash(normalizedHash) || normalizedHash.isEmpty()) return result;
    for (const auto& [storedRelativePath, sampleOverride] : this->sampleOverrides) {
        if (sampleOverride.contentHash.compareIgnoreCase(normalizedHash) != 0) continue;
        if (result.value != nullptr) {
            result.value = nullptr;
            result.storedRelativePath.clear();
            result.ambiguousContentHash = true;
            return result;
        }
        result.value = &sampleOverride;
        result.storedRelativePath = storedRelativePath;
        result.matchedByContentHash = true;
    }
    return result;
}

const MicSourceOverride* KitSourceOverrides::findMicOverride(const juce::String& relativePath, const juce::String& micCode) const {
    const auto* sampleOverride = this->findSampleOverride(relativePath);
    if (sampleOverride == nullptr) return nullptr;
    const auto micOverrideIt = sampleOverride->micOverrides.find(micCode.trim().toLowerCase());
    return micOverrideIt == sampleOverride->micOverrides.end() ? nullptr : &micOverrideIt->second;
}

void KitSourceOverrides::setInstrumentBalance(const juce::String& instrumentCodeRaw, std::optional<int> balance) {
    if (balance.has_value() && (*balance < -127 || *balance > 127)) return;
    if (balance.has_value()) this->instrumentOverrides[instrumentCodeRaw].balance = *balance;
    else {
        const auto instrumentIt = this->instrumentOverrides.find(instrumentCodeRaw);
        if (instrumentIt != this->instrumentOverrides.end()) instrumentIt->second.balance.reset();
    }
    this->removeEmptyInstrumentOverride(instrumentCodeRaw);
}

void KitSourceOverrides::setArticulationBalance(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw, std::optional<int> balance) {
    if (balance.has_value() && (*balance < -127 || *balance > 127)) return;
    if (balance.has_value()) this->instrumentOverrides[instrumentCodeRaw].articulationOverrides[articulationCodeRaw].balance = *balance;
    else {
        const auto instrumentIt = this->instrumentOverrides.find(instrumentCodeRaw);
        if (instrumentIt != this->instrumentOverrides.end()) {
            const auto articulationIt = instrumentIt->second.articulationOverrides.find(articulationCodeRaw);
            if (articulationIt != instrumentIt->second.articulationOverrides.end()) articulationIt->second.balance.reset();
        }
    }
    this->removeEmptyInstrumentOverride(instrumentCodeRaw);
}

void KitSourceOverrides::setVelocityUpperBoundary(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw, uint8_t namedVelocity, std::optional<uint8_t> upperBoundary) {
    if (upperBoundary.has_value()) {
        this->instrumentOverrides[instrumentCodeRaw].articulationOverrides[articulationCodeRaw].velocityUpperBounds[namedVelocity] = *upperBoundary;
        return;
    }

    const auto instrumentIt = this->instrumentOverrides.find(instrumentCodeRaw);
    if (instrumentIt == this->instrumentOverrides.end()) return;
    const auto articulationIt = instrumentIt->second.articulationOverrides.find(articulationCodeRaw);
    if (articulationIt == instrumentIt->second.articulationOverrides.end()) return;
    articulationIt->second.velocityUpperBounds.erase(namedVelocity);
    this->removeEmptyInstrumentOverride(instrumentCodeRaw);
}

void KitSourceOverrides::resetVelocityUpperBoundaries(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw) {
    const auto instrumentIt = this->instrumentOverrides.find(instrumentCodeRaw);
    if (instrumentIt == this->instrumentOverrides.end()) return;
    const auto articulationIt = instrumentIt->second.articulationOverrides.find(articulationCodeRaw);
    if (articulationIt == instrumentIt->second.articulationOverrides.end()) return;
    articulationIt->second.velocityUpperBounds.clear();
    this->removeEmptyInstrumentOverride(instrumentCodeRaw);
}

void KitSourceOverrides::setSampleManualGain(const juce::String& relativePath, const juce::String& contentHash, std::optional<float> manualGainDb) {
    const auto normalizedPath = KitSourceOverrideHelpers::normalizeRelativePath(relativePath);
    if (!KitSourceOverrideHelpers::isSafeRelativePath(normalizedPath) || (manualGainDb.has_value() && !std::isfinite(*manualGainDb))) return;
    if (manualGainDb.has_value()) {
        auto& sampleOverride = this->sampleOverrides[normalizedPath];
        sampleOverride.manualGainDb = *manualGainDb;
        if (KitSourceOverrideHelpers::isValidContentHash(contentHash) && contentHash.isNotEmpty()) sampleOverride.contentHash = contentHash.toLowerCase();
    } else {
        const auto sampleIt = this->sampleOverrides.find(normalizedPath);
        if (sampleIt != this->sampleOverrides.end()) sampleIt->second.manualGainDb.reset();
    }
    this->removeEmptySampleOverride(normalizedPath);
}

void KitSourceOverrides::setSamplePlaybackOffset(const juce::String& relativePath, const juce::String& contentHash, std::optional<float> playbackOffsetMs) {
    const auto normalizedPath = KitSourceOverrideHelpers::normalizeRelativePath(relativePath);
    if (!KitSourceOverrideHelpers::isSafeRelativePath(normalizedPath) || (playbackOffsetMs.has_value() && !std::isfinite(*playbackOffsetMs))) return;
    if (playbackOffsetMs.has_value()) {
        auto& sampleOverride = this->sampleOverrides[normalizedPath];
        sampleOverride.playbackOffsetMs = *playbackOffsetMs;
        if (KitSourceOverrideHelpers::isValidContentHash(contentHash) && contentHash.isNotEmpty()) sampleOverride.contentHash = contentHash.toLowerCase();
    } else {
        const auto sampleIt = this->sampleOverrides.find(normalizedPath);
        if (sampleIt != this->sampleOverrides.end()) sampleIt->second.playbackOffsetMs.reset();
    }
    this->removeEmptySampleOverride(normalizedPath);
}

void KitSourceOverrides::setMicPlaybackOffset(const juce::String& relativePath, const juce::String& contentHash, const juce::String& micCode, std::optional<float> playbackOffsetMs) {
    const auto normalizedPath = KitSourceOverrideHelpers::normalizeRelativePath(relativePath);
    const auto normalizedMicCode = micCode.trim().toLowerCase();
    if (!KitSourceOverrideHelpers::isSafeRelativePath(normalizedPath) || normalizedMicCode.isEmpty() || (playbackOffsetMs.has_value() && !std::isfinite(*playbackOffsetMs))) return;
    if (playbackOffsetMs.has_value()) {
        auto& sampleOverride = this->sampleOverrides[normalizedPath];
        sampleOverride.micOverrides[normalizedMicCode].playbackOffsetMs = *playbackOffsetMs;
        if (KitSourceOverrideHelpers::isValidContentHash(contentHash) && contentHash.isNotEmpty()) sampleOverride.contentHash = contentHash.toLowerCase();
    } else {
        const auto sampleIt = this->sampleOverrides.find(normalizedPath);
        if (sampleIt != this->sampleOverrides.end()) sampleIt->second.micOverrides.erase(normalizedMicCode);
    }
    this->removeEmptySampleOverride(normalizedPath);
}

void KitSourceOverrides::removeEmptyInstrumentOverride(const juce::String& instrumentCodeRaw) {
    const auto instrumentIt = this->instrumentOverrides.find(instrumentCodeRaw);
    if (instrumentIt == this->instrumentOverrides.end()) return;
    std::erase_if(instrumentIt->second.articulationOverrides, [](const auto& entry) {
        return !entry.second.balance.has_value() && entry.second.velocityUpperBounds.empty();
    });
    if (!instrumentIt->second.balance.has_value() && instrumentIt->second.articulationOverrides.empty()) this->instrumentOverrides.erase(instrumentIt);
}

void KitSourceOverrides::removeEmptySampleOverride(const juce::String& relativePath) {
    const auto normalizedPath = KitSourceOverrideHelpers::normalizeRelativePath(relativePath);
    const auto sampleIt = this->sampleOverrides.find(normalizedPath);
    if (sampleIt == this->sampleOverrides.end()) return;
    std::erase_if(sampleIt->second.micOverrides, [](const auto& entry) { return !entry.second.playbackOffsetMs.has_value(); });
    if (!sampleIt->second.manualGainDb.has_value() && !sampleIt->second.playbackOffsetMs.has_value() && sampleIt->second.micOverrides.empty()) this->sampleOverrides.erase(sampleIt);
}

ParseResult<std::vector<uint8_t>> KitSourceOverrides::resolveVelocityUpperBoundaries(const juce::String& instrumentCodeRaw, const juce::String& articulationCodeRaw, const std::vector<uint8_t>& namedVelocities) const {
    ParseResult<std::vector<uint8_t>> result;
    if (namedVelocities.empty()) return result;

    result.value = namedVelocities;
    result.value.back() = 127;
    const auto* articulationOverride = this->findArticulationOverride(instrumentCodeRaw, articulationCodeRaw);
    if (articulationOverride != nullptr) {
        for (const auto& [namedVelocity, upperBoundary] : articulationOverride->velocityUpperBounds) {
            const auto namedVelocityIt = std::find(namedVelocities.begin(), namedVelocities.end(), namedVelocity);
            if (namedVelocityIt == namedVelocities.end()) {
                result.addWarning("Stale velocityUpperBounds key " + juce::String(static_cast<int>(namedVelocity)) + " for " + instrumentCodeRaw + "/" + articulationCodeRaw);
                continue;
            }
            const auto layerIdx = static_cast<size_t>(std::distance(namedVelocities.begin(), namedVelocityIt));
            if (layerIdx + 1 == namedVelocities.size()) {
                result.addWarning("The last velocity layer always ends at 127 for " + instrumentCodeRaw + "/" + articulationCodeRaw);
                continue;
            }
            result.value[layerIdx] = upperBoundary;
        }
    }

    uint8_t previousUpperBoundary = 0;
    for (size_t layerIdx = 0; layerIdx < result.value.size(); ++layerIdx) {
        const auto upperBoundary = result.value[layerIdx];
        if (upperBoundary <= previousUpperBoundary) return result.returnWithError("Velocity upper boundaries must increase for " + instrumentCodeRaw + "/" + articulationCodeRaw);
        previousUpperBoundary = upperBoundary;
    }
    if (result.value.back() != 127) return result.returnWithError("The last velocity upper boundary must be 127 for " + instrumentCodeRaw + "/" + articulationCodeRaw);
    return result;
}

int KitSourceOverrides::getExplicitValueCount() const {
    int count = 0;
    for (const auto& [instrumentCodeRaw, instrumentOverride] : this->instrumentOverrides) {
        juce::ignoreUnused(instrumentCodeRaw);
        if (instrumentOverride.balance.has_value()) ++count;
        for (const auto& [articulationCodeRaw, articulationOverride] : instrumentOverride.articulationOverrides) {
            juce::ignoreUnused(articulationCodeRaw);
            if (articulationOverride.balance.has_value()) ++count;
            count += static_cast<int>(articulationOverride.velocityUpperBounds.size());
        }
    }
    for (const auto& [relativePath, sampleOverride] : this->sampleOverrides) {
        juce::ignoreUnused(relativePath);
        if (sampleOverride.contentHash.isNotEmpty()) ++count;
        if (sampleOverride.manualGainDb.has_value()) ++count;
        if (sampleOverride.playbackOffsetMs.has_value()) ++count;
        for (const auto& [micCode, micOverride] : sampleOverride.micOverrides) {
            juce::ignoreUnused(micCode);
            if (micOverride.playbackOffsetMs.has_value()) ++count;
        }
    }
    return count;
}

} // namespace KitLib
