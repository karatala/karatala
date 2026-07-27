#include "KitLibModel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace KitLib;

namespace KitLibIdentityHelpers {

static juce::String identityKey(const juce::String& value) { return value.trim().toLowerCase(); }

static void addCandidate(std::vector<juce::String>& candidates, const juce::String& value) {
    const auto trimmed = value.trim();
    if (trimmed.isEmpty()) return;
    const auto key = identityKey(trimmed);
    for (const auto& existing : candidates)
        if (identityKey(existing) == key) return;
    candidates.push_back(trimmed);
}

static bool candidateMatches(const std::vector<juce::String>& candidates, const juce::String& id) {
    const auto key = identityKey(id);
    if (key.isEmpty()) return false;
    for (const auto& candidate : candidates)
        if (identityKey(candidate) == key) return true;
    return false;
}

static juce::String descriptionIdentityText(const juce::String& description) { return description.trim().replace(" ", "-"); }

static bool looksLikeBalanceToken(const juce::String& raw) {
    const auto value = raw.trim().toUpperCase();
    if (value.isEmpty()) return false;
    const auto side = value[0];
    if (side != 'L' && side != 'R' && side != 'C') return false;
    if (value.length() == 1) return true;
    for (int idx = 1; idx < value.length(); ++idx)
        if (!juce::CharacterFunctions::isDigit(value[idx])) return false;
    return true;
}

static juce::String invalidBalanceWarning(const juce::String& dirName, const juce::String& token) {
    return "Invalid or missing instrument balance value '" + token + "' in folder '" + dirName + "'; expected L1-L127, R1-R127, or C0; ignoring balance";
}

static juce::String stableContentHash(const juce::String& sourceText) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const char* textPtr = sourceText.toRawUTF8(); *textPtr != '\0'; ++textPtr) {
        hash ^= static_cast<std::uint8_t>(*textPtr);
        hash *= 1099511628211ull;
    }
    return juce::String::toHexString(static_cast<juce::int64>(hash)).toUpperCase();
}

static juce::String relativePathForFingerprint(const juce::File& rootDir, const juce::File& file) {
    return file.getRelativePathFrom(rootDir).replaceCharacter('\\', '/');
}

static juce::String fileMetadataFingerprint(const juce::File& rootDir, const juce::File& file) {
    return "file|" + relativePathForFingerprint(rootDir, file) + "|size=" + juce::String(file.getSize()) + "|mtime=" + juce::String(file.getLastModificationTime().toMilliseconds());
}
} // namespace KitLibIdentityHelpers

namespace KitLibSampleMetadataHelpers {

static std::optional<float> parseSignedGainDb(const juce::String& value) {
    if (value.length() < 2) return std::nullopt;
    const auto direction = value[0];
    if (direction != 'n' && direction != 'p') return std::nullopt;

    const auto numberText = value.substring(1);
    bool hasDigit = false;
    bool hasDecimalPoint = false;
    for (int idx = 0; idx < numberText.length(); ++idx) {
        const auto character = numberText[idx];
        if (juce::CharacterFunctions::isDigit(character)) {
            hasDigit = true;
            continue;
        }
        if (character == '.' && !hasDecimalPoint) {
            hasDecimalPoint = true;
            continue;
        }
        return std::nullopt;
    }
    if (!hasDigit) return std::nullopt;

    const float magnitudeDb = std::abs(static_cast<float>(numberText.getDoubleValue()));
    return direction == 'n' ? -magnitudeDb : magnitudeDb;
}

static std::optional<float> parseSignedOffsetMs(const juce::String& value) {
    if (value.length() < 2) return std::nullopt;
    const auto direction = value[0];
    if (direction != 'n' && direction != 'p') return std::nullopt;

    const auto numberText = value.substring(1);
    bool hasDigit = false;
    bool hasDecimalPoint = false;
    for (int idx = 0; idx < numberText.length(); ++idx) {
        const auto character = numberText[idx];
        if (juce::CharacterFunctions::isDigit(character)) {
            hasDigit = true;
            continue;
        }
        if (character == '.' && !hasDecimalPoint) {
            hasDecimalPoint = true;
            continue;
        }
        return std::nullopt;
    }
    if (!hasDigit) return std::nullopt;

    const float magnitudeMs = std::abs(static_cast<float>(numberText.getDoubleValue()));
    return direction == 'n' ? -magnitudeMs : magnitudeMs;
}

} // namespace KitLibSampleMetadataHelpers

