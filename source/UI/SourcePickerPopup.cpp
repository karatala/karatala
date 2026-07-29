#include "SourcePickerPopup.h"
#include "../Engine/ArticulationNameHelpers.h"
#include "UIHelpers.h"
#include "UITheme.h"

#include <algorithm>
#include <set>

SourcePickerPopup::SourcePickerPopup(const std::vector<SourceItem>& items, const InstrumentCatalog& instrumentCatalog, SelectionMode mode)
    : allItems(items)
    , catalog(instrumentCatalog)
    , selectionMode(mode) {
    this->titleLabel.setFont(UITheme::Fonts::sectionTitle().withHeight(14.0f));
    this->titleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->titleLabel.setJustificationType(juce::Justification::centredLeft);
    this->titleLabel.setMinimumHorizontalScale(0.75f);
    this->addAndMakeVisible(this->titleLabel);

    UIHelpers::configureDialogCloseButton(this->closeButton);
    this->closeButton.onClick = [this] {
        if (this->onDismiss) this->onDismiss();
    };
    this->addAndMakeVisible(this->closeButton);

    UIHelpers::configureBorderlessTextEditor(this->filterField);
    this->filterField.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    this->filterField.setTextToShowWhenEmpty("filter...", UITheme::Colours::textMuted);
    this->filterField.onTextChange = [this] { this->queueRebuildFiltered(); };
    this->addAndMakeVisible(this->filterField);

    for (auto* button : { &this->allCategoryButton, &this->drumsCategoryButton, &this->cymbalsCategoryButton, &this->otherCategoryButton }) {
        UIHelpers::configureCategoryButton(*button);
        this->addAndMakeVisible(*button);
    }
    this->allCategoryButton.onClick = [this] { this->setCategory(Category::All); };
    this->drumsCategoryButton.onClick = [this] { this->setCategory(Category::Drums); };
    this->cymbalsCategoryButton.onClick = [this] { this->setCategory(Category::Cymbals); };
    this->otherCategoryButton.onClick = [this] { this->setCategory(Category::Other); };

    this->kindListLabel.setText("Types", juce::dontSendNotification);
    UIHelpers::configureCaptionLabel(this->kindListLabel, juce::Justification::centredLeft);
    this->addAndMakeVisible(this->kindListLabel);

    this->kindListBox.setModel(&this->kindListModel);
    this->kindListBox.setRowHeight(22);
    this->kindListBox.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    this->addAndMakeVisible(this->kindListBox);

    this->articulationListLabel.setText(this->selectionMode == SelectionMode::WholeInstrument ? "Preview arts" : "Articulations", juce::dontSendNotification);
    UIHelpers::configureCaptionLabel(this->articulationListLabel, juce::Justification::centredLeft);
    this->addAndMakeVisible(this->articulationListLabel);
    this->articulationListBox.setModel(&this->articulationListModel);
    this->articulationListBox.setRowHeight(22);
    this->articulationListBox.setMultipleSelectionEnabled(this->selectionMode == SelectionMode::Articulation);
    this->articulationListBox.setClickingTogglesRowSelection(this->selectionMode == SelectionMode::Articulation);
    this->articulationListBox.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    this->addAndMakeVisible(this->articulationListBox);

    this->listBox.setModel(&this->listModel);
    this->listBox.setRowHeight(22);
    this->listBox.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    this->addAndMakeVisible(this->listBox);

    this->cachedLowerDisplayText.reserve(this->allItems.size());
    for (const auto& item : this->allItems)
        this->cachedLowerDisplayText.push_back(item.displayText().toLowerCase());

    this->rebuildKindEntries();
    this->rebuildArticulationEntries();
    this->rebuildFiltered();
    this->updateCategoryButtons();
}

void SourcePickerPopup::setFilterType(KitLib::InstrumentSpec::Type type) {
    this->selectedCategory = this->categoryForType(type);
    this->selectedKind = type;
    this->rebuildKindEntries();
    this->rebuildArticulationEntries();
    this->rebuildFiltered();
    this->updateCategoryButtons();
}

