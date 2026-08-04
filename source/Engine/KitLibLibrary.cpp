#include "KitLibModel.h"
#include "../Config/PluginLogger.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace KitLib {

ParseResult<DrumKitSpec::MetaInfo> DrumKitSpec::parseInfo(const juce::String& dirName) {
    ParseResult<MetaInfo> result;

    juce::String processed = dirName.trim();
    result.value.codeRaw = processed;
    result.value.name = processed;
    result.value.author = "";
    result.value.license = "";

    auto wrapSegm = Parsing::extractWrappedSegment(processed, "[", "]");
    result.value.license = wrapSegm.wrapped;
    processed = wrapSegm.prefix + " " + wrapSegm.suffix;

    auto parts = splitByString(processed, " by ");
    result.value.name   = parts[0].trim();
    result.value.author = parts.size() > 1 ? parts[1].trim() : "";

    return result;
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
        if (!KitLib::matchesIdentityCandidate(InstrumentSpec::getIdentityCandidates(instrument->meta), id)) continue;
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
            if (otherInstrument != nullptr && KitLib::matchesIdentityCandidate(InstrumentSpec::getIdentityCandidates(otherInstrument->meta), candidate)) ++matchCount;
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
        auto readmeFile = kitDir.getChildFile("README.md");
        if (!readmeFile.existsAsFile()) readmeFile = kitDir.getChildFile("readme.md");
        if (readmeFile.existsAsFile()) kit->readmeMarkdown = readmeFile.loadFileAsString();
        const double metadataStartTimeMs = juce::Time::getMillisecondCounterHiRes();
        auto metadataResult = kit->sampleMetadata.loadFromKitRoot(kitDir);
        auto overrideResult = kit->libraryKitOverrides.loadFromKitRoot(kitDir);
        const double metadataElapsedMs = juce::Time::getMillisecondCounterHiRes() - metadataStartTimeMs;
        for (const auto& metadataMessage : metadataResult.errors) LOG(Warning, "Library kit metadata: " + metadataMessage);
        for (const auto& overrideMessage : overrideResult.errors) LOG(Warning, "Library kit override: " + overrideMessage);
        if (metadataResult.ok() && kit->sampleMetadata.isActive()) LOG(Info, "Loaded " + juce::String(metadataResult.value) + " metadata.csv rows for kit " + kitDir.getFileName() + " in " + juce::String(metadataElapsedMs, 1) + " ms");
        if (overrideResult.ok() && kit->libraryKitOverrides.isActive()) LOG(Info, "Loaded " + juce::String(overrideResult.value) + " library kit override values for kit " + kitDir.getFileName());
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
        for (const auto& [roundRobinLetter, count] : stats.roundRobinCounts) globalStats.roundRobinCounts[roundRobinLetter] += count;
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

        LOG(Info, "Round-robin Usage:");
        for (const auto& [roundRobinLetter, count] : globalStats.roundRobinCounts) LOG(Info, "  " + roundRobinLetter + ": " + juce::String(count) + " sample files");

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
        if (!KitLib::matchesIdentityCandidate(LibraryManager::getIdentityCandidates(kit->meta), id)) continue;
        matched = kit.get();
        ++matchCount;
    }
    return matchCount == 1 ? matched : nullptr;
}

juce::String LibraryManager::getPreferredKitId(const DrumKitSpec& kit) const {
    for (const auto& candidate : LibraryManager::getIdentityCandidates(kit.meta)) {
        int matchCount = 0;
        for (const auto& existing : this->kitSpecs) {
            if (existing != nullptr && KitLib::matchesIdentityCandidate(LibraryManager::getIdentityCandidates(existing->meta), candidate)) ++matchCount;
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

} // namespace KitLib