namespace KitLibCalibrationHelpers {

static constexpr float MAX_POSITIVE_TRIM_DB = 36.0f;
static constexpr float REQUIRED_HEADROOM_DB = 1.0f;

static std::optional<int> referenceMicPriority(const KitLib::SampleVariantSpec& sampleVariant) {
    if (!sampleVariant.intensityDb.has_value()) return std::nullopt;
    if (sampleVariant.micSpec.raw.compareIgnoreCase("ons") == 0) return 0;
    if (sampleVariant.micSpec.type == KitLib::MicSpec::Type::Overhead && sampleVariant.micSpec.position == KitLib::MicSpec::Position::Normal && sampleVariant.micSpec.format == KitLib::MicSpec::Format::Stereo) return 1;
    if (sampleVariant.micSpec.type == KitLib::MicSpec::Type::Overhead && sampleVariant.micSpec.format == KitLib::MicSpec::Format::Stereo) return 2;
    if (sampleVariant.micSpec.type == KitLib::MicSpec::Type::Overhead && sampleVariant.micSpec.format == KitLib::MicSpec::Format::MidSide) return 3;
    return std::nullopt;
}

static float median(std::vector<float> values) {
    std::sort(values.begin(), values.end());
    const size_t middleIdx = values.size() / 2;
    if (values.size() % 2 == 1) return values[middleIdx];
    return 0.5f * (values[middleIdx - 1] + values[middleIdx]);
}

static float defaultGlobalTargetDb(uint8_t velocity) {
    return 20.0f * std::log10(static_cast<float>(juce::jmax(1, static_cast<int>(velocity))) / 127.0f);
}

static void buildLogicalHitMeasurements(KitLib::VelocityLayerSpec& velocityLayer) {
    velocityLayer.logicalHitMeasurements.clear();
    velocityLayer.rawVelocityMedianDb.reset();
    velocityLayer.fittedSourceDb.reset();
    velocityLayer.calibrationTrimDb.reset();
    velocityLayer.calibrationLimited = false;

    std::map<uint8_t, std::vector<KitLib::SampleVariantSpec*>> samplesByVariant;
    for (const auto& sampleVariant : velocityLayer.sampleVariantSpecs) {
        sampleVariant->timingOffsetMs.reset();
        samplesByVariant[KitLib::SampleVariantSpec::letterToIdx(sampleVariant->letter)].push_back(sampleVariant.get());
    }

    for (auto& [variantIdx, linkedSamples] : samplesByVariant) {
        for (const auto* sampleVariant : linkedSamples) {
            if (!sampleVariant->hasCsvMetadata || !sampleVariant->truePeakDb.has_value()) {
                velocityLayer.logicalHitMeasurements.clear();
                return;
            }
        }
        KitLib::SampleVariantSpec* referenceSample = nullptr;
        int referencePriority = std::numeric_limits<int>::max();
        for (auto* sampleVariant : linkedSamples) {
            const auto priority = referenceMicPriority(*sampleVariant);
            if (!priority.has_value()) continue;
            const bool samePriorityEarlierPath = referenceSample != nullptr && *priority == referencePriority && sampleVariant->sampleFile.getFullPathName().compare(referenceSample->sampleFile.getFullPathName()) < 0;
            if (referenceSample == nullptr || *priority < referencePriority || samePriorityEarlierPath) {
                referenceSample = sampleVariant;
                referencePriority = *priority;
            }
        }
        if (referenceSample == nullptr) {
            velocityLayer.logicalHitMeasurements.clear();
            return;
        }

        KitLib::VelocityLayerSpec::LogicalHitMeasurement hitMeasurement;
        hitMeasurement.variantIdx = variantIdx;
        hitMeasurement.intensityDb = *referenceSample->intensityDb + referenceSample->manualGainDb;
        std::optional<float> maxTruePeakDb;
        for (auto* sampleVariant : linkedSamples) {
            if (referenceSample->onsetMs.has_value() && sampleVariant->onsetMs.has_value()) sampleVariant->timingOffsetMs = *sampleVariant->onsetMs - *referenceSample->onsetMs;
            if (!sampleVariant->truePeakDb.has_value()) continue;
            const float effectiveTruePeakDb = *sampleVariant->truePeakDb + sampleVariant->manualGainDb;
            if (!maxTruePeakDb.has_value() || effectiveTruePeakDb > *maxTruePeakDb) maxTruePeakDb = effectiveTruePeakDb;
        }
        hitMeasurement.maxTruePeakDb = maxTruePeakDb;
        velocityLayer.logicalHitMeasurements.push_back(hitMeasurement);
    }

    if (velocityLayer.logicalHitMeasurements.empty()) return;
    std::vector<float> hitIntensities;
    hitIntensities.reserve(velocityLayer.logicalHitMeasurements.size());
    for (const auto& hitMeasurement : velocityLayer.logicalHitMeasurements) hitIntensities.push_back(hitMeasurement.intensityDb);
    velocityLayer.rawVelocityMedianDb = median(std::move(hitIntensities));
}

static void fitMonotonicSourceProfile(KitLib::ArticulationSpec& articulation) {
    struct FitBlock {
        std::vector<KitLib::VelocityLayerSpec*> layers;
        double weightedSum = 0.0;
        int weight = 0;
        double mean() const { return this->weight > 0 ? this->weightedSum / static_cast<double>(this->weight) : 0.0; }
    };

    std::vector<FitBlock> blocks;
    for (const auto& velocityLayer : articulation.velocityLayerSpecs) {
        if (!velocityLayer->rawVelocityMedianDb.has_value()) continue;
        const int weight = juce::jmax(1, static_cast<int>(velocityLayer->logicalHitMeasurements.size()));
        FitBlock block;
        block.layers.push_back(velocityLayer.get());
        block.weightedSum = static_cast<double>(*velocityLayer->rawVelocityMedianDb) * static_cast<double>(weight);
        block.weight = weight;
        blocks.push_back(std::move(block));
        while (blocks.size() >= 2 && blocks[blocks.size() - 2].mean() > blocks.back().mean()) {
            auto rightBlock = std::move(blocks.back());
            blocks.pop_back();
            auto& leftBlock = blocks.back();
            leftBlock.layers.insert(leftBlock.layers.end(), rightBlock.layers.begin(), rightBlock.layers.end());
            leftBlock.weightedSum += rightBlock.weightedSum;
            leftBlock.weight += rightBlock.weight;
        }
    }

    for (const auto& block : blocks) {
        const float fittedValueDb = static_cast<float>(block.mean());
        for (auto* velocityLayer : block.layers) velocityLayer->fittedSourceDb = fittedValueDb;
    }
}

static void finalizeArticulationCalibration(KitLib::ArticulationSpec& articulation) {
    if (articulation.velocityLayerSpecs.empty()) return;
    for (const auto& velocityLayer : articulation.velocityLayerSpecs)
        if (!velocityLayer->rawVelocityMedianDb.has_value() || !velocityLayer->fittedSourceDb.has_value() || velocityLayer->logicalHitMeasurements.empty()) return;

    const float articulationReferenceDb = *articulation.velocityLayerSpecs.back()->fittedSourceDb;
    for (const auto& velocityLayer : articulation.velocityLayerSpecs) {
        const float requiredTrimDb = articulationReferenceDb - *velocityLayer->rawVelocityMedianDb;
        float safeTrimDb = juce::jmin(requiredTrimDb, MAX_POSITIVE_TRIM_DB);
        if (safeTrimDb < requiredTrimDb) velocityLayer->calibrationLimited = true;

        std::optional<float> maxTruePeakDb;
        for (const auto& hitMeasurement : velocityLayer->logicalHitMeasurements)
            if (hitMeasurement.maxTruePeakDb.has_value() && (!maxTruePeakDb.has_value() || *hitMeasurement.maxTruePeakDb > *maxTruePeakDb)) maxTruePeakDb = hitMeasurement.maxTruePeakDb;
        if (maxTruePeakDb.has_value()) {
            const float peakLimitedTrimDb = -REQUIRED_HEADROOM_DB - defaultGlobalTargetDb(velocityLayer->velocity) - *maxTruePeakDb;
            if (safeTrimDb > peakLimitedTrimDb) {
                safeTrimDb = peakLimitedTrimDb;
                velocityLayer->calibrationLimited = true;
            }
        }
        velocityLayer->calibrationTrimDb = safeTrimDb;
    }
}

} // namespace KitLibCalibrationHelpers

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

