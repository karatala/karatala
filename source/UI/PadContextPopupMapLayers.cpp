#include "PadContextPopup.h"
#include "../Engine/ArticulationNames.h"
#include "../Engine/InstrumentMap/InstrumentMapOperations.h"
#include "UIComponentStyle.h"
#include "UITheme.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <unordered_set>

void PadContextPopup::populateLayers() {
    auto mapText = this->savedInstrumentMapText.isNotEmpty() ? this->savedInstrumentMapText : this->engine.getSuggestedInstrumentMapText();
    auto parseResult = DrumKitEngine::parseInstrumentMapText(mapText);
    const auto& existingMapLayers = parseResult.value.layers;

    for (const auto* group : this->articulationGroups) {
        int mappedLayerIndex = 0;
        for (const auto& mapLayer : existingMapLayers) {
            if (mapLayer.slotId != this->slotId) continue;

            const auto& articulationSlotFilters = mapLayer.slotArticulationFilter;
            bool matchesArticulation = articulationSlotFilters.empty();
            for (const auto& filter : articulationSlotFilters) {
                if (ArticulationNames::mapFilterMatchesArticulation(filter, group->artName)) {
                    matchesArticulation = true;
                    break;
                }
            }
            if (!matchesArticulation) continue;

            juce::String qualifiedLibraryArticulationId = mapLayer.kitRef;
            if (mapLayer.instCode.isNotEmpty()) qualifiedLibraryArticulationId += "/" + mapLayer.instCode;
            const auto libArtId = InstrumentMap::libraryArticulationForSlot(mapLayer, group->artName);
            if (libArtId.isNotEmpty()) qualifiedLibraryArticulationId += "/" + libArtId;
            const auto* libraryManager = this->engine.getInstrumentManager();
            const auto* libraryKit = libraryManager != nullptr ? libraryManager->findKitById(mapLayer.kitRef) : nullptr;
            if (libraryKit != nullptr && this->slotType == KitLib::InstrumentSpec::Type::HiHat && InstrumentMap::usesLegacyArticulationSlotExpressionFilter(mapLayer.slotArticulationFilter, group->artName))
                qualifiedLibraryArticulationId = InstrumentMap::resolveLegacyHihatLibraryArticulationId(*libraryKit, qualifiedLibraryArticulationId, group->artName);

            const int tuning = mapLayer.tuningCents.value_or(0);
            const int attack = mapLayer.attackTimeMs.value_or(-1);
            const int decay = mapLayer.decayTimeMs.value_or(-1);
            const float volume = mapLayer.volumeDb.value_or(0.0f);
            this->addMapLayerRow(group->artName, qualifiedLibraryArticulationId, tuning, attack, decay, volume, mapLayer.pan.value_or(0), mapLayer.flipLR, mapLayer.flipLRExplicit, mapLayer.flipPhase, mapLayer.velocityResponse, mapLayer.micBleedLevels, mapLayer.micRouteOverrides, mappedLayerIndex);
            ++mappedLayerIndex;
        }
    }

    this->updateLibraryArticulationActionsVisibility();
}