void SourcePickerPopup::setSelectedItem(int originalIndex) {
    const bool originalIndexValid = originalIndex >= 0 && originalIndex < static_cast<int>(this->allItems.size());
    for (int idx = 0; idx < static_cast<int>(this->filteredEntries.size()); ++idx) {
        const int filteredOriginalIndex = this->filteredEntries[static_cast<size_t>(idx)].originalIndex;
        const bool exactMatch = filteredOriginalIndex == originalIndex;
        const bool wholeInstrumentMatch = this->selectionMode == SelectionMode::WholeInstrument && originalIndexValid
            && SourcePickerPopup::wholeInstrumentKey(this->allItems[static_cast<size_t>(filteredOriginalIndex)]) == SourcePickerPopup::wholeInstrumentKey(this->allItems[static_cast<size_t>(originalIndex)]);
        if (!exactMatch && !wholeInstrumentMatch) continue;
        this->suppressSelectionPreview = true;
        this->listBox.selectRow(idx);
        this->suppressSelectionPreview = false;
        this->listBox.scrollToEnsureRowIsOnscreen(idx);
        if (this->selectionMode == SelectionMode::WholeInstrument && originalIndexValid) {
            juce::SparseSet<int> selectedRows;
            const auto articulationIterator = std::find(this->articulationSourceIndexes.begin(), this->articulationSourceIndexes.end(), originalIndex);
            if (articulationIterator != this->articulationSourceIndexes.end()) {
                const int articulationRow = static_cast<int>(std::distance(this->articulationSourceIndexes.begin(), articulationIterator));
                selectedRows.addRange({ articulationRow, articulationRow + 1 });
            }
            this->articulationListBox.setSelectedRows(selectedRows, juce::dontSendNotification);
        }
        return;
    }
}

void SourcePickerPopup::setTitle(const juce::String& title) {
    this->titleLabel.setText(title, juce::dontSendNotification);
}

void SourcePickerPopup::focusItemList() {
    this->listBox.grabKeyboardFocus();
}

void SourcePickerPopup::resized() {
    auto area = this->getLocalBounds().reduced(4);
    auto titleRow = area.removeFromTop(22);
    this->closeButton.setBounds(titleRow.removeFromRight(22).withSizeKeepingCentre(UITheme::Sizes::standardControlHeight, UITheme::Sizes::standardControlHeight));
    titleRow.removeFromRight(4);
    this->titleLabel.setBounds(titleRow);
    area.removeFromTop(2);

    auto topRow = area.removeFromTop(22);
    const int categoryAreaW = juce::jmin(232, juce::jmax(184, topRow.getWidth() / 2));
    auto categoryArea = topRow.removeFromLeft(categoryAreaW);
    topRow.removeFromLeft(4);
    this->filterField.setBounds(topRow.reduced(0, 1));

    const int buttonW = categoryArea.getWidth() / 4;
    this->allCategoryButton.setBounds(categoryArea.removeFromLeft(buttonW).reduced(1, 1));
    this->drumsCategoryButton.setBounds(categoryArea.removeFromLeft(buttonW).reduced(1, 1));
    this->cymbalsCategoryButton.setBounds(categoryArea.removeFromLeft(buttonW).reduced(1, 1));
    this->otherCategoryButton.setBounds(categoryArea.reduced(1, 1));

    area.removeFromTop(4);
    auto body = area;
    const int leftWidth = juce::jmin(sidebarWidth, juce::jmax(90, body.getWidth() / 4));
    auto sidebarArea = body.removeFromLeft(leftWidth);
    const int labelHeight = 16;
    const int listGap = 4;
    const int availableListHeight = sidebarArea.getHeight() - labelHeight * 2 - listGap;
    const int kindListHeight = juce::jmin(this->kindListBox.getRowHeight() * 10, availableListHeight);
    this->kindListLabel.setBounds(sidebarArea.removeFromTop(labelHeight));
    this->kindListBox.setBounds(sidebarArea.removeFromTop(kindListHeight));
    sidebarArea.removeFromTop(listGap);
    this->articulationListLabel.setBounds(sidebarArea.removeFromTop(labelHeight));
    this->articulationListBox.setBounds(sidebarArea);
    body.removeFromLeft(4);
    this->listBox.setBounds(body);
}