/** Parses an instrument folder name like kick-22 snare-14x6.5 ride-20-R42-Basian OMG Mental  */
InstrumentSpec::InstrumentSpec(DrumKitSpec* kit, const MetaInfo& metaInfo)
    : parent(kit), meta(metaInfo), filenameBalance(metaInfo.balance) {
    const auto* instrumentOverride = this->parent != nullptr ? this->parent->sourceOverrides.findInstrumentOverride(this->meta.codeRaw) : nullptr;
    if (instrumentOverride != nullptr && instrumentOverride->balance.has_value()) this->balanceOverride = instrumentOverride->balance;
}

ParseResult<InstrumentSpec::MetaInfo> InstrumentSpec::parseInfo(const juce::String& dirName) {
    ParseResult<MetaInfo> result;
    juce::String processed = dirName.trim();
    auto parts = juce::StringArray::fromTokens(processed, "-", "");
    result.value.name = parts[0].trim();
    if (parts.size() > 1)
        result.value.size = parts[1].trim();

    int descriptionStartIdx = 2;
    if (parts.size() > 2) {
        const auto balanceToken = parts[2].trim();
        auto parsedBalance = parseBalanceValue(balanceToken);
        if (parsedBalance.has_value()) {
            result.value.balance = *parsedBalance;
            descriptionStartIdx = 3;
        } else if (KitLibIdentityHelpers::looksLikeBalanceToken(balanceToken)) {
            result.addWarning(KitLibIdentityHelpers::invalidBalanceWarning(processed, balanceToken));
            descriptionStartIdx = 3;
        }
    }
    if (parts.size() > descriptionStartIdx) {
        parts.removeRange(0, descriptionStartIdx);
        result.value.description = parts.size() > 0 ? parts.joinIntoString(" ").trim() : "";
    }
    result.value.type = InstrumentSpec::typeFromString(result.value.name);
    result.value.codeRaw = processed;

    return result;
}

std::vector<juce::String> InstrumentSpec::getIdentityCandidates(const InstrumentSpec::MetaInfo& meta) {
    std::vector<juce::String> candidates;
    juce::String base = meta.name.trim();
    if (meta.size.trim().isNotEmpty()) base += "-" + meta.size.trim();
    KitLibIdentityHelpers::addCandidate(candidates, base);

    juce::String balanceBase = base;
    if (meta.balance.has_value()) {
        balanceBase += "-" + balanceValueToString(*meta.balance);
        KitLibIdentityHelpers::addCandidate(candidates, balanceBase);
    }
    if (meta.description.trim().isNotEmpty())
        KitLibIdentityHelpers::addCandidate(candidates, balanceBase + "-" + KitLibIdentityHelpers::descriptionIdentityText(meta.description));
    KitLibIdentityHelpers::addCandidate(candidates, meta.codeRaw);
    return candidates;
}

void ArticulationSpec::addFolderIdentity(const juce::String& folderCodeRaw) {
    const auto trimmed = folderCodeRaw.trim();
    if (trimmed.isEmpty()) return;
    for (const auto& existing : this->folderCodeRaws)
        if (existing.trim().equalsIgnoreCase(trimmed)) return;
    this->folderCodeRaws.push_back(trimmed);
}

std::vector<juce::String> ArticulationSpec::getIdentityCandidates() const {
    std::vector<juce::String> candidates;
    KitLibIdentityHelpers::addCandidate(candidates, this->codeRaw);
    for (const auto& folderCodeRaw : this->folderCodeRaws)
        KitLibIdentityHelpers::addCandidate(candidates, folderCodeRaw);
    return candidates;
}

std::optional<int> ArticulationSpec::getEffectiveBalance() const {
    if (this->balanceOverride.has_value()) return this->balanceOverride;
    if (this->filenameBalance.has_value()) return this->filenameBalance;
    if (this->balance.has_value()) return this->balance;
    if (this->parent != nullptr) return this->parent->getEffectiveBalance();
    return std::nullopt;
}

juce::String InstrumentSpec::getContentFingerprint() const {
    if (this->contentFingerprintCache.has_value()) return this->contentFingerprintCache.value();
    std::vector<juce::String> records;
    records.push_back("instrument|" + this->meta.codeRaw + "|" + this->meta.rootDir.getFullPathName());
    if (this->parent != nullptr) records.push_back("kit-source-overrides|" + this->parent->sourceOverrides.toYaml());

    if (this->meta.rootDir.isDirectory()) {
        const auto childDirs = this->meta.rootDir.findChildFiles(juce::File::findDirectories, true, "*");
        for (const auto& childDir : childDirs)
            records.push_back("dir|" + KitLibIdentityHelpers::relativePathForFingerprint(this->meta.rootDir, childDir));

        const auto childFiles = this->meta.rootDir.findChildFiles(juce::File::findFiles, true, "*");
        for (const auto& childFile : childFiles)
            if (InstrumentSpec::isAudioFile(childFile)) records.push_back(KitLibIdentityHelpers::fileMetadataFingerprint(this->meta.rootDir, childFile));
    } else {
        records.push_back("missing");
    }

    std::sort(records.begin(), records.end());

    juce::String sourceText;
    for (const auto& record : records)
        sourceText += record + "\n";

    this->contentFingerprintCache = KitLibIdentityHelpers::stableContentHash(sourceText);
    return this->contentFingerprintCache.value();
}

bool InstrumentSpec::isAudioFile(const juce::File& file) {
    const auto extension = file.getFileExtension();
    return extension.equalsIgnoreCase(".wav") || extension.equalsIgnoreCase(".flac") || extension.equalsIgnoreCase(".aiff") || extension.equalsIgnoreCase(".mp3") || extension.equalsIgnoreCase(".ogg"); //TODO: refactor dynamical support from plugin's config.yaml
}

