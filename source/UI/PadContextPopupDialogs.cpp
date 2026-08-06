#include "PadContextPopup.h"
#include "../Engine/ArticulationNames.h"
#include "UITheme.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <unordered_set>

void PadContextPopup::showVelocityResponseEditor(MapLayerRow* layer) {
    if (layer == nullptr) return;
    this->flushMapText();
    this->dismissLibraryAssignmentPicker();
    this->dismissLibraryArticulationEditor();
    this->dismissVelocityResponseEditor();

    int layerNumber = 0;
    for (const auto* candidateLayer : this->layerRows) {
        if (candidateLayer == nullptr || candidateLayer->artName.compareIgnoreCase(layer->artName) != 0) continue;
        ++layerNumber;
        if (candidateLayer == layer) break;
    }
    if (layerNumber < 1) layerNumber = 1;

    juce::String qualifiedLibraryArticulationId = layer->mappedLibraryArticulationId;
    juce::String libArtDisplay = qualifiedLibraryArticulationId.isNotEmpty() ? qualifiedLibraryArticulationId.replace("/", " / ") : juce::String("No library articulation selected");
    if (layer->selectedLibraryArticulationIndex >= 0 && layer->selectedLibraryArticulationIndex < static_cast<int>(this->availableLibraryArticulations.size())) {
        const auto& libArt = this->availableLibraryArticulations[static_cast<size_t>(layer->selectedLibraryArticulationIndex)];
        qualifiedLibraryArticulationId = libArt.qualifiedId();
        libArtDisplay = libArt.kitDisplayName;
        if (libArt.instCode.isNotEmpty()) libArtDisplay += " / " + libArt.instCode;
        if (libArt.artCode.isNotEmpty()) libArtDisplay += " / " + libArt.artCode;
    }

    this->activeVelocityResponseEditor = std::make_unique<VelocityResponseEditor>(this->engine);
    VelocityResponseEditor::LayerData data;
    data.title = this->slotLabel.getText() + " / " + layer->artName;
    if (layerNumber > 1) data.title += " (map layer " + juce::String(layerNumber) + ")";
    data.title += "\n" + libArtDisplay;
    data.customDefinition = layer->velocityResponse;
    data.libraryInfo = this->engine.getLibraryVelocityResponseInfo(qualifiedLibraryArticulationId);
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
    if (layer == nullptr || layer->selectedLibraryArticulationIndex < 0 || layer->selectedLibraryArticulationIndex >= static_cast<int>(this->availableLibraryArticulations.size())) return;
    this->flushMapText();
    const auto& libArt = this->availableLibraryArticulations[static_cast<size_t>(layer->selectedLibraryArticulationIndex)];
    if (libArt.rawArtCode.isEmpty()) return;
    this->dismissLibraryAssignmentPicker();
    this->dismissVelocityResponseEditor();
    this->dismissLibraryArticulationEditor();

    this->activeLibraryArticulationEditor = std::make_unique<LibraryArticulationEditor>(this->engine);
    const auto displayTitle = libArt.kitDisplayName + " / " + libArt.instCode + " / " + libArt.artCode;
    this->activeLibraryArticulationEditor->setLibraryArticulation(libArt.rawKitCode, libArt.rawInstCode, libArt.rawArtCode, displayTitle);
    this->activeLibraryArticulationEditor->onPreview = [this, layer, libArt](int velocity, int namedVelocity, const juce::String& roundRobin, const juce::String& relativePath, const juce::String& micCode) {
        const auto previewVelocity = static_cast<juce::uint8>(juce::jlimit(1, 127, velocity));
        if (relativePath.isNotEmpty()) {
            if (micCode.isNotEmpty()) {
                this->engine.previewLibraryArticulation(libArt.rawKitCode, libArt.rawInstCode, libArt.rawArtCode, previewVelocity, relativePath, micCode);
                return;
            }
            if (this->engine.playLibraryLogicalHitPreview(this->slotId, layer->artName, libArt.rawKitCode, libArt.rawInstCode, libArt.rawArtCode, namedVelocity, roundRobin, previewVelocity)) return;
            this->engine.previewLibraryArticulation(libArt.rawKitCode, libArt.rawInstCode, libArt.rawArtCode, previewVelocity, relativePath, micCode);
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

// Library assignment picker

void PadContextPopup::showLibraryArticulationPicker(MapLayerRow* layer) {
    this->flushMapText();
    this->dismissLibraryArticulationEditor();
    this->dismissVelocityResponseEditor();
    this->dismissLibraryAssignmentPicker();

    this->activeLibraryAssignmentPicker = std::make_unique<LibraryAssignmentPicker>(this->availableLibraryArticulations, this->engine.getInstrumentCatalog(), this->engine.getArticulationSlotFallbackRules());
    int layerNumber = 0;
    for (const auto* candidateLayer : this->layerRows) {
        if (candidateLayer == nullptr || candidateLayer->artName.compareIgnoreCase(layer->artName) != 0) continue;
        ++layerNumber;
        if (candidateLayer == layer) break;
    }
    if (layerNumber < 1) layerNumber = 1;
    auto pickerTitle = this->slotLabel.getText() + " / " + layer->artName;
    if (layerNumber > 1) pickerTitle += " (map layer " + juce::String(layerNumber) + ")";
    this->activeLibraryAssignmentPicker->setTitle(pickerTitle);
    this->activeLibraryAssignmentPicker->onDismiss = [this] {
        juce::MessageManager::callAsync([this] { this->dismissLibraryAssignmentPicker(); });
    };
    auto initialFilterType = this->slotType;
    if (layer->selectedLibraryArticulationIndex >= 0 && layer->selectedLibraryArticulationIndex < static_cast<int>(this->availableLibraryArticulations.size())) {
        const auto assignedType = this->availableLibraryArticulations[static_cast<size_t>(layer->selectedLibraryArticulationIndex)].instType;
        if (assignedType != KitLib::InstrumentSpec::Type::Unknown) initialFilterType = assignedType;
    }
    this->activeLibraryAssignmentPicker->setFilterType(initialFilterType);

    this->activeLibraryAssignmentPicker->onLibraryArticulationPreview = [this](int libArtIndex, juce::uint8 velocity) {
        if (libArtIndex < 0 || libArtIndex >= static_cast<int>(this->availableLibraryArticulations.size())) return;
        const auto& libArt = this->availableLibraryArticulations[static_cast<size_t>(libArtIndex)];
        this->engine.previewLibraryArticulation(libArt.kitCodeRaw, libArt.instCode, libArt.artCode, velocity);
    };

    this->activeLibraryAssignmentPicker->onLibraryArticulationSelected = [this, layer](int libArtIndex) {
        if (libArtIndex < 0 || libArtIndex >= static_cast<int>(this->availableLibraryArticulations.size())) return;
        const auto& selectedLibArt = this->availableLibraryArticulations[static_cast<size_t>(libArtIndex)];
        layer->selectedLibraryArticulationIndex = libArtIndex;
        layer->mappedLibraryArticulationId = selectedLibArt.qualifiedId();
        this->updateLibraryArticulationDisplay(layer);
        this->updateLibraryArticulationActionsVisibility();
        this->rebuildBleedControls(layer);
        this->resized();
        juce::MessageManager::callAsync([this, layer] {
            this->dismissLibraryAssignmentPicker();
            this->applyToEngine();
            this->engine.playPreviewNote(this->slotId, layer->artName, this->getPreviewVelocity());
        });
    };
    this->libraryAssignmentOverlay = std::make_unique<DismissOverlay>();
    this->libraryAssignmentOverlay->setDarkened(true);
    this->libraryAssignmentOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissLibraryAssignmentPicker(); });
    };
    this->addAndMakeVisible(*this->libraryAssignmentOverlay);
    this->libraryAssignmentOverlay->toFront(false);

    this->addAndMakeVisible(*this->activeLibraryAssignmentPicker);
    this->activeLibraryAssignmentPicker->toFront(true);
    this->layoutLibraryAssignmentPicker();
    this->activeLibraryAssignmentPicker->setSelectedLibraryArticulation(layer->selectedLibraryArticulationIndex);
    this->activeLibraryAssignmentPicker->focusAssignmentList();
}

