#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class StatusBar : public juce::Component {
public:
    explicit StatusBar();
    ~StatusBar() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setStatusText(const juce::String& text);
    void setInfoText(const juce::String& text);
    void setUiActive(bool active);
    void setLogsActive(bool active);

    std::function<void()> onUiRequested;
    std::function<void()> onInspectRequested;
    std::function<void()> onLogsRequested;

private:
    juce::Label statusLabel;
    juce::Label infoLabel;
    juce::TextButton uiButton { "UI" };
    juce::TextButton inspectButton { "?" };
    juce::TextButton logsButton { "Logs" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};