int InstrumentSpec::scanInstrumentSamples(ScanStats* stats) {
    this->contentFingerprintCache = std::nullopt;
    const auto articulationDirs = meta.rootDir.findChildFiles(juce::File::findDirectories, false, "*");
    int sampleCount = 0;
    int matchedMetadataRowCount = 0;
    int missingMetadataRowCount = 0;
    int staleMetadataRowCount = 0;
    for (const auto& artDir : articulationDirs) {
        if (!artDir.isDirectory())
            continue;

        auto artName = artDir.getFileName();
        auto parts = juce::StringArray::fromTokens(artName, "-", "");
        std::optional<int> folderBalance;
        if (parts.size() > 1) {
            folderBalance = parseBalanceValue(parts[parts.size() - 1]);
            if (folderBalance.has_value())
                parts.remove(parts.size() - 1);
        }

        const auto folderCodeRaw = parts.joinIntoString("-").trim();
        ArticulationSpec* artSpec = this->getOrAddArticulation(parts[0].trim());
        artSpec->addFolderIdentity(folderCodeRaw);
        if (folderBalance.has_value()) {
            if (artSpec->balanceConflict) {
                // Already known ambiguous; keep automatic orientation disabled.
            } else if (!artSpec->balance.has_value()) {
                artSpec->balance = *folderBalance;
            } else if (*artSpec->balance != *folderBalance) {
                LOG(Warning, "Conflicting balance suffixes for articulation '" + artSpec->codeRaw + "' in instrument '" + this->meta.name + "'; automatic orientation disabled");
                artSpec->balance.reset();
                artSpec->balanceConflict = true;
            }
        }

        for (int i = 1; i < parts.size(); ++i) {
            auto expDimPart = parts[i];
            auto expDimCodeEnd = expDimPart.indexOfAnyOf("0123456789");
            if (expDimCodeEnd > 0) {
                juce::String expDimCode = expDimPart.substring(0, expDimCodeEnd);
                juce::String expDimValue = expDimPart.substring(expDimCodeEnd);
                if (KitLib::ArticulationSpec::ExpressionDimension::stringToType(expDimCode) == KitLib::ArticulationSpec::ExpressionDimension::Type::Unknown)
                    LOG(Warning, "Unknown dimension code '" + expDimCode + "' for instrument '" + this->meta.name + "' (folder '" + artDir.getFullPathName() + "')");
                artSpec->addExpressionDimensionValue(expDimCode, static_cast<uint8_t>(expDimValue.getIntValue()));
                // update global dimension usage counts (one per observed value)
                if (stats)
                    stats->dimensionCodeCounts[expDimCode] += 1;
            }
        }

        const auto sampleFiles = artDir.findChildFiles(juce::File::findFiles, false, "*.*");
        for (const auto& sampleFile : sampleFiles) {
            const auto fileName = sampleFile.getFileName();
            if (!InstrumentSpec::isAudioFile(sampleFile)) {
                LOG(Info, "Skipping non-audio file: " + fileName);
                continue;
            }

    
            auto parsedInfo = this->parseSampleFile(fileName);
            if (parsedInfo.ok()) {
                parsedInfo.value.file = sampleFile;
            } else {
                LOG(Warning, "Could not parse filename '" + fileName + "': " + parsedInfo.error());
                continue;
            }

            auto* velSpec = artSpec->getOrAddVelocityLayer(parsedInfo.value.velocity);
            const auto kitRelativePath = sampleFile.getRelativePathFrom(this->parent->meta.rootDir).replaceCharacter('\\', '/');
            const auto sampleFileSize = this->parent->sampleMetadata.isActive() ? sampleFile.getSize() : 0;
            juce::String sampleContentHash;
            if (this->parent->sampleMetadata.isActive()) {
                for (const auto& micDescriptor : parsedInfo.value.micDescriptors) {
                    const auto* metadataRow = this->parent->sampleMetadata.find(kitRelativePath, micDescriptor.raw);
                    if (metadataRow == nullptr || metadataRow->fileSizeBytes <= 0 || metadataRow->fileSizeBytes != sampleFileSize || metadataRow->contentHash.isEmpty()) continue;
                    sampleContentHash = metadataRow->contentHash;
                    break;
                }
            }
            const auto sampleOverrideMatch = this->parent->sourceOverrides.findSampleOverride(kitRelativePath, sampleContentHash);
            const auto* sampleOverride = sampleOverrideMatch.value;
            if (sampleOverrideMatch.ambiguousContentHash) LOG(Warning, "Ambiguous kit source override contentHash for sample '" + kitRelativePath + "'; moved-file recovery was ignored");

            // Add one sample variant per mic chain. Track channel cursor for multi-channel files.
            int channelCursor = 0;
            for (size_t i = 0; i < parsedInfo.value.micDescriptors.size(); i++) {
                uint8_t variantIdx = 0;
                if (!parsedInfo.value.sampleVariant.isEmpty())
                    variantIdx = SampleVariantSpec::letterToIdx(parsedInfo.value.sampleVariant[0]);

                const auto& micDesc = parsedInfo.value.micDescriptors[i];
                auto* sv = velSpec->getOrAddSampleVariant(sampleFile, variantIdx, micDesc, channelCursor);
                const auto micOffsetIt = parsedInfo.value.playbackOffsetMsByMic.find(micDesc.raw.toLowerCase());
                sv->filenameManualGainDb = parsedInfo.value.manualGainDb;
                sv->manualGainDbOverride.reset();
                if (sampleOverride != nullptr) sv->manualGainDbOverride = sampleOverride->manualGainDb;
                sv->manualGainDb = sv->manualGainDbOverride.value_or(sv->filenameManualGainDb);
                sv->filenamePlaybackOffsetMs = micOffsetIt != parsedInfo.value.playbackOffsetMsByMic.end() ? micOffsetIt->second : parsedInfo.value.playbackOffsetMs;
                if (sampleOverride != nullptr) {
                    const auto sampleMicOverrideIt = sampleOverride->micOverrides.find(micDesc.raw.toLowerCase());
                    if (sampleMicOverrideIt != sampleOverride->micOverrides.end() && sampleMicOverrideIt->second.playbackOffsetMs.has_value())
                        sv->playbackOffsetMsOverride = sampleMicOverrideIt->second.playbackOffsetMs;
                    else
                        sv->playbackOffsetMsOverride = sampleOverride->playbackOffsetMs;
                } else {
                    sv->playbackOffsetMsOverride.reset();
                }
                sv->playbackOffsetMs = sv->playbackOffsetMsOverride.value_or(sv->filenamePlaybackOffsetMs);
                sv->peakDb = parsedInfo.value.peakDb + sv->manualGainDb;
                sv->peakMs = parsedInfo.value.peakMs;
                sv->decayMs = parsedInfo.value.decayMs;
                sv->decayCode = parsedInfo.value.decayCode;
                sv->decayEnvelope = parsedInfo.value.peakDb < 0.0f ? DecayEnvelope::decode(sv->decayCode, sv->peakDb, sv->peakMs) : DecayEnvelope {};
                if (this->parent->sampleMetadata.isActive()) {
                    const auto* metadataRow = this->parent->sampleMetadata.find(kitRelativePath, micDesc.raw);
                    const bool fileSizeMatches = metadataRow != nullptr && metadataRow->fileSizeBytes > 0 && metadataRow->fileSizeBytes == sampleFileSize;
                    if (metadataRow != nullptr && fileSizeMatches) {
                        sv->hasCsvMetadata = true;
                        sv->metadataContentHash = metadataRow->contentHash;
                        sv->metadataFileSizeBytes = metadataRow->fileSizeBytes;
                        sv->onsetMs = metadataRow->onsetMs;
                        sv->intensityDb = metadataRow->intensityDb;
                        sv->truePeakDb = metadataRow->truePeakDb;
                        sv->stereoBalance = metadataRow->stereoBalance;
                        sv->peakDb = metadataRow->peakDb.has_value() ? *metadataRow->peakDb + sv->manualGainDb : 0.0f;
                        sv->peakMs = metadataRow->peakMs.value_or(0.0f);
                        sv->decayMs = metadataRow->decayEndMs.value_or(0.0f);
                        sv->decayCode.clear();
                        sv->decayEnvelope = {};
                        if (metadataRow->peakDb.has_value() && metadataRow->peakMs.has_value()) sv->decayEnvelope = DecayEnvelope::fromPoints(sv->peakDb, sv->peakMs, metadataRow->decayPointsMs);
                        ++matchedMetadataRowCount;
                    } else if (metadataRow != nullptr) {
                        ++staleMetadataRowCount;
                    } else {
                        ++missingMetadataRowCount;
                    }
                }
                sampleCount++;

                // update global counters per actual file+mic occurrence (reflects sample files counted per mic)
                if (stats) {
                    stats->micCodeCounts[micDesc.raw]++;
                    juce::String variantLetter = parsedInfo.value.sampleVariant.isEmpty() ? juce::String("") : juce::String::charToString(parsedInfo.value.sampleVariant[0]);
                    stats->sampleVariantCounts[variantLetter]++;
                    stats->velocityCounts[parsedInfo.value.velocity]++;
                }

                // Advance cursor by number of channels described by this mic descriptor
                channelCursor += micDesc.channels;
            }
        }
    }
    this->buildCalibrationProfiles();
    if (this->parent->sampleMetadata.isActive() && (missingMetadataRowCount > 0 || staleMetadataRowCount > 0)) {
        LOG(Warning, "metadata.csv for instrument '" + this->meta.codeRaw + "': " + juce::String(matchedMetadataRowCount) + " matched, " + juce::String(missingMetadataRowCount) + " missing, " + juce::String(staleMetadataRowCount) + " file-size mismatches");
    }
    return sampleCount;
}

