#pragma once

#include <juce_core/juce_core.h>
#include "../Engine/DrumKitEngine.h"

/**
 * @file UITypes.h
 * Shared UI data structures used across multiple UI panels.
 */

struct SourceItem {
    juce::String kitCodeRaw;
    juce::String kitDisplayName;
    juce::String instCode;
    juce::String artCode;
    KitLib::InstrumentSpec::Type instType = KitLib::InstrumentSpec::Type::Unknown;

    juce::String fullPath() const {
        juce::String result = this->kitCodeRaw;
        if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
        if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
        return result;
    }

    juce::String displayText() const {
        juce::String result = this->kitDisplayName;
        if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
        if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
        return result;
    }
};

namespace UIHelpers {
/**
 * Build the source items list from a LibraryManager.
 * Returns a vector of SourceItem entries for all kits, instruments, and articulations.
 */
std::vector<SourceItem> buildSourceItems(const KitLib::LibraryManager* libraryManager);

/**
 * Find the index of a source item matching the given search path.
 * Returns -1 if not found.
 */
int findSourceIndex(const std::vector<SourceItem>& sourceItems, const juce::String& searchPath);

/**
 * Create a horizontal slider with a right-aligned text box.
 * Handles common setup for parameter sliders (tuning, attack, decay, etc).
 */
std::unique_ptr<juce::Slider> createParamSlider(
    double minVal, 
    double maxVal, 
    double interval,
    double initialValue,
    int textBoxWidth = 40, 
    int textBoxHeight = 20,
    std::function<juce::String(double)> textFromValueFn = nullptr);

/**
 * Create a compact rotary pot with text below.
 */
std::unique_ptr<juce::Slider> createCompactPot(
    double minVal, 
    double maxVal, 
    double interval,
    double initialValue,
    std::function<juce::String(double)> textFromValueFn = nullptr);

}  // namespace UIHelpers
