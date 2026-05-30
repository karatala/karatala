#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SourceItem.h"
#include "UITheme.h"

namespace UIHelpers {

std::vector<SourceItem> buildSourceItems(const KitLib::LibraryManager* libraryManager);

int findSourceIndex(const std::vector<SourceItem>& sourceItems, const juce::String& searchPath);

std::unique_ptr<juce::Slider> createParamSlider(
    double minVal,
    double maxVal,
    double interval,
    double initialValue,
    int textBoxWidth = UITheme::Sizes::sliderTextBoxW,
    int textBoxHeight = UITheme::Sizes::sliderTextBoxH,
    std::function<juce::String(double)> textFromValueFn = nullptr);

std::unique_ptr<juce::Slider> createCompactPot(
    double minVal,
    double maxVal,
    double interval,
    double initialValue,
    std::function<juce::String(double)> textFromValueFn = nullptr);

} // namespace UIHelpers