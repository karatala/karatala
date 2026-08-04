#include "LibraryArticulationEditor.h"
#include "../Config/Parsing.h"
#include "../Engine/RoutingTypes.h"
#include "UITheme.h"

#include <algorithm>
#include <cmath>

static juce::String logicalHitHoverKey(int namedVelocity, const juce::String& roundRobin) {
    return "hit:" + juce::String(namedVelocity) + ":" + roundRobin;
}

static juce::String micHoverKey(int namedVelocity, const juce::String& roundRobin, const juce::String& micCode) {
    return "mic:" + juce::String(namedVelocity) + ":" + roundRobin + ":" + micCode.toLowerCase();
}
void LibraryArticulationEditor::toggleLogicalHitSelection(int levelIdx, int roundRobinIdx) {
    if (!this->logicalHitExists(levelIdx, roundRobinIdx) || !this->commitPendingSampleEditors()) return;
    LogicalHitKey hit { this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity, this->roundRobins[static_cast<size_t>(roundRobinIdx)] };
    const auto selectedIt = std::find_if(this->selectedHits.begin(), this->selectedHits.end(), [this, &hit](const auto& selectedHit) { return this->logicalHitsMatch(hit, selectedHit); });
    if (selectedIt == this->selectedHits.end()) this->selectedHits.push_back(hit);
    else this->selectedHits.erase(selectedIt);
    this->activeHit = hit;
    this->hasActiveHit = true;
    this->hoverPreviewKey = logicalHitHoverKey(hit.namedVelocity, hit.roundRobin);
    ++this->hoverPreviewGeneration;
    this->updateSelectedSamples();
    this->previewLogicalHit(hit, hit.namedVelocity);
}

void LibraryArticulationEditor::invertLogicalHitSelection(std::optional<int> levelIdx, std::optional<int> roundRobinIdx) {
    if (!this->commitPendingSampleEditors()) return;
    const int firstLevelIdx = levelIdx.value_or(0);
    const int lastLevelIdx = levelIdx.has_value() ? *levelIdx + 1 : static_cast<int>(this->rangeInfo.levels.size());
    const int firstRoundRobinIdx = roundRobinIdx.value_or(0);
    const int lastRoundRobinIdx = roundRobinIdx.has_value() ? *roundRobinIdx + 1 : static_cast<int>(this->roundRobins.size());
    if (firstLevelIdx < 0 || lastLevelIdx > static_cast<int>(this->rangeInfo.levels.size()) || firstRoundRobinIdx < 0 || lastRoundRobinIdx > static_cast<int>(this->roundRobins.size())) return;

    for (int currentLevelIdx = firstLevelIdx; currentLevelIdx < lastLevelIdx; ++currentLevelIdx) {
        for (int currentRoundRobinIdx = firstRoundRobinIdx; currentRoundRobinIdx < lastRoundRobinIdx; ++currentRoundRobinIdx) {
            if (!this->logicalHitExists(currentLevelIdx, currentRoundRobinIdx)) continue;
            LogicalHitKey hit { this->rangeInfo.levels[static_cast<size_t>(currentLevelIdx)].namedVelocity, this->roundRobins[static_cast<size_t>(currentRoundRobinIdx)] };
            const auto selectedIt = std::find_if(this->selectedHits.begin(), this->selectedHits.end(), [this, &hit](const auto& selectedHit) { return this->logicalHitsMatch(hit, selectedHit); });
            if (selectedIt == this->selectedHits.end()) this->selectedHits.push_back(hit);
            else this->selectedHits.erase(selectedIt);
        }
    }
    this->updateSelectedSamples();
}

void LibraryArticulationEditor::toggleMicSelection(const juce::String& micCode) {
    if (!this->commitPendingSampleEditors()) {
        this->updateMicButtonStates();
        return;
    }
    const auto selectedIt = std::find_if(this->selectedMicCodes.begin(), this->selectedMicCodes.end(), [&micCode](const auto& selectedMicCode) { return selectedMicCode.compareIgnoreCase(micCode) == 0; });
    if (selectedIt == this->selectedMicCodes.end()) this->selectedMicCodes.push_back(micCode);
    else this->selectedMicCodes.erase(selectedIt);
    if (this->hasActiveHit) {
        this->hoverPreviewKey = micHoverKey(this->activeHit.namedVelocity, this->activeHit.roundRobin, micCode);
        ++this->hoverPreviewGeneration;
    } else {
        this->clearHoverPreview();
    }
    this->updateSelectedSamples();
    if (this->hasActiveHit) this->previewMic(this->activeHit, micCode, this->activeHit.namedVelocity);
}

