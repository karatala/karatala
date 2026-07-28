#include "PadContextPopup.h"
#include "../Engine/ArticulationNameHelpers.h"
#include "../Engine/DrumKitEngineMapHelpers.h"
#include "UIHelpers.h"
#include "UITheme.h"

#include <algorithm>
#include <cmath>
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

// MIDI utility

// MIDI note button helpers

void PadContextPopup::syncNoteButtons() {
    if (!this->midiNotesPtr || !this->midiAssignmentControl) return;
    auto& notes = *this->midiNotesPtr;
    const int n1 = !notes.empty()          ? static_cast<int>(notes[0]) : -1;
    const int n2 = notes.size() >= 2 ? static_cast<int>(notes[1]) : -1;
    this->midiAssignmentControl->setNotes(n1, n2);
}

void PadContextPopup::applyNoteToSlot(int slotIdx, int note) {
    if (!this->midiNotesPtr) return;
    UIHelpers::applyMidiNoteToSlot(*this->midiNotesPtr, slotIdx, note);
    this->syncMidiNotesToActiveLayout();
    this->syncNoteButtons();
    if (this->onMidiMappingChanged) this->onMidiMappingChanged();
}

void PadContextPopup::syncMidiNotesToActiveLayout() {
    auto* layout = this->engine.getKitLayout();
    if (layout == nullptr || this->midiNotesPtr == nullptr) return;


    for (auto& slot : layout->instrumentSlots) {
        if (slot.id != this->slotId) continue;

        if (this->selectedArticulationIndex >= 0
            && this->selectedArticulationIndex < static_cast<int>(slot.articulations.size())) {
            slot.articulations[static_cast<size_t>(this->selectedArticulationIndex)].notes = *this->midiNotesPtr;
            return;
        }
        for (auto& art : slot.articulations) {
            if (ArticulationNameHelpers::articulationCodeMatches(art.codeRaw, this->artName)) {
                art.notes = *this->midiNotesPtr;
                return;
            }
        }
        for (auto& art : slot.articulations) {
            if (ArticulationNameHelpers::baseArticulationMatches(art.codeRaw, this->artName)) {
                art.notes = *this->midiNotesPtr;
                return;
            }
        }
        if (!slot.articulations.empty()) {
            slot.articulations[0].notes = *this->midiNotesPtr;
        }
        return;
    }
}

void PadContextPopup::midiLearnNoteReceived(int midiNote) {
    if (this->activeLearningSlot < 0) return;
    this->applyNoteToSlot(this->activeLearningSlot, midiNote);
    this->activeLearningSlot = -1;
    if (this->midiAssignmentControl) this->midiAssignmentControl->setLearningSlot(-1);
    if (this->onMidiLearnStateChanged) this->onMidiLearnStateChanged(false);
}
// Source items

void PadContextPopup::rebuildSourceItems() {
    this->sourceItems = UIHelpers::buildSourceItems(this->engine.getInstrumentManager());
}

void PadContextPopup::addArticulationGroup(const juce::String& articulationName) {
    auto* group = this->articulationGroups.add(new ArticulationGroup());
    group->artName = articulationName;
    group->label.setText(articulationName, juce::dontSendNotification);
    group->label.setFont(UITheme::Fonts::normal());
    group->label.setColour(juce::Label::textColourId, UITheme::Colours::textAccent);
    group->label.setJustificationType(juce::Justification::centredLeft);
    this->layerContent.addAndMakeVisible(group->label);

    group->addLayerButton.setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    group->addLayerButton.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    group->addLayerButton.getProperties().set("compactText", true);
    group->addLayerButton.setTooltip("Add layer to " + articulationName);
    group->addLayerButton.onClick = [this, group] {
        this->addLayerRow(group->artName, "", 0, -1, -1, 0.0f, 0, false, false, false, std::nullopt, {}, {}, -1);
        this->updateRemoveButtonVisibility();
        this->resized();
    };
    this->layerContent.addAndMakeVisible(group->addLayerButton);
}

int PadContextPopup::findSourceIndex(const juce::String& searchPath) const {
    return UIHelpers::findSourceIndex(this->sourceItems, searchPath);
}

juce::uint8 PadContextPopup::getPreviewVelocity() const {
    if (!this->previewVelocityProvider)
        return static_cast<juce::uint8>(100);

    const int velocity = juce::jlimit(1, 127, this->previewVelocityProvider());
    return static_cast<juce::uint8>(velocity);
}

// Constructor

PadContextPopup::PadContextPopup(DrumKitEngine& drumKitEngine, const KitLayout::InstrumentSlot& slot, int artIdx, const juce::String& savedMapText)
    : engine(drumKitEngine)
    , slotId(slot.id)
    , savedInstrumentMapText(savedMapText)
    , slotType(slot.type) {
    // Resolve articulation
    if (artIdx >= 0 && artIdx < static_cast<int>(slot.articulations.size())) {
        this->selectedArticulationIndex = artIdx;
        const auto& art = slot.articulations[static_cast<size_t>(artIdx)];
        this->artName = art.codeRaw;
        this->midiNotesOwned = art.notes;
        this->midiNotesPtr = &this->midiNotesOwned;
    } else if (!slot.articulations.empty()) {
        this->selectedArticulationIndex = 0;
        const auto& art = slot.articulations[0];
        this->artName = art.codeRaw;
        this->midiNotesOwned = art.notes;
        this->midiNotesPtr = &this->midiNotesOwned;
    } else {
        this->artName = "(default)";
    }

    // Header: "Kick"
    juce::String headerText = slot.display.isNotEmpty() ? slot.display : slot.id;
    this->slotLabel.setText(headerText, juce::dontSendNotification);
    this->slotLabel.setFont(UITheme::Fonts::normal());
    this->slotLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    addAndMakeVisible(this->slotLabel);

    UIHelpers::configureToggleButton(this->linkArtsButton);
    this->linkArtsButton.setColour(juce::TextButton::buttonOnColourId, UITheme::Colours::broadEditDanger);
    this->linkArtsButton.setColour(juce::TextButton::textColourOnId, UITheme::Colours::textPrimary);
    this->linkArtsButton.getProperties().set("broadEditMode", true);
    this->linkArtsButton.setTooltip("Apply supported parameter changes to the same layer across all articulations in this slot");
    this->addAndMakeVisible(this->linkArtsButton);

    UIHelpers::configureToggleButton(this->linkTomBleedButton);
    this->linkTomBleedButton.setColour(juce::TextButton::buttonOnColourId, UITheme::Colours::broadEditDanger);
    this->linkTomBleedButton.setColour(juce::TextButton::textColourOnId, UITheme::Colours::textPrimary);
    this->linkTomBleedButton.getProperties().set("broadEditMode", true);
    this->linkTomBleedButton.setTooltip("Apply the changed bleed mic level to every mapped layer in every Tom slot");
    this->linkTomBleedButton.setVisible(this->slotType == KitLib::InstrumentSpec::Type::Tom);
    this->addAndMakeVisible(this->linkTomBleedButton);

    UIHelpers::configureControlButton(this->setInstrumentButton);
    this->setInstrumentButton.setTooltip("Replace every layer in this slot with one complete source instrument");
    this->setInstrumentButton.onClick = [this] { this->showInstrumentPicker(); };
    this->addAndMakeVisible(this->setInstrumentButton);

    // MIDI inline
    this->midiHeaderLabel.setText("MIDI:", juce::dontSendNotification);
    this->midiHeaderLabel.setFont(UITheme::Fonts::small());
    this->midiHeaderLabel.setColour(juce::Label::textColourId, UITheme::Colours::textMuted);
    addAndMakeVisible(this->midiHeaderLabel);

    this->midiAssignmentControl = std::make_unique<MidiNoteAssignmentControl>();
    this->midiAssignmentControl->setLayoutMetrics({ 60, UITheme::Sizes::standardControlHeight, 0, 6, 0 });
    this->midiAssignmentControl->onNoteSelected = [this](int slotIdx, int note) {
        this->applyNoteToSlot(slotIdx, note);
    };
    this->midiAssignmentControl->onLearnButtonClicked = [this](int slotIdx) {
        bool wasLearning = (this->activeLearningSlot >= 0);
        this->activeLearningSlot = (this->activeLearningSlot == slotIdx) ? -1 : slotIdx;
        this->midiAssignmentControl->setLearningSlot(this->activeLearningSlot);
        bool nowLearning = (this->activeLearningSlot >= 0);
        if (nowLearning != wasLearning && this->onMidiLearnStateChanged)
            this->onMidiLearnStateChanged(nowLearning);
    };
    addAndMakeVisible(*this->midiAssignmentControl);

    this->syncNoteButtons();

    // Scrollable layer area
    this->layerContent.layerRowsPtr = &this->layerRows;
    this->layerViewport.setViewedComponent(&this->layerContent, false);
    this->layerViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(this->layerViewport);

    if (slot.articulations.empty()) {
        this->addArticulationGroup("(default)");
    } else {
        for (const auto& articulation : slot.articulations)
            this->addArticulationGroup(articulation.codeRaw);
    }

    // Build source items and populate layers from existing instrument map
    this->rebuildSourceItems();
    this->refreshVelocityResponseChoices();
    this->populateLayers();

}

