#include "KitPage.h"

#include "../Engine/InstrumentMap/InstrumentMapOperations.h"
#include "CompactModalDialog.h"
#include "UIComponentStyle.h"
#include "UITheme.h"
#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <utility>

static juce::String safePresetFileName(juce::String name) {
    return name.trim().replaceCharacters("\\/:|*?<>\"", "_________");
}

KitPage::KitPage(DrumKitEngine& drumKitEngine)
    : engine(drumKitEngine) {
    for (auto* button : { &this->kitsSectionButton, &this->presetsSectionButton }) {
        UIComponentStyle::configureCategoryButton(*button);
        this->addAndMakeVisible(*button);
    }
    this->kitsSectionButton.onClick = [this] { this->setActiveSection(KitSection::LibraryKits); };
    this->presetsSectionButton.onClick = [this] { this->setActiveSection(KitSection::Presets); };

    this->sectionList.setModel(&this->sectionListModel);
    this->sectionList.setRowHeight(26);
    this->sectionList.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    this->addAndMakeVisible(this->sectionList);

    this->infoTitleLabel.setFont(UITheme::Fonts::normal().boldened());
    this->infoTitleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->infoTitleLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->infoTitleLabel);
    this->detailsEditor.setMultiLine(true, false);
    this->detailsEditor.setReadOnly(true);
    this->detailsEditor.setScrollbarsShown(true);
    this->detailsEditor.setFont(UITheme::Fonts::normal());
    this->detailsEditor.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgEditor);
    this->detailsEditor.setColour(juce::TextEditor::textColourId, UITheme::Colours::textSecondary);
    UIComponentStyle::configureBorderlessTextEditor(this->detailsEditor);
    this->addAndMakeVisible(this->detailsEditor);

    this->previewViewport.setViewedComponent(&this->previewGrid, false);
    this->previewViewport.setScrollBarsShown(true, false);
    this->previewViewport.setColour(juce::ScrollBar::backgroundColourId, UITheme::Colours::bgEditor);
    this->addAndMakeVisible(this->previewViewport);
    this->previewGrid.onPreview = [this](const juce::String& slotId, const juce::String& articulationCode, bool useDirectPreview, const std::vector<KitPreviewGrid::PreviewTarget>& previewTargets, juce::uint8 velocity) {
        if (this->previewUsesActiveMap) {
            if (useDirectPreview) this->engine.playPreviewArticulation(slotId, articulationCode, velocity);
            else this->engine.playPreviewNote(slotId, articulationCode, velocity);
            return;
        }

        const auto targets = useDirectPreview ? this->resolveDirectPreviewTargets(previewTargets, articulationCode) : previewTargets;
        std::vector<DrumKitEngine::LibraryPreviewLayer> previewLayers;
        previewLayers.reserve(targets.size());
        for (const auto& target : targets) previewLayers.push_back({ target.libraryArticulation.kitCodeRaw, target.libraryArticulation.instCode, target.libraryArticulation.artCode, target.gainDb });
        this->engine.previewLibraryArticulationGroup(previewLayers, velocity);
    };

    for (auto* button : { &this->primaryActionButton, &this->moreButton }) {
        UIComponentStyle::configureControlButton(*button);
        this->addAndMakeVisible(*button);
    }
    this->primaryActionButton.onClick = [this] { this->performPrimaryAction(); };
    this->moreButton.onClick = [this] { this->showPresetActionsMenu(); };

    this->refresh({});
}

KitPage::~KitPage() {
    this->previewViewport.setViewedComponent(nullptr, false);
}

void KitPage::refresh(const juce::String& activeInstrumentMapText) {
    this->currentMapText = this->engine.normalizeInstrumentMapText(activeInstrumentMapText);
    this->availableLibraryArticulations = AvailableLibraryArticulation::collectFromLibrary(this->engine.getInstrumentManager());
    this->rebuildLibraryEntries();
    this->rebuildPresetEntries(this->selectedPresetPath);
    this->inferActivePreset();
    this->rebuildListIndexes();
}

