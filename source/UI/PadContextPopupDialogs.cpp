#include "PadContextPopup.h"
#include "../Engine/ArticulationNames.h"
#include "../Engine/DrumKitEngineMapHelpers.h"
#include "UIHelpers.h"
#include "UITheme.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <unordered_set>

void PadContextPopup::showVelocityResponseEditor(MapLayerRow* layer) {
    if (layer == nullptr) return;
    this->flushMapText();
    this->dismissSourcePicker();
    this->dismissLibraryArticulationEditor();
    this->dismissVelocityResponseEditor();

    int layerNumber = 0;
    for (const auto* candidateLayer : this->layerRows) {
        if (candidateLayer == nullptr || candidateLayer->artName.compareIgnoreCase(layer->artName) != 0) continue;
        ++layerNumber;
        if (candidateLayer == layer) break;
    }
    if (layerNumber < 1) layerNumber = 1;

    juce::String sourcePath = layer->sourcePathHint;
    juce::String sourceDisplay = sourcePath.isNotEmpty() ? sourcePath.replace("/", " / ") : juce::String("No source selected");
    if (layer->selectedSourceIndex >= 0 && layer->selectedSourceIndex < static_cast<int>(this->sourceItems.size())) {
        const auto& source = this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)];
        sourcePath = source.fullPath();
        sourceDisplay = source.kitDisplayName;
        if (source.instCode.isNotEmpty()) sourceDisplay += " / " + source.instCode;
        if (source.artCode.isNotEmpty()) sourceDisplay += " / " + source.artCode;
    }

    this->activeVelocityResponseEditor = std::make_unique<VelocityResponseEditor>(this->engine);
    VelocityResponseEditor::LayerData data;
    data.title = this->slotLabel.getText() + " / " + layer->artName;
    if (layerNumber > 1) data.title += " (map layer " + juce::String(layerNumber) + ")";
    data.title += "\n" + sourceDisplay;
    data.customDefinition = layer->velocityResponse;
    data.libraryInfo = this->engine.getLibraryVelocityResponseInfo(sourcePath);
    this->activeVelocityResponseEditor->setLayerData(std::move(data));
    this->activeVelocityResponseEditor->onResponseChanged = [this, layer](const std::optional<juce::String>& definition, bool commit) {
        layer->velocityResponse = definition;
        this->updateVelocityResponseDisplay(layer);
        this->applyLayerParameterChange(layer, EditableLayerParameter::VelocityResponse);
        if (commit) this->flushMapText();
    };
    this->activeVelocityResponseEditor->onPreview = [this, layer](int velocity) {
        this->engine.playLayerPreview(this->slotId, layer->artName, layer->mappedLayerIndex, static_cast<juce::uint8>(juce::jlimit(1, 127, velocity)));
    };
    this->activeVelocityResponseEditor->onDone = [this] {
        juce::MessageManager::callAsync([this] { this->dismissVelocityResponseEditor(); });
    };

    this->velocityResponseOverlay = std::make_unique<DismissOverlay>();
    this->velocityResponseOverlay->setDarkened(true);
    this->velocityResponseOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissVelocityResponseEditor(); });
    };
    this->addAndMakeVisible(*this->velocityResponseOverlay);
    this->velocityResponseOverlay->toFront(false);
    this->addAndMakeVisible(*this->activeVelocityResponseEditor);
    this->activeVelocityResponseEditor->toFront(true);
    this->layoutVelocityResponseEditor();
}

void PadContextPopup::dismissVelocityResponseEditor() {
    const bool editorWasOpen = this->activeVelocityResponseEditor != nullptr;
    this->flushMapText();
    if (this->velocityResponseOverlay) {
        this->removeChildComponent(this->velocityResponseOverlay.get());
        this->velocityResponseOverlay.reset();
    }
    if (this->activeVelocityResponseEditor) {
        this->removeChildComponent(this->activeVelocityResponseEditor.get());
        this->activeVelocityResponseEditor.reset();
    }
    if (editorWasOpen) this->refreshVelocityResponseChoices();
}