PadContextPopup::~PadContextPopup() {
    this->dismissSourceOverrideEditor();
    this->dismissVelocityResponseEditor();
    this->dismissSourcePicker();
}

void PadContextPopup::populateLayers() {
    auto mapText = this->savedInstrumentMapText.isNotEmpty() ? this->savedInstrumentMapText : this->engine.getSuggestedInstrumentMapText();
    auto parseResult = DrumKitEngine::parseInstrumentMapText(mapText);
    const auto& existingEntries = parseResult.value.entries;

    for (const auto* group : this->articulationGroups) {
        int mappedLayerIndex = 0;
        for (const auto& entry : existingEntries) {
            if (entry.slotId != this->slotId) continue;

            const auto destinationFilters = DrumKitEngineMapHelpers::effectiveArticulationFilter(entry);
            bool matchesArticulation = destinationFilters.empty();
            for (const auto& filter : destinationFilters) {
                if (ArticulationNameHelpers::mapFilterMatchesArticulation(filter, group->artName)) {
                    matchesArticulation = true;
                    break;
                }
            }
            if (!matchesArticulation) continue;

            juce::String sourceFullPath = entry.kitRef;
            if (entry.instCode.isNotEmpty()) sourceFullPath += "/" + entry.instCode;
            const auto sourceArticulation = DrumKitEngineMapHelpers::sourceArticulationForDestination(entry, group->artName);
            if (sourceArticulation.isNotEmpty()) sourceFullPath += "/" + sourceArticulation;
            if (this->slotType == KitLib::InstrumentSpec::Type::HiHat && UIHelpers::usesLegacyDestinationExpressionFilter(entry.slotArticulationFilter, group->artName))
                sourceFullPath = UIHelpers::resolveLegacyHihatSourcePath(this->sourceItems, sourceFullPath, group->artName);

            const int tuning = entry.tuningCents.value_or(0);
            const int attack = entry.attackTimeMs.value_or(-1);
            const int decay = entry.decayTimeMs.value_or(-1);
            const float volume = entry.volumeDb.value_or(0.0f);
            this->addLayerRow(group->artName, sourceFullPath, tuning, attack, decay, volume, entry.pan.value_or(0), entry.flipLR, entry.flipLRExplicit, entry.flipPhase, entry.velocityResponse, entry.micBleedLevels, entry.micRouteOverrides, mappedLayerIndex);
            ++mappedLayerIndex;
        }
    }

    this->updateRemoveButtonVisibility();
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

    const auto* sourceKit = libraryManager->findKitById(sourceItem.kitCodeRaw);
    const KitLib::InstrumentSpec* sourceInstrument = sourceKit != nullptr ? sourceKit->findInstrumentById(sourceItem.instCode) : nullptr;

    if (sourceInstrument == nullptr) return result;

    std::vector<const KitLib::ArticulationSpec*> sourceArts;
    if (sourceItem.artCode.isNotEmpty()) {
        for (const auto& artPtr : sourceInstrument->articulationSpecs) {
            if (artPtr == nullptr) continue;
            if (ArticulationNameHelpers::articulationCodeMatches(artPtr->codeRaw, sourceItem.artCode) || ArticulationNameHelpers::baseArticulationMatches(artPtr->codeRaw, sourceItem.artCode)) {
                sourceArts.push_back(artPtr.get());
                break;
            }
        }
    } else {
        for (const auto& artPtr : sourceInstrument->articulationSpecs) {
            if (artPtr != nullptr) sourceArts.push_back(artPtr.get());
        }
    }

    if (sourceArts.empty())
        return result;

    std::unordered_set<juce::String> seenCodes;
    for (const auto* artSpec : sourceArts) {
        if (artSpec == nullptr) continue;
        for (const auto& velocityLayer : artSpec->velocityLayerSpecs) {
            if (velocityLayer == nullptr) continue;
            for (const auto& variantSpec : velocityLayer->sampleVariantSpecs) {
                if (variantSpec == nullptr || variantSpec->micSpec.raw.isEmpty()) continue;
                const auto micCode = variantSpec->micSpec.raw;
                if (seenCodes.insert(micCode).second) result.push_back({ micCode, getMicCodeDescription(micCode) });
            }
        }
    }

    std::sort(result.begin(), result.end(), [](const auto& leftMic, const auto& rightMic) { return leftMic.first.compareIgnoreCase(rightMic.first) < 0; });

    return result;
}

