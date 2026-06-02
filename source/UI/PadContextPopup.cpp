#include "PadContextPopup.h"
#include "UIHelpers.h"
#include "UITheme.h"

// ─── MIDI utility ─────────────────────────────────────────────────────────────

// ─── MIDI note button helpers ─────────────────────────────────────────────────

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
    this->syncNoteButtons();
    if (this->onMidiMappingChanged)
        this->onMidiMappingChanged();
}

void PadContextPopup::midiLearnNoteReceived(int midiNote) {
    if (this->activeLearningSlot < 0) return;
    this->applyNoteToSlot(this->activeLearningSlot, midiNote);
    this->activeLearningSlot = -1;
    if (this->midiAssignmentControl)
        this->midiAssignmentControl->setLearningSlot(-1);
    if (this->onMidiLearnStateChanged)
        this->onMidiLearnStateChanged(false);
}
// ─── Source items ─────────────────────────────────────────────────────────────

void PadContextPopup::rebuildSourceItems() {
    this->sourceItems = UIHelpers::buildSourceItems(this->engine.getInstrumentManager());
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

// ─── Constructor ──────────────────────────────────────────────────────────────

PadContextPopup::PadContextPopup(DrumKitEngine& engine, const KitLayout::InstrumentSlot& slot, int artIdx, const juce::String& savedInstrumentMapText)
    : engine(engine)
    , slotId(slot.id)
    , slotType(slot.type)
    , savedInstrumentMapText(savedInstrumentMapText) {
    // Resolve articulation
    if (artIdx >= 0 && artIdx < static_cast<int>(slot.articulations.size())) {
        auto& art = const_cast<KitLayout::InstrumentSlot::ArticulationTrigger&>(slot.articulations[static_cast<size_t>(artIdx)]);
        this->artName = art.codeRaw.upToFirstOccurrenceOf("[", false, false);
        this->midiNotesPtr = &art.notes;
    } else if (!slot.articulations.empty()) {
        auto& art = const_cast<KitLayout::InstrumentSlot::ArticulationTrigger&>(slot.articulations[0]);
        this->artName = art.codeRaw.upToFirstOccurrenceOf("[", false, false);
        this->midiNotesPtr = &art.notes;
    } else {
        this->artName = "(default)";
    }

    // Header: "Kick / center"
    juce::String headerText = (slot.display.isNotEmpty() ? slot.display : slot.id) + " / " + this->artName;
    this->slotLabel.setText(headerText, juce::dontSendNotification);
    this->slotLabel.setFont(UITheme::Fonts::labelBoldMedium());
    this->slotLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    addAndMakeVisible(this->slotLabel);

    // MIDI inline
    this->midiHeaderLabel.setText("MIDI:", juce::dontSendNotification);
    this->midiHeaderLabel.setFont(UITheme::Fonts::small());
    this->midiHeaderLabel.setColour(juce::Label::textColourId, UITheme::Colours::textMuted);
    addAndMakeVisible(this->midiHeaderLabel);

    this->midiAssignmentControl = std::make_unique<MidiNoteAssignmentControl>();
    this->midiAssignmentControl->setLayoutMetrics({ 60, 18, 0, 6, 2 });
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

    // "+ Layer" button in header
    this->addLayerButton.setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    this->addLayerButton.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    addAndMakeVisible(this->addLayerButton);
    this->addLayerButton.onClick = [this] {
        this->addLayerRow("", 0, -1, -1, 0.0f, false, false, {});
        this->updateRemoveButtonVisibility();
        this->resized();
    };

    // Scrollable layer area
    this->layerContent.layerRowsPtr = &this->layerRows;
    this->layerViewport.setViewedComponent(&this->layerContent, false);
    this->layerViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(this->layerViewport);

    // Build source items and populate layers from existing instrument map
    this->rebuildSourceItems();
    this->populateLayers();

    // Set a non-zero fallback size; the editor will replace this with content-aware bounds.
    this->setSize(600, 400);
}

juce::Rectangle<int> PadContextPopup::getPreferredBounds(const juce::Rectangle<int>& availableBounds) const {
    constexpr int popupPadding = UITheme::Sizes::popupPadding;
    constexpr int headerHeight = 24;
    constexpr int headerGap = 6;
    constexpr int potWidth = UITheme::Sizes::potWidth;
    constexpr int flipColumnWidth = 26;
    constexpr int removeWidth = 16;
    constexpr int sourceWidth = UITheme::Sizes::sourceCardWidth;
    constexpr int bleedPotWidth = 50;
    constexpr int groupGap = 6;
    constexpr int groupPad = 4;

    const int layerRowHeight = UITheme::Sizes::layerRowHeight;
    const int mainGroupWidth = potWidth * 4 + flipColumnWidth + groupPad * 2;

    int maxBleedCount = 0;
    for (const auto* layer : this->layerRows)
        maxBleedCount = juce::jmax(maxBleedCount, static_cast<int>(layer->micBleeds.size()));

    const int bleedGroupWidth = maxBleedCount > 0 ? maxBleedCount * bleedPotWidth + groupPad * 2 : 0;
    const int preferredWidth = popupPadding * 2
                             + removeWidth
                             + sourceWidth
                             + mainGroupWidth
                             + bleedGroupWidth
                             + groupGap * 2;
    const int preferredHeight = popupPadding * 2
                              + headerHeight
                              + headerGap
                              + 5 * layerRowHeight;

    const int width = juce::jlimit(420, availableBounds.getWidth(), preferredWidth);
    const int height = juce::jlimit(220, availableBounds.getHeight(), preferredHeight);
    return juce::Rectangle<int>(width, height).withCentre(availableBounds.getCentre());
}

void PadContextPopup::populateLayers() {
    // Parse the saved instrument map to find layers for this slot/art
    // Use saved map if provided, otherwise fall back to suggested map
    auto mapText = this->savedInstrumentMapText.isNotEmpty() ? this->savedInstrumentMapText : this->engine.getSuggestedInstrumentMapText();
    auto parseResult = DrumKitEngine::parseInstrumentMapText(mapText);
    const auto& existingEntries = parseResult.value.entries;

    // Count how many entries for the same slot come before this art group.
    // This offset aligns the popup's layer indices with the engine's per-slot synth order.
    this->layerIndexOffset = 0;
    bool foundFirstMatch = false;
    std::vector<const DrumKitEngine::InstrumentMapEntry*> matchingEntries;
    for (const auto& entry : existingEntries) {
        if (entry.slotId != this->slotId) continue;
        bool matchesArt = entry.slotArticulationFilter.empty();
        if (!matchesArt) {
            for (const auto& filter : entry.slotArticulationFilter)
                if (filter == this->artName) matchesArt = true;
        }
        if (matchesArt) {
            matchingEntries.push_back(&entry);
            foundFirstMatch = true;
        } else if (!foundFirstMatch) {
            ++this->layerIndexOffset;
        }
    }

    // Fallback: entries with no art filter for this slot
    if (matchingEntries.empty()) {
        for (const auto& entry : existingEntries) {
            if (entry.slotId == this->slotId && entry.slotArticulationFilter.empty())
                matchingEntries.push_back(&entry);
        }
    }

    if (matchingEntries.empty()) {
        this->addLayerRow("", 0, -1, -1, 0.0f, false, false, {});
    } else {
        for (const auto* entry : matchingEntries) {
            juce::String sourceFullPath = entry->kitRef;
            if (entry->instCode.isNotEmpty())
                sourceFullPath += "/" + entry->instCode;
            if (!entry->articulationFilter.empty())
                sourceFullPath += "/" + entry->articulationFilter[0];

            int tuning = entry->tuningCents.value_or(0);
            int attack = entry->attackTimeMs.value_or(-1);
            int decay = entry->decayTimeMs.value_or(-1);
            float volume = entry->volumeDb.value_or(0.0f);
            this->addLayerRow(sourceFullPath, tuning, attack, decay, volume, entry->flipLR, entry->flipPhase, entry->micBleedLevels);
        }
    }

    this->updateRemoveButtonVisibility();
}

std::vector<std::pair<juce::String, juce::String>> PadContextPopup::getRelevantMics() const {
    std::vector<std::pair<juce::String, juce::String>> result;
    auto* micLayout = this->engine.getMicLayout();
    if (micLayout == nullptr)
        return result;
    for (const auto& mic : micLayout->mics) {
        if (mic.instrument.isEmpty() || mic.instrument == this->slotId)
            result.push_back({ mic.code, mic.name });
    }
    return result;
}

void PadContextPopup::addLayerRow(const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs,
                                   float volumeDb, bool flipLR, bool flipPhase,
                                   const std::unordered_map<juce::String, float>& micBleedLevels) {
    auto* layer = this->layerRows.add(new LayerRow());
    layer->sourcePathHint = sourceFullPath;
    layer->selectedSourceIndex = this->findSourceIndex(sourceFullPath);

    // Source button (opens picker)
    layer->sourceButton = std::make_unique<juce::TextButton>();
    layer->sourceButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControlAlt);
    layer->sourceButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    this->layerContent.addAndMakeVisible(*layer->sourceButton);
    layer->sourceButton->onClick = [this, layer] { this->showSourcePicker(layer); };

    // Source detail label (multi-line: kit / inst / art)
    layer->sourceDetailLabel = std::make_unique<juce::Label>();
    layer->sourceDetailLabel->setFont(UITheme::Fonts::tableSmall());
    layer->sourceDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
    layer->sourceDetailLabel->setJustificationType(juce::Justification::topLeft);
    layer->sourceDetailLabel->setInterceptsMouseClicks(false, false);
    this->layerContent.addAndMakeVisible(*layer->sourceDetailLabel);
    this->updateSourceDisplay(layer);

    // Tuning pot
    layer->tuningPot = std::make_unique<ParameterPot>("Tuning", -2400.0, 2400.0, 10.0, static_cast<double>(tuningCents));
    this->layerContent.addAndMakeVisible(*layer->tuningPot);
    layer->tuningPot->onValueChange = [this, layer] { this->applyLayerParametersFast(layer); };
    layer->tuningPot->onDragEnd = [this] {
        this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
    };

    // Attack pot
    layer->attackPot = std::make_unique<ParameterPot>("Attack", -1.0, 500.0, 10.0,
        attackMs >= 0 ? static_cast<double>(attackMs) : -1.0, UIHelpers::sentinelValueText);
    this->layerContent.addAndMakeVisible(*layer->attackPot);
    layer->attackPot->onValueChange = [this, layer] { this->applyLayerParametersFast(layer); };
    layer->attackPot->onDragEnd = [this] {
        this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
    };

    // Decay pot
    layer->decayPot = std::make_unique<ParameterPot>("Decay", -1.0, 5000.0, 10.0,
        decayMs >= 0 ? static_cast<double>(decayMs) : -1.0, UIHelpers::sentinelValueText);
    this->layerContent.addAndMakeVisible(*layer->decayPot);
    layer->decayPot->onValueChange = [this, layer] { this->applyLayerParametersFast(layer); };
    layer->decayPot->onDragEnd = [this] {
        this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
    };

    // Gain pot (-60 to +12 dB)
    layer->gainPot = std::make_unique<ParameterPot>("Gain", -60.0, 12.0, 0.5, static_cast<double>(volumeDb),
        [](double value) -> juce::String {
            if (value <= -60.0) return "-inf";
            return juce::String(value, 1);
        });
    this->layerContent.addAndMakeVisible(*layer->gainPot);
    layer->gainPot->onValueChange = [this, layer] { this->applyLayerParametersFast(layer); };
    layer->gainPot->onDragEnd = [this] {
        this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
    };

    // Flip L/R button (toggle)
    layer->flipLRButton = std::make_unique<juce::TextButton>("L/R");
    UIHelpers::configureToggleButton(*layer->flipLRButton,
                                     juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    layer->flipLRButton->setToggleState(flipLR, juce::dontSendNotification);
    this->layerContent.addAndMakeVisible(*layer->flipLRButton);
    layer->flipLRButton->onClick = [this] {
        this->mapTextDirty = true;
        this->applyToEngine();
    };

    // Flip Phase button (toggle)
    layer->flipPhaseButton = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xc3\x98")); // Ø
    UIHelpers::configureToggleButton(*layer->flipPhaseButton,
                                     juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    layer->flipPhaseButton->setToggleState(flipPhase, juce::dontSendNotification);
    this->layerContent.addAndMakeVisible(*layer->flipPhaseButton);
    layer->flipPhaseButton->onClick = [this] {
        this->mapTextDirty = true;
        this->applyToEngine();
    };

    // Per-mic bleed as ParameterPots
    auto relevantMics = this->getRelevantMics();
    for (const auto& [micCode, micName] : relevantMics) {
        auto bleedCtrl = std::make_unique<LayerRow::MicBleedControl>();
        bleedCtrl->micCode = micCode;
        bleedCtrl->micName = micName;
        float level = 1.0f;
        auto it = micBleedLevels.find(micCode);
        if (it != micBleedLevels.end())
            level = it->second;
        bleedCtrl->bleedPot = std::make_unique<ParameterPot>(micName, 0.0, 100.0, 1.0,
            static_cast<double>(level * 100.0f));
        this->layerContent.addAndMakeVisible(*bleedCtrl->bleedPot);
        bleedCtrl->bleedPot->onValueChange = [this] {
            this->mapTextDirty = true;
        };
        layer->micBleeds.push_back(std::move(bleedCtrl));
    }

    // Remove button (×)
    layer->removeButton = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xc3\x97")); // ×
    layer->removeButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::transparent);
    layer->removeButton->setColour(juce::TextButton::textColourOffId, UITheme::Colours::textMuted);
    layer->removeButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    this->layerContent.addAndMakeVisible(*layer->removeButton);
    layer->removeButton->onClick = [this, layer] {
        juce::MessageManager::callAsync([this, layer] {
            this->layerRows.removeObject(layer);
            this->updateRemoveButtonVisibility();
            this->resized();
            this->applyToEngine();
        });
    };
}

void PadContextPopup::updateRemoveButtonVisibility() {
    bool showRemove = this->layerRows.size() > 1;
    for (auto* layer : this->layerRows)
        layer->removeButton->setVisible(showRemove);
}

void PadContextPopup::updateSourceDisplay(LayerRow* layer) {
    if (layer->selectedSourceIndex >= 0) {
        const auto& src = this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)];
        juce::String line1 = src.kitDisplayName + "  " + juce::String::charToString(0x25BC);
        juce::String detail = line1 + "\n" + src.instCode;
        if (src.artCode.isNotEmpty())
            detail += "\n" + src.artCode;
        layer->sourceDetailLabel->setText(detail, juce::dontSendNotification);
        layer->sourceDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
        layer->sourceButton->setTooltip(src.fullPath());
    } else if (layer->sourcePathHint.isNotEmpty()) {
        layer->sourceDetailLabel->setText("Missing source\n" + layer->sourcePathHint, juce::dontSendNotification);
        layer->sourceDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::accentWarning);
        layer->sourceButton->setTooltip(layer->sourcePathHint);
    } else {
        layer->sourceDetailLabel->setText("Select source\n" + juce::String::charToString(0x25BC), juce::dontSendNotification);
        layer->sourceDetailLabel->setColour(juce::Label::textColourId, UITheme::Colours::textMuted);
        layer->sourceButton->setTooltip({});
    }
    layer->sourceButton->setButtonText("");
}