PadContextPopup::MapLayerRow* PadContextPopup::addMapLayerRow(const juce::String& articulationName, const juce::String& qualifiedLibraryArticulationId, int tuningCents, int attackMs, int decayMs, float volumeDb, int pan, bool flipLR, bool flipLRExplicit, bool flipPhase, const std::optional<juce::String>& velocityResponse, const std::unordered_map<juce::String, float>& micBleedLevels, const std::unordered_map<juce::String, juce::String>& micRouteOverrides, int mappedLayerIndex) { //TODO: will it be better to replace multiple params for this with one structure?
    auto* layer = this->layerRows.add(new MapLayerRow());
    layer->artName = articulationName;
    layer->mappedLibraryArticulationId = qualifiedLibraryArticulationId;
    layer->selectedLibraryArticulationIndex = AvailableLibraryArticulation::findIndexByQualifiedId(this->availableLibraryArticulations, qualifiedLibraryArticulationId);
    layer->mappedLayerIndex = mappedLayerIndex;
    layer->velocityResponse = velocityResponse;

    // Library articulation button
    layer->libraryArticulationButton = std::make_unique<juce::TextButton>();
    layer->libraryArticulationButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControlAlt);
    layer->libraryArticulationButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    this->layerContent.addAndMakeVisible(*layer->libraryArticulationButton);
    layer->libraryArticulationButton->onClick = [this, layer] { this->showLibraryArticulationPicker(layer); };

    // Library articulation label (kit / instrument / articulation)
    layer->libraryArticulationDetailLabel = std::make_unique<juce::Label>();
    layer->libraryArticulationDetailLabel->setFont(UITheme::Fonts::normal());
    layer->libraryArticulationDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
    layer->libraryArticulationDetailLabel->setJustificationType(juce::Justification::topLeft);
    layer->libraryArticulationDetailLabel->setInterceptsMouseClicks(false, false);
    this->layerContent.addAndMakeVisible(*layer->libraryArticulationDetailLabel);

    layer->velocityResponseMode = std::make_unique<juce::ComboBox>();
    UIComponentStyle::configureComboBox(*layer->velocityResponseMode);
    layer->velocityResponseMode->getProperties().set("compactRoute", true);
    layer->velocityResponseMode->getProperties().set("hideArrow", true);
    this->layerContent.addAndMakeVisible(*layer->velocityResponseMode);
    layer->velocityResponseMode->onChange = [this, layer] { this->commitVelocityResponse(layer); };
    this->rebuildVelocityResponseChoices(layer);

    layer->libraryArticulationEditorButton = std::make_unique<juce::TextButton>();
    UIComponentStyle::configureSettingsButton(*layer->libraryArticulationEditorButton);
    layer->libraryArticulationEditorButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::transparent);
    layer->libraryArticulationEditorButton->setColour(juce::TextButton::textColourOffId, UITheme::Colours::textSecondary);
    layer->libraryArticulationEditorButton->setTooltip("Edit library kit corrections for velocity ranges, balance, gain, and timing of this library articulation");
    layer->libraryArticulationEditorButton->onClick = [this, layer] { this->showLibraryArticulationEditor(layer); };
    this->layerContent.addAndMakeVisible(*layer->libraryArticulationEditorButton);

    this->updateLibraryArticulationDisplay(layer);

    // Tuning pot
    layer->tuningPot = std::make_unique<CompactParameterControl>("Tuning", -2400.0, 2400.0, 1.0, static_cast<double>(tuningCents));
    layer->tuningPot->setInteraction(0.0, 10.0, 1.0);
    this->layerContent.addAndMakeVisible(*layer->tuningPot);
    layer->tuningPot->onValueChange = [this, layer] { this->applyLayerParameterChange(layer, EditableLayerParameter::Tuning); };

    // Attack pot
    layer->attackPot = std::make_unique<CompactParameterControl>("Attack", -1.0, 500.0, 1.0, attackMs >= 0 ? static_cast<double>(attackMs) : -1.0, CompactParameterControl::sentinelValueText);
    layer->attackPot->setInteraction(-1.0, 10.0, 1.0);
    this->layerContent.addAndMakeVisible(*layer->attackPot);
    layer->attackPot->onValueChange = [this, layer] { this->applyLayerParameterChange(layer, EditableLayerParameter::Attack); };

    // Decay pot
    layer->decayPot = std::make_unique<CompactParameterControl>("Decay", -1.0, 5000.0, 1.0, decayMs >= 0 ? static_cast<double>(decayMs) : -1.0, CompactParameterControl::sentinelValueText);
    layer->decayPot->setInteraction(-1.0, 10.0, 1.0);
    this->layerContent.addAndMakeVisible(*layer->decayPot);
    layer->decayPot->onValueChange = [this, layer] { this->applyLayerParameterChange(layer, EditableLayerParameter::Decay); };

    // Gain pot (-60 to +12 dB)
    layer->gainPot = std::make_unique<CompactParameterControl>("Gain", -60.0, 12.0, 0.1, static_cast<double>(volumeDb),
        [](double value) -> juce::String {
            if (value <= -60.0) return "-inf";
            return juce::String(value, 1);
        });
    layer->gainPot->setInteraction(0.0, 0.5, 0.1);
    this->layerContent.addAndMakeVisible(*layer->gainPot);
    layer->gainPot->onValueChange = [this, layer] { this->applyLayerParameterChange(layer, EditableLayerParameter::Gain); };

    // Pan pot
    layer->panPot = std::make_unique<CompactParameterControl>("Pan", -127.0, 127.0, 1.0, static_cast<double>(pan), [](double value) -> juce::String {
        const int panValue = static_cast<int>(value);
        if (panValue == 0) return "C";
        return panValue < 0 ? "L" + juce::String(-panValue) : "R" + juce::String(panValue);
    });
    layer->panPot->setInteraction(0.0, 4.0, 1.0);
    this->layerContent.addAndMakeVisible(*layer->panPot);
    layer->panPot->onValueChange = [this, layer] { this->applyLayerParameterChange(layer, EditableLayerParameter::Pan); };

    // Flip L/R button (toggle)
    layer->flipLRButton = std::make_unique<juce::TextButton>("L/R");
    UIComponentStyle::configureToggleButton(*layer->flipLRButton,
                                     juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    layer->flipLRButton->setToggleState(flipLR, juce::dontSendNotification);
    layer->flipLRExplicit = flipLRExplicit;
    this->layerContent.addAndMakeVisible(*layer->flipLRButton);
    layer->flipLRButton->onClick = [this, layer] {
        layer->flipLRExplicit = true;
        this->applyLayerParameterChange(layer, EditableLayerParameter::FlipLR);
    };

    // Flip Phase button (toggle)
    layer->flipPhaseButton = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xc3\x98")); // phase invert
    UIComponentStyle::configureToggleButton(*layer->flipPhaseButton, juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    layer->flipPhaseButton->setToggleState(flipPhase, juce::dontSendNotification);
    this->layerContent.addAndMakeVisible(*layer->flipPhaseButton);
    layer->flipPhaseButton->onClick = [this, layer] {
        this->applyLayerParameterChange(layer, EditableLayerParameter::FlipPhase);
    };

    // Per-mic bleed controls
    this->rebuildBleedControls(layer, micBleedLevels, micRouteOverrides);

    // Remove button
    layer->removeButton = std::make_unique<juce::TextButton>();
    UIComponentStyle::configureDialogCloseButton(*layer->removeButton);
    layer->removeButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::transparent);
    layer->removeButton->setColour(juce::TextButton::textColourOffId, UITheme::Colours::accent);
    layer->removeButton->setTooltip("Remove layer");
    this->layerContent.addAndMakeVisible(*layer->removeButton);
    layer->libraryArticulationButton->onStateChange = [this] { this->updateLibraryArticulationActionsVisibility(); };
    layer->libraryArticulationEditorButton->onStateChange = [this] { this->updateLibraryArticulationActionsVisibility(); };
    layer->removeButton->onStateChange = [this] { this->updateLibraryArticulationActionsVisibility(); };
    layer->removeButton->onClick = [this, layer] {
        juce::MessageManager::callAsync([this, layer] {
            this->flushMapText();
            const bool removedMappedLayer = layer->selectedLibraryArticulationIndex >= 0;
            this->layerRows.removeObject(layer);
            this->updateLibraryArticulationActionsVisibility();
            this->resized();
            this->refreshLayerMappings();
            if (removedMappedLayer) this->applyToEngine();
        });
    };
    return layer;
}

