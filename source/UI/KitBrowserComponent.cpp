#include "KitBrowserComponent.h"

#include "../Engine/ArticulationNameHelpers.h"
#include "CompactModalDialog.h"
#include "KitHomePanelHelpers.h"
#include "UIHelpers.h"
#include "UITheme.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

KitBrowserComponent::KitBrowserComponent(DrumKitEngine& drumKitEngine)
    : engine(drumKitEngine) {
    this->titleLabel.setText("Kit and Preset Browser", juce::dontSendNotification);
    UIHelpers::configureSectionLabel(this->titleLabel);
    this->addAndMakeVisible(this->titleLabel);

    for (auto* button : { &this->kitsModeButton, &this->presetsModeButton }) {
        UIHelpers::configureCategoryButton(*button);
        this->addAndMakeVisible(*button);
    }
    this->kitsModeButton.onClick = [this] { this->setSourceMode(SourceMode::LibraryKits); };
    this->presetsModeButton.onClick = [this] { this->setSourceMode(SourceMode::Presets); };

    UIHelpers::configureBorderlessTextEditor(this->filterField);
    this->filterField.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    this->filterField.setTextToShowWhenEmpty("filter...", UITheme::Colours::textMuted);
    this->filterField.onTextChange = [this] { this->startTimer(this->filterDebounceMs); };
    this->addAndMakeVisible(this->filterField);

    this->sourceList.setModel(&this->sourceListModel);
    this->sourceList.setRowHeight(26);
    this->sourceList.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    this->addAndMakeVisible(this->sourceList);

    this->infoTitleLabel.setFont(UITheme::Fonts::sectionTitle());
    this->infoTitleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->infoTitleLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->infoTitleLabel);
    for (auto* label : { &this->infoAuthorLabel, &this->infoLicenseLabel }) {
        UIHelpers::configureCaptionLabel(*label, juce::Justification::centredLeft);
        this->addAndMakeVisible(*label);
    }

    this->detailsEditor.setMultiLine(true);
    this->detailsEditor.setReadOnly(true);
    this->detailsEditor.setScrollbarsShown(true);
    this->detailsEditor.setFont(UITheme::Fonts::normal());
    this->detailsEditor.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgEditor);
    this->detailsEditor.setColour(juce::TextEditor::textColourId, UITheme::Colours::textSecondary);
    UIHelpers::configureBorderlessTextEditor(this->detailsEditor);
    this->addAndMakeVisible(this->detailsEditor);

    this->previewLabel.setText("Instrument preview", juce::dontSendNotification);
    UIHelpers::configureCaptionLabel(this->previewLabel, juce::Justification::centredLeft);
    this->addAndMakeVisible(this->previewLabel);
    this->previewViewport.setViewedComponent(&this->previewGrid, false);
    this->previewViewport.setScrollBarsShown(true, false);
    this->previewViewport.setColour(juce::ScrollBar::backgroundColourId, UITheme::Colours::bgEditor);
    this->addAndMakeVisible(this->previewViewport);
    this->previewGrid.onPreview = [this](const SourceItem& source, juce::uint8 velocity) {
        this->engine.previewSourceItem(source.kitCodeRaw, source.instCode, source.artCode, velocity);
    };

    for (auto* button : { &this->loadButton, &this->saveButton, &this->saveAsButton, &this->deleteButton }) {
        UIHelpers::configureControlButton(*button);
        this->addAndMakeVisible(*button);
    }
    this->loadButton.onClick = [this] { this->loadSelected(); };
    this->saveButton.onClick = [this] { this->saveSelectedPreset(); };
    this->saveAsButton.onClick = [this] { this->savePresetAs(); };
    this->deleteButton.onClick = [this] { this->deleteSelectedPreset(); };

    this->refresh({});
}

KitBrowserComponent::~KitBrowserComponent() {
    this->previewViewport.setViewedComponent(nullptr, false);
}

