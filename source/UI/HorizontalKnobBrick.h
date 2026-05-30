#pragma once

#include "ParameterPot.h"

/**
 * Horizontal composite: left half = title label (top) + editable value (bottom),
 * right half = rotary pot knob.
 *
 * Layout:
 *   [Label ] [ pot ]
 *   [value ] [ pot ]
 */
class HorizontalKnobBrick : public ParameterPot {
public:
    HorizontalKnobBrick(const juce::String& name,
               double minVal, double maxVal, double interval, double initialValue,
               std::function<juce::String(double)> textFromValueFn = nullptr);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HorizontalKnobBrick)
};