int PadContextPopup::getLayerIndex(const LayerRow* layer) const {
    if (layer == nullptr)
        return -1;
    if (layer->selectedSourceIndex < 0)
        return -1;

    int mappedLayerIndex = 0;
    for (int idx = 0; idx < this->layerRows.size(); ++idx) {
        const auto* currentLayer = this->layerRows[idx];
        if (currentLayer->selectedSourceIndex < 0)
            continue;

        if (currentLayer == layer)
            return mappedLayerIndex;

        ++mappedLayerIndex;
    }

    return -1;
}

void PadContextPopup::applyLayerParametersFast(LayerRow* layer) {
    if (layer == nullptr)
        return;

    const int layerIndex = this->getLayerIndex(layer);
    if (layerIndex < 0)
        return;

    const int tuning = static_cast<int>(layer->tuningPot->getValue());
    const int attack = static_cast<int>(layer->attackPot->getValue());
    const int decay = static_cast<int>(layer->decayPot->getValue());

    this->engine.updateSlotParameters(this->slotId,
                                      this->artName,
                                      this->layerIndexOffset + layerIndex,
                                      tuning,
                                      attack,
                                      decay);
    this->mapTextDirty = true;
}

// ─── Generate map text: rebuilds the FULL instrument map, replacing only this slot/art's entries ──

