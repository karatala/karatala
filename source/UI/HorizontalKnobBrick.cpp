#include "HorizontalKnobBrick.h"

static ParameterPot::LayoutOptions makeHorizontalPotLayout() {
    ParameterPot::LayoutOptions options;
    options.layoutMode = ParameterPot::LayoutMode::Horizontal;
    options.titleJustification = juce::Justification::centredLeft;
    options.valueJustification = juce::Justification::centredLeft;
    options.valueFontHeight = 13.0f;
    return options;
}

HorizontalKnobBrick::HorizontalKnobBrick(const juce::String& name, double minVal, double maxVal, double interval, double initialValue, std::function<juce::String(double)> textFromValueFn)
    : ParameterPot(name, minVal, maxVal, interval, initialValue, std::move(textFromValueFn), makeHorizontalPotLayout()) {
}