void SourcePickerPopup::paint(juce::Graphics& g) {
    g.setColour(UITheme::Colours::bgPanelRaised);
    g.fillRoundedRectangle(this->getLocalBounds().toFloat(), 6.0f);
}

void SourcePickerPopup::setCategory(Category category) {
    if (this->selectedCategory == category)
        return;

    this->selectedCategory = category;
    this->selectedKind = KitLib::InstrumentSpec::Type::Unknown;
    this->rebuildKindEntries();
    this->rebuildArticulationEntries();
    this->rebuildFiltered();
    this->updateCategoryButtons();
}

std::optional<InstrumentFamily> SourcePickerPopup::familyForCategory(Category category) {
    switch (category) {
        case Category::Drums: return InstrumentFamily::Drum;
        case Category::Cymbals: return InstrumentFamily::Cymbal;
        case Category::Other: return InstrumentFamily::Other;
        case Category::All: break;
    }
    return std::nullopt;
}

SourcePickerPopup::Category SourcePickerPopup::categoryForType(KitLib::InstrumentSpec::Type type) const {
    switch (this->catalog.familyFor(type)) {
        case InstrumentFamily::Drum: return Category::Drums;
        case InstrumentFamily::Cymbal: return Category::Cymbals;
        case InstrumentFamily::Other: return Category::Other;
        case InstrumentFamily::Unknown: break;
    }
    return Category::All;
}

bool SourcePickerPopup::matchesCategory(KitLib::InstrumentSpec::Type type) const {
    const auto categoryFamily = familyForCategory(this->selectedCategory);
    if (!categoryFamily.has_value())
        return true;
    return this->catalog.familyFor(type) == *categoryFamily;
}

juce::String SourcePickerPopup::kindLabel(KitLib::InstrumentSpec::Type type) {
    return KitLib::InstrumentSpec::typeToString(type).replaceCharacter('_', ' ');
}

juce::String SourcePickerPopup::wholeInstrumentKey(const SourceItem& item) {
    return item.kitCodeRaw.trim().toLowerCase() + "\n" + item.instCode.trim().toLowerCase();
}

juce::String SourcePickerPopup::wholeInstrumentDisplayText(const SourceItem& item) {
    const auto kitName = item.kitDisplayName.isNotEmpty() ? item.kitDisplayName : item.kitCodeRaw;
    return item.instCode.isNotEmpty() ? kitName + "/" + item.instCode : kitName;
}

void SourcePickerPopup::rebuildKindEntries() {
    std::set<KitLib::InstrumentSpec::Type> seenKinds;
    for (const auto& item : this->allItems) {
        if (item.instType == KitLib::InstrumentSpec::Type::Unknown)
            continue;
        if (!this->matchesCategory(item.instType))
            continue;
        seenKinds.insert(item.instType);
    }

    this->kindEntries.assign(seenKinds.begin(), seenKinds.end());
    std::sort(this->kindEntries.begin(), this->kindEntries.end(), [](auto left, auto right) {
        return kindLabel(left).compareIgnoreCase(kindLabel(right)) < 0;
    });

    if (this->selectedKind != KitLib::InstrumentSpec::Type::Unknown && !vectorContains(this->kindEntries, this->selectedKind))
        this->selectedKind = KitLib::InstrumentSpec::Type::Unknown;

    this->kindListBox.updateContent();
    int selectedRow = 0;
    if (this->selectedKind != KitLib::InstrumentSpec::Type::Unknown) {
        auto it = std::find(this->kindEntries.begin(), this->kindEntries.end(), this->selectedKind);
        if (it != this->kindEntries.end())
            selectedRow = static_cast<int>(std::distance(this->kindEntries.begin(), it)) + 1;
    }
    this->kindListBox.selectRow(selectedRow);
}

