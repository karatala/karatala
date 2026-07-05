#include "KitHomePanel.h"

#include "KitHomePanelHelpers.h"
#include "UIHelpers.h"
#include "UITheme.h"
#include <algorithm>
#include <unordered_set>

KitHomePanel::KitHomePanel(DrumKitEngine& engine)
    : engine(engine) {
    this->titleLabel.setText("Kit", juce::dontSendNotification);
    this->titleLabel.setFont(UITheme::Fonts::sectionTitle().withHeight(18.0f));
    this->titleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->contentComponent.addAndMakeVisible(this->titleLabel);

    this->introLabel.setText("Choose a MIDI layout and build the instrument map for this plugin instance.", juce::dontSendNotification);
    this->introLabel.setFont(UITheme::Fonts::labelBoldMedium());
    this->introLabel.setColour(juce::Label::textColourId, UITheme::Colours::textInfo);
    this->contentComponent.addAndMakeVisible(this->introLabel);

    auto setupCaption = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        UIHelpers::configureCaptionLabel(label, juce::Justification::centredLeft);
        this->contentComponent.addAndMakeVisible(label);
    };
    setupCaption(this->layoutLabel, "Kit layout");
    setupCaption(this->kitLabel, "Start from library kit");
    setupCaption(this->presetLabel, "Instrument-map preset");
    setupCaption(this->assignmentsLabel, "Slot assignments");
    setupCaption(this->headerSlotLabel, "Slot");
    setupCaption(this->headerTypeLabel, "Type");
    setupCaption(this->headerBalanceLabel, "Balance");
    setupCaption(this->headerDirectLabel, "Direct");
    setupCaption(this->headerSourceLabel, "Source");
    setupCaption(this->micLabel, "Advanced: output / mic layout");

    this->assignmentsLabel.setFont(UITheme::Fonts::sectionTitle());
    this->contentComponent.addAndMakeVisible(this->layoutCombo);
    this->contentComponent.addAndMakeVisible(this->globalLayoutToggle);
    this->contentComponent.addAndMakeVisible(this->kitCombo);
    this->contentComponent.addAndMakeVisible(this->presetCombo);
    this->contentComponent.addAndMakeVisible(this->presetLoadButton);
    this->contentComponent.addAndMakeVisible(this->presetSaveButton);
    this->contentComponent.addAndMakeVisible(this->presetDuplicateButton);
    this->contentComponent.addAndMakeVisible(this->presetDeleteButton);
    this->contentComponent.addAndMakeVisible(this->micCombo);
    this->contentComponent.addAndMakeVisible(this->micWarningLabel);
    this->contentComponent.addAndMakeVisible(this->loadButton);

    UIHelpers::configureComboBox(this->layoutCombo, UIHelpers::ComboBoxStyle::LayoutSelector);
    UIHelpers::configureComboBox(this->kitCombo);
    UIHelpers::configureComboBox(this->presetCombo);
    UIHelpers::configureComboBox(this->micCombo, UIHelpers::ComboBoxStyle::LayoutSelector);
    for (auto* button : { &this->presetLoadButton, &this->presetSaveButton, &this->presetDuplicateButton, &this->presetDeleteButton, &this->loadButton })
        UIHelpers::configureControlButton(*button);

    this->micWarningLabel.setText(
        "Changes the machine-wide output bus layout. It cannot affect this running instance and is used only by plugin instances created afterward.",
        juce::dontSendNotification);
    this->micWarningLabel.setFont(UITheme::Fonts::small());
    this->micWarningLabel.setColour(juce::Label::textColourId, UITheme::Colours::textAccent);
    this->micWarningLabel.setJustificationType(juce::Justification::topLeft);

    this->layoutCombo.onChange = [this] { this->rebuildSlotRows(); };
    this->kitCombo.onChange = [this] { this->generateDraftFromSelectedKit(); };
    this->presetCombo.onChange = [this] { this->updatePresetButtons(); };
    this->presetLoadButton.onClick = [this] { this->loadSelectedPreset(); };
    this->presetSaveButton.onClick = [this] { this->savePreset(false); };
    this->presetDuplicateButton.onClick = [this] { this->savePreset(true); };
    this->presetDeleteButton.onClick = [this] { this->deleteSelectedPreset(); };
    this->loadButton.onClick = [this] {
        const auto* selectedLayout = this->getSelectedLayout();
        if (selectedLayout == nullptr || this->draftMapText.trim().isEmpty()) {
            this->reportStatus("Choose at least one instrument before loading");
            return;
        }

        auto parsed = DrumKitEngine::parseInstrumentMapText(this->draftMapText);
        if (!parsed.ok()) {
            this->reportStatus(parsed.error());
            return;
        }
        std::unordered_set<std::string> validSlots;
        for (const auto& slot : selectedLayout->layout.instrumentSlots)
            validSlots.insert(slot.id.toStdString());
        parsed.value.entries.erase(std::remove_if(parsed.value.entries.begin(), parsed.value.entries.end(), [&validSlots](const auto& entry) {
            return entry.slotId != "*" && validSlots.find(entry.slotId.toStdString()) == validSlots.end();
        }), parsed.value.entries.end());
        const auto compatibleMapText = DrumKitEngine::serializeInstrumentMap(parsed.value);
        if (compatibleMapText.trim().isEmpty()) {
            this->reportStatus("The current map has no assignments compatible with this kit layout");
            return;
        }

        Commit commit;
        commit.layout = selectedLayout->layout;
        commit.instrumentMapText = compatibleMapText;
        commit.setGlobalKitLayout = this->globalLayoutToggle.getToggleState();
        const int micIdx = this->micCombo.getSelectedId() - 1;
        if (micIdx >= 0 && micIdx < static_cast<int>(this->micLayouts.size()))
            commit.globalMicLayoutId = this->micLayouts[static_cast<size_t>(micIdx)].id;
        if (this->onLoadRequested)
            this->onLoadRequested(std::move(commit));
    };
}

