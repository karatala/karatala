#include "InstrumentConfigPanel.h"
#include "../Engine/ArticulationNameHelpers.h"
#include "UIHelpers.h"

// InstrumentConfigPanel

InstrumentConfigPanel::InstrumentConfigPanel(DrumKitEngine& drumKitEngine)
    : engine(drumKitEngine) {
    auto setupHeader = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        UIHelpers::configureTableHeaderLabel(label);
        this->addAndMakeVisible(label);
    };
    setupHeader(this->headerSlot, "Slot");
    setupHeader(this->headerArticulation, "Articulation");
    setupHeader(this->headerSource, "Source");
    setupHeader(this->headerResponse, "Response");
    setupHeader(this->headerTuning, "Tuning");
    setupHeader(this->headerAttack, "Attack");
    setupHeader(this->headerDecay, "Decay");
    setupHeader(this->headerGain, "Gain");
    setupHeader(this->headerPan, "Pan");
    setupHeader(this->headerLR, "L/R");
    setupHeader(this->headerPhase, "Phase");
}

void InstrumentConfigPanel::resized() {
    this->viewport.setBounds(this->getLocalBounds().withTrimmedTop(headerHeight));
    this->layoutContent();
    this->layoutPopup();
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
    this->velocityResponsePresets = this->engine.getVelocityResponsePresets();
    this->tableRows.clear();
    this->contentComponent.removeAllChildren();
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
                const auto artName = slot.type == KitLib::InstrumentSpec::Type::HiHat ? art.codeRaw : art.codeRaw.upToFirstOccurrenceOf("[", false, false);
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
                        if (ArticulationNameHelpers::mapFilterMatchesArticulation(filter, artName)) found = true;
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
                    if (slot.type == KitLib::InstrumentSpec::Type::HiHat && UIHelpers::usesLegacyDestinationExpressionFilter(entry->slotArticulationFilter, artName))
                        sourceFullPath = UIHelpers::resolveLegacyHihatSourcePath(this->sourceItems, sourceFullPath, artName);

                    int tuning = entry->tuningCents.value_or(0);
                    int attack = entry->attackTimeMs.value_or(-1);
                    int decay = entry->decayTimeMs.value_or(-1);
                    float volume = entry->volumeDb.value_or(0.0f);
                    int pan = entry->pan.value_or(0);
                    this->addRow(slot.id, artName, slot.type, sourceFullPath, tuning, attack, decay, volume, pan, entry->flipLR, entry->flipLRExplicit, entry->flipPhase, -1, entry->velocityResponse, entry->micBleedLevels, entry->micRouteOverrides);
                }
            }
        }
    }

    this->updateRemoveButtonVisibility();
    this->layoutContent();
}