const MicLayout::Mic* PadContextPopup::findLayoutMicForSlot(const juce::String& micCode) const {
    const auto* micLayout = this->engine.getMicLayout();
    if (micLayout == nullptr) return nullptr;

    const MicLayout::Mic* bestMic = nullptr;
    int bestPriority = -1;
    for (const auto& mic : micLayout->mics) {
        if (mic.code.compareIgnoreCase(micCode) != 0) continue;
        const auto selector = parseMicInstrumentSelector(mic.instrument, this->engine.getInstrumentCatalog());
        const int priority = static_cast<int>(matchMicInstrumentSelector(selector, this->slotId, this->slotType, this->engine.getInstrumentCatalog()));
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

    const auto* sourceKit = libraryManager->findKitById(sourceItem.kitCodeRaw);
    const KitLib::InstrumentSpec* sourceInstrument = sourceKit != nullptr ? sourceKit->findInstrumentById(sourceItem.instCode) : nullptr;

    if (sourceInstrument == nullptr) return result;

    std::vector<const KitLib::ArticulationSpec*> sourceArts;
    if (sourceItem.artCode.isNotEmpty()) {
        for (const auto& artPtr : sourceInstrument->articulationSpecs) {
            if (artPtr == nullptr) continue;
            if (ArticulationNameHelpers::articulationCodeMatches(artPtr->codeRaw, sourceItem.artCode) || ArticulationNameHelpers::baseArticulationMatches(artPtr->codeRaw, sourceItem.artCode)) {
                sourceArts.push_back(artPtr.get());
                break;
            }
        }
    } else {
        for (const auto& artPtr : sourceInstrument->articulationSpecs) {
            if (artPtr != nullptr) sourceArts.push_back(artPtr.get());
        }
    }

    std::unordered_set<juce::String> seenCodes;
    for (const auto* artSpec : sourceArts) {
        if (artSpec == nullptr) continue;
        for (const auto& velLayer : artSpec->velocityLayerSpecs) {
            if (velLayer == nullptr) continue;
            for (const auto& varSpec : velLayer->sampleVariantSpecs) {
                if (varSpec == nullptr || varSpec->micSpec.type != KitLib::MicSpec::Type::Direct) continue;
                if (seenCodes.insert(varSpec->micSpec.raw).second)
                    result.push_back(varSpec->micSpec);
            }
        }
    }

    return result;
}

PadContextPopup::MicRoutingDisplay PadContextPopup::buildMicRoutingDisplay(const juce::String& sourcePath, const juce::String& micCode) const {
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
        if (candidate.id == this->slotId) {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr) {
        display.routeText = "No route";
        display.tooltip = "Slot not found: " + this->slotId;
        display.hasProblem = true;
        return display;
    }

    const auto* layoutMic = this->findLayoutMicForSlot(micCode);
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

std::vector<PadContextPopup::MicRoutingChoice> PadContextPopup::buildMicRoutingChoices(const juce::String& sourcePath, const juce::String& micCode) const {
    std::vector<MicRoutingChoice> choices;

    const auto currentDisplay = this->buildMicRoutingDisplay(sourcePath, micCode);
    choices.push_back({ "-", "Unassigned from output", "-", -1, false, false });

    const auto* kitLayout = this->engine.getKitLayout();
    const auto* micLayout = this->engine.getMicLayout();
    if (kitLayout == nullptr || micLayout == nullptr) {
        choices.push_back({ "No route", currentDisplay.tooltip.isNotEmpty() ? currentDisplay.tooltip : juce::String("No active mic layout"), {}, 0, true, true, true });
        return choices;
    }

    const KitLayout::InstrumentSlot* slot = nullptr;
    for (const auto& candidate : kitLayout->instrumentSlots) {
        if (candidate.id == this->slotId) {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr) {
        choices.push_back({ "No route", "Slot not found: " + this->slotId, {}, 0, true, true, true });
        return choices;
    }

    const auto* layoutMic = this->findLayoutMicForSlot(micCode);
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

void PadContextPopup::updateRoutingComboDisplay(LayerRow::MicBleedControl& bleedCtrl) const {
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

std::vector<std::pair<juce::String, juce::String>> PadContextPopup::getRelevantMicsForSourceIndex(int sourceIndex) const {
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(this->sourceItems.size())) return {};
    return this->getRelevantMicsForSourcePath(this->sourceItems[static_cast<size_t>(sourceIndex)].fullPath());
}

void PadContextPopup::rebuildBleedControls(LayerRow* layer, const std::unordered_map<juce::String, float>& seedLevels, const std::unordered_map<juce::String, juce::String>& seedRoutes) {
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
        auto bleedCtrl = std::make_unique<LayerRow::MicBleedControl>();
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
        bleedCtrl->bleedPot->onDragEnd = [this, layer] {
            this->engine.playPreviewNote(this->slotId, layer->artName, this->getPreviewVelocity());
        };

        bleedCtrl->routingChoices = this->buildMicRoutingChoices(sourcePath, micCode);
        bleedCtrl->routingCombo = std::make_unique<juce::ComboBox>();
        UIHelpers::configureComboBox(*bleedCtrl->routingCombo);
        bleedCtrl->routingCombo->getProperties().set("compactRoute", true);
        bleedCtrl->routingCombo->getProperties().set("hideArrow", true);
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
            bleedCtrl->selectedOutputId = selectedChoice.usesAutomaticRouting ? juce::String() : selectedChoice.outputId;
        }
        bleedCtrl->routingCombo->onChange = [this, layer, bleedCtrlPtr] {
            this->applyRoutingComboChange(layer, bleedCtrlPtr);
        };
        this->updateRoutingComboDisplay(*bleedCtrl);
        this->layerContent.addAndMakeVisible(*bleedCtrl->routingCombo);
        layer->micBleeds.push_back(std::move(bleedCtrl));
    }
}

void PadContextPopup::addLayerRow(const juce::String& articulationName, const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs, float volumeDb, int pan, bool flipLR, bool flipLRExplicit, bool flipPhase, const std::optional<juce::String>& velocityResponse, const std::unordered_map<juce::String, float>& micBleedLevels, const std::unordered_map<juce::String, juce::String>& micRouteOverrides, int mappedLayerIndex) { //TODO: will it be better to replace multiple params for this with one structure?
    auto* layer = this->layerRows.add(new LayerRow());
    layer->artName = articulationName;
    layer->sourcePathHint = sourceFullPath;
    layer->selectedSourceIndex = this->findSourceIndex(sourceFullPath);
    layer->mappedLayerIndex = mappedLayerIndex;
    layer->velocityResponse = velocityResponse;

    // Source button (opens picker)
    layer->sourceButton = std::make_unique<juce::TextButton>();
    layer->sourceButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControlAlt);
    layer->sourceButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    this->layerContent.addAndMakeVisible(*layer->sourceButton);
    layer->sourceButton->onClick = [this, layer] { this->showSourcePicker(layer); };

    // Source detail label (kit / instrument / articulation)
    layer->sourceDetailLabel = std::make_unique<juce::Label>();
    layer->sourceDetailLabel->setFont(UITheme::Fonts::normal());
    layer->sourceDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
    layer->sourceDetailLabel->setJustificationType(juce::Justification::topLeft);
    layer->sourceDetailLabel->setInterceptsMouseClicks(false, false);
    this->layerContent.addAndMakeVisible(*layer->sourceDetailLabel);

    layer->velocityResponseMode = std::make_unique<juce::ComboBox>();
    UIHelpers::configureComboBox(*layer->velocityResponseMode);
    layer->velocityResponseMode->getProperties().set("compactRoute", true);
    layer->velocityResponseMode->getProperties().set("hideArrow", true);
    this->layerContent.addAndMakeVisible(*layer->velocityResponseMode);
    layer->velocityResponseMode->onChange = [this, layer] { this->commitVelocityResponse(layer); };
    this->rebuildVelocityResponseChoices(layer);

    layer->sourceSetupButton = std::make_unique<juce::TextButton>("Source setup...");
    UIHelpers::configureControlButton(*layer->sourceSetupButton);
    layer->sourceSetupButton->getProperties().set("compactText", true);
    layer->sourceSetupButton->setTooltip("Edit source ranges, balance, gain, and timing for every map using this articulation");
    layer->sourceSetupButton->onClick = [this, layer] { this->showSourceOverrideEditor(layer); };
    this->layerContent.addAndMakeVisible(*layer->sourceSetupButton);

    this->updateSourceDisplay(layer);

    // Tuning pot
    layer->tuningPot = std::make_unique<CompactParameterControl>("Tuning", -2400.0, 2400.0, 1.0, static_cast<double>(tuningCents));
    layer->tuningPot->setInteraction(0.0, 10.0, 1.0);
    this->layerContent.addAndMakeVisible(*layer->tuningPot);
    layer->tuningPot->onValueChange = [this, layer] { this->applyLayerParameterChange(layer, EditableLayerParameter::Tuning); };
    layer->tuningPot->onDragEnd = [this, layer] {
        this->engine.playPreviewNote(this->slotId, layer->artName, this->getPreviewVelocity());
    };

    // Attack pot
    layer->attackPot = std::make_unique<CompactParameterControl>("Attack", -1.0, 500.0, 1.0, attackMs >= 0 ? static_cast<double>(attackMs) : -1.0, UIHelpers::sentinelValueText);
    layer->attackPot->setInteraction(-1.0, 10.0, 1.0);
    this->layerContent.addAndMakeVisible(*layer->attackPot);
    layer->attackPot->onValueChange = [this, layer] { this->applyLayerParameterChange(layer, EditableLayerParameter::Attack); };
    layer->attackPot->onDragEnd = [this, layer] {
        this->engine.playPreviewNote(this->slotId, layer->artName, this->getPreviewVelocity());
    };

    // Decay pot
    layer->decayPot = std::make_unique<CompactParameterControl>("Decay", -1.0, 5000.0, 1.0, decayMs >= 0 ? static_cast<double>(decayMs) : -1.0, UIHelpers::sentinelValueText);
    layer->decayPot->setInteraction(-1.0, 10.0, 1.0);
    this->layerContent.addAndMakeVisible(*layer->decayPot);
    layer->decayPot->onValueChange = [this, layer] { this->applyLayerParameterChange(layer, EditableLayerParameter::Decay); };
    layer->decayPot->onDragEnd = [this, layer] {
        this->engine.playPreviewNote(this->slotId, layer->artName, this->getPreviewVelocity());
    };

    // Gain pot (-60 to +12 dB)
    layer->gainPot = std::make_unique<CompactParameterControl>("Gain", -60.0, 12.0, 0.1, static_cast<double>(volumeDb),
        [](double value) -> juce::String {
            if (value <= -60.0) return "-inf";
            return juce::String(value, 1);
        });
    layer->gainPot->setInteraction(0.0, 0.5, 0.1);
    this->layerContent.addAndMakeVisible(*layer->gainPot);
    layer->gainPot->onValueChange = [this, layer] { this->applyLayerParameterChange(layer, EditableLayerParameter::Gain); };
    layer->gainPot->onDragEnd = [this, layer] {
        this->engine.playPreviewNote(this->slotId, layer->artName, this->getPreviewVelocity());
    };

    // Pan pot
    layer->panPot = std::make_unique<CompactParameterControl>("Pan", -127.0, 127.0, 1.0, static_cast<double>(pan), [](double value) -> juce::String {
        const int panValue = static_cast<int>(value);
        if (panValue == 0) return "C";
        return panValue < 0 ? "L" + juce::String(-panValue) : "R" + juce::String(panValue);
    });
    layer->panPot->setInteraction(0.0, 4.0, 1.0);
    this->layerContent.addAndMakeVisible(*layer->panPot);
    layer->panPot->onValueChange = [this, layer] { this->applyLayerParameterChange(layer, EditableLayerParameter::Pan); };
    layer->panPot->onDragEnd = [this, layer] {
        this->engine.playPreviewNote(this->slotId, layer->artName, this->getPreviewVelocity());
    };

    // Flip L/R button (toggle)
    layer->flipLRButton = std::make_unique<juce::TextButton>("L/R");
    UIHelpers::configureToggleButton(*layer->flipLRButton,
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
    UIHelpers::configureToggleButton(*layer->flipPhaseButton, juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    layer->flipPhaseButton->setToggleState(flipPhase, juce::dontSendNotification);
    this->layerContent.addAndMakeVisible(*layer->flipPhaseButton);
    layer->flipPhaseButton->onClick = [this, layer] {
        this->applyLayerParameterChange(layer, EditableLayerParameter::FlipPhase);
    };

    // Per-mic bleed controls
    this->rebuildBleedControls(layer, micBleedLevels, micRouteOverrides);

    // Remove button
    layer->removeButton = std::make_unique<juce::TextButton>(UITheme::Symbols::times); // remove
    layer->removeButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::transparent);
    layer->removeButton->setColour(juce::TextButton::textColourOffId, UITheme::Colours::accent);
    layer->removeButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    layer->removeButton->getProperties().set("compactText", true);
    layer->removeButton->setTooltip("Remove layer");
    this->layerContent.addAndMakeVisible(*layer->removeButton);
    layer->sourceButton->onStateChange = [this] { this->updateRemoveButtonVisibility(); };
    layer->removeButton->onStateChange = [this] { this->updateRemoveButtonVisibility(); };
    layer->removeButton->onClick = [this, layer] {
        juce::MessageManager::callAsync([this, layer] {
            const bool removedMappedLayer = layer->selectedSourceIndex >= 0;
            this->layerRows.removeObject(layer);
            this->updateRemoveButtonVisibility();
            this->resized();
            this->refreshLayerMappings();
            if (removedMappedLayer) this->applyToEngine();
        });
    };
}

void PadContextPopup::updateRemoveButtonVisibility() {
    for (auto* layer : this->layerRows) {
        int articulationLayerCount = 0;
        for (const auto* candidate : this->layerRows)
            if (candidate->artName == layer->artName) ++articulationLayerCount;
        const bool canRemove = articulationLayerCount > 1;
        layer->removeButton->setVisible(canRemove);
        layer->removeButton->setAlpha(canRemove && (layer->sourceButton->isMouseOver() || layer->removeButton->isMouseOver()) ? 1.0f : 0.0f);
    }
}

void PadContextPopup::updateSourceDisplay(LayerRow* layer) {
    if (layer->selectedSourceIndex >= 0) {
        const auto& src = this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)];
        const auto sourceArticulation = src.artCode.isNotEmpty() ? src.artCode : layer->artName;
        const auto detail = src.kitDisplayName + "\n" + src.instCode + "\n" + sourceArticulation;
        layer->sourceDetailLabel->setText(detail, juce::dontSendNotification);
        layer->sourceDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
        layer->sourceButton->setTooltip(src.fullPath());
    } else if (layer->sourcePathHint.isNotEmpty()) {
        layer->sourceDetailLabel->setText("Missing source\n" + layer->sourcePathHint, juce::dontSendNotification);
        layer->sourceDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::accentWarning);
        layer->sourceButton->setTooltip(layer->sourcePathHint);
    } else {
        layer->sourceDetailLabel->setText("Select source", juce::dontSendNotification);
        layer->sourceDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::textMuted);
        layer->sourceButton->setTooltip({});
    }
    layer->sourceButton->setButtonText("");
    if (layer->sourceSetupButton != nullptr) layer->sourceSetupButton->setEnabled(layer->selectedSourceIndex >= 0 && this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)].rawArtCode.isNotEmpty());
}

