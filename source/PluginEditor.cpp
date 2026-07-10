#include "PluginEditor.h"
#include "UI/UIHelpers.h"
#include "UI/UITheme.h"
#include "Config/Helpers.h"
#include "Engine/ArticulationNameHelpers.h"
#include <set>
#include <algorithm>
#include "Engine/LayoutManager.h"

static juce::String makeTargetPath(const DrumKitEngine::InstrumentMapEntry& entry, bool includeArticulation) {
    juce::String path = entry.kitRef.trim();
    if (entry.instCode.trim().isNotEmpty()) path += "/" + entry.instCode.trim();
    if (includeArticulation && !entry.articulationFilter.empty() && entry.articulationFilter.front().trim().isNotEmpty()) path += "/" + entry.articulationFilter.front().trim();
    return path;
}

static juce::String replacementMapPathForSourceItem(const SourceItem& item, bool includeArticulation) {
    juce::String path = item.kitCodeRaw;
    if (item.instCode.isNotEmpty()) path += "/" + item.instCode;
    if (includeArticulation && item.artCode.isNotEmpty()) path += "/" + item.artCode;
    return path;
}

static juce::String instrumentCodeTypePart(const juce::String& instCode) {
    return instCode.upToFirstOccurrenceOf("-", false, false).trim().toLowerCase();
}

static juce::String instrumentCodeSizePart(const juce::String& instCode) {
    juce::StringArray parts;
    parts.addTokens(instCode, "-", "");
    return parts.size() > 1 ? parts[1].trim().toLowerCase() : juce::String();
}

static bool sourceTextContains(const SourceItem& item, const juce::String& text) {
    const auto needle = text.trim().toLowerCase();
    if (needle.isEmpty()) return false;
    return item.fullPath().toLowerCase().contains(needle) || item.rawFullPath().toLowerCase().contains(needle) || item.displayText().toLowerCase().contains(needle);
}
static juce::String articulationExpressionName(const juce::String& artCode) {
    return extractWrappedSegment(artCode, "[", "]").wrapped.trim().toLowerCase();
}

static int hihatStateOrder(const juce::String& stateName) {
    const auto state = stateName.trim().toLowerCase();
    if (state == "open") return 0;
    if (state == "half") return 1;
    if (state == "close") return 2;
    if (state == "tight") return 3;
    if (state == "pedal") return 4;
    return -1;
}

static int hihatStateScore(const juce::String& wantedArtCode, const juce::String& candidateArtCode) {
    const auto wantedState = articulationExpressionName(wantedArtCode);
    const auto candidateState = articulationExpressionName(candidateArtCode);
    if (wantedState.isEmpty() || candidateState.isEmpty()) return 0;
    if (wantedState == candidateState) return 80;

    const int wantedOrder = hihatStateOrder(wantedState);
    const int candidateOrder = hihatStateOrder(candidateState);
    if (wantedOrder < 0 || candidateOrder < 0) return 0;

    const int distance = wantedOrder > candidateOrder ? wantedOrder - candidateOrder : candidateOrder - wantedOrder;
    return juce::jmax(0, 58 - distance * 18);
}

struct MissingInstrumentFixTableRow {
    juce::String slotPath;
    juce::String missingPath;
    std::vector<SourceItem> candidates;
};

struct MissingInstrumentFixTableSelection {
    int rowIndex = -1;
    SourceItem replacement;
};

class MissingInstrumentFixTableComponent : public juce::Component {
public:
    explicit MissingInstrumentFixTableComponent(std::vector<MissingInstrumentFixTableRow> rows)
        : rows(std::move(rows)) {
        this->slotHeader.setText("Slot", juce::dontSendNotification);
        this->missingHeader.setText("Missing path", juce::dontSendNotification);
        this->replacementHeader.setText("Replacement", juce::dontSendNotification);
        UIHelpers::configureCaptionLabel(this->slotHeader, juce::Justification::centredLeft, UITheme::Colours::textPrimary);
        UIHelpers::configureCaptionLabel(this->missingHeader, juce::Justification::centredLeft, UITheme::Colours::textPrimary);
        UIHelpers::configureCaptionLabel(this->replacementHeader, juce::Justification::centredLeft, UITheme::Colours::textPrimary);
        this->addAndMakeVisible(this->slotHeader);
        this->addAndMakeVisible(this->missingHeader);
        this->addAndMakeVisible(this->replacementHeader);
        this->addAndMakeVisible(this->viewport);
        this->viewport.setViewedComponent(&this->contentComponent, false);

        for (int rowIndex = 0; rowIndex < static_cast<int>(this->rows.size()); ++rowIndex) {
            auto* slotLabel = this->slotLabels.add(new juce::Label());
            auto* missingLabel = this->missingLabels.add(new juce::Label());
            auto* comboBox = this->replacementCombos.add(new juce::ComboBox());
            UIHelpers::configureTableRowLabel(*slotLabel, UITheme::Colours::textSecondary, juce::Justification::centredLeft);
            UIHelpers::configureTableRowLabel(*missingLabel, UITheme::Colours::textSecondary, juce::Justification::centredLeft);
            UIHelpers::configureComboBox(*comboBox);
            slotLabel->setText(this->rows[static_cast<size_t>(rowIndex)].slotPath, juce::dontSendNotification);
            missingLabel->setText(this->rows[static_cast<size_t>(rowIndex)].missingPath, juce::dontSendNotification);
            slotLabel->setTooltip(this->rows[static_cast<size_t>(rowIndex)].slotPath);
            missingLabel->setTooltip(this->rows[static_cast<size_t>(rowIndex)].missingPath);
            comboBox->addItem("Skip", 1);
            int itemId = 2;
            for (const auto& candidate : this->rows[static_cast<size_t>(rowIndex)].candidates)
                comboBox->addItem(candidate.fullPath(), itemId++);
            comboBox->setSelectedId(this->rows[static_cast<size_t>(rowIndex)].candidates.empty() ? 1 : 2, juce::dontSendNotification);
            this->contentComponent.addAndMakeVisible(slotLabel);
            this->contentComponent.addAndMakeVisible(missingLabel);
            this->contentComponent.addAndMakeVisible(comboBox);
        }
    }

