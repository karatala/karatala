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
            if (!item.displayText().toLowerCase().contains(filterText))
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

void InstrumentConfigPanel::SourcePickerPopup::ListModel::listBoxItemClicked(int row, const juce::MouseEvent& event) {
    if (row < 0 || row >= static_cast<int>(this->owner.filteredEntries.size()))
        return;
    auto originalIdx = this->owner.filteredEntries[static_cast<size_t>(row)].originalIndex;

    // X-axis velocity: left=soft, right=hard
    float xRatio = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.getPosition().getX()) / static_cast<float>(juce::jmax(1, this->owner.listBox.getRowPosition(row, true).getWidth())));
    juce::uint8 velocity = static_cast<juce::uint8>(juce::jlimit(1, 127, static_cast<int>(xRatio * 127.0f)));
    if (this->owner.onPreview)
        this->owner.onPreview(originalIdx, velocity);
}

// ─── InstrumentConfigPanel ────────────────────────────────────────────────────

InstrumentConfigPanel::InstrumentConfigPanel(DrumKitEngine& engine)
    : engine(engine) {
    this->viewport.setViewedComponent(&this->contentComponent, false);
    this->viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(this->viewport);

    auto setupHeader = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
        label.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        label.setJustificationType(juce::Justification::centredLeft);
        this->contentComponent.addAndMakeVisible(label);
    };
    setupHeader(this->headerSlot, "Slot");
    setupHeader(this->headerInstrument, "Instrument");
    setupHeader(this->headerTuning, "Tuning");
    setupHeader(this->headerAttack, "Attack");
    setupHeader(this->headerDecay, "Decay");
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

int InstrumentConfigPanel::findSourceIndex(const juce::String& searchPath) const {
    auto needle = searchPath.trim().toLowerCase();
    for (int idx = 0; idx < static_cast<int>(this->sourceItems.size()); ++idx) {
        const auto& item = this->sourceItems[static_cast<size_t>(idx)];
        if (item.fullPath().toLowerCase() == needle || item.displayText().toLowerCase() == needle)
            return idx;
    }
    DBG("InstrumentConfigPanel: no source match for '" + searchPath + "'");
    return -1;
}

void InstrumentConfigPanel::populate(const juce::String& instrumentMapText) {
    this->dismissPopup();
    this->rebuildSourceItems();
    this->tableRows.clear();
    this->contentComponent.removeAllChildren();
    this->contentComponent.addAndMakeVisible(this->headerSlot);
    this->contentComponent.addAndMakeVisible(this->headerInstrument);
    this->contentComponent.addAndMakeVisible(this->headerTuning);
    this->contentComponent.addAndMakeVisible(this->headerAttack);
    this->contentComponent.addAndMakeVisible(this->headerDecay);
    this->populated = true;

    auto* layout = this->engine.getKitLayout();
    if (layout == nullptr)
        return;

    juce::String mapText = instrumentMapText;
    if (mapText.trim().isEmpty())
        mapText = this->engine.getSuggestedInstrumentMapText();

    auto parseResult = DrumKitEngine::parseInstrumentMapText(mapText);
    const auto& existingEntries = parseResult.value.entries;

    for (const auto& slot : layout->instrumentSlots) {
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
            std::vector<const DrumKitEngine::InstrumentMapEntry*> matchingEntries;
            for (const auto& entry : existingEntries) {
                if (entry.slotId != slot.id)
                    continue;
                if (!entry.slotArticulationFilter.empty()) {
                    bool found = false;
                    for (const auto& filter : entry.slotArticulationFilter)
                        if (filter == artName) found = true;
                    if (!found) continue;
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
                this->addRow(slot.id, artName, slot.type, "", 0, -1, -1);
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
                    this->addRow(slot.id, artName, slot.type, sourceFullPath, tuning, attack, decay);
                }
            }
        }
    }

    this->updateRemoveButtonVisibility();
    this->layoutContent();
    this->engine.seedInstrumentMapFingerprints(this->generateMapText());
}

