#include "InstrumentConfigPanel.h"

// ─── ArticulationCard ─────────────────────────────────────────────────────────

InstrumentConfigPanel::ArticulationCard::ArticulationCard(const juce::String& artName)
    : artName(artName) {
    addAndMakeVisible(this->addLayerButton);
}

int InstrumentConfigPanel::ArticulationCard::getRequiredHeight() const {
    return static_cast<int>(this->layers.size()) * layerHeight + addButtonHeight + 4;
}

void InstrumentConfigPanel::ArticulationCard::resized() {
    auto area = getLocalBounds().reduced(2, 0);

    for (auto* layer : this->layers) {
        auto row = area.removeFromTop(layerHeight);

        layer->removeButton->setBounds(row.removeFromRight(22).reduced(1));
        row.removeFromRight(2);

        // Combo takes about 40% of remaining width
        const int comboWidth = juce::jmax(120, row.getWidth() * 2 / 5);
        layer->sourceCombo->setBounds(row.removeFromLeft(comboWidth).reduced(0, 2));
        row.removeFromLeft(4);

        // 3 sliders share remaining space equally
        const int sliderWidth = (row.getWidth() - 8) / 3;
        layer->tuningSlider->setBounds(row.removeFromLeft(sliderWidth).reduced(0, 2));
        row.removeFromLeft(4);
        layer->attackSlider->setBounds(row.removeFromLeft(sliderWidth).reduced(0, 2));
        row.removeFromLeft(4);
        layer->decaySlider->setBounds(row.reduced(0, 2));
    }

    auto btnRow = area.removeFromTop(addButtonHeight);
    this->addLayerButton.setBounds(btnRow.removeFromLeft(70).reduced(0, 1));
}

// ─── SlotCard ─────────────────────────────────────────────────────────────────

InstrumentConfigPanel::SlotCard::SlotCard(const juce::String& slotId)
    : slotId(slotId) {
    this->headerLabel.setText(slotId, juce::dontSendNotification);
    this->headerLabel.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    this->headerLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(this->headerLabel);
}

int InstrumentConfigPanel::SlotCard::getRequiredHeight() const {
    int total = headerHeight;
    for (const auto* artCard : this->artCards)
        total += artCard->getRequiredHeight() + 2;
    return total + 4; // bottom padding
}

void InstrumentConfigPanel::SlotCard::resized() {
    auto area = getLocalBounds().reduced(4, 0);
    this->headerLabel.setBounds(area.removeFromTop(headerHeight));

    for (auto* artCard : this->artCards) {
        int cardHeight = artCard->getRequiredHeight();
        artCard->setBounds(area.removeFromTop(cardHeight));
        area.removeFromTop(2);
    }
}

// ─── InstrumentConfigPanel ────────────────────────────────────────────────────

InstrumentConfigPanel::InstrumentConfigPanel(DrumKitEngine& engine)
    : engine(engine) {
    this->viewport.setViewedComponent(&this->contentComponent, false);
    this->viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(this->viewport);
}

void InstrumentConfigPanel::resized() {
    this->viewport.setBounds(getLocalBounds());
    this->layoutContent();
}

void InstrumentConfigPanel::rebuildDropdownItems() {
    this->dropdownItems.clear();
    const auto* libraryManager = this->engine.getInstrumentManager();
    if (libraryManager == nullptr)
        return;

    for (const auto& kitPtr : libraryManager->getKits()) {
        std::vector<const KitLib::InstrumentSpec*> instruments;
        for (const auto& [code, instPtr] : kitPtr->getInstrumentSpecsMap())
            instruments.push_back(instPtr.get());

        std::sort(instruments.begin(), instruments.end(), [](const auto* a, const auto* b) {
            return a->meta.codeRaw < b->meta.codeRaw;
        });

        for (const auto* inst : instruments) {
            if (inst->articulationSpecs.empty()) {
                this->dropdownItems.add(kitPtr->meta.codeRaw + "/" + inst->meta.codeRaw);
            } else {
                for (const auto& artPtr : inst->articulationSpecs) {
                    auto artName = artPtr->codeRaw.upToFirstOccurrenceOf("[", false, false);
                    this->dropdownItems.add(kitPtr->meta.codeRaw + "/" + inst->meta.codeRaw + "/" + artName);
                }
            }
        }
    }
}