void PadContextPopup::updateLibraryArticulationActionsVisibility() {
    for (auto* layer : this->layerRows) {
        int articulationLayerCount = 0;
        for (const auto* candidate : this->layerRows)
            if (candidate->artName == layer->artName) ++articulationLayerCount;
        const bool canRemove = articulationLayerCount > 1;
        const bool canOpenLibraryArticulationEditor = layer->libraryArticulationEditorButton->isEnabled();
        const bool libraryArticulationHovered = layer->libraryArticulationButton->isMouseOver() || layer->libraryArticulationEditorButton->isMouseOver() || layer->removeButton->isMouseOver();
        layer->removeButton->setVisible(canRemove);
        layer->removeButton->setAlpha(canRemove && libraryArticulationHovered ? 1.0f : 0.0f);
        layer->libraryArticulationEditorButton->setVisible(canOpenLibraryArticulationEditor);
        layer->libraryArticulationEditorButton->setAlpha(canOpenLibraryArticulationEditor && libraryArticulationHovered ? 1.0f : 0.0f);
    }
}

void PadContextPopup::updateLibraryArticulationDisplay(MapLayerRow* layer) {
    if (layer->selectedLibraryArticulationIndex >= 0) {
        const auto& src = this->availableLibraryArticulations[static_cast<size_t>(layer->selectedLibraryArticulationIndex)];
        const auto libraryArticulation = src.artCode.isNotEmpty() ? src.artCode : layer->artName;
        const auto detail = src.kitDisplayName + "\n" + src.instCode + "\n" + libraryArticulation;
        layer->libraryArticulationDetailLabel->setText(detail, juce::dontSendNotification);
        layer->libraryArticulationDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
        layer->libraryArticulationButton->setTooltip(src.qualifiedId());
    } else if (layer->mappedLibraryArticulationId.isNotEmpty()) {
        layer->libraryArticulationDetailLabel->setText("Missing library articulation\n" + layer->mappedLibraryArticulationId, juce::dontSendNotification);
        layer->libraryArticulationDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::accentWarning);
        layer->libraryArticulationButton->setTooltip(layer->mappedLibraryArticulationId);
    } else {
        layer->libraryArticulationDetailLabel->setText("Select library articulation", juce::dontSendNotification);
        layer->libraryArticulationDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::textMuted);
        layer->libraryArticulationButton->setTooltip({});
    }
    layer->libraryArticulationButton->setButtonText("");
    if (layer->libraryArticulationEditorButton != nullptr) layer->libraryArticulationEditorButton->setEnabled(layer->selectedLibraryArticulationIndex >= 0 && this->availableLibraryArticulations[static_cast<size_t>(layer->selectedLibraryArticulationIndex)].rawArtCode.isNotEmpty());
}