void InstrumentSpec::buildCalibrationProfiles() {
    for (const auto& articulation : this->articulationSpecs) {
        if (!articulation->sourceBalanceResolved) {
            articulation->filenameBalance = articulation->balance;
            articulation->sourceBalanceResolved = true;
        }
        articulation->balance = articulation->filenameBalance;
        articulation->balanceOverride.reset();
        const auto* articulationOverride = this->parent != nullptr ? this->parent->sourceOverrides.findArticulationOverride(this->meta.codeRaw, articulation->codeRaw) : nullptr;
        if (articulationOverride != nullptr && articulationOverride->balance.has_value()) articulation->balanceOverride = articulationOverride->balance;
        articulation->sortVelocityLayers();
        const auto boundaryResult = this->applyVelocityUpperBoundaryOverrides(*articulation);
        for (const auto& boundaryMessage : boundaryResult.errors) LOG(Warning, "Kit source override: " + boundaryMessage);
        for (const auto& velocityLayer : articulation->velocityLayerSpecs) KitLibCalibrationHelpers::buildLogicalHitMeasurements(*velocityLayer);
        KitLibCalibrationHelpers::fitMonotonicSourceProfile(*articulation);
        KitLibCalibrationHelpers::finalizeArticulationCalibration(*articulation);
    }
}

ParseResult<int> InstrumentSpec::applyVelocityUpperBoundaryOverrides(ArticulationSpec& articulation) {
    ParseResult<int> result;
    articulation.sortVelocityLayers();
    if (articulation.velocityLayerSpecs.empty()) return result;

    std::vector<uint8_t> namedVelocities;
    namedVelocities.reserve(articulation.velocityLayerSpecs.size());
    for (const auto& velocityLayer : articulation.velocityLayerSpecs) namedVelocities.push_back(velocityLayer->velocity);

    auto effectiveUpperBoundaries = namedVelocities;
    effectiveUpperBoundaries.back() = 127;
    if (this->parent != nullptr) {
        auto resolved = this->parent->sourceOverrides.resolveVelocityUpperBoundaries(this->meta.codeRaw, articulation.codeRaw, namedVelocities);
        for (const auto& message : resolved.errors) result.errors.push_back(message);
        result.isValid = resolved.isValid;
        if (resolved.ok()) effectiveUpperBoundaries = std::move(resolved.value);
    }
    for (size_t layerIdx = 0; layerIdx < articulation.velocityLayerSpecs.size(); ++layerIdx) articulation.velocityLayerSpecs[layerIdx]->selectionUpperVelocity = effectiveUpperBoundaries[layerIdx];
    result.value = static_cast<int>(effectiveUpperBoundaries.size());
    return result;
}

