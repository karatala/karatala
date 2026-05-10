#include "InstrumentConfigPanel.h"

// ─── SourcePickerPopup ────────────────────────────────────────────────────────

InstrumentConfigPanel::SourcePickerPopup::SourcePickerPopup(const std::vector<SourceItem>& items)
    : allItems(items) {
    this->filterField.setTextToShowWhenEmpty("filter...", juce::Colours::grey);
    this->filterField.onTextChange = [this] { this->rebuildFiltered(); };
    addAndMakeVisible(this->filterField);

    this->relatedOnlyToggle.setToggleState(true, juce::dontSendNotification);
    this->relatedOnlyToggle.onClick = [this] { this->rebuildFiltered(); };
    addAndMakeVisible(this->relatedOnlyToggle);

    this->listBox.setModel(&this->listModel);
    this->listBox.setRowHeight(18);
    this->listBox.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    addAndMakeVisible(this->listBox);

    this->rebuildFiltered();
}

void InstrumentConfigPanel::SourcePickerPopup::setFilterType(KitLib::InstrumentSpec::Type type) {
    this->filterType = type;
    this->rebuildFiltered();
}

void InstrumentConfigPanel::SourcePickerPopup::resized() {
    auto area = getLocalBounds().reduced(4);
    auto topRow = area.removeFromTop(22);
    this->filterField.setBounds(topRow.removeFromLeft(topRow.getWidth() - 70));
    topRow.removeFromLeft(4);
    this->relatedOnlyToggle.setBounds(topRow);
    area.removeFromTop(2);
    this->listBox.setBounds(area);
}

void InstrumentConfigPanel::SourcePickerPopup::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff222222));
    g.setColour(juce::Colour(0xff555555));
    g.drawRect(getLocalBounds(), 1);
}

void InstrumentConfigPanel::SourcePickerPopup::rebuildFiltered() {
    this->filteredEntries.clear();
    auto filterText = this->filterField.getText().toLowerCase();
    bool relatedOnly = this->relatedOnlyToggle.getToggleState()
                       && this->filterType != KitLib::InstrumentSpec::Type::Unknown;

    for (int idx = 0; idx < static_cast<int>(this->allItems.size()); ++idx) {
        const auto& item = this->allItems[static_cast<size_t>(idx)];

        if (relatedOnly && item.instType != this->filterType)
            continue;

        if (filterText.isNotEmpty()) {
            auto displayLower = item.displayText().toLowerCase();
            if (!displayLower.contains(filterText))
                continue;
        }

        this->filteredEntries.push_back({ idx });
    }

    this->listBox.updateContent();
    this->listBox.repaint();
}

int InstrumentConfigPanel::SourcePickerPopup::ListModel::getNumRows() {
    return static_cast<int>(this->owner.filteredEntries.size());
}

