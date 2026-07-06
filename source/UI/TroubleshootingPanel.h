#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class DrumKitEngine;

class TroubleshootingPanel : public juce::Component,
                             private juce::Timer {
public:
    explicit TroubleshootingPanel(DrumKitEngine& engine);
    ~TroubleshootingPanel() override = default;

    void resized() override;
    void refresh();

    std::function<void()> onFixMissingInstrumentsRequested;
    std::function<void()> onReloadLibraryRequested;
    std::function<void(const juce::String&)> onStatusMessage;

private:
    DrumKitEngine& engine;

    juce::Label titleLabel;
    juce::Label logLevelLabel;
    juce::ComboBox logLevelCombo;
    juce::ToggleButton writeLogFilesToggle { "Write Log Files" };
    juce::TextButton openLogButton { "Open Log" };
    juce::TextButton logsFolderButton { "Logs Folder" };
    juce::TextButton fixMissingButton { "Fix Missing Instruments" };
    juce::TextButton reloadLibraryButton { "Reload Library" };
    juce::TextEditor importantLogsEditor;

    void refreshLogText();
    void openCurrentLogFile();
    void openLogsFolder();
    void timerCallback() override;
    void sendStatus(const juce::String& message);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TroubleshootingPanel)
};
