#include "DrumKitModel.h"

using namespace juce;

ParseResult<MicDescriptor> MicDescriptor::tryCreateFromCode(const String& code) {
    ParseResult<MicDescriptor> result;
    const int codelength = code.length();
    if (codelength < 3 || codelength > 4) {
        return result.returnWithError("MIC code must be 3 or 4 characters long: '" + code + "'");
    }

    const juce_wchar type = code[0];
    const juce_wchar pos = code[1];
    const juce_wchar format = code[2];
    const juce::String variant = (codelength == 4 ? code.substring(3) : "");

    if (!String("dor").containsChar(type)) {
        return result.returnWithError("Invalid mic type: '" + String::charToString(type) + "' in '" + code + "'. Expected one of: d (direct), o (overhead), r (room)");
    }

    if (type == 'd' && !String("ustbio").containsChar(pos)) {
        return result.returnWithError("Invalid position for direct mic: '" + String::charToString(pos) + "' in '" + code + "'. Expected: u (sub), s (side), t (top), b (bottom), i (in), o (out)");
    }
    if (type == 'o' && !String("nw").containsChar(pos)) {
        return result.returnWithError("Invalid position for overhead mic: '" + String::charToString(pos) + "' in '" + code + "'. Expected: n (normal), w (wide)");
    }
    if (type == 'r' && !String("cf").containsChar(pos)) {
        return result.returnWithError("Invalid position for room mic: '" + String::charToString(pos) + "' in '" + code + "'. Expected: c (close), f (far)");
    }

    if (!String("msi").containsChar(format)) {
        return result.returnWithError("Invalid mic format: '" + String::charToString(format) + "' in '" + code + "'. Expected: m (mono), s (stereo), i (mid-side)");
    }

    MicDescriptor md;
    md.raw = code;
    md.type = typeFromChar(type);
    md.position = positionFromChar(pos);
    md.format = formatFromChar(format);
    md.variant = variant;
    md.code = code.substring(0, 2);
    result.value = md;

    return result;
}

ParseResult<std::vector<MicDescriptor>> MicDescriptor::parseMicCodesChain(const String& codeChain) {
    ParseResult<std::vector<MicDescriptor>> result;

    if (codeChain.isEmpty()) {
        return result.returnWithError("Mic chain code is empty");
    }

    StringArray tokens;
    tokens.addTokens(codeChain, "_", "");
    tokens.removeEmptyStrings();

    for (const auto& token : tokens) {
        MicDescriptor mic;
        String tokenError;

        auto item = MicDescriptor::tryCreateFromCode(token);
        if (item.ok()) {
            result.value.push_back(item.value);
        } else {
            result.addWarning("Skipping invalid mic code '" + token + "' in chain '" + codeChain + "': '" + item.error() + "'");
        }
    }

    return result;
}

ParseResult<DrumKitInstrument::MetaInfo> DrumKitInstrument::parseInfo(const File& dir) {
    ParseResult<MetaInfo> result;

    if (!dir.isDirectory()) {
        return result.returnWithError("Parse DrumKitInstrument failed: rootDir is not a directory: " + dir.getFullPathName());
    }
    auto fileName = dir.getFileName();
    StringArray parts;
    parts.addTokens(fileName, "-", "");
    result.value.name = parts[0].trim();
    if (parts.size() > 1)
        result.value.size = parts[1].trim();
    if (parts.size() > 2) {
        parts.removeRange(0, 2);
        result.value.description = parts.size() > 0 ? parts.joinIntoString(" ").trim() : "";
    }
    result.value.type = DrumKitInstrument::typeFromString(result.value.name);
    result.value.code = fileName.trim();
    result.value.rootDir = dir;

    return result;
}

bool DrumKitInstrument::isAudioFile(const File& file) {
    StringArray audioFileExtensions = { ".wav", ".flac", ".aiff", ".mp3", ".ogg" }; //TODO: refactor dynamical support from plugin's config.yaml
    String ext = file.getFileExtension().toLowerCase();
    for (int i = 0; i < audioFileExtensions.size(); ++i) {
        if (ext == audioFileExtensions[i]) {
            return true;
        }
    }
    return false;
}

int DrumKitInstrument::scanInstrumentSamples() {
    const auto articulationDirs = meta.rootDir.findChildFiles(juce::File::findDirectories, false, "*.*");

    for (const auto& artDir : articulationDirs) {
        if (!artDir.isDirectory())
            continue;

        auto artName = artDir.getFileName();
        StringArray artParts;
        artParts.addTokens(artName, "-", "");

        String articulation = artParts[0].trim();

        InstrumentArticulation* articulationObj = getOrAddArticulation(articulation);

        for (int i = 1; i < artParts.size(); ++i) {
            auto dimPart = artParts[i];
            auto dimCodeEnd = dimPart.indexOfAnyOf("0123456789");
            if (dimCodeEnd > 0) {
                String dimCode = dimPart.substring(0, dimCodeEnd);
                String dimValue = dimPart.substring(dimCodeEnd);
                articulationObj->addDimensionValue(dimCode, static_cast<uint8_t>(dimValue.getIntValue()));
            }
        }

        const auto sampleFiles = artDir.findChildFiles(juce::File::findFiles, false, "*.*");
        for (const auto& sampleFile : sampleFiles) {
            if (!isAudioFile(sampleFile)) {
                DBG("Skipping non-audio file: " + sampleFile.getFileName());
                continue;
            }

            auto parsedInfo = parseSampleFile(sampleFile);
            if (!parsedInfo.ok()) {
                DBG("WARN: Could not parse filename '" + sampleFile.getFileName() + "': " + parsedInfo.error());
                continue;
            }

            auto* velocityLayer = articulationObj->getOrAddVelocityLayer(parsedInfo.value.velocity);

            // Add one sample variant per mic chain
            for (size_t i = 0; i < parsedInfo.value.micDescriptors.size(); i++) {
                uint8_t variantIdx = 0;
                if (!parsedInfo.value.sampleVariant.isEmpty())
                    variantIdx = LayerSampleVariant::letterToIdx(parsedInfo.value.sampleVariant[0]);

                auto sampleVariant = std::make_unique<LayerSampleVariant>(sampleFile, variantIdx, parsedInfo.value.micDescriptors[i]);
                velocityLayer->addSampleVariant(std::move(sampleVariant));
                sampleCount++;
            }
        }
    }
    return sampleCount;
}

