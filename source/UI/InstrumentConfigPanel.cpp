#include "InstrumentConfigPanel.h"
#include "UIHelpers.h"

// InstrumentConfigPanel

InstrumentConfigPanel::InstrumentConfigPanel(DrumKitEngine& engine)
    : engine(engine) {
    auto setupHeader = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(UITheme::Fonts::small().boldened());
        label.setColour(juce::Label::textColourId, UITheme::Colours::textMuted);
        label.setJustificationType(juce::Justification::centredLeft);
        this->contentComponent.addAndMakeVisible(label);
    };
    setupHeader(this->headerSlot, "Slot");
    setupHeader(this->headerInstrument, "Instrument");
    setupHeader(this->headerTuning, "Tuning");
    setupHeader(this->headerAttack, "Attack");
    setupHeader(this->headerDecay, "Decay");
    setupHeader(this->headerGain, "Gain");
    setupHeader(this->headerPan, "Pan");
    setupHeader(this->headerLR, "L/R");
    setupHeader(this->headerPhase, "Phase");
}

void InstrumentConfigPanel::resized() {
    this->dismissPopup();
    this->layoutViewportToBounds();
    this->layoutContent();
}

void InstrumentConfigPanel::rebuildSourceItems() {
    this->sourceItems = UIHelpers::buildSourceItems(this->engine.getInstrumentManager());
}

int InstrumentConfigPanel::findSourceIndex(const juce::String& searchPath) const { //TODO: why wrapper?
    return UIHelpers::findSourceIndex(this->sourceItems, searchPath);
}