void InstrumentConfigPanel::addRow(const juce::String& slotId, const juce::String& artName,
                                    KitLib::InstrumentSpec::Type slotType,
                                    const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs,
                                    float volumeDb, int pan, bool flipLR, bool flipLRExplicit, bool flipPhase, int insertionIndex,
                                    const std::optional<juce::String>& velocityResponse,
                                    const std::unordered_map<juce::String, float>& micBleedLevels,
                                    const std::unordered_map<juce::String, juce::String>& micRouteOverrides) {
    auto* row = new TableRow();
    if (insertionIndex >= 0 && insertionIndex < this->tableRows.size())
        this->tableRows.insert(insertionIndex, row);
    else
        this->tableRows.add(row);

    row->slotId = slotId;
    row->artName = artName;
    row->slotType = slotType;
    row->sourcePathHint = sourceFullPath;
    row->velocityResponse = velocityResponse;
    row->micBleedLevels = micBleedLevels;
    row->micRouteOverrides = micRouteOverrides;

    // Slot label
    row->slotLabel = std::make_unique<juce::Label>();
    row->slotLabel->setText(slotId, juce::dontSendNotification);
    UIHelpers::configureTableRowLabel(*row->slotLabel, UITheme::Colours::textPrimary);
    this->contentComponent.addAndMakeVisible(*row->slotLabel);

    // Art label
    row->artLabel = std::make_unique<juce::Label>();
    row->artLabel->setText(artName, juce::dontSendNotification);
    UIHelpers::configureTableRowLabel(*row->artLabel, UITheme::Colours::textAccent);
    this->contentComponent.addAndMakeVisible(*row->artLabel);

    // Source button
    row->sourceButton = std::make_unique<juce::TextButton>();
    row->selectedSourceIndex = this->findSourceIndex(sourceFullPath);
    row->sourceButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    row->sourceButton->getProperties().set("leftAlignedText", true);
    this->contentComponent.addAndMakeVisible(*row->sourceButton);
    row->sourceButton->onClick = [this, row] { this->showPickerPopup(row); };
    this->updateSourceButtonDisplay(row);

    row->velocityResponseStatus = std::make_unique<juce::Label>();
    row->velocityResponseStatus->setFont(UITheme::Fonts::small());
    row->velocityResponseStatus->setJustificationType(juce::Justification::centred);
    this->contentComponent.addAndMakeVisible(*row->velocityResponseStatus);
    this->updateVelocityResponseStatus(row);

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
    row->removeButton = std::make_unique<juce::TextButton>(UITheme::Symbols::times);
    row->removeButton->setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    row->removeButton->setConnectedEdges(juce::TextButton::ConnectedOnLeft | juce::TextButton::ConnectedOnRight);
    this->contentComponent.addAndMakeVisible(*row->removeButton);
    row->removeButton->onClick = [this, row] {
        juce::MessageManager::callAsync([this, row] {
            const auto changedSlotId = row->slotId;
            this->tableRows.removeObject(row);
            this->updateRemoveButtonVisibility();
            this->layoutContent();
            this->applyToEngine(changedSlotId);
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
    juce::String slotTitle = row->slotId;
    if (const auto* layout = this->engine.getKitLayout()) {
        for (const auto& slot : layout->instrumentSlots) {
            if (slot.id.compareIgnoreCase(row->slotId) != 0) continue;
            if (slot.display.isNotEmpty()) slotTitle = slot.display;
            break;
        }
    }
    int layerNumber = 0;
    for (const auto* candidateRow : this->tableRows) {
        if (candidateRow == nullptr || candidateRow->slotId.compareIgnoreCase(row->slotId) != 0 || candidateRow->artName.compareIgnoreCase(row->artName) != 0) continue;
        ++layerNumber;
        if (candidateRow == row) break;
    }
    if (layerNumber < 1) layerNumber = 1;
    auto pickerTitle = slotTitle + " / " + row->artName;
    if (layerNumber > 1) pickerTitle += " (layer " + juce::String(layerNumber) + ")";
    this->activePopup->setTitle(pickerTitle);
    this->activePopup->onDismiss = [this] {
        juce::MessageManager::callAsync([this] { this->dismissPopup(); });
    };
    auto initialFilterType = row->slotType;
    if (row->selectedSourceIndex >= 0 && row->selectedSourceIndex < static_cast<int>(this->sourceItems.size())) {
        const auto assignedType = this->sourceItems[static_cast<size_t>(row->selectedSourceIndex)].instType;
        if (assignedType != KitLib::InstrumentSpec::Type::Unknown) initialFilterType = assignedType;
    }
    this->activePopup->setFilterType(initialFilterType);

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
        row->micBleedLevels.clear();
        row->micRouteOverrides.clear();
        this->updateSourceButtonDisplay(row);
        juce::MessageManager::callAsync([this, row] {
            this->dismissPopup();
            this->applyToEngine(row->slotId);
            this->engine.playPreviewNote(row->slotId, row->artName, 100);
        });
    };
    this->popupOverlay = std::make_unique<DismissOverlay>();
    this->popupOverlay->setDarkened(true);
    this->popupOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissPopup(); });
    };
    this->addAndMakeVisible(*this->popupOverlay);
    this->popupOverlay->toFront(false);

    this->addAndMakeVisible(*this->activePopup);
    this->activePopup->toFront(true);
    this->layoutPopup();
    this->activePopup->setSelectedItem(row->selectedSourceIndex);
    this->activePopup->focusItemList();
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