void KitHomePanel::refresh(const juce::String& currentMapText) {
    this->draftMapText = currentMapText;
    this->rebuildLayoutEntries();
    this->rebuildKitEntries();
    this->rebuildMicEntries();
    this->rebuildPresetEntries();
    this->rebuildSlotRows();
}

void KitHomePanel::rebuildLayoutEntries() {
    this->layouts.clear();
    this->layoutCombo.clear(juce::dontSendNotification);

    LayoutEntry defaultEntry;
    defaultEntry.layout = this->engine.getKitLayoutManager()->getDefault();
    this->layouts.push_back(std::move(defaultEntry));

    for (const auto& layout : this->engine.getKitLayoutManager()->getLayouts())
        this->layouts.push_back({ layout, false });

    const auto userDir = this->engine.getUserPresetsDir();
    if (userDir.isDirectory()) {
        for (const auto& file : userDir.findChildFiles(juce::File::findFiles, false, "*.yml;*.yaml")) {
            auto parsed = LayoutTraits<KitLayout>::parseYaml(file.loadFileAsString());
            if (!parsed.ok())
                continue;
            parsed.value.id = file.getFileNameWithoutExtension();
            parsed.value.rebuildIndices();
            this->layouts.push_back({ parsed.value, true });
        }
    }

    juce::String currentId;
    if (const auto* current = this->engine.getKitLayout())
        currentId = current->id;
    int selectedId = 1;
    for (size_t idx = 0; idx < this->layouts.size(); ++idx) {
        const auto& entry = this->layouts[idx];
        const auto label = (entry.isUser ? "User: " : "") + (entry.layout.name.isNotEmpty() ? entry.layout.name : entry.layout.id);
        this->layoutCombo.addItem(label, static_cast<int>(idx + 1));
        if (entry.layout.id == currentId)
            selectedId = static_cast<int>(idx + 1);
    }
    this->layoutCombo.setSelectedId(selectedId, juce::dontSendNotification);
}

