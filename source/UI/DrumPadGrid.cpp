#include "DrumPadGrid.h"
#include <array>

DrumPadGrid::DrumPadGrid() {
}

void DrumPadGrid::populate(const KitLayout* layout) {
    this->pads.clear();
    this->padSlotIds.clear();

    int totalCells = this->gridColumns * this->gridRows;
    int slotCount = layout ? static_cast<int>(layout->instrumentSlots.size()) : 0;

    for (int idx = 0; idx < totalCells; ++idx) {
        auto pad = std::make_unique<DrumPad>();
        pad->setPadMode(this->padMode);
        pad->setPadGap(this->padGap);

        if (idx < slotCount) {
            pad->setSlot(&layout->instrumentSlots[static_cast<size_t>(idx)]);
            this->padSlotIds.push_back(layout->instrumentSlots[static_cast<size_t>(idx)].id);
        } else {
            pad->setSlot(nullptr);
            this->padSlotIds.emplace_back();
        }

        pad->onPadClicked = [this](const KitLayout::InstrumentSlot* slot, int artIdx, float vel) {
            if (this->onPadClicked)
                this->onPadClicked(slot, artIdx, vel);
        };

        pad->onPadRightClicked = [this](const KitLayout::InstrumentSlot* slot, int artIdx, juce::Point<int> screenPos) {
            if (this->onPadRightClicked)
                this->onPadRightClicked(slot, artIdx, screenPos);
        };

        addAndMakeVisible(*pad);
        this->pads.push_back(std::move(pad));
    }

    this->resized();
}

void DrumPadGrid::setPadMode(PadMode mode) {
    this->padMode = mode;
    for (auto& pad : this->pads)
        pad->setPadMode(mode);
}

void DrumPadGrid::setGridSize(int columns, int rows) {
    this->gridColumns = columns;
    this->gridRows = rows;
    // Caller should re-populate after changing grid size
}

void DrumPadGrid::setPadGap(int gap) {
    this->padGap = juce::jmax(0, gap);
    for (auto& pad : this->pads)
        pad->setPadGap(this->padGap);
    this->resized();
}

void DrumPadGrid::setClickVelocity(float velocity) {
    for (auto& pad : this->pads)
        pad->setClickVelocity(velocity);
}

void DrumPadGrid::flashPad(const juce::String& slotId, float velocity) {
    for (int idx = 0; idx < static_cast<int>(this->pads.size()) && idx < static_cast<int>(this->padSlotIds.size()); ++idx) {
        if (this->padSlotIds[static_cast<size_t>(idx)] == slotId) {
            this->pads[static_cast<size_t>(idx)]->flash(velocity);
            return;
        }
    }
}

void DrumPadGrid::resized() {
    if (this->pads.empty())
        return;

    if (this->gridColumns <= 0 || this->gridRows <= 0)
        return;

    constexpr int maxGridColumns = 12;
    constexpr int maxGridRows = 8;
    const int clampedGridColumns = juce::jlimit(1, maxGridColumns, this->gridColumns);
    const int clampedGridRows = juce::jlimit(1, maxGridRows, this->gridRows);

    auto bounds = getLocalBounds();
    const int gap = this->padGap;
    const int totalGapX = gap * juce::jmax(0, clampedGridColumns - 1);
    const int totalGapY = gap * juce::jmax(0, clampedGridRows - 1);
    const int availableWidth = juce::jmax(0, bounds.getWidth() - totalGapX);
    const int availableHeight = juce::jmax(0, bounds.getHeight() - totalGapY);
    const int baseCellWidth = availableWidth / clampedGridColumns;
    const int baseCellHeight = availableHeight / clampedGridRows;
    const int extraWidthPixels = availableWidth % clampedGridColumns;
    const int extraHeightPixels = availableHeight % clampedGridRows;

    std::array<int, maxGridColumns> columnWidths {};
    std::array<int, maxGridRows> rowHeights {};
    std::array<int, maxGridColumns> columnOffsets {};
    std::array<int, maxGridRows> rowOffsets {};

    for (int idx = 0; idx < clampedGridColumns; ++idx)
        columnWidths[static_cast<size_t>(idx)] = baseCellWidth;
    for (int idx = 0; idx < clampedGridRows; ++idx)
        rowHeights[static_cast<size_t>(idx)] = baseCellHeight;
    for (int idx = 0; idx < clampedGridColumns; ++idx)
        columnOffsets[static_cast<size_t>(idx)] = bounds.getX();
    for (int idx = 0; idx < clampedGridRows; ++idx)
        rowOffsets[static_cast<size_t>(idx)] = bounds.getY();

    for (int idx = 0; idx < extraWidthPixels; ++idx)
        ++columnWidths[static_cast<size_t>(idx)];
    for (int idx = 0; idx < extraHeightPixels; ++idx)
        ++rowHeights[static_cast<size_t>(idx)];

    for (int idx = 1; idx < clampedGridColumns; ++idx)
        columnOffsets[static_cast<size_t>(idx)] = columnOffsets[static_cast<size_t>(idx - 1)] + columnWidths[static_cast<size_t>(idx - 1)] + gap;
    for (int idx = 1; idx < clampedGridRows; ++idx)
        rowOffsets[static_cast<size_t>(idx)] = rowOffsets[static_cast<size_t>(idx - 1)] + rowHeights[static_cast<size_t>(idx - 1)] + gap;

    for (int idx = 0; idx < static_cast<int>(this->pads.size()); ++idx) {
        int col = idx % clampedGridColumns;
        int row = idx / clampedGridColumns;

        if (row >= clampedGridRows)
            break;

        auto cellBounds = juce::Rectangle<int>(
            columnOffsets[static_cast<size_t>(col)],
            rowOffsets[static_cast<size_t>(row)],
            columnWidths[static_cast<size_t>(col)],
            rowHeights[static_cast<size_t>(row)]);

        this->pads[static_cast<size_t>(idx)]->setBounds(cellBounds);
    }
}
