#include "TroubleshootingPanel.h"

#include "../Config/PluginLogger.h"
#include "../Engine/DrumKitEngine.h"
#include "ParameterPotHelpers.h"
#include "UIHelpers.h"
#include "UITheme.h"

TroubleshootingPanel::TroubleshootingPanel(DrumKitEngine& engine)
    : engine(engine) {
    this->titleLabel.setText("Important logs:", juce::dontSendNotification);
    this->titleLabel.setFont(UITheme::Fonts::labelBoldMedium());
    this->titleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->titleLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->titleLabel);

    this->logLevelLabel.setText("Log level:", juce::dontSendNotification);
    UIHelpers::configureCaptionLabel(this->logLevelLabel, juce::Justification::centredLeft, UITheme::Colours::textSecondary);
    this->addAndMakeVisible(this->logLevelLabel);

    UIHelpers::configureComboBox(this->logLevelCombo);
    this->logLevelCombo.addItemList({ "OFF", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG", "VERBOSE" }, 1);
    this->logLevelCombo.onChange = [this] {
        const int selectedId = this->logLevelCombo.getSelectedId();
        if (selectedId <= 0) return;
        this->engine.setGlobalLogLevel(ParameterPotHelpers::choiceIdToDebugLevel(selectedId));
        this->engine.saveSettings();
        this->sendStatus("Log level saved: " + this->logLevelCombo.getText());
        this->refreshLogText();
    };
    this->addAndMakeVisible(this->logLevelCombo);

    this->writeLogFilesToggle.setColour(juce::ToggleButton::textColourId, UITheme::Colours::textSecondary);
    this->writeLogFilesToggle.onClick = [this] {
        GlobalLogger::instance().setWriteLogFilesEnabled(this->writeLogFilesToggle.getToggleState());
        this->sendStatus(this->writeLogFilesToggle.getToggleState() ? "Log file writing enabled" : "Log file writing disabled");
    };
    this->addAndMakeVisible(this->writeLogFilesToggle);

    UIHelpers::configureControlButton(this->openLogButton);
    this->openLogButton.onClick = [this] { this->openCurrentLogFile(); };
    this->addAndMakeVisible(this->openLogButton);

    UIHelpers::configureControlButton(this->logsFolderButton);
    this->logsFolderButton.onClick = [this] { this->openLogsFolder(); };
    this->addAndMakeVisible(this->logsFolderButton);

    UIHelpers::configureControlButton(this->fixMissingButton);
    this->fixMissingButton.onClick = [this] {
        if (this->onFixMissingInstrumentsRequested) this->onFixMissingInstrumentsRequested();
    };
    this->addAndMakeVisible(this->fixMissingButton);

    UIHelpers::configureControlButton(this->reloadLibraryButton);
    this->reloadLibraryButton.onClick = [this] {
        if (this->onReloadLibraryRequested) this->onReloadLibraryRequested();
    };
    this->addAndMakeVisible(this->reloadLibraryButton);

    UIHelpers::configureMonoTextEditor(this->importantLogsEditor, UITheme::Colours::textSecondary, true);
    this->importantLogsEditor.setReadOnly(true);
    this->importantLogsEditor.setCaretVisible(false);
    this->importantLogsEditor.setTextToShowWhenEmpty("No error, warning, or notice lines in this session.", UITheme::Colours::textMuted);
    this->addAndMakeVisible(this->importantLogsEditor);

    this->refresh();
    this->startTimer(1000);
}

void TroubleshootingPanel::refresh() {
    this->logLevelCombo.setSelectedId(ParameterPotHelpers::debugLevelToChoiceId(this->engine.getGlobalLogLevel()), juce::dontSendNotification);
    this->writeLogFilesToggle.setToggleState(GlobalLogger::instance().getWriteLogFilesEnabled(), juce::dontSendNotification);
    this->refreshLogText();
}

void TroubleshootingPanel::refreshLogText() {
    const auto lines = GlobalLogger::instance().getRecentImportantLines();
    this->importantLogsEditor.setText(lines, false);
}

void TroubleshootingPanel::openCurrentLogFile() {
    GlobalLogger::instance().flush();
    const auto file = GlobalLogger::instance().getCurrentLogFile();
    if (!file.existsAsFile()) {
        this->sendStatus("Log file does not exist yet");
        return;
    }
    file.startAsProcess();
}

void TroubleshootingPanel::openLogsFolder() {
    auto folder = GlobalLogger::instance().getLogsDirectory();
    if (folder.getFullPathName().isEmpty()) folder = this->engine.dataPath.getChildFile("logs");
    folder.createDirectory();
    folder.startAsProcess();
}

void TroubleshootingPanel::timerCallback() {
    this->refreshLogText();
}

void TroubleshootingPanel::sendStatus(const juce::String& message) {
    if (this->onStatusMessage) this->onStatusMessage(message);
}

void TroubleshootingPanel::resized() {
    auto area = this->getLocalBounds().reduced(UITheme::Spacing::md);
    auto header = area.removeFromTop(32);
    const int gap = UITheme::Spacing::sm;

    this->titleLabel.setBounds(header.removeFromLeft(104));
    header.removeFromLeft(gap);
    this->logLevelLabel.setBounds(header.removeFromLeft(66));
    this->logLevelCombo.setBounds(header.removeFromLeft(92).reduced(0, 3));
    header.removeFromLeft(gap);
    this->writeLogFilesToggle.setBounds(header.removeFromLeft(128));
    header.removeFromLeft(gap);
    this->openLogButton.setBounds(header.removeFromLeft(78).reduced(0, 3));
    header.removeFromLeft(gap);
    this->logsFolderButton.setBounds(header.removeFromLeft(90).reduced(0, 3));
    header.removeFromLeft(gap);
    this->reloadLibraryButton.setBounds(header.removeFromRight(112).reduced(0, 3));
    header.removeFromRight(gap);
    this->fixMissingButton.setBounds(header.removeFromRight(148).reduced(0, 3));

    area.removeFromTop(UITheme::Spacing::md);
    this->importantLogsEditor.setBounds(area);
}