juce::String PadContextPopup::generateFullMapText() const {
    // Start with current map, remove lines for this slot/art, then append our layers
    auto currentMapText = this->savedInstrumentMapText.isNotEmpty() ? this->savedInstrumentMapText : this->engine.getSuggestedInstrumentMapText();
    juce::StringArray existingLines;
    existingLines.addTokens(currentMapText, "\n", "");

    juce::StringArray result;
    juce::String slotArtPrefix1 = this->slotId + "/" + this->artName + ":";
    juce::String slotArtPrefix2 = this->slotId + ":";

    for (const auto& line : existingLines) {
        auto trimmed = line.trim();
        if (trimmed.isEmpty()) continue;

        // Remove lines matching this slot/art
        if (trimmed.startsWith(slotArtPrefix1)) continue;
        // For (default) articulation or unfiltered entries: remove if matches slot
        if (this->artName == "(default)" && trimmed.startsWith(slotArtPrefix2) && !trimmed.substring(slotArtPrefix2.length()).contains("/"))
            continue;
        // Also remove lines that match this slot with exact art filter
        // Parse the slot part to check
        auto colonPos = trimmed.indexOf(":");
        if (colonPos > 0) {
            auto slotPart = trimmed.substring(0, colonPos);
            auto slashPos = slotPart.indexOf("/");
            if (slashPos > 0) {
                auto lineSlotId = slotPart.substring(0, slashPos);
                auto lineArt = slotPart.substring(slashPos + 1);
                if (lineSlotId == this->slotId && lineArt == this->artName)
                    continue;
            }
        }
        result.add(trimmed);
    }

    // Now add our layers
    for (const auto* layer : this->layerRows) {
        if (layer->selectedSourceIndex < 0) continue;

        const auto& srcItem = this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)];
        juce::String slotRef = this->slotId;
        if (this->artName != "(default)")
            slotRef += "/" + this->artName;

        juce::String targetRef = srcItem.kitCodeRaw;
        if (srcItem.instCode.isNotEmpty())
            targetRef += "/" + srcItem.instCode;
        if (srcItem.artCode.isNotEmpty())
            targetRef += "/" + srcItem.artCode;

        juce::StringArray params;
        int tuning = static_cast<int>(layer->tuningPot->getValue());
        if (tuning != 0) params.add("tuning=" + juce::String(tuning));
        double attackVal = layer->attackPot->getValue();
        if (attackVal >= 0.0) params.add("attack=" + juce::String(static_cast<int>(attackVal)));
        double decayVal = layer->decayPot->getValue();
        if (decayVal >= 0.0) params.add("decay=" + juce::String(static_cast<int>(decayVal)));
        double volVal = layer->gainPot->getValue();
        if (std::abs(volVal) > 0.01) params.add("vol=" + juce::String(volVal, 1));
        if (layer->flipLRButton->getToggleState()) params.add("flipLR=1");
        if (layer->flipPhaseButton->getToggleState()) params.add("flipPhase=1");

        // Bleed levels: only emit non-default values (< 100)
        juce::StringArray bleedParts;
        for (const auto& bleedCtrl : layer->micBleeds) {
            double bleedVal = bleedCtrl->bleedPot->getValue() / 100.0;
            if (bleedVal < 0.99)
                bleedParts.add(bleedCtrl->micCode + ":" + juce::String(bleedVal, 2));
        }
        if (!bleedParts.isEmpty())
            params.add("bleed=" + bleedParts.joinIntoString(","));

        juce::String line = slotRef + ":" + targetRef;
        if (!params.isEmpty())
            line += ":" + params.joinIntoString(",");
        result.add(line);
    }

    return result.joinIntoString("\n");
}

