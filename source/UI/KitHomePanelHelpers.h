#pragma once

#include "../Engine/KitLibModel.h"
#include "../Engine/RoutingTypes.h"

namespace KitHomePanelHelpers {

struct TypeChoice {
    KitLib::InstrumentSpec::Type type;
    const char* label;
};

juce::String safePresetFileName(juce::String name);
const std::vector<TypeChoice>& editableTypeChoices();
int typeComboIdFor(KitLib::InstrumentSpec::Type type);
KitLib::InstrumentSpec::Type typeFromComboId(int selectedId);
std::optional<DirectMicOutPolicy> policyFromComboId(int selectedId);
int comboIdForPolicy(std::optional<DirectMicOutPolicy> policy);

template <typename Choice>
juce::String sourcePath(const Choice& choice) {
    if (choice.kitCode.isEmpty() || choice.instrumentCode.isEmpty())
        return {};

    return choice.kitCode + "/" + choice.instrumentCode;
}

} // namespace KitHomePanelHelpers
