#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ParameterSlider : public juce::Slider {
public:
    ParameterSlider() = default;

    void setInteraction(double defaultValue, double wheelStep, double fineWheelStep = 0.0);

    double getWheelStep() const { return this->wheelStep; }
    double getFineWheelStep() const { return this->fineWheelStep; }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    double wheelStep = 0.0;
    double fineWheelStep = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterSlider)
};