void KitBrowserComponent::refresh(const juce::String& activeInstrumentMapText) {
    this->currentMapText = activeInstrumentMapText;
    this->sourceItems = UIHelpers::buildSourceItems(this->engine.getInstrumentManager());
    this->rebuildLibraryEntries();
    this->rebuildPresetEntries(this->selectedPresetPath);
    this->rebuildFilteredEntries();
}

void KitBrowserComponent::rebuildLibraryEntries() {
    const auto previousSelection = this->selectedKitId;
    this->kits.clear();
    const auto* library = this->engine.getInstrumentManager();
    if (library != nullptr) {
        for (const auto& kit : library->getKits()) {
            if (kit == nullptr) continue;
            KitEntry entry;
            entry.id = library->getPreferredKitId(*kit);
            entry.rawId = kit->meta.codeRaw;
            entry.name = kit->meta.name.isNotEmpty() ? kit->meta.name : kit->meta.codeRaw;
            entry.author = kit->meta.author;
            entry.license = kit->meta.license;
            entry.readmeMarkdown = kit->readmeMarkdown;
            entry.searchText = (entry.name + "\n" + entry.author + "\n" + entry.license + "\n" + entry.rawId).toLowerCase();
            this->kits.push_back(std::move(entry));
        }
    }
    std::sort(this->kits.begin(), this->kits.end(), [](const auto& leftEntry, const auto& rightEntry) {
        return leftEntry.name.compareIgnoreCase(rightEntry.name) < 0;
    });
    this->selectedKitId = previousSelection;
    const bool selectionExists = std::any_of(this->kits.begin(), this->kits.end(), [this](const auto& entry) {
        return entry.id == this->selectedKitId;
    });
    if (!selectionExists) this->selectedKitId = this->kits.empty() ? juce::String() : this->kits.front().id;
}

juce::File KitBrowserComponent::getUserPresetDirectory() const {
    return this->engine.getUserPresetsDir().getChildFile("instrument_maps");
}

void KitBrowserComponent::rebuildPresetEntries(const juce::String& preferredSelectionPath) {
    const auto wantedPath = preferredSelectionPath.isNotEmpty() ? preferredSelectionPath : this->selectedPresetPath;
    this->presets.clear();
    auto scanDirectory = [this](const juce::File& directory, bool isUser) {
        if (!directory.isDirectory()) return;
        for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.map;*.txt")) {
            PresetEntry entry;
            entry.file = file;
            entry.name = file.getFileNameWithoutExtension();
            entry.isUser = isUser;
            entry.searchText = ((isUser ? "user\n" : "factory\n") + entry.name).toLowerCase();
            this->presets.push_back(std::move(entry));
        }
    };
    scanDirectory(this->engine.dataPath.getChildFile("instrument_map_presets"), false);
    scanDirectory(this->getUserPresetDirectory(), true);
    std::sort(this->presets.begin(), this->presets.end(), [](const auto& leftEntry, const auto& rightEntry) {
        if (leftEntry.isUser != rightEntry.isUser) return !leftEntry.isUser;
        return leftEntry.name.compareIgnoreCase(rightEntry.name) < 0;
    });

    this->selectedPresetPath = wantedPath;
    const bool selectionExists = std::any_of(this->presets.begin(), this->presets.end(), [this](const auto& entry) {
        return entry.file.getFullPathName() == this->selectedPresetPath;
    });
    if (!selectionExists) this->selectedPresetPath = this->presets.empty() ? juce::String() : this->presets.front().file.getFullPathName();
}

bool KitBrowserComponent::matchesFilter(const juce::String& searchText, const juce::String& filterText) {
    juce::StringArray terms;
    terms.addTokens(filterText.toLowerCase(), " \t\r\n", "");
    terms.removeEmptyStrings();
    for (const auto& term : terms) {
        if (term.startsWithChar('-') && term.length() > 1) {
            if (searchText.contains(term.substring(1))) return false;
        } else if (term != "-" && !searchText.contains(term)) {
            return false;
        }
    }
    return true;
}

