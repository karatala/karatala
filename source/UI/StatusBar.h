#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class StatusBar : public juce::Component {
public:
    explicit StatusBar();
    ~StatusBar() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setStatusText(const juce::String& text);
    void setInfoText(const juce::String& text);

private:
    juce::Label statusLabel;
    juce::Label infoLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};