int PadContextPopup::getLayerIndex(const LayerRow* layer) const {
    if (layer == nullptr) return -1;
    if (layer->selectedSourceIndex < 0) return -1;

    int mappedLayerIndex = 0;
    for (int idx = 0; idx < this->layerRows.size(); ++idx) {
        const auto* currentLayer = this->layerRows[idx];
        if (currentLayer->artName != layer->artName) continue;
        if (currentLayer->selectedSourceIndex < 0) continue;
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

void PadContextPopup::rebuildVelocityResponseChoices(LayerRow* layer) {
    if (layer == nullptr || layer->velocityResponseMode == nullptr) return;

    layer->velocityResponseMode->clear(juce::dontSendNotification);
    layer->velocityResponseMode->addItem("Use Global", velocityResponseGlobalItemId);
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

void PadContextPopup::commitVelocityResponse(LayerRow* layer) {
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

void PadContextPopup::updateVelocityResponseDisplay(LayerRow* layer) {
    if (layer == nullptr || layer->velocityResponseMode == nullptr) return;
    const bool layerOverride = layer->velocityResponse.has_value();
    int selectedItemId = velocityResponseGlobalItemId;
    bool unnamedLayerCurve = false;
    juce::String tooltip = "Use global: " + this->engine.getVelocityResponse().getDefinition();
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
        layer->velocityResponseMode->setText("Unnamed layer curve*", juce::dontSendNotification);
    } else {
        layer->velocityResponseMode->setSelectedId(selectedItemId, juce::dontSendNotification);
    }
    layer->velocityResponseMode->setTooltip(tooltip);
    layer->velocityResponseMode->setColour(juce::ComboBox::textColourId, layerOverride ? UITheme::Colours::textAccent : UITheme::Colours::textSecondary);
}

void PadContextPopup::applyLayerParameterChange(LayerRow* layer, EditableLayerParameter parameter, const juce::String& micCode) {
    if (this->suppressParameterCallbacks || layer == nullptr) return;
    if (layer->mappedLayerIndex < 0 || layer->selectedSourceIndex < 0) {
        this->mapTextDirty = true;
        return;
    }

    const auto currentMapText = this->savedInstrumentMapText.isNotEmpty() ? this->savedInstrumentMapText : this->engine.getSuggestedInstrumentMapText();
    auto parsedMap = DrumKitEngine::parseInstrumentMapText(currentMapText);
    const auto* layout = this->engine.getKitLayout();
    if (!parsedMap.ok() || layout == nullptr) return;

    auto projections = DrumKitEngineMapHelpers::projectDestinationLayers(parsedMap.value, *layout);
    auto findSourceProjection = [this, layer](const auto& candidateProjections) -> const DrumKitEngineMapHelpers::InstrumentMapLayerProjection* {
        for (const auto& projection : candidateProjections) {
            if (projection.slotId.compareIgnoreCase(this->slotId) != 0 || projection.articulationName.compareIgnoreCase(layer->artName) != 0 || projection.layerIndex != layer->mappedLayerIndex) continue;
            return &projection;
        }
        return nullptr;
    };

    const auto* sourceProjection = findSourceProjection(projections);
    if (sourceProjection == nullptr) return;

    const bool linkArts = this->linkArtsButton.getToggleState() && parameter != EditableLayerParameter::VelocityResponse;
    const bool linkTomBleed = parameter == EditableLayerParameter::Bleed && this->linkTomBleedButton.isVisible() && this->linkTomBleedButton.getToggleState();
    if (!linkArts && !linkTomBleed) {
        const auto slotIterator = std::find_if(layout->instrumentSlots.begin(), layout->instrumentSlots.end(), [this](const auto& slot) {
            return slot.id.compareIgnoreCase(this->slotId) == 0;
        });
        if (slotIterator != layout->instrumentSlots.end()) {
            DrumKitEngineMapHelpers::isolateEntryForDestinationArticulation(parsedMap.value, *slotIterator, sourceProjection->entryIndex, layer->artName);
            projections = DrumKitEngineMapHelpers::projectDestinationLayers(parsedMap.value, *layout);
            sourceProjection = findSourceProjection(projections);
            if (sourceProjection == nullptr) return;
        }
    }

    auto targetProjections = DrumKitEngineMapHelpers::collectLinkedParameterTargets(projections, this->slotId, layer->artName, layer->mappedLayerIndex, linkArts, linkTomBleed);

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
        std::erase_if(targetProjections, [this, &parsedMap, &micCode](const auto& projection) {
            if (projection.entryIndex >= parsedMap.value.entries.size()) return true;
            const auto& entry = parsedMap.value.entries[projection.entryIndex];
            juce::String sourcePath = entry.kitRef;
            if (entry.instCode.isNotEmpty()) sourcePath += "/" + entry.instCode;
            auto sourceArticulation = DrumKitEngineMapHelpers::sourceArticulationForDestination(entry, projection.articulationName);
            if (sourceArticulation.isEmpty() && projection.articulationName != "(default)") sourceArticulation = projection.articulationName;
            if (sourceArticulation.isNotEmpty()) sourcePath += "/" + sourceArticulation;
            const auto relevantMics = this->getRelevantMicsForSourcePath(sourcePath);
            return std::none_of(relevantMics.begin(), relevantMics.end(), [&micCode](const auto& mic) {
                return mic.first.compareIgnoreCase(micCode) == 0;
            });
        });
    }
    if (targetProjections.empty()) return;

    std::unordered_set<size_t> updatedEntryIndexes;
    for (const auto& projection : targetProjections) {
        if (projection.entryIndex >= parsedMap.value.entries.size() || !updatedEntryIndexes.insert(projection.entryIndex).second) continue;
        auto& entry = parsedMap.value.entries[projection.entryIndex];
        switch (parameter) {
            case EditableLayerParameter::Tuning:
                if (integerValue == 0) entry.tuningCents.reset();
                else entry.tuningCents = integerValue;
                break;
            case EditableLayerParameter::Attack:
                if (integerValue < 0) entry.attackTimeMs.reset();
                else entry.attackTimeMs = integerValue;
                break;
            case EditableLayerParameter::Decay:
                if (integerValue < 0) entry.decayTimeMs.reset();
                else entry.decayTimeMs = integerValue;
                break;
            case EditableLayerParameter::Gain:
                if (std::abs(floatValue) <= 0.01f) entry.volumeDb.reset();
                else entry.volumeDb = floatValue;
                break;
            case EditableLayerParameter::Pan:
                if (integerValue == 0) entry.pan.reset();
                else entry.pan = integerValue;
                break;
            case EditableLayerParameter::FlipLR:
                entry.flipLR = boolValue;
                entry.flipLRExplicit = true;
                break;
            case EditableLayerParameter::FlipPhase:
                entry.flipPhase = boolValue;
                break;
            case EditableLayerParameter::Bleed: {
                for (auto levelIterator = entry.micBleedLevels.begin(); levelIterator != entry.micBleedLevels.end();) {
                    if (levelIterator->first.compareIgnoreCase(micCode) == 0) levelIterator = entry.micBleedLevels.erase(levelIterator);
                    else ++levelIterator;
                }
                if (bleedValue < 0.99f) entry.micBleedLevels[micCode] = bleedValue;
                break;
            }
            case EditableLayerParameter::VelocityResponse:
                entry.velocityResponse = layer->velocityResponse;
                break;
        }
    }

    this->suppressParameterCallbacks = true;
    for (const auto& projection : targetProjections) {
        if (projection.slotId.compareIgnoreCase(this->slotId) != 0) continue;
        auto* targetLayer = static_cast<LayerRow*>(nullptr);
        for (auto* candidateLayer : this->layerRows) {
            if (candidateLayer != nullptr && candidateLayer->artName.compareIgnoreCase(projection.articulationName) == 0 && candidateLayer->mappedLayerIndex == projection.layerIndex) {
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
    for (const auto& projection : targetProjections) runtimeTargets.push_back({ projection.slotId, projection.articulationName, projection.layerIndex, runtimeUpdate });
    this->engine.updateSlotParametersBatch(runtimeTargets);

    this->savedInstrumentMapText = DrumKitEngine::serializeInstrumentMap(parsedMap.value);
    this->mapTextDirty = false;
    this->refreshLayerMappings();
    if (this->onMapTextChanged) this->onMapTextChanged(this->savedInstrumentMapText);
}
// Generate map text: rebuilds this slot while preserving every other slot.

juce::String PadContextPopup::generateFullMapText() const {
    auto currentMapText = this->savedInstrumentMapText.isNotEmpty() ? this->savedInstrumentMapText : this->engine.getSuggestedInstrumentMapText();
    auto parseResult = DrumKitEngine::parseInstrumentMapText(currentMapText);
    if (!parseResult.ok()) return currentMapText;

    auto& entries = parseResult.value.entries;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [this](const DrumKitEngine::InstrumentMapEntry& entry) { return entry.slotId.compareIgnoreCase(this->slotId) == 0; }), entries.end());

    for (const auto* layer : this->layerRows) {
        if (layer->selectedSourceIndex < 0) continue;

        const auto& srcItem = this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)];
        DrumKitEngine::InstrumentMapEntry entry;
        entry.slotId = this->slotId;
        if (layer->artName != "(default)") entry.slotArticulationFilter.push_back(layer->artName);
        entry.kitRef = srcItem.kitCodeRaw;
        entry.instCode = srcItem.instCode;
        if (srcItem.artCode.isNotEmpty()) entry.articulationFilter.push_back(srcItem.artCode);

        const int tuning = static_cast<int>(layer->tuningPot->getValue());
        if (tuning != 0) entry.tuningCents = tuning;
        const int attack = static_cast<int>(layer->attackPot->getValue());
        if (attack >= 0) entry.attackTimeMs = attack;
        const int decay = static_cast<int>(layer->decayPot->getValue());
        if (decay >= 0) entry.decayTimeMs = decay;
        const float volume = static_cast<float>(layer->gainPot->getValue());
        if (std::abs(volume) > 0.01f) entry.volumeDb = volume;
        const int pan = static_cast<int>(layer->panPot->getValue());
        if (pan != 0) entry.pan = pan;
        entry.flipLR = layer->flipLRButton->getToggleState();
        entry.flipLRExplicit = entry.flipLR || layer->flipLRExplicit;
        entry.flipPhase = layer->flipPhaseButton->getToggleState();
        entry.velocityResponse = layer->velocityResponse;

        for (const auto& bleedCtrl : layer->micBleeds) {
            if (bleedCtrl == nullptr || bleedCtrl->micCode.isEmpty()) continue;
            const float bleed = static_cast<float>(bleedCtrl->bleedPot->getValue() / 100.0);
            if (bleed < 0.99f) entry.micBleedLevels[bleedCtrl->micCode] = bleed;
            if (bleedCtrl->selectedOutputId.isNotEmpty()) entry.micRouteOverrides[bleedCtrl->micCode] = bleedCtrl->selectedOutputId;
        }
        entries.push_back(std::move(entry));
    }

    return this->engine.normalizeInstrumentMapText(DrumKitEngine::serializeInstrumentMap(parseResult.value));
}

void PadContextPopup::applyRoutingComboChange(LayerRow* layer, LayerRow::MicBleedControl* bleedCtrl) {
    if (layer == nullptr || bleedCtrl == nullptr || bleedCtrl->routingCombo == nullptr) return;

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

    const int layerIndex = layer->mappedLayerIndex >= 0 ? layer->mappedLayerIndex : this->getLayerIndex(layer);
    this->engine.logMessage("Pad popup route: slot=" + this->slotId
        + " art=" + layer->artName
        + " layer=" + juce::String(layerIndex + 1)
        + " sourceMic=" + bleedCtrl->micCode
        + " old=" + (oldRoute.isNotEmpty() ? oldRoute : juce::String("auto"))
        + " new=" + (newRoute.isNotEmpty() ? newRoute : juce::String("auto")), DebugLevel::Debug);
    if (this->engine.updateSlotMicRoute(this->slotId, layer->artName, layerIndex, bleedCtrl->micCode, newRoute)) {
        this->engine.logMessage("Mic route change SUCCESS: slot=" + this->slotId + " art=" + layer->artName + " source=" + bleedCtrl->micCode + " target=" + (newRoute.isNotEmpty() ? newRoute : juce::String("auto")) + " mode=live", DebugLevel::Debug);
        this->mapTextDirty = true;
        this->flushMapText();
        return;
    }
    this->engine.logMessage("Mic route live update FAILED: slot=" + this->slotId + " art=" + layer->artName + " source=" + bleedCtrl->micCode + " target=" + (newRoute.isNotEmpty() ? newRoute : juce::String("auto")) + " fallback=full apply", DebugLevel::Debug);
    const bool fallbackApplied = this->applyToEngine();
    this->engine.logMessage("Mic route fallback " + juce::String(fallbackApplied ? "SUCCESS" : "FAILED") + ": slot=" + this->slotId + " art=" + layer->artName + " source=" + bleedCtrl->micCode, DebugLevel::Debug);
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
    const auto projections = DrumKitEngineMapHelpers::projectDestinationLayers(parsedMap.value, *layout);

    for (const auto* group : this->articulationGroups) {
        std::vector<LayerRow*> mappedRows;
        for (auto* layer : this->layerRows)
            if (layer != nullptr && layer->artName.compareIgnoreCase(group->artName) == 0 && layer->selectedSourceIndex >= 0) mappedRows.push_back(layer);

        for (const auto& projection : projections) {
            if (projection.slotId.compareIgnoreCase(this->slotId) != 0 || projection.articulationName.compareIgnoreCase(group->artName) != 0) continue;
            if (projection.layerIndex < 0 || projection.layerIndex >= static_cast<int>(mappedRows.size())) continue;
            auto* mappedRow = mappedRows[static_cast<size_t>(projection.layerIndex)];
            mappedRow->mappedLayerIndex = projection.layerIndex;
        }
    }
}

void PadContextPopup::rebuildLayerRows() {
    this->layerRows.clear(true);
    this->populateLayers();
    this->resized();
}

void PadContextPopup::flushMapText() {
    if (!this->mapTextDirty) return;

    auto text = this->generateFullMapText();
    if (text.trim().isEmpty()) return;

    this->savedInstrumentMapText = text;
    this->mapTextDirty = false;

    if (this->onMapTextChanged) this->onMapTextChanged(text);
}

void PadContextPopup::showVelocityResponseEditor(LayerRow* layer) {
    if (layer == nullptr) return;
    this->dismissSourcePicker();
    this->dismissSourceOverrideEditor();
    this->dismissVelocityResponseEditor();

    juce::String sourcePath = layer->sourcePathHint;
    juce::String sourceDisplay = sourcePath.isNotEmpty() ? sourcePath : juce::String("No source selected");
    if (layer->selectedSourceIndex >= 0 && layer->selectedSourceIndex < static_cast<int>(this->sourceItems.size())) {
        const auto& source = this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)];
        sourcePath = source.fullPath();
        sourceDisplay = source.displayText();
    }

    this->activeVelocityResponseEditor = std::make_unique<VelocityResponseEditor>(this->engine);
    VelocityResponseEditor::LayerData data;
    data.title = layer->artName + " | " + sourceDisplay;
    data.customDefinition = layer->velocityResponse;
    data.sourceInfo = this->engine.getSourceVelocityResponseInfo(sourcePath);
    this->activeVelocityResponseEditor->setLayerData(std::move(data));
    this->activeVelocityResponseEditor->onResponseChanged = [this, layer](const std::optional<juce::String>& definition, bool commit) {
        layer->velocityResponse = definition;
        this->updateVelocityResponseDisplay(layer);
        this->applyLayerParameterChange(layer, EditableLayerParameter::VelocityResponse);
        if (commit) this->flushMapText();
    };
    this->activeVelocityResponseEditor->onPreview = [this, layer](int velocity) {
        this->engine.playPreviewNote(this->slotId, layer->artName, static_cast<juce::uint8>(juce::jlimit(1, 127, velocity)));
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
        const int editorWidth = this->getWidth() * 9 / 10;
        const int editorHeight = this->getHeight() * 17 / 20;
        this->activeVelocityResponseEditor->setBounds(this->getLocalBounds().withSizeKeepingCentre(editorWidth, editorHeight));
    }
}

