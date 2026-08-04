#pragma once

#include <juce_core/juce_core.h>
#include "../Engine/InstrumentType.h"

struct SourceItem {
    juce::String kitCodeRaw;
    juce::String kitDisplayName;
    juce::String instCode;
    juce::String artCode;
    juce::String rawKitCode;
    juce::String rawInstCode;
    juce::String rawArtCode;
    KitLib::InstrumentType instType = KitLib::InstrumentType::Unknown;

    juce::String fullPath() const {
        juce::String result = this->kitCodeRaw;
        if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
        if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
        return result;
    }

    juce::String rawFullPath() const {
        juce::String result = this->rawKitCode.isNotEmpty() ? this->rawKitCode : this->kitCodeRaw;
        const auto instrumentCode = this->rawInstCode.isNotEmpty() ? this->rawInstCode : this->instCode;
        const auto articulationCode = this->rawArtCode.isNotEmpty() ? this->rawArtCode : this->artCode;
        if (instrumentCode.isNotEmpty()) result += "/" + instrumentCode;
        if (articulationCode.isNotEmpty()) result += "/" + articulationCode;
        return result;
    }

    juce::String displayText() const {
        juce::String result = this->kitDisplayName;
        if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
        if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
        return result;
    }
};
