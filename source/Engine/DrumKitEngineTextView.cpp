#include "DrumKitEngineTextView.h"
#include "InstrumentSynth.h"
#include <algorithm>

namespace DrumKitEngineTextView {

juce::String buildSuggestedInstrumentMapText(const KitLayout* layout, const std::unordered_map<juce::String, InstrumentSynth*>& instrumentSynthById) {
    if (layout == nullptr)
        return {};

    juce::StringArray lines;
    for (const auto& slot : layout->instrumentSlots) {
        auto slotIt = instrumentSynthById.find(slot.id);
        if (slotIt == instrumentSynthById.end())
            continue;

        const auto* loadedInstrument = slotIt->second->getLoadedInstrumentSpec();
        if (loadedInstrument == nullptr || loadedInstrument->parent == nullptr)
            continue;

        lines.add(slot.id + ":" + loadedInstrument->parent->meta.codeRaw + "/" + loadedInstrument->meta.codeRaw);
    }

    return lines.joinIntoString("\n");
}

juce::String buildReferenceText(const KitLayout* layout, const KitLib::LibraryManager* libraryManager) {
    juce::StringArray lines;

    lines.add("# Layout slots  (slotId/articulation:)");
    if (layout != nullptr) {
        for (const auto& slot : layout->instrumentSlots) {
            if (slot.articulations.empty()) {
                lines.add(slot.id + "/:");
            } else {
                for (const auto& art : slot.articulations) {
                    const auto artName = art.codeRaw.upToFirstOccurrenceOf("[", false, false);
                    lines.add(slot.id + "/" + artName + ":");
                }
            }
        }
    }

    lines.add("");
    lines.add("# Library  (KitCode/instrument/articulation)");
    if (libraryManager != nullptr) {
        for (const auto& kitPtr : libraryManager->getKits()) {
            std::vector<const KitLib::InstrumentSpec*> instruments;
            instruments.reserve(kitPtr->getInstrumentSpecsMap().size());
            for (const auto& [code, instPtr] : kitPtr->getInstrumentSpecsMap())
                instruments.push_back(instPtr.get());

            std::sort(instruments.begin(), instruments.end(), [](const auto* a, const auto* b) {
                return a->meta.codeRaw < b->meta.codeRaw;
            });

            for (const auto* inst : instruments) {
                if (inst->articulationSpecs.empty()) {
                    lines.add(kitPtr->meta.codeRaw + "/" + inst->meta.codeRaw);
                } else {
                    for (const auto& artPtr : inst->articulationSpecs) {
                        const auto artName = artPtr->codeRaw.upToFirstOccurrenceOf("[", false, false);
                        lines.add(kitPtr->meta.codeRaw + "/" + inst->meta.codeRaw + "/" + artName);
                    }
                }
            }
        }
    }

    return lines.joinIntoString("\n");
}

}
