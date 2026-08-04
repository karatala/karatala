#pragma once

#include <juce_core/juce_core.h>
#include "../Engine/InstrumentType.h"
#include <vector>

namespace KitLib {
class LibraryManager;
}

struct SourceItem {
    juce::String kitCodeRaw;
    juce::String kitDisplayName;
    juce::String instCode;
    juce::String artCode;
    juce::String rawKitCode;
    juce::String rawInstCode;
    juce::String rawArtCode;
    KitLib::InstrumentType instType = KitLib::InstrumentType::Unknown;

    juce::String fullPath() const;
    juce::String rawFullPath() const;
    juce::String displayText() const;

    static std::vector<SourceItem> collectFromLibrary(const KitLib::LibraryManager* libraryManager);
    static int findIndex(const std::vector<SourceItem>& sourceItems, const juce::String& searchPath);
    static bool usesLegacyArticulationSlotExpressionFilter(const std::vector<juce::String>& articulationSlotFilters, const juce::String& articulationSlotCode);
    static juce::String resolveLegacyHihatSourcePath(const std::vector<SourceItem>& sourceItems, const juce::String& sourcePath, const juce::String& articulationSlotCode);
};
