#include "PadContextPopup.h"

// ─── MIDI utility ─────────────────────────────────────────────────────────────

static const char* NOTE_NAMES[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

juce::String PadContextPopup::midiNoteToString(int note) {
    if (note < 0 || note > 127)
        return "--";
    int octave = (note / 12) - 2;
    int noteIdx = note % 12;
    return juce::String(NOTE_NAMES[noteIdx]) + juce::String(octave) + "/" + juce::String(note);
}

void PadContextPopup::populateNoteComboBox(juce::ComboBox& combo) {
    combo.clear(juce::dontSendNotification);
    combo.addItem("--", 1);
    for (int note = 0; note <= 127; ++note)
        combo.addItem(midiNoteToString(note), note + 2);
}

void PadContextPopup::syncComboFromNotes() {
    if (!this->midiNotesPtr) return;
    auto& notes = *this->midiNotesPtr;
    this->noteCombo1.setSelectedId(!notes.empty() ? static_cast<int>(notes[0]) + 2 : 1, juce::dontSendNotification);
    this->noteCombo2.setSelectedId(notes.size() >= 2 ? static_cast<int>(notes[1]) + 2 : 1, juce::dontSendNotification);
}

void PadContextPopup::applyComboToNotes() {
    if (!this->midiNotesPtr) return;
    this->midiNotesPtr->clear();
    int id1 = this->noteCombo1.getSelectedId();
    int id2 = this->noteCombo2.getSelectedId();
    if (id1 > 1) this->midiNotesPtr->push_back(static_cast<uint8_t>(id1 - 2));
    if (id2 > 1) this->midiNotesPtr->push_back(static_cast<uint8_t>(id2 - 2));
    if (this->onMidiMappingChanged)
        this->onMidiMappingChanged();
}

// ─── Source items ─────────────────────────────────────────────────────────────

void PadContextPopup::rebuildSourceItems() {
    this->sourceItems = UIHelpers::buildSourceItems(this->engine.getInstrumentManager());
}

int PadContextPopup::findSourceIndex(const juce::String& searchPath) const {
    return UIHelpers::findSourceIndex(this->sourceItems, searchPath);
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

    // Header
    this->slotLabel.setText(slot.display.isNotEmpty() ? slot.display : slot.id, juce::dontSendNotification);
    this->slotLabel.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    this->slotLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(this->slotLabel);

    this->artLabel.setText(this->artName, juce::dontSendNotification);
    this->artLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    this->artLabel.setColour(juce::Label::textColourId, juce::Colour(0xff99bbdd));
    addAndMakeVisible(this->artLabel);

    // MIDI map section
    this->midiHeaderLabel.setText("MIDI", juce::dontSendNotification);
    this->midiHeaderLabel.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
    this->midiHeaderLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
    addAndMakeVisible(this->midiHeaderLabel);

    populateNoteComboBox(this->noteCombo1);
    populateNoteComboBox(this->noteCombo2);
    addAndMakeVisible(this->noteCombo1);
    addAndMakeVisible(this->noteCombo2);
    this->noteCombo1.onChange = [this] { this->applyComboToNotes(); };
    this->noteCombo2.onChange = [this] { this->applyComboToNotes(); };
    if (auto* label1 = dynamic_cast<juce::Label*>(this->noteCombo1.getChildComponent(0)))
        label1->setFont(juce::Font(juce::FontOptions(9.0f)));
    if (auto* label2 = dynamic_cast<juce::Label*>(this->noteCombo2.getChildComponent(0)))
        label2->setFont(juce::Font(juce::FontOptions(9.0f)));

    this->learnButton1.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
    this->learnButton2.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
    addAndMakeVisible(this->learnButton1);
    addAndMakeVisible(this->learnButton2);
    this->learnButton1.onClick = [this] {
        this->activeLearningSlot = (this->activeLearningSlot == 0) ? -1 : 0;
        this->learnButton1.setColour(juce::TextButton::textColourOffId, this->activeLearningSlot == 0 ? juce::Colours::yellow : juce::Colours::red);
        this->learnButton2.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
    };
    this->learnButton2.onClick = [this] {
        this->activeLearningSlot = (this->activeLearningSlot == 1) ? -1 : 1;
        this->learnButton2.setColour(juce::TextButton::textColourOffId, this->activeLearningSlot == 1 ? juce::Colours::yellow : juce::Colours::red);
        this->learnButton1.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
    };

    this->syncComboFromNotes();

    // Layer header
    this->layerHeaderLabel.setText("Layers", juce::dontSendNotification);
    this->layerHeaderLabel.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
    this->layerHeaderLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
    addAndMakeVisible(this->layerHeaderLabel);

    // Build source items and populate layers from existing instrument map
    this->rebuildSourceItems();
    this->populateLayers();

    // Set initial size so the editor can position the popup
    // Don't set initial size here — let the editor control popup size to be 80% of viewport
    // For now, set a reasonable minimum to avoid zero-size issues
    this->setSize(600, 400);
}

void PadContextPopup::populateLayers() {
    // Parse the saved instrument map to find layers for this slot/art
    // Use saved map if provided, otherwise fall back to suggested map
    auto mapText = this->savedInstrumentMapText.isNotEmpty() ? this->savedInstrumentMapText : this->engine.getSuggestedInstrumentMapText();
    auto parseResult = DrumKitEngine::parseInstrumentMapText(mapText);
    const auto& existingEntries = parseResult.value.entries;

    std::vector<const DrumKitEngine::InstrumentMapEntry*> matchingEntries;
    for (const auto& entry : existingEntries) {
        if (entry.slotId != this->slotId) continue;
        if (!entry.slotArticulationFilter.empty()) {
            bool found = false;
            for (const auto& filter : entry.slotArticulationFilter)
                if (filter == this->artName) found = true;
            if (!found) continue;
        }
        matchingEntries.push_back(&entry);
    }

    // Fallback: entries with no art filter for this slot
    if (matchingEntries.empty()) {
        for (const auto& entry : existingEntries) {
            if (entry.slotId == this->slotId && entry.slotArticulationFilter.empty())
                matchingEntries.push_back(&entry);
        }
    }

    if (matchingEntries.empty()) {
        this->addLayerRow("", 0, -1, -1);
    } else {
        for (const auto* entry : matchingEntries) {
            juce::String sourceFullPath = entry->kitRef;
            if (entry->instCode.isNotEmpty())
                sourceFullPath += "/" + entry->instCode;
            if (!entry->articulationFilter.empty())
                sourceFullPath += "/" + entry->articulationFilter[0];
            else if (this->artName != "(default)")
                sourceFullPath += "/" + this->artName;

            int tuning = entry->tuningCents.value_or(0);
            int attack = entry->attackTimeMs.value_or(-1);
            int decay = entry->decayTimeMs.value_or(-1);
            this->addLayerRow(sourceFullPath, tuning, attack, decay);
        }
    }

    this->updateRemoveButtonVisibility();
}

void PadContextPopup::addLayerRow(const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs) {
    auto* layer = this->layerRows.add(new LayerRow());
    layer->selectedSourceIndex = this->findSourceIndex(sourceFullPath);

    layer->sourceButton = std::make_unique<juce::TextButton>();
    if (layer->selectedSourceIndex >= 0)
        layer->sourceButton->setButtonText(this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)].displayText());
    else
        layer->sourceButton->setButtonText("--");
    layer->sourceButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    addAndMakeVisible(*layer->sourceButton);
    layer->sourceButton->onClick = [this, layer] { this->showSourcePicker(layer); };

    // Sentinel text function for attack/decay sliders
    auto sentinelTextFn = [](double value) -> juce::String {
        return value < 0.0 ? "--" : juce::String(static_cast<int>(value));
    };

    // Tuning slider
    layer->tuningSlider = UIHelpers::createParamSlider(-2400.0, 2400.0, 10.0, static_cast<double>(tuningCents), 40, 20);
    addAndMakeVisible(*layer->tuningSlider);
    layer->tuningSlider->onValueChange = [this] { this->applyToEngine(); };

    // Attack slider
    layer->attackSlider = UIHelpers::createParamSlider(-1.0, 500.0, 10.0, attackMs >= 0 ? static_cast<double>(attackMs) : -1.0, 40, 20, sentinelTextFn);
    addAndMakeVisible(*layer->attackSlider);
    layer->attackSlider->onValueChange = [this] { this->applyToEngine(); };

    // Decay slider
    layer->decaySlider = UIHelpers::createParamSlider(-1.0, 5000.0, 10.0, decayMs >= 0 ? static_cast<double>(decayMs) : -1.0, 40, 20, sentinelTextFn);
    addAndMakeVisible(*layer->decaySlider);
    layer->decaySlider->onValueChange = [this] { this->applyToEngine(); };

    layer->addButton = std::make_unique<juce::TextButton>("+");
    layer->addButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    addAndMakeVisible(*layer->addButton);
    layer->addButton->onClick = [this] {
        this->addLayerRow("", 0, -1, -1);
        this->updateRemoveButtonVisibility();
        this->resized();
    };

    layer->removeButton = std::make_unique<juce::TextButton>("x");
    layer->removeButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    addAndMakeVisible(*layer->removeButton);
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

        juce::String targetRef = srcItem.kitDisplayName;
        if (srcItem.instCode.isNotEmpty())
            targetRef += "/" + srcItem.instCode;

        juce::StringArray params;
        int tuning = static_cast<int>(layer->tuningSlider->getValue());
        if (tuning != 0) params.add("tuning=" + juce::String(tuning));
        double attackVal = layer->attackSlider->getValue();
        if (attackVal >= 0.0) params.add("attack=" + juce::String(static_cast<int>(attackVal)));
        double decayVal = layer->decaySlider->getValue();
        if (decayVal >= 0.0) params.add("decay=" + juce::String(static_cast<int>(decayVal)));
        if (srcItem.artCode.isNotEmpty() && srcItem.artCode != this->artName)
            params.add("art=" + srcItem.artCode);

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

    if (this->onMapTextChanged)
        this->onMapTextChanged(text);
}

