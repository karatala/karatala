#include "KitLibModel.h"

using namespace KitLib;

ParseResult<MicSpecs> MicSpecs::parseMicCode(const juce::String& code) {
    ParseResult<MicSpecs> result;
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
    result.value.channels = result.value.format == MicSpecs::Format::Mono ? 1 : 2;

    return result;
}

ParseResult<std::vector<MicSpecs>> MicSpecs::parseMicCodesChain(const juce::String& codeChain) {
    ParseResult<std::vector<MicSpecs>> result;

    if (codeChain.isEmpty()) {
        return result.returnWithError("Mic chain code is empty");
    }

    juce::StringArray tokens;
    tokens.addTokens(codeChain, "_", "");
    tokens.removeEmptyStrings();

    for (const auto& token : tokens) {
        MicSpecs mic;
        juce::String tokenError;

        auto item = MicSpecs::parseMicCode(token);
        if (item.ok()) {
            result.value.push_back(item.value);
        } else {
            result.addWarning("Skipping invalid mic code '" + token + "' in chain '" + codeChain + "': '" + item.error() + "'");
        }
    }

    return result;
}


ParseResult<InstrumentSpecs::MetaInfo> InstrumentSpecs::parseInfo(const juce::String& dirName) {
    ParseResult<MetaInfo> result;
    juce::StringArray parts;
    juce::String processed = dirName.trim();
    parts.addTokens(processed, "-", "");
    result.value.name = parts[0].trim();
    if (parts.size() > 1)
        result.value.size = parts[1].trim();
    if (parts.size() > 2) {
        parts.removeRange(0, 2);
        result.value.description = parts.size() > 0 ? parts.joinIntoString(" ").trim() : "";
    }
    result.value.type = InstrumentSpecs::typeFromString(result.value.name);
    result.value.code = processed;

    return result;
}

bool InstrumentSpecs::isAudioFile(const juce::File& file) {
    juce::StringArray audioFileExtensions = { ".wav", ".flac", ".aiff", ".mp3", ".ogg" }; //TODO: refactor dynamical support from plugin's config.yaml
    juce::String ext = file.getFileExtension().toLowerCase();
    for (int i = 0; i < audioFileExtensions.size(); ++i) {
        if (ext == audioFileExtensions[i]) {
            return true;
        }
    }
    return false;
}

int InstrumentSpecs::scanInstrumentSamples() {
    const auto articulationDirs = meta.rootDir.findChildFiles(juce::File::findDirectories, false, "*.*");
    int sampleCount = 0;
    for (const auto& artDir : articulationDirs) {
        if (!artDir.isDirectory())
            continue;

        auto artName = artDir.getFileName();
        juce::StringArray artParts;
        artParts.addTokens(artName, "-", "");

        juce::String articulation = artParts[0].trim();

        ArticulationSpecs* articulationObj = getOrAddArticulation(articulation);

        for (int i = 1; i < artParts.size(); ++i) {
            auto dimPart = artParts[i];
            auto dimCodeEnd = dimPart.indexOfAnyOf("0123456789");
            if (dimCodeEnd > 0) {
                juce::String dimCode = dimPart.substring(0, dimCodeEnd);
                juce::String dimValue = dimPart.substring(dimCodeEnd);
                articulationObj->addDimensionValue(dimCode, static_cast<uint8_t>(dimValue.getIntValue()));
            }
        }

        const auto sampleFiles = artDir.findChildFiles(juce::File::findFiles, false, "*.*");
        for (const auto& sampleFile : sampleFiles) {
            if (!sampleFile.existsAsFile()) {
                DBG("File does not exist: " + sampleFile.getFullPathName());
                continue;
            }

            const auto fileName = sampleFile.getFileName();
            if (!isAudioFile(sampleFile)) {
                DBG("Skipping non-audio file: " + fileName);
                continue;
            }

    
            auto parsedInfo = this->parseSampleFile(fileName);
            if (parsedInfo.ok()) {
                parsedInfo.value.file = sampleFile;
            } else {
                DBG("WARN: Could not parse filename '" + fileName + "': " + parsedInfo.error());
                continue;
            }

            auto* velocityLayer = articulationObj->getOrAddVelocityLayer(parsedInfo.value.velocity);

            // Add one sample variant per mic chain
            for (size_t i = 0; i < parsedInfo.value.micDescriptors.size(); i++) {
                uint8_t variantIdx = 0;
                if (!parsedInfo.value.sampleVariant.isEmpty())
                    variantIdx = SampleVariantSpecs::letterToIdx(parsedInfo.value.sampleVariant[0]);

                auto sampleVariant = std::make_unique<SampleVariantSpecs>(sampleFile, variantIdx, parsedInfo.value.micDescriptors[i]);
                velocityLayer->addSampleVariant(std::move(sampleVariant));
                sampleCount++;
            }
        }
    }
    return sampleCount;
}