void InstrumentConfigPanel::populate(const juce::String& instrumentMapText) {
    this->dismissPopup();
    this->stopTimer();
    this->mapTextDirty = false;
    this->rebuildSourceItems();
    this->tableRows.clear();
    this->contentComponent.removeAllChildren();
    this->contentComponent.addAndMakeVisible(this->headerSlot);
    this->contentComponent.addAndMakeVisible(this->headerInstrument);
    this->contentComponent.addAndMakeVisible(this->headerTuning);
    this->contentComponent.addAndMakeVisible(this->headerAttack);
    this->contentComponent.addAndMakeVisible(this->headerDecay);
    this->contentComponent.addAndMakeVisible(this->headerGain);
    this->contentComponent.addAndMakeVisible(this->headerPan);
    this->contentComponent.addAndMakeVisible(this->headerLR);
    this->contentComponent.addAndMakeVisible(this->headerPhase);
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
                if (!artName.isEmpty() && !vectorContains(artNames, artName)) artNames.push_back(artName);
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
                this->addRow(slot.id, artName, slot.type, "", 0, -1, -1, 0.0f, 0, false, false, false);
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
                    int pan = entry->pan.value_or(0);
                    this->addRow(slot.id, artName, slot.type, sourceFullPath, tuning, attack, decay, volume, pan, entry->flipLR, entry->flipLRExplicit, entry->flipPhase);
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
                                    const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs,
                                    float volumeDb, int pan, bool flipLR, bool flipLRExplicit, bool flipPhase, int insertionIndex) {
    auto* row = new TableRow();
    if (insertionIndex >= 0 && insertionIndex < this->tableRows.size())
        this->tableRows.insert(insertionIndex, row);
    else
        this->tableRows.add(row);

    row->slotId = slotId;
    row->artName = artName;
    row->slotType = slotType;
    row->sourcePathHint = sourceFullPath;

    // Slot label
    row->slotLabel = std::make_unique<juce::Label>();
    row->slotLabel->setText(slotId, juce::dontSendNotification);
    row->slotLabel->setFont(UITheme::Fonts::labelBold());
    row->slotLabel->setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->contentComponent.addAndMakeVisible(*row->slotLabel);

    // Art label
    row->artLabel = std::make_unique<juce::Label>();
    row->artLabel->setText(artName, juce::dontSendNotification);
    row->artLabel->setFont(UITheme::Fonts::small());
    row->artLabel->setColour(juce::Label::textColourId, UITheme::Colours::textAccent);
    this->contentComponent.addAndMakeVisible(*row->artLabel);

    // Source button
    row->sourceButton = std::make_unique<juce::TextButton>();
    row->selectedSourceIndex = this->findSourceIndex(sourceFullPath);
    row->sourceButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    row->sourceButton->getProperties().set("leftAlignedText", true);
    this->contentComponent.addAndMakeVisible(*row->sourceButton);
    row->sourceButton->onClick = [this, row] { this->showPickerPopup(row); };
    this->updateSourceButtonDisplay(row);

    auto configureCompactControl = [this, row](CompactParameterControl& control) {
        control.setLabelVisible(false);
        this->contentComponent.addAndMakeVisible(control);
        control.onValueChange = [this, row] { this->applyRowParametersFast(row); };
        control.onDragEnd = [this, row] {
            this->flushMapText();
            this->engine.playPreviewNote(row->slotId, row->artName, 100);
        };
    };

    row->tuningControl = std::make_unique<CompactParameterControl>("Tuning", -2400.0, 2400.0, 1.0, static_cast<double>(tuningCents));
    row->tuningControl->setInteraction(0.0, 10.0, 1.0);
    configureCompactControl(*row->tuningControl);

    row->attackControl = std::make_unique<CompactParameterControl>("Attack", -1.0, 500.0, 1.0, attackMs >= 0 ? static_cast<double>(attackMs) : -1.0, UIHelpers::sentinelValueText);
    row->attackControl->setInteraction(-1.0, 10.0, 1.0);
    configureCompactControl(*row->attackControl);

    row->decayControl = std::make_unique<CompactParameterControl>("Decay", -1.0, 5000.0, 1.0, decayMs >= 0 ? static_cast<double>(decayMs) : -1.0, UIHelpers::sentinelValueText);
    row->decayControl->setInteraction(-1.0, 10.0, 1.0);
    configureCompactControl(*row->decayControl);

    row->gainControl = std::make_unique<CompactParameterControl>("Gain", -60.0, 12.0, 0.1, static_cast<double>(volumeDb), [](double value) -> juce::String {
        if (value <= -60.0) return "-inf";
        return juce::String(value, 1);
    });
    row->gainControl->setInteraction(0.0, 0.5, 0.1);
    configureCompactControl(*row->gainControl);

    row->panControl = std::make_unique<CompactParameterControl>("Pan", -127.0, 127.0, 1.0, static_cast<double>(pan), [](double value) -> juce::String {
        const int panValue = static_cast<int>(value);
        if (panValue == 0) return "C";
        return panValue < 0 ? "L" + juce::String(-panValue) : "R" + juce::String(panValue);
    });
    row->panControl->setInteraction(0.0, 4.0, 1.0);
    configureCompactControl(*row->panControl);

    row->flipLRButton = std::make_unique<juce::TextButton>("L/R");
    UIHelpers::configureToggleButton(*row->flipLRButton, juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    row->flipLRButton->setToggleState(flipLR, juce::dontSendNotification);
    row->flipLRExplicit = flipLRExplicit;
    this->contentComponent.addAndMakeVisible(*row->flipLRButton);
    row->flipLRButton->onClick = [this, row] {
        row->flipLRExplicit = true;
        this->applyRowParametersFast(row);
    };

    row->flipPhaseButton = std::make_unique<juce::TextButton>(juce::String::charToString(0x00D8));
    UIHelpers::configureToggleButton(*row->flipPhaseButton, juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    row->flipPhaseButton->setToggleState(flipPhase, juce::dontSendNotification);
    this->contentComponent.addAndMakeVisible(*row->flipPhaseButton);
    row->flipPhaseButton->onClick = [this, row] { this->applyRowParametersFast(row); };
    // Add layer button
    row->addButton = std::make_unique<juce::TextButton>("+");
    row->addButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
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
            this->addRow(row->slotId, row->artName, row->slotType, "", 0, -1, -1, 0.0f, 0, false, false, false, insertAfter + 1);
            this->updateRemoveButtonVisibility();
            this->layoutContent();
        }
    };

    // Remove button
    row->removeButton = std::make_unique<juce::TextButton>("x");
    row->removeButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
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

    this->activePopup = std::make_unique<SourcePickerPopup>(this->sourceItems, this->engine.getInstrumentCatalog());
    this->activePopup->setFilterType(row->slotType);

    auto buttonBounds = row->sourceButton->getBoundsInParent();
    auto posInContent = buttonBounds.getPosition();
    auto posInViewport = posInContent - juce::Point<int>(0, this->viewport.getViewPositionY());
    auto posInPanel = posInViewport + this->viewport.getPosition();

    const int maxPopupWidth = juce::jmax(300, getWidth() - 8);
    int popupWidth = juce::jmin(maxPopupWidth, juce::jmax(460, buttonBounds.getWidth() + 160));
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
        row->sourcePathHint = sourceIndex >= 0
            ? this->sourceItems[static_cast<size_t>(sourceIndex)].fullPath()
            : juce::String();
        this->updateSourceButtonDisplay(row);
        juce::MessageManager::callAsync([this, row] {
            this->dismissPopup();
            this->applyToEngine();
            this->engine.playPreviewNote(row->slotId, row->artName, 100);
        });
    };

    this->popupOverlay = std::make_unique<DismissOverlay>();
    this->popupOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissPopup(); });
    };
    this->popupOverlay->setBounds(getLocalBounds());
    addAndMakeVisible(*this->popupOverlay);
    this->popupOverlay->toFront(false);

    addAndMakeVisible(*this->activePopup);
    this->activePopup->toFront(true);
}