void KitHomePanel::rebuildKitEntries() {
    this->kits.clear();
    this->kitCombo.clear(juce::dontSendNotification);
    this->kitCombo.addItem("(keep current assignments)", 1);
    if (const auto* library = this->engine.getInstrumentManager()) {
        for (const auto& kit : library->getKits()) {
            this->kits.push_back(kit.get());
            this->kitCombo.addItem(kit->meta.name.isNotEmpty() ? kit->meta.name : kit->meta.codeRaw,
                                   static_cast<int>(this->kits.size() + 1));
        }
    }
    this->kitCombo.setSelectedId(1, juce::dontSendNotification);
}

void KitHomePanel::rebuildMicEntries() {
    this->micLayouts.clear();
    this->micCombo.clear(juce::dontSendNotification);
    this->micLayouts.push_back(this->engine.getMicLayoutManager()->getDefault());
    for (const auto& layout : this->engine.getMicLayoutManager()->getLayouts())
        this->micLayouts.push_back(layout);

    int selectedId = 1;
    const auto currentId = this->engine.getGlobalMicLayoutId();
    for (size_t idx = 0; idx < this->micLayouts.size(); ++idx) {
        const auto& layout = this->micLayouts[idx];
        this->micCombo.addItem(layout.name.isNotEmpty() ? layout.name : layout.id, static_cast<int>(idx + 1));
        if (layout.id == currentId)
            selectedId = static_cast<int>(idx + 1);
    }
    this->micCombo.setSelectedId(selectedId, juce::dontSendNotification);
}

juce::File KitHomePanel::getUserPresetDirectory() const {
    return this->engine.getUserPresetsDir().getChildFile("instrument_maps");
}

void KitHomePanel::rebuildPresetEntries() {
    this->presets.clear();
    this->presetCombo.clear(juce::dontSendNotification);

    auto scan = [this](const juce::File& directory, bool isUser) {
        if (!directory.isDirectory())
            return;
        for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.map;*.txt")) {
            this->presets.push_back({ file, file.getFileNameWithoutExtension(), isUser });
        }
    };
    scan(this->engine.dataPath.getChildFile("instrument_map_presets"), false);
    scan(this->getUserPresetDirectory(), true);

    for (size_t idx = 0; idx < this->presets.size(); ++idx) {
        const auto& entry = this->presets[idx];
        this->presetCombo.addItem((entry.isUser ? "User: " : "Factory: ") + entry.name, static_cast<int>(idx + 1));
    }
    this->presetCombo.setTextWhenNothingSelected("(no preset selected)");
    this->updatePresetButtons();
}

const KitHomePanel::LayoutEntry* KitHomePanel::getSelectedLayout() const {
    const int idx = this->layoutCombo.getSelectedId() - 1;
    if (idx < 0 || idx >= static_cast<int>(this->layouts.size()))
        return nullptr;
    return &this->layouts[static_cast<size_t>(idx)];
}

