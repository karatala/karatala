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

    auto bounds = getLocalBounds();
    float cellWidth = static_cast<float>(bounds.getWidth()) / static_cast<float>(this->gridColumns);
    float cellHeight = static_cast<float>(bounds.getHeight()) / static_cast<float>(this->gridRows);

    for (int idx = 0; idx < static_cast<int>(this->pads.size()); ++idx) {
        int col = idx % this->gridColumns;
        int row = idx / this->gridColumns;

        if (row >= this->gridRows)
            break;

        auto cellBounds = juce::Rectangle<float>(
            static_cast<float>(bounds.getX()) + cellWidth * static_cast<float>(col),
            static_cast<float>(bounds.getY()) + cellHeight * static_cast<float>(row),
            cellWidth,
            cellHeight
        ).toNearestInt();

        this->pads[static_cast<size_t>(idx)]->setBounds(cellBounds);
    }
}
