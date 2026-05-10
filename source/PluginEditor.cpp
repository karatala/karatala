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
    {
        auto& settings = this->processorRef.getEngine().engineSettings;
        this->drumPadGrid.setGridSize(settings.padGridColumns, settings.padGridRows);
    }
    this->drumPadGrid.populate(this->processorRef.getEngine().getKitLayout());
    this->drumPadGrid.onPadClicked = [this](const KitLayout::InstrumentSlot* slot, int artIdx, float velocity) {
        if (!slot || artIdx < 0 || artIdx >= static_cast<int>(slot->articulations.size()))
            return;
        auto& art = slot->articulations[static_cast<size_t>(artIdx)];
        if (art.notes.empty())
            return;
        int midiNote = static_cast<int>(art.notes[0]);
        float vel = velocity / 127.0f;
        this->processorRef.injectMidiFromUI(juce::MidiMessage::noteOn(1, midiNote, vel));
    };
    this->sidebar.velocityKnob.onValueChange = [this] {
        this->drumPadGrid.setClickVelocity(static_cast<float>(this->sidebar.velocityKnob.getValue()));
    };

    // HiHat openness knob — sends CC4
    this->sidebar.hihatKnob.onValueChange = [this] {
        int ccValue = static_cast<int>(this->sidebar.hihatKnob.getValue());
        this->processorRef.injectMidiFromUI(juce::MidiMessage::controllerEvent(1, 4, ccValue));
    };

    // Round-robin lock knob — sets engine setting directly
    this->sidebar.rrLockKnob.onValueChange = [this] {
        this->processorRef.getEngine().engineSettings.roundRobinLock = static_cast<int>(this->sidebar.rrLockKnob.getValue());
    };

    // Raw mode components
    addChildComponent(this->referencePanel);
    addChildComponent(this->referencePanelLabel);
    addChildComponent(this->instrumentMapEditor);
    addChildComponent(this->instrumentMapLabel);
    addChildComponent(this->applyButton);

    this->referencePanelLabel.setText("Reference", juce::dontSendNotification);
    this->referencePanelLabel.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    this->referencePanelLabel.setJustificationType(juce::Justification::centredLeft);

    this->referencePanel.setMultiLine(true);
    this->referencePanel.setReadOnly(true);
    this->referencePanel.setScrollbarsShown(true);
    this->referencePanel.setFont(juce::Font(juce::FontOptions(11.0f).withName(juce::Font::getDefaultMonospacedFontName())));
    this->referencePanel.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1e1e1e));
    this->referencePanel.setColour(juce::TextEditor::textColourId, juce::Colour(0xffaaaaaa));
    this->referencePanel.setText(this->processorRef.getEngine().buildReferenceText(), false);

    this->instrumentMapLabel.setText("Instrument Map", juce::dontSendNotification);
    this->instrumentMapLabel.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
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
        this->midiMapTable.populate(this->processorRef.getEngine().getKitLayout());
    };

    // Map mode — MIDI note assignment table
    addChildComponent(this->midiMapTable);
    this->midiMapTable.populate(this->processorRef.getEngine().getKitLayout());
    this->midiMapTable.onMappingChanged = [this] {
        // Rebuild engine note routing after mapping changes
        auto* layout = this->processorRef.getEngine().getKitLayout();
        if (layout) {
            layout->rebuildIndices();
        }
        this->statusBar.setStatusText("MIDI mapping updated");
    };

    // Settings mode
    this->settingsPanel = std::make_unique<EngineSettingsPanel>(this->processorRef.getEngine());
    addChildComponent(*this->settingsPanel);
    this->settingsPanel->onGridSizeChanged = [this] {
        auto& settings = this->processorRef.getEngine().engineSettings;
        this->drumPadGrid.setGridSize(settings.padGridColumns, settings.padGridRows);
        this->drumPadGrid.populate(this->processorRef.getEngine().getKitLayout());
    };

    // Instrument config mode
    this->instrumentConfigPanel = std::make_unique<InstrumentConfigPanel>(this->processorRef.getEngine());
    addChildComponent(*this->instrumentConfigPanel);
    this->instrumentConfigPanel->onStatusMessage = [this](const juce::String& msg) {
        this->statusBar.setStatusText(msg);
        this->drumPadGrid.populate(this->processorRef.getEngine().getKitLayout());
        this->midiMapTable.populate(this->processorRef.getEngine().getKitLayout());
    };
    this->instrumentConfigPanel->onMapTextChanged = [this](const juce::String& mapText) {
        this->processorRef.getValueTreeState().state.setProperty("instrument_map", mapText, nullptr);
        this->instrumentMapEditor.setText(mapText, false);
    };

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

    if (mode == ViewMode::Map)
        this->midiMapTable.populate(this->processorRef.getEngine().getKitLayout());

    if (mode == ViewMode::Instrument) {
        auto savedMap = this->processorRef.getValueTreeState().state["instrument_map"].toString();
        this->instrumentConfigPanel->populate(savedMap);
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

    // Map mode
    this->midiMapTable.setVisible(mode == ViewMode::Map);

    // Instrument config mode
    this->instrumentConfigPanel->setVisible(mode == ViewMode::Instrument);

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

        case ViewMode::Instrument:
            this->instrumentConfigPanel->setBounds(mainArea);
            break;

        case ViewMode::Map:
            this->midiMapTable.setBounds(mainArea);
            break;
    }
}
