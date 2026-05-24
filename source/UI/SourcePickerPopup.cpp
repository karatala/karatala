#include "SourcePickerPopup.h"

SourcePickerPopup::SourcePickerPopup(const std::vector<SourceItem>& items)
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

void SourcePickerPopup::setFilterType(KitLib::InstrumentSpec::Type type) {
    this->filterType = type;
    this->rebuildFiltered();
}

void SourcePickerPopup::resized() {
    auto area = getLocalBounds().reduced(4);
    auto topRow = area.removeFromTop(22);
    this->filterField.setBounds(topRow.removeFromLeft(topRow.getWidth() - 70));
    topRow.removeFromLeft(4);
    this->relatedOnlyToggle.setBounds(topRow);
    area.removeFromTop(2);
    this->listBox.setBounds(area);
}

void SourcePickerPopup::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff222222));
    g.setColour(juce::Colour(0xff555555));
    g.drawRect(getLocalBounds(), 1);
}

void SourcePickerPopup::rebuildFiltered() {
    this->filteredEntries.clear();
    auto filterText = this->filterField.getText().toLowerCase();
    bool relatedOnly = this->relatedOnlyToggle.getToggleState()
                       && this->filterType != KitLib::InstrumentSpec::Type::Unknown;

    for (int idx = 0; idx < static_cast<int>(this->allItems.size()); ++idx) {
        const auto& item = this->allItems[static_cast<size_t>(idx)];
        if (relatedOnly && item.instType != this->filterType)
            continue;
        if (filterText.isNotEmpty() && !item.displayText().toLowerCase().contains(filterText))
            continue;
        this->filteredEntries.push_back({ idx });
    }

    this->listBox.updateContent();
    this->listBox.repaint();
}

int SourcePickerPopup::ListModel::getNumRows() {
    return static_cast<int>(this->owner.filteredEntries.size());
}

void SourcePickerPopup::ListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.filteredEntries.size()))
        return;
    auto originalIdx = this->owner.filteredEntries[static_cast<size_t>(rowNumber)].originalIndex;
    const auto& item = this->owner.allItems[static_cast<size_t>(originalIdx)];
    g.fillAll(rowIsSelected ? juce::Colour(0xff3a5a8a) : (rowNumber % 2 == 0 ? juce::Colour(0xff2a2a2a) : juce::Colour(0xff2e2e2e)));
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(item.displayText(), 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void SourcePickerPopup::ListModel::listBoxItemClicked(int row, const juce::MouseEvent& event) {
    if (row < 0 || row >= static_cast<int>(this->owner.filteredEntries.size()))
        return;
    auto originalIdx = this->owner.filteredEntries[static_cast<size_t>(row)].originalIndex;
    
    // Handle preview callback (used by InstrumentConfigPanel)
    if (this->owner.onPreview) {
        float xRatio = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.getPosition().getX()) / static_cast<float>(juce::jmax(1, this->owner.listBox.getRowPosition(row, true).getWidth())));
        juce::uint8 velocity = static_cast<juce::uint8>(juce::jlimit(1, 127, static_cast<int>(xRatio * 127.0f)));
        this->owner.onPreview(originalIdx, velocity);
    }
    
    // Handle selection callback (used by both panels)
    if (this->owner.onItemSelected)
        this->owner.onItemSelected(originalIdx);
}
