#include "DrumPad.h"
#include "DrumPadHelpers.h"
#include "UITheme.h"
#include <algorithm>
#include <array>
#include <numeric>

void DrumPad::setSlot(const KitLayout::InstrumentSlot* slot) {
    this->slotPtr = slot;
    this->refreshDisplayOrder();
    repaint();
}

void DrumPad::refreshDisplayOrder() { //FIXME: get rid of display order, we will keep default order as it was specified in kit layout yaml
    this->displayOrder.clear();
    if (!this->slotPtr || this->slotPtr->articulations.empty())
        return;

    this->displayOrder = DrumPadHelpers::buildDisplayOrder(this->slotPtr->articulations);
}

void DrumPad::flash(float velocity) {
    if (this->slotPtr == nullptr) {
        this->flashAlpha = 0.0f;
        this->stopTimer();
        return;
    }

    this->flashAlpha = juce::jlimit(0.0f, 1.0f, velocity / 127.0f);
    this->startTimerHz(flashTimerHz);
    repaint();
}

void DrumPad::timerCallback() {
    const float flashStep = 1.0f / static_cast<float>((flashFadeDurationMs * flashTimerHz) / 1000);
    this->flashAlpha = juce::jmax(0.0f, this->flashAlpha - flashStep);

    if (this->flashAlpha <= 0.0f) {
        this->flashAlpha = 0.0f;
        this->stopTimer();
    }
    repaint();
}

juce::Colour DrumPad::getColourForType(KitLib::InstrumentSpec::Type type) { return UITheme::Colours::instrumentType(type); }

void DrumPad::paint(juce::Graphics& g) {
    this->paintZoned(g);
}

void DrumPad::paintSimple(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    auto baseColour = this->slotPtr ? getColourForType(this->slotPtr->type) : UITheme::Colours::padEmpty;

    // Fill with base colour, darkened
    g.setColour(baseColour.withMultipliedBrightness(0.3f));
    g.fillRect(bounds);

    // Flash overlay
    if (this->flashAlpha > 0.0f) {
        g.setColour(baseColour.withAlpha(this->flashAlpha * 0.6f));
        g.fillRect(bounds);
    }

    // Border — only when there's a visible gap between pads
    if (this->padGap > 0) {
        g.setColour(UITheme::Colours::separator);
        g.drawRect(bounds, 1);
    }

    const auto labelBounds = bounds.reduced(6);
    if (this->slotPtr == nullptr) {
        DrumPadHelpers::drawPadLabel(g, labelBounds, "Empty", UITheme::Colours::textMuted, UITheme::Fonts::small().withHeight(11.0f), 1);
        return;
    }

    const float labelHeight = static_cast<float>(juce::jlimit(10, 15, juce::jmin(bounds.getWidth() / 5, bounds.getHeight() / 3)));
    DrumPadHelpers::drawPadLabel(g, labelBounds, DrumPadHelpers::getPadDisplayName(*this->slotPtr), UITheme::Colours::textPrimary.withAlpha(0.9f), UITheme::Fonts::labelBold().withHeight(labelHeight), 2);
}

void DrumPad::paintZoned(juce::Graphics& g) {
    const auto fullBounds = this->getLocalBounds();
    auto bounds = fullBounds;
    auto baseColour = this->slotPtr ? getColourForType(this->slotPtr->type) : UITheme::Colours::padEmpty;

    if (!this->slotPtr || this->slotPtr->articulations.empty() || this->displayOrder.empty()) {
        this->paintSimple(g);
        return;
    }

    const auto titleBounds = bounds.removeFromLeft(juce::jmin(UITheme::Sizes::padTitleRailWidth, bounds.getWidth()));
    const auto cells = this->buildArticulationCells(bounds);
    if (cells.empty()) {
        this->paintSimple(g);
        return;
    }

    for (int cellIdx = 0; cellIdx < static_cast<int>(cells.size()); ++cellIdx) {
        const auto& cell = cells[static_cast<size_t>(cellIdx)];
        const float brightness = cell.articulationIdx >= 0 ? 0.31f : 0.25f;
        g.setColour(baseColour.withMultipliedBrightness(brightness));
        g.fillRect(cell.bounds);
        g.setColour(UITheme::Colours::separator);
        g.drawHorizontalLine(cell.bounds.getBottom() - 1, static_cast<float>(cell.bounds.getX()), static_cast<float>(cell.bounds.getRight()));
        g.drawVerticalLine(cell.bounds.getRight() - 1, static_cast<float>(cell.bounds.getY()), static_cast<float>(cell.bounds.getBottom()));

        if (cell.label.isNotEmpty()) DrumPadHelpers::drawPadLabel(g, cell.bounds.reduced(3, 2), cell.label, UITheme::Colours::textPrimary.withAlpha(0.85f), UITheme::Fonts::normal().boldened(), 2);
    }

    this->paintInstrumentTitle(g, titleBounds);

    // Flash overlay
    if (this->flashAlpha > 0.0f) {
        g.setColour(baseColour.withAlpha(this->flashAlpha * 0.5f));
        g.fillRect(fullBounds);
    }

    // Outer border — only when there's a visible gap between pads
    if (this->padGap > 0) {
        g.setColour(UITheme::Colours::separator);
        g.drawRect(fullBounds, 1);
    }
}