void PadContextPopup::showSourceOverrideEditor(LayerRow* layer) {
    if (layer == nullptr || layer->selectedSourceIndex < 0 || layer->selectedSourceIndex >= static_cast<int>(this->sourceItems.size())) return;
    const auto& source = this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)];
    if (source.rawArtCode.isEmpty()) return;
    this->dismissSourcePicker();
    this->dismissVelocityResponseEditor();
    this->dismissSourceOverrideEditor();

    this->activeSourceOverrideEditor = std::make_unique<SourceArticulationOverrideEditor>(this->engine);
    const auto displayTitle = source.kitDisplayName + " / " + source.instCode + " / " + source.artCode;
    this->activeSourceOverrideEditor->setSource(source.rawKitCode, source.rawInstCode, source.rawArtCode, displayTitle);
    this->activeSourceOverrideEditor->onPreview = [this, layer, source](int velocity, int namedVelocity, const juce::String& roundRobin, const juce::String& relativePath, const juce::String& micCode) {
        const auto previewVelocity = static_cast<juce::uint8>(juce::jlimit(1, 127, velocity));
        if (relativePath.isNotEmpty()) {
            if (micCode.isNotEmpty()) {
                this->engine.previewSourceItem(source.rawKitCode, source.rawInstCode, source.rawArtCode, previewVelocity, relativePath, micCode);
                return;
            }
            if (this->engine.playSourceLogicalHitPreview(this->slotId, layer->artName, source.rawKitCode, source.rawInstCode, source.rawArtCode, namedVelocity, roundRobin, previewVelocity)) return;
            this->engine.previewSourceItem(source.rawKitCode, source.rawInstCode, source.rawArtCode, previewVelocity, relativePath, micCode);
            return;
        }
        this->engine.playPreviewNote(this->slotId, layer->artName, previewVelocity);
    };
    this->activeSourceOverrideEditor->onDone = [this] {
        juce::MessageManager::callAsync([this] { this->dismissSourceOverrideEditor(); });
    };

    this->sourceOverrideOverlay = std::make_unique<DismissOverlay>();
    this->sourceOverrideOverlay->setDarkened(true);
    this->sourceOverrideOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissSourceOverrideEditor(); });
    };
    this->addAndMakeVisible(*this->sourceOverrideOverlay);
    this->sourceOverrideOverlay->toFront(false);
    this->addAndMakeVisible(*this->activeSourceOverrideEditor);
    this->activeSourceOverrideEditor->toFront(true);
    this->layoutSourceOverrideEditor();
}

