#include "HorizontalKnobBrick.h"
#include "ParameterPotHelpers.h"

HorizontalKnobBrick::HorizontalKnobBrick(const juce::String& name, double minVal, double maxVal, double interval, double initialValue, std::function<juce::String(double)> textFromValueFn)
    : ParameterPot(name, minVal, maxVal, interval, initialValue, std::move(textFromValueFn), ParameterPotHelpers::makeHorizontalPotLayout()) {
}