void PadContextPopup::layoutVelocityResponseEditor() {
    if (this->velocityResponseOverlay) this->velocityResponseOverlay->setBounds(this->getLocalBounds());
    if (this->activeVelocityResponseEditor) {
        const int editorWidth = juce::jmin(this->getWidth() - 16, juce::jmax(520, this->getWidth() * 3 / 5));
        const int editorHeight = juce::jmin(this->getHeight() - 16, juce::jmax(260, this->getHeight() * 11 / 20));
        this->activeVelocityResponseEditor->setBounds(this->getLocalBounds().withSizeKeepingCentre(editorWidth, editorHeight));
    }
}

void PadContextPopup::showLibraryArticulationEditor(MapLayerRow* layer) {
    if (layer == nullptr || layer->selectedSourceIndex < 0 || layer->selectedSourceIndex >= static_cast<int>(this->sourceItems.size())) return;
    this->flushMapText();
    const auto& source = this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)];
    if (source.rawArtCode.isEmpty()) return;
    this->dismissSourcePicker();
    this->dismissVelocityResponseEditor();
    this->dismissLibraryArticulationEditor();

    this->activeLibraryArticulationEditor = std::make_unique<LibraryArticulationEditor>(this->engine);
    const auto displayTitle = source.kitDisplayName + " / " + source.instCode + " / " + source.artCode;
    this->activeLibraryArticulationEditor->setLibraryArticulation(source.rawKitCode, source.rawInstCode, source.rawArtCode, displayTitle);
    this->activeLibraryArticulationEditor->onPreview = [this, layer, source](int velocity, int namedVelocity, const juce::String& roundRobin, const juce::String& relativePath, const juce::String& micCode) {
        const auto previewVelocity = static_cast<juce::uint8>(juce::jlimit(1, 127, velocity));
        if (relativePath.isNotEmpty()) {
            if (micCode.isNotEmpty()) {
                this->engine.previewSourceItem(source.rawKitCode, source.rawInstCode, source.rawArtCode, previewVelocity, relativePath, micCode);
                return;
            }
            if (this->engine.playLibraryLogicalHitPreview(this->slotId, layer->artName, source.rawKitCode, source.rawInstCode, source.rawArtCode, namedVelocity, roundRobin, previewVelocity)) return;
            this->engine.previewSourceItem(source.rawKitCode, source.rawInstCode, source.rawArtCode, previewVelocity, relativePath, micCode);
            return;
        }
        this->engine.playPreviewNote(this->slotId, layer->artName, previewVelocity);
    };
    this->activeLibraryArticulationEditor->onDone = [this] {
        juce::MessageManager::callAsync([this] { this->dismissLibraryArticulationEditor(); });
    };

    this->libraryArticulationOverlay = std::make_unique<DismissOverlay>();
    this->libraryArticulationOverlay->setDarkened(true);
    this->libraryArticulationOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissLibraryArticulationEditor(); });
    };
    this->addAndMakeVisible(*this->libraryArticulationOverlay);
    this->libraryArticulationOverlay->toFront(false);
    this->addAndMakeVisible(*this->activeLibraryArticulationEditor);
    this->activeLibraryArticulationEditor->toFront(true);
    this->layoutLibraryArticulationEditor();
}

void PadContextPopup::dismissLibraryArticulationEditor() {
    if (this->libraryArticulationOverlay) {
        this->removeChildComponent(this->libraryArticulationOverlay.get());
        this->libraryArticulationOverlay.reset();
    }
    if (this->activeLibraryArticulationEditor) {
        this->removeChildComponent(this->activeLibraryArticulationEditor.get());
        this->activeLibraryArticulationEditor.reset();
    }
}

