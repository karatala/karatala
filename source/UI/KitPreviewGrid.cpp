#include "KitPreviewGrid.h"

#include "UITheme.h"
#include <cmath>
#include <utility>

void KitPreviewGrid::setArticulationPads(std::vector<ArticulationPad> newArticulationPads) {
    this->articulationPads = std::move(newArticulationPads);
    this->flashedArticulationPadIndex = -1;
    this->resized();
    this->repaint();
}

int KitPreviewGrid::getPreferredHeight(int availableWidth) {
    return this->layoutArticulationPads(availableWidth, false);
}

int KitPreviewGrid::layoutArticulationPads(int availableWidth, bool storeBounds) {
    if (storeBounds) this->articulationPadBounds.clear();
    if (this->articulationPads.empty()) return 48;

    const int contentWidth = juce::jmax(this->preferredPadWidth, availableWidth - this->padGap * 2);
    const int columnCount = juce::jmax(1, contentWidth / (this->preferredPadWidth + this->padGap));
    const int padWidth = juce::jmax(72, (contentWidth - (columnCount - 1) * this->padGap) / columnCount);
    int currentY = this->padGap;
    size_t articulationPadIndex = 0;

    while (articulationPadIndex < this->articulationPads.size()) {
        const auto groupId = this->articulationPads[articulationPadIndex].groupId;
        currentY += this->groupHeaderHeight;
        int groupPadCount = 0;
        while (articulationPadIndex + static_cast<size_t>(groupPadCount) < this->articulationPads.size() && this->articulationPads[articulationPadIndex + static_cast<size_t>(groupPadCount)].groupId == groupId)
            ++groupPadCount;

        const int rowCount = static_cast<int>(std::ceil(static_cast<double>(groupPadCount) / static_cast<double>(columnCount)));
        if (storeBounds) {
            for (int groupPadIndex = 0; groupPadIndex < groupPadCount; ++groupPadIndex) {
                const int columnIndex = groupPadIndex % columnCount;
                const int rowIndex = groupPadIndex / columnCount;
                ArticulationPadBounds padBounds;
                padBounds.articulationPadIndex = articulationPadIndex + static_cast<size_t>(groupPadIndex);
                padBounds.bounds = { this->padGap + columnIndex * (padWidth + this->padGap), currentY + rowIndex * (this->padHeight + this->padGap), padWidth, this->padHeight };
                this->articulationPadBounds.push_back(padBounds);
            }
        }

        currentY += rowCount * (this->padHeight + this->padGap);
        currentY += this->groupGap;
        articulationPadIndex += static_cast<size_t>(groupPadCount);
    }

    return currentY + this->padGap;
}

void KitPreviewGrid::resized() {
    this->layoutArticulationPads(this->getWidth(), true);
}

void KitPreviewGrid::paint(juce::Graphics& graphics) {
    graphics.fillAll(UITheme::Colours::bgEditor);
    if (this->articulationPads.empty()) {
        graphics.setColour(UITheme::Colours::textMuted);
        graphics.setFont(UITheme::Fonts::normal());
        graphics.drawText("No instruments to preview", this->getLocalBounds().reduced(8), juce::Justification::centred);
        return;
    }

    juce::String lastGroupId;
    for (const auto& padBounds : this->articulationPadBounds) {
        if (padBounds.articulationPadIndex >= this->articulationPads.size()) continue;
        const auto& articulationPad = this->articulationPads[padBounds.articulationPadIndex];
        if (articulationPad.groupId != lastGroupId) {
            const int titleY = padBounds.bounds.getY() - this->groupHeaderHeight;
            graphics.setColour(UITheme::Colours::textSecondary);
            graphics.setFont(UITheme::Fonts::labelBoldMedium());
            graphics.drawText(articulationPad.groupTitle, this->padGap, titleY, this->getWidth() - this->padGap * 2, this->groupHeaderHeight, juce::Justification::centredLeft);
            lastGroupId = articulationPad.groupId;
        }

        auto colour = UITheme::Colours::instrumentType(articulationPad.libraryArticulation.instType);
        if (static_cast<int>(padBounds.articulationPadIndex) == this->flashedArticulationPadIndex) colour = colour.brighter(0.65f);
        graphics.setColour(colour.withMultipliedBrightness(0.58f));
        graphics.fillRoundedRectangle(padBounds.bounds.toFloat(), 3.0f);
        graphics.setColour(colour.brighter(0.35f));
        graphics.drawRoundedRectangle(padBounds.bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
        graphics.setColour(UITheme::Colours::textPrimary);
        graphics.setFont(UITheme::Fonts::normal());
        const auto label = articulationPad.libraryArticulation.artCode.isNotEmpty() ? articulationPad.libraryArticulation.artCode : juce::String("(default)");
        graphics.drawFittedText(label, padBounds.bounds.reduced(6, 2), juce::Justification::centred, 1);
    }
}

void KitPreviewGrid::mouseDown(const juce::MouseEvent& event) {
    for (const auto& padBounds : this->articulationPadBounds) {
        if (!padBounds.bounds.contains(event.getPosition()) || padBounds.articulationPadIndex >= this->articulationPads.size()) continue;
        this->flashedArticulationPadIndex = static_cast<int>(padBounds.articulationPadIndex);
        this->repaint(padBounds.bounds.expanded(1));
        this->startTimer(160);
        const float positionRatio = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.getPosition().getX() - padBounds.bounds.getX()) / static_cast<float>(juce::jmax(1, padBounds.bounds.getWidth())));
        const auto velocity = static_cast<juce::uint8>(juce::jlimit(1, 127, static_cast<int>(positionRatio * 127.0f)));
        if (this->onLibraryArticulationPreview) this->onLibraryArticulationPreview(this->articulationPads[padBounds.articulationPadIndex].libraryArticulation, velocity);
        return;
    }
}

void KitPreviewGrid::timerCallback() {
    this->stopTimer();
    this->flashedArticulationPadIndex = -1;
    this->repaint();
}