void InstrumentConfigPanel::SourcePickerPopup::ListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.filteredEntries.size()))
        return;

    auto originalIdx = this->owner.filteredEntries[static_cast<size_t>(rowNumber)].originalIndex;
    const auto& item = this->owner.allItems[static_cast<size_t>(originalIdx)];

    if (rowIsSelected)
        g.fillAll(juce::Colour(0xff3a5a8a));
    else
        g.fillAll(rowNumber % 2 == 0 ? juce::Colour(0xff2a2a2a) : juce::Colour(0xff2e2e2e));

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(item.displayText(), 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void InstrumentConfigPanel::SourcePickerPopup::ListModel::listBoxItemClicked(int row, const juce::MouseEvent&) {
    if (row < 0 || row >= static_cast<int>(this->owner.filteredEntries.size()))
        return;

    auto originalIdx = this->owner.filteredEntries[static_cast<size_t>(row)].originalIndex;
    if (this->owner.onItemSelected)
        this->owner.onItemSelected(originalIdx);
}

// ─── ArticulationCard ─────────────────────────────────────────────────────────

InstrumentConfigPanel::ArticulationCard::ArticulationCard(const juce::String& artName)
    : artName(artName) {
    this->artLabel.setText(artName, juce::dontSendNotification);
    this->artLabel.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
    this->artLabel.setColour(juce::Label::textColourId, juce::Colour(0xff99bbdd));
    addAndMakeVisible(this->artLabel);
    addAndMakeVisible(this->addLayerButton);
    this->addLayerButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
}

int InstrumentConfigPanel::ArticulationCard::getRequiredHeight() const {
    return headerHeight + static_cast<int>(this->layers.size()) * layerHeight + addButtonHeight + 2;
}

void InstrumentConfigPanel::ArticulationCard::updateRemoveButtonVisibility() {
    bool showRemove = this->layers.size() > 1;
    for (auto* layer : this->layers)
        layer->removeButton->setVisible(showRemove);
}

void InstrumentConfigPanel::ArticulationCard::paint(juce::Graphics& g) {
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(getLocalBounds().reduced(1, 0), 1);
}

void InstrumentConfigPanel::ArticulationCard::resized() {
    auto area = getLocalBounds().reduced(4, 0);

    // Articulation name header
    this->artLabel.setBounds(area.removeFromTop(headerHeight));

    for (auto* layer : this->layers) {
        auto row = area.removeFromTop(layerHeight);

        if (layer->removeButton->isVisible()) {
            layer->removeButton->setBounds(row.removeFromRight(20).reduced(1));
            row.removeFromRight(1);
        }

        // Source button
        const int buttonWidth = juce::jmax(100, row.getWidth() * 2 / 5);
        layer->sourceButton->setBounds(row.removeFromLeft(buttonWidth).reduced(0, 1));
        row.removeFromLeft(2);

        // T: [slider]  A: [slider]  D: [slider]
        const int remaining = row.getWidth();
        const int labelWidth = 12;
        const int sliderWidth = (remaining - labelWidth * 3 - 6) / 3;

        layer->tuningLabel->setBounds(row.removeFromLeft(labelWidth));
        layer->tuningSlider->setBounds(row.removeFromLeft(sliderWidth).reduced(0, 1));
        row.removeFromLeft(3);
        layer->attackLabel->setBounds(row.removeFromLeft(labelWidth));
        layer->attackSlider->setBounds(row.removeFromLeft(sliderWidth).reduced(0, 1));
        row.removeFromLeft(3);
        layer->decayLabel->setBounds(row.removeFromLeft(labelWidth));
        layer->decaySlider->setBounds(row.reduced(0, 1));
    }

    auto btnRow = area.removeFromTop(addButtonHeight);
    this->addLayerButton.setBounds(btnRow.removeFromLeft(24).reduced(0, 1));
}

// ─── SlotCard ─────────────────────────────────────────────────────────────────

InstrumentConfigPanel::SlotCard::SlotCard(const juce::String& slotId, KitLib::InstrumentSpec::Type type)
    : slotId(slotId)
    , slotType(type) {
    this->headerLabel.setText(slotId, juce::dontSendNotification);
    this->headerLabel.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    this->headerLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(this->headerLabel);
}

int InstrumentConfigPanel::SlotCard::getRequiredHeight() const {
    int total = headerHeight;
    for (const auto* artCard : this->artCards)
        total += artCard->getRequiredHeight() + 1;
    return total + 2;
}

void InstrumentConfigPanel::SlotCard::paint(juce::Graphics& g) {
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
    g.setColour(juce::Colour(0xff444444));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
}

void InstrumentConfigPanel::SlotCard::resized() {
    auto area = getLocalBounds().reduced(4, 2);
    this->headerLabel.setBounds(area.removeFromTop(headerHeight));

    for (auto* artCard : this->artCards) {
        int cardHeight = artCard->getRequiredHeight();
        artCard->setBounds(area.removeFromTop(cardHeight));
        area.removeFromTop(1);
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
    this->dismissPopup();
    this->viewport.setBounds(getLocalBounds());
    this->layoutContent();
}

void InstrumentConfigPanel::rebuildSourceItems() {
    this->sourceItems.clear();
    const auto* libraryManager = this->engine.getInstrumentManager();
    if (libraryManager == nullptr)
        return;

    for (const auto& kitPtr : libraryManager->getKits()) {
        juce::String kitShort = kitPtr->meta.name;
        if (kitShort.isEmpty())
            kitShort = kitPtr->meta.codeRaw;

        std::vector<const KitLib::InstrumentSpec*> instruments;
        for (const auto& [code, instPtr] : kitPtr->getInstrumentSpecsMap())
            instruments.push_back(instPtr.get());
        std::sort(instruments.begin(), instruments.end(), [](const auto* a, const auto* b) {
            return a->meta.codeRaw < b->meta.codeRaw;
        });

        for (const auto* inst : instruments) {
            if (inst->articulationSpecs.empty()) {
                SourceItem item;
                item.kitCodeRaw = kitPtr->meta.codeRaw;
                item.kitDisplayName = kitShort;
                item.instCode = inst->meta.codeRaw;
                item.instType = inst->meta.type;
                this->sourceItems.push_back(std::move(item));
            } else {
                for (const auto& artPtr : inst->articulationSpecs) {
                    SourceItem item;
                    item.kitCodeRaw = kitPtr->meta.codeRaw;
                    item.kitDisplayName = kitShort;
                    item.instCode = inst->meta.codeRaw;
                    item.artCode = artPtr->codeRaw.upToFirstOccurrenceOf("[", false, false);
                    item.instType = inst->meta.type;
                    this->sourceItems.push_back(std::move(item));
                }
            }
        }
    }
}

int InstrumentConfigPanel::findSourceIndex(const juce::String& fullPath) const {
    for (int idx = 0; idx < static_cast<int>(this->sourceItems.size()); ++idx) {
        if (this->sourceItems[static_cast<size_t>(idx)].fullPath() == fullPath)
            return idx;
    }
    return -1;
}

void InstrumentConfigPanel::populate(const juce::String& instrumentMapText) {
    this->dismissPopup();
    this->rebuildSourceItems();
    this->slotCards.clear();
    this->contentComponent.removeAllChildren();
    this->populated = true;

    auto* layout = this->engine.getKitLayout();
    if (layout == nullptr)
        return;

    // Parse the provided instrument map text (from ValueTreeState, not default)
    juce::String mapText = instrumentMapText;
    if (mapText.trim().isEmpty())
        mapText = this->engine.getSuggestedInstrumentMapText();

    auto parseResult = DrumKitEngine::parseInstrumentMapText(mapText);
    const auto& existingEntries = parseResult.value.entries;

    for (const auto& slot : layout->instrumentSlots) {
        auto* slotCard = this->slotCards.add(new SlotCard(slot.id, slot.type));
        this->contentComponent.addAndMakeVisible(slotCard);

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

            std::vector<const DrumKitEngine::InstrumentMapEntry*> matchingEntries;
            for (const auto& entry : existingEntries) {
                if (entry.slotId != slot.id)
                    continue;
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

            if (matchingEntries.empty()) {
                for (const auto& entry : existingEntries) {
                    if (entry.slotId == slot.id && entry.slotArticulationFilter.empty())
                        matchingEntries.push_back(&entry);
                }
            }

            if (matchingEntries.empty()) {
                this->addLayerRow(artCard, "", 0, -1, -1);
            } else {
                for (const auto* entry : matchingEntries) {
                    juce::String sourceFullPath = entry->kitRef;
                    if (entry->instCode.isNotEmpty())
                        sourceFullPath += "/" + entry->instCode;
                    if (!entry->articulationFilter.empty())
                        sourceFullPath += "/" + entry->articulationFilter[0];
                    else if (artName != "(default)")
                        sourceFullPath += "/" + artName;

                    int tuning = entry->tuningCents.value_or(0);
                    int attack = entry->attackTimeMs.value_or(-1);
                    int decay = entry->decayTimeMs.value_or(-1);
                    this->addLayerRow(artCard, sourceFullPath, tuning, attack, decay);
                }
            }

            artCard->updateRemoveButtonVisibility();

            artCard->addLayerButton.onClick = [this, artCard] {
                this->addLayerRow(artCard, "", 0, -1, -1);
                artCard->updateRemoveButtonVisibility();
                this->layoutContent();
                this->resized();
            };
        }
    }

    this->layoutContent();
}

static juce::Label* makeCompactLabel(juce::Component* parent, const juce::String& text) {
    auto* label = new juce::Label();
    label->setText(text, juce::dontSendNotification);
    label->setFont(juce::Font(juce::FontOptions(9.0f)));
    label->setColour(juce::Label::textColourId, juce::Colour(0xff888888));
    label->setJustificationType(juce::Justification::centred);
    parent->addAndMakeVisible(label);
    return label;
}

void InstrumentConfigPanel::addLayerRow(ArticulationCard* artCard, const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs) {
    auto* layer = artCard->layers.add(new LayerRow());

    // Source button
    layer->sourceButton = std::make_unique<juce::TextButton>();
    layer->selectedSourceIndex = this->findSourceIndex(sourceFullPath);
    if (layer->selectedSourceIndex >= 0)
        layer->sourceButton->setButtonText(this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)].displayText());
    else
        layer->sourceButton->setButtonText("--");
    layer->sourceButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    artCard->addAndMakeVisible(*layer->sourceButton);

    // T: tuning label + slider
    layer->tuningLabel.reset(makeCompactLabel(artCard, "T"));
    layer->tuningSlider = std::make_unique<juce::Slider>();
    layer->tuningSlider->setSliderStyle(juce::Slider::LinearHorizontal);
    layer->tuningSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
    layer->tuningSlider->setRange(-2400.0, 2400.0, 10.0);
    layer->tuningSlider->setValue(static_cast<double>(tuningCents), juce::dontSendNotification);
    layer->tuningSlider->setTextValueSuffix(" ct");
    artCard->addAndMakeVisible(*layer->tuningSlider);
    layer->tuningSlider->onValueChange = [this] { this->applyToEngine(); };

    // A: attack label + slider
    layer->attackLabel.reset(makeCompactLabel(artCard, "A"));
    layer->attackSlider = std::make_unique<juce::Slider>();
    layer->attackSlider->setSliderStyle(juce::Slider::LinearHorizontal);
    layer->attackSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 18);
    layer->attackSlider->setRange(-10.0, 500.0, 10.0);
    layer->attackSlider->setValue(attackMs >= 0 ? static_cast<double>(attackMs) : -10.0, juce::dontSendNotification);
    layer->attackSlider->textFromValueFunction = [](double value) -> juce::String {
        return value < 0.0 ? "--" : juce::String(static_cast<int>(value));
    };
    artCard->addAndMakeVisible(*layer->attackSlider);
    layer->attackSlider->onValueChange = [this] { this->applyToEngine(); };

    // D: decay label + slider
    layer->decayLabel.reset(makeCompactLabel(artCard, "D"));
    layer->decaySlider = std::make_unique<juce::Slider>();
    layer->decaySlider->setSliderStyle(juce::Slider::LinearHorizontal);
    layer->decaySlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 18);
    layer->decaySlider->setRange(-10.0, 5000.0, 10.0);
    layer->decaySlider->setValue(decayMs >= 0 ? static_cast<double>(decayMs) : -10.0, juce::dontSendNotification);
    layer->decaySlider->textFromValueFunction = [](double value) -> juce::String {
        return value < 0.0 ? "--" : juce::String(static_cast<int>(value));
    };
    artCard->addAndMakeVisible(*layer->decaySlider);
    layer->decaySlider->onValueChange = [this] { this->applyToEngine(); };

    // Remove button
    layer->removeButton = std::make_unique<juce::TextButton>("x");
    layer->removeButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    artCard->addAndMakeVisible(*layer->removeButton);
    layer->removeButton->onClick = [this, artCard, layer] {
        juce::MessageManager::callAsync([this, artCard, layer] {
            artCard->layers.removeObject(layer);
            artCard->updateRemoveButtonVisibility();
            this->layoutContent();
            this->resized();
            this->applyToEngine();
        });
    };
}