void SourcePickerPopup::rebuildArticulationEntries() {
    if (this->selectionMode == SelectionMode::WholeInstrument) {
        this->articulationEntries.clear();
        this->articulationSourceIndexes.clear();
        if (this->selectedWholeInstrumentItemIndex >= 0 && this->selectedWholeInstrumentItemIndex < static_cast<int>(this->allItems.size())) {
            const auto selectedInstrumentKey = SourcePickerPopup::wholeInstrumentKey(this->allItems[static_cast<size_t>(this->selectedWholeInstrumentItemIndex)]);
            std::set<juce::String> seenArticulations;
            for (int itemIndex = 0; itemIndex < static_cast<int>(this->allItems.size()); ++itemIndex) {
                const auto& item = this->allItems[static_cast<size_t>(itemIndex)];
                if (SourcePickerPopup::wholeInstrumentKey(item) != selectedInstrumentKey) continue;
                const auto articulationName = item.artCode.isNotEmpty() ? item.artCode : juce::String("(default)");
                if (!seenArticulations.insert(articulationName.toLowerCase()).second) continue;
                this->articulationEntries.push_back(articulationName);
                this->articulationSourceIndexes.push_back(itemIndex);
            }
        }
        this->articulationListBox.updateContent();
        juce::SparseSet<int> selectedRows;
        this->articulationListBox.setSelectedRows(selectedRows, juce::dontSendNotification);
        return;
    }

    std::set<juce::String> availableArticulations;
    for (const auto& item : this->allItems) {
        if (!this->matchesCategory(item.instType)) continue;
        if (this->selectedKind != KitLib::InstrumentSpec::Type::Unknown && item.instType != this->selectedKind) continue;
        const auto articulationName = ArticulationNameHelpers::normalizeArticulationName(item.rawArtCode.isNotEmpty() ? item.rawArtCode : item.artCode);
        if (articulationName.isNotEmpty()) availableArticulations.insert(articulationName);
    }

    this->articulationEntries.assign(availableArticulations.begin(), availableArticulations.end());
    for (auto selectedIterator = this->selectedArticulations.begin(); selectedIterator != this->selectedArticulations.end();) {
        if (availableArticulations.find(*selectedIterator) == availableArticulations.end())
            selectedIterator = this->selectedArticulations.erase(selectedIterator);
        else
            ++selectedIterator;
    }

    this->articulationListBox.updateContent();
    juce::SparseSet<int> selectedRows;
    for (int idx = 0; idx < static_cast<int>(this->articulationEntries.size()); ++idx) {
        if (this->selectedArticulations.find(this->articulationEntries[static_cast<size_t>(idx)]) != this->selectedArticulations.end())
            selectedRows.addRange({ idx, idx + 1 });
    }
    this->articulationListBox.setSelectedRows(selectedRows, juce::dontSendNotification);
}

bool SourcePickerPopup::matchesCurrentFilters(const SourceItem& item, const juce::String& lowerDisplayText, const juce::String& filterText) const {
    if (!this->matchesCategory(item.instType))
        return false;
    if (this->selectedKind != KitLib::InstrumentSpec::Type::Unknown && item.instType != this->selectedKind)
        return false;
    if (this->selectionMode == SelectionMode::Articulation && !this->selectedArticulations.empty()) {
        const auto articulationName = ArticulationNameHelpers::normalizeArticulationName(item.rawArtCode.isNotEmpty() ? item.rawArtCode : item.artCode);
        if (this->selectedArticulations.find(articulationName) == this->selectedArticulations.end()) return false;
    }

    juce::StringArray filterTerms;
    filterTerms.addTokens(filterText, " \t\r\n", "");
    filterTerms.removeEmptyStrings();
    for (const auto& filterTerm : filterTerms) {
        if (filterTerm.startsWithChar('-') && filterTerm.length() > 1) {
            if (lowerDisplayText.contains(filterTerm.substring(1))) return false;
        } else if (filterTerm != "-" && !lowerDisplayText.contains(filterTerm)) {
            return false;
        }
    }
    return true;
}

