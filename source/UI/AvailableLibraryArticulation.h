#pragma once

#include <juce_core/juce_core.h>
#include "../Engine/InstrumentType.h"
#include <vector>

namespace KitLib {
class LibraryManager;
}

struct AvailableLibraryArticulation {
    juce::String kitCodeRaw;
    juce::String kitDisplayName;
    juce::String instCode;
    juce::String artCode;
    juce::String rawKitCode;
    juce::String rawInstCode;
    juce::String rawArtCode;
    KitLib::InstrumentType instType = KitLib::InstrumentType::Unknown;

    juce::String qualifiedId() const;
    juce::String rawQualifiedId() const;
    juce::String displayLabel() const;

    static std::vector<AvailableLibraryArticulation> collectFromLibrary(const KitLib::LibraryManager* libraryManager);
    static int findIndexByQualifiedId(const std::vector<AvailableLibraryArticulation>& availableLibraryArticulations, const juce::String& qualifiedLibraryArticulationId);
};
