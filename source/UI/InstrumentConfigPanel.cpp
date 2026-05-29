#include "InstrumentConfigPanel.h"

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
    this->sourceItems = UIHelpers::buildSourceItems(this->engine.getInstrumentManager());
}

int InstrumentConfigPanel::findSourceIndex(const juce::String& searchPath) const {
    return UIHelpers::findSourceIndex(this->sourceItems, searchPath);
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

    // Sentinel text function for attack/decay sliders
    auto sentinelTextFn = [](double value) -> juce::String {
        return value < 0.0 ? "--" : juce::String(static_cast<int>(value));
    };

    // Tuning slider
    row->tuningSlider = UIHelpers::createParamSlider(-2400.0, 2400.0, 10.0, static_cast<double>(tuningCents), 40, 20);
    this->contentComponent.addAndMakeVisible(*row->tuningSlider);
    row->tuningSlider->onValueChange = [this, row] { this->applyRowParametersFast(row); };
    row->tuningSlider->onDragEnd = [this, row] {
        this->engine.playPreviewNote(row->slotId, row->artName, 100);
    };

    // Attack slider
    row->attackSlider = UIHelpers::createParamSlider(-1.0, 500.0, 10.0, attackMs >= 0 ? static_cast<double>(attackMs) : -1.0, 40, 20, sentinelTextFn);
    this->contentComponent.addAndMakeVisible(*row->attackSlider);
    row->attackSlider->onValueChange = [this, row] { this->applyRowParametersFast(row); };
    row->attackSlider->onDragEnd = [this, row] {
        this->engine.playPreviewNote(row->slotId, row->artName, 100);
    };

    // Decay slider
    row->decaySlider = UIHelpers::createParamSlider(-1.0, 5000.0, 10.0, decayMs >= 0 ? static_cast<double>(decayMs) : -1.0, 40, 20, sentinelTextFn);
    this->contentComponent.addAndMakeVisible(*row->decaySlider);
    row->decaySlider->onValueChange = [this, row] { this->applyRowParametersFast(row); };
    row->decaySlider->onDragEnd = [this, row] {
        this->engine.playPreviewNote(row->slotId, row->artName, 100);
    };

    // Add layer button
    row->addButton = std::make_unique<juce::TextButton>("+");
    row->addButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    row->addButton->setConnectedEdges(juce::TextButton::ConnectedOnLeft | juce::TextButton::ConnectedOnRight);
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
            newRow->tuningSlider = UIHelpers::createParamSlider(-2400.0, 2400.0, 10.0, 0.0, 40, 20);
            this->contentComponent.addAndMakeVisible(*newRow->tuningSlider);
            newRow->tuningSlider->onValueChange = [this, newRow] { this->applyRowParametersFast(newRow); };
            newRow->tuningSlider->onDragEnd = [this, newRow] {
                this->engine.playPreviewNote(newRow->slotId, newRow->artName, 100);
            };
            newRow->attackSlider = UIHelpers::createParamSlider(-1.0, 500.0, 10.0, -1.0, 40, 20, 
                [](double value) -> juce::String { return value < 0.0 ? "--" : juce::String(static_cast<int>(value)); });
            this->contentComponent.addAndMakeVisible(*newRow->attackSlider);
            newRow->attackSlider->onValueChange = [this, newRow] { this->applyRowParametersFast(newRow); };
            newRow->attackSlider->onDragEnd = [this, newRow] {
                this->engine.playPreviewNote(newRow->slotId, newRow->artName, 100);
            };
            newRow->decaySlider = UIHelpers::createParamSlider(-1.0, 5000.0, 10.0, -1.0, 40, 20,
                [](double value) -> juce::String { return value < 0.0 ? "--" : juce::String(static_cast<int>(value)); });
            this->contentComponent.addAndMakeVisible(*newRow->decaySlider);
            newRow->decaySlider->onValueChange = [this, newRow] { this->applyRowParametersFast(newRow); };
            newRow->decaySlider->onDragEnd = [this, newRow] {
                this->engine.playPreviewNote(newRow->slotId, newRow->artName, 100);
            };
            newRow->addButton = std::make_unique<juce::TextButton>("+");
            newRow->addButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
            newRow->addButton->setConnectedEdges(juce::TextButton::ConnectedOnLeft | juce::TextButton::ConnectedOnRight);
            this->contentComponent.addAndMakeVisible(*newRow->addButton);
            newRow->addButton->onClick = [this, newRow] { /* handled via same pattern but simplified — just re-populate */ };
            newRow->removeButton = std::make_unique<juce::TextButton>("x");
            newRow->removeButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
            newRow->removeButton->setConnectedEdges(juce::TextButton::ConnectedOnLeft | juce::TextButton::ConnectedOnRight);
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
    row->removeButton->setConnectedEdges(juce::TextButton::ConnectedOnLeft | juce::TextButton::ConnectedOnRight);
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

    this->activePopup->onPreview = [this](int sourceIndex, juce::uint8 velocity) {
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(this->sourceItems.size())) return;
        const auto& item = this->sourceItems[static_cast<size_t>(sourceIndex)];
        this->engine.previewSourceItem(item.kitCodeRaw, item.instCode, item.artCode, velocity);
    };

    this->activePopup->onItemSelected = [this, row](int sourceIndex) {
        row->selectedSourceIndex = sourceIndex;
        if (sourceIndex >= 0)
            row->sourceButton->setButtonText(this->sourceItems[static_cast<size_t>(sourceIndex)].displayText());
        else
            row->sourceButton->setButtonText("--");
        juce::MessageManager::callAsync([this, row] {
            this->dismissPopup();
            this->applyToEngine();
            this->engine.playPreviewNote(row->slotId, row->artName, 100);
        });
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
        const int btnSize = juce::jmax(20, rowHeight - 2); // minimum square
        row->addButton->setBounds(xPos, yPos + 1, btnSize, rowHeight - 2);
        if (row->removeButton->isVisible())
            row->removeButton->setBounds(xPos + btnSize + 1, yPos + 1, btnSize, rowHeight - 2);

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

        juce::String targetRef = srcItem.kitCodeRaw;
        if (srcItem.instCode.isNotEmpty())
            targetRef += "/" + srcItem.instCode;
        if (srcItem.artCode.isNotEmpty())
            targetRef += "/" + srcItem.artCode;

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

        juce::String line = slotRef + ":" + targetRef;
        if (!params.isEmpty())
            line += ":" + params.joinIntoString(",");

        lines.add(line);
    }

    return lines.joinIntoString("\n");
}

int InstrumentConfigPanel::getMappedLayerIndexForRow(const TableRow* row) const {
    if (row == nullptr)
        return -1;
    if (row->selectedSourceIndex < 0)
        return -1;

    int mappedLayerIndex = 0;
    for (int idx = 0; idx < this->tableRows.size(); ++idx) {
        const auto* currentRow = this->tableRows[idx];
        if (currentRow->slotId != row->slotId)
            continue;
        if (currentRow->selectedSourceIndex < 0)
            continue;

        if (currentRow == row)
            return mappedLayerIndex;

        ++mappedLayerIndex;
    }

    return -1;
}

void InstrumentConfigPanel::applyRowParametersFast(TableRow* row) {
    if (row == nullptr)
        return;

    const int layerIndex = this->getMappedLayerIndexForRow(row);
    if (layerIndex < 0)
        return;

    const int tuning = static_cast<int>(row->tuningSlider->getValue());
    const int attack = static_cast<int>(row->attackSlider->getValue());
    const int decay = static_cast<int>(row->decaySlider->getValue());

    this->engine.updateSlotParameters(row->slotId, row->artName, layerIndex, tuning, attack, decay);

    if (this->onMapTextChanged)
        this->onMapTextChanged(this->generateMapText());
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
