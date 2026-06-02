#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ScrollablePanel : public juce::Component {
public:
    ScrollablePanel();
    ~ScrollablePanel() override = default;

protected:
    juce::Viewport viewport;
    juce::Component contentComponent;

    void layoutViewportToBounds();
};
