#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

class DismissOverlay : public juce::Component {
public:
    std::function<void()> onClick;

    void mouseDown(const juce::MouseEvent&) override {
        if (this->onClick)
            this->onClick();
    }
};