/**  Parses a sample filename <instrumentType>-<size>-<articulation>[optionalExpressionShortName]-{velocity}{sampleVariant}-{micChain}-{otherOptionalParams} */
ParseResult<InstrumentSpec::SampleFileInfo> InstrumentSpec::parseSampleFile(const juce::String& fileName) {
    ParseResult<InstrumentSpec::SampleFileInfo> result;
    const auto fileNameWithoutExt = fileName.upToLastOccurrenceOf(".", false, false);
    juce::StringArray nameParts;
    nameParts.addTokens(fileNameWithoutExt, "-", "");

    if (nameParts.size() < 5) {
        return result.returnWithError("Filename must be in format <instrumentType>-<size>-<articulation>[optionalExpressionShortName]-{velocity}{sampleVariant}-{micChain}-{otherOptionalParams}: " + fileNameWithoutExt);
    }

    juce::String velRRCode = nameParts[3];
    int splitIndex = 0;
    while (splitIndex < velRRCode.length() && juce::CharacterFunctions::isDigit(velRRCode[splitIndex])) {
        splitIndex++;
    }
    if (splitIndex == 0) {
        return result.returnWithError("velocity+sampleVariant code must start with digits (e.g., '001a', '127b', '005'): " + velRRCode);
    }

    juce::String velocityStr = velRRCode.substring(0, splitIndex);
    uint8_t velocity = static_cast<uint8_t>(velocityStr.getIntValue());
    if (velocity <= 0 || velocity > 127) {
        return result.returnWithError("Velocity must be 1-127, got: " + velocityStr);
    }

    juce::String sampleVariant;
    if (splitIndex < velRRCode.length()) {
        sampleVariant = velRRCode.substring(splitIndex);
        if (sampleVariant.length() == 1) {
            if (sampleVariant[0] < 'a' || sampleVariant[0] > 'z') {
                result.addWarning("Round-robin letter should be a-z, got: '" + sampleVariant + "'; treating as absent");
            }
        } else if (sampleVariant.length() > 1) {
            result.addWarning("Round-robin should be single letter, got: '" + sampleVariant + "'; using first letter '" + juce::String(sampleVariant[0]) + "'");
        }
    }

    auto parsedMics = MicSpec::parseMicCodesChain(nameParts[4]);
    if (!parsedMics.ok()) {
        result.addWarning("Mic chain parsing failed: " + parsedMics.error() + "; file will use default routing");
    }

    result.value.velocity = velocity;
    result.value.sampleVariant = sampleVariant;
    result.value.micDescriptors = parsedMics.value;

    // Optional metadata examples: vn4 = -4 dB, on5.5 = advance 5.5 ms, op2_ons_rfs = delay ons and rfs by 2 ms.
    if (nameParts.size() > 5) {
        for (int partIdx = 5; partIdx < nameParts.size(); ++partIdx) {
            const juce::String& part = nameParts[partIdx];
            if (part.isEmpty()) continue;
            
            const juce::juce_wchar paramType = part[0];
            const juce::String paramValue = part.substring(1);
            
            switch (paramType) {
                case 'v': { // manual gain, dB (negative or positive)
                    const auto parsedGainDb = KitLibSampleMetadataHelpers::parseSignedGainDb(paramValue.toLowerCase());
                    if (parsedGainDb.has_value()) result.value.manualGainDb = *parsedGainDb;
                    else result.addWarning("Unknown volume trim format: " + paramValue + "; expected vn<dB> or vp<dB>");
                    break;
                }
                case 'o': { // playback offset in ms: negative advances, positive delays
                    juce::StringArray offsetParts;
                    offsetParts.addTokens(paramValue, "_", "");
                    offsetParts.removeEmptyStrings();
                    std::optional<float> parsedOffsetMs;
                    if (!offsetParts.isEmpty()) parsedOffsetMs = KitLibSampleMetadataHelpers::parseSignedOffsetMs(offsetParts[0].toLowerCase());
                    if (!parsedOffsetMs.has_value()) {
                        result.addWarning("Unknown playback offset format: " + paramValue + "; expected on<ms> or op<ms>");
                        break;
                    }
                    if (offsetParts.size() == 1) {
                        result.value.playbackOffsetMs = *parsedOffsetMs;
                        break;
                    }
                    for (int micIdx = 1; micIdx < offsetParts.size(); ++micIdx) {
                        const auto micCode = offsetParts[micIdx].trim().toLowerCase();
                        const bool knownMic = std::any_of(result.value.micDescriptors.begin(), result.value.micDescriptors.end(), [&micCode](const auto& micDescriptor) {
                            return micDescriptor.raw.compareIgnoreCase(micCode) == 0;
                        });
                        if (knownMic)
                            result.value.playbackOffsetMsByMic[micCode] = *parsedOffsetMs;
                        else
                            result.addWarning("Playback offset references unknown mic code '" + micCode + "'");
                    }
                    break;
                }
                case 'p': { // peak in dB*-10 (negative value, e.g., p19 means -1.9dB) //FIXME: remove obsolete a/p/d/c (where is a?)
                    juce::StringArray peakParts;
                    peakParts.addTokens(paramValue, "_", "");
                    if (peakParts.size() >= 1)
                        result.value.peakDb = static_cast<float>(peakParts[0].getIntValue()) / -10.0f;
                    if (peakParts.size() >= 2)
                        result.value.peakMs = static_cast<float>(peakParts[1].getIntValue());
                    break;
                }
                case 'd': //FIXME: remove obsolete
                    result.value.decayMs = static_cast<float>(paramValue.getIntValue());
                    break;
                case 'c': // decay code (base62 encoded) //FIXME: remove obsolete
                    result.value.decayCode = paramValue;
                    break;
                default:
                    LOG(Notice, "Unknown parameter type '" + juce::String::charToString(paramType) + "' in optional parameters of file '" + fileNameWithoutExt + "': '" + part + "'; skipping this parameter");
                    break;
            }
        }
    }

    return result;
}