void PadContextPopup::dismissSourceOverrideEditor() {
    if (this->sourceOverrideOverlay) {
        this->removeChildComponent(this->sourceOverrideOverlay.get());
        this->sourceOverrideOverlay.reset();
    }
    if (this->activeSourceOverrideEditor) {
        this->removeChildComponent(this->activeSourceOverrideEditor.get());
        this->activeSourceOverrideEditor.reset();
    }
}

void PadContextPopup::layoutSourceOverrideEditor() {
    if (this->sourceOverrideOverlay) this->sourceOverrideOverlay->setBounds(this->getLocalBounds());
    if (this->activeSourceOverrideEditor) {
        const int editorWidth = this->getWidth() * 9 / 10;
        const int editorHeight = this->getHeight() * 17 / 20;
        this->activeSourceOverrideEditor->setBounds(this->getLocalBounds().withSizeKeepingCentre(editorWidth, editorHeight));
    }
}

// Source picker

void PadContextPopup::showSourcePicker(LayerRow* layer) {
    this->dismissSourceOverrideEditor();
    this->dismissVelocityResponseEditor();
    this->dismissSourcePicker();

    this->activePicker = std::make_unique<SourcePickerPopup>(this->sourceItems, this->engine.getInstrumentCatalog());
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
    this->dismissSourceOverrideEditor();
    this->dismissVelocityResponseEditor();
    this->dismissSourcePicker();

    this->activePicker = std::make_unique<SourcePickerPopup>(this->sourceItems, this->engine.getInstrumentCatalog(), SourcePickerPopup::SelectionMode::WholeInstrument);
    this->activePicker->setFilterType(this->slotType);
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
            this->rebuildLayerRows();
            if (this->onMapTextChanged) this->onMapTextChanged(this->savedInstrumentMapText);
            if (this->onInstrumentChanged) this->onInstrumentChanged();
            if (this->onStatusMessage) this->onStatusMessage("Instrument replaced in " + this->slotId);
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
        if (ArticulationNameHelpers::articulationCodeMatches(layer->artName, this->artName)) break;
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

// Paint / Layout

void PadContextPopup::LayerContentComponent::paint(juce::Graphics& g) {
    if (this->layerRowsPtr == nullptr) return;
    g.setColour(UITheme::Colours::bgPanel);
    for (const auto* layer : *this->layerRowsPtr) {
        if (!layer->mainGroupBounds.isEmpty())
            g.fillRoundedRectangle(layer->mainGroupBounds.toFloat(), 4.0f);
        if (!layer->bleedGroupBounds.isEmpty())
            g.fillRoundedRectangle(layer->bleedGroupBounds.toFloat(), 4.0f);
    }
}

void PadContextPopup::paint(juce::Graphics& g) {
    const auto bounds = this->getLocalBounds().toFloat();
    juce::Path backgroundPath;
    backgroundPath.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), 6.0f, 6.0f, true, true, false, false);
    g.setColour(UITheme::Colours::bgPanelRaised);
    g.fillPath(backgroundPath);
}