    int getPreferredHeight() const {
        const int visibleRows = juce::jlimit(1, 10, static_cast<int>(this->rows.size()));
        return headerHeight + visibleRows * rowHeight + 8;
    }

    std::vector<MissingInstrumentFixTableSelection> getSelections() const {
        std::vector<MissingInstrumentFixTableSelection> selections;
        for (int rowIndex = 0; rowIndex < this->replacementCombos.size(); ++rowIndex) {
            const int selectedIndex = this->replacementCombos[rowIndex]->getSelectedId() - 2;
            if (selectedIndex < 0) continue;
            const auto& row = this->rows[static_cast<size_t>(rowIndex)];
            if (selectedIndex >= static_cast<int>(row.candidates.size())) continue;
            MissingInstrumentFixTableSelection selection;
            selection.rowIndex = rowIndex;
            selection.replacement = row.candidates[static_cast<size_t>(selectedIndex)];
            selections.push_back(std::move(selection));
        }
        return selections;
    }

    void resized() override {
        auto area = this->getLocalBounds();
        auto headerArea = area.removeFromTop(headerHeight);
        const int slotWidth = 120;
        const int missingWidth = 250;
        this->slotHeader.setBounds(headerArea.removeFromLeft(slotWidth));
        this->missingHeader.setBounds(headerArea.removeFromLeft(missingWidth));
        this->replacementHeader.setBounds(headerArea);

        this->viewport.setBounds(area);
        const int contentWidth = juce::jmax(area.getWidth() - this->viewport.getScrollBarThickness(), slotWidth + missingWidth + 220);
        this->contentComponent.setBounds(0, 0, contentWidth, static_cast<int>(this->rows.size()) * rowHeight);

        for (int rowIndex = 0; rowIndex < static_cast<int>(this->rows.size()); ++rowIndex) {
            auto rowArea = juce::Rectangle<int>(0, rowIndex * rowHeight, contentWidth, rowHeight).reduced(0, 2);
            this->slotLabels[rowIndex]->setBounds(rowArea.removeFromLeft(slotWidth).reduced(0, 1));
            this->missingLabels[rowIndex]->setBounds(rowArea.removeFromLeft(missingWidth).reduced(0, 1));
            this->replacementCombos[rowIndex]->setBounds(rowArea.reduced(0, 1));
        }
    }

private:
    static constexpr int headerHeight = 22;
    static constexpr int rowHeight = 30;

    std::vector<MissingInstrumentFixTableRow> rows;
    juce::Label slotHeader;
    juce::Label missingHeader;
    juce::Label replacementHeader;
    juce::Viewport viewport;
    juce::Component contentComponent;
    juce::OwnedArray<juce::Label> slotLabels;
    juce::OwnedArray<juce::Label> missingLabels;
    juce::OwnedArray<juce::ComboBox> replacementCombos;
};


PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p)
    , processorRef(p) {
    this->setLookAndFeel(&this->lookAndFeel);

    this->setupSidebarAndStatusBar();
    this->setupKitMode();
    this->setupDrumPadGridMode();
    this->setupSidebarKnobs();
    this->setupRawMode();
    this->setupMapMode();
    this->setupSettingsMode();
    this->setupTroubleshootingMode();
    this->setupInstrumentConfigMode();

    const auto instrumentMap = this->processorRef.getValueTreeState().state["instrument_map"].toString();
    this->activeMode = instrumentMap.trim().isEmpty() ? ViewMode::Kit : ViewMode::Zone;
    this->sidebar.setActiveMode(this->activeMode);
    this->showComponentsForMode(this->activeMode);

    setSize(1000, 580);
}