void InstrumentConfigPanel::addRow(const juce::String& slotId, const juce::String& artName,
                                    KitLib::InstrumentSpec::Type slotType,
                                    const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs) {
    auto* row = this->tableRows.add(new TableRow());
    row->slotId = slotId;
    row->artName = artName;
    row->slotType = slotType;

    // Slot label
    row->slotLabel = std::make_unique<juce::Label>();
    row->slotLabel->setText(slotId, juce::dontSendNotification);
    row->slotLabel->setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
    row->slotLabel->setColour(juce::Label::textColourId, juce::Colours::white);
    this->contentComponent.addAndMakeVisible(*row->slotLabel);

    // Art label
    row->artLabel = std::make_unique<juce::Label>();
    row->artLabel->setText(artName, juce::dontSendNotification);
    row->artLabel->setFont(juce::Font(juce::FontOptions(9.0f)));
    row->artLabel->setColour(juce::Label::textColourId, juce::Colour(0xff99bbdd));
    this->contentComponent.addAndMakeVisible(*row->artLabel);

    // Source button
    row->sourceButton = std::make_unique<juce::TextButton>();
    row->selectedSourceIndex = this->findSourceIndex(sourceFullPath);
    if (row->selectedSourceIndex >= 0)
        row->sourceButton->setButtonText(this->sourceItems[static_cast<size_t>(row->selectedSourceIndex)].displayText());
    else
        row->sourceButton->setButtonText("--");
    row->sourceButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    this->contentComponent.addAndMakeVisible(*row->sourceButton);
    row->sourceButton->onClick = [this, row] { this->showPickerPopup(row); };

    // Tuning slider
    row->tuningSlider = std::make_unique<juce::Slider>();
    row->tuningSlider->setSliderStyle(juce::Slider::LinearHorizontal);
    row->tuningSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 18);
    row->tuningSlider->setRange(-2400.0, 2400.0, 10.0);
    row->tuningSlider->setValue(static_cast<double>(tuningCents), juce::dontSendNotification);
    row->tuningSlider->setTextValueSuffix(" c");
    this->contentComponent.addAndMakeVisible(*row->tuningSlider);
    row->tuningSlider->onValueChange = [this] { this->applyToEngine(); };

    // Attack slider
    row->attackSlider = std::make_unique<juce::Slider>();
    row->attackSlider->setSliderStyle(juce::Slider::LinearHorizontal);
    row->attackSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 32, 18);
    row->attackSlider->setRange(-10.0, 500.0, 10.0);
    row->attackSlider->setValue(attackMs >= 0 ? static_cast<double>(attackMs) : -10.0, juce::dontSendNotification);
    row->attackSlider->textFromValueFunction = [](double value) -> juce::String {
        return value < 0.0 ? "--" : juce::String(static_cast<int>(value));
    };
    this->contentComponent.addAndMakeVisible(*row->attackSlider);
    row->attackSlider->onValueChange = [this] { this->applyToEngine(); };

    // Decay slider
    row->decaySlider = std::make_unique<juce::Slider>();
    row->decaySlider->setSliderStyle(juce::Slider::LinearHorizontal);
    row->decaySlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 32, 18);
    row->decaySlider->setRange(-10.0, 5000.0, 10.0);
    row->decaySlider->setValue(decayMs >= 0 ? static_cast<double>(decayMs) : -10.0, juce::dontSendNotification);
    row->decaySlider->textFromValueFunction = [](double value) -> juce::String {
        return value < 0.0 ? "--" : juce::String(static_cast<int>(value));
    };
    this->contentComponent.addAndMakeVisible(*row->decaySlider);
    row->decaySlider->onValueChange = [this] { this->applyToEngine(); };

    // Add layer button
    row->addButton = std::make_unique<juce::TextButton>("+");
    row->addButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    this->contentComponent.addAndMakeVisible(*row->addButton);
    row->addButton->onClick = [this, row] {
        // Find insertion point: after the last row with same slotId/artName
        int insertAfter = -1;
        for (int idx = 0; idx < this->tableRows.size(); ++idx) {
            if (this->tableRows[idx]->slotId == row->slotId && this->tableRows[idx]->artName == row->artName)
                insertAfter = idx;
        }
        if (insertAfter >= 0) {
            auto* newRow = new TableRow();
            newRow->slotId = row->slotId;
            newRow->artName = row->artName;
            newRow->slotType = row->slotType;
            // Temporarily add to end, then move
            this->tableRows.insert(insertAfter + 1, newRow);
            // Re-populate by calling addRow-like setup inline
            newRow->slotLabel = std::make_unique<juce::Label>();
            newRow->slotLabel->setText("", juce::dontSendNotification);
            this->contentComponent.addAndMakeVisible(*newRow->slotLabel);
            newRow->artLabel = std::make_unique<juce::Label>();
            newRow->artLabel->setText("", juce::dontSendNotification);
            this->contentComponent.addAndMakeVisible(*newRow->artLabel);
            newRow->sourceButton = std::make_unique<juce::TextButton>("--");
            newRow->sourceButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
            this->contentComponent.addAndMakeVisible(*newRow->sourceButton);
            newRow->sourceButton->onClick = [this, newRow] { this->showPickerPopup(newRow); };
            newRow->tuningSlider = std::make_unique<juce::Slider>();
            newRow->tuningSlider->setSliderStyle(juce::Slider::LinearHorizontal);
            newRow->tuningSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 18);
            newRow->tuningSlider->setRange(-2400.0, 2400.0, 10.0);
            newRow->tuningSlider->setTextValueSuffix(" c");
            this->contentComponent.addAndMakeVisible(*newRow->tuningSlider);
            newRow->tuningSlider->onValueChange = [this] { this->applyToEngine(); };
            newRow->attackSlider = std::make_unique<juce::Slider>();
            newRow->attackSlider->setSliderStyle(juce::Slider::LinearHorizontal);
            newRow->attackSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 32, 18);
            newRow->attackSlider->setRange(-10.0, 500.0, 10.0);
            newRow->attackSlider->setValue(-10.0, juce::dontSendNotification);
            newRow->attackSlider->textFromValueFunction = [](double value) -> juce::String { return value < 0.0 ? "--" : juce::String(static_cast<int>(value)); };
            this->contentComponent.addAndMakeVisible(*newRow->attackSlider);
            newRow->attackSlider->onValueChange = [this] { this->applyToEngine(); };
            newRow->decaySlider = std::make_unique<juce::Slider>();
            newRow->decaySlider->setSliderStyle(juce::Slider::LinearHorizontal);
            newRow->decaySlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 32, 18);
            newRow->decaySlider->setRange(-10.0, 5000.0, 10.0);
            newRow->decaySlider->setValue(-10.0, juce::dontSendNotification);
            newRow->decaySlider->textFromValueFunction = [](double value) -> juce::String { return value < 0.0 ? "--" : juce::String(static_cast<int>(value)); };
            this->contentComponent.addAndMakeVisible(*newRow->decaySlider);
            newRow->decaySlider->onValueChange = [this] { this->applyToEngine(); };
            newRow->addButton = std::make_unique<juce::TextButton>("+");
            newRow->addButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
            this->contentComponent.addAndMakeVisible(*newRow->addButton);
            newRow->addButton->onClick = [this, newRow] { /* handled via same pattern but simplified — just re-populate */ };
            newRow->removeButton = std::make_unique<juce::TextButton>("x");
            newRow->removeButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
            this->contentComponent.addAndMakeVisible(*newRow->removeButton);
            newRow->removeButton->onClick = [this, newRow] {
                juce::MessageManager::callAsync([this, newRow] {
                    this->tableRows.removeObject(newRow);
                    this->updateRemoveButtonVisibility();
                    this->layoutContent();
                    this->applyToEngine();
                });
            };
            this->updateRemoveButtonVisibility();
            this->layoutContent();
        }
    };

    // Remove button
    row->removeButton = std::make_unique<juce::TextButton>("x");
    row->removeButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    this->contentComponent.addAndMakeVisible(*row->removeButton);
    row->removeButton->onClick = [this, row] {
        juce::MessageManager::callAsync([this, row] {
            this->tableRows.removeObject(row);
            this->updateRemoveButtonVisibility();
            this->layoutContent();
            this->applyToEngine();
        });
    };
}