void InstrumentConfigPanel::populate() {
    this->rebuildDropdownItems();
    this->slotCards.clear();
    this->contentComponent.removeAllChildren();

    auto* layout = this->engine.getKitLayout();
    if (layout == nullptr)
        return;

    // Parse current instrument map to get existing entries
    auto mapText = this->engine.getSuggestedInstrumentMapText();
    auto parseResult = DrumKitEngine::parseInstrumentMapText(mapText);
    const auto& existingEntries = parseResult.value.entries;

    for (const auto& slot : layout->instrumentSlots) {
        auto* slotCard = this->slotCards.add(new SlotCard(slot.id));
        this->contentComponent.addAndMakeVisible(slotCard);

        // Collect articulation names for this slot
        std::vector<juce::String> artNames;
        if (slot.articulations.empty()) {
            artNames.push_back("(default)");
        } else {
            for (const auto& art : slot.articulations) {
                auto artName = art.codeRaw.upToFirstOccurrenceOf("[", false, false);
                if (!artName.isEmpty() && std::find(artNames.begin(), artNames.end(), artName) == artNames.end())
                    artNames.push_back(artName);
            }
        }

        for (const auto& artName : artNames) {
            auto* artCard = slotCard->artCards.add(new ArticulationCard(artName));
            slotCard->addAndMakeVisible(artCard);

            // Find matching entries from existing map
            std::vector<const DrumKitEngine::InstrumentMapEntry*> matchingEntries;
            for (const auto& entry : existingEntries) {
                if (entry.slotId != slot.id)
                    continue;
                // Match articulation filter
                if (!entry.slotArticulationFilter.empty()) {
                    bool found = false;
                    for (const auto& filter : entry.slotArticulationFilter)
                        if (filter == artName)
                            found = true;
                    if (!found)
                        continue;
                }
                matchingEntries.push_back(&entry);
            }

            // If no matching entries and artName is not (default), check entries with no filter
            if (matchingEntries.empty()) {
                for (const auto& entry : existingEntries) {
                    if (entry.slotId == slot.id && entry.slotArticulationFilter.empty())
                        matchingEntries.push_back(&entry);
                }
            }

            // Create layer rows for matching entries
            if (matchingEntries.empty()) {
                // Add one empty layer
                this->addLayerRow(artCard, "", 0, -1, -1);
            } else {
                for (const auto* entry : matchingEntries) {
                    juce::String sourceText = entry->kitRef;
                    if (entry->instCode.isNotEmpty())
                        sourceText += "/" + entry->instCode;
                    if (!entry->articulationFilter.empty())
                        sourceText += "/" + entry->articulationFilter[0];

                    int tuning = entry->tuningCents.value_or(0);
                    int attack = entry->attackTimeMs.value_or(-1);
                    int decay = entry->decayTimeMs.value_or(-1);
                    this->addLayerRow(artCard, sourceText, tuning, attack, decay);
                }
            }

            // Wire add layer button
            artCard->addLayerButton.onClick = [this, artCard] {
                this->addLayerRow(artCard, "", 0, -1, -1);
                this->layoutContent();
                this->resized();
            };
        }
    }

    this->layoutContent();
}

void InstrumentConfigPanel::addLayerRow(ArticulationCard* artCard, const juce::String& sourceText, int tuningCents, int attackMs, int decayMs) {
    auto* layer = artCard->layers.add(new LayerRow());

    // Source combo
    layer->sourceCombo = std::make_unique<juce::ComboBox>();
    layer->sourceCombo->addItem("--", 1);
    for (int idx = 0; idx < this->dropdownItems.size(); ++idx)
        layer->sourceCombo->addItem(this->dropdownItems[idx], idx + 2);

    // Set selected item
    if (sourceText.isNotEmpty()) {
        int itemIdx = this->dropdownItems.indexOf(sourceText);
        if (itemIdx >= 0)
            layer->sourceCombo->setSelectedId(itemIdx + 2, juce::dontSendNotification);
        else
            layer->sourceCombo->setSelectedId(1, juce::dontSendNotification);
    } else {
        layer->sourceCombo->setSelectedId(1, juce::dontSendNotification);
    }

    auto comboFont = juce::Font(juce::FontOptions(10.0f));
    layer->sourceCombo->setLookAndFeel(&getLookAndFeel());
    artCard->addAndMakeVisible(*layer->sourceCombo);
    layer->sourceCombo->onChange = [this] { this->applyToEngine(); };

    // Tuning slider
    layer->tuningSlider = std::make_unique<juce::Slider>();
    layer->tuningSlider->setSliderStyle(juce::Slider::LinearHorizontal);
    layer->tuningSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    layer->tuningSlider->setRange(-2400.0, 2400.0, 10.0);
    layer->tuningSlider->setValue(static_cast<double>(tuningCents), juce::dontSendNotification);
    layer->tuningSlider->setTextValueSuffix(" ct");
    artCard->addAndMakeVisible(*layer->tuningSlider);
    layer->tuningSlider->onValueChange = [this] { this->applyToEngine(); };

    // Attack slider
    layer->attackSlider = std::make_unique<juce::Slider>();
    layer->attackSlider->setSliderStyle(juce::Slider::LinearHorizontal);
    layer->attackSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
    layer->attackSlider->setRange(-10.0, 500.0, 10.0);
    layer->attackSlider->setValue(attackMs >= 0 ? static_cast<double>(attackMs) : -10.0, juce::dontSendNotification);
    layer->attackSlider->textFromValueFunction = [](double value) -> juce::String {
        return value < 0.0 ? "--" : juce::String(static_cast<int>(value)) + " ms";
    };
    artCard->addAndMakeVisible(*layer->attackSlider);
    layer->attackSlider->onValueChange = [this] { this->applyToEngine(); };

    // Decay slider
    layer->decaySlider = std::make_unique<juce::Slider>();
    layer->decaySlider->setSliderStyle(juce::Slider::LinearHorizontal);
    layer->decaySlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
    layer->decaySlider->setRange(-10.0, 5000.0, 10.0);
    layer->decaySlider->setValue(decayMs >= 0 ? static_cast<double>(decayMs) : -10.0, juce::dontSendNotification);
    layer->decaySlider->textFromValueFunction = [](double value) -> juce::String {
        return value < 0.0 ? "--" : juce::String(static_cast<int>(value)) + " ms";
    };
    artCard->addAndMakeVisible(*layer->decaySlider);
    layer->decaySlider->onValueChange = [this] { this->applyToEngine(); };

    // Remove button
    layer->removeButton = std::make_unique<juce::TextButton>("x");
    artCard->addAndMakeVisible(*layer->removeButton);
    layer->removeButton->onClick = [this, artCard, layer] {
        artCard->layers.removeObject(layer);
        this->layoutContent();
        this->resized();
        this->applyToEngine();
    };
}

