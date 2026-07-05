#pragma once

#include "ParameterSlider.h"

#include <functional>

class CompactParameterControl : public ParameterSlider {
public:
    CompactParameterControl(const juce::String& name, double minVal, double maxVal, double interval, double initialValue, std::function<juce::String(double)> textFromValueFn = nullptr);

    void paint(juce::Graphics& graphics) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompactParameterControl)
};