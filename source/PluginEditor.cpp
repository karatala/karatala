#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p)
    , processorRef(p) {
    this->setLookAndFeel(&this->lookAndFeel);

    // Sidebar
    addAndMakeVisible(this->sidebar);
    this->sidebar.onModeChanged = [this](ViewMode mode) {
        this->setViewMode(mode);
    };
    this->sidebar.inspectButton.onClick = [this] {
        if (!this->inspector) {
            this->inspector = std::make_unique<melatonin::Inspector>(*this);
            this->inspector->onClose = [this]() { this->inspector.reset(); };
        }
        this->inspector->setVisible(true);
    };

    // Status bar
    addAndMakeVisible(this->statusBar);
    this->statusBar.setStatusText(this->processorRef.getEngine().getLastInstrumentMapStatus());

    // Drum pad grid (Zen / Zoned modes)
    addAndMakeVisible(this->drumPadGrid);
    this->drumPadGrid.populate(this->processorRef.getEngine().getKitLayout());
    this->sidebar.velocityKnob.onValueChange = [this] {
        this->drumPadGrid.setClickVelocity(static_cast<float>(this->sidebar.velocityKnob.getValue()));
    };

    // Raw mode components
    addChildComponent(this->referencePanel);
    addChildComponent(this->referencePanelLabel);
    addChildComponent(this->instrumentMapEditor);
    addChildComponent(this->instrumentMapLabel);
    addChildComponent(this->applyButton);

    this->referencePanelLabel.setText("Reference", juce::dontSendNotification);
    this->referencePanelLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    this->referencePanelLabel.setJustificationType(juce::Justification::centredLeft);

    this->referencePanel.setMultiLine(true);
    this->referencePanel.setReadOnly(true);
    this->referencePanel.setScrollbarsShown(true);
    this->referencePanel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    this->referencePanel.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1e1e1e));
    this->referencePanel.setColour(juce::TextEditor::textColourId, juce::Colour(0xffaaaaaa));
    this->referencePanel.setText(this->processorRef.getEngine().buildReferenceText(), false);

    this->instrumentMapLabel.setText("Instrument Map", juce::dontSendNotification);
    this->instrumentMapLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    this->instrumentMapLabel.setJustificationType(juce::Justification::centredLeft);

    this->instrumentMapEditor.setMultiLine(true);
    this->instrumentMapEditor.setReturnKeyStartsNewLine(true);
    this->instrumentMapEditor.setScrollbarsShown(true);
    this->instrumentMapEditor.setTextToShowWhenEmpty("kick1:MyKit/kick-24\nsnare1:MyKit/snare-14:tuning=-20,attack=25,decay=250", juce::Colours::grey);
    auto instrumentMapText = this->processorRef.getValueTreeState().state["instrument_map"].toString();
    if (instrumentMapText.trim().isEmpty()) {
        instrumentMapText = this->processorRef.getEngine().getSuggestedInstrumentMapText();
        if (instrumentMapText.isNotEmpty())
            this->processorRef.getValueTreeState().state.setProperty("instrument_map", instrumentMapText, nullptr);
    }
    this->instrumentMapEditor.setText(instrumentMapText, false);

    this->applyButton.onClick = [this] {
        struct ProcessingSuspendGuard {
            PluginProcessor& processor;
            explicit ProcessingSuspendGuard(PluginProcessor& proc)
                : processor(proc) {
                this->processor.suspendProcessing(true);
            }
            ~ProcessingSuspendGuard() {
                this->processor.suspendProcessing(false);
            }
        } processingSuspendGuard(this->processorRef);

        const juce::ScopedLock callbackLock(this->processorRef.getCallbackLock());

        auto text = this->instrumentMapEditor.getText();
        this->processorRef.getValueTreeState().state.setProperty("instrument_map", text, nullptr);

        const bool applied = this->processorRef.getEngine().useInstrumentMap(text);
        if (!applied && text.trim().isEmpty()) {
            this->processorRef.getEngine().mapDefaultKit();
            const auto generatedMap = this->processorRef.getEngine().getSuggestedInstrumentMapText();
            this->instrumentMapEditor.setText(generatedMap, false);
            this->processorRef.getValueTreeState().state.setProperty("instrument_map", generatedMap, nullptr);
        }

        this->statusBar.setStatusText(this->processorRef.getEngine().getLastInstrumentMapStatus());
        // Re-populate pad grid after instrument map change
        this->drumPadGrid.populate(this->processorRef.getEngine().getKitLayout());
    };

    // Settings mode
    this->settingsPanel = std::make_unique<EngineSettingsPanel>(this->processorRef.getEngine());
    addChildComponent(*this->settingsPanel);

    // Show default mode
    this->showComponentsForMode(this->activeMode);

    setSize(1000, 580);
}

PluginEditor::~PluginEditor() {
    this->setLookAndFeel(nullptr);
}

void PluginEditor::setViewMode(ViewMode mode) {
    this->activeMode = mode;
    this->showComponentsForMode(mode);

    if (mode == ViewMode::Settings)
        this->settingsPanel->loadFromEngine();

    if (mode == ViewMode::Zen || mode == ViewMode::Zoned) {
        this->drumPadGrid.setPadMode(mode == ViewMode::Zoned ? PadMode::Zoned : PadMode::Zen);
        this->drumPadGrid.populate(this->processorRef.getEngine().getKitLayout());
    }

    this->resized();
}

void PluginEditor::showComponentsForMode(ViewMode mode) {
    // Drum pad grid — visible for Zen and Zoned
    bool showPads = (mode == ViewMode::Zen || mode == ViewMode::Zoned);
    this->drumPadGrid.setVisible(showPads);

    // Raw mode components
    bool showRaw = (mode == ViewMode::Raw);
    this->referencePanel.setVisible(showRaw);
    this->referencePanelLabel.setVisible(showRaw);
    this->instrumentMapEditor.setVisible(showRaw);
    this->instrumentMapLabel.setVisible(showRaw);
    this->applyButton.setVisible(showRaw);

    // Settings
    this->settingsPanel->setVisible(mode == ViewMode::Settings);
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void PluginEditor::resized() {
    auto area = getLocalBounds();

    // Sidebar on left
    constexpr int sidebarWidth = 56;
    this->sidebar.setBounds(area.removeFromLeft(sidebarWidth));

    // Status bar at bottom
    constexpr int statusBarHeight = 24;
    this->statusBar.setBounds(area.removeFromBottom(statusBarHeight));

    // Main area — remaining space
    auto mainArea = area;

    switch (this->activeMode) {
        case ViewMode::Zen:
        case ViewMode::Zoned:
            this->drumPadGrid.setBounds(mainArea);
            break;

        case ViewMode::Raw: {
            auto rawArea = mainArea.reduced(8);
            auto buttonArea = rawArea.removeFromBottom(32);
            this->applyButton.setBounds(buttonArea.removeFromLeft(180));

            const int gap = 8;
            const int refWidth = (rawArea.getWidth() - gap) / 3;
            auto refArea = rawArea.removeFromLeft(refWidth);
            rawArea.removeFromLeft(gap);

            const int labelHeight = 18;
            this->referencePanelLabel.setBounds(refArea.removeFromTop(labelHeight));
            this->referencePanel.setBounds(refArea);
            this->instrumentMapLabel.setBounds(rawArea.removeFromTop(labelHeight));
            this->instrumentMapEditor.setBounds(rawArea);
            break;
        }

        case ViewMode::Settings:
            this->settingsPanel->setBounds(mainArea);
            break;

        case ViewMode::Map:
            // TODO: MidiMapTable
            break;
    }
}
