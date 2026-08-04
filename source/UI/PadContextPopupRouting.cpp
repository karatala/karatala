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

static juce::String getMicCodeDescription(const juce::String& micCode) {
    const auto parsed = KitLib::MicSpec::parseMicCode(micCode);
    if (!parsed.ok()) return micCode;

    auto description = parsed.value.getDescription();
    description = description.replaceFirstOccurrenceOf("Overhead Normal", "OH");
    description = description.replaceFirstOccurrenceOf("Overhead", "OH");
    description = description.replaceFirstOccurrenceOf("Direct", "DI");
    description = description.replaceFirstOccurrenceOf("MidSide", "Mid-Side");
    return description;
}

std::vector<std::pair<juce::String, juce::String>> PadContextPopup::getRelevantMicsForSourcePath(const juce::String& sourcePath) const {
    std::vector<std::pair<juce::String, juce::String>> result;
    const auto normalizedPath = sourcePath.trim();
    if (normalizedPath.isEmpty()) return result;

    const auto* libraryManager = this->engine.getInstrumentManager();
    if (libraryManager == nullptr) return result;

    const int matchingIndex = this->findSourceIndex(normalizedPath);
    SourceItem sourceItem;
    if (matchingIndex >= 0) {
        sourceItem = this->sourceItems[static_cast<size_t>(matchingIndex)];
    } else {
        juce::StringArray pathParts;
        pathParts.addTokens(normalizedPath, "/", "");
        pathParts.removeEmptyStrings();
        if (pathParts.isEmpty()) return result;

        sourceItem.kitCodeRaw = pathParts[0];
        sourceItem.instCode = pathParts.size() > 1 ? pathParts[1] : juce::String();
        sourceItem.artCode = pathParts.size() > 2 ? pathParts[2] : juce::String();
    }

    const auto* libraryKit = libraryManager->findKitById(sourceItem.kitCodeRaw);
    const KitLib::InstrumentSpec* libraryInstrument = libraryKit != nullptr ? libraryKit->findInstrumentById(sourceItem.instCode) : nullptr;

    if (libraryInstrument == nullptr) return result;

    std::vector<const KitLib::ArticulationSpec*> libraryArticulations;
    if (sourceItem.artCode.isNotEmpty()) {
        for (const auto& artPtr : libraryInstrument->articulationSpecs) {
            if (artPtr == nullptr) continue;
            if (ArticulationNames::articulationCodeMatches(artPtr->codeRaw, sourceItem.artCode) || ArticulationNames::baseArticulationMatches(artPtr->codeRaw, sourceItem.artCode)) {
                libraryArticulations.push_back(artPtr.get());
                break;
            }
        }
    } else {
        for (const auto& artPtr : libraryInstrument->articulationSpecs) {
            if (artPtr != nullptr) libraryArticulations.push_back(artPtr.get());
        }
    }

    if (libraryArticulations.empty())
        return result;

    std::unordered_set<juce::String> seenCodes;
    for (const auto* libraryArticulation : libraryArticulations) {
        if (libraryArticulation == nullptr) continue;
        for (const auto& velocityLevel : libraryArticulation->velocityLevels) {
            if (velocityLevel == nullptr) continue;
            for (const auto& logicalHit : velocityLevel->logicalHits) {
                if (logicalHit == nullptr) continue;
                for (const auto& sample : logicalHit->samples) {
                    if (sample == nullptr || sample->micSpec.raw.isEmpty()) continue;
                    const auto micCode = sample->micSpec.raw;
                    if (seenCodes.insert(micCode).second) result.push_back({ micCode, getMicCodeDescription(micCode) });
                }
            }
        }
    }

    std::sort(result.begin(), result.end(), [](const auto& leftMic, const auto& rightMic) { return leftMic.first.compareIgnoreCase(rightMic.first) < 0; });

    return result;
}

