#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class ThemePlaygroundPanel : public juce::Component {
public:
    ThemePlaygroundPanel();
    ~ThemePlaygroundPanel() override;

    void resized() override;

private:
    class Content;

    std::unique_ptr<Content> content;
    juce::Viewport viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemePlaygroundPanel)
};