void KitBrowserComponent::rebuildFilteredEntries() {
    this->stopTimer();
    this->filteredIndexes.clear();
    const auto filterText = this->filterField.getText();
    if (this->sourceMode == SourceMode::LibraryKits) {
        for (int idx = 0; idx < static_cast<int>(this->kits.size()); ++idx)
            if (KitBrowserComponent::matchesFilter(this->kits[static_cast<size_t>(idx)].searchText, filterText)) this->filteredIndexes.push_back(idx);
    } else {
        for (int idx = 0; idx < static_cast<int>(this->presets.size()); ++idx)
            if (KitBrowserComponent::matchesFilter(this->presets[static_cast<size_t>(idx)].searchText, filterText)) this->filteredIndexes.push_back(idx);
    }

    this->sourceList.updateContent();
    int selectedRow = -1;
    for (int rowIndex = 0; rowIndex < static_cast<int>(this->filteredIndexes.size()); ++rowIndex) {
        const int itemIndex = this->filteredIndexes[static_cast<size_t>(rowIndex)];
        const bool selected = this->sourceMode == SourceMode::LibraryKits
            ? this->kits[static_cast<size_t>(itemIndex)].id == this->selectedKitId
            : this->presets[static_cast<size_t>(itemIndex)].file.getFullPathName() == this->selectedPresetPath;
        if (selected) {
            selectedRow = rowIndex;
            break;
        }
    }
    if (selectedRow < 0 && !this->filteredIndexes.empty()) {
        selectedRow = 0;
        const int itemIndex = this->filteredIndexes.front();
        if (this->sourceMode == SourceMode::LibraryKits)
            this->selectedKitId = this->kits[static_cast<size_t>(itemIndex)].id;
        else
            this->selectedPresetPath = this->presets[static_cast<size_t>(itemIndex)].file.getFullPathName();
    }
    this->sourceList.selectRow(selectedRow, false, false);
    this->updateSelectedContent();
}

void KitBrowserComponent::setSourceMode(SourceMode mode) {
    if (this->sourceMode == mode) return;
    this->sourceMode = mode;
    this->filterField.clear();
    this->rebuildFilteredEntries();
    this->resized();
}

const KitBrowserComponent::KitEntry* KitBrowserComponent::getSelectedKit() const {
    for (const auto& kit : this->kits)
        if (kit.id == this->selectedKitId) return &kit;
    return nullptr;
}

const KitBrowserComponent::PresetEntry* KitBrowserComponent::getSelectedPreset() const {
    for (const auto& preset : this->presets)
        if (preset.file.getFullPathName() == this->selectedPresetPath) return &preset;
    return nullptr;
}

std::vector<KitPreviewGrid::Item> KitBrowserComponent::buildKitPreviewItems(const KitEntry& kit) const {
    std::vector<KitPreviewGrid::Item> result;
    for (const auto& source : this->sourceItems) {
        if (!source.rawKitCode.equalsIgnoreCase(kit.rawId) && !source.kitCodeRaw.equalsIgnoreCase(kit.id)) continue;
        KitPreviewGrid::Item item;
        item.groupId = source.rawKitCode + "\n" + source.rawInstCode;
        item.groupTitle = source.rawInstCode.isNotEmpty() ? source.rawInstCode : source.instCode;
        item.source = source;
        result.push_back(std::move(item));
    }
    return result;
}