// ─── Source picker ────────────────────────────────────────────────────────────

void PadContextPopup::showSourcePicker(LayerRow* layer) {
    this->dismissSourcePicker();

    this->activePickerPopup = std::make_unique<SourcePickerPopup>(this->sourceItems);
    this->activePickerPopup->setFilterType(this->slotType);

    // Position relative to this popup — allow to overflow beyond popup bounds
    int popupWidth = juce::jmax(280, getWidth() - 8);
    int popupHeight = 240;
    int popupX = 4;
    // Always try to open downward first
    int popupY = layer->sourceButton->getBounds().getBottom() + 2;
    // If there's not enough room below, let it overflow (don't clamp to popup height)

    this->activePickerPopup->setBounds(popupX, popupY, popupWidth, popupHeight);

    this->activePickerPopup->onItemSelected = [this, layer](int sourceIndex) {
        layer->selectedSourceIndex = sourceIndex;
        if (sourceIndex >= 0)
            layer->sourceButton->setButtonText(this->sourceItems[static_cast<size_t>(sourceIndex)].displayText());
        else
            layer->sourceButton->setButtonText("--");
        juce::MessageManager::callAsync([this] {
            this->dismissSourcePicker();
            this->applyToEngine();
        });
    };

    this->pickerOverlay = std::make_unique<PickerOverlay>();
    this->pickerOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissSourcePicker(); });
    };
    this->pickerOverlay->setBounds(getLocalBounds());
    addAndMakeVisible(*this->pickerOverlay);
    this->pickerOverlay->toFront(false);

    addAndMakeVisible(*this->activePickerPopup);
    this->activePickerPopup->toFront(true);
}