ParseResult<InstrumentSpecs::SampleFileInfo> InstrumentSpecs::parseSampleFile(const juce::String& fileName) {
    ParseResult<InstrumentSpecs::SampleFileInfo> result;
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

    auto parsedMics = MicSpecs::parseMicCodesChain(nameParts[1]);
    if (!parsedMics.ok()) {
        result.addWarning("Mic chain parsing failed: " + parsedMics.error() + "; file will use default routing");
    }

    result.value.velocity = velocity;
    result.value.sampleVariant = sampleVariant;
    result.value.micDescriptors = parsedMics.value;

    return result;
}

ParseResult<DrumKitSpecs::MetaInfo> DrumKitSpecs::parseInfo(const juce::String& dirName) {
    ParseResult<MetaInfo> result;

    juce::String processed = dirName.trim();
    result.value.code = processed;
    result.value.name = processed;
    result.value.author = "";
    result.value.license = "";

    int lIdx = processed.indexOf("[");
    int rIdx = processed.indexOf("]");
    if (lIdx >= 0 && rIdx > lIdx) {
        result.value.license = processed.substring(lIdx + 1, rIdx).trim();
        processed = (processed.substring(0, lIdx) + processed.substring(rIdx + 1)).trim();
    }

    int sepIdx = processed.indexOf(" by ");
    if (sepIdx >= 0) {
        result.value.name = processed.substring(0, sepIdx).trim();
        result.value.author = processed.substring(sepIdx + 4).trim();
    } else {
        result.value.name = processed.trim();
    }

    return result;
}

int DrumKitSpecs::scanInstruments() {
    const auto subDirs = meta.rootDir.findChildFiles(juce::File::findDirectories, false, "*.*");

    for (const auto& subDir : subDirs) {
        if (!subDir.isDirectory()) {
            DBG("Parse InstrumentSpecs failed: rootDir is not a directory: " + subDir.getFullPathName());
            continue;
        }

        auto parsedInfo = InstrumentSpecs::parseInfo(subDir.getFileName());
        if (parsedInfo.ok()) {
            parsedInfo.value.rootDir = subDir;
            if (findInstrument(parsedInfo.value.code) == nullptr) {
                auto newInst = std::make_unique<InstrumentSpecs>(this, parsedInfo.value);
                instruments[parsedInfo.value.code] = std::move(newInst);
            }
        } else DBG(parsedInfo.error());
    }

    return (int) instruments.size();
}

InstrumentSpecs* DrumKitSpecs::findInstrument(const juce::String& code) {
    auto it = instruments.find(code);
    if (it != instruments.end())
        return it->second.get();
    return nullptr;
}

std::vector<InstrumentSpecs*> DrumKitSpecs::getInstrumentsByType(InstrumentSpecs::Type type) const {
    std::vector<InstrumentSpecs*> result;
    for (const auto& pair : instruments) {
        auto* inst = pair.second.get();
        if (inst && inst->meta.type == type)
            result.push_back(inst);
    }
    std::sort(result.begin(), result.end(), [](InstrumentSpecs* a, InstrumentSpecs* b){
        return a->meta.code < b->meta.code;
    });
    return result;
}