void KitHomePanel::rebuildSlotRows() {
    for (auto& row : this->slotRows) {
        this->contentComponent.removeChildComponent(&row->slotLabel);
        this->contentComponent.removeChildComponent(&row->typeCombo);
        this->contentComponent.removeChildComponent(&row->balanceEditor);
        this->contentComponent.removeChildComponent(&row->directMicOutsCombo);
        this->contentComponent.removeChildComponent(&row->sourceCombo);
    }
    this->slotRows.clear();

    const auto* selectedLayout = this->getSelectedLayout();
    if (selectedLayout == nullptr)
        return;

    DrumKitEngine::InstrumentMap existingMap;
    auto parsed = DrumKitEngine::parseInstrumentMapText(this->draftMapText);
    if (parsed.ok())
        existingMap = std::move(parsed.value);

    for (const auto& slot : selectedLayout->layout.instrumentSlots) {
        auto row = std::make_unique<SlotRow>();
        row->slotId = slot.id;
        row->slotLabel.setText(slot.display.isNotEmpty() ? slot.display : slot.id, juce::dontSendNotification);
        UIHelpers::configureTableRowLabel(row->slotLabel);
        this->contentComponent.addAndMakeVisible(row->slotLabel);

        UIHelpers::configureComboBox(row->typeCombo);
        const auto& typeChoices = KitHomePanelHelpers::editableTypeChoices();
        for (size_t typeIdx = 0; typeIdx < typeChoices.size(); ++typeIdx)
            row->typeCombo.addItem(typeChoices[typeIdx].label, static_cast<int>(typeIdx + 1));
        row->typeCombo.setSelectedId(KitHomePanelHelpers::typeComboIdFor(slot.type), juce::dontSendNotification);
        this->contentComponent.addAndMakeVisible(row->typeCombo);

        UIHelpers::configureMonoTextEditor(row->balanceEditor, UITheme::Colours::textPrimary, false);
        row->balanceEditor.setText(slot.balance.has_value() ? balanceValueToString(*slot.balance) : juce::String(), false);
        row->balanceEditor.setTextToShowWhenEmpty("--", UITheme::Colours::textMuted);
        row->balanceEditor.setTooltip("Use C0, L1..L127, or R1..R127. Leave empty for unknown.");
        this->contentComponent.addAndMakeVisible(row->balanceEditor);

        UIHelpers::configureComboBox(row->directMicOutsCombo);
        row->directMicOutsCombo.addItem("catalog", 1);
        row->directMicOutsCombo.addItem("own", 2);
        row->directMicOutsCombo.addItem("auto", 3);
        row->directMicOutsCombo.addItem("shared", 4);
        row->directMicOutsCombo.setSelectedId(KitHomePanelHelpers::comboIdForPolicy(slot.directMicOuts), juce::dontSendNotification);
        row->directMicOutsCombo.setTooltip("Direct mic output override; catalog uses the type default.");
        this->contentComponent.addAndMakeVisible(row->directMicOutsCombo);

        UIHelpers::configureComboBox(row->sourceCombo);
        this->contentComponent.addAndMakeVisible(row->sourceCombo);
        row->sourceCombo.addItem("(empty)", 1);

        if (const auto* library = this->engine.getInstrumentManager()) {
            for (const auto& kit : library->getKits()) {
                std::vector<const KitLib::InstrumentSpec*> instruments;
                for (const auto& [id, instrument] : kit->getInstrumentSpecsMap()) {
                    juce::ignoreUnused(id);
                    if (slot.type == KitLib::InstrumentSpec::Type::Unknown || instrument->meta.type == slot.type)
                        instruments.push_back(instrument.get());
                }
                std::sort(instruments.begin(), instruments.end(), [](const auto* left, const auto* right) {
                    return left->meta.codeRaw < right->meta.codeRaw;
                });
                for (const auto* instrument : instruments) {
                    SourceChoice choice;
                    choice.kitCode = kit->meta.codeRaw;
                    choice.instrumentCode = instrument->meta.codeRaw;
                    choice.display = (kit->meta.name.isNotEmpty() ? kit->meta.name : kit->meta.codeRaw) + "/" + instrument->meta.codeRaw;
                    choice.type = instrument->meta.type;
                    row->choices.push_back(std::move(choice));
                    row->sourceCombo.addItem(row->choices.back().display, static_cast<int>(row->choices.size() + 1));
                }
            }
        }

        int selectedId = 1;
        for (const auto& entry : existingMap.entries) {
            if (entry.slotId != slot.id)
                continue;
            const auto wanted = entry.kitRef + "/" + entry.instCode;
            for (size_t idx = 0; idx < row->choices.size(); ++idx) {
                if (KitHomePanelHelpers::sourcePath(row->choices[idx]).compareIgnoreCase(wanted) == 0) {
                    selectedId = static_cast<int>(idx + 2);
                    break;
                }
            }
            break;
        }
        row->sourceCombo.setSelectedId(selectedId, juce::dontSendNotification);
        auto* rowPtr = row.get();
        row->typeCombo.onChange = [this, rowPtr] { this->applySlotMetadata(*rowPtr); };
        row->balanceEditor.onReturnKey = [this, rowPtr] { this->applySlotMetadata(*rowPtr); };
        row->balanceEditor.onFocusLost = [this, rowPtr] { this->applySlotMetadata(*rowPtr); };
        row->directMicOutsCombo.onChange = [this, rowPtr] { this->applySlotMetadata(*rowPtr); };
        row->sourceCombo.onChange = [this, rowPtr] { this->replaceDraftSlot(*rowPtr); };
        this->slotRows.push_back(std::move(row));
    }
    this->resized();
}

