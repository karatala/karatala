#pragma once

#include "ParameterPot.h"

/**
 * Vertical composite: title on top, rotary knob in center, value on bottom.
 *
 * Layout:
 *   [ title ]
 *   [  pot  ]
 *   [ value ]
 */
class LabeledPot : public ParameterPot {
public:
    LabeledPot(const juce::String& name,
               double minVal, double maxVal, double interval, double initialValue,
               std::function<juce::String(double)> textFromValueFn = nullptr);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabeledPot)
};