void PadContextPopup::applyToEngine() {
    auto text = this->generateFullMapText();
    if (text.trim().isEmpty()) return;

    this->engine.useInstrumentMap(text);
    this->savedInstrumentMapText = text;
    this->mapTextDirty = false;
    this->recalculateLayerIndexOffset();

    if (this->onMapTextChanged)
        this->onMapTextChanged(text);
}

void PadContextPopup::recalculateLayerIndexOffset() {
    auto mapText = this->savedInstrumentMapText.isNotEmpty()
                       ? this->savedInstrumentMapText
                       : this->engine.getSuggestedInstrumentMapText();
    auto parseResult = DrumKitEngine::parseInstrumentMapText(mapText);
    const auto& entries = parseResult.value.entries;

    this->layerIndexOffset = 0;
    bool foundFirstMatch = false;
    for (const auto& entry : entries) {
        if (entry.slotId != this->slotId) continue;
        bool matchesArt = entry.slotArticulationFilter.empty();
        if (!matchesArt) {
            for (const auto& filter : entry.slotArticulationFilter)
                if (filter == this->artName) matchesArt = true;
        }
        if (matchesArt)
            foundFirstMatch = true;
        else if (!foundFirstMatch)
            ++this->layerIndexOffset;
    }
}

void PadContextPopup::flushMapText() {
    if (!this->mapTextDirty)
        return;

    auto text = this->generateFullMapText();
    if (text.trim().isEmpty())
        return;

    this->savedInstrumentMapText = text;
    this->mapTextDirty = false;

    if (this->onMapTextChanged)
        this->onMapTextChanged(text);
}

