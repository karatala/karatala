#pragma once

#include <juce_core/juce_core.h>
#include "../Engine/KitLibModel.h"

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