void KitPage::setPreviewVelocity(float velocity) {
    this->previewGrid.setClickVelocity(velocity);
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

void KitPage::rebuildListIndexes() {
    this->entryIndexes.clear();
    const int entryCount = this->activeSection == KitSection::LibraryKits ? static_cast<int>(this->kits.size()) : static_cast<int>(this->presets.size());
    this->entryIndexes.reserve(static_cast<size_t>(entryCount));
    for (int entryIndex = 0; entryIndex < entryCount; ++entryIndex) this->entryIndexes.push_back(entryIndex);

    this->sectionList.updateContent();
    int selectedRow = -1;
    for (int rowIndex = 0; rowIndex < static_cast<int>(this->entryIndexes.size()); ++rowIndex) {
        const int sectionEntryIndex = this->entryIndexes[static_cast<size_t>(rowIndex)];
        const bool selected = this->activeSection == KitSection::LibraryKits
            ? this->kits[static_cast<size_t>(sectionEntryIndex)].id == this->selectedKitId
            : this->presets[static_cast<size_t>(sectionEntryIndex)].file.getFullPathName() == this->selectedPresetPath;
        if (selected) {
            selectedRow = rowIndex;
            break;
        }
    }
    if (selectedRow < 0 && !this->entryIndexes.empty()) {
        selectedRow = 0;
        const int sectionEntryIndex = this->entryIndexes.front();
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
    this->rebuildListIndexes();
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

bool KitPage::isActivePreset(const PresetEntry& preset) const {
    return preset.file.getFullPathName() == this->activePresetPath;
}

bool KitPage::isActivePresetDirty() const {
    return this->activePresetPath.isNotEmpty() && this->currentMapText != this->activePresetMapText;
}

void KitPage::inferActivePreset() {
    if (this->activePresetPath.isNotEmpty()) {
        const bool activePresetExists = std::any_of(this->presets.begin(), this->presets.end(), [this](const auto& preset) { return preset.file.getFullPathName() == this->activePresetPath; });
        if (!activePresetExists) this->clearActivePreset();
        else if (this->currentMapText == this->activePresetMapText) return;
    }
    if (this->currentMapText.trim().isEmpty()) return;

    for (const auto& preset : this->presets) {
        const auto presetMapText = this->engine.normalizeInstrumentMapText(preset.file.loadFileAsString());
        if (presetMapText != this->currentMapText) continue;
        this->setActivePreset(preset, presetMapText);
        this->selectedPresetPath = preset.file.getFullPathName();
        return;
    }
}

void KitPage::setActivePreset(const PresetEntry& preset, const juce::String& mapText) {
    this->activePresetPath = preset.file.getFullPathName();
    this->activePresetMapText = this->engine.normalizeInstrumentMapText(mapText);
}

void KitPage::clearActivePreset() {
    this->activePresetPath.clear();
    this->activePresetMapText.clear();
}

std::vector<KitPreviewGrid::PreviewInstrument> KitPage::buildKitPreviewInstruments(const KitEntry& kit) const {
    std::vector<KitPreviewGrid::PreviewInstrument> result;
    std::unordered_map<std::string, size_t> resultIndexByInstrument;
    for (const auto& libArt : this->availableLibraryArticulations) {
        if (!libArt.rawKitCode.equalsIgnoreCase(kit.rawId) && !libArt.kitCodeRaw.equalsIgnoreCase(kit.id)) continue;
        const auto instrumentKey = (libArt.rawKitCode + "\n" + libArt.rawInstCode).toLowerCase().toStdString();
        auto resultIndexIterator = resultIndexByInstrument.find(instrumentKey);
        if (resultIndexIterator == resultIndexByInstrument.end()) {
            KitPreviewGrid::PreviewInstrument previewInstrument;
            previewInstrument.slot.id = libArt.instCode;
            previewInstrument.slot.display = libArt.rawInstCode.isNotEmpty() ? libArt.rawInstCode : libArt.instCode;
            previewInstrument.slot.type = libArt.instType;
            result.push_back(std::move(previewInstrument));
            resultIndexIterator = resultIndexByInstrument.emplace(instrumentKey, result.size() - 1).first;
        }

        auto& previewInstrument = result[resultIndexIterator->second];
        const auto articulationCode = libArt.rawArtCode.isNotEmpty() ? libArt.rawArtCode : libArt.artCode;
        const auto articulationIterator = std::find_if(previewInstrument.slot.articulations.begin(), previewInstrument.slot.articulations.end(), [&articulationCode](const auto& articulation) {
            return articulation.codeRaw.compareIgnoreCase(articulationCode) == 0;
        });
        if (articulationIterator != previewInstrument.slot.articulations.end()) continue;
        previewInstrument.slot.articulations.emplace_back(articulationCode);
        previewInstrument.articulationTargets.push_back({ { libArt, 0.0f } });
    }
    return result;
}

std::vector<KitPreviewGrid::PreviewInstrument> KitPage::buildPresetPreviewInstruments(const juce::String& mapText) const {
    std::vector<KitPreviewGrid::PreviewInstrument> result;
    auto parsed = DrumKitEngine::parseInstrumentMapText(mapText);
    const auto* library = this->engine.getInstrumentManager();
    const auto* layout = this->engine.getKitLayout();
    if (!parsed.ok() || library == nullptr || layout == nullptr) return result;

    const auto projections = InstrumentMap::projectLayers(parsed.value, *layout);
    for (const auto& layoutSlot : layout->instrumentSlots) {
        KitPreviewGrid::PreviewInstrument previewInstrument;
        previewInstrument.slot.id = layoutSlot.id;
        previewInstrument.slot.display = layoutSlot.display;
        previewInstrument.slot.type = layoutSlot.type;

        std::vector<juce::String> articulationCodes;
        if (layoutSlot.articulations.empty()) articulationCodes.push_back("(default)");
        else for (const auto& articulation : layoutSlot.articulations) articulationCodes.push_back(articulation.codeRaw);

        for (const auto& articulationCode : articulationCodes) {
            std::vector<KitPreviewGrid::PreviewTarget> articulationTargets;
            for (const auto& projection : projections) {
                if (projection.slotId.compareIgnoreCase(layoutSlot.id) != 0 || projection.articulationSlotCode.compareIgnoreCase(articulationCode) != 0 || projection.mapLayerIndex >= parsed.value.layers.size()) continue;
                const auto& mapLayer = parsed.value.layers[projection.mapLayerIndex];
                const auto* libraryKit = library->findKitById(mapLayer.kitRef);
                if (libraryKit == nullptr) continue;

                auto qualifiedLibraryArticulationId = InstrumentMap::qualifiedLibraryArticulationIdForSlot(mapLayer, articulationCode);
                juce::StringArray qualifiedIdParts;
                qualifiedIdParts.addTokens(qualifiedLibraryArticulationId, "/", "");
                qualifiedIdParts.removeEmptyStrings();
                if (qualifiedIdParts.size() < 3 && articulationCode != "(default)") qualifiedLibraryArticulationId += "/" + articulationCode;
                if (layoutSlot.type == KitLib::InstrumentSpec::Type::HiHat && InstrumentMap::usesLegacyArticulationSlotExpressionFilter(mapLayer.slotArticulationFilter, articulationCode)) {
                    qualifiedLibraryArticulationId = InstrumentMap::resolveLegacyHihatLibraryArticulationId(*libraryKit, qualifiedLibraryArticulationId, articulationCode);
                }

                int availableIndex = AvailableLibraryArticulation::findIndexByQualifiedId(this->availableLibraryArticulations, qualifiedLibraryArticulationId);
                if (availableIndex < 0) {
                    availableIndex = static_cast<int>(std::distance(this->availableLibraryArticulations.begin(), std::find_if(this->availableLibraryArticulations.begin(), this->availableLibraryArticulations.end(), [&mapLayer](const auto& libArt) {
                        const bool matchesKit = libArt.kitCodeRaw.equalsIgnoreCase(mapLayer.kitRef) || libArt.rawKitCode.equalsIgnoreCase(mapLayer.kitRef);
                        const bool matchesInstrument = libArt.instCode.equalsIgnoreCase(mapLayer.instCode) || libArt.rawInstCode.equalsIgnoreCase(mapLayer.instCode);
                        return matchesKit && matchesInstrument;
                    })));
                    if (availableIndex >= static_cast<int>(this->availableLibraryArticulations.size())) availableIndex = -1;
                }
                if (availableIndex >= 0) articulationTargets.push_back({ this->availableLibraryArticulations[static_cast<size_t>(availableIndex)], mapLayer.volumeDb.value_or(0.0f) });
            }
            if (articulationTargets.empty()) continue;
            previewInstrument.slot.articulations.emplace_back(articulationCode);
            previewInstrument.articulationTargets.push_back(std::move(articulationTargets));
        }
        if (!previewInstrument.articulationTargets.empty()) result.push_back(std::move(previewInstrument));
    }
    return result;
}

std::vector<KitPreviewGrid::PreviewTarget> KitPage::resolveDirectPreviewTargets(const std::vector<KitPreviewGrid::PreviewTarget>& targets, const juce::String& articulationCode) const {
    std::vector<KitPreviewGrid::PreviewTarget> result;
    result.reserve(targets.size());
    for (const auto& target : targets) {
        const auto& source = target.libraryArticulation;
        const auto matchingIterator = std::find_if(this->availableLibraryArticulations.begin(), this->availableLibraryArticulations.end(), [&source, &articulationCode](const auto& candidate) {
            const bool sameKit = candidate.kitCodeRaw.equalsIgnoreCase(source.kitCodeRaw) || candidate.rawKitCode.equalsIgnoreCase(source.rawKitCode);
            const bool sameInstrument = candidate.instCode.equalsIgnoreCase(source.instCode) || candidate.rawInstCode.equalsIgnoreCase(source.rawInstCode);
            const bool sameArticulation = candidate.artCode.equalsIgnoreCase(articulationCode) || candidate.rawArtCode.equalsIgnoreCase(articulationCode);
            return sameKit && sameInstrument && sameArticulation;
        });
        result.push_back(matchingIterator != this->availableLibraryArticulations.end() ? KitPreviewGrid::PreviewTarget { *matchingIterator, target.gainDb } : target);
    }
    return result;
}

void KitPage::updateSelectedContent() {
    std::vector<KitPreviewGrid::PreviewInstrument> previewInstruments;
    this->previewUsesActiveMap = false;
    if (this->entryIndexes.empty()) {
        const auto emptyText = this->activeSection == KitSection::LibraryKits ? juce::String("No library kits available") : juce::String("No presets available");
        this->infoTitleLabel.setText(emptyText, juce::dontSendNotification);
        this->detailsEditor.clear();
        this->previewGrid.setPreviewInstruments({});
        this->updatePreviewGridSize();
        this->updateActionButtons();
        return;
    }
    if (this->activeSection == KitSection::LibraryKits) {
        const auto* kit = this->getSelectedKit();
        if (kit != nullptr) {
            auto headerText = kit->name + " by " + (kit->author.isNotEmpty() ? kit->author : juce::String("Unknown author"));
            if (kit->license.isNotEmpty()) headerText += " [" + kit->license + "]";
            this->infoTitleLabel.setText(headerText, juce::dontSendNotification);
            this->detailsEditor.setFont(UITheme::Fonts::normal());
            this->detailsEditor.setText(kit->readmeMarkdown.isNotEmpty() ? kit->readmeMarkdown : "No README.md found", false);
            previewInstruments = this->buildKitPreviewInstruments(*kit);
        } else {
            this->infoTitleLabel.setText("No library kits", juce::dontSendNotification);
            this->detailsEditor.clear();
        }
    } else {
        const auto* preset = this->getSelectedPreset();
        if (preset != nullptr) {
            const bool selectedPresetIsDirty = this->isActivePreset(*preset) && this->isActivePresetDirty();
            const auto displayMapText = selectedPresetIsDirty ? this->currentMapText : preset->file.loadFileAsString();
            const auto headerText = preset->name + (selectedPresetIsDirty ? "*" : "") + " by " + (preset->isUser ? "User" : "Factory");
            this->infoTitleLabel.setText(headerText, juce::dontSendNotification);
            this->detailsEditor.setFont(UITheme::Fonts::mono());
            this->detailsEditor.setText(displayMapText, false);
            this->previewUsesActiveMap = this->isActivePreset(*preset);
            previewInstruments = this->buildPresetPreviewInstruments(displayMapText);
        } else {
            this->infoTitleLabel.setText("No presets", juce::dontSendNotification);
            this->detailsEditor.clear();
        }
    }
    this->detailsEditor.setCaretPosition(0);
    this->previewGrid.setPreviewInstruments(std::move(previewInstruments));
    this->updatePreviewGridSize();
    this->updateActionButtons();
}

void KitPage::updateActionButtons() {
    const bool presetsSectionActive = this->activeSection == KitSection::Presets;
    const auto* preset = this->getSelectedPreset();
    const bool selectedPresetIsDirty = presetsSectionActive && preset != nullptr && this->isActivePreset(*preset) && this->isActivePresetDirty();
    this->primaryActionButton.setButtonText(selectedPresetIsDirty ? "SAVE" : (presetsSectionActive ? "LOAD PRESET" : "LOAD KIT"));
    this->primaryActionButton.setEnabled(selectedPresetIsDirty ? this->currentMapText.trim().isNotEmpty() : (presetsSectionActive ? preset != nullptr : this->getSelectedKit() != nullptr));
    this->moreButton.setVisible(presetsSectionActive);
    this->moreButton.setEnabled(preset != nullptr);

    this->kitsSectionButton.setColour(juce::TextButton::buttonColourId, !presetsSectionActive ? UITheme::Colours::selectionStrong : UITheme::Colours::bgControl);
    this->presetsSectionButton.setColour(juce::TextButton::buttonColourId, presetsSectionActive ? UITheme::Colours::selectionStrong : UITheme::Colours::bgControl);
}

void KitPage::performPrimaryAction() {
    if (this->activeSection == KitSection::LibraryKits) {
        this->loadSelectedKit();
        return;
    }

    const auto* preset = this->getSelectedPreset();
    if (preset != nullptr && this->isActivePreset(*preset) && this->isActivePresetDirty()) {
        if (preset->isUser) this->saveSelectedPreset();
        else this->savePresetAs();
        return;
    }
    this->loadSelectedPreset();
}

void KitPage::showPresetActionsMenu() {
    const auto* preset = this->getSelectedPreset();
    if (this->activeSection != KitSection::Presets || preset == nullptr) return;

    juce::PopupMenu menu;
    menu.setLookAndFeel(&this->getLookAndFeel());
    menu.addItem(1, "SAVE AS...", this->currentMapText.trim().isNotEmpty());
    if (preset->isUser) {
        menu.addSeparator();
        menu.addItem(2, "DELETE");
    }

    juce::Component::SafePointer<KitPage> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this->moreButton), [safeThis](int result) {
        if (safeThis == nullptr) return;
        if (result == 1) safeThis->savePresetAs();
        else if (result == 2) safeThis->deleteSelectedPreset();
    });
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
        this->clearActivePreset();
        this->sectionList.repaint();
        this->updateSelectedContent();
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
        this->setActivePreset(*preset, mapText);
        this->sectionList.repaint();
        this->updateSelectedContent();
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
    this->setActivePreset(*preset, normalizedMapText);
    this->sectionList.repaint();
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
        safeThis->activePresetPath = file.getFullPathName();
        safeThis->activePresetMapText = normalizedMapText;
        safeThis->rebuildPresetEntries(file.getFullPathName());
        safeThis->rebuildListIndexes();
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
        if (safeThis->isActivePreset(preset)) safeThis->clearActivePreset();
        safeThis->selectedPresetPath.clear();
        safeThis->rebuildPresetEntries();
        safeThis->rebuildListIndexes();
        safeThis->reportStatus("Preset deleted");
        return juce::String();
    });
}