void InstrumentConfigPanel::updateRemoveButtonVisibility() {
    // Count rows per slot/art group; show [x] only if group has >1 row
    for (int idx = 0; idx < this->tableRows.size(); ++idx) {
        auto* row = this->tableRows[idx];
        int groupCount = 0;
        for (int jdx = 0; jdx < this->tableRows.size(); ++jdx) {
            if (this->tableRows[jdx]->slotId == row->slotId && this->tableRows[jdx]->artName == row->artName)
                ++groupCount;
        }
        row->removeButton->setVisible(groupCount > 1);
    }
}

void InstrumentConfigPanel::showPickerPopup(TableRow* row) {
    this->dismissPopup();

    this->activePopup = std::make_unique<SourcePickerPopup>(this->sourceItems);
    this->activePopup->setFilterType(row->slotType);

    auto buttonBounds = row->sourceButton->getBoundsInParent();
    auto posInContent = buttonBounds.getPosition();
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

    this->activePopup->onPreview = [this, row](int sourceIndex, juce::uint8 velocity) {
        // Apply the clicked source immediately and play preview
        row->selectedSourceIndex = sourceIndex;
        if (sourceIndex >= 0)
            row->sourceButton->setButtonText(this->sourceItems[static_cast<size_t>(sourceIndex)].displayText());
        else
            row->sourceButton->setButtonText("--");
        this->applyToEngine();
        this->engine.playPreviewNote(row->slotId, row->artName, velocity);
    };

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
    const int contentWidth = this->viewport.getWidth() - this->viewport.getScrollBarThickness();
    const int totalRows = this->tableRows.size();
    const int headerH = 18;
    const int totalHeight = headerH + totalRows * rowHeight + 4;

    this->contentComponent.setBounds(0, 0, juce::jmax(contentWidth, 600), totalHeight);

    // Column layout: | slotCol | artCol | sourceCol | tuningCol | attackCol | decayCol | btnsCol |
    const int width = this->contentComponent.getWidth();
    const int slotCol = 70;
    const int artCol = 58;
    const int btnsCol = 44;
    const int sliderCols = width - slotCol - artCol - btnsCol;
    const int sourceCol = juce::jmax(140, sliderCols * 40 / 100);
    const int remaining = sliderCols - sourceCol;
    const int tuningCol = remaining * 40 / 100;
    const int attackCol = remaining * 30 / 100;
    const int decayCol = remaining - tuningCol - attackCol;

    // Header row
    int xPos = 0;
    this->headerSlot.setBounds(xPos, 0, slotCol, headerH); xPos += slotCol;
    xPos += artCol; // no header for art column
    this->headerInstrument.setBounds(xPos, 0, sourceCol, headerH); xPos += sourceCol;
    this->headerTuning.setBounds(xPos, 0, tuningCol, headerH); xPos += tuningCol;
    this->headerAttack.setBounds(xPos, 0, attackCol, headerH); xPos += attackCol;
    this->headerDecay.setBounds(xPos, 0, decayCol, headerH);

    // Track which slot/art was last shown to suppress duplicate labels
    juce::String lastSlotId, lastArtName;
    int yPos = headerH;

    for (int idx = 0; idx < this->tableRows.size(); ++idx) {
        auto* row = this->tableRows[idx];
        xPos = 0;

        // Slot label: show only on first row of each slot group
        bool showSlot = (row->slotId != lastSlotId);
        row->slotLabel->setText(showSlot ? row->slotId : "", juce::dontSendNotification);
        row->slotLabel->setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
        row->slotLabel->setColour(juce::Label::textColourId, juce::Colours::white);
        row->slotLabel->setBounds(xPos, yPos, slotCol, rowHeight);
        xPos += slotCol;

        // Art label: show only on first row of each slot/art group
        bool showArt = showSlot || (row->artName != lastArtName);
        row->artLabel->setText(showArt ? row->artName : "", juce::dontSendNotification);
        row->artLabel->setFont(juce::Font(juce::FontOptions(9.0f)));
        row->artLabel->setColour(juce::Label::textColourId, juce::Colour(0xff99bbdd));
        row->artLabel->setBounds(xPos, yPos, artCol, rowHeight);
        xPos += artCol;

        row->sourceButton->setBounds(xPos, yPos + 1, sourceCol - 2, rowHeight - 2);
        xPos += sourceCol;

        row->tuningSlider->setBounds(xPos, yPos + 1, tuningCol - 2, rowHeight - 2);
        xPos += tuningCol;

        row->attackSlider->setBounds(xPos, yPos + 1, attackCol - 2, rowHeight - 2);
        xPos += attackCol;

        row->decaySlider->setBounds(xPos, yPos + 1, decayCol - 2, rowHeight - 2);
        xPos += decayCol;

        // Buttons: [+] always, [x] conditional
        row->addButton->setBounds(xPos, yPos + 1, 20, rowHeight - 2);
        if (row->removeButton->isVisible())
            row->removeButton->setBounds(xPos + 21, yPos + 1, 20, rowHeight - 2);

        lastSlotId = row->slotId;
        lastArtName = row->artName;
        yPos += rowHeight;
    }
}

