#include "DrumPadGrid.h"
#include <array>
#include <cmath>

DrumPadGrid::DrumPadGrid() {
}

void DrumPadGrid::populate(const KitLayout* layout) {
    this->pads.clear();
    this->padSlotIds.clear();

    int slotCount = layout ? static_cast<int>(layout->instrumentSlots.size()) : 0;
    this->resolveGridSize(slotCount);
    int totalCells = this->effectiveGridColumns * this->effectiveGridRows;

    for (int idx = 0; idx < totalCells; ++idx) {
        auto pad = std::make_unique<DrumPad>();
        pad->setPadGap(this->padGap);

        if (idx < slotCount) {
            pad->setSlot(&layout->instrumentSlots[static_cast<size_t>(idx)]);
            this->padSlotIds.push_back(layout->instrumentSlots[static_cast<size_t>(idx)].id);
        } else {
            pad->setSlot(nullptr);
            this->padSlotIds.emplace_back();
        }

        pad->onPadClicked = [this](const KitLayout::InstrumentSlot* slot, int artIdx, const juce::String& previewArticulationCode, bool useDirectPreview, float vel) {
            if (this->onPadClicked)
                this->onPadClicked(slot, artIdx, previewArticulationCode, useDirectPreview, vel);
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

void DrumPadGrid::setGridSize(int columns, int rows) {
    this->gridColumns = juce::jlimit(0, 12, columns);
    this->gridRows = juce::jlimit(0, 8, rows);
    // Caller should re-populate after changing grid size
}

void DrumPadGrid::resolveGridSize(int instrumentCount) {
    constexpr int maxGridColumns = 12;
    constexpr int maxGridRows = 8;
    const int requiredCells = juce::jmax(1, instrumentCount);

    if (this->gridColumns > 0 && this->gridRows > 0) {
        this->effectiveGridColumns = this->gridColumns;
        this->effectiveGridRows = this->gridRows;
        return;
    }

    if (this->gridColumns > 0) {
        this->effectiveGridColumns = this->gridColumns;
        this->effectiveGridRows = juce::jlimit(
            1, maxGridRows,
            (requiredCells + this->effectiveGridColumns - 1) / this->effectiveGridColumns);
        return;
    }

    if (this->gridRows > 0) {
        this->effectiveGridRows = this->gridRows;
        this->effectiveGridColumns = juce::jlimit(
            1, maxGridColumns,
            (requiredCells + this->effectiveGridRows - 1) / this->effectiveGridRows);
        return;
    }

    constexpr double targetAspectRatio = 1.5;
    double bestScore = 1.0e9;
    int bestColumns = maxGridColumns;
    int bestRows = maxGridRows;

    for (int columns = 1; columns <= maxGridColumns; ++columns) {
        const int rows = (requiredCells + columns - 1) / columns;
        if (rows > maxGridRows)
            continue;

        const int emptyCells = columns * rows - requiredCells;
        const double emptyRatio = static_cast<double>(emptyCells) / requiredCells;
        const double gridAspectRatio = static_cast<double>(columns) / rows;
        const double aspectError = std::abs(std::log(gridAspectRatio / targetAspectRatio));
        const double score = emptyRatio + aspectError;

        if (score < bestScore) {
            bestScore = score;
            bestColumns = columns;
            bestRows = rows;
        }
    }

    this->effectiveGridColumns = bestColumns;
    this->effectiveGridRows = bestRows;
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

    constexpr int maxGridColumns = 12;
    constexpr int maxGridRows = 8;
    const int clampedGridColumns = juce::jlimit(1, maxGridColumns, this->effectiveGridColumns);
    const int clampedGridRows = juce::jlimit(1, maxGridRows, this->effectiveGridRows);

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