bool LibraryArticulationEditor::isMicSelected(const juce::String& micCode) const {
    return std::any_of(this->selectedMicCodes.begin(), this->selectedMicCodes.end(), [&micCode](const auto& selectedMicCode) { return selectedMicCode.compareIgnoreCase(micCode) == 0; });
}

void LibraryArticulationEditor::previewLogicalHit(const LogicalHitKey& hit, int playbackVelocity) {
    const auto* sample = this->findSample(hit);
    if (sample == nullptr || !this->onPreview) return;
    this->activeHit = hit;
    this->hasActiveHit = true;
    this->lastPreviewHit = hit;
    this->lastPreviewMicCode.clear();
    this->lastPreviewVelocity = juce::jlimit(1, 127, playbackVelocity);
    this->lastPreviewWasMicSolo = false;
    this->hasLastPreview = true;
    this->sampleMatrix.repaint();
    this->onPreview(this->lastPreviewVelocity, hit.namedVelocity, hit.roundRobin, sample->relativePath, {});
}

void LibraryArticulationEditor::previewMic(const LogicalHitKey& hit, const juce::String& micCode, int playbackVelocity) {
    const auto* sample = this->findSample(hit, micCode);
    if (sample == nullptr || !this->onPreview) return;
    this->activeHit = hit;
    this->hasActiveHit = true;
    this->lastPreviewHit = hit;
    this->lastPreviewMicCode = sample->micCode;
    this->lastPreviewVelocity = juce::jlimit(1, 127, playbackVelocity);
    this->lastPreviewWasMicSolo = true;
    this->hasLastPreview = true;
    this->sampleMatrix.repaint();
    this->onPreview(this->lastPreviewVelocity, hit.namedVelocity, hit.roundRobin, sample->relativePath, sample->micCode);
}

void LibraryArticulationEditor::previewVelocityAt(float xPosition) {
    if (this->rangeBounds.getWidth() <= 0.0f || this->rangeInfo.levels.empty()) return;
    const float proportion = juce::jlimit(0.0f, 1.0f, (xPosition - this->rangeBounds.getX()) / this->rangeBounds.getWidth());
    const int velocity = juce::jlimit(1, 127, 1 + juce::roundToInt(proportion * 126.0f));
    const int levelIdx = this->getLevelIndexForVelocity(velocity);
    if (levelIdx < 0) return;
    const int namedVelocity = this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity;
    LogicalHitKey hit { namedVelocity, this->hasActiveHit ? this->activeHit.roundRobin : juce::String() };
    if (!this->logicalHitExists(hit)) {
        const auto sampleIt = std::find_if(this->rangeInfo.samples.begin(), this->rangeInfo.samples.end(), [namedVelocity](const auto& sample) { return sample.namedVelocity == namedVelocity; });
        if (sampleIt == this->rangeInfo.samples.end()) return;
        hit.roundRobin = sampleIt->roundRobin;
    }
    this->previewLogicalHit(hit, velocity);
}

void LibraryArticulationEditor::previewChangedValues() {
    if (!this->selectedSampleIndices.empty()) {
        const int sampleIdx = this->selectedSampleIndices.back();
        if (sampleIdx >= 0 && sampleIdx < static_cast<int>(this->rangeInfo.samples.size())) {
            const auto& sample = this->rangeInfo.samples[static_cast<size_t>(sampleIdx)];
            this->previewLogicalHit({ sample.namedVelocity, sample.roundRobin }, sample.namedVelocity);
            return;
        }
    }
    if (!this->selectedHits.empty()) {
        const auto& hit = this->selectedHits.back();
        this->previewLogicalHit(hit, hit.namedVelocity);
        return;
    }
    if (this->hasActiveHit) this->previewLogicalHit(this->activeHit, this->activeHit.namedVelocity);
    else if (this->hasLastPreview) this->previewLogicalHit(this->lastPreviewHit, this->lastPreviewVelocity);
}