void PadContextPopup::showInstrumentPicker() {
    this->flushMapText();
    this->dismissLibraryArticulationEditor();
    this->dismissVelocityResponseEditor();
    this->dismissLibraryAssignmentPicker();

    this->activeLibraryAssignmentPicker = std::make_unique<LibraryAssignmentPicker>(this->availableLibraryArticulations, this->engine.getInstrumentCatalog(), this->engine.getArticulationSlotFallbackRules(), LibraryAssignmentPicker::SelectionMode::Instrument);
    this->activeLibraryAssignmentPicker->setTitle(this->slotLabel.getText());
    this->activeLibraryAssignmentPicker->onDismiss = [this] {
        juce::MessageManager::callAsync([this] { this->dismissLibraryAssignmentPicker(); });
    };
    this->activeLibraryAssignmentPicker->onLibraryArticulationPreview = [this](int libArtIndex, juce::uint8 velocity) {
        if (libArtIndex < 0 || libArtIndex >= static_cast<int>(this->availableLibraryArticulations.size())) return;
        const auto& libArt = this->availableLibraryArticulations[static_cast<size_t>(libArtIndex)];
        this->engine.previewLibraryArticulation(libArt.kitCodeRaw, libArt.instCode, libArt.artCode, velocity);
    };
    this->activeLibraryAssignmentPicker->onLibraryInstrumentSelected = [this](const LibraryAssignmentPicker::LibraryInstrumentSelection& selection) {
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
            this->dismissLibraryAssignmentPicker();
            this->rebuildMapLayerRows();
            if (this->onMapTextChanged) this->onMapTextChanged(this->savedInstrumentMapText);
            if (this->onInstrumentChanged) this->onInstrumentChanged();
            if (this->onStatusMessage) this->onStatusMessage(updatedMap.errors.empty() ? "Instrument replaced in " + this->slotId : updatedMap.error());
            this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
        });
    };

    this->libraryAssignmentOverlay = std::make_unique<DismissOverlay>();
    this->libraryAssignmentOverlay->setDarkened(true);
    this->libraryAssignmentOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissLibraryAssignmentPicker(); });
    };
    this->addAndMakeVisible(*this->libraryAssignmentOverlay);
    this->libraryAssignmentOverlay->toFront(false);
    this->addAndMakeVisible(*this->activeLibraryAssignmentPicker);
    this->activeLibraryAssignmentPicker->toFront(true);
    this->layoutLibraryAssignmentPicker();

    int initialLibArtIndex = -1;
    for (const auto* layer : this->layerRows) {
        if (layer == nullptr || layer->selectedLibraryArticulationIndex < 0) continue;
        initialLibArtIndex = layer->selectedLibraryArticulationIndex;
        if (ArticulationNames::articulationCodeMatches(layer->artName, this->artName)) break;
    }
    this->activeLibraryAssignmentPicker->setSelectedLibraryArticulation(initialLibArtIndex);
    this->activeLibraryAssignmentPicker->focusAssignmentList();
}

void PadContextPopup::dismissLibraryAssignmentPicker() {
    if (this->libraryAssignmentOverlay) {
        this->removeChildComponent(this->libraryAssignmentOverlay.get());
        this->libraryAssignmentOverlay.reset();
    }
    if (this->activeLibraryAssignmentPicker) {
        this->removeChildComponent(this->activeLibraryAssignmentPicker.get());
        this->activeLibraryAssignmentPicker.reset();
    }
}

void PadContextPopup::layoutLibraryAssignmentPicker() {
    if (this->libraryAssignmentOverlay) this->libraryAssignmentOverlay->setBounds(this->getLocalBounds());
    if (this->activeLibraryAssignmentPicker) {
        const int pickerWidth = this->getWidth() * 4 / 5;
        const int pickerHeight = this->getHeight() * 4 / 5;
        this->activeLibraryAssignmentPicker->setBounds(this->getLocalBounds().withSizeKeepingCentre(pickerWidth, pickerHeight));
    }
}

bool PadContextPopup::dismissActiveDialog() {
    if (this->activeLibraryAssignmentPicker) {
        this->dismissLibraryAssignmentPicker();
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