void PluginEditor::setupKitMode() {
    this->kitHomePanel = std::make_unique<KitHomePanel>(this->processorRef.getEngine());
    this->addChildComponent(*this->kitHomePanel);
    this->kitHomePanel->onStatusMessage = [this](const juce::String& message) {
        this->statusBar.setStatusText(message);
    };
    this->kitHomePanel->onLoadRequested = [this](KitHomePanel::Commit commit) {
        struct ProcessingSuspendGuard {
            PluginProcessor& processor;
            explicit ProcessingSuspendGuard(PluginProcessor& processor)
                : processor(processor) {
                this->processor.suspendProcessing(true);
            }
            ~ProcessingSuspendGuard() {
                this->processor.suspendProcessing(false);
            }
        } processingGuard(this->processorRef);

        const juce::ScopedLock callbackLock(this->processorRef.getCallbackLock());
        auto& engine = this->processorRef.getEngine();
        if (!engine.setKitLayoutDirect(commit.layout, false)) {
            this->statusBar.setStatusText("Could not apply kit layout");
            return;
        }
        if (!engine.useInstrumentMap(commit.instrumentMapText)) {
            this->statusBar.setStatusText(engine.getLastInstrumentMapStatus());
            return;
        }

        auto& state = this->processorRef.getValueTreeState().state;
        state.setProperty("kit_layout", commit.layout.id, nullptr);
        state.setProperty("instrument_map", commit.instrumentMapText, nullptr);
        this->instrumentMapEditor.setText(commit.instrumentMapText, false);

        juce::StringArray status;
        status.add(engine.getLastInstrumentMapStatus());
        if (commit.setGlobalKitLayout && !engine.setGlobalKitLayoutId(commit.layout.id))
            status.add("Could not save the default kit layout");
        if (commit.globalMicLayoutId.isNotEmpty()
            && commit.globalMicLayoutId != engine.getGlobalMicLayoutId()) {
            if (engine.setGlobalMicLayoutId(commit.globalMicLayoutId))
                status.add("Mic layout saved for newly created plugin instances");
            else
                status.add("Could not save the global mic layout");
        }

        this->markDrumPadGridDirty();
        this->markMidiMapTableDirty();
        this->markInstrumentConfigDirty();
        this->statusBar.setStatusText(status.joinIntoString(" | "));
        this->setViewMode(ViewMode::Zone);
    };
    this->kitHomePanel->refresh(this->processorRef.getValueTreeState().state["instrument_map"].toString());
}

void PluginEditor::setupSidebarAndStatusBar() {
    // Sidebar
    this->addAndMakeVisible(this->sidebar);
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
    this->addAndMakeVisible(this->statusBar);
    this->statusBar.setStatusText(this->processorRef.getEngine().getLastInstrumentMapStatus());
}

void PluginEditor::setupDrumPadGridMode() {
    // Drum pad grid (Zen / Zone modes)
    this->addAndMakeVisible(this->drumPadGrid);
    {
        auto& settings = this->processorRef.getEngine().engineSettings;
        this->drumPadGrid.setGridSize(settings.padGridColumns.load(), settings.padGridRows.load());
        this->drumPadGrid.setPadGap(settings.padGap.load());
    }
    this->refreshDrumPadGridIfNeeded(true);
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
}