void KitHomePanel::generateDraftFromSelectedKit() {
    const int kitIdx = this->kitCombo.getSelectedId() - 2;
    const auto* selectedLayout = this->getSelectedLayout();
    if (kitIdx < 0 || kitIdx >= static_cast<int>(this->kits.size()) || selectedLayout == nullptr)
        return;
    this->draftMapText = this->engine.buildInstrumentMapForKit(selectedLayout->layout, *this->kits[static_cast<size_t>(kitIdx)]);
    this->rebuildSlotRows();
    this->reportStatus("Kit assignments prepared. Press Load Kit to apply.");
}

void KitHomePanel::replaceDraftSlot(const SlotRow& row) {
    DrumKitEngine::InstrumentMap map;
    auto parsed = DrumKitEngine::parseInstrumentMapText(this->draftMapText);
    if (parsed.ok())
        map = std::move(parsed.value);

    map.entries.erase(std::remove_if(map.entries.begin(), map.entries.end(), [&row](const auto& entry) {
        return entry.slotId == row.slotId;
    }), map.entries.end());

    const int choiceIdx = row.sourceCombo.getSelectedId() - 2;
    if (choiceIdx >= 0 && choiceIdx < static_cast<int>(row.choices.size())) {
        const auto& choice = row.choices[static_cast<size_t>(choiceIdx)];
        DrumKitEngine::InstrumentMapEntry entry;
        entry.slotId = row.slotId;
        entry.kitRef = choice.kitCode;
        entry.instCode = choice.instrumentCode;
        map.entries.push_back(std::move(entry));
    }
    this->draftMapText = DrumKitEngine::serializeInstrumentMap(map);
}


void KitHomePanel::applySlotMetadata(SlotRow& row) {
    const int layoutIdx = this->layoutCombo.getSelectedId() - 1;
    if (layoutIdx < 0 || layoutIdx >= static_cast<int>(this->layouts.size()))
        return;

    auto& layout = this->layouts[static_cast<size_t>(layoutIdx)].layout;
    auto slotIt = std::find_if(layout.instrumentSlots.begin(), layout.instrumentSlots.end(), [&row](const auto& slot) {
        return slot.id == row.slotId;
    });
    if (slotIt == layout.instrumentSlots.end())
        return;

    const auto previousType = slotIt->type;
    const auto newType = KitHomePanelHelpers::typeFromComboId(row.typeCombo.getSelectedId());
    slotIt->type = newType;

    const auto balanceText = row.balanceEditor.getText().trim();
    if (balanceText.isEmpty()) {
        slotIt->balance.reset();
    } else if (auto parsedBalance = parseBalanceValue(balanceText)) {
        slotIt->balance = *parsedBalance;
        row.balanceEditor.setText(balanceValueToString(*parsedBalance), false);
    } else {
        row.balanceEditor.setText(slotIt->balance.has_value() ? balanceValueToString(*slotIt->balance) : juce::String(), false);
        this->reportStatus("Invalid balance for " + row.slotId + ": use C0, L1..L127, or R1..R127");
    }

    slotIt->directMicOuts = KitHomePanelHelpers::policyFromComboId(row.directMicOutsCombo.getSelectedId());
    layout.rebuildIndices();

    if (newType != previousType)
        this->rebuildSlotRows();
}
void KitHomePanel::loadSelectedPreset() {
    const int presetIdx = this->presetCombo.getSelectedId() - 1;
    const auto* selectedLayout = this->getSelectedLayout();
    if (presetIdx < 0 || presetIdx >= static_cast<int>(this->presets.size()) || selectedLayout == nullptr)
        return;

    auto parsedPreset = DrumKitEngine::parseInstrumentMapText(this->presets[static_cast<size_t>(presetIdx)].file.loadFileAsString());
    if (!parsedPreset.ok()) {
        this->reportStatus("Preset is invalid: " + parsedPreset.error());
        return;
    }

    DrumKitEngine::InstrumentMap merged;
    auto parsedCurrent = DrumKitEngine::parseInstrumentMapText(this->draftMapText);
    if (parsedCurrent.ok())
        merged = std::move(parsedCurrent.value);

    std::unordered_set<std::string> validSlots;
    for (const auto& slot : selectedLayout->layout.instrumentSlots)
        validSlots.insert(slot.id.toStdString());

    std::unordered_set<std::string> replacedSlots;
    juce::StringArray skippedSlots;
    for (const auto& entry : parsedPreset.value.entries) {
        if (validSlots.find(entry.slotId.toStdString()) == validSlots.end()) {
            if (!skippedSlots.contains(entry.slotId))
                skippedSlots.add(entry.slotId);
            continue;
        }
        replacedSlots.insert(entry.slotId.toStdString());
    }

    merged.entries.erase(std::remove_if(merged.entries.begin(), merged.entries.end(), [&replacedSlots](const auto& entry) {
        return replacedSlots.find(entry.slotId.toStdString()) != replacedSlots.end();
    }), merged.entries.end());
    for (const auto& entry : parsedPreset.value.entries)
        if (validSlots.find(entry.slotId.toStdString()) != validSlots.end())
            merged.entries.push_back(entry);

    this->draftMapText = DrumKitEngine::serializeInstrumentMap(merged);
    this->rebuildSlotRows();
    juce::String status = "Preset loaded into draft";
    if (!skippedSlots.isEmpty())
        status += "; skipped missing slots: " + skippedSlots.joinIntoString(", ");
    this->reportStatus(status);
}