void PadContextPopup::resized() {
    auto area = getLocalBounds().reduced(8);

    // Header row: slot | link controls | set instrument | MIDI controls
    auto headerRow = area.removeFromTop(24);

    // MIDI: right-aligned in header
    const int midiControlW = this->midiAssignmentControl ? this->midiAssignmentControl->getPreferredWidth() : 0;
    if (this->midiAssignmentControl) this->midiAssignmentControl->setBounds(headerRow.removeFromRight(midiControlW).reduced(0, 2));
    headerRow.removeFromRight(4);
    this->midiHeaderLabel.setBounds(headerRow.removeFromRight(32).reduced(0, 2));
    headerRow.removeFromRight(6);
    this->setInstrumentButton.setBounds(headerRow.removeFromRight(104).reduced(1, 2));
    headerRow.removeFromRight(4);
    if (this->linkTomBleedButton.isVisible()) {
        this->linkTomBleedButton.setBounds(headerRow.removeFromRight(112).reduced(1, 2));
        headerRow.removeFromRight(4);
    }
    this->linkArtsButton.setBounds(headerRow.removeFromRight(78).reduced(1, 2));
    headerRow.removeFromRight(6);

    // Slot label fills remaining left
    this->slotLabel.setBounds(headerRow);
    area.removeFromTop(6);

    // Viewport occupies remaining space
    this->layerViewport.setBounds(area);

    // Layer rows - single row per layer (compact parameter controls):
    // [Source Card with hover delete] | [Tuning|Attack|Decay + Curve|Gain|L/R+Phase] | [Bleed1+Route|Bleed2+Route|...]
    const int potW = 44;
    const int potH = 44;
    const int flipColW = 26;
    const int sourceW = UITheme::Sizes::sourceCardWidth;
    const int routingComboH = 14;
    const int bleedControlH = potH + routingComboH + 2;
    const int sourceControlH = 68;
    const int layerRowH = juce::jmax(sourceControlH, juce::jmax(potH, bleedControlH)) + 4;
    const int layerGap = 2;
    const int bleedPotW = 50;
    const int groupGap = 6;
    const int groupPad = 4;
    const int articulationHeaderH = UITheme::Spacing::rowNormal;
    const int compactButtonSize = UITheme::Sizes::compactButton;
    const int rightPadding = UITheme::Spacing::sm;

    const int contentHeight = this->articulationGroups.size() * articulationHeaderH + this->layerRows.size() * (layerRowH + layerGap);
    const bool needsVerticalScrollbar = contentHeight > area.getHeight();
    const int scrollbarWidth = needsVerticalScrollbar ? this->layerViewport.getScrollBarThickness() : 0;
    const int contentWidth = juce::jmax(0, area.getWidth() - scrollbarWidth - rightPadding);
    this->layerViewport.setScrollBarsShown(needsVerticalScrollbar, false);
    this->layerContent.setSize(contentWidth, juce::jmax(area.getHeight(), contentHeight));

    auto contentArea = this->layerContent.getLocalBounds();
    for (auto* group : this->articulationGroups) {
        auto articulationHeaderArea = contentArea.removeFromTop(articulationHeaderH);
        auto addLayerButtonArea = articulationHeaderArea.removeFromRight(compactButtonSize);
        group->addLayerButton.setBounds(addLayerButtonArea.withSizeKeepingCentre(compactButtonSize, compactButtonSize));
        articulationHeaderArea.removeFromRight(UITheme::Spacing::sm);
        group->label.setBounds(articulationHeaderArea);

        for (auto* layer : this->layerRows) {
            if (layer->artName != group->artName) continue;

            auto layerArea = contentArea.removeFromTop(layerRowH);
            auto rowArea = layerArea.reduced(0, 2);

            // Source card
            auto srcArea = rowArea.removeFromLeft(sourceW);
            layer->sourceButton->setBounds(srcArea);
            auto sourceInner = srcArea.reduced(6, 2).withTrimmedRight(compactButtonSize + UITheme::Spacing::sm);
            layer->sourceDetailLabel->setBounds(sourceInner);
            auto removeButtonArea = srcArea.reduced(UITheme::Spacing::xs);
            auto removeButtonRow = removeButtonArea.removeFromBottom(compactButtonSize);
            layer->removeButton->setBounds(removeButtonRow.removeFromRight(compactButtonSize));

            rowArea.removeFromLeft(groupGap);

            // Main params group: Tuning, Attack, Decay, Gain, Pan + L/R, phase
            const int mainGroupW = potW * 5 + flipColW + groupPad * 2;
            auto mainGroupArea = rowArea.removeFromLeft(mainGroupW);
            layer->mainGroupBounds = mainGroupArea;
            auto mainInner = mainGroupArea.reduced(groupPad, 0);
            auto mainParameterRow = mainInner.removeFromTop(potH);
            layer->tuningPot->setBounds(mainParameterRow.removeFromLeft(potW));
            layer->attackPot->setBounds(mainParameterRow.removeFromLeft(potW));
            layer->decayPot->setBounds(mainParameterRow.removeFromLeft(potW));
            layer->gainPot->setBounds(mainParameterRow.removeFromLeft(potW));
            layer->panPot->setBounds(mainParameterRow.removeFromLeft(potW));
            auto flipArea = mainParameterRow.removeFromLeft(flipColW);
            layer->flipLRButton->setBounds(flipArea.removeFromTop(potH / 2).reduced(2, 1));
            layer->flipPhaseButton->setBounds(flipArea.reduced(2, 1));
            auto secondaryControlRow = mainInner.removeFromTop(routingComboH);
            layer->velocityResponseMode->setBounds(secondaryControlRow.removeFromLeft(potW * 3).reduced(1, 0));
            layer->sourceSetupButton->setBounds(secondaryControlRow.reduced(1, 0));

            rowArea.removeFromLeft(groupGap);

            // Bleed group
            const int numBleeds = static_cast<int>(layer->micBleeds.size());
            if (numBleeds > 0) {
                const int bleedGroupW = juce::jmin(rowArea.getWidth(), numBleeds * bleedPotW + groupPad * 2);
                auto bleedGroupArea = rowArea.removeFromLeft(bleedGroupW);
                layer->bleedGroupBounds = bleedGroupArea;
                auto bleedInner = bleedGroupArea.reduced(groupPad, 0);
                const int actualBleedW = bleedInner.getWidth() / numBleeds;
                for (auto& bleedCtrl : layer->micBleeds) {
                    auto bleedArea = bleedInner.removeFromLeft(actualBleedW);
                    bleedCtrl->bleedPot->setBounds(bleedArea.removeFromTop(potH).withHeight(potH));
                    if (bleedCtrl->routingCombo != nullptr) bleedCtrl->routingCombo->setBounds(bleedArea.removeFromTop(routingComboH).reduced(1, 0));
                }
            } else {
                layer->bleedGroupBounds = {};
            }

            contentArea.removeFromTop(layerGap);
        }
    }
    this->layerContent.repaint();
    this->layoutSourcePicker();
    this->layoutVelocityResponseEditor();
    this->layoutSourceOverrideEditor();
}