int PadContextPopup::getLayerIndex(const MapLayerRow* layer) const {
    if (layer == nullptr) return -1;
    if (layer->selectedLibraryArticulationIndex < 0) return -1;

    int mappedLayerIndex = 0;
    for (int idx = 0; idx < this->layerRows.size(); ++idx) {
        const auto* currentLayer = this->layerRows[idx];
        if (currentLayer->artName != layer->artName) continue;
        if (currentLayer->selectedLibraryArticulationIndex < 0) continue;
        if (currentLayer == layer) return mappedLayerIndex;
        ++mappedLayerIndex;
    }

    return -1;
}

int PadContextPopup::findVelocityResponsePresetIndex(const std::optional<juce::String>& definition) const {
    if (!definition.has_value()) return -1;
    for (int presetIdx = 0; presetIdx < static_cast<int>(this->velocityResponsePresets.size()); ++presetIdx) {
        if (this->velocityResponsePresets[static_cast<size_t>(presetIdx)].definition == *definition) return presetIdx;
    }
    return -1;
}

void PadContextPopup::rebuildVelocityResponseChoices(MapLayerRow* layer) {
    if (layer == nullptr || layer->velocityResponseMode == nullptr) return;

    layer->velocityResponseMode->clear(juce::dontSendNotification);
    layer->velocityResponseMode->addItem("Global Vel-to-dB curve", velocityResponseGlobalItemId);
    layer->velocityResponseMode->addSeparator();
    for (int presetIdx = 0; presetIdx < static_cast<int>(this->velocityResponsePresets.size()); ++presetIdx) {
        const auto& preset = this->velocityResponsePresets[static_cast<size_t>(presetIdx)];
        layer->velocityResponseMode->addItem(preset.name + (preset.isUser ? "" : " (Built-in)"), velocityResponsePresetBaseItemId + presetIdx);
    }
    layer->velocityResponseMode->addSeparator();
    layer->velocityResponseMode->addItem("Edit curve...", velocityResponseEditItemId);
    this->updateVelocityResponseDisplay(layer);
}

void PadContextPopup::refreshVelocityResponseChoices() {
    this->velocityResponsePresets = this->engine.getVelocityResponsePresets();
    for (auto* layer : this->layerRows) this->rebuildVelocityResponseChoices(layer);
}

void PadContextPopup::commitVelocityResponse(MapLayerRow* layer) {
    if (layer == nullptr || layer->velocityResponseMode == nullptr) return;
    const int selectedMode = layer->velocityResponseMode->getSelectedId();
    if (selectedMode == velocityResponseEditItemId) {
        this->updateVelocityResponseDisplay(layer);
        this->showVelocityResponseEditor(layer);
        return;
    }
    if (selectedMode == velocityResponseGlobalItemId) {
        layer->velocityResponse.reset();
    } else {
        const int presetIdx = selectedMode - velocityResponsePresetBaseItemId;
        if (presetIdx < 0 || presetIdx >= static_cast<int>(this->velocityResponsePresets.size())) {
            this->updateVelocityResponseDisplay(layer);
            return;
        }
        layer->velocityResponse = this->velocityResponsePresets[static_cast<size_t>(presetIdx)].definition;
    }
    this->updateVelocityResponseDisplay(layer);
    this->applyLayerParameterChange(layer, EditableLayerParameter::VelocityResponse);
    this->flushMapText();
    this->resized();
}

void PadContextPopup::updateVelocityResponseDisplay(MapLayerRow* layer) {
    if (layer == nullptr || layer->velocityResponseMode == nullptr) return;
    const bool layerOverride = layer->velocityResponse.has_value();
    int selectedItemId = velocityResponseGlobalItemId;
    bool unnamedLayerCurve = false;
    juce::String tooltip = "Uses the Global Velocity-to-dB Curve selected in Conf: " + this->engine.getVelocityResponse().getDefinition();
    if (layerOverride) {
        const int presetIdx = this->findVelocityResponsePresetIndex(layer->velocityResponse);
        if (presetIdx >= 0) {
            const auto& preset = this->velocityResponsePresets[static_cast<size_t>(presetIdx)];
            selectedItemId = velocityResponsePresetBaseItemId + presetIdx;
            tooltip = preset.name + ": " + preset.definition;
        } else {
            unnamedLayerCurve = true;
            tooltip = "Saved with this layer, but not saved as a reusable preset: " + *layer->velocityResponse;
        }
    }
    if (unnamedLayerCurve) {
        layer->velocityResponseMode->setSelectedId(0, juce::dontSendNotification);
        layer->velocityResponseMode->setText("Edited Map Layer Curve*", juce::dontSendNotification);
    } else {
        layer->velocityResponseMode->setSelectedId(selectedItemId, juce::dontSendNotification);
    }
    layer->velocityResponseMode->setTooltip(tooltip);
    layer->velocityResponseMode->setColour(juce::ComboBox::textColourId, layerOverride ? UITheme::Colours::textAccent : UITheme::Colours::textSecondary);
}

