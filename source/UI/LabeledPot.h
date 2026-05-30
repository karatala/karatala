#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Vertical composite: title on top, rotary knob in center, value on bottom.
 *
 * Layout:
 *   [ title ]
 *   [  pot  ]
 *   [ value ]
 */
class LabeledPot : public juce::Component {
public:
    LabeledPot(const juce::String& name,
               double minVal, double maxVal, double interval, double initialValue,
               std::function<juce::String(double)> textFromValueFn = nullptr);

    void resized() override;

    juce::Slider& getPot() { return this->pot; }
    const juce::Slider& getPot() const { return this->pot; }

    double getValue() const { return this->pot.getValue(); }
    void setValue(double val, juce::NotificationType notification = juce::dontSendNotification) {
        this->pot.setValue(val, notification);
    }

    std::function<void()> onValueChange;
    std::function<void()> onDragEnd;

private:
    juce::Label titleLabel;
    juce::Label valueLabel;
    juce::Slider pot;

    void updateValueText();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabeledPot)
};