const MicLayout::Mic* PadContextPopup::findLayoutMicForSlot(const KitLayout::InstrumentSlot& slot, const juce::String& micCode) const {
    const auto* micLayout = this->engine.getMicLayout();
    if (micLayout == nullptr) return nullptr;

    const MicLayout::Mic* bestMic = nullptr;
    int bestPriority = -1;
    for (const auto& mic : micLayout->mics) {
        if (mic.code.compareIgnoreCase(micCode) != 0) continue;
        const auto selector = parseMicInstrumentSelector(mic.instrument, this->engine.getInstrumentCatalog());
        const int priority = static_cast<int>(matchMicInstrumentSelector(selector, slot.id, slot.type, this->engine.getInstrumentCatalog()));
        if (priority > bestPriority) {
            bestMic = &mic;
            bestPriority = priority;
        }
    }
    return bestMic;
}


std::vector<KitLib::MicSpec> PadContextPopup::getDirectMicSpecsForSourcePath(const juce::String& sourcePath) const {
    std::vector<KitLib::MicSpec> result;
    const auto normalizedPath = sourcePath.trim();
    if (normalizedPath.isEmpty()) return result;

    const auto* libraryManager = this->engine.getInstrumentManager();
    if (libraryManager == nullptr)
        return result;

    const int matchingIndex = this->findSourceIndex(normalizedPath);
    SourceItem sourceItem;
    if (matchingIndex >= 0) {
        sourceItem = this->sourceItems[static_cast<size_t>(matchingIndex)];
    } else {
        juce::StringArray pathParts;
        pathParts.addTokens(normalizedPath, "/", "");
        pathParts.removeEmptyStrings();
        if (pathParts.isEmpty()) return result;

        sourceItem.kitCodeRaw = pathParts[0];
        sourceItem.instCode = pathParts.size() > 1 ? pathParts[1] : juce::String();
        sourceItem.artCode = pathParts.size() > 2 ? pathParts[2] : juce::String();
    }

    const auto* libraryKit = libraryManager->findKitById(sourceItem.kitCodeRaw);
    const KitLib::InstrumentSpec* libraryInstrument = libraryKit != nullptr ? libraryKit->findInstrumentById(sourceItem.instCode) : nullptr;

    if (libraryInstrument == nullptr) return result;

    std::vector<const KitLib::ArticulationSpec*> libraryArticulations;
    if (sourceItem.artCode.isNotEmpty()) {
        for (const auto& artPtr : libraryInstrument->articulationSpecs) {
            if (artPtr == nullptr) continue;
            if (ArticulationNames::articulationCodeMatches(artPtr->codeRaw, sourceItem.artCode) || ArticulationNames::baseArticulationMatches(artPtr->codeRaw, sourceItem.artCode)) {
                libraryArticulations.push_back(artPtr.get());
                break;
            }
        }
    } else {
        for (const auto& artPtr : libraryInstrument->articulationSpecs) {
            if (artPtr != nullptr) libraryArticulations.push_back(artPtr.get());
        }
    }

    std::unordered_set<juce::String> seenCodes;
    for (const auto* libraryArticulation : libraryArticulations) {
        if (libraryArticulation == nullptr) continue;
        for (const auto& velocityLevel : libraryArticulation->velocityLevels) {
            if (velocityLevel == nullptr) continue;
            for (const auto& logicalHit : velocityLevel->logicalHits) {
                if (logicalHit == nullptr) continue;
                for (const auto& sample : logicalHit->samples) {
                    if (sample == nullptr || sample->micSpec.type != KitLib::MicSpec::Type::Direct) continue;
                    if (seenCodes.insert(sample->micSpec.raw).second) result.push_back(sample->micSpec);
                }
            }
        }
    }

    return result;
}

