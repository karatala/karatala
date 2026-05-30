#include "PluginEditor.h"
#include "UI/UITheme.h"

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
        this->drumPadGrid.setPadGap(settings.padGap);
    }
    this->drumPadGrid.populate(this->processorRef.getEngine().getKitLayout());
    this->drumPadGrid.onPadClicked = [this](const KitLayout::InstrumentSlot* slot, int artIdx, float velocity) {
        if (!slot || artIdx < 0 || artIdx >= static_cast<int>(slot->articulations.size()))
            return;
        auto& art = slot->articulations[static_cast<size_t>(artIdx)];
        if (art.notes.empty())
            return;
        this->flashPadForSlot(slot->id, velocity);
        int midiNote = static_cast<int>(art.notes[0]);
        float vel = velocity / 127.0f;
        this->processorRef.injectMidiFromUI(juce::MidiMessage::noteOn(1, midiNote, vel));
    };
    this->drumPadGrid.onPadRightClicked = [this](const KitLayout::InstrumentSlot* slot, int artIdx, juce::Point<int> screenPos) {
        this->showPadContextPopup(slot, artIdx, screenPos);
    };
    this->sidebar.velocityKnob.onValueChange = [this] {
        this->drumPadGrid.setClickVelocity(static_cast<float>(this->sidebar.velocityKnob.getValue()));
        this->sidebar.updateKnobLabels();
    };

    // HiHat openness knob — sends CC4
    this->sidebar.hihatKnob.onValueChange = [this] {
        int ccValue = static_cast<int>(this->sidebar.hihatKnob.getValue());
        this->processorRef.injectMidiFromUI(juce::MidiMessage::controllerEvent(1, 4, ccValue));
        this->sidebar.updateKnobLabels();
    };

    // Round-robin lock knob — sets engine setting directly
    this->sidebar.rrLockKnob.onValueChange = [this] {
        this->processorRef.getEngine().engineSettings.roundRobinLock = static_cast<int>(this->sidebar.rrLockKnob.getValue());
        this->sidebar.updateKnobLabels();
    };

    // Raw mode components
    addChildComponent(this->referencePanel);
    addChildComponent(this->referencePanelLabel);
    addChildComponent(this->instrumentMapEditor);
    addChildComponent(this->instrumentMapLabel);
    addChildComponent(this->applyButton);

    this->referencePanelLabel.setText("Reference", juce::dontSendNotification);
    this->referencePanelLabel.setFont(UITheme::Fonts::labelBold().withHeight(12.0f));
    this->referencePanelLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->referencePanelLabel.setJustificationType(juce::Justification::centredLeft);

    this->referencePanel.setMultiLine(true);
    this->referencePanel.setReadOnly(true);
    this->referencePanel.setScrollbarsShown(true);
    this->referencePanel.setFont(UITheme::Fonts::mono());
    this->referencePanel.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgEditor);
    this->referencePanel.setColour(juce::TextEditor::textColourId, UITheme::Colours::textInfo);
    this->referencePanel.setText(this->processorRef.getEngine().buildReferenceText(), false);

    this->instrumentMapLabel.setText("Instrument Map", juce::dontSendNotification);
    this->instrumentMapLabel.setFont(UITheme::Fonts::labelBold().withHeight(12.0f));
    this->instrumentMapLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->instrumentMapLabel.setJustificationType(juce::Justification::centredLeft);

    this->instrumentMapEditor.setMultiLine(true);
    this->instrumentMapEditor.setReturnKeyStartsNewLine(true);
    this->instrumentMapEditor.setScrollbarsShown(true);
    this->instrumentMapEditor.setFont(UITheme::Fonts::mono());
    this->instrumentMapEditor.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgEditor);
    this->instrumentMapEditor.setColour(juce::TextEditor::textColourId, UITheme::Colours::textPrimary);
    this->instrumentMapEditor.setTextToShowWhenEmpty("kick1:MyKit/kick-24\nsnare1:MyKit/snare-14:tuning=-20,attack=25,decay=250", UITheme::Colours::textMuted);
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
    this->midiMapTable.onMidiLearnStateChanged = [this](bool learning) {
        if (learning) {
            this->processorRef.setMidiLearnCallback([this](int noteNumber) {
                this->flashPadForMidiNote(noteNumber, 127.0f);
                this->midiMapTable.midiLearnNoteReceived(noteNumber);
            });
        } else {
            this->processorRef.setMidiLearnCallback(nullptr);
        }
    };

    // Settings mode
    this->settingsPanel = std::make_unique<EngineSettingsPanel>(this->processorRef.getEngine());
    addChildComponent(*this->settingsPanel);
    this->settingsPanel->onGridSizeChanged = [this] {
        auto& settings = this->processorRef.getEngine().engineSettings;
        this->drumPadGrid.setGridSize(settings.padGridColumns, settings.padGridRows);
        this->drumPadGrid.setPadGap(settings.padGap);
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
    this->sidebar.setBounds(area.removeFromLeft(UITheme::Sizes::sidebarWidth));

    // Status bar at bottom
    this->statusBar.setBounds(area.removeFromBottom(UITheme::Sizes::statusBarHeight));

    // Main area — remaining space
    auto mainArea = area;

    switch (this->activeMode) {
        case ViewMode::Zen:
        case ViewMode::Zoned:
            this->drumPadGrid.setBounds(mainArea);
            break;

        case ViewMode::Raw: {
            auto rawArea = mainArea.reduced(UITheme::Spacing::md);
            auto buttonArea = rawArea.removeFromBottom(32);
            this->applyButton.setBounds(buttonArea.removeFromLeft(180));

            const int gap = UITheme::Spacing::md;
            const int refWidth = (rawArea.getWidth() - gap) / 3;
            auto refArea = rawArea.removeFromLeft(refWidth);
            rawArea.removeFromLeft(gap);

            const int labelHeight = UITheme::Spacing::rowCompact;
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

void PluginEditor::dismissPadContextPopup() {
    if (this->padContextPopup) {
        this->padContextPopup->flushMapText();
        // Stop MIDI learn when closing popup
        this->processorRef.setMidiLearnCallback(nullptr);
    }

    if (this->padPopupOverlay) {
        removeChildComponent(this->padPopupOverlay.get());
        this->padPopupOverlay.reset();
    }
    if (this->padContextPopup) {
        removeChildComponent(this->padContextPopup.get());
        this->padContextPopup.reset();
    }
}

void PluginEditor::showPadContextPopup(const KitLayout::InstrumentSlot* slot, int artIdx, juce::Point<int> screenPos) {
    this->dismissPadContextPopup();
    if (!slot)
        return;

    // Pass the saved instrument map from ValueTreeState so layers are correctly restored
    auto savedMapText = this->processorRef.getValueTreeState().state["instrument_map"].toString();
    this->padContextPopup = std::make_unique<PadContextPopup>(this->processorRef.getEngine(), *slot, artIdx, savedMapText);
    this->padContextPopup->setPreviewVelocityProvider([this]() {
        return static_cast<int>(this->sidebar.velocityKnob.getValue());
    });
    this->padContextPopup->onDismiss = [this] {
        juce::MessageManager::callAsync([this] { this->dismissPadContextPopup(); });
    };
    this->padContextPopup->onMapTextChanged = [this](const juce::String& mapText) {
        this->processorRef.getValueTreeState().state.setProperty("instrument_map", mapText, nullptr);
        this->instrumentMapEditor.setText(mapText, false);
        this->statusBar.setStatusText(this->processorRef.getEngine().getLastInstrumentMapStatus());
    };
    this->padContextPopup->onMidiMappingChanged = [this] {
        auto* layout = this->processorRef.getEngine().getKitLayout();
        if (layout) layout->rebuildIndices();
        this->statusBar.setStatusText("MIDI mapping updated");
    };
    this->padContextPopup->onMidiLearnStateChanged = [this](bool learning) {
        if (learning) {
            this->processorRef.setMidiLearnCallback([this](int noteNumber) {
                this->flashPadForMidiNote(noteNumber, 127.0f);
                if (this->padContextPopup)
                    this->padContextPopup->midiLearnNoteReceived(noteNumber);
            });
        } else {
            this->processorRef.setMidiLearnCallback(nullptr);
        }
    };

    // Set popup to 80% of editor viewport, centered
    int popupWidth = static_cast<int>(getWidth() * 0.8f);
    int popupHeight = static_cast<int>(getHeight() * 0.8f);
    int popupX = (getWidth() - popupWidth) / 2;
    int popupY = (getHeight() - popupHeight) / 2;

    this->padContextPopup->setBounds(popupX, popupY, popupWidth, popupHeight);

    this->padPopupOverlay = std::make_unique<DismissOverlay>();
    this->padPopupOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] {
            if (this->padContextPopup) {
                this->padContextPopup->flushMapText();
                if (this->padContextPopup->onDismiss) {
                    this->padContextPopup->onDismiss();
                    return;
                }
            }
            this->dismissPadContextPopup();
        });
    };
    this->padPopupOverlay->setBounds(getLocalBounds());
    addAndMakeVisible(*this->padPopupOverlay);
    this->padPopupOverlay->toFront(false);

    addAndMakeVisible(*this->padContextPopup);
    this->padContextPopup->toFront(true);
}

void PluginEditor::flashPadForSlot(const juce::String& slotId, float velocity) {
    if (slotId.isEmpty())
        return;

    this->drumPadGrid.flashPad(slotId, velocity);
}

void PluginEditor::flashPadForMidiNote(int midiNote, float velocity) {
    auto* layout = this->processorRef.getEngine().getKitLayout();
    if (layout == nullptr)
        return;

    for (const auto& slot : layout->instrumentSlots) {
        for (const auto& articulation : slot.articulations) {
            if (std::find(articulation.notes.begin(), articulation.notes.end(), static_cast<uint8_t>(midiNote)) != articulation.notes.end()) {
                this->flashPadForSlot(slot.id, velocity);
                return;
            }
        }
    }
}