void InstrumentConfigPanel::showPickerPopup(LayerRow* layer, SlotCard* slotCard) {
    this->dismissPopup();

    this->activePopup = std::make_unique<SourcePickerPopup>(this->sourceItems);
    this->activePopup->setFilterType(slotCard->slotType);

    auto buttonBounds = layer->sourceButton->getBoundsInParent();
    auto* artCard = dynamic_cast<juce::Component*>(layer->sourceButton->getParentComponent());
    if (artCard != nullptr) {
        auto posInContent = artCard->getPosition() + slotCard->getPosition() + buttonBounds.getPosition();
        auto posInViewport = posInContent - juce::Point<int>(0, this->viewport.getViewPositionY());
        auto posInPanel = posInViewport + this->viewport.getPosition();

        int popupWidth = juce::jmax(300, buttonBounds.getWidth());
        int popupHeight = 280;
        int popupX = posInPanel.getX();
        int popupY = posInPanel.getY() + buttonBounds.getHeight();

        if (popupY + popupHeight > getHeight())
            popupY = posInPanel.getY() - popupHeight;
        if (popupX + popupWidth > getWidth())
            popupX = getWidth() - popupWidth;

        this->activePopup->setBounds(popupX, popupY, popupWidth, popupHeight);
    }

    this->activePopup->onItemSelected = [this, layer](int sourceIndex) {
        layer->selectedSourceIndex = sourceIndex;
        if (sourceIndex >= 0)
            layer->sourceButton->setButtonText(this->sourceItems[static_cast<size_t>(sourceIndex)].displayText());
        else
            layer->sourceButton->setButtonText("--");
        juce::MessageManager::callAsync([this] {
            this->dismissPopup();
            this->applyToEngine();
        });
    };

    // Transparent overlay behind popup to catch outside clicks
    this->popupOverlay = std::make_unique<PopupOverlay>();
    this->popupOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissPopup(); });
    };
    this->popupOverlay->setBounds(getLocalBounds());
    addAndMakeVisible(*this->popupOverlay);
    this->popupOverlay->toFront(false);

    addAndMakeVisible(*this->activePopup);
    this->activePopup->toFront(true);
}