PadContextPopup::MicRoutingDisplay PadContextPopup::buildMicRoutingDisplay(const juce::String& sourcePath, const juce::String& micCode, const juce::String& targetSlotId) const {
    MicRoutingDisplay display;
    const auto* kitLayout = this->engine.getKitLayout();
    const auto* micLayout = this->engine.getMicLayout();
    if (kitLayout == nullptr || micLayout == nullptr) {
        display.routeText = "No route";
        display.tooltip = "No active mic layout";
        display.hasProblem = true;
        return display;
    }

    const KitLayout::InstrumentSlot* slot = nullptr;
    for (const auto& candidate : kitLayout->instrumentSlots) {
        if (candidate.id.compareIgnoreCase(targetSlotId) == 0) {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr) {
        display.routeText = "No route";
        display.tooltip = "Slot not found: " + targetSlotId;
        display.hasProblem = true;
        return display;
    }

    const auto* layoutMic = this->findLayoutMicForSlot(*slot, micCode);
    const auto parsedSourceMic = KitLib::MicSpec::parseMicCode(micCode);

    const auto directSpecs = this->getDirectMicSpecsForSourcePath(sourcePath);
    for (const auto& micSpec : directSpecs) {
        bool directMicMatches = micSpec.raw.compareIgnoreCase(micCode) == 0;
        if (!directMicMatches && layoutMic != nullptr) directMicMatches = directMicSpecsMatchExactly(micSpec, layoutMic->specs);
        if (!directMicMatches) continue;

        MicRoutingPlan::SlotRequest request;
        request.slotId = slot->id;
        request.slotType = slot->type;
        request.directPolicy = slot->directMicOuts.value_or(this->engine.getInstrumentCatalog().defaultDirectMicOutsFor(slot->type).value_or(DirectMicOutPolicy::Shared));
        request.directMicSpecs = directSpecs;

        const auto plan = MicRoutingPlan::build(*micLayout, { request }, this->engine.getInstrumentCatalog());
        const auto assignment = plan.resolve(slot->id, slot->type, micSpec);
        if (assignment.matched()) {
            display.routeText = assignment.outputName;
            display.tooltip = micCode + " -> " + assignment.outputName;
            display.outputId = assignment.outputId;
            return display;
        }

        display.routeText = "No route";
        display.tooltip = assignment.warning.isNotEmpty() ? assignment.warning : "No compatible direct output for " + micCode;
        display.hasProblem = true;
        return display;
    }

    if (layoutMic == nullptr && !parsedSourceMic.ok()) {
        display.routeText = "No route";
        display.tooltip = "No mic layout entry for " + micCode;
        display.hasProblem = true;
        return display;
    }

    const auto* plan = this->engine.getMicRoutingPlan();
    if (plan == nullptr) {
        display.routeText = layoutMic->name.isNotEmpty() ? layoutMic->name : layoutMic->code;
        display.tooltip = "No active routing plan";
        display.hasProblem = true;
        return display;
    }

    const auto& sourceMicSpec = layoutMic != nullptr ? layoutMic->specs : parsedSourceMic.value;
    const auto assignment = plan->resolve(slot->id, slot->type, sourceMicSpec);
    if (assignment.matched()) {
        display.routeText = assignment.outputName;
        display.tooltip = micCode + " -> " + assignment.outputName;
        display.outputId = assignment.outputId;
        return display;
    }

    display.routeText = "No route";
    display.tooltip = assignment.warning.isNotEmpty() ? assignment.warning : "No route for " + micCode;
    display.hasProblem = true;
    return display;
}

std::vector<PadContextPopup::MicRoutingChoice> PadContextPopup::buildMicRoutingChoices(const juce::String& sourcePath, const juce::String& micCode, const juce::String& targetSlotId) const {
    std::vector<MicRoutingChoice> choices;

    const auto currentDisplay = this->buildMicRoutingDisplay(sourcePath, micCode, targetSlotId);
    choices.push_back({ "-", "Unassigned from output", "-", -1, false, false });

    const auto* kitLayout = this->engine.getKitLayout();
    const auto* micLayout = this->engine.getMicLayout();
    if (kitLayout == nullptr || micLayout == nullptr) {
        choices.push_back({ "No route", currentDisplay.tooltip.isNotEmpty() ? currentDisplay.tooltip : juce::String("No active mic layout"), {}, 0, true, true, true });
        return choices;
    }

    const KitLayout::InstrumentSlot* slot = nullptr;
    for (const auto& candidate : kitLayout->instrumentSlots) {
        if (candidate.id.compareIgnoreCase(targetSlotId) == 0) {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr) {
        choices.push_back({ "No route", "Slot not found: " + targetSlotId, {}, 0, true, true, true });
        return choices;
    }

    const auto* layoutMic = this->findLayoutMicForSlot(*slot, micCode);
    const auto parsedSourceMic = KitLib::MicSpec::parseMicCode(micCode);

    std::optional<KitLib::MicSpec> sourceDirectSpec;
    const auto directSpecs = this->getDirectMicSpecsForSourcePath(sourcePath);
    for (const auto& micSpec : directSpecs) {
        bool directMicMatches = micSpec.raw.compareIgnoreCase(micCode) == 0;
        if (!directMicMatches && layoutMic != nullptr) directMicMatches = directMicSpecsMatchExactly(micSpec, layoutMic->specs);
        if (!directMicMatches && layoutMic != nullptr) directMicMatches = directMicSpecsCanUseGroupOutput(micSpec, layoutMic->specs);
        if (directMicMatches) {
            sourceDirectSpec = micSpec;
            break;
        }
    }

    auto addChoice = [&choices, &currentDisplay](const MicLayout::Mic& mic, const juce::String& outputId, int rank, const juce::String& detail) {
        const juce::String label = mic.name.isNotEmpty() ? mic.name : mic.code;
        const bool isDefault = !currentDisplay.hasProblem && currentDisplay.outputId.compareIgnoreCase(outputId) == 0;
        MicRoutingChoice choice;
        choice.label = label;
        choice.tooltip = (isDefault ? "Default routing: " : juce::String()) + label + " (" + outputId + ")" + detail;
        choice.outputId = outputId;
        choice.rank = isDefault ? 0 : rank;
        choice.isCurrent = isDefault;
        choice.usesAutomaticRouting = isDefault;
        for (auto& existingChoice : choices) {
            if (existingChoice.outputId.compareIgnoreCase(outputId) != 0) continue;
            if (choice.isCurrent || choice.rank < existingChoice.rank) existingChoice = std::move(choice);
            return;
        }
        choices.push_back(std::move(choice));
    };

    const auto slotIdLower = slot->id.trim().toLowerCase();
    for (const auto& mic : micLayout->mics) {
        const auto& outputId = mic.id;
        bool compatible = false;
        bool exactDirect = false;
        bool groupDirect = false;
        if (sourceDirectSpec.has_value()) {
            exactDirect = directMicSpecsMatchExactly(*sourceDirectSpec, mic.specs);
            groupDirect = directMicSpecsCanUseGroupOutput(*sourceDirectSpec, mic.specs);
            compatible = exactDirect || groupDirect;
        } else if (layoutMic != nullptr || parsedSourceMic.ok()) {
            const auto& sourceMicSpec = layoutMic != nullptr ? layoutMic->specs : parsedSourceMic.value;
            compatible = micSpecsMatchOutput(sourceMicSpec, mic.specs);
        }
        if (!compatible) continue;

        const auto instrumentText = mic.instrument.trim().toLowerCase();
        const bool sameInstrument = instrumentText == slotIdLower;
        const bool pooledOutput = instrumentText.isEmpty();
        const auto selector = parseMicInstrumentSelector(mic.instrument, this->engine.getInstrumentCatalog());
        const bool selectorMatches = !pooledOutput && matchMicInstrumentSelector(selector, slot->id, slot->type, this->engine.getInstrumentCatalog()) != MicSelectorPriority::None;
        if (!sameInstrument && !pooledOutput && !selectorMatches) continue;

        int rank = 80;
        if (sameInstrument && mic.code.compareIgnoreCase(micCode) == 0) rank = 10;
        else if (sameInstrument) rank = 20;
        else if (selectorMatches && mic.code.compareIgnoreCase(micCode) == 0) rank = 30;
        else if (selectorMatches) rank = 40;
        else if (pooledOutput && mic.code.compareIgnoreCase(micCode) == 0) rank = 50;
        else if (pooledOutput && exactDirect) rank = 60;
        else if (pooledOutput && groupDirect) rank = 70;

        const juce::String detail = sameInstrument ? " for " + slot->id : (pooledOutput ? " pool" : " for " + mic.instrument);
        addChoice(mic, outputId, rank, detail);
    }

    if (!currentDisplay.hasProblem) {
        bool foundCurrent = false;
        for (auto& choice : choices) {
            if (choice.isCurrent) foundCurrent = true;
        }
        if (!foundCurrent && currentDisplay.routeText.isNotEmpty()) choices.push_back({ currentDisplay.routeText, currentDisplay.tooltip, currentDisplay.outputId, 5, false, true, true });
    } else {
        choices.push_back({ "No route", currentDisplay.tooltip, {}, 5, true, true, true });
    }

    std::sort(choices.begin() + 1, choices.end(), [](const auto& leftChoice, const auto& rightChoice) {
        if (leftChoice.rank != rightChoice.rank) return leftChoice.rank < rightChoice.rank;
        return leftChoice.label.compareIgnoreCase(rightChoice.label) < 0;
    });

    return choices;
}

void PadContextPopup::updateRoutingComboDisplay(MapLayerRow::MicBleedControl& bleedCtrl) const {
    if (bleedCtrl.routingCombo == nullptr) return;

    const int selectedIndex = bleedCtrl.routingCombo->getSelectedId() - 1;
    bool hasProblem = true;
    juce::String tooltip = "Automatic routing";
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(bleedCtrl.routingChoices.size())) {
        const auto& choice = bleedCtrl.routingChoices[static_cast<size_t>(selectedIndex)];
        hasProblem = choice.hasProblem;
        tooltip = choice.tooltip;
    }

    bleedCtrl.routingCombo->setTooltip(tooltip);
    bleedCtrl.routingCombo->setColour(juce::ComboBox::outlineColourId, hasProblem ? UITheme::Colours::accentWarning : UITheme::Colours::borderSubtle);
    bleedCtrl.routingCombo->repaint();
}

int PadContextPopup::findRoutingChoiceIndex(const std::vector<MicRoutingChoice>& choices, const juce::String& outputId) {
    for (int choiceIndex = 0; choiceIndex < static_cast<int>(choices.size()); ++choiceIndex) {
        const auto& choice = choices[static_cast<size_t>(choiceIndex)];
        if (choice.hasProblem) continue;
        if (outputId.isEmpty() && choice.usesAutomaticRouting) return choiceIndex;
        if (outputId.isNotEmpty() && choice.outputId.compareIgnoreCase(outputId) == 0) return choiceIndex;
    }
    return -1;
}

std::vector<std::pair<juce::String, juce::String>> PadContextPopup::getRelevantMicsForSourceIndex(int sourceIndex) const {
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(this->sourceItems.size())) return {};
    return this->getRelevantMicsForSourcePath(this->sourceItems[static_cast<size_t>(sourceIndex)].fullPath());
}

