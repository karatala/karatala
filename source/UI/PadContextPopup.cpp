#include "PadContextPopup.h"

// ─── MIDI utility ─────────────────────────────────────────────────────────────

// ─── MIDI note button helpers ─────────────────────────────────────────────────

void PadContextPopup::syncNoteButtons() {
    if (!this->midiNotesPtr) return;
    auto& notes = *this->midiNotesPtr;
    const int n1 = !notes.empty()          ? static_cast<int>(notes[0]) : -1;
    const int n2 = notes.size() >= 2 ? static_cast<int>(notes[1]) : -1;
    this->noteButton1.setButtonText(MidiNotePicker::noteToButtonText(n1));
    this->noteButton2.setButtonText(MidiNotePicker::noteToButtonText(n2));
}

void PadContextPopup::applyNoteToSlot(int slotIdx, int note) {
    if (!this->midiNotesPtr) return;
    auto& notes = *this->midiNotesPtr;
    // Ensure the vector has at least (slotIdx+1) entries
    while (static_cast<int>(notes.size()) <= slotIdx)
        notes.push_back(0);
    if (note >= 0 && note <= 127)
        notes[static_cast<size_t>(slotIdx)] = static_cast<uint8_t>(note);
    else
        notes.erase(notes.begin() + slotIdx);
    this->syncNoteButtons();
    if (this->onMidiMappingChanged)
        this->onMidiMappingChanged();
}

void PadContextPopup::showMidiNotePicker(int slotIdx) {
    auto picker = std::make_unique<MidiNotePicker>();

    // Pre-select current note
    if (this->midiNotesPtr && static_cast<int>(this->midiNotesPtr->size()) > slotIdx)
        picker->setCurrentNote(static_cast<int>((*this->midiNotesPtr)[static_cast<size_t>(slotIdx)]));

    picker->onNoteSelected = [this, slotIdx](int note) {
        this->applyNoteToSlot(slotIdx, note);
        // Dismiss the CallOutBox
        if (auto* modal = juce::Component::getCurrentlyModalComponent())
            modal->exitModalState(0);
    };

    auto& btn = (slotIdx == 0) ? this->noteButton1 : this->noteButton2;
    this->activeMidiPicker = picker.get();
    juce::CallOutBox::launchAsynchronously(std::move(picker), btn.getScreenBounds(), nullptr);
}

