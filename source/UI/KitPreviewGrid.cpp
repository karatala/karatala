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
    if (storeBounds) {
        this->articulationGroupBounds.clear();
        this->articulationPadBounds.clear();
    }
    if (this->articulationPads.empty()) return 48;

    const int contentWidth = juce::jmax(this->minimumPadWidth + this->cardPadding * 2, availableWidth - this->padGap * 2);
    const int groupColumnCount = juce::jmax(1, (contentWidth + this->groupGap) / (this->preferredGroupWidth + this->groupGap));
    const int groupWidth = (contentWidth - (groupColumnCount - 1) * this->groupGap) / groupColumnCount;
    int currentY = this->padGap;
    size_t articulationPadIndex = 0;

    while (articulationPadIndex < this->articulationPads.size()) {
        struct GroupLayout {
            size_t firstArticulationPadIndex = 0;
            int padCount = 0;
            int padColumnCount = 1;
            int height = 0;
        };
        std::vector<GroupLayout> rowGroups;
        rowGroups.reserve(static_cast<size_t>(groupColumnCount));
        int rowHeight = 0;

        while (articulationPadIndex < this->articulationPads.size() && static_cast<int>(rowGroups.size()) < groupColumnCount) {
            GroupLayout groupLayout;
            groupLayout.firstArticulationPadIndex = articulationPadIndex;
            const auto groupId = this->articulationPads[articulationPadIndex].groupId;
            while (articulationPadIndex + static_cast<size_t>(groupLayout.padCount) < this->articulationPads.size()
                   && this->articulationPads[articulationPadIndex + static_cast<size_t>(groupLayout.padCount)].groupId == groupId) ++groupLayout.padCount;

            const int padAreaWidth = groupWidth - this->cardPadding * 2;
            const int availablePadColumns = juce::jmax(1, (padAreaWidth + this->padGap) / (this->minimumPadWidth + this->padGap));
            groupLayout.padColumnCount = juce::jmin(groupLayout.padCount, availablePadColumns);
            const int padRowCount = static_cast<int>(std::ceil(static_cast<double>(groupLayout.padCount) / static_cast<double>(groupLayout.padColumnCount)));
            groupLayout.height = this->cardPadding + this->groupHeaderHeight + padRowCount * this->padHeight + juce::jmax(0, padRowCount - 1) * this->padGap + this->cardPadding;
            rowHeight = juce::jmax(rowHeight, groupLayout.height);
            rowGroups.push_back(groupLayout);
            articulationPadIndex += static_cast<size_t>(groupLayout.padCount);
        }

        for (int groupColumnIndex = 0; groupColumnIndex < static_cast<int>(rowGroups.size()); ++groupColumnIndex) {
            const auto& groupLayout = rowGroups[static_cast<size_t>(groupColumnIndex)];
            const int groupX = this->padGap + groupColumnIndex * (groupWidth + this->groupGap);
            const int padAreaX = groupX + this->cardPadding;
            const int padAreaY = currentY + this->cardPadding + this->groupHeaderHeight;
            const int padAreaWidth = groupWidth - this->cardPadding * 2;
            const int padWidth = (padAreaWidth - (groupLayout.padColumnCount - 1) * this->padGap) / groupLayout.padColumnCount;

            if (storeBounds) {
                ArticulationGroupBounds groupBounds;
                groupBounds.firstArticulationPadIndex = groupLayout.firstArticulationPadIndex;
                groupBounds.bounds = { groupX, currentY, groupWidth, groupLayout.height };
                groupBounds.titleBounds = { padAreaX, currentY + this->cardPadding, padAreaWidth, this->groupHeaderHeight };
                this->articulationGroupBounds.push_back(groupBounds);

                for (int groupPadIndex = 0; groupPadIndex < groupLayout.padCount; ++groupPadIndex) {
                    const int padColumnIndex = groupPadIndex % groupLayout.padColumnCount;
                    const int padRowIndex = groupPadIndex / groupLayout.padColumnCount;
                    ArticulationPadBounds padBounds;
                    padBounds.articulationPadIndex = groupLayout.firstArticulationPadIndex + static_cast<size_t>(groupPadIndex);
                    padBounds.bounds = { padAreaX + padColumnIndex * (padWidth + this->padGap), padAreaY + padRowIndex * (this->padHeight + this->padGap), padWidth, this->padHeight };
                    this->articulationPadBounds.push_back(padBounds);
                }
            }
        }

        currentY += rowHeight + this->groupGap;
    }

    return currentY - this->groupGap + this->padGap;
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

    for (const auto& groupBounds : this->articulationGroupBounds) {
        if (groupBounds.firstArticulationPadIndex >= this->articulationPads.size()) continue;
        const auto& articulationPad = this->articulationPads[groupBounds.firstArticulationPadIndex];
        graphics.setColour(UITheme::Colours::bgControlAlt);
        graphics.fillRoundedRectangle(groupBounds.bounds.toFloat(), 3.0f);
        graphics.setColour(UITheme::Colours::textSecondary);
        graphics.setFont(UITheme::Fonts::small());
        graphics.drawFittedText(articulationPad.groupTitle, groupBounds.titleBounds, juce::Justification::centredLeft, 1, 0.7f);
    }

    for (const auto& padBounds : this->articulationPadBounds) {
        if (padBounds.articulationPadIndex >= this->articulationPads.size()) continue;
        const auto& articulationPad = this->articulationPads[padBounds.articulationPadIndex];
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