void PadContextPopup::rebuildBleedControls(MapLayerRow* layer, const std::unordered_map<juce::String, float>& seedLevels, const std::unordered_map<juce::String, juce::String>& seedRoutes) {
    if (layer == nullptr) return;

    std::unordered_map<juce::String, float> existingLevels = seedLevels;
    std::unordered_map<juce::String, juce::String> existingRoutes = seedRoutes;
    if (!layer->micBleeds.empty()) {
        existingLevels.clear();
        existingRoutes.clear();
        for (const auto& bleedCtrl : layer->micBleeds) {
            if (bleedCtrl == nullptr || bleedCtrl->bleedPot == nullptr || bleedCtrl->micCode.isEmpty()) continue;
            existingLevels[bleedCtrl->micCode] = static_cast<float>(bleedCtrl->bleedPot->getValue() / 100.0);
            if (bleedCtrl->selectedOutputId.isNotEmpty()) existingRoutes[bleedCtrl->micCode] = bleedCtrl->selectedOutputId;
        }
    }

    layer->micBleeds.clear();

    juce::String sourcePath;
    if (layer->selectedSourceIndex >= 0 && layer->selectedSourceIndex < static_cast<int>(this->sourceItems.size()))
        sourcePath = this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)].fullPath();
    else
        sourcePath = layer->sourcePathHint;

    const auto micEntries = this->getRelevantMicsForSourcePath(sourcePath);
    std::vector<std::pair<juce::String, juce::String>> entriesToShow = micEntries;
    if (entriesToShow.empty() && (!existingLevels.empty() || !existingRoutes.empty())) {
        for (const auto& [micCode, level] : existingLevels) {
            juce::ignoreUnused(level);
            entriesToShow.push_back({ micCode, micCode });
        }
        for (const auto& [micCode, outputId] : existingRoutes) {
            juce::ignoreUnused(outputId);
            bool alreadyAdded = false;
            for (const auto& existingEntry : entriesToShow) {
                if (existingEntry.first.compareIgnoreCase(micCode) == 0) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (!alreadyAdded) entriesToShow.push_back({ micCode, micCode });
        }
        std::sort(entriesToShow.begin(), entriesToShow.end(), [](const auto& leftEntry, const auto& rightEntry) {
            return leftEntry.first.compareIgnoreCase(rightEntry.first) < 0;
        });
    }

    for (const auto& [micCode, micName] : entriesToShow) {
        auto bleedCtrl = std::make_unique<MapLayerRow::MicBleedControl>();
        bleedCtrl->micCode = micCode;
        bleedCtrl->micName = micName;
        float level = 1.0f;
        const auto it = existingLevels.find(micCode);
        if (it != existingLevels.end())
            level = juce::jlimit(0.0f, 1.0f, it->second);
        const auto routeIt = existingRoutes.find(micCode);
        if (routeIt != existingRoutes.end()) bleedCtrl->selectedOutputId = routeIt->second;
        bleedCtrl->bleedPot = std::make_unique<CompactParameterControl>(micCode, 0.0, 100.0, 1.0, static_cast<double>(level * 100.0f));
        bleedCtrl->bleedPot->setTooltip(micName);
        bleedCtrl->bleedPot->setInteraction(100.0, 1.0);
        this->layerContent.addAndMakeVisible(*bleedCtrl->bleedPot);
        auto* bleedCtrlPtr = bleedCtrl.get();
        bleedCtrl->bleedPot->onValueChange = [this, layer, bleedCtrlPtr] {
            this->applyLayerParameterChange(layer, EditableLayerParameter::Bleed, bleedCtrlPtr->micCode);
        };

        bleedCtrl->routingChoices = this->buildMicRoutingChoices(sourcePath, micCode, this->slotId);
        bleedCtrl->routingCombo = std::make_unique<juce::ComboBox>();
        UIComponentStyle::configureComboBox(*bleedCtrl->routingCombo);
        bleedCtrl->routingCombo->getProperties().set("compactRoute", true);
        bleedCtrl->routingCombo->getProperties().set("hideArrow", true);
        const auto requestedOutputId = bleedCtrl->selectedOutputId;
        int selectedItemId = 0;
        for (int choiceIdx = 0; choiceIdx < static_cast<int>(bleedCtrl->routingChoices.size()); ++choiceIdx) {
            const auto& choice = bleedCtrl->routingChoices[static_cast<size_t>(choiceIdx)];
            const int itemId = choiceIdx + 1;
            bleedCtrl->routingCombo->addItem(choice.label, itemId);
            if (bleedCtrl->selectedOutputId.isEmpty() && choice.usesAutomaticRouting) selectedItemId = itemId;
            else if (bleedCtrl->selectedOutputId.isNotEmpty() && choice.outputId.compareIgnoreCase(bleedCtrl->selectedOutputId) == 0) selectedItemId = itemId;
        }
        if (selectedItemId == 0) selectedItemId = 1;
        bleedCtrl->routingCombo->setSelectedId(selectedItemId, juce::dontSendNotification);
        const int selectedIndex = selectedItemId - 1;
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(bleedCtrl->routingChoices.size())) {
            const auto& selectedChoice = bleedCtrl->routingChoices[static_cast<size_t>(selectedIndex)];
            const bool selectedRequestedOutput = requestedOutputId.isNotEmpty() && selectedChoice.outputId.compareIgnoreCase(requestedOutputId) == 0;
            bleedCtrl->selectedOutputId = selectedRequestedOutput ? requestedOutputId : (selectedChoice.usesAutomaticRouting ? juce::String() : selectedChoice.outputId);
        }
        bleedCtrl->routingCombo->onChange = [this, layer, bleedCtrlPtr] {
            this->applyRoutingComboChange(layer, bleedCtrlPtr);
        };
        this->updateRoutingComboDisplay(*bleedCtrl);
        this->layerContent.addAndMakeVisible(*bleedCtrl->routingCombo);
        layer->micBleeds.push_back(std::move(bleedCtrl));
    }
}


