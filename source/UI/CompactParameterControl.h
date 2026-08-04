#pragma once

#include "ParameterSlider.h"

#include <functional>

class CompactParameterControl : public ParameterSlider {
public:
    CompactParameterControl(const juce::String& name, double minVal, double maxVal, double interval, double initialValue, std::function<juce::String(double)> textFromValueFn = nullptr);

    static juce::String sentinelValueText(double value);

    void paint(juce::Graphics& graphics) override;
    void setLabelVisible(bool shouldShow);

private:
    bool showLabel = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompactParameterControl)
};