std::vector<KitPreviewGrid::Item> KitBrowserComponent::buildPresetPreviewItems(const PresetEntry& preset) const {
    std::vector<KitPreviewGrid::Item> result;
    auto parsed = DrumKitEngine::parseInstrumentMapText(preset.file.loadFileAsString());
    const auto* library = this->engine.getInstrumentManager();
    if (!parsed.ok() || library == nullptr) return result;

    std::unordered_map<std::string, int> layerCounts;
    for (size_t entryIndex = 0; entryIndex < parsed.value.entries.size(); ++entryIndex) {
        const auto& mapEntry = parsed.value.entries[entryIndex];
        const auto* kit = library->findKitById(mapEntry.kitRef);
        const auto* instrument = kit != nullptr ? kit->findInstrumentById(mapEntry.instCode) : nullptr;
        if (kit == nullptr || instrument == nullptr) continue;

        juce::StringArray destinationFilters;
        for (const auto& destinationFilter : mapEntry.slotArticulationFilter) destinationFilters.add(destinationFilter);
        const auto destinationText = destinationFilters.joinIntoString(",");
        const auto layerKey = (mapEntry.slotId + "\n" + destinationText).toLowerCase().toStdString();
        const int layerNumber = ++layerCounts[layerKey];
        juce::String groupTitle = mapEntry.slotId;
        if (destinationText.isNotEmpty()) groupTitle += " / " + destinationText;
        if (layerNumber > 1) groupTitle += " (layer " + juce::String(layerNumber) + ")";
        groupTitle += " - " + (kit->meta.name.isNotEmpty() ? kit->meta.name : kit->meta.codeRaw) + "/" + instrument->meta.codeRaw;
        const auto groupId = juce::String(static_cast<juce::int64>(entryIndex));

        std::unordered_set<std::string> addedArticulations;
        const auto& sourceFilters = !mapEntry.articulationFilter.empty() ? mapEntry.articulationFilter : mapEntry.slotArticulationFilter;
        for (const auto& source : this->sourceItems) {
            if (!source.rawKitCode.equalsIgnoreCase(kit->meta.codeRaw) || !source.rawInstCode.equalsIgnoreCase(instrument->meta.codeRaw)) continue;
            if (!sourceFilters.empty()) {
                const bool matches = std::any_of(sourceFilters.begin(), sourceFilters.end(), [&source](const auto& filter) {
                    return ArticulationNameHelpers::mapFilterMatchesArticulation(filter, source.rawArtCode);
                });
                if (!matches) continue;
            }
            const auto articulationKey = source.rawArtCode.toLowerCase().toStdString();
            if (!addedArticulations.insert(articulationKey).second) continue;
            result.push_back({ groupId, groupTitle, source });
        }
    }
    return result;
}