void PadContextPopup::applyRoutingComboChange(MapLayerRow* layer, MapLayerRow::MicBleedControl* bleedCtrl) {
    if (this->suppressParameterCallbacks || layer == nullptr || bleedCtrl == nullptr || bleedCtrl->routingCombo == nullptr) return;
    this->flushMapText();

    const auto oldRoute = bleedCtrl->selectedOutputId;
    const int selectedIndex = bleedCtrl->routingCombo->getSelectedId() - 1;
    juce::String newRoute;
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(bleedCtrl->routingChoices.size())) {
        const auto& selectedChoice = bleedCtrl->routingChoices[static_cast<size_t>(selectedIndex)];
        if (!selectedChoice.usesAutomaticRouting) newRoute = selectedChoice.outputId;
    }

    bleedCtrl->selectedOutputId = newRoute;
    this->updateRoutingComboDisplay(*bleedCtrl);
    if (oldRoute == newRoute) return;

    auto restoreCurrentRoute = [this, bleedCtrl, &oldRoute] {
        bleedCtrl->selectedOutputId = oldRoute;
        const int oldChoiceIndex = PadContextPopup::findRoutingChoiceIndex(bleedCtrl->routingChoices, oldRoute);
        if (oldChoiceIndex >= 0) bleedCtrl->routingCombo->setSelectedId(oldChoiceIndex + 1, juce::dontSendNotification);
        this->updateRoutingComboDisplay(*bleedCtrl);
    };

    const int layerIndex = layer->mappedLayerIndex >= 0 ? layer->mappedLayerIndex : this->getLayerIndex(layer);
    if (layerIndex < 0) {
        restoreCurrentRoute();
        return;
    }

    const auto currentMapText = this->savedInstrumentMapText.isNotEmpty() ? this->savedInstrumentMapText : this->engine.getSuggestedInstrumentMapText();
    auto parsedMap = DrumKitEngine::parseInstrumentMapText(currentMapText);
    const auto* layout = this->engine.getKitLayout();
    if (!parsedMap.ok() || layout == nullptr) {
        restoreCurrentRoute();
        return;
    }

    auto projections = InstrumentMap::projectLayers(parsedMap.value, *layout);
    auto findMapLayerProjection = [this, layer, layerIndex](const auto& candidateProjections) -> const InstrumentMap::LayerProjection* {
        for (const auto& projection : candidateProjections) {
            if (projection.slotId.compareIgnoreCase(this->slotId) != 0 || projection.articulationSlotCode.compareIgnoreCase(layer->artName) != 0 || projection.articulationSlotLayerIndex != layerIndex) continue;
            return &projection;
        }
        return nullptr;
    };

    const auto* mapLayerProjection = findMapLayerProjection(projections);
    if (mapLayerProjection == nullptr) {
        restoreCurrentRoute();
        return;
    }

    const bool linkArts = this->linkArtsButton.getToggleState();
    const bool linkTomBleed = this->linkTomBleedButton.isVisible() && this->linkTomBleedButton.getToggleState();
    if (!linkArts && !linkTomBleed) {
        const auto slotIterator = std::find_if(layout->instrumentSlots.begin(), layout->instrumentSlots.end(), [this](const auto& slot) {
            return slot.id.compareIgnoreCase(this->slotId) == 0;
        });
        if (slotIterator != layout->instrumentSlots.end()) {
            InstrumentMap::isolateLayerForArticulationSlot(parsedMap.value, *slotIterator, mapLayerProjection->mapLayerIndex, layer->artName);
            projections = InstrumentMap::projectLayers(parsedMap.value, *layout);
            mapLayerProjection = findMapLayerProjection(projections);
            if (mapLayerProjection == nullptr) {
                restoreCurrentRoute();
                return;
            }
        }
    }

    auto targetProjections = InstrumentMap::collectLinkedMapLayerTargets(projections, this->slotId, layer->artName, layerIndex, linkArts, linkTomBleed);
    std::erase_if(targetProjections, [this, &parsedMap, bleedCtrl, &newRoute](const auto& projection) {
        if (projection.mapLayerIndex >= parsedMap.value.layers.size()) return true;
        const auto& mapLayer = parsedMap.value.layers[projection.mapLayerIndex];
        const auto libraryPath = InstrumentMap::libraryPathForArticulationSlot(mapLayer, projection.articulationSlotCode);
        const auto relevantMics = this->getRelevantMicsForSourcePath(libraryPath);
        const bool hasSourceMic = std::any_of(relevantMics.begin(), relevantMics.end(), [bleedCtrl](const auto& mic) {
            return mic.first.compareIgnoreCase(bleedCtrl->micCode) == 0;
        });
        if (!hasSourceMic) return true;
        const auto choices = this->buildMicRoutingChoices(libraryPath, bleedCtrl->micCode, projection.slotId);
        return PadContextPopup::findRoutingChoiceIndex(choices, newRoute) < 0;
    });
    if (targetProjections.empty()) {
        restoreCurrentRoute();
        return;
    }

    std::unordered_set<size_t> updatedMapLayerIndexes;
    for (const auto& projection : targetProjections) {
        if (projection.mapLayerIndex >= parsedMap.value.layers.size() || !updatedMapLayerIndexes.insert(projection.mapLayerIndex).second) continue;
        auto& routeOverrides = parsedMap.value.layers[projection.mapLayerIndex].micRouteOverrides;
        for (auto routeIterator = routeOverrides.begin(); routeIterator != routeOverrides.end();) {
            if (routeIterator->first.compareIgnoreCase(bleedCtrl->micCode) == 0) routeIterator = routeOverrides.erase(routeIterator);
            else ++routeIterator;
        }
        if (newRoute.isNotEmpty()) routeOverrides[bleedCtrl->micCode] = newRoute;
    }

    this->suppressParameterCallbacks = true;
    for (const auto& projection : targetProjections) {
        if (projection.slotId.compareIgnoreCase(this->slotId) != 0) continue;
        MapLayerRow* targetLayer = nullptr;
        for (auto* candidateLayer : this->layerRows) {
            if (candidateLayer != nullptr && candidateLayer->artName.compareIgnoreCase(projection.articulationSlotCode) == 0 && candidateLayer->mappedLayerIndex == projection.articulationSlotLayerIndex) {
                targetLayer = candidateLayer;
                break;
            }
        }
        if (targetLayer == nullptr) continue;
        for (const auto& targetBleedControl : targetLayer->micBleeds) {
            if (targetBleedControl == nullptr || targetBleedControl->micCode.compareIgnoreCase(bleedCtrl->micCode) != 0) continue;
            const int targetChoiceIndex = PadContextPopup::findRoutingChoiceIndex(targetBleedControl->routingChoices, newRoute);
            if (targetChoiceIndex < 0) continue;
            targetBleedControl->selectedOutputId = newRoute;
            targetBleedControl->routingCombo->setSelectedId(targetChoiceIndex + 1, juce::dontSendNotification);
            this->updateRoutingComboDisplay(*targetBleedControl);
        }
    }
    this->suppressParameterCallbacks = false;

    std::vector<DrumKitEngine::MicRouteTarget> runtimeTargets;
    std::unordered_set<size_t> runtimeMapLayerIndexes;
    runtimeTargets.reserve(updatedMapLayerIndexes.size());
    for (const auto& projection : targetProjections) {
        if (!runtimeMapLayerIndexes.insert(projection.mapLayerIndex).second) continue;
        runtimeTargets.push_back({ projection.slotId, projection.articulationSlotCode, projection.articulationSlotLayerIndex, bleedCtrl->micCode, newRoute });
    }
    const bool runtimeUpdated = this->engine.updateSlotMicRoutesBatch(runtimeTargets);
    const auto routeTargetName = newRoute.isNotEmpty() ? newRoute : juce::String("auto");
    const auto routeStatus = "Linked mic route " + juce::String(runtimeUpdated ? "SUCCESS" : "FAILED") + ": source=" + bleedCtrl->micCode
        + " target=" + routeTargetName + " layers=" + juce::String(static_cast<int>(runtimeTargets.size()));
    this->engine.logMessage(routeStatus, DebugLevel::Debug);

    this->savedInstrumentMapText = DrumKitEngine::serializeInstrumentMap(parsedMap.value);
    this->mapTextDirty = false;
    this->refreshLayerMappings();
    if (this->onMapTextChanged) this->onMapTextChanged(this->savedInstrumentMapText);
}
