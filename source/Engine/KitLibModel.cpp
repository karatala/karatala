#include "KitLibModel.h"

using namespace KitLib;

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
    result.value.code = processed;

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

int InstrumentSpec::scanInstrumentSamples() {
    const auto articulationDirs = meta.rootDir.findChildFiles(juce::File::findDirectories, false, "*.*");
    int sampleCount = 0;
    for (const auto& artDir : articulationDirs) {
        if (!artDir.isDirectory())
            continue;

        auto artName = artDir.getFileName();
        auto parts = juce::StringArray::fromTokens(artName, "-", "");
        auto wrapRes = getWrappedValue(parts[0].trim(), "[", "]");
        ArticulationSpec* artSpec = this->getOrAddArticulation(wrapRes.cleaned, wrapRes.wrapped);

        for (int i = 1; i < parts.size(); ++i) {
            auto expDimPart = parts[i];
            auto expDimCodeEnd = expDimPart.indexOfAnyOf("0123456789");
            if (expDimCodeEnd > 0) {
                juce::String expDimCode = expDimPart.substring(0, expDimCodeEnd);
                juce::String expDimValue = expDimPart.substring(expDimCodeEnd);
                if (KitLib::ArticulationSpec::ExpressionDimension::stringToType(expDimCode) == KitLib::ArticulationSpec::ExpressionDimension::Type::Unknown)
                    LOG(Warning, "Unknown dimension code '" + expDimCode + "' in articulation folder '" + artDir.getFullPathName() + "'");
                artSpec->addExpressionDimensionValue(expDimCode, static_cast<uint8_t>(expDimValue.getIntValue()));
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

            // Add one sample variant per mic chain
            for (size_t i = 0; i < parsedInfo.value.micDescriptors.size(); i++) {
                uint8_t variantIdx = 0;
                if (!parsedInfo.value.sampleVariant.isEmpty())
                    variantIdx = SampleVariantSpec::letterToIdx(parsedInfo.value.sampleVariant[0]);

                velSpec->getOrAddSampleVariant(sampleFile, variantIdx, parsedInfo.value.micDescriptors[i]);
                sampleCount++;
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

    if (nameParts.size() < 2) {
        return result.returnWithError("Filename must be in format {velocity}{sampleVariant}-{micChain}: " + fileNameWithoutExt);
    }

    juce::String velRRCode = nameParts[0];
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

    auto parsedMics = MicSpec::parseMicCodesChain(nameParts[1]);
    if (!parsedMics.ok()) {
        result.addWarning("Mic chain parsing failed: " + parsedMics.error() + "; file will use default routing");
    }

    result.value.velocity = velocity;
    result.value.sampleVariant = sampleVariant;
    result.value.micDescriptors = parsedMics.value;

    return result;
}

ParseResult<DrumKitSpec::MetaInfo> DrumKitSpec::parseInfo(const juce::String& dirName) {
    ParseResult<MetaInfo> result;

    juce::String processed = dirName.trim();
    result.value.code = processed;
    result.value.name = processed;
    result.value.author = "";
    result.value.license = "";

    auto wrapRes = getWrappedValue(processed, "[", "]");
    result.value.license = wrapRes.wrapped;
    processed = wrapRes.cleaned;

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
            if (findInstrument(parsedInfo.value.code) == nullptr) {
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
        return a->meta.code < b->meta.code;
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
    std::unordered_map<juce::String, int> dimensionCodeCounts;
    std::unordered_map<juce::String, int> micCodeCounts;
    std::unordered_map<juce::String, int> sampleVariantCounts;
    std::unordered_map<int, int> velocityCounts;

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

        for (auto&[id, instPtr] : kit->instrumentSpecsMap) {
            auto samplesInInstr = instPtr->scanInstrumentSamples();
            if (samplesInInstr > 0) {
                samplesInKit += samplesInInstr;
                totalSamples += samplesInInstr;
                totalInstruments++;

                for (const auto& artSpec : instPtr->articulationSpecs) {
                    for (const auto& velSpec : artSpec->velocityLayerSpecs) {
                        velocityCounts[velSpec->velocity]++;
                        for (const auto& varSpec : velSpec->sampleVariantSpecs) {
                            micCodeCounts[varSpec->micSpec.raw]++;
                            sampleVariantCounts[juce::String(varSpec->letter)]++;
                        }
                    }
                }
            }
        }
        this->kitSpecs.push_back(std::move(kit));
        totalKits++;
        LOG(Info, "Scanned kit: " + kitDir.getFileName() + ", " + juce::String(instrsInKit) + " instruments, " + juce::String(samplesInKit) + " samples");
    
    }

    LOG(Info, "Dimension Code Usage:");
    for (const auto& [key, count] : dimensionCodeCounts) LOG(Info, "  " + key + ": " + juce::String(count));

    LOG(Info, "Mic Code Usage:");
    for (const auto& [key, count] : micCodeCounts) LOG(Info, "  " + key + ": " + juce::String(count));

    LOG(Info, "Sample Variant Usage:");
    for (const auto& [key, count] : sampleVariantCounts) LOG(Info, "  " + key + ": " + juce::String(count));

    LOG(Info, "Velocity Usage:");
    for (const auto& [key, count] : velocityCounts) LOG(Info, "  " + juce::String(key) + ": " + juce::String(count));

    LOG(Info, "Total: " + juce::String(totalKits) + " kits, " + juce::String(totalInstruments) + " instruments, " + juce::String(totalSamples) + " samples");

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