void SourcePickerPopup::rebuildFiltered() {
    this->stopTimer();
    this->listBox.deselectAllRows();
    this->filteredEntries.clear();
    this->actionRow = -1;
    this->actionRowBeforeMouseSelection = -1;
    this->mouseSelectionChanged = false;
    auto filterText = this->filterField.getText().toLowerCase();
    std::set<juce::String> addedInstruments;

    for (int idx = 0; idx < static_cast<int>(this->allItems.size()); ++idx) {
        const auto& item = this->allItems[static_cast<size_t>(idx)];
        if (!this->matchesCurrentFilters(item, this->cachedLowerDisplayText[static_cast<size_t>(idx)], filterText))
            continue;
        if (this->selectionMode == SelectionMode::WholeInstrument && !addedInstruments.insert(SourcePickerPopup::wholeInstrumentKey(item)).second) continue;
        this->filteredEntries.push_back({ idx });
    }

    if (this->selectionMode == SelectionMode::WholeInstrument) {
        this->selectedWholeInstrumentItemIndex = -1;
        this->rebuildArticulationEntries();
    }
    this->listBox.updateContent();
}

void SourcePickerPopup::updateCategoryButtons() {
    auto apply = [this](juce::TextButton& button, Category category) {
        const bool selected = this->selectedCategory == category;
        button.setColour(juce::TextButton::buttonColourId, selected ? UITheme::Colours::selectionStrong : UITheme::Colours::bgControl);
        button.setColour(juce::TextButton::textColourOffId, selected ? UITheme::Colours::textPrimary : UITheme::Colours::textSecondary);
        button.repaint();
    };

    apply(this->allCategoryButton, Category::All);
    apply(this->drumsCategoryButton, Category::Drums);
    apply(this->cymbalsCategoryButton, Category::Cymbals);
    apply(this->otherCategoryButton, Category::Other);
}

void SourcePickerPopup::confirmItem(int originalIndex) {
    if (originalIndex < 0 || originalIndex >= static_cast<int>(this->allItems.size())) return;
    if (this->selectionMode == SelectionMode::Articulation) {
        if (this->onItemSelected) this->onItemSelected(originalIndex);
        return;
    }

    const auto& item = this->allItems[static_cast<size_t>(originalIndex)];
    if (!this->onInstrumentSelected) return;
    InstrumentSelection selection;
    selection.kitId = item.kitCodeRaw;
    selection.instrumentId = item.instCode;
    this->onInstrumentSelected(selection);
}

void SourcePickerPopup::timerCallback() {
    this->rebuildFiltered();
}

void SourcePickerPopup::queueRebuildFiltered() {
    this->startTimer(filterDebounceMs);
}

int SourcePickerPopup::KindListModel::getNumRows() {
    return static_cast<int>(this->owner.kindEntries.size()) + 1;
}

void SourcePickerPopup::KindListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    const bool selected = (rowNumber == 0 && this->owner.selectedKind == KitLib::InstrumentSpec::Type::Unknown)
        || (rowNumber > 0
            && rowNumber - 1 < static_cast<int>(this->owner.kindEntries.size())
            && this->owner.kindEntries[static_cast<size_t>(rowNumber - 1)] == this->owner.selectedKind);

    g.fillAll(selected || rowIsSelected
        ? UITheme::Colours::listSelection
        : (rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd));

    juce::String text = "all";
    if (rowNumber > 0 && rowNumber - 1 < static_cast<int>(this->owner.kindEntries.size()))
        text = kindLabel(this->owner.kindEntries[static_cast<size_t>(rowNumber - 1)]);

    g.setColour(selected ? UITheme::Colours::textPrimary : UITheme::Colours::textSecondary);
    g.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    g.drawText(text, 6, 0, width - 10, height, juce::Justification::centredLeft);
}

void SourcePickerPopup::KindListModel::listBoxItemClicked(int row, const juce::MouseEvent& event) {
    juce::ignoreUnused(event);
    if (row <= 0) {
        this->owner.selectedKind = KitLib::InstrumentSpec::Type::Unknown;
    } else if (row - 1 < static_cast<int>(this->owner.kindEntries.size())) {
        this->owner.selectedKind = this->owner.kindEntries[static_cast<size_t>(row - 1)];
    } else {
        return;
    }

    this->owner.kindListBox.selectRow(row);
    this->owner.kindListBox.repaint();
    this->owner.rebuildArticulationEntries();
    this->owner.rebuildFiltered();
}

