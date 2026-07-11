#include "SourcePickerPopup.h"
#include "UIHelpers.h"
#include "UITheme.h"

#include <algorithm>
#include <set>

SourcePickerPopup::SourcePickerPopup(const std::vector<SourceItem>& items, const InstrumentCatalog& instrumentCatalog)
    : allItems(items)
    , catalog(instrumentCatalog) {
    this->filterField.setFont(UITheme::Fonts::comboBox().withHeight(14.0f));
    this->filterField.setTextToShowWhenEmpty("filter...", UITheme::Colours::textMuted);
    this->filterField.onTextChange = [this] { this->queueRebuildFiltered(); };
    addAndMakeVisible(this->filterField);

    for (auto* button : { &this->allCategoryButton, &this->drumsCategoryButton, &this->cymbalsCategoryButton, &this->otherCategoryButton }) {
        UIHelpers::configureCategoryButton(*button);
        addAndMakeVisible(*button);
    }
    this->allCategoryButton.onClick = [this] { this->setCategory(Category::All); };
    this->drumsCategoryButton.onClick = [this] { this->setCategory(Category::Drums); };
    this->cymbalsCategoryButton.onClick = [this] { this->setCategory(Category::Cymbals); };
    this->otherCategoryButton.onClick = [this] { this->setCategory(Category::Other); };

    this->kindListBox.setModel(&this->kindListModel);
    this->kindListBox.setRowHeight(22);
    this->kindListBox.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    addAndMakeVisible(this->kindListBox);

    this->listBox.setModel(&this->listModel);
    this->listBox.setRowHeight(22);
    this->listBox.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    addAndMakeVisible(this->listBox);

    this->cachedLowerDisplayText.reserve(this->allItems.size());
    for (const auto& item : this->allItems)
        this->cachedLowerDisplayText.push_back(item.displayText().toLowerCase());

    this->rebuildKindEntries();
    this->rebuildFiltered();
    this->updateCategoryButtons();
}

void SourcePickerPopup::setFilterType(KitLib::InstrumentSpec::Type type) {
    this->filterType = type;
    this->selectedCategory = this->categoryForType(type);
    this->selectedKind = type;
    this->rebuildKindEntries();
    this->rebuildFiltered();
    this->updateCategoryButtons();
}

void SourcePickerPopup::resized() {
    auto area = getLocalBounds().reduced(4);
    auto topRow = area.removeFromTop(22);
    const int categoryAreaW = juce::jmin(232, juce::jmax(184, topRow.getWidth() / 2));
    auto categoryArea = topRow.removeFromRight(categoryAreaW);
    this->filterField.setBounds(topRow.reduced(0, 1));
    categoryArea.removeFromLeft(4);

    const int buttonW = categoryArea.getWidth() / 4;
    this->allCategoryButton.setBounds(categoryArea.removeFromLeft(buttonW).reduced(1, 1));
    this->drumsCategoryButton.setBounds(categoryArea.removeFromLeft(buttonW).reduced(1, 1));
    this->cymbalsCategoryButton.setBounds(categoryArea.removeFromLeft(buttonW).reduced(1, 1));
    this->otherCategoryButton.setBounds(categoryArea.reduced(1, 1));

    area.removeFromTop(4);
    auto body = area;
    const int kindsW = juce::jmin(kindListWidth, juce::jmax(70, body.getWidth() / 3));
    this->kindListBox.setBounds(body.removeFromLeft(kindsW));
    body.removeFromLeft(4);
    this->listBox.setBounds(body);
}

void SourcePickerPopup::paint(juce::Graphics& g) {
    g.fillAll(UITheme::Colours::bgPanelRaised);
    g.setColour(UITheme::Colours::borderStrong);
    g.drawRect(getLocalBounds(), 1);
}

void SourcePickerPopup::setCategory(Category category) {
    if (this->selectedCategory == category)
        return;

    this->selectedCategory = category;
    this->selectedKind = KitLib::InstrumentSpec::Type::Unknown;
    this->rebuildKindEntries();
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

bool SourcePickerPopup::matchesCurrentFilters(const SourceItem& item, const juce::String& lowerDisplayText, const juce::String& filterText) const {
    if (!this->matchesCategory(item.instType))
        return false;
    if (this->selectedKind != KitLib::InstrumentSpec::Type::Unknown && item.instType != this->selectedKind)
        return false;
    if (filterText.isNotEmpty() && !lowerDisplayText.contains(filterText))
        return false;
    return true;
}

void SourcePickerPopup::rebuildFiltered() {
    this->stopTimer();
    this->filteredEntries.clear();
    this->actionRow = -1;
    auto filterText = this->filterField.getText().toLowerCase();

    for (int idx = 0; idx < static_cast<int>(this->allItems.size()); ++idx) {
        const auto& item = this->allItems[static_cast<size_t>(idx)];
        if (!this->matchesCurrentFilters(item, this->cachedLowerDisplayText[static_cast<size_t>(idx)], filterText))
            continue;
        this->filteredEntries.push_back({ idx });
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
    g.setFont(UITheme::Fonts::comboBox().withHeight(14.0f));
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
    this->owner.rebuildFiltered();
}

int SourcePickerPopup::ListModel::getNumRows() {
    return this->owner.filteredEntries.empty() ? 1 : static_cast<int>(this->owner.filteredEntries.size());
}

void SourcePickerPopup::ListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (this->owner.filteredEntries.empty()) {
        g.fillAll(UITheme::Colours::bgControlAlt);
        g.setColour(UITheme::Colours::textMuted);
        g.setFont(UITheme::Fonts::comboBox().withHeight(14.0f));

        const auto emptyMessage = this->owner.allItems.empty()
            ? juce::String("No sources available")
            : juce::String("No matching sources");
        const auto emptyHint = this->owner.allItems.empty()
            ? juce::String("Check your sample library path")
            : juce::String("Try a different search or category");

        g.drawText(emptyMessage, 8, 1, width - 16, height / 2, juce::Justification::centredLeft);
        g.setColour(UITheme::Colours::textMuted.withAlpha(0.75f));
        g.setFont(UITheme::Fonts::comboBox().withHeight(14.0f));
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
    g.setFont(UITheme::Fonts::comboBox().withHeight(14.0f));
    g.drawText(item.displayText(), 4, 0, textWidth - 8, height, juce::Justification::centredLeft);

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
    g.setFont(UITheme::Fonts::comboBox().withHeight(14.0f));
    g.drawText(juce::String::charToString(0x2713), btnArea, juce::Justification::centred);
}

void SourcePickerPopup::ListModel::listBoxItemClicked(int row, const juce::MouseEvent& event) {
    if (this->owner.filteredEntries.empty()) return;
    if (row < 0 || row >= static_cast<int>(this->owner.filteredEntries.size())) return;

    auto originalIdx = this->owner.filteredEntries[static_cast<size_t>(row)].originalIndex;
    const bool wasActionRow = this->owner.actionRow == row;
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
        if (this->owner.onItemSelected) this->owner.onItemSelected(originalIdx);
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
