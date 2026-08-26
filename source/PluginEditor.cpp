#include "PluginEditor.h"
#include "UI/CompactModalDialog.h"
#include "UI/UIComponentStyle.h"
#include "UI/UITheme.h"
#include "Engine/LayoutManager.h"
#include <set>

static auto makeProcessingSuspendGuard(PluginProcessor& processor) {
    processor.suspendProcessing(true);
    return juce::ScopeGuard { [processorPtr = &processor] { processorPtr->suspendProcessing(false); } };
}

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p)
    , processorRef(p) {
    this->setLookAndFeel(&this->lookAndFeel);
    this->tooltipWindow.setOpaque(false);

    this->setupSidebarAndStatusBar();
    this->setupKitPage();
    this->setupDrumPadGridPage();
    this->setupSidebarKnobs();
    this->setupRawPage();
    this->setupMapPage();
    this->setupSettingsPage();
    this->setupTroubleshootingPage();
    this->setupThemePlaygroundPage();
    this->setupInstrumentConfigPage();

    const auto instrumentMap = this->processorRef.getValueTreeState().state["instrument_map"].toString();
    this->activePageId = instrumentMap.trim().isEmpty() ? PageId::Kit : PageId::Zone;
    this->sidebar.setActivePageId(this->activePageId);
    this->showComponentsForPage(this->activePageId);

    setSize(1000, 580);
}

void PluginEditor::setupKitPage() {
    this->kitPage = std::make_unique<KitPage>(this->processorRef.getEngine());
    this->kitPage->setPreviewVelocity(static_cast<float>(this->sidebar.velocityKnob.getValue()));
    this->addChildComponent(*this->kitPage);
    this->kitPage->onStatusMessage = [this](const juce::String& message) {
        this->statusBar.setStatusText(message);
    };
    this->kitPage->onMapLoadRequested = [this](const juce::String& mapText, const juce::String& statusText) {
        return this->applyKitPageInstrumentMap(mapText, statusText);
    };
    this->kitPage->onPresetReferenceChanged = [this](const juce::String& presetReference) {
        this->processorRef.getValueTreeState().state.setProperty("instrument_map_preset_ref", presetReference, nullptr);
    };
    this->kitPage->refresh(this->processorRef.getValueTreeState().state["instrument_map"].toString(), this->processorRef.getValueTreeState().state["instrument_map_preset_ref"].toString());
}

void PluginEditor::setupSidebarAndStatusBar() {
    // Sidebar
    this->addAndMakeVisible(this->sidebar);
    this->sidebar.onPageChanged = [this](PageId pageId) {
        this->setPage(pageId);
    };
    this->statusBar.onUiRequested = [this] {
        this->setPage(PageId::Playground);
    };
    this->statusBar.onInspectRequested = [this] {
        if (!this->inspector) {
            this->inspector = std::make_unique<melatonin::Inspector>(*this);
            this->inspector->onClose = [this]() { this->inspector.reset(); };
        }
        this->inspector->setVisible(true);
    };
    this->statusBar.onLogsRequested = [this] {
        this->setPage(PageId::Fix);
    };

    // Status bar
    this->addAndMakeVisible(this->statusBar);
    this->statusBar.setStatusText(this->processorRef.getEngine().getLastInstrumentMapStatus());
}

void PluginEditor::setupThemePlaygroundPage() {
    this->themePlaygroundPanel = std::make_unique<ThemePlaygroundPanel>();
    this->addChildComponent(*this->themePlaygroundPanel);
}

