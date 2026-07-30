#include "KitPreviewGrid.h"

#include "UITheme.h"
#include <cmath>
#include <utility>

void KitPreviewGrid::setItems(std::vector<Item> newItems) {
    this->items = std::move(newItems);
    this->flashedItemIndex = -1;
    this->resized();
    this->repaint();
}

int KitPreviewGrid::getPreferredHeight(int availableWidth) {
    return this->layoutItems(availableWidth, false);
}

int KitPreviewGrid::layoutItems(int availableWidth, bool storeBounds) {
    if (storeBounds) this->itemBounds.clear();
    if (this->items.empty()) return 48;

    const int contentWidth = juce::jmax(this->preferredPadWidth, availableWidth - this->padGap * 2);
    const int columnCount = juce::jmax(1, contentWidth / (this->preferredPadWidth + this->padGap));
    const int padWidth = juce::jmax(72, (contentWidth - (columnCount - 1) * this->padGap) / columnCount);
    int currentY = this->padGap;
    size_t itemIndex = 0;

    while (itemIndex < this->items.size()) {
        const auto groupId = this->items[itemIndex].groupId;
        currentY += this->groupHeaderHeight;
        int groupItemCount = 0;
        while (itemIndex + static_cast<size_t>(groupItemCount) < this->items.size() && this->items[itemIndex + static_cast<size_t>(groupItemCount)].groupId == groupId)
            ++groupItemCount;

        const int rowCount = static_cast<int>(std::ceil(static_cast<double>(groupItemCount) / static_cast<double>(columnCount)));
        if (storeBounds) {
            for (int groupItemIndex = 0; groupItemIndex < groupItemCount; ++groupItemIndex) {
                const int columnIndex = groupItemIndex % columnCount;
                const int rowIndex = groupItemIndex / columnCount;
                ItemBounds itemBound;
                itemBound.itemIndex = itemIndex + static_cast<size_t>(groupItemIndex);
                itemBound.bounds = { this->padGap + columnIndex * (padWidth + this->padGap), currentY + rowIndex * (this->padHeight + this->padGap), padWidth, this->padHeight };
                this->itemBounds.push_back(itemBound);
            }
        }

        currentY += rowCount * (this->padHeight + this->padGap);
        currentY += this->groupGap;
        itemIndex += static_cast<size_t>(groupItemCount);
    }

    return currentY + this->padGap;
}

void KitPreviewGrid::resized() {
    this->layoutItems(this->getWidth(), true);
}

void KitPreviewGrid::paint(juce::Graphics& graphics) {
    graphics.fillAll(UITheme::Colours::bgEditor);
    if (this->items.empty()) {
        graphics.setColour(UITheme::Colours::textMuted);
        graphics.setFont(UITheme::Fonts::normal());
        graphics.drawText("No instruments to preview", this->getLocalBounds().reduced(8), juce::Justification::centred);
        return;
    }

    juce::String lastGroupId;
    for (const auto& itemBound : this->itemBounds) {
        if (itemBound.itemIndex >= this->items.size()) continue;
        const auto& item = this->items[itemBound.itemIndex];
        if (item.groupId != lastGroupId) {
            const int titleY = itemBound.bounds.getY() - this->groupHeaderHeight;
            graphics.setColour(UITheme::Colours::textSecondary);
            graphics.setFont(UITheme::Fonts::labelBoldMedium());
            graphics.drawText(item.groupTitle, this->padGap, titleY, this->getWidth() - this->padGap * 2, this->groupHeaderHeight, juce::Justification::centredLeft);
            lastGroupId = item.groupId;
        }

        auto colour = UITheme::Colours::instrumentType(item.source.instType);
        if (static_cast<int>(itemBound.itemIndex) == this->flashedItemIndex) colour = colour.brighter(0.65f);
        graphics.setColour(colour.withMultipliedBrightness(0.58f));
        graphics.fillRoundedRectangle(itemBound.bounds.toFloat(), 3.0f);
        graphics.setColour(colour.brighter(0.35f));
        graphics.drawRoundedRectangle(itemBound.bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
        graphics.setColour(UITheme::Colours::textPrimary);
        graphics.setFont(UITheme::Fonts::normal());
        const auto label = item.source.artCode.isNotEmpty() ? item.source.artCode : juce::String("(default)");
        graphics.drawFittedText(label, itemBound.bounds.reduced(6, 2), juce::Justification::centred, 1);
    }
}

void KitPreviewGrid::mouseDown(const juce::MouseEvent& event) {
    for (const auto& itemBound : this->itemBounds) {
        if (!itemBound.bounds.contains(event.getPosition()) || itemBound.itemIndex >= this->items.size()) continue;
        this->flashedItemIndex = static_cast<int>(itemBound.itemIndex);
        this->repaint(itemBound.bounds.expanded(1));
        this->startTimer(160);
        const float positionRatio = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.getPosition().getX() - itemBound.bounds.getX()) / static_cast<float>(juce::jmax(1, itemBound.bounds.getWidth())));
        const auto velocity = static_cast<juce::uint8>(juce::jlimit(1, 127, static_cast<int>(positionRatio * 127.0f)));
        if (this->onPreview) this->onPreview(this->items[itemBound.itemIndex].source, velocity);
        return;
    }
}

void KitPreviewGrid::timerCallback() {
    this->stopTimer();
    this->flashedItemIndex = -1;
    this->repaint();
}