void KitHomePanel::savePreset(bool forceNewName) {
    if (this->draftMapText.trim().isEmpty()) {
        this->reportStatus("There is no instrument map to save");
        return;
    }

    const int presetIdx = this->presetCombo.getSelectedId() - 1;
    const bool canOverwrite = !forceNewName && presetIdx >= 0 && presetIdx < static_cast<int>(this->presets.size())
                           && this->presets[static_cast<size_t>(presetIdx)].isUser;
    if (canOverwrite) {
        if (this->presets[static_cast<size_t>(presetIdx)].file.replaceWithText(this->draftMapText))
            this->reportStatus("Instrument-map preset saved");
        else
            this->reportStatus("Could not save instrument-map preset");
        return;
    }

    auto* dialog = new juce::AlertWindow("Save Instrument-Map Preset", "Enter a preset name:", juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor("name", "My Kit", "Name:");
    dialog->addButton("Save", 1);
    dialog->addButton("Cancel", 0);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, dialog](int result) {
        if (result != 1)
            return;
        const auto name = KitHomePanelHelpers::safePresetFileName(dialog->getTextEditorContents("name"));
        if (name.isEmpty())
            return;
        auto directory = this->getUserPresetDirectory();
        directory.createDirectory();
        if (directory.getChildFile(name + ".map").replaceWithText(this->draftMapText)) {
            this->rebuildPresetEntries();
            this->reportStatus("Saved preset: " + name);
        } else {
            this->reportStatus("Could not save preset: " + name);
        }
    }), true);
}

void KitHomePanel::deleteSelectedPreset() {
    const int presetIdx = this->presetCombo.getSelectedId() - 1;
    if (presetIdx < 0 || presetIdx >= static_cast<int>(this->presets.size()))
        return;
    const auto entry = this->presets[static_cast<size_t>(presetIdx)];
    if (!entry.isUser)
        return;

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Delete Instrument-Map Preset")
            .withMessage("Delete \"" + entry.name + "\"?")
            .withButton("Delete")
            .withButton("Cancel"),
        [this, entry](int result) {
            if (result == 1 && entry.file.deleteFile()) {
                this->rebuildPresetEntries();
                this->reportStatus("Preset deleted");
            }
        });
}

void KitHomePanel::updatePresetButtons() {
    const int idx = this->presetCombo.getSelectedId() - 1;
    const bool selected = idx >= 0 && idx < static_cast<int>(this->presets.size());
    const bool userSelected = selected && this->presets[static_cast<size_t>(idx)].isUser;
    this->presetLoadButton.setEnabled(selected);
    this->presetDeleteButton.setEnabled(userSelected);
    this->presetSaveButton.setButtonText(userSelected ? "Save" : "Save As");
}