void KitBrowserComponent::updateSelectedContent() {
    std::vector<KitPreviewGrid::Item> previewItems;
    if (this->filteredIndexes.empty()) {
        const bool sourceEmpty = this->sourceMode == SourceMode::LibraryKits ? this->kits.empty() : this->presets.empty();
        this->infoTitleLabel.setText(sourceEmpty ? "No items available" : "No matching items", juce::dontSendNotification);
        this->infoAuthorLabel.setText(sourceEmpty && this->sourceMode == SourceMode::LibraryKits ? "Check the sample library path" : "Try a different filter", juce::dontSendNotification);
        this->infoLicenseLabel.setText(juce::String(), juce::dontSendNotification);
        this->detailsEditor.clear();
        this->previewGrid.setItems({});
        this->updatePreviewGridSize();
        this->updateActionButtons();
        return;
    }
    if (this->sourceMode == SourceMode::LibraryKits) {
        const auto* kit = this->getSelectedKit();
        if (kit != nullptr) {
            this->infoTitleLabel.setText(kit->name, juce::dontSendNotification);
            this->infoAuthorLabel.setText(kit->author.isNotEmpty() ? kit->author : "Unknown author", juce::dontSendNotification);
            this->infoLicenseLabel.setText(kit->license.isNotEmpty() ? "[" + kit->license + "]" : "No license information", juce::dontSendNotification);
            this->detailsEditor.setFont(UITheme::Fonts::normal());
            this->detailsEditor.setText(kit->readmeMarkdown.isNotEmpty() ? kit->readmeMarkdown : "No README.md found", false);
            previewItems = this->buildKitPreviewItems(*kit);
        } else {
            this->infoTitleLabel.setText("No library kits", juce::dontSendNotification);
            this->infoAuthorLabel.setText("Check the sample library path", juce::dontSendNotification);
            this->infoLicenseLabel.setText(juce::String(), juce::dontSendNotification);
            this->detailsEditor.clear();
        }
    } else {
        const auto* preset = this->getSelectedPreset();
        if (preset != nullptr) {
            this->infoTitleLabel.setText(preset->name, juce::dontSendNotification);
            this->infoAuthorLabel.setText(preset->isUser ? "User instrument-map preset" : "Factory instrument-map preset", juce::dontSendNotification);
            this->infoLicenseLabel.setText("Kit layout and MIDI notes are not included", juce::dontSendNotification);
            this->detailsEditor.setFont(UITheme::Fonts::mono());
            this->detailsEditor.setText(preset->file.loadFileAsString(), false);
            previewItems = this->buildPresetPreviewItems(*preset);
        } else {
            this->infoTitleLabel.setText("No presets", juce::dontSendNotification);
            this->infoAuthorLabel.setText("Save the current instrument map to create one", juce::dontSendNotification);
            this->infoLicenseLabel.setText(juce::String(), juce::dontSendNotification);
            this->detailsEditor.clear();
        }
    }
    this->detailsEditor.setCaretPosition(0);
    this->previewGrid.setItems(std::move(previewItems));
    this->updatePreviewGridSize();
    this->updateActionButtons();
}

void KitBrowserComponent::updateActionButtons() {
    const bool presetMode = this->sourceMode == SourceMode::Presets;
    const auto* preset = this->getSelectedPreset();
    const bool hasVisibleSelection = !this->filteredIndexes.empty();
    this->loadButton.setButtonText(presetMode ? "LOAD PRESET" : "LOAD KIT");
    this->loadButton.setEnabled(hasVisibleSelection && (presetMode ? preset != nullptr : this->getSelectedKit() != nullptr));
    this->saveButton.setVisible(presetMode);
    this->saveAsButton.setVisible(presetMode);
    this->deleteButton.setVisible(presetMode);
    this->saveButton.setEnabled(hasVisibleSelection && preset != nullptr && preset->isUser && this->currentMapText.trim().isNotEmpty());
    this->saveAsButton.setEnabled(this->currentMapText.trim().isNotEmpty());
    this->deleteButton.setEnabled(hasVisibleSelection && preset != nullptr && preset->isUser);

    this->kitsModeButton.setColour(juce::TextButton::buttonColourId, !presetMode ? UITheme::Colours::selectionStrong : UITheme::Colours::bgControl);
    this->presetsModeButton.setColour(juce::TextButton::buttonColourId, presetMode ? UITheme::Colours::selectionStrong : UITheme::Colours::bgControl);
}

void KitBrowserComponent::loadSelected() {
    if (this->sourceMode == SourceMode::LibraryKits) this->loadSelectedKit();
    else this->loadSelectedPreset();
}