void InstrumentConfigPanel::dismissPopup() {
    if (this->popupOverlay) {
        removeChildComponent(this->popupOverlay.get());
        this->popupOverlay.reset();
    }
    if (this->activePopup) {
        removeChildComponent(this->activePopup.get());
        this->activePopup.reset();
    }
}

void InstrumentConfigPanel::layoutContent() {
    int totalHeight = 0;
    for (const auto* slotCard : this->slotCards)
        totalHeight += slotCard->getRequiredHeight() + 4;

    const int contentWidth = this->viewport.getWidth() - this->viewport.getScrollBarThickness();
    this->contentComponent.setBounds(0, 0, juce::jmax(contentWidth, 400), totalHeight);

    int yPos = 0;
    for (auto* slotCard : this->slotCards) {
        int cardHeight = slotCard->getRequiredHeight();
        slotCard->setBounds(2, yPos, this->contentComponent.getWidth() - 4, cardHeight);
        yPos += cardHeight + 4;
    }

    // Wire source button onClick for each layer (needs slotCard ref)
    for (auto* slotCard : this->slotCards) {
        for (auto* artCard : slotCard->artCards) {
            for (auto* layer : artCard->layers) {
                layer->sourceButton->onClick = [this, layer, slotCard] {
                    this->showPickerPopup(layer, slotCard);
                };
            }
        }
    }
}

juce::String InstrumentConfigPanel::generateMapText() const {
    juce::StringArray lines;

    for (const auto* slotCard : this->slotCards) {
        for (const auto* artCard : slotCard->artCards) {
            for (const auto* layer : artCard->layers) {
                if (layer->selectedSourceIndex < 0)
                    continue;

                const auto& srcItem = this->sourceItems[static_cast<size_t>(layer->selectedSourceIndex)];

                juce::String slotRef = slotCard->slotId;
                if (artCard->artName != "(default)")
                    slotRef += "/" + artCard->artName;

                juce::String targetRef = srcItem.kitCodeRaw;
                if (srcItem.instCode.isNotEmpty())
                    targetRef += "/" + srcItem.instCode;

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

                if (srcItem.artCode.isNotEmpty())
                    params.add("art=" + srcItem.artCode);

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
    if (text.trim().isEmpty())
        return;

    this->engine.useInstrumentMap(text);

    if (this->onMapTextChanged)
        this->onMapTextChanged(text);

    if (this->onStatusMessage)
        this->onStatusMessage(this->engine.getLastInstrumentMapStatus());
}
