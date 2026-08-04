#include "KitPage.h"

#include "../Engine/ArticulationNames.h"
#include "CompactModalDialog.h"
#include "UIComponentStyle.h"
#include "UITheme.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

static juce::String safePresetFileName(juce::String name) {
    return name.trim().replaceCharacters("\\/:|*?<>\"", "_________");
}

KitPage::KitPage(DrumKitEngine& drumKitEngine)
    : engine(drumKitEngine) {
    this->titleLabel.setText("Kits and Presets", juce::dontSendNotification);
    UIComponentStyle::configureSectionLabel(this->titleLabel);
    this->addAndMakeVisible(this->titleLabel);

    for (auto* button : { &this->kitsSectionButton, &this->presetsSectionButton }) {
        UIComponentStyle::configureCategoryButton(*button);
        this->addAndMakeVisible(*button);
    }
    this->kitsSectionButton.onClick = [this] { this->setActiveSection(KitSection::LibraryKits); };
    this->presetsSectionButton.onClick = [this] { this->setActiveSection(KitSection::Presets); };

    UIComponentStyle::configureBorderlessTextEditor(this->filterField);
    this->filterField.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    this->filterField.setTextToShowWhenEmpty("filter...", UITheme::Colours::textMuted);
    this->filterField.onTextChange = [this] { this->startTimer(this->filterDebounceMs); };
    this->addAndMakeVisible(this->filterField);

    this->sectionList.setModel(&this->sectionListModel);
    this->sectionList.setRowHeight(26);
    this->sectionList.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    this->addAndMakeVisible(this->sectionList);

    this->infoTitleLabel.setFont(UITheme::Fonts::sectionTitle());
    this->infoTitleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->infoTitleLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->infoTitleLabel);
    for (auto* label : { &this->infoAuthorLabel, &this->infoLicenseLabel }) {
        UIComponentStyle::configureCaptionLabel(*label, juce::Justification::centredLeft);
        this->addAndMakeVisible(*label);
    }

    this->detailsEditor.setMultiLine(true);
    this->detailsEditor.setReadOnly(true);
    this->detailsEditor.setScrollbarsShown(true);
    this->detailsEditor.setFont(UITheme::Fonts::normal());
    this->detailsEditor.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgEditor);
    this->detailsEditor.setColour(juce::TextEditor::textColourId, UITheme::Colours::textSecondary);
    UIComponentStyle::configureBorderlessTextEditor(this->detailsEditor);
    this->addAndMakeVisible(this->detailsEditor);

    this->previewLabel.setText("Instrument preview", juce::dontSendNotification);
    UIComponentStyle::configureCaptionLabel(this->previewLabel, juce::Justification::centredLeft);
    this->addAndMakeVisible(this->previewLabel);
    this->previewViewport.setViewedComponent(&this->previewGrid, false);
    this->previewViewport.setScrollBarsShown(true, false);
    this->previewViewport.setColour(juce::ScrollBar::backgroundColourId, UITheme::Colours::bgEditor);
    this->addAndMakeVisible(this->previewViewport);
    this->previewGrid.onLibraryArticulationPreview = [this](const AvailableLibraryArticulation& libArt, juce::uint8 velocity) {
        this->engine.previewLibraryArticulation(libArt.kitCodeRaw, libArt.instCode, libArt.artCode, velocity);
    };

    for (auto* button : { &this->loadButton, &this->saveButton, &this->saveAsButton, &this->deleteButton }) {
        UIComponentStyle::configureControlButton(*button);
        this->addAndMakeVisible(*button);
    }
    this->loadButton.onClick = [this] { this->loadSelected(); };
    this->saveButton.onClick = [this] { this->saveSelectedPreset(); };
    this->saveAsButton.onClick = [this] { this->savePresetAs(); };
    this->deleteButton.onClick = [this] { this->deleteSelectedPreset(); };

    this->refresh({});
}