bool LibraryManager::validateKitsDir(const juce::File& dir) {
    if (!dir.isDirectory()) {
        DBG("Warning: kitsDir is not a directory: " + dir.getFullPathName());
        return false;
    }
    return true;
}

void LibraryManager::setKitsDir(const juce::File& dir) {
    if (!this->validateKitsDir(dir))
        return;
    this->mKitsDir = dir;
    this->mKits.clear();
}

int LibraryManager::scanAll() {
    this->mKits.clear();

    if (!this->validateKitsDir(this->mKitsDir)) return 0;
    
    DBG("INFO: LibraryManager: Scanning for drum kits in " + this->mKitsDir.getFullPathName());
    
    int totalKits = 0;
    int totalInstruments = 0;
    int totalSamples = 0;
    std::unordered_map<juce::String, int> dimensionCodeCounts;
    std::unordered_map<juce::String, int> micCodeCounts;
    std::unordered_map<juce::String, int> sampleVariantCounts;
    std::unordered_map<int, int> velocityCounts;

    for (const juce::DirectoryEntry& entry : juce::RangedDirectoryIterator(this->mKitsDir, false, "*", juce::File::TypesOfFileToFind::findDirectories)) {
        const juce::File& kitDir = entry.getFile();
        if (!kitDir.isDirectory()) {
            DBG("Parse DrumKitSpecs failed: rootDir is not a directory: " + kitDir.getFullPathName());
            continue;
        }

        auto parsedInfo = DrumKitSpecs::parseInfo(kitDir.getFileName());
        if (parsedInfo.ok()) {
            parsedInfo.value.rootDir = kitDir;
        } else {
            DBG("WARN: Could not parse kitDir '" + kitDir.getFullPathName() + "': " + parsedInfo.error());
            continue;
        }

        auto kit = std::make_unique<DrumKitSpecs>(parsedInfo.value);
        int instrsInKit = kit->scanInstruments();
        int samplesInKit = 0;

        for (auto& pair : kit->instruments) {
            auto samplesInInstr = pair.second->scanInstrumentSamples();
            if (samplesInInstr > 0) {
                samplesInKit += samplesInInstr;
                totalSamples += samplesInInstr;
                totalInstruments++;

                for (const auto& articulation : pair.second->articulations) {
                    for (const auto& velocityLayer : articulation->velocityLayers) {
                        velocityCounts[velocityLayer->velocity]++;
                        for (const auto& sampleVariant : velocityLayer->sampleVariants) {
                            micCodeCounts[sampleVariant->micSpecs.raw]++;
                            sampleVariantCounts[juce::String(sampleVariant->letter)]++;
                        }
                    }
                }
            }
        }
        this->mKits.push_back(std::move(kit));
        totalKits++;
        DBG("INFO: Scanned kit: " + kitDir.getFileName() + ", " + juce::String(instrsInKit) + " instruments, " + juce::String(samplesInKit) + " samples");
    
    }

    DBG("INFO: Dimension Code Usage:");
    for (const auto& [key, count] : dimensionCodeCounts) DBG("  " + key + ": " + juce::String(count));

    DBG("INFO: Mic Code Usage:");
    for (const auto& [key, count] : micCodeCounts) DBG("  " + key + ": " + juce::String(count));

    DBG("INFO: Sample Variant Usage:");
    for (const auto& [key, count] : sampleVariantCounts) DBG("  " + key + ": " + juce::String(count));

    DBG("INFO: Velocity Usage:");
    for (const auto& [key, count] : velocityCounts) DBG("  " + juce::String(key) + ": " + juce::String(count));

    DBG("INFO: Total: " + juce::String(totalKits) + " kits, " + juce::String(totalInstruments) + " instruments, " + juce::String(totalSamples) + " samples");

    return totalKits;
}

DrumKitSpecs* LibraryManager::getKit(int index) {
    if (index < 0 || index >= static_cast<int>(this->mKits.size()))
        return nullptr;
    return this->mKits[index].get();
}

DrumKitSpecs* LibraryManager::findKit(const juce::String& name) {
    for (auto& kit : this->mKits)
        if (kit->meta.name == name)
            return kit.get();
    return nullptr;
}