int SourcePickerPopup::ArticulationListModel::getNumRows() {
    return static_cast<int>(this->owner.articulationEntries.size());
}

void SourcePickerPopup::ArticulationListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.articulationEntries.size())) return;
    g.fillAll(rowIsSelected ? UITheme::Colours::listSelection : (rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd));
    g.setColour(rowIsSelected ? UITheme::Colours::textPrimary : UITheme::Colours::textSecondary);
    g.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    g.drawText(this->owner.articulationEntries[static_cast<size_t>(rowNumber)], 6, 0, width - 10, height, juce::Justification::centredLeft);
}

void SourcePickerPopup::ArticulationListModel::selectedRowsChanged(int lastRowSelected) {
    if (this->owner.selectionMode == SelectionMode::WholeInstrument) {
        if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(this->owner.articulationSourceIndexes.size())) return;
        const int sourceIndex = this->owner.articulationSourceIndexes[static_cast<size_t>(lastRowSelected)];
        if (this->owner.onPreview) this->owner.onPreview(sourceIndex, static_cast<juce::uint8>(127));
        return;
    }

    juce::ignoreUnused(lastRowSelected);
    this->owner.selectedArticulations.clear();
    for (int idx = 0; idx < this->owner.articulationListBox.getNumSelectedRows(); ++idx) {
        const int selectedRow = this->owner.articulationListBox.getSelectedRow(idx);
        if (selectedRow >= 0 && selectedRow < static_cast<int>(this->owner.articulationEntries.size()))
            this->owner.selectedArticulations.insert(this->owner.articulationEntries[static_cast<size_t>(selectedRow)]);
    }
    this->owner.rebuildFiltered();
}

int SourcePickerPopup::ListModel::getNumRows() {
    return this->owner.filteredEntries.empty() ? 1 : static_cast<int>(this->owner.filteredEntries.size());
}

void SourcePickerPopup::ListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (this->owner.filteredEntries.empty()) {
        g.fillAll(UITheme::Colours::bgControlAlt);
        g.setColour(UITheme::Colours::textMuted);
        g.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));

        const auto emptyMessage = this->owner.allItems.empty()
            ? juce::String("No sources available")
            : juce::String("No matching sources");
        const auto emptyHint = this->owner.allItems.empty()
            ? juce::String("Check your sample library path")
            : juce::String("Try a different search or category");

        g.drawText(emptyMessage, 8, 1, width - 16, height / 2, juce::Justification::centredLeft);
        g.setColour(UITheme::Colours::textMuted.withAlpha(0.75f));
        g.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
        g.drawText(emptyHint, 8, height / 2 - 1, width - 16, height / 2, juce::Justification::centredLeft);
        return;
    }

    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.filteredEntries.size())) return;

    auto originalIdx = this->owner.filteredEntries[static_cast<size_t>(rowNumber)].originalIndex;
    const auto& item = this->owner.allItems[static_cast<size_t>(originalIdx)];
    const int btnW = SourcePickerPopup::selectButtonWidth;
    const int prevW = SourcePickerPopup::previewButtonWidth;
    const bool showActions = rowNumber == this->owner.actionRow;
    const int actionWidth = showActions ? btnW + prevW : 0;
    const int textWidth = width - actionWidth;

    g.fillAll(rowIsSelected ? UITheme::Colours::listSelection : (rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd));

    g.setColour(UITheme::Colours::textPrimary);
    g.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    const auto displayText = this->owner.selectionMode == SelectionMode::WholeInstrument ? SourcePickerPopup::wholeInstrumentDisplayText(item) : item.displayText();
    g.drawText(displayText, 4, 0, textWidth - 8, height, juce::Justification::centredLeft);

    if (!showActions) return;

    auto prevArea = juce::Rectangle<int>(textWidth, 0, prevW, height).reduced(2, 3);
    juce::Path previewTriangle;
    previewTriangle.addTriangle(static_cast<float>(prevArea.getX()), static_cast<float>(prevArea.getBottom()),
                                static_cast<float>(prevArea.getRight()), static_cast<float>(prevArea.getBottom()),
                                static_cast<float>(prevArea.getRight()), static_cast<float>(prevArea.getY()));
    g.setColour(UITheme::Colours::accentSoft);
    g.fillPath(previewTriangle);

    auto btnArea = juce::Rectangle<int>(textWidth + prevW, 0, btnW, height).withSizeKeepingCentre(btnW, btnW).reduced(1);
    g.setColour(UITheme::Colours::borderMid);
    g.fillRoundedRectangle(btnArea.toFloat(), 2.0f);
    g.setColour(UITheme::Colours::textPrimary.withAlpha(0.8f));
    g.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    g.drawText(juce::String::charToString(0x2713), btnArea, juce::Justification::centred);
}