void PadContextPopup::dismissSourcePicker() {
    if (this->pickerOverlay) {
        removeChildComponent(this->pickerOverlay.get());
        this->pickerOverlay.reset();
    }
    if (this->activePickerPopup) {
        removeChildComponent(this->activePickerPopup.get());
        this->activePickerPopup.reset();
    }
}

// ─── Paint / Layout ───────────────────────────────────────────────────────────

void PadContextPopup::paint(juce::Graphics& g) {
    g.setColour(juce::Colour(0xff222222));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
    g.setColour(juce::Colour(0xff555555));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void PadContextPopup::resized() {
    auto area = getLocalBounds().reduced(6);

    // Header
    auto headerRow = area.removeFromTop(16);
    this->slotLabel.setBounds(headerRow.removeFromLeft(headerRow.getWidth() / 2));
    this->artLabel.setBounds(headerRow);
    area.removeFromTop(2);

    // MIDI section
    this->midiHeaderLabel.setBounds(area.removeFromTop(14));
    auto midiRow = area.removeFromTop(22);
    const int learnBtnW = 18;
    const int comboW = (midiRow.getWidth() - learnBtnW * 2 - 8) / 2;
    this->noteCombo1.setBounds(midiRow.removeFromLeft(comboW).reduced(0, 1));
    this->learnButton1.setBounds(midiRow.removeFromLeft(learnBtnW).reduced(1));
    midiRow.removeFromLeft(4);
    this->noteCombo2.setBounds(midiRow.removeFromLeft(comboW).reduced(0, 1));
    this->learnButton2.setBounds(midiRow.removeFromLeft(learnBtnW).reduced(1));
    area.removeFromTop(4);

    // Layer section
    this->layerHeaderLabel.setBounds(area.removeFromTop(14));

    const int layerRowH = 24;
    const int sliderAreaWidth = area.getWidth();
    const int btnAreaW = 42;
    const int sourceW = juce::jmax(180, (sliderAreaWidth - btnAreaW) / 2);
    const int sliderW = juce::jmax(0, (sliderAreaWidth - sourceW - btnAreaW) / 3);

    for (auto* layer : this->layerRows) {
        auto row = area.removeFromTop(layerRowH);
        layer->sourceButton->setBounds(row.removeFromLeft(sourceW).reduced(0, 1));
        layer->tuningSlider->setBounds(row.removeFromLeft(sliderW).reduced(1, 1));
        layer->attackSlider->setBounds(row.removeFromLeft(sliderW).reduced(1, 1));
        layer->decaySlider->setBounds(row.removeFromLeft(sliderW).reduced(1, 1));
        layer->addButton->setBounds(row.removeFromLeft(20).reduced(1));
        if (layer->removeButton->isVisible())
            layer->removeButton->setBounds(row.removeFromLeft(20).reduced(1));
        area.removeFromTop(1);
    }

    // Don't resize the popup itself; let it flow content within the bounds set by the editor
}