void PadContextPopup::midiLearnNoteReceived(int midiNote) {
    if (this->activeLearningSlot < 0) return;
    this->applyNoteToSlot(this->activeLearningSlot, midiNote);
    this->activeLearningSlot = -1;
    this->learnButton1.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
    this->learnButton2.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
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

    // Note buttons (open the custom MidiNotePicker grid when clicked)
    this->noteButton1.setButtonText("--");
    this->noteButton2.setButtonText("--");
    for (auto* btn : { &this->noteButton1, &this->noteButton2 })
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    addAndMakeVisible(this->noteButton1);
    addAndMakeVisible(this->noteButton2);
    this->noteButton1.onClick = [this] { this->showMidiNotePicker(0); };
    this->noteButton2.onClick = [this] { this->showMidiNotePicker(1); };

    this->learnButton1.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
    this->learnButton2.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
    addAndMakeVisible(this->learnButton1);
    addAndMakeVisible(this->learnButton2);
    this->learnButton1.onClick = [this] {
        bool wasLearning = (this->activeLearningSlot >= 0);
        this->activeLearningSlot = (this->activeLearningSlot == 0) ? -1 : 0;
        this->learnButton1.setColour(juce::TextButton::textColourOffId, this->activeLearningSlot == 0 ? juce::Colours::yellow : juce::Colours::red);
        this->learnButton2.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
        bool nowLearning = (this->activeLearningSlot >= 0);
        if (nowLearning != wasLearning && this->onMidiLearnStateChanged)
            this->onMidiLearnStateChanged(nowLearning);
    };
    this->learnButton2.onClick = [this] {
        bool wasLearning = (this->activeLearningSlot >= 0);
        this->activeLearningSlot = (this->activeLearningSlot == 1) ? -1 : 1;
        this->learnButton2.setColour(juce::TextButton::textColourOffId, this->activeLearningSlot == 1 ? juce::Colours::yellow : juce::Colours::red);
        this->learnButton1.setColour(juce::TextButton::textColourOffId, juce::Colours::red);
        bool nowLearning = (this->activeLearningSlot >= 0);
        if (nowLearning != wasLearning && this->onMidiLearnStateChanged)
            this->onMidiLearnStateChanged(nowLearning);
    };

    this->syncNoteButtons();

    // Layer header
    this->layerHeaderLabel.setText("Instrument", juce::dontSendNotification);
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
    layer->selectedSourceIndex = this->findSourceIndex(sourceFullPath);

    layer->sourceButton = std::make_unique<juce::TextButton>();
    if (layer->selectedSourceIndex >= 0)
        layer->sourceButton->setButtonText(this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)].displayText());
    else
        layer->sourceButton->setButtonText("--");
    layer->sourceButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    addAndMakeVisible(*layer->sourceButton);
    layer->sourceButton->onClick = [this, layer] { this->showSourcePicker(layer); };

    // Sentinel text function for attack/decay
    auto sentinelTextFn = [](double value) -> juce::String {
        return value < 0.0 ? "--" : juce::String(static_cast<int>(value));
    };

    // Volume pot (-60 to +12 dB)
    layer->volumePot = UIHelpers::createCompactPot(-60.0, 12.0, 0.5, static_cast<double>(volumeDb),
        [](double value) -> juce::String {
            if (value <= -60.0) return "-inf";
            return juce::String(value, 1);
        });
    addAndMakeVisible(*layer->volumePot);
    layer->volumePot->onValueChange = [this, layer] { this->applyLayerParametersFast(layer); };
    layer->volumePot->onDragEnd = [this] {
        this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
    };

    // Tuning pot
    layer->tuningPot = UIHelpers::createCompactPot(-2400.0, 2400.0, 10.0, static_cast<double>(tuningCents));
    addAndMakeVisible(*layer->tuningPot);
    layer->tuningPot->onValueChange = [this, layer] { this->applyLayerParametersFast(layer); };
    layer->tuningPot->onDragEnd = [this] {
        this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
    };

    // Attack pot
    layer->attackPot = UIHelpers::createCompactPot(-1.0, 500.0, 10.0, attackMs >= 0 ? static_cast<double>(attackMs) : -1.0, sentinelTextFn);
    addAndMakeVisible(*layer->attackPot);
    layer->attackPot->onValueChange = [this, layer] { this->applyLayerParametersFast(layer); };
    layer->attackPot->onDragEnd = [this] {
        this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
    };

    // Decay pot
    layer->decayPot = UIHelpers::createCompactPot(-1.0, 5000.0, 10.0, decayMs >= 0 ? static_cast<double>(decayMs) : -1.0, sentinelTextFn);
    addAndMakeVisible(*layer->decayPot);
    layer->decayPot->onValueChange = [this, layer] { this->applyLayerParametersFast(layer); };
    layer->decayPot->onDragEnd = [this] {
        this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
    };

    // Per-layer pot labels
    auto makePotLabel = [this](const juce::String& text) {
        auto label = std::make_unique<juce::Label>("", text);
        label->setFont(juce::Font(juce::FontOptions(9.0f)));
        label->setColour(juce::Label::textColourId, juce::Colour(0xff777777));
        label->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*label);
        return label;
    };
    layer->tuningLabel = makePotLabel("Tuning");
    layer->attackLabel = makePotLabel("Attack");
    layer->decayLabel = makePotLabel("Decay");
    layer->volumeLabel = makePotLabel("Vol");

    // Flip L/R button (toggle)
    layer->flipLRButton = std::make_unique<juce::TextButton>("L/R");
    layer->flipLRButton->setClickingTogglesState(true);
    layer->flipLRButton->setToggleState(flipLR, juce::dontSendNotification);
    layer->flipLRButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    layer->flipLRButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff883333));
    layer->flipLRButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
    layer->flipLRButton->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    layer->flipLRButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    addAndMakeVisible(*layer->flipLRButton);
    layer->flipLRButton->onClick = [this] {
        this->mapTextDirty = true;
        this->applyToEngine();
    };

    // Flip Phase button (toggle)
    layer->flipPhaseButton = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xc3\x98")); // Ø
    layer->flipPhaseButton->setClickingTogglesState(true);
    layer->flipPhaseButton->setToggleState(flipPhase, juce::dontSendNotification);
    layer->flipPhaseButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    layer->flipPhaseButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff883333));
    layer->flipPhaseButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
    layer->flipPhaseButton->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    layer->flipPhaseButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    addAndMakeVisible(*layer->flipPhaseButton);
    layer->flipPhaseButton->onClick = [this] {
        this->mapTextDirty = true;
        this->applyToEngine();
    };

    // Per-mic bleed pots
    auto relevantMics = this->getRelevantMics();
    for (const auto& [micCode, micName] : relevantMics) {
        auto bleedCtrl = std::make_unique<LayerRow::MicBleedControl>();
        bleedCtrl->micCode = micCode;
        bleedCtrl->micName = micName;
        float level = 1.0f;
        auto it = micBleedLevels.find(micCode);
        if (it != micBleedLevels.end())
            level = it->second;
        bleedCtrl->bleedPot = std::make_unique<juce::Slider>();
        bleedCtrl->bleedPot->setSliderStyle(juce::Slider::LinearBar);
        bleedCtrl->bleedPot->setRange(0.0, 1.0, 0.01);
        bleedCtrl->bleedPot->setValue(static_cast<double>(level), juce::dontSendNotification);
        bleedCtrl->bleedPot->textFromValueFunction = [](double value) {
            return juce::String(static_cast<int>(value * 100.0));
        };
        bleedCtrl->bleedPot->setColour(juce::Slider::trackColourId, juce::Colour(0xff444444));
        bleedCtrl->bleedPot->setColour(juce::Slider::thumbColourId, juce::Colour(0xff666666));
        bleedCtrl->bleedPot->setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffcccccc));
        bleedCtrl->bleedPot->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        bleedCtrl->bleedPot->setTooltip(micName);
        addAndMakeVisible(*bleedCtrl->bleedPot);
        bleedCtrl->bleedPot->onValueChange = [this] {
            this->mapTextDirty = true;
        };

        // Mic name label (left of bar)
        bleedCtrl->bleedLabel = std::make_unique<juce::Label>("", micName);
        bleedCtrl->bleedLabel->setFont(juce::Font(juce::FontOptions(9.0f)));
        bleedCtrl->bleedLabel->setColour(juce::Label::textColourId, juce::Colour(0xff777777));
        bleedCtrl->bleedLabel->setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(*bleedCtrl->bleedLabel);

        layer->micBleeds.push_back(std::move(bleedCtrl));
    }

    layer->addButton = std::make_unique<juce::TextButton>("+");
    layer->addButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    layer->addButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    addAndMakeVisible(*layer->addButton);
    layer->addButton->onClick = [this] {
        this->addLayerRow("", 0, -1, -1, 0.0f, false, false, {});
        this->updateRemoveButtonVisibility();
        this->resized();
    };

    layer->removeButton = std::make_unique<juce::TextButton>("x");
    layer->removeButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    layer->removeButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
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