ParseResult<DrumKitInstrument::SampleFileInfo> DrumKitInstrument::parseSampleFile(const File& file) {
    ParseResult<DrumKitInstrument::SampleFileInfo> result;

    if (!file.existsAsFile()) {
        return result.returnWithError("File does not exist: " + file.getFullPathName());
    }

    String fileNameWithoutExt = file.getFileNameWithoutExtension();

    StringArray nameParts;
    nameParts.addTokens(fileNameWithoutExt, "-", "");

    if (nameParts.size() < 2) {
        return result.returnWithError("Filename must be in format {velocity}{sampleVariant}-{micChain}: " + fileNameWithoutExt);
    }

    String velRRCode = nameParts[0];
    int splitIndex = 0;
    while (splitIndex < velRRCode.length() && CharacterFunctions::isDigit(velRRCode[splitIndex])) {
        splitIndex++;
    }
    if (splitIndex == 0) {
        return result.returnWithError("velocity+sampleVariant code must start with digits (e.g., '001a', '127b', '005'): " + velRRCode);
    }

    String velocityStr = velRRCode.substring(0, splitIndex);
    uint8_t velocity = static_cast<uint8_t>(velocityStr.getIntValue());
    if (velocity <= 0 || velocity > 127) {
        return result.returnWithError("Velocity must be 1-127, got: " + velocityStr);
    }

    String sampleVariant;
    if (splitIndex < velRRCode.length()) {
        sampleVariant = velRRCode.substring(splitIndex);
        if (sampleVariant.length() == 1) {
            if (sampleVariant[0] < 'a' || sampleVariant[0] > 'z') {
                result.addWarning("Round-robin letter should be a-z, got: '" + sampleVariant + "'; treating as absent");
            }
        } else if (sampleVariant.length() > 1) {
            result.addWarning("Round-robin should be single letter, got: '" + sampleVariant + "'; using first letter '" + String(sampleVariant[0]) + "'");
        }
    }

    auto parsedMics = MicDescriptor::parseMicCodesChain(nameParts[1]);
    if (!parsedMics.ok()) {
        result.addWarning("Mic chain parsing failed: " + parsedMics.error() + "; file will use default routing");
    }

    result.value.velocity = velocity;
    result.value.sampleVariant = sampleVariant;
    result.value.micDescriptors = parsedMics.value;
    result.value.extension = file.getFileExtension();
    result.value.file = file;

    return result;
}

ParseResult<DrumKit::MetaInfo> DrumKit::parseInfo(const File& dir) {
    ParseResult<MetaInfo> result;

    if (!dir.isDirectory()) {
        return result.returnWithError("Parse DrumKit failed: rootDir is not a directory: " + dir.getFullPathName());
    }

    String processed = dir.getFileName().trim();
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
    result.value.rootDir = dir;

    return result;
}

int DrumKit::scanInstruments() {
    const auto subDirs = meta.rootDir.findChildFiles(juce::File::findDirectories, false, "*.*");

    for (const auto& subDir : subDirs) {
        if (!subDir.isDirectory())
            continue;

        auto parsedInfo = DrumKitInstrument::parseInfo(subDir);
        if (parsedInfo.ok()) {
            if (findInstrument(parsedInfo.value.code) == nullptr) {
                auto newInst = std::make_unique<DrumKitInstrument>(this, parsedInfo.value);
                instruments[parsedInfo.value.code] = std::move(newInst);
            }
        } else {
            DBG(parsedInfo.error());
        }
    }

    // sortInstruments();
    updateModifiedTime();
    return (int) instruments.size();
}

DrumKitInstrument* DrumKit::findInstrument(const String& code) {
    auto it = instruments.find(code);
    if (it != instruments.end())
        return it->second.get();
    return nullptr;
}

std::vector<DrumKitInstrument*> DrumKit::getInstrumentsByType(DrumKitInstrument::Type type) const {
    std::vector<DrumKitInstrument*> result;
    for (const auto& pair : instruments) {
        auto* inst = pair.second.get();
        if (inst && inst->meta.type == type)
            result.push_back(inst);
    }
    // stable sort by code so mapping is deterministic
    std::sort(result.begin(), result.end(), [](DrumKitInstrument* a, DrumKitInstrument* b){
        return a->meta.code < b->meta.code;
    });
    return result;
}

// std::vector<DrumKitInstrument*> DrumKit::findInstruments(const String& name) const {
//     std::vector<DrumKitInstrument*> results;
//     for (const auto& [key, instrument] : instruments) {
//         if (instrument && instrument->getName() == name)
//             results.push_back(instrument.get());
//     }
//     return results;
// }

// void DrumKit::sortInstruments() {
//     std::sort(instruments.begin(), instruments.end(), [](const auto& a, const auto& b) {
//         return a->meta.code < b->meta.code;
//     });
// }

bool DrumKit::hasChanged() const {
    return meta.rootDir.getLastModificationTime().toMilliseconds() > lastModifiedTime;
}

void DrumKit::updateModifiedTime() {
    lastModifiedTime = meta.rootDir.getLastModificationTime().toMilliseconds();
}