void PadContextPopup::applyLayerParameterChange(MapLayerRow* layer, EditableLayerParameter parameter, const juce::String& micCode) {
    if (this->suppressParameterCallbacks || layer == nullptr) return;
    if (layer->mappedLayerIndex < 0 || layer->selectedLibraryArticulationIndex < 0) {
        this->mapTextDirty = true;
        return;
    }

    DrumKitEngine::InstrumentMap workingMap;
    if (this->pendingInstrumentMap.has_value()) {
        workingMap = *this->pendingInstrumentMap;
    } else {
        const auto currentMapText = this->savedInstrumentMapText.isNotEmpty() ? this->savedInstrumentMapText : this->engine.getSuggestedInstrumentMapText();
        auto parsedMap = DrumKitEngine::parseInstrumentMapText(currentMapText);
        if (!parsedMap.ok()) return;
        workingMap = std::move(parsedMap.value);
    }
    const auto* layout = this->engine.getKitLayout();
    if (layout == nullptr) return;

    auto projections = InstrumentMap::projectLayers(workingMap, *layout);
    auto findMapLayerProjection = [this, layer](const auto& candidateProjections) -> const InstrumentMap::LayerProjection* {
        for (const auto& projection : candidateProjections) {
            if (projection.slotId.compareIgnoreCase(this->slotId) != 0 || projection.articulationSlotCode.compareIgnoreCase(layer->artName) != 0 || projection.articulationSlotLayerIndex != layer->mappedLayerIndex) continue;
            return &projection;
        }
        return nullptr;
    };

    const auto* mapLayerProjection = findMapLayerProjection(projections);
    if (mapLayerProjection == nullptr) return;

    const bool linkArts = this->linkArtsButton.getToggleState() && parameter != EditableLayerParameter::VelocityResponse;
    const bool linkTomBleed = parameter == EditableLayerParameter::Bleed && this->linkTomBleedButton.isVisible() && this->linkTomBleedButton.getToggleState();
    if (!linkArts && !linkTomBleed) {
        const auto slotIterator = std::find_if(layout->instrumentSlots.begin(), layout->instrumentSlots.end(), [this](const auto& slot) {
            return slot.id.compareIgnoreCase(this->slotId) == 0;
        });
        if (slotIterator != layout->instrumentSlots.end()) {
            InstrumentMap::isolateLayerForArticulationSlot(workingMap, *slotIterator, mapLayerProjection->mapLayerIndex, layer->artName);
            projections = InstrumentMap::projectLayers(workingMap, *layout);
            mapLayerProjection = findMapLayerProjection(projections);
            if (mapLayerProjection == nullptr) return;
        }
    }

    auto targetProjections = InstrumentMap::collectLinkedMapLayerTargets(projections, this->slotId, layer->artName, layer->mappedLayerIndex, linkArts, linkTomBleed);

    int integerValue = 0;
    float floatValue = 0.0f;
    bool boolValue = false;
    float bleedValue = 1.0f;
    DrumKitEngine::LayerParameterUpdate runtimeUpdate;
    switch (parameter) {
        case EditableLayerParameter::Tuning:
            integerValue = static_cast<int>(layer->tuningPot->getValue());
            runtimeUpdate.tuningCents = integerValue;
            break;
        case EditableLayerParameter::Attack:
            integerValue = static_cast<int>(layer->attackPot->getValue());
            runtimeUpdate.attackMs = integerValue;
            break;
        case EditableLayerParameter::Decay:
            integerValue = static_cast<int>(layer->decayPot->getValue());
            runtimeUpdate.decayMs = integerValue;
            break;
        case EditableLayerParameter::Gain:
            floatValue = static_cast<float>(layer->gainPot->getValue());
            runtimeUpdate.volumeDb = floatValue;
            break;
        case EditableLayerParameter::Pan:
            integerValue = static_cast<int>(layer->panPot->getValue());
            runtimeUpdate.pan = integerValue;
            break;
        case EditableLayerParameter::FlipLR:
            boolValue = layer->flipLRButton->getToggleState();
            runtimeUpdate.flipLR = boolValue;
            break;
        case EditableLayerParameter::FlipPhase:
            boolValue = layer->flipPhaseButton->getToggleState();
            runtimeUpdate.flipPhase = boolValue;
            break;
        case EditableLayerParameter::Bleed:
            for (const auto& bleedControl : layer->micBleeds) {
                if (bleedControl == nullptr || bleedControl->micCode.compareIgnoreCase(micCode) != 0) continue;
                bleedValue = static_cast<float>(bleedControl->bleedPot->getValue() / 100.0);
                break;
            }
            runtimeUpdate.micBleedLevel = std::make_pair(micCode, bleedValue);
            break;
        case EditableLayerParameter::VelocityResponse:
            runtimeUpdate.updateVelocityResponse = true;
            if (layer->velocityResponse.has_value()) {
                auto parsedResponse = VelocityResponse::fromDefinition(*layer->velocityResponse);
                if (parsedResponse.ok()) runtimeUpdate.velocityResponse = std::move(parsedResponse.value);
            }
            break;
    }

    if (parameter == EditableLayerParameter::Bleed) {
        std::erase_if(targetProjections, [this, &workingMap, &micCode](const auto& projection) {
            if (projection.mapLayerIndex >= workingMap.layers.size()) return true;
            const auto& mapLayer = workingMap.layers[projection.mapLayerIndex];
            const auto qualifiedLibArtId = InstrumentMap::qualifiedLibraryArticulationIdForSlot(mapLayer, projection.articulationSlotCode);
            const auto relevantMics = this->getRelevantMicsForQualifiedLibraryArticulationId(qualifiedLibArtId);
            return std::none_of(relevantMics.begin(), relevantMics.end(), [&micCode](const auto& mic) {
                return mic.first.compareIgnoreCase(micCode) == 0;
            });
        });
    }
    if (targetProjections.empty()) return;

    std::unordered_set<size_t> updatedMapLayerIndexes;
    for (const auto& projection : targetProjections) {
        if (projection.mapLayerIndex >= workingMap.layers.size() || !updatedMapLayerIndexes.insert(projection.mapLayerIndex).second) continue;
        auto& mapLayer = workingMap.layers[projection.mapLayerIndex];
        switch (parameter) {
            case EditableLayerParameter::Tuning:
                if (integerValue == 0) mapLayer.tuningCents.reset();
                else mapLayer.tuningCents = integerValue;
                break;
            case EditableLayerParameter::Attack:
                if (integerValue < 0) mapLayer.attackTimeMs.reset();
                else mapLayer.attackTimeMs = integerValue;
                break;
            case EditableLayerParameter::Decay:
                if (integerValue < 0) mapLayer.decayTimeMs.reset();
                else mapLayer.decayTimeMs = integerValue;
                break;
            case EditableLayerParameter::Gain:
                if (std::abs(floatValue) <= 0.01f) mapLayer.volumeDb.reset();
                else mapLayer.volumeDb = floatValue;
                break;
            case EditableLayerParameter::Pan:
                if (integerValue == 0) mapLayer.pan.reset();
                else mapLayer.pan = integerValue;
                break;
            case EditableLayerParameter::FlipLR:
                mapLayer.flipLR = boolValue;
                mapLayer.flipLRExplicit = true;
                break;
            case EditableLayerParameter::FlipPhase:
                mapLayer.flipPhase = boolValue;
                break;
            case EditableLayerParameter::Bleed: {
                for (auto levelIterator = mapLayer.micBleedLevels.begin(); levelIterator != mapLayer.micBleedLevels.end();) {
                    if (levelIterator->first.compareIgnoreCase(micCode) == 0) levelIterator = mapLayer.micBleedLevels.erase(levelIterator);
                    else ++levelIterator;
                }
                if (bleedValue < 0.99f) mapLayer.micBleedLevels[micCode] = bleedValue;
                break;
            }
            case EditableLayerParameter::VelocityResponse:
                mapLayer.velocityResponse = layer->velocityResponse;
                break;
        }
    }

    this->suppressParameterCallbacks = true;
    for (const auto& projection : targetProjections) {
        if (projection.slotId.compareIgnoreCase(this->slotId) != 0) continue;
        auto* targetLayer = static_cast<MapLayerRow*>(nullptr);
        for (auto* candidateLayer : this->layerRows) {
            if (candidateLayer != nullptr && candidateLayer->artName.compareIgnoreCase(projection.articulationSlotCode) == 0 && candidateLayer->mappedLayerIndex == projection.articulationSlotLayerIndex) {
                targetLayer = candidateLayer;
                break;
            }
        }
        if (targetLayer == nullptr) continue;
        switch (parameter) {
            case EditableLayerParameter::Tuning: targetLayer->tuningPot->setValue(integerValue, juce::dontSendNotification); break;
            case EditableLayerParameter::Attack: targetLayer->attackPot->setValue(integerValue, juce::dontSendNotification); break;
            case EditableLayerParameter::Decay: targetLayer->decayPot->setValue(integerValue, juce::dontSendNotification); break;
            case EditableLayerParameter::Gain: targetLayer->gainPot->setValue(floatValue, juce::dontSendNotification); break;
            case EditableLayerParameter::Pan: targetLayer->panPot->setValue(integerValue, juce::dontSendNotification); break;
            case EditableLayerParameter::FlipLR:
                targetLayer->flipLRButton->setToggleState(boolValue, juce::dontSendNotification);
                targetLayer->flipLRExplicit = true;
                break;
            case EditableLayerParameter::FlipPhase: targetLayer->flipPhaseButton->setToggleState(boolValue, juce::dontSendNotification); break;
            case EditableLayerParameter::Bleed:
                for (const auto& bleedControl : targetLayer->micBleeds)
                    if (bleedControl != nullptr && bleedControl->micCode.compareIgnoreCase(micCode) == 0) bleedControl->bleedPot->setValue(bleedValue * 100.0f, juce::dontSendNotification);
                break;
            case EditableLayerParameter::VelocityResponse: break;
        }
    }
    this->suppressParameterCallbacks = false;

    std::vector<DrumKitEngine::LayerParameterTarget> runtimeTargets;
    runtimeTargets.reserve(targetProjections.size());
    for (const auto& projection : targetProjections) runtimeTargets.push_back({ projection.slotId, projection.articulationSlotCode, projection.articulationSlotLayerIndex, runtimeUpdate });
    this->engine.updateSlotParametersBatch(runtimeTargets);

    this->pendingInstrumentMap = std::move(workingMap);
    this->mapTextDirty = true;
    if (parameter == EditableLayerParameter::Tuning || parameter == EditableLayerParameter::Attack || parameter == EditableLayerParameter::Decay || parameter == EditableLayerParameter::Gain || parameter == EditableLayerParameter::Pan || parameter == EditableLayerParameter::Bleed)
        this->pendingPreviewArtName = layer->artName;
    this->startTimer(this->parameterChangeDebounceMs);
}