void LibraryArticulationEditor::replayLastPreview() {
    if (!this->hasLastPreview) return;
    if (this->lastPreviewWasMicSolo) this->previewMic(this->lastPreviewHit, this->lastPreviewMicCode, this->lastPreviewVelocity);
    else this->previewLogicalHit(this->lastPreviewHit, this->lastPreviewVelocity);
}

void LibraryArticulationEditor::queueLogicalHitHover(const LogicalHitKey& hit) {
    const auto hoverKey = logicalHitHoverKey(hit.namedVelocity, hit.roundRobin);
    if (this->hoverPreviewKey == hoverKey) return;
    this->hoverPreviewKey = hoverKey;
    const int previewGeneration = ++this->hoverPreviewGeneration;
    juce::Component::SafePointer<LibraryArticulationEditor> safeEditor(this);
    juce::Timer::callAfterDelay(150, [safeEditor, previewGeneration, hit] {
        if (safeEditor == nullptr || safeEditor->hoverPreviewGeneration != previewGeneration) return;
        safeEditor->previewLogicalHit(hit, hit.namedVelocity);
    });
}

void LibraryArticulationEditor::queueMicHover(const juce::String& micCode) {
    if (!this->hasActiveHit) return;
    const auto hit = this->activeHit;
    const auto hoverKey = micHoverKey(hit.namedVelocity, hit.roundRobin, micCode);
    if (this->hoverPreviewKey == hoverKey) return;
    this->hoverPreviewKey = hoverKey;
    const int previewGeneration = ++this->hoverPreviewGeneration;
    juce::Component::SafePointer<LibraryArticulationEditor> safeEditor(this);
    juce::Timer::callAfterDelay(150, [safeEditor, previewGeneration, hit, micCode] {
        if (safeEditor == nullptr || safeEditor->hoverPreviewGeneration != previewGeneration) return;
        safeEditor->previewMic(hit, micCode, hit.namedVelocity);
    });
}

void LibraryArticulationEditor::clearHoverPreview() {
    this->hoverPreviewKey.clear();
    ++this->hoverPreviewGeneration;
}

void LibraryArticulationEditor::updateHoverTarget(const juce::MouseEvent& event) {
    const auto micCode = this->getMicCodeAt(event);
    if (micCode.isNotEmpty()) {
        this->queueMicHover(micCode);
        return;
    }
    const auto matrixHit = this->getMatrixHitAt(event);
    if (matrixHit.has_value()) {
        this->queueLogicalHitHover(*matrixHit);
        return;
    }
    this->clearHoverPreview();
}

std::optional<LibraryArticulationEditor::LogicalHitKey> LibraryArticulationEditor::getMatrixHitAt(const juce::MouseEvent& event) const {
    if (event.eventComponent == nullptr) return std::nullopt;
    if (event.eventComponent != &this->sampleMatrix && !this->sampleMatrix.isParentOf(event.eventComponent)) return std::nullopt;
    const auto matrixPosition = this->sampleMatrix.getLocalPoint(event.eventComponent, event.getPosition());
    for (int rowIdx = 0; rowIdx < static_cast<int>(this->roundRobins.size()); ++rowIdx) {
        for (int levelIdx = 0; levelIdx < static_cast<int>(this->rangeInfo.levels.size()); ++levelIdx) {
            if (!this->sampleMatrix.getCellPosition(velocityColumnIdBase + levelIdx, rowIdx, true).contains(matrixPosition) || !this->logicalHitExists(levelIdx, rowIdx)) continue;
            return LogicalHitKey { this->rangeInfo.levels[static_cast<size_t>(levelIdx)].namedVelocity, this->roundRobins[static_cast<size_t>(rowIdx)] };
        }
    }
    return std::nullopt;
}

juce::String LibraryArticulationEditor::getMicCodeAt(const juce::MouseEvent& event) const {
    if (event.eventComponent == nullptr) return {};
    for (int micIdx = 0; micIdx < static_cast<int>(this->micButtons.size()) && micIdx < static_cast<int>(this->micCodes.size()); ++micIdx) {
        const auto* button = this->micButtons[static_cast<size_t>(micIdx)].get();
        if (event.eventComponent == button || button->isParentOf(event.eventComponent)) return this->micCodes[static_cast<size_t>(micIdx)];
    }
    return {};
}