void PadContextPopup::layoutLibraryArticulationEditor() {
    if (this->libraryArticulationOverlay) this->libraryArticulationOverlay->setBounds(this->getLocalBounds());
    if (this->activeLibraryArticulationEditor) {
        const int editorWidth = this->getWidth() * 9 / 10;
        const int editorHeight = this->getHeight() * 17 / 20;
        this->activeLibraryArticulationEditor->setBounds(this->getLocalBounds().withSizeKeepingCentre(editorWidth, editorHeight));
    }
}

// Source picker

void PadContextPopup::showSourcePicker(MapLayerRow* layer) {
    this->flushMapText();
    this->dismissLibraryArticulationEditor();
    this->dismissVelocityResponseEditor();
    this->dismissSourcePicker();

    this->activePicker = std::make_unique<SourcePickerPopup>(this->sourceItems, this->engine.getInstrumentCatalog());
    int layerNumber = 0;
    for (const auto* candidateLayer : this->layerRows) {
        if (candidateLayer == nullptr || candidateLayer->artName.compareIgnoreCase(layer->artName) != 0) continue;
        ++layerNumber;
        if (candidateLayer == layer) break;
    }
    if (layerNumber < 1) layerNumber = 1;
    auto pickerTitle = this->slotLabel.getText() + " / " + layer->artName;
    if (layerNumber > 1) pickerTitle += " (map layer " + juce::String(layerNumber) + ")";
    this->activePicker->setTitle(pickerTitle);
    this->activePicker->onDismiss = [this] {
        juce::MessageManager::callAsync([this] { this->dismissSourcePicker(); });
    };
    auto initialFilterType = this->slotType;
    if (layer->selectedSourceIndex >= 0 && layer->selectedSourceIndex < static_cast<int>(this->sourceItems.size())) {
        const auto assignedType = this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)].instType;
        if (assignedType != KitLib::InstrumentSpec::Type::Unknown) initialFilterType = assignedType;
    }
    this->activePicker->setFilterType(initialFilterType);

    this->activePicker->onPreview = [this](int sourceIndex, juce::uint8 velocity) {
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(this->sourceItems.size())) return;
        const auto& item = this->sourceItems[static_cast<size_t>(sourceIndex)];
        this->engine.previewSourceItem(item.kitCodeRaw, item.instCode, item.artCode, velocity);
    };

    this->activePicker->onItemSelected = [this, layer](int sourceIndex) {
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(this->sourceItems.size())) return;
        const auto& selectedItem = this->sourceItems[static_cast<size_t>(sourceIndex)];
        layer->selectedSourceIndex = sourceIndex;
        layer->sourcePathHint = selectedItem.fullPath();
        this->updateSourceDisplay(layer);
        this->updateSourceCardButtonVisibility();
        this->rebuildBleedControls(layer);
        this->resized();
        juce::MessageManager::callAsync([this, layer] {
            this->dismissSourcePicker();
            this->applyToEngine();
            this->engine.playPreviewNote(this->slotId, layer->artName, this->getPreviewVelocity());
        });
    };
    this->pickerOverlay = std::make_unique<DismissOverlay>();
    this->pickerOverlay->setDarkened(true);
    this->pickerOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissSourcePicker(); });
    };
    this->addAndMakeVisible(*this->pickerOverlay);
    this->pickerOverlay->toFront(false);

    this->addAndMakeVisible(*this->activePicker);
    this->activePicker->toFront(true);
    this->layoutSourcePicker();
    this->activePicker->setSelectedItem(layer->selectedSourceIndex);
    this->activePicker->focusItemList();
}