int PadContextPopup::getLayerIndex(const LayerRow* layer) const {
    for (int idx = 0; idx < this->layerRows.size(); ++idx) {
        if (this->layerRows[idx] == layer)
            return idx;
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
                                      layerIndex,
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
        double volVal = layer->volumePot->getValue();
        if (std::abs(volVal) > 0.01) params.add("vol=" + juce::String(volVal, 1));
        if (layer->flipLRButton->getToggleState()) params.add("flipLR=1");
        if (layer->flipPhaseButton->getToggleState()) params.add("flipPhase=1");

        // Bleed levels: only emit non-default values (< 1.0)
        juce::StringArray bleedParts;
        for (const auto& bleedCtrl : layer->micBleeds) {
            double bleedVal = bleedCtrl->bleedPot->getValue();
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

    if (this->onMapTextChanged)
        this->onMapTextChanged(text);
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

    this->activePickerPopup = std::make_unique<SourcePickerPopup>(this->sourceItems);
    this->activePickerPopup->setFilterType(this->slotType);

    // Position relative to this popup — allow to overflow beyond popup bounds
    int popupWidth = juce::jmax(200, getWidth() - 8);
    int popupHeight = 240;
    int popupX = 4;
    // Always try to open downward first
    int popupY = layer->sourceButton->getBounds().getBottom() + 2;
    // If there's not enough room below, let it overflow (don't clamp to popup height)

    this->activePickerPopup->setBounds(popupX, popupY, popupWidth, popupHeight);

    this->activePickerPopup->onPreview = [this](int sourceIndex, juce::uint8 velocity) {
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(this->sourceItems.size())) return;
        const auto& item = this->sourceItems[static_cast<size_t>(sourceIndex)];
        this->engine.previewSourceItem(item.kitCodeRaw, item.instCode, item.artCode, velocity);
    };

    this->activePickerPopup->onItemSelected = [this, layer](int sourceIndex) {
        layer->selectedSourceIndex = sourceIndex;
        if (sourceIndex >= 0)
            layer->sourceButton->setButtonText(this->sourceItems[static_cast<size_t>(sourceIndex)].displayText());
        else
            layer->sourceButton->setButtonText("--");
        juce::MessageManager::callAsync([this] {
            this->dismissSourcePicker();
            this->applyToEngine();
            this->engine.playPreviewNote(this->slotId, this->artName, this->getPreviewVelocity());
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
    const int learnBtnW = 22; // minimum square
    const int noteW = juce::jmin(80, (midiRow.getWidth() - learnBtnW * 2 - 8) / 2);
    this->noteButton1.setBounds(midiRow.removeFromLeft(noteW).reduced(0, 1));
    this->learnButton1.setBounds(midiRow.removeFromLeft(learnBtnW).reduced(1));
    midiRow.removeFromLeft(4);
    this->noteButton2.setBounds(midiRow.removeFromLeft(noteW).reduced(0, 1));
    this->learnButton2.setBounds(midiRow.removeFromLeft(learnBtnW).reduced(1));
    area.removeFromTop(4);

    // Layer header
    this->layerHeaderLabel.setBounds(area.removeFromTop(14));

    const int potW = 44;
    const int flipBtnW = 22;
    const int btnSide = 20;
    const int labelH = 12;
    const int potH = 40;
    const int layerRowH = labelH + potH; // label row + pot row
    const int bleedRowH = 20;
    const int potsAreaW = potW * 4 + flipBtnW * 2; // tuning+attack+decay + L/R+Ø + vol
    const int totalW = area.getWidth();
    const int sourceW = juce::jmax(80, totalW - potsAreaW);

    for (auto* layer : this->layerRows) {
        auto layerArea = area.removeFromTop(layerRowH);

        // Left side: source button + add/remove
        auto leftArea = layerArea.removeFromLeft(sourceW);
        auto sourceRow = leftArea;
        auto btnRow = sourceRow;
        if (layer->removeButton->isVisible())
            layer->removeButton->setBounds(btnRow.removeFromRight(btnSide).reduced(0, (layerRowH - btnSide) / 2));
        layer->addButton->setBounds(btnRow.removeFromRight(btnSide).reduced(0, (layerRowH - btnSide) / 2));
        layer->sourceButton->setBounds(btnRow.reduced(0, (layerRowH - 22) / 2));

        // Right side: labels row + pots row
        auto rightArea = layerArea; // remaining width = potsAreaW
        auto labelRow = rightArea.removeFromTop(labelH);
        auto potRow = rightArea.removeFromTop(potH);

        // Labels: Tuning, Attack, Decay, (skip flips), Vol
        auto lr = labelRow;
        layer->tuningLabel->setBounds(lr.removeFromLeft(potW));
        layer->attackLabel->setBounds(lr.removeFromLeft(potW));
        layer->decayLabel->setBounds(lr.removeFromLeft(potW));
        lr.removeFromLeft(flipBtnW * 2); // skip flip button columns
        layer->volumeLabel->setBounds(lr.removeFromLeft(potW));

        // Pots: Tuning, Attack, Decay, L/R, Ø, Vol
        auto pr = potRow;
        layer->tuningPot->setBounds(pr.removeFromLeft(potW).reduced(1, 0));
        layer->attackPot->setBounds(pr.removeFromLeft(potW).reduced(1, 0));
        layer->decayPot->setBounds(pr.removeFromLeft(potW).reduced(1, 0));
        layer->flipLRButton->setBounds(pr.removeFromLeft(flipBtnW).reduced(1, (potH - 18) / 2));
        layer->flipPhaseButton->setBounds(pr.removeFromLeft(flipBtnW).reduced(1, (potH - 18) / 2));
        layer->volumePot->setBounds(pr.removeFromLeft(potW).reduced(1, 0));

        // Bleed row: [label value] [label value] ... (only if there are mics)
        if (!layer->micBleeds.empty()) {
            auto bleedRow = area.removeFromTop(bleedRowH);
            const int numBleeds = static_cast<int>(layer->micBleeds.size());
            const int cellW = juce::jmax(80, bleedRow.getWidth() / juce::jmax(1, numBleeds));
            for (auto& bleedCtrl : layer->micBleeds) {
                auto cell = bleedRow.removeFromLeft(cellW);
                const int barW = 36;
                bleedCtrl->bleedLabel->setBounds(cell.removeFromLeft(cell.getWidth() - barW - 2).reduced(2, 1));
                bleedCtrl->bleedPot->setBounds(cell.removeFromLeft(barW).reduced(0, 1));
            }
        }
        area.removeFromTop(2);
    }

    // Don't resize the popup itself; let it flow content within the bounds set by the editor
}
