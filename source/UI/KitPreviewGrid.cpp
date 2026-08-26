#include "KitPreviewGrid.h"

#include "UITheme.h"
#include <utility>

void KitPreviewGrid::setPreviewInstruments(std::vector<PreviewInstrument> newPreviewInstruments) {
    this->previewInstruments = std::move(newPreviewInstruments);
    this->rebuildPads();
    this->repaint();
}

int KitPreviewGrid::getPreferredHeight(int availableWidth) {
    if (this->previewInstruments.empty()) return 48;
    const int columnCount = this->getColumnCount(availableWidth);
    const int rowCount = (static_cast<int>(this->previewInstruments.size()) + columnCount - 1) / columnCount;
    return this->getHeightForRows(rowCount);
}

int KitPreviewGrid::getHeightForRows(int rowCount) const {
    const int clampedRowCount = juce::jmax(0, rowCount);
    return this->padGap * 2 + clampedRowCount * this->padHeight + juce::jmax(0, clampedRowCount - 1) * this->padGap;
}

int KitPreviewGrid::getColumnCount(int availableWidth) const {
    const int contentWidth = juce::jmax(1, availableWidth - this->padGap * 2);
    return juce::jmax(1, (contentWidth + this->padGap) / (this->preferredPadWidth + this->padGap));
}

void KitPreviewGrid::resized() {
    if (this->pads.empty()) return;
    const int columnCount = this->getColumnCount(this->getWidth());
    const int contentWidth = juce::jmax(1, this->getWidth() - this->padGap * 2);
    const int padWidth = (contentWidth - juce::jmax(0, columnCount - 1) * this->padGap) / columnCount;
    for (int padIndex = 0; padIndex < static_cast<int>(this->pads.size()); ++padIndex) {
        const int columnIndex = padIndex % columnCount;
        const int rowIndex = padIndex / columnCount;
        this->pads[static_cast<size_t>(padIndex)]->setBounds(this->padGap + columnIndex * (padWidth + this->padGap), this->padGap + rowIndex * (this->padHeight + this->padGap), padWidth, this->padHeight);
    }
}

void KitPreviewGrid::paint(juce::Graphics& graphics) {
    graphics.fillAll(UITheme::Colours::bgEditor);
    if (this->previewInstruments.empty()) {
        graphics.setColour(UITheme::Colours::textMuted);
        graphics.setFont(UITheme::Fonts::normal());
        graphics.drawText("No instruments to preview", this->getLocalBounds().reduced(8), juce::Justification::centred);
    }
}

void KitPreviewGrid::setClickVelocity(float velocity) {
    this->clickVelocity = juce::jlimit(1.0f, 127.0f, velocity);
    for (auto& pad : this->pads) pad->setClickVelocity(this->clickVelocity);
}

void KitPreviewGrid::rebuildPads() {
    this->pads.clear();
    this->pads.reserve(this->previewInstruments.size());
    for (int previewInstrumentIndex = 0; previewInstrumentIndex < static_cast<int>(this->previewInstruments.size()); ++previewInstrumentIndex) {
        auto pad = std::make_unique<DrumPad>();
        pad->setSlot(&this->previewInstruments[static_cast<size_t>(previewInstrumentIndex)].slot);
        pad->setPadGap(1);
        pad->setVisualScale(0.5f);
        pad->setClickVelocity(this->clickVelocity);
        auto* padPointer = pad.get();
        pad->onPadClicked = [this, previewInstrumentIndex, padPointer](const KitLayout::InstrumentSlot* slot, int articulationIndex, const juce::String& previewArticulationCode, bool useDirectPreview, float velocity) {
            if (slot == nullptr || articulationIndex < 0 || previewInstrumentIndex < 0 || previewInstrumentIndex >= static_cast<int>(this->previewInstruments.size())) return;
            const auto& previewInstrument = this->previewInstruments[static_cast<size_t>(previewInstrumentIndex)];
            if (articulationIndex >= static_cast<int>(previewInstrument.articulationTargets.size())) return;
            padPointer->flash(velocity);
            if (this->onPreview) this->onPreview(slot->id, previewArticulationCode, useDirectPreview, previewInstrument.articulationTargets[static_cast<size_t>(articulationIndex)], static_cast<juce::uint8>(velocity));
        };
        this->addAndMakeVisible(*pad);
        this->pads.push_back(std::move(pad));
    }
    this->resized();
}