// ─── Source picker ────────────────────────────────────────────────────────────

void PadContextPopup::showSourcePicker(LayerRow* layer) {
    this->dismissSourcePicker();

    auto pickerPopup = std::make_unique<SourcePickerPopup>(this->sourceItems);
    pickerPopup->setFilterType(this->slotType);
    pickerPopup->setSize(juce::jlimit(240, 420, getWidth() - 16), 240);

    pickerPopup->onPreview = [this](int sourceIndex, juce::uint8 velocity) {
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(this->sourceItems.size())) return;
        const auto& item = this->sourceItems[static_cast<size_t>(sourceIndex)];
        this->engine.previewSourceItem(item.kitCodeRaw, item.instCode, item.artCode, velocity);
    };

    pickerPopup->onItemSelected = [this, layer](int sourceIndex) {
        layer->selectedSourceIndex = sourceIndex;
        layer->sourcePathHint = sourceIndex >= 0
            ? this->sourceItems[static_cast<size_t>(sourceIndex)].fullPath()
            : juce::String();
        this->updateSourceDisplay(layer);
        juce::MessageManager::callAsync([this] {
            this->dismissSourcePicker();
            this->applyToEngine();
            this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
        });
    };

    auto& pickerCallout = juce::CallOutBox::launchAsynchronously(std::move(pickerPopup),
                                                                 layer->sourceButton->getScreenBounds(),
                                                                 nullptr);
    this->activePickerCallout = &pickerCallout;
}

