#include "SourceItem.h"

#include "../Engine/ArticulationNames.h"
#include "../Engine/KitLibModel.h"

#include <algorithm>
#include <unordered_set>

juce::String SourceItem::fullPath() const {
    juce::String result = this->kitCodeRaw;
    if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
    if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
    return result;
}

juce::String SourceItem::rawFullPath() const {
    juce::String result = this->rawKitCode.isNotEmpty() ? this->rawKitCode : this->kitCodeRaw;
    const auto instrumentCode = this->rawInstCode.isNotEmpty() ? this->rawInstCode : this->instCode;
    const auto articulationCode = this->rawArtCode.isNotEmpty() ? this->rawArtCode : this->artCode;
    if (instrumentCode.isNotEmpty()) result += "/" + instrumentCode;
    if (articulationCode.isNotEmpty()) result += "/" + articulationCode;
    return result;
}

juce::String SourceItem::displayText() const {
    juce::String result = this->kitDisplayName;
    if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
    if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
    return result;
}

std::vector<SourceItem> SourceItem::collectFromLibrary(const KitLib::LibraryManager* libraryManager) {
    std::vector<SourceItem> sourceItems;
    if (libraryManager == nullptr) return sourceItems;

    std::unordered_set<std::string> seenFullPathKeys;
    auto addUniqueSourceItem = [&sourceItems, &seenFullPathKeys](SourceItem sourceItem) {
        const auto fullPathKey = sourceItem.fullPath().trim().toLowerCase().toStdString();
        if (fullPathKey.empty()) return;
        if (seenFullPathKeys.insert(fullPathKey).second) sourceItems.push_back(std::move(sourceItem));
    };

    for (const auto& kitPtr : libraryManager->getKits()) {
        if (kitPtr == nullptr) continue;
        const auto kitId = libraryManager->getPreferredKitId(*kitPtr);
        juce::String kitShort = kitPtr->meta.name;
        if (kitShort.isEmpty()) kitShort = kitPtr->meta.codeRaw;

        std::vector<const KitLib::InstrumentSpec*> instruments;
        for (const auto& [code, instrumentPtr] : kitPtr->getInstrumentSpecsMap()) {
            juce::ignoreUnused(code);
            instruments.push_back(instrumentPtr.get());
        }
        std::sort(instruments.begin(), instruments.end(), [](const auto* leftInstrument, const auto* rightInstrument) { return leftInstrument->meta.codeRaw < rightInstrument->meta.codeRaw; });

        for (const auto* instrument : instruments) {
            if (instrument == nullptr || instrument->articulationSpecs.empty()) continue;
            for (const auto& articulationPtr : instrument->articulationSpecs) {
                if (articulationPtr == nullptr) continue;
                SourceItem item;
                item.kitCodeRaw = kitId;
                item.kitDisplayName = kitShort;
                item.instCode = kitPtr->getPreferredInstrumentId(*instrument);
                item.artCode = instrument->getPreferredArticulationId(*articulationPtr);
                item.rawKitCode = kitPtr->meta.codeRaw;
                item.rawInstCode = instrument->meta.codeRaw;
                item.rawArtCode = articulationPtr->codeRaw;
                item.instType = instrument->meta.type;
                addUniqueSourceItem(std::move(item));
            }
        }
    }
    return sourceItems;
}

int SourceItem::findIndex(const std::vector<SourceItem>& sourceItems, const juce::String& searchPath) {
    const auto needle = searchPath.trim().toLowerCase();
    if (needle.isEmpty()) return -1;
    for (int idx = 0; idx < static_cast<int>(sourceItems.size()); ++idx) {
        const auto& item = sourceItems[static_cast<size_t>(idx)];
        if (item.fullPath().toLowerCase() == needle || item.rawFullPath().toLowerCase() == needle) return idx;
    }

    juce::StringArray needleParts;
    needleParts.addTokens(needle, "/", "");
    needleParts.removeEmptyStrings();
    if (needleParts.size() < 3) return -1;

    const auto needleBaseArticulation = ArticulationNames::normalizeArticulationName(needleParts[2]);
    for (int idx = 0; idx < static_cast<int>(sourceItems.size()); ++idx) {
        const auto& item = sourceItems[static_cast<size_t>(idx)];
        if (item.kitCodeRaw.toLowerCase() != needleParts[0] && item.rawKitCode.toLowerCase() != needleParts[0]) continue;
        if (item.instCode.toLowerCase() != needleParts[1] && item.rawInstCode.toLowerCase() != needleParts[1]) continue;
        if (ArticulationNames::normalizeArticulationName(item.artCode) == needleBaseArticulation || ArticulationNames::normalizeArticulationName(item.rawArtCode) == needleBaseArticulation) return idx;
    }
    return -1;
}

bool SourceItem::usesLegacyArticulationSlotExpressionFilter(const std::vector<juce::String>& articulationSlotFilters, const juce::String& articulationSlotCode) {
    if (!articulationSlotCode.containsChar('[') || articulationSlotFilters.empty()) return false;
    for (const auto& filter : articulationSlotFilters)
        if (ArticulationNames::articulationCodeMatches(filter, articulationSlotCode)) return false;
    for (const auto& filter : articulationSlotFilters)
        if (ArticulationNames::mapFilterMatchesArticulation(filter, articulationSlotCode)) return true;
    return false;
}

juce::String SourceItem::resolveLegacyHihatSourcePath(const std::vector<SourceItem>& sourceItems, const juce::String& sourcePath, const juce::String& articulationSlotCode) {
    const int expressionStartIdx = articulationSlotCode.indexOfChar('[');
    const int expressionEndIdx = articulationSlotCode.indexOfChar(expressionStartIdx + 1, ']');
    if (expressionStartIdx < 0 || expressionEndIdx <= expressionStartIdx + 1) return sourcePath;

    juce::StringArray sourceParts;
    sourceParts.addTokens(sourcePath.trim(), "/", "");
    sourceParts.removeEmptyStrings();
    if (sourceParts.size() < 3) return sourcePath;

    const auto libraryArticulation = ArticulationNames::baseArticulationName(sourceParts[2]);
    const auto articulationSlotExpression = articulationSlotCode.substring(expressionStartIdx + 1, expressionEndIdx).trim();
    if (libraryArticulation.isEmpty() || articulationSlotExpression.isEmpty()) return sourcePath;

    const auto expressionSourcePath = sourceParts[0] + "/" + sourceParts[1] + "/" + libraryArticulation + "[" + articulationSlotExpression + "]";
    for (const auto& sourceItem : sourceItems)
        if (sourceItem.fullPath().equalsIgnoreCase(expressionSourcePath) || sourceItem.rawFullPath().equalsIgnoreCase(expressionSourcePath)) return sourceItem.fullPath();
    return sourcePath;
}
