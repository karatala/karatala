#include "AvailableLibraryArticulation.h"

#include "../Engine/ArticulationNames.h"
#include "../Engine/KitLibModel.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

juce::String AvailableLibraryArticulation::qualifiedId() const {
    juce::String result = this->kitCodeRaw;
    if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
    if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
    return result;
}

juce::String AvailableLibraryArticulation::rawQualifiedId() const {
    juce::String result = this->rawKitCode.isNotEmpty() ? this->rawKitCode : this->kitCodeRaw;
    const auto instrumentCode = this->rawInstCode.isNotEmpty() ? this->rawInstCode : this->instCode;
    const auto articulationCode = this->rawArtCode.isNotEmpty() ? this->rawArtCode : this->artCode;
    if (instrumentCode.isNotEmpty()) result += "/" + instrumentCode;
    if (articulationCode.isNotEmpty()) result += "/" + articulationCode;
    return result;
}

juce::String AvailableLibraryArticulation::displayLabel() const {
    juce::String result = this->kitDisplayName;
    if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
    if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
    return result;
}

std::vector<AvailableLibraryArticulation> AvailableLibraryArticulation::collectFromLibrary(const KitLib::LibraryManager* libraryManager) {
    std::vector<AvailableLibraryArticulation> availableLibraryArticulations;
    if (libraryManager == nullptr) return availableLibraryArticulations;

    std::unordered_set<std::string> seenQualifiedIds;
    auto addUniqueLibArt = [&availableLibraryArticulations, &seenQualifiedIds](AvailableLibraryArticulation libArt) {
        const auto qualifiedIdKey = libArt.qualifiedId().trim().toLowerCase().toStdString();
        if (qualifiedIdKey.empty()) return;
        if (seenQualifiedIds.insert(qualifiedIdKey).second) availableLibraryArticulations.push_back(std::move(libArt));
    };

    for (const auto& kitPtr : libraryManager->getKits()) {
        if (kitPtr == nullptr) continue;
        const auto kitId = libraryManager->getPreferredKitId(*kitPtr);
        juce::String kitDisplayName = kitPtr->meta.name;
        if (kitDisplayName.isEmpty()) kitDisplayName = kitPtr->meta.codeRaw;

        std::vector<const KitLib::InstrumentSpec*> libInstruments;
        for (const auto& [code, instrumentPtr] : kitPtr->getInstrumentSpecsMap()) {
            juce::ignoreUnused(code);
            libInstruments.push_back(instrumentPtr.get());
        }
        std::sort(libInstruments.begin(), libInstruments.end(), [](const auto* leftInstrument, const auto* rightInstrument) { return leftInstrument->meta.codeRaw < rightInstrument->meta.codeRaw; });

        for (const auto* libInst : libInstruments) {
            if (libInst == nullptr || libInst->articulationSpecs.empty()) continue;
            for (const auto& libArtPtr : libInst->articulationSpecs) {
                if (libArtPtr == nullptr) continue;
                AvailableLibraryArticulation libArt;
                libArt.kitCodeRaw = kitId;
                libArt.kitDisplayName = kitDisplayName;
                libArt.instCode = kitPtr->getPreferredInstrumentId(*libInst);
                libArt.artCode = libInst->getPreferredArticulationId(*libArtPtr);
                libArt.rawKitCode = kitPtr->meta.codeRaw;
                libArt.rawInstCode = libInst->meta.codeRaw;
                libArt.rawArtCode = libArtPtr->codeRaw;
                libArt.instType = libInst->meta.type;
                addUniqueLibArt(std::move(libArt));
            }
        }
    }
    return availableLibraryArticulations;
}

int AvailableLibraryArticulation::findIndexByQualifiedId(const std::vector<AvailableLibraryArticulation>& availableLibraryArticulations, const juce::String& qualifiedLibraryArticulationId) {
    const auto needle = qualifiedLibraryArticulationId.trim().toLowerCase();
    if (needle.isEmpty()) return -1;
    for (int idx = 0; idx < static_cast<int>(availableLibraryArticulations.size()); ++idx) {
        const auto& libArt = availableLibraryArticulations[static_cast<size_t>(idx)];
        if (libArt.qualifiedId().toLowerCase() == needle || libArt.rawQualifiedId().toLowerCase() == needle) return idx;
    }

    juce::StringArray needleParts;
    needleParts.addTokens(needle, "/", "");
    needleParts.removeEmptyStrings();
    if (needleParts.size() < 3) return -1;

    const auto needleBaseArticulation = ArticulationNames::normalizeArticulationName(needleParts[2]);
    for (int idx = 0; idx < static_cast<int>(availableLibraryArticulations.size()); ++idx) {
        const auto& libArt = availableLibraryArticulations[static_cast<size_t>(idx)];
        if (libArt.kitCodeRaw.toLowerCase() != needleParts[0] && libArt.rawKitCode.toLowerCase() != needleParts[0]) continue;
        if (libArt.instCode.toLowerCase() != needleParts[1] && libArt.rawInstCode.toLowerCase() != needleParts[1]) continue;
        if (ArticulationNames::normalizeArticulationName(libArt.artCode) == needleBaseArticulation || ArticulationNames::normalizeArticulationName(libArt.rawArtCode) == needleBaseArticulation) return idx;
    }
    return -1;
}