void PluginEditor::setupSidebarKnobs() {
    this->sidebar.velocityKnob.onValueChange = [this] {
        this->drumPadGrid.setClickVelocity(static_cast<float>(this->sidebar.velocityKnob.getValue()));
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

void PluginEditor::setupRawMode() {
    // Raw mode components
    this->addChildComponent(this->instrumentMapEditor);
    this->addChildComponent(this->instrumentMapLabel);
    this->addChildComponent(this->applyButton);

    this->instrumentMapLabel.setText("Instrument Map", juce::dontSendNotification);
    this->instrumentMapLabel.setFont(UITheme::Fonts::labelBoldMedium());
    this->instrumentMapLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->instrumentMapLabel.setJustificationType(juce::Justification::centredLeft);

    UIHelpers::configureMonoTextEditor(this->instrumentMapEditor, UITheme::Colours::textPrimary, true);
    this->instrumentMapEditor.setReturnKeyStartsNewLine(true);
    this->instrumentMapEditor.setTextToShowWhenEmpty("kick1/center:MyKit/kick-24/center\nsnare1/center:MyKit/snare-14/center:tuning=-20;attack=25;decay=250", UITheme::Colours::textMuted);
    auto instrumentMapText = this->processorRef.getValueTreeState().state["instrument_map"].toString();
    if (instrumentMapText.trim().isEmpty())
        instrumentMapText = this->processorRef.getEngine().getSuggestedInstrumentMapText();
    this->instrumentMapEditor.setText(instrumentMapText, false);

    this->applyButton.onClick = [this] { this->applyInstrumentMapFromEditor(); };
}

void PluginEditor::applyInstrumentMapFromEditor() {
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

void PluginEditor::setupMapMode() {
    // Map mode - MIDI note assignment table
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

void PluginEditor::setupSettingsMode() {
    // Settings mode
    this->settingsPanel = std::make_unique<EngineSettingsPanel>(this->processorRef.getEngine());
    this->addChildComponent(*this->settingsPanel);
    this->settingsPanel->onGridSizeChanged = [this] {
        auto& settings = this->processorRef.getEngine().engineSettings;
        this->drumPadGrid.setGridSize(settings.padGridColumns.load(), settings.padGridRows.load());
        this->drumPadGrid.setPadGap(settings.padGap.load());
        this->markDrumPadGridDirty();
        this->refreshDrumPadGridIfNeeded();
    };
}

void PluginEditor::setupTroubleshootingMode() {
    this->troubleshootingPanel = std::make_unique<TroubleshootingPanel>(this->processorRef.getEngine());
    this->addChildComponent(*this->troubleshootingPanel);
    this->troubleshootingPanel->onStatusMessage = [this](const juce::String& message) {
        this->statusBar.setStatusText(message);
    };
    this->troubleshootingPanel->onReloadLibraryRequested = [this] { this->reloadLibraryFromTroubleshooting(); };
    this->troubleshootingPanel->onFixMissingInstrumentsRequested = [this] { this->fixMissingInstrumentsFromTroubleshooting(); };
}

void PluginEditor::reloadLibraryFromTroubleshooting() {
    struct ProcessingSuspendGuard {
        PluginProcessor& processor;
        explicit ProcessingSuspendGuard(PluginProcessor& processor)
            : processor(processor) {
            this->processor.suspendProcessing(true);
        }
        ~ProcessingSuspendGuard() {
            this->processor.suspendProcessing(false);
        }
    } processingGuard(this->processorRef);

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
    if (this->kitHomePanel)
        this->kitHomePanel->refresh(this->processorRef.getValueTreeState().state["instrument_map"].toString());
    if (this->troubleshootingPanel)
        this->troubleshootingPanel->refresh();
    this->statusBar.setStatusText("Library reloaded. " + engine.getLastInstrumentMapStatus());
}

static KitLib::InstrumentSpec::Type findSlotTypeForMapEntry(const KitLayout* layout, const DrumKitEngine::InstrumentMapEntry& entry) {
    if (layout == nullptr) return KitLib::InstrumentSpec::Type::Unknown;
    for (const auto& slot : layout->instrumentSlots)
        if (slot.id == entry.slotId) return slot.type;
    return KitLib::InstrumentSpec::Type::Unknown;
}

std::vector<PluginEditor::MissingInstrumentFixChoice> PluginEditor::collectMissingInstrumentFixChoices(const juce::String& mapText) const {
    std::vector<MissingInstrumentFixChoice> choices;
    auto parsed = DrumKitEngine::parseInstrumentMapText(mapText);
    if (!parsed.ok()) return choices;

    auto* libraryManager = this->processorRef.getEngine().getInstrumentManager();
    if (libraryManager == nullptr) return choices;

    auto makeSlotPath = [](const DrumKitEngine::InstrumentMapEntry& entry) {
        juce::String slotPath = entry.slotId.trim();
        if (!entry.slotArticulationFilter.empty() && entry.slotArticulationFilter.front().trim().isNotEmpty()) slotPath += "/" + entry.slotArticulationFilter.front().trim();
        return slotPath;
    };

    auto addChoice = [&choices, &makeSlotPath](const DrumKitEngine::InstrumentMapEntry& entry, int entryIndex, const juce::String& oldPath, const juce::String& reason, KitLib::InstrumentSpec::Type slotType) {
        const auto oldPathKey = oldPath.trim().toLowerCase();
        if (oldPathKey.isEmpty()) return;
        for (auto& existingChoice : choices) {
            if (existingChoice.oldPath.trim().toLowerCase() == oldPathKey) {
                existingChoice.entryIndexes.push_back(entryIndex);
                const auto slotPath = makeSlotPath(entry);
                if (slotPath.isNotEmpty() && !existingChoice.slotPath.contains(slotPath)) existingChoice.slotPath = existingChoice.slotPath.isEmpty() ? slotPath : existingChoice.slotPath + ", " + slotPath;
                return;
            }
        }

        MissingInstrumentFixChoice choice;
        choice.oldPath = oldPath.trim();
        choice.reason = reason;
        choice.slotPath = makeSlotPath(entry);
        choice.wantedInstCode = entry.instCode;
        choice.wantedArtCode = entry.articulationFilter.empty() ? juce::String() : entry.articulationFilter.front();
        choice.slotType = slotType;
        choice.entryIndexes.push_back(entryIndex);
        choices.push_back(std::move(choice));
    };

    const auto* layout = this->processorRef.getEngine().getKitLayout();
    for (int entryIndex = 0; entryIndex < static_cast<int>(parsed.value.entries.size()); ++entryIndex) {
        const auto& entry = parsed.value.entries[static_cast<size_t>(entryIndex)];
        const auto slotType = findSlotTypeForMapEntry(layout, entry);
        const auto* kitSpec = libraryManager->findKitById(entry.kitRef);
        if (kitSpec == nullptr) {
            addChoice(entry, entryIndex, makeTargetPath(entry, false), "Kit not found or ambiguous", slotType);
            continue;
        }

        if (entry.instCode.trim().isEmpty()) {
            addChoice(entry, entryIndex, makeTargetPath(entry, false), "Instrument code is empty", slotType);
            continue;
        }

        const auto* instrumentSpec = kitSpec->findInstrumentById(entry.instCode);
        if (instrumentSpec == nullptr) {
            addChoice(entry, entryIndex, makeTargetPath(entry, false), "Instrument not found or ambiguous", slotType);
            continue;
        }

        for (const auto& artCode : entry.articulationFilter) {
            if (artCode.trim().isEmpty()) continue;
            if (instrumentSpec->findArticulationById(artCode) == nullptr)
                addChoice(entry, entryIndex, entry.kitRef + "/" + entry.instCode + "/" + artCode, "Articulation not found or ambiguous", slotType);
        }
    }

    for (auto& choice : choices)
        choice.candidates = this->buildReplacementCandidates(choice);

    return choices;
}

std::vector<SourceItem> PluginEditor::buildReplacementCandidates(const MissingInstrumentFixChoice& choice) const {
    auto sourceItems = UIHelpers::buildSourceItems(this->processorRef.getEngine().getInstrumentManager());

    struct ScoredSourceItem {
        SourceItem item;
        int score = 0;
    };

    std::vector<ScoredSourceItem> scoredItems;
    scoredItems.reserve(sourceItems.size());
    for (const auto& item : sourceItems) {
        ScoredSourceItem scoredItem;
        scoredItem.item = item;
        int score = 0;
        if (choice.slotType != KitLib::InstrumentSpec::Type::Unknown && item.instType == choice.slotType) score += 120;

        const auto wantedType = instrumentCodeTypePart(choice.wantedInstCode);
        if (wantedType.isNotEmpty() && (instrumentCodeTypePart(item.instCode) == wantedType || instrumentCodeTypePart(item.rawInstCode) == wantedType)) score += 45;

        const auto wantedSize = instrumentCodeSizePart(choice.wantedInstCode);
        if (wantedSize.isNotEmpty() && (instrumentCodeSizePart(item.instCode) == wantedSize || instrumentCodeSizePart(item.rawInstCode) == wantedSize)) score += 35;

        const auto wantedArt = ArticulationNameHelpers::normalizeArticulationName(choice.wantedArtCode);
        if (wantedArt.isNotEmpty()) {
            if (ArticulationNameHelpers::normalizeArticulationName(item.artCode) == wantedArt) score += 55;
            if (ArticulationNameHelpers::normalizeArticulationName(item.rawArtCode) == wantedArt) score += 55;
            score += hihatStateScore(choice.wantedArtCode, item.artCode);
            score += hihatStateScore(choice.wantedArtCode, item.rawArtCode);
        }

        if (sourceTextContains(item, choice.wantedInstCode)) score += 12;
        if (sourceTextContains(item, choice.wantedArtCode)) score += 8;
        scoredItem.score = score;
        scoredItems.push_back(std::move(scoredItem));
    }

    std::sort(scoredItems.begin(), scoredItems.end(), [](const auto& leftItem, const auto& rightItem) {
        if (leftItem.score != rightItem.score) return leftItem.score > rightItem.score;
        return leftItem.item.displayText() < rightItem.item.displayText();
    });

    std::vector<SourceItem> result;
    juce::StringArray seenReplacementPaths;
    const bool includeArticulation = choice.wantedArtCode.trim().isNotEmpty();
    const int maxCandidateCount = 25;
    for (int itemIndex = 0; itemIndex < static_cast<int>(scoredItems.size()) && static_cast<int>(result.size()) < maxCandidateCount; ++itemIndex) {
        const auto replacementPath = replacementMapPathForSourceItem(scoredItems[static_cast<size_t>(itemIndex)].item, includeArticulation).toLowerCase();
        if (seenReplacementPaths.contains(replacementPath)) continue;
        seenReplacementPaths.add(replacementPath);
        result.push_back(scoredItems[static_cast<size_t>(itemIndex)].item);
    }
    return result;
}


void PluginEditor::applyFixedInstrumentMap(const juce::String& mapText, const juce::String& statusPrefix) {
    struct ProcessingSuspendGuard {
        PluginProcessor& processor;
        explicit ProcessingSuspendGuard(PluginProcessor& processor)
            : processor(processor) {
            this->processor.suspendProcessing(true);
        }
        ~ProcessingSuspendGuard() {
            this->processor.suspendProcessing(false);
        }
    } processingGuard(this->processorRef);

    const juce::ScopedLock callbackLock(this->processorRef.getCallbackLock());
    auto& engine = this->processorRef.getEngine();
    const auto normalizedMapText = engine.normalizeInstrumentMapText(mapText);
    this->instrumentMapEditor.setText(normalizedMapText, false);
    this->processorRef.getValueTreeState().state.setProperty("instrument_map", normalizedMapText, nullptr);
    engine.useInstrumentMap(normalizedMapText);

    this->markDrumPadGridDirty();
    this->markMidiMapTableDirty();
    this->markInstrumentConfigDirty();
    this->refreshDrumPadGridIfNeeded(true);
    this->refreshMidiMapTableIfNeeded(true);
    this->refreshInstrumentConfigIfNeeded(true);
    if (this->troubleshootingPanel)
        this->troubleshootingPanel->refresh();

    const auto status = engine.getLastInstrumentMapStatus().isNotEmpty() ? engine.getLastInstrumentMapStatus() : juce::String("Instrument map checked");
    this->statusBar.setStatusText(statusPrefix + status);
}

void PluginEditor::showMissingInstrumentFixTable(const juce::String& mapText) {
    auto parsed = DrumKitEngine::parseInstrumentMapText(mapText);
    if (!parsed.ok()) {
        this->statusBar.setStatusText(parsed.error());
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Missing Instruments", parsed.error());
        return;
    }

    auto choices = this->collectMissingInstrumentFixChoices(mapText);
    if (choices.empty()) {
        this->statusBar.setStatusText("No missing instruments found");
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Missing Instruments", "No missing instruments found");
        return;
    }

    std::vector<MissingInstrumentFixTableRow> tableRows;
    tableRows.reserve(choices.size());
    for (const auto& choice : choices) {
        MissingInstrumentFixTableRow row;
        row.slotPath = choice.slotPath;
        row.missingPath = choice.oldPath;
        row.candidates = choice.candidates;
        tableRows.push_back(std::move(row));
    }

    auto* table = new MissingInstrumentFixTableComponent(std::move(tableRows));
    table->setSize(760, table->getPreferredHeight());

    auto* dialog = new juce::AlertWindow("Fix Missing Instruments", "Choose replacements, then confirm once.", juce::MessageBoxIconType::NoIcon);
    dialog->addCustomComponent(table);
    dialog->addButton("Confirm", 1);
    dialog->addButton("Cancel", 0);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, dialog, table, mapText, choices](int result) mutable {
        juce::String updatedMapText = mapText;
        int appliedCount = 0;

        if (result == 1) {
            auto parsedForApply = DrumKitEngine::parseInstrumentMapText(mapText);
            if (parsedForApply.ok()) {
                const auto selections = table->getSelections();
                for (const auto& selection : selections) {
                    if (selection.rowIndex < 0 || selection.rowIndex >= static_cast<int>(choices.size())) continue;
                    const auto& choice = choices[static_cast<size_t>(selection.rowIndex)];
                    for (const auto entryIndex : choice.entryIndexes) {
                        if (entryIndex < 0 || entryIndex >= static_cast<int>(parsedForApply.value.entries.size())) continue;
                        auto& entry = parsedForApply.value.entries[static_cast<size_t>(entryIndex)];
                        const auto oldArtFilter = entry.articulationFilter;
                        const bool slotArtWasImplicit = entry.slotArticulationFilter == entry.articulationFilter;
                        entry.kitRef = selection.replacement.kitCodeRaw;
                        entry.instCode = selection.replacement.instCode;
                        if (!oldArtFilter.empty() && selection.replacement.artCode.isNotEmpty()) {
                            entry.articulationFilter.clear();
                            entry.articulationFilter.push_back(selection.replacement.artCode);
                            if (slotArtWasImplicit) entry.slotArticulationFilter = entry.articulationFilter;
                        }
                    }
                    ++appliedCount;
                }
                updatedMapText = DrumKitEngine::serializeInstrumentMap(parsedForApply.value);
            }
        }

        dialog->removeCustomComponent(0);
        delete table;

        if (result != 1) {
            this->statusBar.setStatusText("Missing instrument fixer cancelled");
            return;
        }

        if (appliedCount == 0) {
            this->statusBar.setStatusText("No replacements selected");
            return;
        }

        this->applyFixedInstrumentMap(updatedMapText, "Updated " + juce::String(appliedCount) + " missing path(s). ");
    }), true);
}

void PluginEditor::fixMissingInstrumentsFromTroubleshooting() {
    auto mapText = this->instrumentMapEditor.getText();
    if (mapText.trim().isEmpty())
        mapText = this->processorRef.getValueTreeState().state["instrument_map"].toString();
    this->showMissingInstrumentFixTable(mapText);
}

void PluginEditor::setupInstrumentConfigMode() {
    // Instrument config mode
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

void PluginEditor::refreshPresetList() {
    std::vector<MidiMapTable::PresetEntry> entries;

    MidiMapTable::PresetEntry defaultEntry;
    auto defaultLayout = KitLayout::getDefault();
    defaultEntry.id = defaultLayout.id;
    defaultEntry.name = defaultLayout.name.isNotEmpty() ? defaultLayout.name : defaultLayout.id;
    defaultEntry.isUser = false;
    entries.push_back(defaultEntry);

    // Factory presets from kit_layouts dir
    auto& engine = this->processorRef.getEngine();
    auto factoryLayouts = engine.getKitLayoutManager()->getLayouts();
    for (const auto& layout : factoryLayouts) {
        if (layout.id == defaultEntry.id) continue;
        MidiMapTable::PresetEntry entry;
        entry.id = layout.id;
        entry.name = layout.name.isNotEmpty() ? layout.name : layout.id;
        entry.isUser = false;
        entries.push_back(entry);
    }

    // User presets from presets dir
    auto userDir = engine.getUserPresetsDir();
    if (userDir.isDirectory()) {
        auto files = userDir.findChildFiles(juce::File::findFiles, false, "*.yml;*.yaml");
        for (const auto& file : files) {
            auto yaml = file.loadFileAsString();
            auto parsed = LayoutTraits<KitLayout>::parseYaml(yaml);
            MidiMapTable::PresetEntry entry;
            entry.id = file.getFileNameWithoutExtension();
            entry.name = parsed.ok() && parsed.value.name.isNotEmpty() ? parsed.value.name : entry.id;
            entry.isUser = true;
            entries.push_back(entry);
        }
    }

    auto* layout = engine.getKitLayout();
    juce::String currentId = layout ? layout->id : juce::String();
    this->midiMapTable.populatePresets(entries, currentId);
}

void PluginEditor::saveLayoutToUserPresets(const juce::String& fileName, const KitLayout& layout) {
    auto userDir = this->processorRef.getEngine().getUserPresetsDir();
    userDir.createDirectory();
    auto file = userDir.getChildFile(fileName + ".yml");
    auto yaml = LayoutTraits<KitLayout>::toYaml(layout);
    file.replaceWithText(yaml);
}

juce::String PluginEditor::makeUniqueCopyName(const juce::String& baseName) {
    auto userDir = this->processorRef.getEngine().getUserPresetsDir();
    int num = 1;
    juce::String candidate = baseName + " Copy";
    while (userDir.getChildFile(candidate + ".yml").exists() ||
           userDir.getChildFile(candidate + ".yaml").exists()) {
        num++;
        candidate = baseName + " Copy " + juce::String(num);
    }
    return candidate;
}

void PluginEditor::handlePresetSave() {
    auto* layout = this->processorRef.getEngine().getKitLayout();
    if (!layout) return;

    auto userDir = this->processorRef.getEngine().getUserPresetsDir();
    bool isUser = userDir.getChildFile(layout->id + ".yml").exists() ||
                  userDir.getChildFile(layout->id + ".yaml").exists();

    if (isUser) {
        this->saveLayoutToUserPresets(layout->id, *layout);
        this->statusBar.setStatusText("Preset saved: " + layout->name);
        this->refreshPresetList();
    } else {
        juce::String defaultName = this->makeUniqueCopyName(layout->name.isNotEmpty() ? layout->name : layout->id);
        auto* dlg = new juce::AlertWindow("Save As User Preset", "Enter preset name:", juce::MessageBoxIconType::QuestionIcon);
        dlg->addTextEditor("name", defaultName, "Name:");
        dlg->addButton("Save", 1);
        dlg->addButton("Cancel", 0);
        KitLayout layoutCopy = *layout;
        dlg->enterModalState(true, juce::ModalCallbackFunction::create([this, dlg, layoutCopy](int result) mutable {
            if (result == 1) {
                juce::String name = dlg->getTextEditorContents("name").trim();
                if (name.isEmpty()) return;
                juce::String fileName = name.replaceCharacters("\\/:|*?<>\"", "_________");
                layoutCopy.name = name;
                layoutCopy.id = fileName;
                this->saveLayoutToUserPresets(fileName, layoutCopy);
                this->processorRef.getEngine().setKitLayoutDirect(layoutCopy);
                this->statusBar.setStatusText("Saved as: " + name);
                this->markDrumPadGridDirty();
                this->refreshMidiMapTableIfNeeded(true);
                this->refreshDrumPadGridIfNeeded();
            }
        }), true);
    }
}

void PluginEditor::handlePresetClone() {
    auto* layout = this->processorRef.getEngine().getKitLayout();
    if (!layout) return;

    juce::String baseName = layout->name.isNotEmpty() ? layout->name : layout->id;
    juce::String defaultName = this->makeUniqueCopyName(baseName);
    auto* dlg = new juce::AlertWindow("Clone Preset", "Enter name for the copy:", juce::MessageBoxIconType::QuestionIcon);
    dlg->addTextEditor("name", defaultName, "Name:");
    dlg->addButton("Clone", 1);
    dlg->addButton("Cancel", 0);
    KitLayout layoutCopy = *layout;
    dlg->enterModalState(true, juce::ModalCallbackFunction::create([this, dlg, layoutCopy](int result) mutable {
        if (result == 1) {
            juce::String name = dlg->getTextEditorContents("name").trim();
            if (name.isEmpty()) return;
            juce::String fileName = name.replaceCharacters("\\/:|*?<>\"", "_________");
            layoutCopy.name = name;
            layoutCopy.id = fileName;
            this->saveLayoutToUserPresets(fileName, layoutCopy);
            this->processorRef.getEngine().setKitLayoutDirect(layoutCopy);
            this->statusBar.setStatusText("Cloned as: " + name);
            this->markDrumPadGridDirty();
            this->refreshMidiMapTableIfNeeded(true);
            this->refreshDrumPadGridIfNeeded();
        }
    }), true);
}

void PluginEditor::handlePresetDelete() {
    auto* layout = this->processorRef.getEngine().getKitLayout();
    if (!layout) return;

    auto userDir = this->processorRef.getEngine().getUserPresetsDir();
    auto file = userDir.getChildFile(layout->id + ".yml");
    if (!file.exists()) file = userDir.getChildFile(layout->id + ".yaml");
    if (!file.exists()) return;

    juce::String presetName = layout->name.isNotEmpty() ? layout->name : layout->id;
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Delete Preset")
            .withMessage("Delete \"" + presetName + "\"? This cannot be undone.")
            .withButton("Delete")
            .withButton("Cancel"),
        [this, file](int result) {
            if (result == 1) {
                file.deleteFile();
                this->statusBar.setStatusText("Preset deleted");
                this->refreshPresetList();
            }
        });
}

void PluginEditor::setViewMode(ViewMode mode) {
    if (mode != ViewMode::Map)
        this->dismissCrosstalkPopup();
    if (this->activeMode == ViewMode::Instrument && mode != ViewMode::Instrument)
        this->instrumentConfigPanel->flushMapText();

    this->activeMode = mode;
    this->sidebar.setActiveMode(mode);
    this->showComponentsForMode(mode);

    if (mode == ViewMode::Settings)
        this->settingsPanel->loadFromEngine();

    if (mode == ViewMode::Fix)
        this->troubleshootingPanel->refresh();

    if (mode == ViewMode::Kit)
        this->kitHomePanel->refresh(this->processorRef.getValueTreeState().state["instrument_map"].toString());

    if (mode == ViewMode::Zen || mode == ViewMode::Zone) {
        this->refreshDrumPadGridIfNeeded();
        this->drumPadGrid.setPadMode(mode == ViewMode::Zone ? PadMode::Zone : PadMode::Zen);
    }

    if (mode == ViewMode::Map)
        this->refreshMidiMapTableIfNeeded();

    if (mode == ViewMode::Instrument)
        this->refreshInstrumentConfigIfNeeded();

    this->resized();
}

void PluginEditor::showCrosstalkPopup() {
    this->dismissCrosstalkPopup();
    this->dismissPadContextPopup();
    this->processorRef.setMidiLearnCallback(nullptr);

    this->crosstalkPopup = std::make_unique<CrosstalkCalibrationPopup>(this->processorRef.getEngine());
    this->crosstalkPopup->onCaptureStateChanged = [this](bool enabled) {
        this->processorRef.setMidiCaptureEnabled(enabled);
    };
    this->crosstalkPopup->onMidiPoll = [this](int& noteNumber, int& velocity) {
        const bool received = this->processorRef.popCapturedMidiNote(noteNumber, velocity);
        if (received)
            this->flashPadForMidiNote(noteNumber, static_cast<float>(velocity));
        return received;
    };
    this->crosstalkPopup->onStatusMessage = [this](const juce::String& message) {
        this->statusBar.setStatusText(message);
    };
    this->crosstalkPopup->onDismiss = [this] {
        juce::MessageManager::callAsync([this] { this->dismissCrosstalkPopup(); });
    };

    const auto availableBounds = this->getLocalBounds().reduced(32, 24);
    this->crosstalkPopup->setBounds(this->crosstalkPopup->getPreferredBounds(availableBounds));

    this->crosstalkPopupOverlay = std::make_unique<DismissOverlay>();
    this->crosstalkPopupOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissCrosstalkPopup(); });
    };
    this->crosstalkPopupOverlay->setBounds(this->getLocalBounds());
    this->addAndMakeVisible(*this->crosstalkPopupOverlay);
    this->crosstalkPopupOverlay->toFront(false);

    this->addAndMakeVisible(*this->crosstalkPopup);
    this->crosstalkPopup->toFront(true);
}