void InstrumentConfigPanel::updateSourceButtonDisplay(TableRow* row) {
    if (row == nullptr || row->sourceButton == nullptr)
        return;

    if (row->selectedSourceIndex >= 0) {
        row->sourceButton->setButtonText(this->sourceItems[static_cast<size_t>(row->selectedSourceIndex)].displayText());
        row->sourceButton->setColour(juce::TextButton::textColourOffId, UITheme::Colours::textPrimary);
        row->sourceButton->setTooltip(row->sourcePathHint);
        return;
    }

    if (row->sourcePathHint.isNotEmpty()) {
        row->sourceButton->setButtonText("Missing source");
        row->sourceButton->setColour(juce::TextButton::textColourOffId, UITheme::Colours::accentWarning);
        row->sourceButton->setTooltip(row->sourcePathHint);
        return;
    }

    row->sourceButton->setButtonText("Select source");
    row->sourceButton->setColour(juce::TextButton::textColourOffId, UITheme::Colours::textMuted);
    row->sourceButton->setTooltip({});
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
    const int headerH = UITheme::Spacing::rowCompact;
    const int totalHeight = headerH + totalRows * rowHeight + 4;

    this->contentComponent.setBounds(0, 0, juce::jmax(contentWidth, 820), totalHeight);

    const int width = this->contentComponent.getWidth();
    const int slotCol = 70;
    const int artCol = 58;
    const int btnsCol = 44;
    const int flipCol = 40;
    const int availableCol = width - slotCol - artCol - btnsCol;
    const int sourceCol = juce::jmax(150, availableCol * 34 / 100);
    const int parameterArea = juce::jmax(260, availableCol - sourceCol - flipCol * 2);
    const int parameterCol = parameterArea / 5;
    const int tuningCol = parameterCol;
    const int attackCol = parameterCol;
    const int decayCol = parameterCol;
    const int gainCol = parameterCol;
    const int panCol = parameterArea - tuningCol - attackCol - decayCol - gainCol;

    int xPos = 0;
    this->headerSlot.setBounds(xPos, 0, slotCol, headerH); xPos += slotCol;
    xPos += artCol;
    this->headerInstrument.setBounds(xPos, 0, sourceCol, headerH); xPos += sourceCol;
    this->headerTuning.setBounds(xPos, 0, tuningCol, headerH); xPos += tuningCol;
    this->headerAttack.setBounds(xPos, 0, attackCol, headerH); xPos += attackCol;
    this->headerDecay.setBounds(xPos, 0, decayCol, headerH); xPos += decayCol;
    this->headerGain.setBounds(xPos, 0, gainCol, headerH); xPos += gainCol;
    this->headerPan.setBounds(xPos, 0, panCol, headerH); xPos += panCol;
    this->headerLR.setBounds(xPos, 0, flipCol, headerH); xPos += flipCol;
    this->headerPhase.setBounds(xPos, 0, flipCol, headerH);

    juce::String lastSlotId, lastArtName;
    int yPos = headerH;

    for (int idx = 0; idx < this->tableRows.size(); ++idx) {
        auto* row = this->tableRows[idx];
        xPos = 0;

        const bool showSlot = row->slotId != lastSlotId;
        row->slotLabel->setText(showSlot ? row->slotId : "", juce::dontSendNotification);
        row->slotLabel->setBounds(xPos, yPos, slotCol, rowHeight);
        xPos += slotCol;

        const bool showArt = showSlot || row->artName != lastArtName;
        row->artLabel->setText(showArt ? row->artName : "", juce::dontSendNotification);
        row->artLabel->setBounds(xPos, yPos, artCol, rowHeight);
        xPos += artCol;

        row->sourceButton->setBounds(xPos, yPos + 1, sourceCol - 2, rowHeight - 2);
        xPos += sourceCol;

        row->tuningControl->setBounds(xPos, yPos + 1, tuningCol - 2, rowHeight - 2);
        xPos += tuningCol;

        row->attackControl->setBounds(xPos, yPos + 1, attackCol - 2, rowHeight - 2);
        xPos += attackCol;

        row->decayControl->setBounds(xPos, yPos + 1, decayCol - 2, rowHeight - 2);
        xPos += decayCol;

        row->gainControl->setBounds(xPos, yPos + 1, gainCol - 2, rowHeight - 2);
        xPos += gainCol;

        row->panControl->setBounds(xPos, yPos + 1, panCol - 2, rowHeight - 2);
        xPos += panCol;

        row->flipLRButton->setBounds(xPos, yPos + 1, flipCol - 2, rowHeight - 2);
        xPos += flipCol;

        row->flipPhaseButton->setBounds(xPos, yPos + 1, flipCol - 2, rowHeight - 2);
        xPos += flipCol;

        const int btnSize = juce::jmax(20, rowHeight - 2);
        row->addButton->setBounds(xPos, yPos + 1, btnSize, rowHeight - 2);
        if (row->removeButton->isVisible()) row->removeButton->setBounds(xPos + btnSize + 1, yPos + 1, btnSize, rowHeight - 2);

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
        int tuning = static_cast<int>(row->tuningControl->getValue());
        if (tuning != 0)
            params.add("tuning=" + juce::String(tuning));

        double attackVal = row->attackControl->getValue();
        if (attackVal >= 0.0)
            params.add("attack=" + juce::String(static_cast<int>(attackVal)));

        double decayVal = row->decayControl->getValue();
        if (decayVal >= 0.0)
            params.add("decay=" + juce::String(static_cast<int>(decayVal)));

        double volVal = row->gainControl->getValue();
        if (volVal <= -0.01 || volVal >= 0.01)
            params.add("vol=" + juce::String(volVal, 1));

        int panValue = static_cast<int>(row->panControl->getValue());
        if (panValue != 0)
            params.add("pan=" + juce::String(panValue));

        if (row->flipLRButton->getToggleState() || row->flipLRExplicit)
            params.add("flipLR=" + juce::String(row->flipLRButton->getToggleState() ? "1" : "0"));
        if (row->flipPhaseButton->getToggleState())
            params.add("flipPhase=1");

        juce::String line = slotRef + ":" + targetRef;
        if (!params.isEmpty())
            line += ":" + params.joinIntoString(",");

        lines.add(line);
    }
    return this->engine.normalizeInstrumentMapText(lines.joinIntoString("\n"));
}