void PadContextPopup::timerCallback() {
    const auto previewArtName = this->pendingPreviewArtName;
    this->pendingPreviewArtName.clear();
    this->flushMapText();
    if (previewArtName.isNotEmpty()) this->engine.playPreviewNote(this->slotId, previewArtName, this->getPreviewVelocity());
}
// Generate map text: rebuilds this slot while preserving every other slot.

juce::String PadContextPopup::generateFullMapText() const {
    auto currentMapText = this->savedInstrumentMapText.isNotEmpty() ? this->savedInstrumentMapText : this->engine.getSuggestedInstrumentMapText();
    auto parseResult = DrumKitEngine::parseInstrumentMapText(currentMapText);
    if (!parseResult.ok()) return currentMapText;

    auto& mapLayers = parseResult.value.layers;
    std::vector<DrumKitEngine::InstrumentMapLayer> unmatchedMapLayers;
    for (const auto& mapLayer : mapLayers) {
        if (mapLayer.slotId.compareIgnoreCase(this->slotId) != 0) continue;

        const auto& articulationSlotFilters = mapLayer.slotArticulationFilter;
        bool matchesVisibleArticulationSlot = articulationSlotFilters.empty();
        for (const auto* articulationGroup : this->articulationGroups) {
            if (articulationGroup == nullptr) continue;
            for (const auto& filter : articulationSlotFilters) {
                if (!ArticulationNames::mapFilterMatchesArticulation(filter, articulationGroup->artName)) continue;
                matchesVisibleArticulationSlot = true;
                break;
            }
            if (matchesVisibleArticulationSlot) break;
        }
        if (!matchesVisibleArticulationSlot) unmatchedMapLayers.push_back(mapLayer);
    }
    mapLayers.erase(std::remove_if(mapLayers.begin(), mapLayers.end(), [this](const DrumKitEngine::InstrumentMapLayer& mapLayer) { return mapLayer.slotId.compareIgnoreCase(this->slotId) == 0; }), mapLayers.end());
    mapLayers.insert(mapLayers.end(), std::make_move_iterator(unmatchedMapLayers.begin()), std::make_move_iterator(unmatchedMapLayers.end()));

    for (const auto* layer : this->layerRows) {
        if (layer->selectedLibraryArticulationIndex < 0) continue;

        const auto& libArt = this->availableLibraryArticulations[static_cast<size_t>(layer->selectedLibraryArticulationIndex)];
        DrumKitEngine::InstrumentMapLayer mapLayer;
        mapLayer.slotId = this->slotId;
        if (layer->artName != "(default)") mapLayer.slotArticulationFilter.push_back(layer->artName);
        mapLayer.kitRef = libArt.kitCodeRaw;
        mapLayer.instCode = libArt.instCode;
        if (libArt.artCode.isNotEmpty()) mapLayer.articulationFilter.push_back(libArt.artCode);

        const int tuning = static_cast<int>(layer->tuningPot->getValue());
        if (tuning != 0) mapLayer.tuningCents = tuning;
        const int attack = static_cast<int>(layer->attackPot->getValue());
        if (attack >= 0) mapLayer.attackTimeMs = attack;
        const int decay = static_cast<int>(layer->decayPot->getValue());
        if (decay >= 0) mapLayer.decayTimeMs = decay;
        const float volume = static_cast<float>(layer->gainPot->getValue());
        if (std::abs(volume) > 0.01f) mapLayer.volumeDb = volume;
        const int pan = static_cast<int>(layer->panPot->getValue());
        if (pan != 0) mapLayer.pan = pan;
        mapLayer.flipLR = layer->flipLRButton->getToggleState();
        mapLayer.flipLRExplicit = mapLayer.flipLR || layer->flipLRExplicit;
        mapLayer.flipPhase = layer->flipPhaseButton->getToggleState();
        mapLayer.velocityResponse = layer->velocityResponse;

        for (const auto& bleedCtrl : layer->micBleeds) {
            if (bleedCtrl == nullptr || bleedCtrl->micCode.isEmpty()) continue;
            const float bleed = static_cast<float>(bleedCtrl->bleedPot->getValue() / 100.0);
            if (bleed < 0.99f) mapLayer.micBleedLevels[bleedCtrl->micCode] = bleed;
            if (bleedCtrl->selectedOutputId.isNotEmpty()) mapLayer.micRouteOverrides[bleedCtrl->micCode] = bleedCtrl->selectedOutputId;
        }
        mapLayers.push_back(std::move(mapLayer));
    }

    return this->engine.normalizeInstrumentMapText(DrumKitEngine::serializeInstrumentMap(parseResult.value));
}


