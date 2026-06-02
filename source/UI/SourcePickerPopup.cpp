#include "SourcePickerPopup.h"
#include "UITheme.h"

SourcePickerPopup::SourcePickerPopup(const std::vector<SourceItem>& items)
    : allItems(items) {
    this->filterField.setTextToShowWhenEmpty("filter...", UITheme::Colours::textMuted);
    this->filterField.onTextChange = [this] { this->queueRebuildFiltered(); };
    addAndMakeVisible(this->filterField);

    this->relatedOnlyToggle.setToggleState(true, juce::dontSendNotification);
    this->relatedOnlyToggle.onClick = [this] { this->rebuildFiltered(); };
    addAndMakeVisible(this->relatedOnlyToggle);

    this->listBox.setModel(&this->listModel);
    this->listBox.setRowHeight(18);
    this->listBox.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    addAndMakeVisible(this->listBox);

    this->cachedLowerDisplayText.reserve(this->allItems.size());
    for (const auto& item : this->allItems) {
        this->cachedLowerDisplayText.push_back(item.displayText().toLowerCase());
    }
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
    g.fillAll(UITheme::Colours::bgPanelRaised);
    g.setColour(UITheme::Colours::borderStrong);
    g.drawRect(getLocalBounds(), 1);
}

void SourcePickerPopup::rebuildFiltered() {
    this->stopTimer();
    this->filteredEntries.clear();
    auto filterText = this->filterField.getText().toLowerCase();
    bool relatedOnly = this->relatedOnlyToggle.getToggleState() && this->filterType != KitLib::InstrumentSpec::Type::Unknown;

    for (int idx = 0; idx < static_cast<int>(this->allItems.size()); ++idx) {
        const auto& item = this->allItems[static_cast<size_t>(idx)];
        if (relatedOnly && item.instType != this->filterType)
            continue;
        if (filterText.isNotEmpty() && !this->cachedLowerDisplayText[static_cast<size_t>(idx)].contains(filterText))
            continue;
        this->filteredEntries.push_back({ idx });
    }

    this->listBox.updateContent();
}

void SourcePickerPopup::timerCallback() {
    this->rebuildFiltered();
}

void SourcePickerPopup::queueRebuildFiltered() {
    this->startTimer(filterDebounceMs);
}

int SourcePickerPopup::ListModel::getNumRows() {
    return this->owner.filteredEntries.empty() ? 1 : static_cast<int>(this->owner.filteredEntries.size());
}

void SourcePickerPopup::ListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (this->owner.filteredEntries.empty()) {
        g.fillAll(UITheme::Colours::bgControlAlt);
        g.setColour(UITheme::Colours::textMuted);
        g.setFont(UITheme::Fonts::small());


        const auto emptyMessage = this->owner.allItems.empty()
            ? juce::String("No sources available")
            : juce::String("No matching sources");
        const auto emptyHint = this->owner.allItems.empty()
            ? juce::String("Check your sample library path")
            : juce::String("Try a different filter or disable related");

        g.drawText(emptyMessage, 8, 1, width - 16, height / 2, juce::Justification::centredLeft);
        g.setColour(UITheme::Colours::textMuted.withAlpha(0.75f));
        g.setFont(UITheme::Fonts::small().withHeight(10.0f));
        g.drawText(emptyHint, 8, height / 2 - 1, width - 16, height / 2, juce::Justification::centredLeft);
        return;
    }

    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.filteredEntries.size())) 
        return;
    auto originalIdx = this->owner.filteredEntries[static_cast<size_t>(rowNumber)].originalIndex;
    const auto& item = this->owner.allItems[static_cast<size_t>(originalIdx)];
    const int btnW = SourcePickerPopup::selectButtonWidth;
    const int prevW = SourcePickerPopup::previewButtonWidth;
    int textWidth = width - btnW - prevW;
    g.fillAll(rowIsSelected 
        ? UITheme::Colours::listSelection      
        : (rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd)
    );

    // Item text area (left portion — click for max-velocity zone preview)
    g.setColour(UITheme::Colours::textPrimary);
    g.setFont(UITheme::Fonts::table());
    g.drawText(item.displayText(), 4, 0, textWidth - 8, height, juce::Justification::centredLeft);

    // Preview button area (middle — velocity-aware preview with triangle indicator)
    auto prevArea = juce::Rectangle<int>(textWidth, 0, prevW, height).reduced(1);
    g.setColour(UITheme::Colours::bgControlRaised);
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
        g.setColour(UITheme::Colours::accentSoft);
        g.fillPath(triangle);
        // "preview" text
        triArea.removeFromLeft(2);
        g.setColour(UITheme::Colours::textPrimary.withAlpha(0.7f));
        g.setFont(UITheme::Fonts::small().withHeight(8.0f));
        g.drawText("preview", triArea, juce::Justification::centredLeft);
    }

    // Select/Use button area (right portion)
    auto btnArea = juce::Rectangle<int>(textWidth + prevW, 0, btnW, height).reduced(1);
    g.setColour(UITheme::Colours::borderMid);
    g.fillRoundedRectangle(btnArea.toFloat(), 2.0f);
    g.setColour(UITheme::Colours::textPrimary.withAlpha(0.8f));
    g.setFont(UITheme::Fonts::small());
    g.drawText("use", btnArea, juce::Justification::centred);
}

void SourcePickerPopup::ListModel::listBoxItemClicked(int row, const juce::MouseEvent& event) {
    if (this->owner.filteredEntries.empty())
        return;
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
        if (this->owner.onItemSelected) this->owner.onItemSelected(originalIdx);
    } else if (clickX >= textWidth) {
        // Clicked preview button — velocity from x-position within preview area
        if (this->owner.onPreview) {
            float xRatio = juce::jlimit(0.0f, 1.0f, static_cast<float>(clickX - textWidth) / static_cast<float>(juce::jmax(1, prevW)));
            juce::uint8 velocity = static_cast<juce::uint8>(juce::jlimit(1, 127, static_cast<int>(xRatio * 127.0f)));
            this->owner.onPreview(originalIdx, velocity);
        }
    } else {
        // Clicked item text — zone preview at max velocity
        if (this->owner.onPreview) this->owner.onPreview(originalIdx, static_cast<juce::uint8>(127));
    }
}