void KitBrowserComponent::loadSelectedKit() {
    const auto* selectedKit = this->getSelectedKit();
    const auto* layout = this->engine.getKitLayout();
    const auto* library = this->engine.getInstrumentManager();
    if (selectedKit == nullptr || layout == nullptr || library == nullptr) {
        this->reportStatus("A library kit and active kit layout are required");
        return;
    }
    const auto* kit = library->findKitById(selectedKit->id);
    if (kit == nullptr) {
        this->reportStatus("The selected library kit is no longer available");
        return;
    }

    const auto mapText = this->engine.normalizeInstrumentMapText(this->engine.buildInstrumentMapForKit(*layout, *kit));
    auto parsed = DrumKitEngine::parseInstrumentMapText(mapText);
    if (!parsed.ok() || parsed.value.entries.empty()) {
        this->reportStatus("This kit has no instruments compatible with the active kit layout");
        return;
    }

    std::unordered_set<std::string> mappedSlots;
    for (const auto& entry : parsed.value.entries) mappedSlots.insert(entry.slotId.toLowerCase().toStdString());
    juce::StringArray skippedSlots;
    for (const auto& slot : layout->instrumentSlots)
        if (mappedSlots.find(slot.id.toLowerCase().toStdString()) == mappedSlots.end()) skippedSlots.add(slot.display.isNotEmpty() ? slot.display : slot.id);

    juce::String status = "Loaded kit: " + selectedKit->name;
    if (!skippedSlots.isEmpty()) status += " | no compatible instrument for: " + skippedSlots.joinIntoString(", ");
    if (this->onMapLoadRequested && this->onMapLoadRequested(mapText, status)) {
        this->currentMapText = mapText;
        this->updateActionButtons();
    }
}

void KitBrowserComponent::loadSelectedPreset() {
    const auto* preset = this->getSelectedPreset();
    const auto* layout = this->engine.getKitLayout();
    if (preset == nullptr || layout == nullptr) {
        this->reportStatus("A preset and active kit layout are required");
        return;
    }

    auto parsed = DrumKitEngine::parseInstrumentMapText(preset->file.loadFileAsString());
    if (!parsed.ok()) {
        this->reportStatus("Preset is invalid: " + parsed.error());
        return;
    }

    std::unordered_map<std::string, juce::String> validSlots;
    for (const auto& slot : layout->instrumentSlots) validSlots[slot.id.toLowerCase().toStdString()] = slot.id;
    DrumKitEngine::InstrumentMap compatibleMap;
    juce::StringArray skippedSlots;
    for (const auto& entry : parsed.value.entries) {
        const auto slotIterator = validSlots.find(entry.slotId.toLowerCase().toStdString());
        if (slotIterator != validSlots.end()) {
            auto compatibleEntry = entry;
            compatibleEntry.slotId = slotIterator->second;
            compatibleMap.entries.push_back(std::move(compatibleEntry));
        } else if (!skippedSlots.contains(entry.slotId)) {
            skippedSlots.add(entry.slotId);
        }
    }
    if (compatibleMap.entries.empty()) {
        this->reportStatus("The preset has no entries for the active kit layout");
        return;
    }

    const auto mapText = this->engine.normalizeInstrumentMapText(DrumKitEngine::serializeInstrumentMap(compatibleMap));
    juce::String status = "Loaded preset: " + preset->name;
    if (!skippedSlots.isEmpty()) status += " | skipped missing slots: " + skippedSlots.joinIntoString(", ");
    if (this->onMapLoadRequested && this->onMapLoadRequested(mapText, status)) {
        this->currentMapText = mapText;
        this->updateActionButtons();
    }
}

void KitBrowserComponent::saveSelectedPreset() {
    const auto* preset = this->getSelectedPreset();
    if (preset == nullptr || !preset->isUser || this->currentMapText.trim().isEmpty()) return;
    const auto normalizedMapText = this->engine.normalizeInstrumentMapText(this->currentMapText);
    if (!preset->file.replaceWithText(normalizedMapText)) {
        this->reportStatus("Could not save preset: " + preset->name);
        return;
    }
    this->currentMapText = normalizedMapText;
    this->updateSelectedContent();
    this->reportStatus("Saved preset: " + preset->name);
}