void SourcePickerPopup::ListModel::listBoxItemClicked(int row, const juce::MouseEvent& event) {
    if (this->owner.filteredEntries.empty()) return;
    if (row < 0 || row >= static_cast<int>(this->owner.filteredEntries.size())) return;

    const auto originalIdx = this->owner.filteredEntries[static_cast<size_t>(row)].originalIndex;
    const bool wasActionRow = this->owner.mouseSelectionChanged ? this->owner.actionRowBeforeMouseSelection == row : this->owner.actionRow == row;
    this->owner.mouseSelectionChanged = false;
    this->owner.actionRowBeforeMouseSelection = -1;
    this->owner.actionRow = row;
    this->owner.listBox.selectRow(row);
    this->owner.listBox.repaint();

    if (!wasActionRow) {
        if (this->owner.onPreview) this->owner.onPreview(originalIdx, static_cast<juce::uint8>(127));
        return;
    }

    const int btnW = SourcePickerPopup::selectButtonWidth;
    const int prevW = SourcePickerPopup::previewButtonWidth;
    const int totalWidth = this->owner.listBox.getWidth();
    const int textWidth = totalWidth - btnW - prevW;
    const int clickX = event.getPosition().getX();

    if (clickX >= textWidth + prevW) {
        this->owner.confirmItem(originalIdx);
    } else if (clickX >= textWidth) {
        if (this->owner.onPreview) {
            const float xRatio = juce::jlimit(0.0f, 1.0f, static_cast<float>(clickX - textWidth) / static_cast<float>(juce::jmax(1, prevW)));
            const juce::uint8 velocity = static_cast<juce::uint8>(juce::jlimit(1, 127, static_cast<int>(xRatio * 127.0f)));
            this->owner.onPreview(originalIdx, velocity);
        }
    } else {
        if (this->owner.onPreview) this->owner.onPreview(originalIdx, static_cast<juce::uint8>(127));
    }
}

void SourcePickerPopup::ListModel::selectedRowsChanged(int lastRowSelected) {
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(this->owner.filteredEntries.size())) {
        this->owner.actionRow = -1;
        if (this->owner.selectionMode == SelectionMode::WholeInstrument) {
            this->owner.selectedWholeInstrumentItemIndex = -1;
            this->owner.rebuildArticulationEntries();
        }
        this->owner.listBox.repaint();
        return;
    }

    if (this->owner.listBox.isMouseButtonDown(true)) {
        this->owner.actionRowBeforeMouseSelection = this->owner.actionRow;
        this->owner.mouseSelectionChanged = true;
    }
    this->owner.actionRow = lastRowSelected;
    const int originalIndex = this->owner.filteredEntries[static_cast<size_t>(lastRowSelected)].originalIndex;
    if (this->owner.selectionMode == SelectionMode::WholeInstrument) {
        this->owner.selectedWholeInstrumentItemIndex = originalIndex;
        this->owner.rebuildArticulationEntries();
    }
    this->owner.listBox.repaint();

    if (!this->owner.suppressSelectionPreview && !this->owner.listBox.isMouseButtonDown(true) && this->owner.onPreview) {
        this->owner.onPreview(originalIndex, static_cast<juce::uint8>(127));
    }
}

void SourcePickerPopup::ListModel::returnKeyPressed(int lastRowSelected) {
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(this->owner.filteredEntries.size())) return;
    const int originalIdx = this->owner.filteredEntries[static_cast<size_t>(lastRowSelected)].originalIndex;
    this->owner.confirmItem(originalIdx);
}
