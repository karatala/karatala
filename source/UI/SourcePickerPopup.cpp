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
    const int btnW = SourcePickerPopup::selectButtonWidth;
    const int prevW = SourcePickerPopup::previewButtonWidth;
    int textWidth = width - btnW - prevW;
    g.fillAll(rowIsSelected ? juce::Colour(0xff3a5a8a) : (rowNumber % 2 == 0 ? juce::Colour(0xff2a2a2a) : juce::Colour(0xff2e2e2e)));

    // Item text area (left portion — click for max-velocity zone preview)
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(item.displayText(), 4, 0, textWidth - 8, height, juce::Justification::centredLeft);

    // Preview button area (middle — velocity-aware preview with triangle indicator)
    auto prevArea = juce::Rectangle<int>(textWidth, 0, prevW, height).reduced(1);
    g.setColour(juce::Colour(0xff3a3a3a));
    g.fillRoundedRectangle(prevArea.toFloat(), 2.0f);
    // Draw volume-level triangle (left-to-right descending like a volume indicator)
    {
        auto triArea = prevArea.reduced(2, 3);
        int triW = juce::jmin(14, triArea.getWidth() / 3);
        auto triRect = triArea.removeFromLeft(triW);
        juce::Path triangle;
        triangle.addTriangle(
            static_cast<float>(triRect.getX()), static_cast<float>(triRect.getBottom()),
            static_cast<float>(triRect.getRight()), static_cast<float>(triRect.getBottom()),
            static_cast<float>(triRect.getRight()), static_cast<float>(triRect.getY())
        );
        g.setColour(juce::Colour(0xff88aacc));
        g.fillPath(triangle);
        // "preview" text
        triArea.removeFromLeft(2);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.setFont(juce::Font(juce::FontOptions(8.0f)));
        g.drawText("preview", triArea, juce::Justification::centredLeft);
    }

    // Select/Use button area (right portion)
    auto btnArea = juce::Rectangle<int>(textWidth + prevW, 0, btnW, height).reduced(1);
    g.setColour(juce::Colour(0xff444444));
    g.fillRoundedRectangle(btnArea.toFloat(), 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.drawText("use", btnArea, juce::Justification::centred);
}

void SourcePickerPopup::ListModel::listBoxItemClicked(int row, const juce::MouseEvent& event) {
    if (row < 0 || row >= static_cast<int>(this->owner.filteredEntries.size()))
        return;
    auto originalIdx = this->owner.filteredEntries[static_cast<size_t>(row)].originalIndex;

    const int btnW = SourcePickerPopup::selectButtonWidth;
    const int prevW = SourcePickerPopup::previewButtonWidth;
    int totalWidth = this->owner.listBox.getWidth();
    int textWidth = totalWidth - btnW - prevW;
    int clickX = event.getPosition().getX();

    if (clickX >= textWidth + prevW) {
        // Clicked "use" button — confirm selection
        if (this->owner.onItemSelected)
            this->owner.onItemSelected(originalIdx);
    } else if (clickX >= textWidth) {
        // Clicked preview button — velocity from x-position within preview area
        if (this->owner.onPreview) {
            float xRatio = juce::jlimit(0.0f, 1.0f, static_cast<float>(clickX - textWidth) / static_cast<float>(juce::jmax(1, prevW)));
            juce::uint8 velocity = static_cast<juce::uint8>(juce::jlimit(1, 127, static_cast<int>(xRatio * 127.0f)));
            this->owner.onPreview(originalIdx, velocity);
        }
    } else {
        // Clicked item text — zone preview at max velocity
        if (this->owner.onPreview)
            this->owner.onPreview(originalIdx, static_cast<juce::uint8>(127));
    }
}