void PadContextPopup::dismissSourcePicker() {
    if (this->activePickerCallout != nullptr) {
        this->activePickerCallout->exitModalState(0);
        this->activePickerCallout = nullptr;
    }
}

// ─── Paint / Layout ───────────────────────────────────────────────────────────

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
    g.setColour(UITheme::Colours::bgPanelRaised);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
    g.setColour(UITheme::Colours::borderStrong);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void PadContextPopup::resized() {
    auto area = getLocalBounds().reduced(8);

    // Header row: "Kick / center" | MIDI: [btn][●] [btn][●] | [+ Layer]
    auto headerRow = area.removeFromTop(24);
    const int addLayerBtnW = 60;
    this->addLayerButton.setBounds(headerRow.removeFromRight(addLayerBtnW).reduced(0, 2));
    headerRow.removeFromRight(8);

    // MIDI: right-aligned in header
    const int midiControlW = this->midiAssignmentControl ? this->midiAssignmentControl->getPreferredWidth() : 0;
    if (this->midiAssignmentControl)
        this->midiAssignmentControl->setBounds(headerRow.removeFromRight(midiControlW).reduced(0, 2));
    headerRow.removeFromRight(4);
    this->midiHeaderLabel.setBounds(headerRow.removeFromRight(32).reduced(0, 2));

    // Slot label fills remaining left
    this->slotLabel.setBounds(headerRow);
    area.removeFromTop(6);

    // Viewport occupies remaining space
    this->layerViewport.setBounds(area);

    // Layer rows — single row per layer (vertical pot layout):
    // [×] [Source Card] | [Tuning|Attack|Decay|Gain|L/R+Ø] | [Bleed1|Bleed2|...]
    const int potW = 50;
    const int potH = 56; // 12(title) + 28(knob) + 14(value) + 2(gap)
    const int flipColW = 26;
    const int removeW = 16;
    const int sourceW = 170;
    const int layerRowH = potH + 8; // pot height + padding
    const int bleedPotW = 50;
    const int groupGap = 6;
    const int groupPad = 4;

    const int contentHeight = static_cast<int>(this->layerRows.size()) * (layerRowH + 2);
    this->layerContent.setSize(area.getWidth() - (this->layerViewport.isVerticalScrollBarShown() ? this->layerViewport.getScrollBarThickness() : 0),
                               juce::jmax(area.getHeight(), contentHeight));

    auto contentArea = this->layerContent.getLocalBounds();
    for (auto* layer : this->layerRows) {
        auto layerArea = contentArea.removeFromTop(layerRowH);
        auto rowArea = layerArea.reduced(0, 4);

        // Remove button (×) on far left, vertically centered
        if (layer->removeButton->isVisible())
            layer->removeButton->setBounds(rowArea.removeFromLeft(removeW).withSizeKeepingCentre(14, 14));

        // Source card
        auto srcArea = rowArea.removeFromLeft(sourceW);
        layer->sourceButton->setBounds(srcArea);
        layer->sourceDetailLabel->setBounds(srcArea.reduced(6, 4));

        rowArea.removeFromLeft(groupGap);

        // Main params group: Tuning, Attack, Decay, Gain + L/R, Ø
        const int mainGroupW = potW * 4 + flipColW + groupPad * 2;
        auto mainGroupArea = rowArea.removeFromLeft(mainGroupW);
        layer->mainGroupBounds = mainGroupArea;
        auto mainInner = mainGroupArea.reduced(groupPad, 0);
        layer->tuningPot->setBounds(mainInner.removeFromLeft(potW).withHeight(potH));
        layer->attackPot->setBounds(mainInner.removeFromLeft(potW).withHeight(potH));
        layer->decayPot->setBounds(mainInner.removeFromLeft(potW).withHeight(potH));
        layer->gainPot->setBounds(mainInner.removeFromLeft(potW).withHeight(potH));
        auto flipArea = mainInner.removeFromLeft(flipColW).withHeight(potH);
        layer->flipLRButton->setBounds(flipArea.removeFromTop(potH / 2).reduced(2, 4));
        layer->flipPhaseButton->setBounds(flipArea.reduced(2, 4));

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
                bleedCtrl->bleedPot->setBounds(bleedInner.removeFromLeft(actualBleedW).withHeight(potH));
            }
        }

        contentArea.removeFromTop(2); // gap between layers
    }
}
