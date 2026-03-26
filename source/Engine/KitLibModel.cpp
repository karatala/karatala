#include "KitLibModel.h"

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
            const float denom = juce::jmax(0.001f, pointRelDb - prevRelDb);
            const float t = juce::jlimit(0.0f, 1.0f, (targetRelDb - prevRelDb) / denom);
            const float pointMs = this->points[static_cast<size_t>(i)];
            return prevMs + t * (pointMs - prevMs);
        }
    }

    return -1.0f;
}

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

    if (!juce::String("dor").containsChar(type)) {
        return result.returnWithError("Invalid mic type: '" + juce::String::charToString(type) + "' in '" + code + "'. Expected one of: d (direct), o (overhead), r (room)");
    }

    if (type == 'd' && !juce::String("ustbio").containsChar(pos)) {
        return result.returnWithError("Invalid position for direct mic: '" + juce::String::charToString(pos) + "' in '" + code + "'. Expected: u (sub), s (side), t (top), b (bottom), i (in), o (out)");
    }
    if (type == 'o' && !juce::String("nw").containsChar(pos)) {
        return result.returnWithError("Invalid position for overhead mic: '" + juce::String::charToString(pos) + "' in '" + code + "'. Expected: n (normal), w (wide)");
    }
    if (type == 'r' && !juce::String("cf").containsChar(pos)) {
        return result.returnWithError("Invalid position for room mic: '" + juce::String::charToString(pos) + "' in '" + code + "'. Expected: c (close), f (far)");
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


ParseResult<InstrumentSpec::MetaInfo> InstrumentSpec::parseInfo(const juce::String& dirName) {
    ParseResult<MetaInfo> result;
    juce::String processed = dirName.trim();
    auto parts = juce::StringArray::fromTokens(processed, "-", "");
    result.value.name = parts[0].trim();
    if (parts.size() > 1)
        result.value.size = parts[1].trim();
    if (parts.size() > 2) {
        parts.removeRange(0, 2);
        result.value.description = parts.size() > 0 ? parts.joinIntoString(" ").trim() : "";
    }
    result.value.type = InstrumentSpec::typeFromString(result.value.name);
    result.value.codeRaw = processed;

    return result;
}

bool InstrumentSpec::isAudioFile(const juce::File& file) {
    juce::StringArray audioFileExtensions = { ".wav", ".flac", ".aiff", ".mp3", ".ogg" }; //TODO: refactor dynamical support from plugin's config.yaml
    juce::String ext = file.getFileExtension().toLowerCase();
    for (int i = 0; i < audioFileExtensions.size(); ++i)
        if (ext == audioFileExtensions[i])
            return true;
    return false;
}

int InstrumentSpec::scanInstrumentSamples(ScanStats* stats) {
    const auto articulationDirs = meta.rootDir.findChildFiles(juce::File::findDirectories, false, "*.*");
    int sampleCount = 0;
    for (const auto& artDir : articulationDirs) {
        if (!artDir.isDirectory())
            continue;

        auto artName = artDir.getFileName();
        auto parts = juce::StringArray::fromTokens(artName, "-", "");

        ArticulationSpec* artSpec = this->getOrAddArticulation(parts[0].trim());

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
            if (!sampleFile.existsAsFile()) {
                LOG(Info, "File does not exist: " + sampleFile.getFullPathName());
                continue;
            }

            const auto fileName = sampleFile.getFileName();
            if (!isAudioFile(sampleFile)) {
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

            // Add one sample variant per mic chain. Track channel cursor for multi-channel files.
            int channelCursor = 0;
            for (size_t i = 0; i < parsedInfo.value.micDescriptors.size(); i++) {
                uint8_t variantIdx = 0;
                if (!parsedInfo.value.sampleVariant.isEmpty())
                    variantIdx = SampleVariantSpec::letterToIdx(parsedInfo.value.sampleVariant[0]);

                const auto& micDesc = parsedInfo.value.micDescriptors[i];
                auto* sv = velSpec->getOrAddSampleVariant(sampleFile, variantIdx, micDesc, channelCursor);
                sv->peakDb = parsedInfo.value.peakDb;
                sv->peakMs = parsedInfo.value.peakMs;
                sv->decayMs = parsedInfo.value.decayMs;
                sv->decayCode = parsedInfo.value.decayCode;
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
    return sampleCount;
}

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

    // Parse optional metadata parameters if present (format: p{peak_db*-10}_{peak_ms}-d{decay_ms}-c{decay_code})
    // Example: hihat-14-bow[half]-p556_51-d1650-2U_c26_2g_3S_1s_1C_26_42_6s_3S.flac
    if (nameParts.size() > 5) {
        for (int i = 5; i < nameParts.size(); ++i) {
            const juce::String& part = nameParts[i];
            if (part.isEmpty()) continue;
            
            const juce::juce_wchar paramType = part[0];
            const juce::String paramValue = part.substring(1);
            
            switch (paramType) {
                case 'p': { // peak in dB*-10 (negative value, e.g., p19 means -1.9dB)
                    juce::StringArray peakParts;
                    peakParts.addTokens(paramValue, "_", "");
                    if (peakParts.size() >= 1)
                        result.value.peakDb = static_cast<float>(peakParts[0].getIntValue()) / -10.0f;
                    if (peakParts.size() >= 2)
                        result.value.peakMs = static_cast<float>(peakParts[1].getIntValue());
                    break;
                }
                case 'd':
                    result.value.decayMs = static_cast<float>(paramValue.getIntValue());
                    break;
                case 'c': // decay code (base62 encoded)
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

int DrumKitSpec::scanInstruments() {
    const auto subDirs = meta.rootDir.findChildFiles(juce::File::findDirectories, false, "*.*");

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
        LOG(Info, "Scanned kit: " + kitDir.getFileName() + ", " + juce::String(instrsInKit) + " instruments, " + juce::String(samplesInKit) + " samples");
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
    return this->kitSpecs[index].get();
}

DrumKitSpec* LibraryManager::findKit(const juce::String& name) {
    for (auto& kit : this->kitSpecs)
        if (kit->meta.name == name)
            return kit.get();
    return nullptr;
}