void PadContextPopup::showInstrumentPicker() {
    this->flushMapText();
    this->dismissLibraryArticulationEditor();
    this->dismissVelocityResponseEditor();
    this->dismissSourcePicker();

    this->activePicker = std::make_unique<SourcePickerPopup>(this->sourceItems, this->engine.getInstrumentCatalog(), SourcePickerPopup::SelectionMode::WholeInstrument);
    this->activePicker->setTitle(this->slotLabel.getText());
    this->activePicker->onDismiss = [this] {
        juce::MessageManager::callAsync([this] { this->dismissSourcePicker(); });
    };
    this->activePicker->onPreview = [this](int sourceIndex, juce::uint8 velocity) {
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(this->sourceItems.size())) return;
        const auto& item = this->sourceItems[static_cast<size_t>(sourceIndex)];
        this->engine.previewSourceItem(item.kitCodeRaw, item.instCode, item.artCode, velocity);
    };
    this->activePicker->onInstrumentSelected = [this](const SourcePickerPopup::InstrumentSelection& selection) {
        juce::MessageManager::callAsync([this, selection] {
            const auto currentMapText = this->savedInstrumentMapText.isNotEmpty() ? this->savedInstrumentMapText : this->engine.getSuggestedInstrumentMapText();
            auto updatedMap = this->engine.buildInstrumentMapWithSlotInstrument(currentMapText, this->slotId, selection.kitId, selection.instrumentId);
            if (!updatedMap.ok()) {
                if (this->onStatusMessage) this->onStatusMessage(updatedMap.error());
                return;
            }
            const bool mapChanged = updatedMap.value != this->engine.normalizeInstrumentMapText(currentMapText);
            if (mapChanged && !this->engine.applyInstrumentMapSlotChange(updatedMap.value, this->slotId)) {
                if (this->onStatusMessage) this->onStatusMessage(this->engine.getLastInstrumentMapStatus());
                return;
            }

            this->savedInstrumentMapText = updatedMap.value;
            this->mapTextDirty = false;
            this->dismissSourcePicker();
            this->rebuildMapLayerRows();
            if (this->onMapTextChanged) this->onMapTextChanged(this->savedInstrumentMapText);
            if (this->onInstrumentChanged) this->onInstrumentChanged();
            if (this->onStatusMessage) this->onStatusMessage(updatedMap.errors.empty() ? "Instrument replaced in " + this->slotId : updatedMap.error());
            this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
        });
    };

    this->pickerOverlay = std::make_unique<DismissOverlay>();
    this->pickerOverlay->setDarkened(true);
    this->pickerOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissSourcePicker(); });
    };
    this->addAndMakeVisible(*this->pickerOverlay);
    this->pickerOverlay->toFront(false);
    this->addAndMakeVisible(*this->activePicker);
    this->activePicker->toFront(true);
    this->layoutSourcePicker();

    int initialSourceIndex = -1;
    for (const auto* layer : this->layerRows) {
        if (layer == nullptr || layer->selectedSourceIndex < 0) continue;
        initialSourceIndex = layer->selectedSourceIndex;
        if (ArticulationNames::articulationCodeMatches(layer->artName, this->artName)) break;
    }
    this->activePicker->setSelectedItem(initialSourceIndex);
    this->activePicker->focusItemList();
}

void PadContextPopup::dismissSourcePicker() {
    if (this->pickerOverlay) {
        this->removeChildComponent(this->pickerOverlay.get());
        this->pickerOverlay.reset();
    }
    if (this->activePicker) {
        this->removeChildComponent(this->activePicker.get());
        this->activePicker.reset();
    }
}

void PadContextPopup::layoutSourcePicker() {
    if (this->pickerOverlay) this->pickerOverlay->setBounds(this->getLocalBounds());
    if (this->activePicker) {
        const int pickerWidth = this->getWidth() * 4 / 5;
        const int pickerHeight = this->getHeight() * 4 / 5;
        this->activePicker->setBounds(this->getLocalBounds().withSizeKeepingCentre(pickerWidth, pickerHeight));
    }
}

bool PadContextPopup::dismissActiveDialog() {
    if (this->activePicker) {
        this->dismissSourcePicker();
        return true;
    }
    if (this->activeVelocityResponseEditor) {
        this->dismissVelocityResponseEditor();
        return true;
    }
    if (this->activeLibraryArticulationEditor) {
        this->dismissLibraryArticulationEditor();
        return true;
    }
    return false;
}

// Paint / Layout