const ArticulationSpec* InstrumentSpec::findArticulationById(const juce::String& id) const {
    const ArticulationSpec* matched = nullptr;
    int matchCount = 0;
    for (const auto& existing : this->articulationSpecs) {
        if (existing == nullptr) continue;
        if (!KitLibIdentityHelpers::candidateMatches(existing->getIdentityCandidates(), id)) continue;
        matched = existing.get();
        ++matchCount;
    }
    return matchCount == 1 ? matched : nullptr;
}

juce::String InstrumentSpec::getPreferredArticulationId(const ArticulationSpec& articulation) const {
    for (const auto& candidate : articulation.getIdentityCandidates()) {
        int matchCount = 0;
        for (const auto& existing : this->articulationSpecs) {
            if (existing != nullptr && KitLibIdentityHelpers::candidateMatches(existing->getIdentityCandidates(), candidate)) ++matchCount;
        }
        if (matchCount == 1) return candidate;
    }
    return articulation.codeRaw;
}

/** Parses a drum kit folder name, like "Studio Kit by Someone [CC-BY-4.0]" */
ParseResult<DrumKitSpec::MetaInfo> DrumKitSpec::parseInfo(const juce::String& dirName) {
    ParseResult<MetaInfo> result;

    juce::String processed = dirName.trim();
    result.value.codeRaw = processed;
    result.value.name = processed;
    result.value.author = "";
    result.value.license = "";

    auto wrapSegm = extractWrappedSegment(processed, "[", "]");
    result.value.license = wrapSegm.wrapped;
    processed = wrapSegm.prefix + " " + wrapSegm.suffix;

    auto parts = splitByString(processed, " by ");
    result.value.name   = parts[0].trim();
    result.value.author = parts.size() > 1 ? parts[1].trim() : "";

    return result;
}

std::vector<juce::String> DrumKitSpec::getIdentityCandidates(const DrumKitSpec::MetaInfo& meta) {
    std::vector<juce::String> candidates;
    const auto name = meta.name.trim();
    const auto author = meta.author.trim();
    const auto license = meta.license.trim();
    KitLibIdentityHelpers::addCandidate(candidates, name);
    if (author.isNotEmpty()) KitLibIdentityHelpers::addCandidate(candidates, name + " by " + author);
    if (license.isNotEmpty()) {
        const auto base = author.isNotEmpty() ? name + " by " + author : name;
        KitLibIdentityHelpers::addCandidate(candidates, base + " [" + license + "]");
    }
    KitLibIdentityHelpers::addCandidate(candidates, meta.codeRaw);
    return candidates;
}

int DrumKitSpec::scanInstruments() {
    const auto subDirs = meta.rootDir.findChildFiles(juce::File::findDirectories, false, "*");

    for (const auto& subDir : subDirs) {
        if (!subDir.isDirectory()) {
            LOG(Info, "Parse InstrumentSpec failed: rootDir is not a directory: " + subDir.getFullPathName());
            continue;
        }

        auto parsedInfo = InstrumentSpec::parseInfo(subDir.getFileName());
        if (parsedInfo.ok()) {
            parsedInfo.value.rootDir = subDir;
            if (findInstrument(parsedInfo.value.codeRaw) == nullptr) {
                this->addInstrument(parsedInfo.value);
            }
        } else DBG(parsedInfo.error());
    }

    return (int) this->instrumentSpecsMap.size();
}

InstrumentSpec* DrumKitSpec::findInstrument(const juce::String& id) {
    auto it = this->instrumentSpecsMap.find(id);
    if (it != this->instrumentSpecsMap.end())
        return it->second.get();
    return nullptr;
}


const InstrumentSpec* DrumKitSpec::findInstrumentById(const juce::String& id) const {
    const InstrumentSpec* matched = nullptr;
    int matchCount = 0;
    for (auto& existing : this->instrumentSpecsMap) {
        auto* instrument = existing.second.get();
        if (instrument == nullptr) continue;
        if (!KitLibIdentityHelpers::candidateMatches(InstrumentSpec::getIdentityCandidates(instrument->meta), id)) continue;
        matched = instrument;
        ++matchCount;
    }
    return matchCount == 1 ? matched : nullptr;
}

juce::String DrumKitSpec::getPreferredInstrumentId(const InstrumentSpec& instrument) const {
    for (const auto& candidate : InstrumentSpec::getIdentityCandidates(instrument.meta)) {
        int matchCount = 0;
        for (const auto& existing : this->instrumentSpecsMap) {
            const auto* otherInstrument = existing.second.get();
            if (otherInstrument != nullptr && KitLibIdentityHelpers::candidateMatches(InstrumentSpec::getIdentityCandidates(otherInstrument->meta), candidate)) ++matchCount;
        }
        if (matchCount == 1) return candidate;
    }
    return instrument.meta.codeRaw;
}
std::vector<InstrumentSpec*> DrumKitSpec::getInstrumentsByType(InstrumentSpec::Type type) const {
    std::vector<InstrumentSpec*> result;
    for (const auto&[id, instPtr] : this->instrumentSpecsMap) {
        auto* inst = instPtr.get();
        if (inst && inst->meta.type == type)
            result.push_back(inst);
    }
    std::sort(result.begin(), result.end(), [](InstrumentSpec* a, InstrumentSpec* b){
        return a->meta.codeRaw < b->meta.codeRaw;
    });
    return result;
}



bool LibraryManager::validateKitsDir(const juce::File& dir) {
    if (!dir.isDirectory()) {
        LOG(Info, "Warning: kitsDir is not a directory: " + dir.getFullPathName());
        return false;
    }
    return true;
}

void LibraryManager::setKitsDir(const juce::File& dir) {
    if (!this->validateKitsDir(dir))
        return;
    this->rootDir = dir;
    this->kitSpecs.clear();
}