bool PadContextPopup::applyToEngine() {
    auto text = this->generateFullMapText();
    if (text.trim().isEmpty()) return false;
    if (text == this->savedInstrumentMapText) {
        this->mapTextDirty = false;
        this->refreshLayerMappings();
        return true;
    }

    const bool applied = this->engine.applyInstrumentMapSlotChange(text, this->slotId);
    this->savedInstrumentMapText = text;
    this->mapTextDirty = false;
    this->refreshLayerMappings();

    if (this->onMapTextChanged) this->onMapTextChanged(text);
    return applied;
}

void PadContextPopup::refreshLayerMappings() {
    for (auto* layer : this->layerRows) {
        if (layer == nullptr) continue;
        layer->mappedLayerIndex = -1;
    }

    const auto* layout = this->engine.getKitLayout();
    if (layout == nullptr) return;
    auto parsedMap = DrumKitEngine::parseInstrumentMapText(this->savedInstrumentMapText);
    if (!parsedMap.ok()) return;
    const auto projections = InstrumentMap::projectLayers(parsedMap.value, *layout);

    for (const auto* group : this->articulationGroups) {
        std::vector<MapLayerRow*> mappedRows;
        for (auto* layer : this->layerRows)
            if (layer != nullptr && layer->artName.compareIgnoreCase(group->artName) == 0 && layer->selectedLibraryArticulationIndex >= 0) mappedRows.push_back(layer);

        for (const auto& projection : projections) {
            if (projection.slotId.compareIgnoreCase(this->slotId) != 0 || projection.articulationSlotCode.compareIgnoreCase(group->artName) != 0) continue;
            if (projection.articulationSlotLayerIndex < 0 || projection.articulationSlotLayerIndex >= static_cast<int>(mappedRows.size())) continue;
            auto* mappedRow = mappedRows[static_cast<size_t>(projection.articulationSlotLayerIndex)];
            mappedRow->mappedLayerIndex = projection.articulationSlotLayerIndex;
        }
    }
}

void PadContextPopup::rebuildMapLayerRows() {
    this->layerRows.clear(true);
    this->populateLayers();
    this->resized();
}

void PadContextPopup::flushMapText() {
    this->stopTimer();
    this->pendingPreviewArtName.clear();
    if (!this->mapTextDirty) {
        this->pendingInstrumentMap.reset();
        return;
    }

    const auto text = this->pendingInstrumentMap.has_value() ? DrumKitEngine::serializeInstrumentMap(*this->pendingInstrumentMap) : this->generateFullMapText();
    if (text.trim().isEmpty()) return;

    this->savedInstrumentMapText = text;
    this->pendingInstrumentMap.reset();
    this->mapTextDirty = false;
    this->refreshLayerMappings();

    if (this->onMapTextChanged) this->onMapTextChanged(text);
}