void InstrumentConfigPanel::updateVelocityResponseStatus(TableRow* row) {
    if (row == nullptr || row->velocityResponseStatus == nullptr) return;

    juce::String statusText = "Global";
    juce::String tooltip = "Uses the Global Velocity-to-dB Curve selected in Conf";
    if (row->velocityResponse.has_value()) {
        statusText = "Unnamed*";
        tooltip = "Saved with this layer, but not saved as a reusable preset: " + *row->velocityResponse;
        for (const auto& preset : this->velocityResponsePresets) {
            if (preset.definition != *row->velocityResponse) continue;
            statusText = preset.name;
            tooltip = preset.name + (preset.isUser ? "" : " (Built-in)") + ": " + preset.definition;
            break;
        }
    }

    row->velocityResponseStatus->setText(statusText, juce::dontSendNotification);
    row->velocityResponseStatus->setColour(juce::Label::textColourId, row->velocityResponse.has_value() ? UITheme::Colours::textAccent : UITheme::Colours::textMuted);
    row->velocityResponseStatus->setTooltip(tooltip);
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

void InstrumentConfigPanel::layoutPopup() {
    if (this->popupOverlay) this->popupOverlay->setBounds(this->getLocalBounds());
    if (this->activePopup) {
        const int popupWidth = this->getWidth() * 4 / 5;
        const int popupHeight = this->getHeight() * 4 / 5;
        this->activePopup->setBounds(this->getLocalBounds().withSizeKeepingCentre(popupWidth, popupHeight));
    }
}

void InstrumentConfigPanel::layoutContent() {
    const int contentWidth = this->viewport.getWidth() - this->viewport.getScrollBarThickness();
    const int totalRows = this->tableRows.size();
    const int totalHeight = totalRows * rowHeight + UITheme::Spacing::sm;

    this->contentComponent.setBounds(0, 0, juce::jmax(contentWidth, 880), totalHeight);

    const int width = this->contentComponent.getWidth();
    const int slotCol = UITheme::Sizes::tableInstrumentColumnWidth;
    const int artCol = UITheme::Sizes::tableArticulationColumnWidth;
    const int btnsCol = 44;
    const int flipCol = 40;
    const int responseCol = 62;
    const int availableCol = width - slotCol - artCol - btnsCol - responseCol;
    const int sourceCol = juce::jmax(150, availableCol * 34 / 100);
    const int parameterArea = juce::jmax(260, availableCol - sourceCol - flipCol * 2);
    const int parameterCol = parameterArea / 5;
    const int tuningCol = parameterCol;
    const int attackCol = parameterCol;
    const int decayCol = parameterCol;
    const int gainCol = parameterCol;
    const int panCol = parameterArea - tuningCol - attackCol - decayCol - gainCol;

    int xPos = 0;
    this->headerSlot.setBounds(xPos, 0, slotCol, headerHeight); xPos += slotCol;
    this->headerArticulation.setBounds(xPos, 0, artCol, headerHeight); xPos += artCol;
    this->headerSource.setBounds(xPos, 0, sourceCol, headerHeight); xPos += sourceCol;
    this->headerResponse.setBounds(xPos, 0, responseCol, headerHeight); xPos += responseCol;
    this->headerTuning.setBounds(xPos, 0, tuningCol, headerHeight); xPos += tuningCol;
    this->headerAttack.setBounds(xPos, 0, attackCol, headerHeight); xPos += attackCol;
    this->headerDecay.setBounds(xPos, 0, decayCol, headerHeight); xPos += decayCol;
    this->headerGain.setBounds(xPos, 0, gainCol, headerHeight); xPos += gainCol;
    this->headerPan.setBounds(xPos, 0, panCol, headerHeight); xPos += panCol;
    this->headerLR.setBounds(xPos, 0, flipCol, headerHeight); xPos += flipCol;
    this->headerPhase.setBounds(xPos, 0, flipCol, headerHeight);

    juce::String lastSlotId, lastArtName;
    int yPos = 0;

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

        row->velocityResponseStatus->setBounds(xPos, yPos + 1, responseCol - 2, rowHeight - 2);
        xPos += responseCol;

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
    DrumKitEngine::InstrumentMap instrumentMap;

    for (const auto* row : this->tableRows) {
        if (row->selectedSourceIndex < 0)
            continue;

        const auto& srcItem = this->sourceItems[static_cast<size_t>(row->selectedSourceIndex)];
        DrumKitEngine::InstrumentMapEntry entry;
        entry.slotId = row->slotId;
        if (row->artName != "(default)") entry.slotArticulationFilter.push_back(row->artName);
        entry.kitRef = srcItem.kitCodeRaw;
        entry.instCode = srcItem.instCode;
        if (srcItem.artCode.isNotEmpty()) entry.articulationFilter.push_back(srcItem.artCode);

        int tuning = static_cast<int>(row->tuningControl->getValue());
        if (tuning != 0) entry.tuningCents = tuning;

        double attackVal = row->attackControl->getValue();
        if (attackVal >= 0.0) entry.attackTimeMs = static_cast<int>(attackVal);

        double decayVal = row->decayControl->getValue();
        if (decayVal >= 0.0) entry.decayTimeMs = static_cast<int>(decayVal);

        double volVal = row->gainControl->getValue();
        if (volVal <= -0.01 || volVal >= 0.01) entry.volumeDb = static_cast<float>(volVal);

        int panValue = static_cast<int>(row->panControl->getValue());
        if (panValue != 0) entry.pan = panValue;
        entry.velocityResponse = row->velocityResponse;
        entry.flipLR = row->flipLRButton->getToggleState();
        entry.flipLRExplicit = entry.flipLR || row->flipLRExplicit;
        entry.flipPhase = row->flipPhaseButton->getToggleState();
        entry.micBleedLevels = row->micBleedLevels;
        entry.micRouteOverrides = row->micRouteOverrides;
        instrumentMap.entries.push_back(std::move(entry));
    }
    return this->engine.normalizeInstrumentMapText(DrumKitEngine::serializeInstrumentMap(instrumentMap));
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

void InstrumentConfigPanel::applyToEngine(const juce::String& changedSlotId) {
    this->stopTimer();
    auto text = this->generateMapText();
    if (text.trim().isEmpty()) return;
    DBG("InstrumentConfigPanel::applyToEngine map text:\n" + text);
    this->engine.applyInstrumentMapSlotChange(text, changedSlotId);
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