void KitHomePanel::reportStatus(const juce::String& message) {
    if (this->onStatusMessage)
        this->onStatusMessage(message);
}

void KitHomePanel::resized() {
    this->layoutViewportToBounds();
    const int width = juce::jmax(860, this->viewport.getWidth() - this->viewport.getScrollBarThickness());
    const int rowHeight = 26;
    const int leftLabelWidth = 150;
    const int contentHeight = 260 + static_cast<int>(this->slotRows.size()) * rowHeight + 116;
    this->contentComponent.setBounds(0, 0, width, contentHeight);

    auto area = this->contentComponent.getLocalBounds().reduced(16, 10);
    this->titleLabel.setBounds(area.removeFromTop(24));
    this->introLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);

    auto placeComboRow = [&area, leftLabelWidth](juce::Label& label, juce::Component& combo) {
        auto row = area.removeFromTop(26);
        label.setBounds(row.removeFromLeft(leftLabelWidth));
        combo.setBounds(row.removeFromLeft(340).reduced(0, 2));
        area.removeFromTop(4);
    };
    placeComboRow(this->layoutLabel, this->layoutCombo);
    auto globalRow = area.removeFromTop(22);
    globalRow.removeFromLeft(leftLabelWidth);
    this->globalLayoutToggle.setBounds(globalRow.removeFromLeft(300));
    area.removeFromTop(4);
    placeComboRow(this->kitLabel, this->kitCombo);

    auto presetRow = area.removeFromTop(28);
    this->presetLabel.setBounds(presetRow.removeFromLeft(leftLabelWidth));
    this->presetCombo.setBounds(presetRow.removeFromLeft(260).reduced(0, 2));
    presetRow.removeFromLeft(4);
    for (auto* button : { &this->presetLoadButton, &this->presetSaveButton, &this->presetDuplicateButton, &this->presetDeleteButton }) {
        const int buttonWidth = button == &this->presetDuplicateButton ? 74 : 62;
        button->setBounds(presetRow.removeFromLeft(buttonWidth).reduced(1, 2));
    }
    area.removeFromTop(8);

    this->assignmentsLabel.setBounds(area.removeFromTop(24));
    auto headerArea = area.removeFromTop(18);
    this->headerSlotLabel.setBounds(headerArea.removeFromLeft(leftLabelWidth));
    this->headerTypeLabel.setBounds(headerArea.removeFromLeft(108));
    this->headerBalanceLabel.setBounds(headerArea.removeFromLeft(72));
    this->headerDirectLabel.setBounds(headerArea.removeFromLeft(90));
    this->headerSourceLabel.setBounds(headerArea.removeFromLeft(420));
    for (auto& row : this->slotRows) {
        auto slotArea = area.removeFromTop(rowHeight);
        row->slotLabel.setBounds(slotArea.removeFromLeft(leftLabelWidth));
        row->typeCombo.setBounds(slotArea.removeFromLeft(104).reduced(0, 2));
        slotArea.removeFromLeft(4);
        row->balanceEditor.setBounds(slotArea.removeFromLeft(64).reduced(0, 3));
        slotArea.removeFromLeft(8);
        row->directMicOutsCombo.setBounds(slotArea.removeFromLeft(82).reduced(0, 2));
        slotArea.removeFromLeft(8);
        row->sourceCombo.setBounds(slotArea.removeFromLeft(420).reduced(0, 2));
    }
    area.removeFromTop(10);

    auto micRow = area.removeFromTop(26);
    this->micLabel.setBounds(micRow.removeFromLeft(leftLabelWidth));
    this->micCombo.setBounds(micRow.removeFromLeft(340).reduced(0, 2));
    this->micWarningLabel.setBounds(area.removeFromTop(42).withTrimmedLeft(leftLabelWidth));
    area.removeFromTop(6);
    this->loadButton.setBounds(area.removeFromTop(34).removeFromLeft(180));
}