void KitBrowserComponent::savePresetAs() {
    if (this->currentMapText.trim().isEmpty()) {
        this->reportStatus("There is no instrument map to save");
        return;
    }

    CompactModalDialog::Options options;
    options.title = "Save Instrument-Map Preset";
    options.message = "Enter a preset name.";
    options.confirmText = "Save";
    options.fields.push_back({ "name", "Name", "My Kit", 0, {} });
    juce::Component::SafePointer<KitBrowserComponent> safeThis(this);
    CompactModalDialog::show(*this, std::move(options), [safeThis](const CompactModalDialog& dialog) {
        if (safeThis == nullptr) return juce::String();
        const auto name = KitHomePanelHelpers::safePresetFileName(dialog.getFieldText("name"));
        if (name.isEmpty()) return juce::String("Enter a preset name.");
        auto directory = safeThis->getUserPresetDirectory();
        directory.createDirectory();
        auto file = directory.getChildFile(name + ".map");
        if (file.existsAsFile()) return juce::String("A preset with this name already exists.");
        const auto normalizedMapText = safeThis->engine.normalizeInstrumentMapText(safeThis->currentMapText);
        if (!file.replaceWithText(normalizedMapText)) return juce::String("Could not save this preset.");
        safeThis->currentMapText = normalizedMapText;
        safeThis->filterField.setText(juce::String(), false);
        safeThis->rebuildPresetEntries(file.getFullPathName());
        safeThis->rebuildFilteredEntries();
        safeThis->reportStatus("Saved preset: " + name);
        return juce::String();
    });
}

void KitBrowserComponent::deleteSelectedPreset() {
    const auto* selectedPreset = this->getSelectedPreset();
    if (selectedPreset == nullptr || !selectedPreset->isUser) return;
    const auto preset = *selectedPreset;

    CompactModalDialog::Options options;
    options.title = "Delete Instrument-Map Preset";
    options.message = "Delete \"" + preset.name + "\"?";
    options.confirmText = "Delete";
    options.destructive = true;
    juce::Component::SafePointer<KitBrowserComponent> safeThis(this);
    CompactModalDialog::show(*this, std::move(options), [safeThis, preset](const CompactModalDialog&) {
        if (safeThis == nullptr) return juce::String();
        if (!preset.file.deleteFile()) return juce::String("Could not delete this preset.");
        safeThis->selectedPresetPath.clear();
        safeThis->rebuildPresetEntries();
        safeThis->rebuildFilteredEntries();
        safeThis->reportStatus("Preset deleted");
        return juce::String();
    });
}

void KitBrowserComponent::reportStatus(const juce::String& message) {
    if (this->onStatusMessage) this->onStatusMessage(message);
}

void KitBrowserComponent::timerCallback() {
    this->rebuildFilteredEntries();
}

int KitBrowserComponent::SourceListModel::getNumRows() {
    return this->owner.filteredIndexes.empty() ? 1 : static_cast<int>(this->owner.filteredIndexes.size());
}

void KitBrowserComponent::SourceListModel::paintListBoxItem(int rowNumber, juce::Graphics& graphics, int width, int height, bool rowIsSelected) {
    if (this->owner.filteredIndexes.empty()) {
        graphics.fillAll(UITheme::Colours::bgControlAlt);
        graphics.setColour(UITheme::Colours::textMuted);
        graphics.setFont(UITheme::Fonts::normal());
        const bool sourceEmpty = this->owner.sourceMode == SourceMode::LibraryKits ? this->owner.kits.empty() : this->owner.presets.empty();
        graphics.drawFittedText(sourceEmpty ? "No items available" : "No matching items", 6, 0, width - 12, height, juce::Justification::centredLeft, 1);
        return;
    }
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.filteredIndexes.size())) return;

    graphics.fillAll(rowIsSelected ? UITheme::Colours::listSelection : (rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd));
    const int itemIndex = this->owner.filteredIndexes[static_cast<size_t>(rowNumber)];
    juce::String title;
    juce::String detail;
    if (this->owner.sourceMode == SourceMode::LibraryKits) {
        const auto& kit = this->owner.kits[static_cast<size_t>(itemIndex)];
        title = kit.name;
        detail = kit.author.isNotEmpty() ? kit.author : "Unknown author";
    } else {
        const auto& preset = this->owner.presets[static_cast<size_t>(itemIndex)];
        title = preset.name;
        detail = preset.isUser ? "User" : "Factory";
    }
    graphics.setColour(UITheme::Colours::textPrimary);
    graphics.setFont(UITheme::Fonts::normal());
    graphics.drawFittedText(title, 6, 1, width - 12, 14, juce::Justification::centredLeft, 1);
    graphics.setColour(UITheme::Colours::textMuted);
    graphics.setFont(UITheme::Fonts::small());
    graphics.drawFittedText(detail, 6, 14, width - 12, height - 14, juce::Justification::centredLeft, 1);
}