int LibraryManager::scanAll() {
    this->kitSpecs.clear();

    if (!this->validateKitsDir(this->rootDir)) return 0;
    
    LOG(Info, "LibraryManager: Scanning for drum kits in " + this->rootDir.getFullPathName());
    
    int totalKits = 0;
    int totalInstruments = 0;
    int totalSamples = 0;
    InstrumentSpec::ScanStats globalStats;

    for (const juce::DirectoryEntry& entry : juce::RangedDirectoryIterator(this->rootDir, false, "*", juce::File::TypesOfFileToFind::findDirectories)) {
        const juce::File& kitDir = entry.getFile();
        const double kitScanStartTimeMs = juce::Time::getMillisecondCounterHiRes();
        if (!kitDir.isDirectory()) {
            LOG(Info, "Parse DrumKitSpec failed: rootDir is not a directory: " + kitDir.getFullPathName());
            continue;
        }

        auto parsedInfo = DrumKitSpec::parseInfo(kitDir.getFileName());
        if (parsedInfo.ok()) {
            parsedInfo.value.rootDir = kitDir;
        } else {
            LOG(Warning, "Could not parse kitDir '" + kitDir.getFullPathName() + "': " + parsedInfo.error());
            continue;
        }

        auto kit = std::make_unique<DrumKitSpec>(parsedInfo.value);
        const double metadataStartTimeMs = juce::Time::getMillisecondCounterHiRes();
        auto metadataResult = kit->sampleMetadata.loadFromKitRoot(kitDir);
        auto overrideResult = kit->sourceOverrides.loadFromKitRoot(kitDir);
        const double metadataElapsedMs = juce::Time::getMillisecondCounterHiRes() - metadataStartTimeMs;
        for (const auto& metadataMessage : metadataResult.errors) LOG(Warning, "Kit metadata: " + metadataMessage);
        for (const auto& overrideMessage : overrideResult.errors) LOG(Warning, "Kit source override: " + overrideMessage);
        if (metadataResult.ok() && kit->sampleMetadata.isActive()) LOG(Info, "Loaded " + juce::String(metadataResult.value) + " metadata.csv rows for kit " + kitDir.getFileName() + " in " + juce::String(metadataElapsedMs, 1) + " ms");
        if (overrideResult.ok() && kit->sourceOverrides.isActive()) LOG(Info, "Loaded " + juce::String(overrideResult.value) + " source override values for kit " + kitDir.getFileName());
        int instrsInKit = kit->scanInstruments();
        int samplesInKit = 0;

        InstrumentSpec::ScanStats stats;
        for (auto&[id, instPtr] : kit->instrumentSpecsMap) {
            auto samplesInInstr = instPtr->scanInstrumentSamples(&stats);
            if (samplesInInstr > 0) {
                samplesInKit += samplesInInstr;
                totalSamples += samplesInInstr;
                totalInstruments++;
            }
        }
        this->kitSpecs.push_back(std::move(kit));

        for (const auto& [k, v] : stats.dimensionCodeCounts) globalStats.dimensionCodeCounts[k] += v;
        for (const auto& [k, v] : stats.micCodeCounts) globalStats.micCodeCounts[k] += v;
        for (const auto& [k, v] : stats.sampleVariantCounts) globalStats.sampleVariantCounts[k] += v;
        for (const auto& [k, v] : stats.velocityCounts) globalStats.velocityCounts[k] += v;
        totalKits++;
        const double kitScanElapsedMs = juce::Time::getMillisecondCounterHiRes() - kitScanStartTimeMs;
        LOG(Info, "Scanned kit: " + kitDir.getFileName() + ", " + juce::String(instrsInKit) + " instruments, " + juce::String(samplesInKit) + " samples in " + juce::String(kitScanElapsedMs, 1) + " ms");
    }

    if (LOG_ENABLED(Info)) {
        LOG(Info, "Dimension Code Usage:");
        for (const auto& [key, count] : globalStats.dimensionCodeCounts) LOG(Info, "  " + key + ": " + juce::String(count) + " articulation folders");

        LOG(Info, "Mic Code Usage:");
        for (const auto& [key, count] : globalStats.micCodeCounts) LOG(Info, "  " + key + ": " + juce::String(count) + " sample files");

        LOG(Info, "Sample Variant Usage:");
        for (const auto& [key, count] : globalStats.sampleVariantCounts) LOG(Info, "  " + key + ": " + juce::String(count) + " sample files");

        LOG(Info, "Velocity Usage:");
        for (const auto& [key, count] : globalStats.velocityCounts) LOG(Info, "  " + juce::String(key) + ": " + juce::String(count) + " sample files");

        LOG(Info, "Total: " + juce::String(totalKits) + " kits, " + juce::String(totalInstruments) + " instruments, " + juce::String(totalSamples) + " sample files");
    }
    return totalKits;
}

DrumKitSpec* LibraryManager::getKit(int index) {
    if (index < 0 || index >= static_cast<int>(this->kitSpecs.size()))
        return nullptr;
    return this->kitSpecs[static_cast<size_t>(index)].get();
}


const DrumKitSpec* LibraryManager::findKitById(const juce::String& id) const {
    const DrumKitSpec* matched = nullptr;
    int matchCount = 0;
    for (auto& kit : this->kitSpecs) {
        if (kit == nullptr) continue;
        if (!KitLibIdentityHelpers::candidateMatches(LibraryManager::getIdentityCandidates(kit->meta), id)) continue;
        matched = kit.get();
        ++matchCount;
    }
    return matchCount == 1 ? matched : nullptr;
}

juce::String LibraryManager::getPreferredKitId(const DrumKitSpec& kit) const {
    for (const auto& candidate : LibraryManager::getIdentityCandidates(kit.meta)) {
        int matchCount = 0;
        for (const auto& existing : this->kitSpecs) {
            if (existing != nullptr && KitLibIdentityHelpers::candidateMatches(LibraryManager::getIdentityCandidates(existing->meta), candidate)) ++matchCount;
        }
        if (matchCount == 1) return candidate;
    }
    return kit.meta.codeRaw;
}

std::vector<juce::String> LibraryManager::getIdentityCandidates(const DrumKitSpec::MetaInfo& meta) {
    return DrumKitSpec::getIdentityCandidates(meta);
}
DrumKitSpec* LibraryManager::findKit(const juce::String& name) {
    const auto needle = name.trim().toLowerCase();
    for (auto& kit : this->kitSpecs)
        if (kit->meta.name.trim().toLowerCase() == needle || kit->meta.codeRaw.trim().toLowerCase() == needle)
            return kit.get();
    return nullptr;
}
