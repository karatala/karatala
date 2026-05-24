#include "DrumPadGrid.h"

DrumPadGrid::DrumPadGrid() {
}

void DrumPadGrid::populate(const KitLayout* layout) {
    this->pads.clear();

    int totalCells = this->gridColumns * this->gridRows;
    int slotCount = layout ? static_cast<int>(layout->instrumentSlots.size()) : 0;

    for (int idx = 0; idx < totalCells; ++idx) {
        auto pad = std::make_unique<DrumPad>();
        pad->setPadMode(this->padMode);

        if (idx < slotCount) {
            pad->setSlot(&layout->instrumentSlots[static_cast<size_t>(idx)]);
        } else {
            pad->setSlot(nullptr);
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
    this->resized();
}

void DrumPadGrid::setClickVelocity(float velocity) {
    for (auto& pad : this->pads)
        pad->setClickVelocity(velocity);
}

void DrumPadGrid::flashPad(const juce::String& slotId, float velocity) {
    juce::ignoreUnused(slotId, velocity);
    // TODO: match pad by slotId and call pad->flash(velocity)
}

void DrumPadGrid::resized() {
    if (this->pads.empty())
        return;

    if (this->gridColumns <= 0 || this->gridRows <= 0)
        return;

    auto bounds = getLocalBounds();
    const int gap = this->padGap;
    const int totalGapX = gap * juce::jmax(0, this->gridColumns - 1);
    const int totalGapY = gap * juce::jmax(0, this->gridRows - 1);
    const int availableWidth = juce::jmax(0, bounds.getWidth() - totalGapX);
    const int availableHeight = juce::jmax(0, bounds.getHeight() - totalGapY);
    const int baseCellWidth = availableWidth / this->gridColumns;
    const int baseCellHeight = availableHeight / this->gridRows;
    const int extraWidthPixels = availableWidth % this->gridColumns;
    const int extraHeightPixels = availableHeight % this->gridRows;

    std::vector<int> columnWidths(static_cast<size_t>(this->gridColumns), baseCellWidth);
    std::vector<int> rowHeights(static_cast<size_t>(this->gridRows), baseCellHeight);
    std::vector<int> columnOffsets(static_cast<size_t>(this->gridColumns), bounds.getX());
    std::vector<int> rowOffsets(static_cast<size_t>(this->gridRows), bounds.getY());

    for (int idx = 0; idx < extraWidthPixels; ++idx)
        ++columnWidths[static_cast<size_t>(idx)];
    for (int idx = 0; idx < extraHeightPixels; ++idx)
        ++rowHeights[static_cast<size_t>(idx)];

    for (int idx = 1; idx < this->gridColumns; ++idx)
        columnOffsets[static_cast<size_t>(idx)] = columnOffsets[static_cast<size_t>(idx - 1)] + columnWidths[static_cast<size_t>(idx - 1)] + gap;
    for (int idx = 1; idx < this->gridRows; ++idx)
        rowOffsets[static_cast<size_t>(idx)] = rowOffsets[static_cast<size_t>(idx - 1)] + rowHeights[static_cast<size_t>(idx - 1)] + gap;

    for (int idx = 0; idx < static_cast<int>(this->pads.size()); ++idx) {
        int col = idx % this->gridColumns;
        int row = idx / this->gridColumns;

        if (row >= this->gridRows)
            break;

        auto cellBounds = juce::Rectangle<int>(
            columnOffsets[static_cast<size_t>(col)],
            rowOffsets[static_cast<size_t>(row)],
            columnWidths[static_cast<size_t>(col)],
            rowHeights[static_cast<size_t>(row)]);

        this->pads[static_cast<size_t>(idx)]->setBounds(cellBounds);
    }
}