void DrumPad::paintInstrumentTitle(juce::Graphics& g, const juce::Rectangle<int>& bounds) const {
    g.setColour(UITheme::Colours::bgApp);
    g.fillRect(bounds);
    g.setColour(UITheme::Colours::separator);
    g.drawVerticalLine(bounds.getRight() - 1, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));

    juce::Graphics::ScopedSaveState saveState(g);
    g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::halfPi).translated(static_cast<float>(bounds.getRight()), static_cast<float>(bounds.getY())));
    const auto textBounds = juce::Rectangle<int>(0, 0, bounds.getHeight(), bounds.getWidth()).reduced(7, 2);
    g.setColour(UITheme::Colours::textPrimary);
    g.setFont(UITheme::Fonts::small().boldened());
    g.drawFittedText(DrumPadHelpers::getPadDisplayName(*this->slotPtr), textBounds, juce::Justification::centredLeft, 1, 0.9f);
}

std::vector<DrumPad::ArticulationCell> DrumPad::buildArticulationCells(const juce::Rectangle<int>& bounds) const {
    if (this->slotPtr == nullptr || this->displayOrder.empty() || bounds.isEmpty()) return {};
    if (this->slotPtr->type == KitLib::InstrumentSpec::Type::HiHat) return this->buildHiHatCells(bounds);

    std::vector<ArticulationCell> cells;
    cells.reserve(this->displayOrder.size());
    for (int displayIdx = 0; displayIdx < static_cast<int>(this->displayOrder.size()); ++displayIdx) {
        const int articulationIdx = this->displayOrder[static_cast<size_t>(displayIdx)];
        const int cellTop = bounds.getY() + bounds.getHeight() * displayIdx / static_cast<int>(this->displayOrder.size());
        const int cellBottom = bounds.getY() + bounds.getHeight() * (displayIdx + 1) / static_cast<int>(this->displayOrder.size());
        const auto& articulationCode = this->slotPtr->articulations[static_cast<size_t>(articulationIdx)].codeRaw;
        cells.push_back({ juce::Rectangle<int>(bounds.getX(), cellTop, bounds.getWidth(), cellBottom - cellTop), articulationCode, articulationCode, articulationIdx, false });
    }
    return cells;
}