juce::String InstrumentConfigPanel::generateMapText() const {
    juce::StringArray lines;

    for (const auto* row : this->tableRows) {
        if (row->selectedSourceIndex < 0)
            continue;

        const auto& srcItem = this->sourceItems[static_cast<size_t>(row->selectedSourceIndex)];

        juce::String slotRef = row->slotId;
        if (row->artName != "(default)")
            slotRef += "/" + row->artName;

        juce::String targetRef = srcItem.kitDisplayName;
        if (srcItem.instCode.isNotEmpty())
            targetRef += "/" + srcItem.instCode;

        juce::StringArray params;
        int tuning = static_cast<int>(row->tuningSlider->getValue());
        if (tuning != 0)
            params.add("tuning=" + juce::String(tuning));

        double attackVal = row->attackSlider->getValue();
        if (attackVal >= 0.0)
            params.add("attack=" + juce::String(static_cast<int>(attackVal)));

        double decayVal = row->decaySlider->getValue();
        if (decayVal >= 0.0)
            params.add("decay=" + juce::String(static_cast<int>(decayVal)));

        if (srcItem.artCode.isNotEmpty() && srcItem.artCode != row->artName)
            params.add("art=" + srcItem.artCode);

        juce::String line = slotRef + ":" + targetRef;
        if (!params.isEmpty())
            line += ":" + params.joinIntoString(",");

        lines.add(line);
    }

    return lines.joinIntoString("\n");
}

void InstrumentConfigPanel::applyToEngine() {
    auto text = this->generateMapText();
    if (text.trim().isEmpty())
        return;

    DBG("InstrumentConfigPanel::applyToEngine map text:\n" + text);
    this->engine.useInstrumentMap(text);

    if (this->onMapTextChanged)
        this->onMapTextChanged(text);

    if (this->onStatusMessage)
        this->onStatusMessage(this->engine.getLastInstrumentMapStatus());
}