void KitPage::reportStatus(const juce::String& message) {
    if (this->onStatusMessage) this->onStatusMessage(message);
}

int KitPage::SectionListModel::getNumRows() {
    return this->owner.entryIndexes.empty() ? 1 : static_cast<int>(this->owner.entryIndexes.size());
}

void KitPage::SectionListModel::paintListBoxItem(int rowNumber, juce::Graphics& graphics, int width, int height, bool rowIsSelected) {
    if (this->owner.entryIndexes.empty()) {
        graphics.fillAll(UITheme::Colours::bgControlAlt);
        graphics.setColour(UITheme::Colours::textMuted);
        graphics.setFont(UITheme::Fonts::normal());
        const auto emptyText = this->owner.activeSection == KitSection::LibraryKits ? juce::String("No library kits available") : juce::String("No presets available");
        graphics.drawFittedText(emptyText, 6, 0, width - 12, height, juce::Justification::centredLeft, 1);
        return;
    }
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.entryIndexes.size())) return;

    graphics.fillAll(rowIsSelected ? UITheme::Colours::listSelection : (rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd));
    const int sectionEntryIndex = this->owner.entryIndexes[static_cast<size_t>(rowNumber)];
    juce::String title;
    juce::String detail;
    if (this->owner.activeSection == KitSection::LibraryKits) {
        const auto& kit = this->owner.kits[static_cast<size_t>(sectionEntryIndex)];
        title = kit.name;
        detail = kit.author.isNotEmpty() ? kit.author : "Unknown author";
    } else {
        const auto& preset = this->owner.presets[static_cast<size_t>(sectionEntryIndex)];
        title = preset.name + (this->owner.isActivePreset(preset) && this->owner.isActivePresetDirty() ? "*" : "");
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
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(this->owner.entryIndexes.size())) return;
    const int sectionEntryIndex = this->owner.entryIndexes[static_cast<size_t>(lastRowSelected)];
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
    const int listWidth = juce::jlimit(110, 130, area.getWidth() * 13 / 100);
    auto headerRow = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    auto sectionTabs = headerRow.removeFromLeft(listWidth);
    const int kitsTabWidth = sectionTabs.getWidth() / 2;
    this->kitsSectionButton.setBounds(sectionTabs.removeFromLeft(kitsTabWidth));
    this->presetsSectionButton.setBounds(sectionTabs);
    headerRow.removeFromLeft(UITheme::Spacing::md);
    if (this->activeSection == KitSection::Presets) {
        this->moreButton.setBounds(headerRow.removeFromRight(28));
        headerRow.removeFromRight(UITheme::Spacing::sm);
    }
    this->primaryActionButton.setBounds(headerRow.removeFromRight(100));
    headerRow.removeFromRight(UITheme::Spacing::sm);
    this->infoTitleLabel.setBounds(headerRow);
    area.removeFromTop(UITheme::Spacing::md);

    auto listArea = area.removeFromLeft(listWidth);
    area.removeFromLeft(UITheme::Spacing::md);
    auto contentArea = area;

    this->sectionList.setBounds(listArea);
    const int fourPreviewRowsHeight = this->previewGrid.getHeightForRows(4);
    const int maximumDetailsHeight = juce::jmax(90, contentArea.getHeight() - UITheme::Spacing::md - fourPreviewRowsHeight);
    this->detailsEditor.setBounds(contentArea.removeFromTop(juce::jmin(160, maximumDetailsHeight)));
    contentArea.removeFromTop(UITheme::Spacing::md);
    this->previewViewport.setBounds(contentArea);
    this->updatePreviewGridSize();
}