void PluginEditor::setupDrumPadGridPage() {
    // Drum pad grid (Zone page)
    this->addAndMakeVisible(this->drumPadGrid);
    {
        auto& settings = this->processorRef.getEngine().engineSettings;
        this->drumPadGrid.setGridSize(settings.padGridColumns.load(), settings.padGridRows.load());
        this->drumPadGrid.setPadGap(settings.padGap.load());
    }
    this->refreshDrumPadGridIfNeeded(true);
    this->drumPadGrid.onPadClicked = [this](const KitLayout::InstrumentSlot* slot, int artIdx, const juce::String& previewArticulationCode, bool useDirectPreview, float velocity) {
        if (!slot || artIdx < 0 || artIdx >= static_cast<int>(slot->articulations.size()))
            return;
        if (useDirectPreview) {
            this->flashPadForSlot(slot->id, velocity);
            this->processorRef.getEngine().playPreviewArticulation(slot->id, previewArticulationCode, static_cast<juce::uint8>(velocity));
            return;
        }
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
}

void PluginEditor::setupSidebarKnobs() {
    this->sidebar.velocityKnob.onValueChange = [this] {
        this->drumPadGrid.setClickVelocity(static_cast<float>(this->sidebar.velocityKnob.getValue()));
        this->kitPage->setPreviewVelocity(static_cast<float>(this->sidebar.velocityKnob.getValue()));
        this->sidebar.updateKnobLabels();
    };

    // HiHat openness knob - sends CC to all pdl_opn_cls controllers in the layout
    this->sidebar.hihatKnob.onValueChange = [this] {
        int ccValue = static_cast<int>(this->sidebar.hihatKnob.getValue());
        auto* layout = this->processorRef.getEngine().getKitLayout();
        if (layout != nullptr) {
            std::set<int> sentCCs;
            for (const auto& slot : layout->instrumentSlots) {
                for (const auto& cc : slot.controllers) {
                    if (cc.codeRaw == "pdl_opn_cls" && sentCCs.insert(cc.number).second)
                        this->processorRef.injectMidiFromUI(juce::MidiMessage::controllerEvent(1, cc.number, ccValue));
                }
            }
        }
        this->sidebar.updateKnobLabels();
    };

    // Round-robin lock knob - sets engine setting directly
    this->sidebar.rrLockKnob.onValueChange = [this] {
        this->processorRef.getEngine().engineSettings.roundRobinLock.store(static_cast<int>(this->sidebar.rrLockKnob.getValue()));
        this->sidebar.updateKnobLabels();
    };
}

void PluginEditor::setupRawPage() {
    // Raw page components
    this->addChildComponent(this->instrumentMapEditor);
    this->addChildComponent(this->instrumentMapLabel);
    this->addChildComponent(this->applyButton);

    this->instrumentMapLabel.setText("Instrument Map", juce::dontSendNotification);
    this->instrumentMapLabel.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    this->instrumentMapLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->instrumentMapLabel.setJustificationType(juce::Justification::centredLeft);

    UIComponentStyle::configureMonoTextEditor(this->instrumentMapEditor, UITheme::Colours::textPrimary, true);
    UIComponentStyle::configureBorderlessTextEditor(this->instrumentMapEditor);
    this->instrumentMapEditor.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    this->instrumentMapEditor.setReturnKeyStartsNewLine(true);
    this->instrumentMapEditor.setTextToShowWhenEmpty("kick1/center:MyKit/kick-24/center\nsnare1/center:MyKit/snare-14/center:tuning=-20;attack=25;decay=250", UITheme::Colours::textMuted);
    auto instrumentMapText = this->processorRef.getValueTreeState().state["instrument_map"].toString();
    if (instrumentMapText.trim().isEmpty())
        instrumentMapText = this->processorRef.getEngine().getSuggestedInstrumentMapText();
    this->instrumentMapEditor.setText(instrumentMapText, false);

    UIComponentStyle::configureControlButton(this->applyButton);
    this->applyButton.onClick = [this] { this->applyInstrumentMapFromEditor(); };
}

void PluginEditor::applyInstrumentMapFromEditor() {
    const auto processingGuard = makeProcessingSuspendGuard(this->processorRef);

    const juce::ScopedLock callbackLock(this->processorRef.getCallbackLock());

    auto text = this->processorRef.getEngine().normalizeInstrumentMapText(this->instrumentMapEditor.getText());
    this->instrumentMapEditor.setText(text, false);
    this->processorRef.getValueTreeState().state.setProperty("instrument_map", text, nullptr);

    const bool applied = this->processorRef.getEngine().useInstrumentMap(text);
    if (!applied && text.trim().isEmpty()) {
        this->processorRef.getEngine().mapDefaultKit();
        const auto generatedMap = this->processorRef.getEngine().getSuggestedInstrumentMapText();
        this->instrumentMapEditor.setText(generatedMap, false);
        this->processorRef.getValueTreeState().state.setProperty("instrument_map", generatedMap, nullptr);
    }

    this->statusBar.setStatusText(this->processorRef.getEngine().getLastInstrumentMapStatus());
    this->markDrumPadGridDirty();
    this->markMidiMapTableDirty();
    this->markInstrumentConfigDirty();
    this->refreshDrumPadGridIfNeeded();
    this->refreshMidiMapTableIfNeeded();
}

bool PluginEditor::applyKitPageInstrumentMap(const juce::String& mapText, const juce::String& statusText) {
    const auto processingGuard = makeProcessingSuspendGuard(this->processorRef);

    const juce::ScopedLock callbackLock(this->processorRef.getCallbackLock());
    auto& engine = this->processorRef.getEngine();
    const auto normalizedMapText = engine.normalizeInstrumentMapText(mapText);
    const auto currentMapText = engine.normalizeInstrumentMapText(this->processorRef.getValueTreeState().state["instrument_map"].toString());
    if (normalizedMapText == currentMapText) {
        this->statusBar.setStatusText(statusText + " | instrument map was already active");
        return true;
    }
    if (!engine.useInstrumentMap(normalizedMapText)) {
        this->statusBar.setStatusText(engine.getLastInstrumentMapStatus());
        return false;
    }

    this->processorRef.getValueTreeState().state.setProperty("instrument_map", normalizedMapText, nullptr);
    this->instrumentMapEditor.setText(normalizedMapText, false);
    this->markDrumPadGridDirty();
    this->markMidiMapTableDirty();
    this->markInstrumentConfigDirty();
    this->statusBar.setStatusText(statusText);
    return true;
}

void PluginEditor::setupMapPage() {
    // Map page - MIDI note assignment table
    this->addChildComponent(this->midiMapTable);
    this->refreshMidiMapTableIfNeeded(true);
    this->midiMapTable.onMappingChanged = [this] {
        // Rebuild engine note routing after mapping changes
        auto* layout = this->processorRef.getEngine().getKitLayout();
        if (layout) {
            layout->rebuildIndices();
            this->processorRef.getEngine().rebuildMidiNoteRoutingFromLayout();
        }
        this->statusBar.setStatusText("MIDI mapping updated");
    };
    this->midiMapTable.onLayoutReapplyRequested = [this] {
        auto* layout = this->processorRef.getEngine().getKitLayout();
        if (layout) {
            layout->rebuildIndices();
            this->processorRef.getEngine().rebuildMidiNoteRoutingFromLayout();
        }
        this->markDrumPadGridDirty();
        this->markInstrumentConfigDirty();
        this->statusBar.setStatusText("Expression curve updated");
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
    this->midiMapTable.onCrosstalkPopupRequested = [this] { this->showCrosstalkPopup(); };

    this->midiMapTable.onPresetSelected = [this](const MidiMapTable::PresetEntry& entry) {
        if (entry.isUser) {
            auto userFile = this->processorRef.getEngine().getUserPresetsDir().getChildFile(entry.id + ".yml");
            auto yaml = userFile.loadFileAsString();
            auto parsed = LayoutTraits<KitLayout>::parseYaml(yaml);
            if (parsed.ok()) {
                parsed.value.id = entry.id;
                parsed.value.rebuildIndices();
                this->processorRef.getEngine().setKitLayoutDirect(std::move(parsed.value));
            }
        } else {
            this->processorRef.getEngine().useKitLayout(entry.id);
        }
        this->statusBar.setStatusText("Loaded preset: " + entry.name);
        this->markDrumPadGridDirty();
        this->refreshMidiMapTableIfNeeded(true);
        this->refreshDrumPadGridIfNeeded();
    };
    this->midiMapTable.onPresetSaveRequested = [this] { this->handlePresetSave(); };
    this->midiMapTable.onPresetCloneRequested = [this] { this->handlePresetClone(); };
    this->midiMapTable.onPresetDeleteRequested = [this] { this->handlePresetDelete(); };
    this->midiMapTable.onOpenUserFolderRequested = [this] {
        this->processorRef.getEngine().getUserPresetsDir().createDirectory();
        this->processorRef.getEngine().getUserPresetsDir().startAsProcess();
    };
    this->midiMapTable.onOpenFactoryFolderRequested = [this] {
        auto factoryDir = this->processorRef.getEngine().dataPath.getChildFile("kit_layouts");
        if (factoryDir.isDirectory())
            factoryDir.startAsProcess();
    };
}

void PluginEditor::setupSettingsPage() {
    // Settings page
    this->settingsPanel = std::make_unique<EngineSettingsPanel>(this->processorRef.getEngine());
    this->addChildComponent(*this->settingsPanel);
    this->settingsPanel->onGridSizeChanged = [this] {
        auto& settings = this->processorRef.getEngine().engineSettings;
        this->drumPadGrid.setGridSize(settings.padGridColumns.load(), settings.padGridRows.load());
        this->drumPadGrid.setPadGap(settings.padGap.load());
        this->markDrumPadGridDirty();
        this->refreshDrumPadGridIfNeeded();
    };
    this->settingsPanel->onLibraryReloadRequested = [this] { this->reloadLibraryFromTroubleshooting(); };
    this->settingsPanel->onGlobalVelocityResponseChanged = [this](const juce::String& definition) {
        return this->processorRef.setVelocityResponseDefinition(definition);
    };
}

void PluginEditor::setupTroubleshootingPage() {
    this->troubleshootingPanel = std::make_unique<TroubleshootingPanel>(this->processorRef.getEngine());
    this->addChildComponent(*this->troubleshootingPanel);
    this->troubleshootingPanel->onStatusMessage = [this](const juce::String& message) {
        this->statusBar.setStatusText(message);
    };
    this->troubleshootingPanel->onReloadLibraryRequested = [this] { this->reloadLibraryFromTroubleshooting(); };
}

void PluginEditor::reloadLibraryFromTroubleshooting() {
    const auto processingGuard = makeProcessingSuspendGuard(this->processorRef);

    const juce::ScopedLock callbackLock(this->processorRef.getCallbackLock());
    auto& engine = this->processorRef.getEngine();
    engine.reloadAll();
    engine.useState(this->processorRef.getValueTreeState());

    this->markDrumPadGridDirty();
    this->markMidiMapTableDirty();
    this->markInstrumentConfigDirty();
    this->refreshDrumPadGridIfNeeded(true);
    this->refreshMidiMapTableIfNeeded(true);
    this->refreshInstrumentConfigIfNeeded(true);
    if (this->kitPage)
        this->kitPage->refresh(this->processorRef.getValueTreeState().state["instrument_map"].toString(), this->processorRef.getValueTreeState().state["instrument_map_preset_ref"].toString());
    if (this->troubleshootingPanel)
        this->troubleshootingPanel->refresh();
    this->statusBar.setStatusText("Library reloaded. " + engine.getLastInstrumentMapStatus());
}

void PluginEditor::setupInstrumentConfigPage() {
    // Instrument config page
    this->instrumentConfigPanel = std::make_unique<InstrumentConfigPanel>(this->processorRef.getEngine());
    this->addChildComponent(*this->instrumentConfigPanel);
    this->instrumentConfigPanel->onStatusMessage = [this](const juce::String& msg) {
        this->statusBar.setStatusText(msg);
        this->markDrumPadGridDirty();
        this->markMidiMapTableDirty();
        this->refreshDrumPadGridIfNeeded();
        this->refreshMidiMapTableIfNeeded();
    };
    this->instrumentConfigPanel->onMapTextChanged = [this](const juce::String& mapText) {
        this->processorRef.getValueTreeState().state.setProperty("instrument_map", mapText, nullptr);
        this->instrumentMapEditor.setText(mapText, false);
        this->instrumentConfigSnapshot = mapText;
        this->instrumentConfigDirty = false;
    };
}

PluginEditor::~PluginEditor() {
    this->dismissCrosstalkPopup();
    this->processorRef.setMidiLearnCallback(nullptr);
    this->processorRef.setMidiCaptureEnabled(false);
    this->setLookAndFeel(nullptr);
}

void PluginEditor::markDrumPadGridDirty() {
    this->drumPadGridDirty = true;
}

void PluginEditor::markMidiMapTableDirty() {
    this->midiMapTableDirty = true;
}

void PluginEditor::markInstrumentConfigDirty() {
    this->instrumentConfigDirty = true;
}

void PluginEditor::refreshDrumPadGridIfNeeded(bool force) {
    if (!force && !this->drumPadGridDirty)
        return;
    if (!force && !this->drumPadGrid.isVisible())
        return;

    this->drumPadGrid.populate(this->processorRef.getEngine().getKitLayout());
    this->drumPadGridDirty = false;
}

void PluginEditor::refreshMidiMapTableIfNeeded(bool force) {
    if (!force && !this->midiMapTableDirty)
        return;
    if (!force && !this->midiMapTable.isVisible())
        return;

    this->midiMapTable.populate(this->processorRef.getEngine().getKitLayout());
    this->midiMapTableDirty = false;
    this->refreshPresetList();
}

void PluginEditor::refreshInstrumentConfigIfNeeded(bool force) {//FIXME: is it really needed?
    auto savedMap = this->processorRef.getValueTreeState().state["instrument_map"].toString();
    if (!force && !this->instrumentConfigDirty && savedMap == this->instrumentConfigSnapshot)
        return;
    if (!force && !this->instrumentConfigPanel->isVisible())
        return;

    this->instrumentConfigPanel->populate(savedMap);
    this->instrumentConfigSnapshot = savedMap;
    this->instrumentConfigDirty = false;
}


void PluginEditor::setPage(PageId pageId) {
    if (pageId != this->activePageId)
        this->dismissPadContextPopup();
    if (pageId != PageId::Map)
        this->dismissCrosstalkPopup();
    if (this->activePageId == PageId::Instrument && pageId != PageId::Instrument)
        this->instrumentConfigPanel->flushMapText();

    this->activePageId = pageId;
    this->sidebar.setActivePageId(pageId);
    this->showComponentsForPage(pageId);

    if (pageId == PageId::Settings)
        this->settingsPanel->loadFromEngine();

    if (pageId == PageId::Fix)
        this->troubleshootingPanel->refresh();

    if (pageId == PageId::Kit)
        this->kitPage->refresh(this->processorRef.getValueTreeState().state["instrument_map"].toString(), this->processorRef.getValueTreeState().state["instrument_map_preset_ref"].toString());

    if (pageId == PageId::Zone) {
        this->refreshDrumPadGridIfNeeded();
    }

    if (pageId == PageId::Map)
        this->refreshMidiMapTableIfNeeded();

    if (pageId == PageId::Instrument)
        this->refreshInstrumentConfigIfNeeded();

    this->resized();
    this->statusBar.setUiActive(pageId == PageId::Playground);
    this->statusBar.setLogsActive(pageId == PageId::Fix);
}


void PluginEditor::showComponentsForPage(PageId pageId) {
    this->kitPage->setVisible(pageId == PageId::Kit);

    // Drum pad grid - visible for Zone
    this->drumPadGrid.setVisible(pageId == PageId::Zone);

    // Raw page components
    bool showRaw = (pageId == PageId::Raw);
    this->instrumentMapEditor.setVisible(showRaw);
    this->instrumentMapLabel.setVisible(showRaw);
    this->applyButton.setVisible(showRaw);

    // Map page
    this->midiMapTable.setVisible(pageId == PageId::Map);

    // Instrument config page
    this->instrumentConfigPanel->setVisible(pageId == PageId::Instrument);

    // Settings
    this->settingsPanel->setVisible(pageId == PageId::Settings);

    // Fix
    this->troubleshootingPanel->setVisible(pageId == PageId::Fix);

    // UI theme playground
    this->themePlaygroundPanel->setVisible(pageId == PageId::Playground);
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

    // Main area - remaining space
    auto mainArea = area;

    switch (this->activePageId) {
        case PageId::Kit:
            this->kitPage->setBounds(mainArea);
            break;

        case PageId::Zone:
            this->drumPadGrid.setBounds(mainArea);
            break;

        case PageId::Raw: {
            auto rawArea = mainArea.reduced(UITheme::Spacing::md);
            const int gap = UITheme::Spacing::sm;
            auto headerArea = rawArea.removeFromTop(UITheme::Sizes::standardControlHeight);
            this->applyButton.setBounds(headerArea.removeFromRight(60));
            headerArea.removeFromRight(gap);
            this->instrumentMapLabel.setBounds(headerArea);
            rawArea.removeFromTop(gap);
            this->instrumentMapEditor.setBounds(rawArea);
            break;
        }

        case PageId::Settings:
            this->settingsPanel->setBounds(mainArea);
            break;

        case PageId::Fix:
            this->troubleshootingPanel->setBounds(mainArea);
            break;

        case PageId::Instrument:
            this->instrumentConfigPanel->setBounds(mainArea);
            break;

        case PageId::Map:
            this->midiMapTable.setBounds(mainArea);
            break;

        case PageId::Playground:
            this->themePlaygroundPanel->setBounds(mainArea);
            break;
    }

    if (this->crosstalkPopupOverlay)
        this->crosstalkPopupOverlay->setBounds(this->getLocalBounds());
    if (this->crosstalkPopup) {
        const auto availableBounds = this->getLocalBounds().reduced(32, 24);
        this->crosstalkPopup->setBounds(this->crosstalkPopup->getPreferredBounds(availableBounds));
    }
    if (this->padPopupOverlay) {
        this->padPopupOverlay->setBounds(mainArea);
        const int overlayStripHeight = mainArea.getHeight() - mainArea.getHeight() * 9 / 10;
        const int closeButtonSize = juce::jmin(44, overlayStripHeight);
        this->padPopupOverlay->setCloseButtonBounds({ UITheme::Spacing::md, (overlayStripHeight - closeButtonSize) / 2, closeButtonSize, closeButtonSize });
    }
    if (this->padContextPopup) {
        auto padPanelBounds = mainArea;
        this->padContextPopup->setBounds(padPanelBounds.removeFromBottom(mainArea.getHeight() * 9 / 10));
    }
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
            if (vectorContains(articulation.notes, static_cast<uint8_t>(midiNote))) {
                this->flashPadForSlot(slot.id, velocity);
                return;
            }
        }
    }
}
