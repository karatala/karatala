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
            if (ArticulationNames::articulationCodeMatches(art.codeRaw, this->artName)) {
                art.notes = *this->midiNotesPtr;
                return;
            }
        }
        for (auto& art : slot.articulations) {
            if (ArticulationNames::baseArticulationMatches(art.codeRaw, this->artName)) {
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
        auto* newLayer = this->addMapLayerRow(group->artName, "", 0, -1, -1, 0.0f, 0, false, false, false, std::nullopt, {}, {}, -1);
        this->updateSourceCardButtonVisibility();
        this->resized();
        this->showSourcePicker(newLayer);
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
    this->linkArtsButton.setTooltip("Apply supported parameter and mic routing changes to the same layer across all articulations in this slot");
    this->addAndMakeVisible(this->linkArtsButton);

    UIHelpers::configureToggleButton(this->linkTomBleedButton);
    this->linkTomBleedButton.setColour(juce::TextButton::buttonOnColourId, UITheme::Colours::broadEditDanger);
    this->linkTomBleedButton.setColour(juce::TextButton::textColourOnId, UITheme::Colours::textPrimary);
    this->linkTomBleedButton.getProperties().set("broadEditMode", true);
    this->linkTomBleedButton.setTooltip("Apply the changed bleed mic level or route to every mapped layer in every Tom slot");
    this->linkTomBleedButton.setVisible(this->slotType == KitLib::InstrumentSpec::Type::Tom);
    this->addAndMakeVisible(this->linkTomBleedButton);

    UIHelpers::configureControlButton(this->setInstrumentButton);
    this->setInstrumentButton.setTooltip("Replace every map layer in this slot with one complete library instrument");
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
    this->dismissLibraryArticulationEditor();
    this->dismissVelocityResponseEditor();
    this->dismissSourcePicker();
}


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
    // [Source Card with hover setup/delete] | [Tuning|Attack|Decay + Curve|Gain|L/R+Phase] | [Bleed1+Route|Bleed2+Route|...]
    const int potW = 44;
    const int potH = 32;
    const int flipColW = 36;
    const int sourceW = UITheme::Sizes::sourceCardWidth;
    const int routingComboH = 14;
    const int layerControlH = potH + routingComboH;
    const int layerRowH = layerControlH + 4;
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
            auto sourceCardButtonArea = srcArea.reduced(UITheme::Spacing::xs);
            auto libraryArticulationEditorButtonRow = sourceCardButtonArea.removeFromTop(compactButtonSize);
            layer->libraryArticulationEditorButton->setBounds(libraryArticulationEditorButtonRow.removeFromRight(compactButtonSize));
            auto removeButtonRow = sourceCardButtonArea.removeFromBottom(compactButtonSize);
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
    this->layoutLibraryArticulationEditor();
}