void KitBrowserComponent::SourceListModel::selectedRowsChanged(int lastRowSelected) {
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(this->owner.filteredIndexes.size())) return;
    const int itemIndex = this->owner.filteredIndexes[static_cast<size_t>(lastRowSelected)];
    if (this->owner.sourceMode == SourceMode::LibraryKits)
        this->owner.selectedKitId = this->owner.kits[static_cast<size_t>(itemIndex)].id;
    else
        this->owner.selectedPresetPath = this->owner.presets[static_cast<size_t>(itemIndex)].file.getFullPathName();
    this->owner.updateSelectedContent();
}

void KitBrowserComponent::paint(juce::Graphics& graphics) {
    graphics.fillAll(UITheme::Colours::bgEditor);
}

void KitBrowserComponent::updatePreviewGridSize() {
    const int width = juce::jmax(1, this->previewViewport.getWidth() - this->previewViewport.getScrollBarThickness());
    const int height = juce::jmax(this->previewViewport.getHeight(), this->previewGrid.getPreferredHeight(width));
    this->previewGrid.setSize(width, height);
}

void KitBrowserComponent::resized() {
    auto area = this->getLocalBounds().reduced(UITheme::Spacing::md);
    auto titleRow = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->titleLabel.setBounds(titleRow);
    area.removeFromTop(UITheme::Spacing::sm);

    auto toolbar = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->kitsModeButton.setBounds(toolbar.removeFromLeft(110));
    this->presetsModeButton.setBounds(toolbar.removeFromLeft(90));
    toolbar.removeFromLeft(UITheme::Spacing::md);
    this->filterField.setBounds(toolbar.removeFromLeft(220));
    area.removeFromTop(UITheme::Spacing::md);

    auto actions = area.removeFromBottom(UITheme::Sizes::standardControlHeight);
    this->loadButton.setBounds(actions.removeFromLeft(110));
    if (this->sourceMode == SourceMode::Presets) {
        this->deleteButton.setBounds(actions.removeFromRight(76));
        actions.removeFromRight(UITheme::Spacing::sm);
        this->saveAsButton.setBounds(actions.removeFromRight(82));
        actions.removeFromRight(UITheme::Spacing::sm);
        this->saveButton.setBounds(actions.removeFromRight(64));
    }
    area.removeFromBottom(UITheme::Spacing::md);

    const int listWidth = juce::jmax(180, area.getWidth() * 24 / 100);
    const int infoWidth = juce::jmax(220, area.getWidth() * 30 / 100);
    auto listArea = area.removeFromLeft(listWidth);
    area.removeFromLeft(UITheme::Spacing::md);
    auto infoArea = area.removeFromLeft(infoWidth);
    area.removeFromLeft(UITheme::Spacing::md);
    auto previewArea = area;

    this->sourceList.setBounds(listArea);
    this->infoTitleLabel.setBounds(infoArea.removeFromTop(24));
    this->infoAuthorLabel.setBounds(infoArea.removeFromTop(18));
    this->infoLicenseLabel.setBounds(infoArea.removeFromTop(18));
    infoArea.removeFromTop(UITheme::Spacing::sm);
    this->detailsEditor.setBounds(infoArea);
    this->previewLabel.setBounds(previewArea.removeFromTop(18));
    this->previewViewport.setBounds(previewArea);
    this->updatePreviewGridSize();
}