std::vector<DrumPad::ArticulationCell> DrumPad::buildHiHatCells(const juce::Rectangle<int>& bounds) const {
    const juce::StringArray zoneNames { "bow", "edge" };
    const juce::StringArray stateNames { "tight", "close", "half", "open" };
    std::array<std::array<int, 4>, 2> zoneArticulationIndices;
    for (auto& zoneIndices : zoneArticulationIndices) zoneIndices.fill(-1);
    std::vector<int> otherArticulationIndices;
    int pedalArticulationIdx = -1;

    for (const int articulationIdx : this->displayOrder) {
        const auto& articulation = this->slotPtr->articulations[static_cast<size_t>(articulationIdx)];
        const auto baseName = ArticulationNameHelpers::normalizeArticulationName(articulation.codeRaw);
        if (baseName == "pedal") {
            pedalArticulationIdx = articulationIdx;
            continue;
        }

        const int zoneIdx = zoneNames.indexOf(baseName);
        const int stateIdx = stateNames.indexOf(DrumPadHelpers::getArticulationExpressionName(articulation.codeRaw));
        if (zoneIdx >= 0 && stateIdx >= 0) {
            zoneArticulationIndices[static_cast<size_t>(zoneIdx)][static_cast<size_t>(stateIdx)] = articulationIdx;
            continue;
        }
        otherArticulationIndices.push_back(articulationIdx);
    }

    int zoneRowCount = 0;
    for (const auto& zoneIndices : zoneArticulationIndices)
        if (std::any_of(zoneIndices.begin(), zoneIndices.end(), [](int articulationIdx) { return articulationIdx >= 0; })) ++zoneRowCount;
    const int mainRowCount = zoneRowCount + static_cast<int>(otherArticulationIndices.size());
    int pedalHeight = 0;
    if (pedalArticulationIdx >= 0) pedalHeight = mainRowCount > 0 ? juce::jmin(34, juce::jmax(1, bounds.getHeight() / (mainRowCount + 1))) : bounds.getHeight();
    const int mainAreaHeight = bounds.getHeight() - pedalHeight;
    const int totalRowCount = mainRowCount + (pedalArticulationIdx >= 0 ? 1 : 0);
    if (totalRowCount == 0) return {};

    std::vector<ArticulationCell> cells;
    int rowIdx = 0;
    for (int zoneIdx = 0; zoneIdx < static_cast<int>(zoneArticulationIndices.size()); ++zoneIdx) {
        const auto& zoneIndices = zoneArticulationIndices[static_cast<size_t>(zoneIdx)];
        if (!std::any_of(zoneIndices.begin(), zoneIndices.end(), [](int articulationIdx) { return articulationIdx >= 0; })) continue;
        int previewAnchorIdx = zoneIndices[3];
        if (previewAnchorIdx < 0) {
            for (const int articulationIdx : zoneIndices) {
                if (articulationIdx >= 0) {
                    previewAnchorIdx = articulationIdx;
                    break;
                }
            }
        }
        const int rowTop = bounds.getY() + mainAreaHeight * rowIdx / juce::jmax(1, mainRowCount);
        const int rowBottom = bounds.getY() + mainAreaHeight * (rowIdx + 1) / juce::jmax(1, mainRowCount);
        for (int stateIdx = 0; stateIdx < static_cast<int>(stateNames.size()); ++stateIdx) {
            const int cellLeft = bounds.getX() + bounds.getWidth() * stateIdx / static_cast<int>(stateNames.size());
            const int cellRight = bounds.getX() + bounds.getWidth() * (stateIdx + 1) / static_cast<int>(stateNames.size());
            const int mappedArticulationIdx = zoneIndices[static_cast<size_t>(stateIdx)];
            const bool useDirectPreview = mappedArticulationIdx < 0;
            const int interactionArticulationIdx = useDirectPreview ? previewAnchorIdx : mappedArticulationIdx;
            const auto previewArticulationCode = zoneNames[zoneIdx] + "[" + stateNames[stateIdx] + "]";
            const auto label = stateIdx == 0 ? zoneNames[zoneIdx] + "\n" + stateNames[stateIdx] : stateNames[stateIdx];
            cells.push_back({ juce::Rectangle<int>(cellLeft, rowTop, cellRight - cellLeft, rowBottom - rowTop), label, previewArticulationCode, interactionArticulationIdx, useDirectPreview });
        }
        ++rowIdx;
    }

    for (const int articulationIdx : otherArticulationIndices) {
        const int rowTop = bounds.getY() + mainAreaHeight * rowIdx / juce::jmax(1, mainRowCount);
        const int rowBottom = bounds.getY() + mainAreaHeight * (rowIdx + 1) / juce::jmax(1, mainRowCount);
        const auto& articulationCode = this->slotPtr->articulations[static_cast<size_t>(articulationIdx)].codeRaw;
        cells.push_back({ juce::Rectangle<int>(bounds.getX(), rowTop, bounds.getWidth(), rowBottom - rowTop), articulationCode, articulationCode, articulationIdx, false });
        ++rowIdx;
    }

    if (pedalArticulationIdx >= 0) cells.push_back({ juce::Rectangle<int>(bounds.getX(), bounds.getBottom() - juce::jmax(1, pedalHeight), bounds.getWidth(), juce::jmax(1, pedalHeight)), "pedal", "pedal", pedalArticulationIdx, false });
    return cells;
}

std::optional<DrumPad::ArticulationCell> DrumPad::getCellAt(juce::Point<int> position) const {
    if (!this->slotPtr || this->slotPtr->articulations.empty() || this->displayOrder.empty())
        return std::nullopt;

    auto bodyBounds = this->getLocalBounds();
    bodyBounds.removeFromLeft(juce::jmin(UITheme::Sizes::padTitleRailWidth, bodyBounds.getWidth()));
    for (const auto& cell : this->buildArticulationCells(bodyBounds))
        if (cell.articulationIdx >= 0 && cell.bounds.contains(position)) return cell;
    return std::nullopt;
}

void DrumPad::mouseDown(const juce::MouseEvent& event) {
    if (!this->slotPtr)
        return;
    const auto cell = this->getCellAt(event.getPosition());
    if (!cell.has_value()) return;

    if (event.mods.isPopupMenu()) {
        if (this->onPadRightClicked) this->onPadRightClicked(this->slotPtr, cell->articulationIdx, event.getScreenPosition());
        return;
    }
    if (this->onPadClicked) this->onPadClicked(this->slotPtr, cell->articulationIdx, cell->previewArticulationCode, cell->useDirectPreview, this->clickVelocity);
}

void DrumPad::resized() {
}