int InstrumentConfigPanel::getMappedLayerIndexForRow(const TableRow* row) const {
    if (row == nullptr) return -1;
    if (row->selectedSourceIndex < 0) return -1;

    int mappedLayerIndex = 0;
    for (int idx = 0; idx < this->tableRows.size(); ++idx) {
        const auto* currentRow = this->tableRows[idx];
        if (currentRow->slotId != row->slotId) continue;
        if (currentRow->selectedSourceIndex < 0) continue;
        if (currentRow == row) return mappedLayerIndex;
        ++mappedLayerIndex;
    }
    return -1;
}

void InstrumentConfigPanel::applyRowParametersFast(TableRow* row) {
    if (row == nullptr) return;

    const int layerIndex = this->getMappedLayerIndexForRow(row);
    if (layerIndex < 0) return;

    DrumKitEngine::LayerParameterUpdate update;
    update.tuningCents = static_cast<int>(row->tuningControl->getValue());
    update.attackMs = static_cast<int>(row->attackControl->getValue());
    update.decayMs = static_cast<int>(row->decayControl->getValue());
    update.volumeDb = static_cast<float>(row->gainControl->getValue());
    update.pan = static_cast<int>(row->panControl->getValue());
    update.flipLR = row->flipLRButton->getToggleState();
    update.flipPhase = row->flipPhaseButton->getToggleState();

    this->engine.updateSlotParameters(row->slotId, row->artName, layerIndex, update);
    this->mapTextDirty = true;
    this->queueMapTextFlush();
}

void InstrumentConfigPanel::timerCallback() { this->flushMapText(); }

void InstrumentConfigPanel::applyToEngine() {
    this->stopTimer();
    auto text = this->generateMapText();
    if (text.trim().isEmpty()) return;
    DBG("InstrumentConfigPanel::applyToEngine map text:\n" + text);
    this->engine.useInstrumentMap(text);
    this->mapTextDirty = false;
    if (this->onMapTextChanged) this->onMapTextChanged(text);
    if (this->onStatusMessage) this->onStatusMessage(this->engine.getLastInstrumentMapStatus());
}

void InstrumentConfigPanel::flushMapText() {
    if (!this->mapTextDirty) return;
    this->stopTimer();
    const auto text = this->generateMapText();
    if (text.trim().isEmpty()) return;
    this->mapTextDirty = false;
    if (this->onMapTextChanged) this->onMapTextChanged(text);
}

void InstrumentConfigPanel::queueMapTextFlush() { this->startTimer(mapTextFlushDebounceMs); }