void PluginEditor::dismissCrosstalkPopup() {
    if (this->crosstalkPopup) {
        this->crosstalkPopup->cancelAndRestore();
        this->processorRef.setMidiCaptureEnabled(false);
    }
    if (this->crosstalkPopupOverlay) {
        this->removeChildComponent(this->crosstalkPopupOverlay.get());
        this->crosstalkPopupOverlay.reset();
    }
    if (this->crosstalkPopup) {
        this->removeChildComponent(this->crosstalkPopup.get());
        this->crosstalkPopup.reset();
    }
}

void PluginEditor::showComponentsForMode(ViewMode mode) {
    this->kitHomePanel->setVisible(mode == ViewMode::Kit);

    // Drum pad grid - visible for Zen and Zone
    bool showPads = (mode == ViewMode::Zen || mode == ViewMode::Zone);
    this->drumPadGrid.setVisible(showPads);

    // Raw mode components
    bool showRaw = (mode == ViewMode::Raw);
    this->instrumentMapEditor.setVisible(showRaw);
    this->instrumentMapLabel.setVisible(showRaw);
    this->applyButton.setVisible(showRaw);

    // Map mode
    this->midiMapTable.setVisible(mode == ViewMode::Map);

    // Instrument config mode
    this->instrumentConfigPanel->setVisible(mode == ViewMode::Instrument);

    // Settings
    this->settingsPanel->setVisible(mode == ViewMode::Settings);

    // Fix
    this->troubleshootingPanel->setVisible(mode == ViewMode::Fix);
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

    switch (this->activeMode) {
        case ViewMode::Kit:
            this->kitHomePanel->setBounds(mainArea);
            break;

        case ViewMode::Zen:
        case ViewMode::Zone:
            this->drumPadGrid.setBounds(mainArea);
            break;

        case ViewMode::Raw: {
            auto rawArea = mainArea.reduced(UITheme::Spacing::md);
            const int gap = UITheme::Spacing::md;
            const int labelHeight = UITheme::Spacing::rowCompact;
            const int buttonHeight = 32;
            auto headerArea = rawArea.removeFromTop(buttonHeight);
            this->applyButton.setBounds(headerArea.removeFromRight(180));
            headerArea.removeFromRight(gap);
            this->instrumentMapLabel.setBounds(headerArea.removeFromTop(labelHeight));
            this->instrumentMapEditor.setBounds(rawArea);
            break;
        }

        case ViewMode::Settings:
            this->settingsPanel->setBounds(mainArea);
            break;

        case ViewMode::Fix:
            this->troubleshootingPanel->setBounds(mainArea);
            break;

        case ViewMode::Instrument:
            this->instrumentConfigPanel->setBounds(mainArea);
            break;

        case ViewMode::Map:
            this->midiMapTable.setBounds(mainArea);
            break;
    }

    if (this->crosstalkPopupOverlay)
        this->crosstalkPopupOverlay->setBounds(this->getLocalBounds());
    if (this->crosstalkPopup) {
        const auto availableBounds = this->getLocalBounds().reduced(32, 24);
        this->crosstalkPopup->setBounds(this->crosstalkPopup->getPreferredBounds(availableBounds));
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

void PluginEditor::showPadContextPopup(const KitLayout::InstrumentSlot* slot, int artIdx, juce::Point<int> /*screenPos*/) {
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
        this->markInstrumentConfigDirty();
    };
    this->padContextPopup->onMidiMappingChanged = [this] {
        auto* layout = this->processorRef.getEngine().getKitLayout();
        if (layout) {
            layout->rebuildIndices();
            this->processorRef.getEngine().rebuildMidiNoteRoutingFromLayout();
        }
        this->statusBar.setStatusText("MIDI mapping updated");
        this->markMidiMapTableDirty();
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

    const auto availablePopupBounds = getLocalBounds().reduced(24, 18);
    this->padContextPopup->setBounds(this->padContextPopup->getPreferredBounds(availablePopupBounds));

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
            if (vectorContains(articulation.notes, static_cast<uint8_t>(midiNote))) {
                this->flashPadForSlot(slot.id, velocity);
                return;
            }
        }
    }
}