KitPage::~KitPage() {
    this->previewViewport.setViewedComponent(nullptr, false);
}

void KitPage::refresh(const juce::String& activeInstrumentMapText) {
    this->currentMapText = activeInstrumentMapText;
    this->availableLibraryArticulations = AvailableLibraryArticulation::collectFromLibrary(this->engine.getInstrumentManager());
    this->rebuildLibraryEntries();
    this->rebuildPresetEntries(this->selectedPresetPath);
    this->rebuildFilteredEntries();
}

void KitPage::rebuildLibraryEntries() {
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

juce::File KitPage::getUserPresetDirectory() const {
    return this->engine.getUserPresetsDir().getChildFile("instrument_maps");
}

void KitPage::rebuildPresetEntries(const juce::String& preferredSelectionPath) {
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

bool KitPage::matchesFilter(const juce::String& searchText, const juce::String& filterText) {
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

void KitPage::rebuildFilteredEntries() {
    this->stopTimer();
    this->filteredIndexes.clear();
    const auto filterText = this->filterField.getText();
    if (this->activeSection == KitSection::LibraryKits) {
        for (int idx = 0; idx < static_cast<int>(this->kits.size()); ++idx)
            if (KitPage::matchesFilter(this->kits[static_cast<size_t>(idx)].searchText, filterText)) this->filteredIndexes.push_back(idx);
    } else {
        for (int idx = 0; idx < static_cast<int>(this->presets.size()); ++idx)
            if (KitPage::matchesFilter(this->presets[static_cast<size_t>(idx)].searchText, filterText)) this->filteredIndexes.push_back(idx);
    }

    this->sectionList.updateContent();
    int selectedRow = -1;
    for (int rowIndex = 0; rowIndex < static_cast<int>(this->filteredIndexes.size()); ++rowIndex) {
        const int sectionEntryIndex = this->filteredIndexes[static_cast<size_t>(rowIndex)];
        const bool selected = this->activeSection == KitSection::LibraryKits
            ? this->kits[static_cast<size_t>(sectionEntryIndex)].id == this->selectedKitId
            : this->presets[static_cast<size_t>(sectionEntryIndex)].file.getFullPathName() == this->selectedPresetPath;
        if (selected) {
            selectedRow = rowIndex;
            break;
        }
    }
    if (selectedRow < 0 && !this->filteredIndexes.empty()) {
        selectedRow = 0;
        const int sectionEntryIndex = this->filteredIndexes.front();
        if (this->activeSection == KitSection::LibraryKits)
            this->selectedKitId = this->kits[static_cast<size_t>(sectionEntryIndex)].id;
        else
            this->selectedPresetPath = this->presets[static_cast<size_t>(sectionEntryIndex)].file.getFullPathName();
    }
    this->sectionList.selectRow(selectedRow, false, false);
    this->updateSelectedContent();
}

void KitPage::setActiveSection(KitSection section) {
    if (this->activeSection == section) return;
    this->activeSection = section;
    this->filterField.clear();
    this->rebuildFilteredEntries();
    this->resized();
}

const KitPage::KitEntry* KitPage::getSelectedKit() const {
    for (const auto& kit : this->kits)
        if (kit.id == this->selectedKitId) return &kit;
    return nullptr;
}

const KitPage::PresetEntry* KitPage::getSelectedPreset() const {
    for (const auto& preset : this->presets)
        if (preset.file.getFullPathName() == this->selectedPresetPath) return &preset;
    return nullptr;
}

std::vector<KitPreviewGrid::ArticulationPad> KitPage::buildKitArticulationPads(const KitEntry& kit) const {
    std::vector<KitPreviewGrid::ArticulationPad> result;
    for (const auto& libArt : this->availableLibraryArticulations) {
        if (!libArt.rawKitCode.equalsIgnoreCase(kit.rawId) && !libArt.kitCodeRaw.equalsIgnoreCase(kit.id)) continue;
        KitPreviewGrid::ArticulationPad articulationPad;
        articulationPad.groupId = libArt.rawKitCode + "\n" + libArt.rawInstCode;
        articulationPad.groupTitle = libArt.rawInstCode.isNotEmpty() ? libArt.rawInstCode : libArt.instCode;
        articulationPad.libraryArticulation = libArt;
        result.push_back(std::move(articulationPad));
    }
    return result;
}

std::vector<KitPreviewGrid::ArticulationPad> KitPage::buildPresetArticulationPads(const PresetEntry& preset) const {
    std::vector<KitPreviewGrid::ArticulationPad> result;
    auto parsed = DrumKitEngine::parseInstrumentMapText(preset.file.loadFileAsString());
    const auto* library = this->engine.getInstrumentManager();
    if (!parsed.ok() || library == nullptr) return result;

    std::unordered_map<std::string, int> layerCounts;
    for (size_t mapLayerIndex = 0; mapLayerIndex < parsed.value.layers.size(); ++mapLayerIndex) {
        const auto& mapLayer = parsed.value.layers[mapLayerIndex];
        const auto* kit = library->findKitById(mapLayer.kitRef);
        const auto* instrument = kit != nullptr ? kit->findInstrumentById(mapLayer.instCode) : nullptr;
        if (kit == nullptr || instrument == nullptr) continue;

        juce::StringArray articulationSlotFilters;
        for (const auto& articulationSlotFilter : mapLayer.slotArticulationFilter) articulationSlotFilters.add(articulationSlotFilter);
        const auto articulationSlotText = articulationSlotFilters.joinIntoString(",");
        const auto layerKey = (mapLayer.slotId + "\n" + articulationSlotText).toLowerCase().toStdString();
        const int layerNumber = ++layerCounts[layerKey];
        juce::String groupTitle = mapLayer.slotId;
        if (articulationSlotText.isNotEmpty()) groupTitle += " / " + articulationSlotText;
        if (layerNumber > 1) groupTitle += " (map layer " + juce::String(layerNumber) + ")";
        groupTitle += " - " + (kit->meta.name.isNotEmpty() ? kit->meta.name : kit->meta.codeRaw) + "/" + instrument->meta.codeRaw;
        const auto groupId = juce::String(static_cast<juce::int64>(mapLayerIndex));

        std::unordered_set<std::string> addedArticulations;
        const auto& libraryArticulationFilters = !mapLayer.articulationFilter.empty() ? mapLayer.articulationFilter : mapLayer.slotArticulationFilter;
        for (const auto& libArt : this->availableLibraryArticulations) {
            if (!libArt.rawKitCode.equalsIgnoreCase(kit->meta.codeRaw) || !libArt.rawInstCode.equalsIgnoreCase(instrument->meta.codeRaw)) continue;
            if (!libraryArticulationFilters.empty()) {
                const bool matches = std::any_of(libraryArticulationFilters.begin(), libraryArticulationFilters.end(), [&libArt](const auto& filter) {
                    return ArticulationNames::mapFilterMatchesArticulation(filter, libArt.rawArtCode);
                });
                if (!matches) continue;
            }
            const auto articulationKey = libArt.rawArtCode.toLowerCase().toStdString();
            if (!addedArticulations.insert(articulationKey).second) continue;
            result.push_back({ groupId, groupTitle, libArt });
        }
    }
    return result;
}

void KitPage::updateSelectedContent() {
    std::vector<KitPreviewGrid::ArticulationPad> articulationPads;
    if (this->filteredIndexes.empty()) {
        const bool sectionEmpty = this->activeSection == KitSection::LibraryKits ? this->kits.empty() : this->presets.empty();
        const auto emptyText = this->activeSection == KitSection::LibraryKits ? juce::String("No library kits available") : juce::String("No presets available");
        const auto noMatchText = this->activeSection == KitSection::LibraryKits ? juce::String("No matching library kits") : juce::String("No matching presets");
        this->infoTitleLabel.setText(sectionEmpty ? emptyText : noMatchText, juce::dontSendNotification);
        this->infoAuthorLabel.setText(sectionEmpty && this->activeSection == KitSection::LibraryKits ? "Check the sample library path" : "Try a different filter", juce::dontSendNotification);
        this->infoLicenseLabel.setText(juce::String(), juce::dontSendNotification);
        this->detailsEditor.clear();
        this->previewGrid.setArticulationPads({});
        this->updatePreviewGridSize();
        this->updateActionButtons();
        return;
    }
    if (this->activeSection == KitSection::LibraryKits) {
        const auto* kit = this->getSelectedKit();
        if (kit != nullptr) {
            this->infoTitleLabel.setText(kit->name, juce::dontSendNotification);
            this->infoAuthorLabel.setText(kit->author.isNotEmpty() ? kit->author : "Unknown author", juce::dontSendNotification);
            this->infoLicenseLabel.setText(kit->license.isNotEmpty() ? "[" + kit->license + "]" : "No license information", juce::dontSendNotification);
            this->detailsEditor.setFont(UITheme::Fonts::normal());
            this->detailsEditor.setText(kit->readmeMarkdown.isNotEmpty() ? kit->readmeMarkdown : "No README.md found", false);
            articulationPads = this->buildKitArticulationPads(*kit);
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
            articulationPads = this->buildPresetArticulationPads(*preset);
        } else {
            this->infoTitleLabel.setText("No presets", juce::dontSendNotification);
            this->infoAuthorLabel.setText("Save the current instrument map to create one", juce::dontSendNotification);
            this->infoLicenseLabel.setText(juce::String(), juce::dontSendNotification);
            this->detailsEditor.clear();
        }
    }
    this->detailsEditor.setCaretPosition(0);
    this->previewGrid.setArticulationPads(std::move(articulationPads));
    this->updatePreviewGridSize();
    this->updateActionButtons();
}

void KitPage::updateActionButtons() {
    const bool presetsSectionActive = this->activeSection == KitSection::Presets;
    const auto* preset = this->getSelectedPreset();
    const bool hasVisibleSelection = !this->filteredIndexes.empty();
    this->loadButton.setButtonText(presetsSectionActive ? "LOAD PRESET" : "LOAD KIT");
    this->loadButton.setEnabled(hasVisibleSelection && (presetsSectionActive ? preset != nullptr : this->getSelectedKit() != nullptr));
    this->saveButton.setVisible(presetsSectionActive);
    this->saveAsButton.setVisible(presetsSectionActive);
    this->deleteButton.setVisible(presetsSectionActive);
    this->saveButton.setEnabled(hasVisibleSelection && preset != nullptr && preset->isUser && this->currentMapText.trim().isNotEmpty());
    this->saveAsButton.setEnabled(this->currentMapText.trim().isNotEmpty());
    this->deleteButton.setEnabled(hasVisibleSelection && preset != nullptr && preset->isUser);

    this->kitsSectionButton.setColour(juce::TextButton::buttonColourId, !presetsSectionActive ? UITheme::Colours::selectionStrong : UITheme::Colours::bgControl);
    this->presetsSectionButton.setColour(juce::TextButton::buttonColourId, presetsSectionActive ? UITheme::Colours::selectionStrong : UITheme::Colours::bgControl);
}

void KitPage::loadSelected() {
    if (this->activeSection == KitSection::LibraryKits) this->loadSelectedKit();
    else this->loadSelectedPreset();
}

void KitPage::loadSelectedKit() {
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

    juce::StringArray fallbackWarnings;
    const auto mapText = this->engine.normalizeInstrumentMapText(this->engine.buildInstrumentMapForKit(*layout, *kit, &fallbackWarnings));
    auto parsed = DrumKitEngine::parseInstrumentMapText(mapText);
    if (!parsed.ok() || parsed.value.layers.empty()) {
        this->reportStatus("This kit has no instruments compatible with the active kit layout");
        return;
    }

    std::unordered_set<std::string> mappedSlots;
    for (const auto& mapLayer : parsed.value.layers) mappedSlots.insert(mapLayer.slotId.toLowerCase().toStdString());
    juce::StringArray skippedSlots;
    for (const auto& slot : layout->instrumentSlots)
        if (mappedSlots.find(slot.id.toLowerCase().toStdString()) == mappedSlots.end()) skippedSlots.add(slot.display.isNotEmpty() ? slot.display : slot.id);

    juce::String status = "Loaded kit: " + selectedKit->name;
    if (!skippedSlots.isEmpty()) status += " | no compatible instrument for: " + skippedSlots.joinIntoString(", ");
    if (!fallbackWarnings.isEmpty()) status += " | " + fallbackWarnings.joinIntoString(" | ");
    if (this->onMapLoadRequested && this->onMapLoadRequested(mapText, status)) {
        this->currentMapText = mapText;
        this->updateActionButtons();
    }
}

void KitPage::loadSelectedPreset() {
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
    for (const auto& mapLayer : parsed.value.layers) {
        const auto slotIterator = validSlots.find(mapLayer.slotId.toLowerCase().toStdString());
        if (slotIterator != validSlots.end()) {
            auto compatibleMapLayer = mapLayer;
            compatibleMapLayer.slotId = slotIterator->second;
            compatibleMap.layers.push_back(std::move(compatibleMapLayer));
        } else if (!skippedSlots.contains(mapLayer.slotId)) {
            skippedSlots.add(mapLayer.slotId);
        }
    }
    if (compatibleMap.layers.empty()) {
        this->reportStatus("The preset has no map layers for the active kit layout");
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

void KitPage::saveSelectedPreset() {
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

void KitPage::savePresetAs() {
    if (this->currentMapText.trim().isEmpty()) {
        this->reportStatus("There is no instrument map to save");
        return;
    }

    CompactModalDialog::Options options;
    options.title = "Save Instrument-Map Preset";
    options.message = "Enter a preset name.";
    options.confirmText = "Save";
    options.fields.push_back({ "name", "Name", "My Kit", 0, {} });
    juce::Component::SafePointer<KitPage> safeThis(this);
    CompactModalDialog::show(*this, std::move(options), [safeThis](const CompactModalDialog& dialog) {
        if (safeThis == nullptr) return juce::String();
        const auto name = safePresetFileName(dialog.getFieldText("name"));
        if (name.isEmpty()) return juce::String("Enter a preset name.");
        const bool nameExists = std::any_of(safeThis->presets.begin(), safeThis->presets.end(), [&name](const auto& preset) {
            return preset.isUser && preset.name.equalsIgnoreCase(name);
        });
        if (nameExists) return juce::String("A preset with this name already exists.");
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

void KitPage::deleteSelectedPreset() {
    const auto* selectedPreset = this->getSelectedPreset();
    if (selectedPreset == nullptr || !selectedPreset->isUser) return;
    const auto preset = *selectedPreset;

    CompactModalDialog::Options options;
    options.title = "Delete Instrument-Map Preset";
    options.message = "Delete \"" + preset.name + "\"?";
    options.confirmText = "Delete";
    options.destructive = true;
    juce::Component::SafePointer<KitPage> safeThis(this);
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

void KitPage::reportStatus(const juce::String& message) {
    if (this->onStatusMessage) this->onStatusMessage(message);
}

void KitPage::timerCallback() {
    this->rebuildFilteredEntries();
}

int KitPage::SectionListModel::getNumRows() {
    return this->owner.filteredIndexes.empty() ? 1 : static_cast<int>(this->owner.filteredIndexes.size());
}

void KitPage::SectionListModel::paintListBoxItem(int rowNumber, juce::Graphics& graphics, int width, int height, bool rowIsSelected) {
    if (this->owner.filteredIndexes.empty()) {
        graphics.fillAll(UITheme::Colours::bgControlAlt);
        graphics.setColour(UITheme::Colours::textMuted);
        graphics.setFont(UITheme::Fonts::normal());
        const bool sectionEmpty = this->owner.activeSection == KitSection::LibraryKits ? this->owner.kits.empty() : this->owner.presets.empty();
        const auto emptyText = this->owner.activeSection == KitSection::LibraryKits ? juce::String("No library kits available") : juce::String("No presets available");
        const auto noMatchText = this->owner.activeSection == KitSection::LibraryKits ? juce::String("No matching library kits") : juce::String("No matching presets");
        graphics.drawFittedText(sectionEmpty ? emptyText : noMatchText, 6, 0, width - 12, height, juce::Justification::centredLeft, 1);
        return;
    }
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.filteredIndexes.size())) return;

    graphics.fillAll(rowIsSelected ? UITheme::Colours::listSelection : (rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd));
    const int sectionEntryIndex = this->owner.filteredIndexes[static_cast<size_t>(rowNumber)];
    juce::String title;
    juce::String detail;
    if (this->owner.activeSection == KitSection::LibraryKits) {
        const auto& kit = this->owner.kits[static_cast<size_t>(sectionEntryIndex)];
        title = kit.name;
        detail = kit.author.isNotEmpty() ? kit.author : "Unknown author";
    } else {
        const auto& preset = this->owner.presets[static_cast<size_t>(sectionEntryIndex)];
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

void KitPage::SectionListModel::selectedRowsChanged(int lastRowSelected) {
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(this->owner.filteredIndexes.size())) return;
    const int sectionEntryIndex = this->owner.filteredIndexes[static_cast<size_t>(lastRowSelected)];
    if (this->owner.activeSection == KitSection::LibraryKits)
        this->owner.selectedKitId = this->owner.kits[static_cast<size_t>(sectionEntryIndex)].id;
    else
        this->owner.selectedPresetPath = this->owner.presets[static_cast<size_t>(sectionEntryIndex)].file.getFullPathName();
    this->owner.updateSelectedContent();
}

void KitPage::paint(juce::Graphics& graphics) {
    graphics.fillAll(UITheme::Colours::bgEditor);
}

void KitPage::updatePreviewGridSize() {
    const int width = juce::jmax(1, this->previewViewport.getWidth() - this->previewViewport.getScrollBarThickness());
    const int height = juce::jmax(this->previewViewport.getHeight(), this->previewGrid.getPreferredHeight(width));
    this->previewGrid.setSize(width, height);
}

void KitPage::resized() {
    auto area = this->getLocalBounds().reduced(UITheme::Spacing::md);
    auto titleRow = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->titleLabel.setBounds(titleRow);
    area.removeFromTop(UITheme::Spacing::sm);

    auto toolbar = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->kitsSectionButton.setBounds(toolbar.removeFromLeft(110));
    this->presetsSectionButton.setBounds(toolbar.removeFromLeft(90));
    toolbar.removeFromLeft(UITheme::Spacing::md);
    this->filterField.setBounds(toolbar.removeFromLeft(220));
    area.removeFromTop(UITheme::Spacing::md);

    auto actions = area.removeFromBottom(UITheme::Sizes::standardControlHeight);
    this->loadButton.setBounds(actions.removeFromLeft(110));
    if (this->activeSection == KitSection::Presets) {
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

    this->sectionList.setBounds(listArea);
    this->infoTitleLabel.setBounds(infoArea.removeFromTop(24));
    this->infoAuthorLabel.setBounds(infoArea.removeFromTop(18));
    this->infoLicenseLabel.setBounds(infoArea.removeFromTop(18));
    infoArea.removeFromTop(UITheme::Spacing::sm);
    this->detailsEditor.setBounds(infoArea);
    this->previewLabel.setBounds(previewArea.removeFromTop(18));
    this->previewViewport.setBounds(previewArea);
    this->updatePreviewGridSize();
}
