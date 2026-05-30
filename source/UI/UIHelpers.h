#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SourceItem.h"
#include "UITheme.h"

namespace UIHelpers {

void configureCaptionLabel(juce::Label& label,
                           juce::Justification justification = juce::Justification::centred,
                           juce::Colour colour = UITheme::Colours::textSecondary);

void configureValueLabel(juce::Label& label,
                         juce::Justification justification = juce::Justification::centred,
                         float fontHeight = 0.0f);

void configureSectionLabel(juce::Label& label);

void configureRotarySlider(juce::Slider& slider,
                           juce::Slider::TextEntryBoxPosition textBoxPosition,
                           int textBoxWidth,
                           int textBoxHeight);

void configureControlButton(juce::TextButton& button, int connectedEdges = 0);

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