void InstrumentConfigPanel::layoutContent() {
    int totalHeight = 0;
    for (const auto* slotCard : this->slotCards)
        totalHeight += slotCard->getRequiredHeight() + 6;

    const int contentWidth = this->viewport.getWidth() - this->viewport.getScrollBarThickness();
    this->contentComponent.setBounds(0, 0, juce::jmax(contentWidth, 400), totalHeight);

    int yPos = 0;
    for (auto* slotCard : this->slotCards) {
        int cardHeight = slotCard->getRequiredHeight();
        slotCard->setBounds(0, yPos, this->contentComponent.getWidth(), cardHeight);
        yPos += cardHeight + 6;
    }
}

juce::String InstrumentConfigPanel::generateMapText() const {
    juce::StringArray lines;

    for (const auto* slotCard : this->slotCards) {
        for (const auto* artCard : slotCard->artCards) {
            for (const auto* layer : artCard->layers) {
                int selectedId = layer->sourceCombo->getSelectedId();
                if (selectedId <= 1)
                    continue; // unassigned

                juce::String sourceText = this->dropdownItems[selectedId - 2];

                // Parse source into kitRef/instCode and optional artFilter
                juce::String kitRef, instCode;
                juce::String artFilter;
                auto parts = juce::StringArray::fromTokens(sourceText, "/", "");
                if (parts.size() >= 1) kitRef = parts[0];
                if (parts.size() >= 2) instCode = parts[1];
                if (parts.size() >= 3) artFilter = parts[2];

                // Build slot ref: slotId or slotId/artName
                juce::String slotRef = slotCard->slotId;
                if (artCard->artName != "(default)")
                    slotRef += "/" + artCard->artName;

                // Build target ref: kitRef/instCode
                juce::String targetRef = kitRef;
                if (instCode.isNotEmpty())
                    targetRef += "/" + instCode;

                // Build params
                juce::StringArray params;
                int tuning = static_cast<int>(layer->tuningSlider->getValue());
                if (tuning != 0)
                    params.add("tuning=" + juce::String(tuning));

                double attackVal = layer->attackSlider->getValue();
                if (attackVal >= 0.0)
                    params.add("attack=" + juce::String(static_cast<int>(attackVal)));

                double decayVal = layer->decaySlider->getValue();
                if (decayVal >= 0.0)
                    params.add("decay=" + juce::String(static_cast<int>(decayVal)));

                if (artFilter.isNotEmpty())
                    params.add("art=" + artFilter);

                juce::String line = slotRef + ":" + targetRef;
                if (!params.isEmpty())
                    line += ":" + params.joinIntoString(",");

                lines.add(line);
            }
        }
    }

    return lines.joinIntoString("\n");
}

void InstrumentConfigPanel::applyToEngine() {
    auto text = this->generateMapText();
    this->engine.useInstrumentMap(text);

    if (this->onStatusMessage)
        this->onStatusMessage(this->engine.getLastInstrumentMapStatus());
}
