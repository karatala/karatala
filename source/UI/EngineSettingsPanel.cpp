#include "EngineSettingsPanel.h"
#include "../Engine/DrumKitEngine.h"

EngineSettingsPanel::EngineSettingsPanel(DrumKitEngine& engine)
    : engine(engine) {
    auto& settings = this->engine.engineSettings;

    // Group labels
    chokeGroupLabel.setText("Aftertouch Choke", juce::dontSendNotification);
    chokeGroupLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    addAndMakeVisible(chokeGroupLabel);

    tailGroupLabel.setText("Tail Shortening", juce::dontSendNotification);
    tailGroupLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    addAndMakeVisible(tailGroupLabel);

    hihatGroupLabel.setText("Hi-Hat / Polyphony", juce::dontSendNotification);
    hihatGroupLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    addAndMakeVisible(hihatGroupLabel);

    // Aftertouch Choke sliders
    this->addSlider("Max Duck Depth", &settings.chokeMaxDuckDepth, 0.0f, 1.0f, 0.01f);
    this->addSlider("Duck Time (sec)", &settings.chokeDuckTimeSec, 0.01f, 1.0f, 0.01f);
    this->addSlider("Release Time (sec)", &settings.chokeReleaseTimeSec, 0.01f, 0.5f, 0.01f);
    this->addSlider("Mute Tail (sec)", &settings.chokeMuteTailSec, 0.01f, 0.5f, 0.01f);
    this->addSlider("Release Boost", &settings.chokeReleaseBoost, 0.0f, 0.5f, 0.01f);
    this->addSlider("Mute Threshold Ratio", &settings.chokeMuteThresholdRatio, 0.0f, 1.0f, 0.01f);

    // Tail Shortening sliders
    this->addSlider("Load Threshold", &settings.tailShortenLoadThreshold, 0.0f, 1.0f, 0.01f);
    this->addSlider("Age Multiplier", &settings.tailShortenAgeMultiplier, 0.0f, 0.2f, 0.001f);
    this->addSlider("Amp Protect", &settings.tailShortenAmpProtect, 0.0f, 1.0f, 0.01f);
    this->addSlider("Min Factor", &settings.tailShortenMinFactor, 0.0f, 1.0f, 0.01f);
    this->addSlider("Voice Min Clamp", &settings.voiceTailShortenMinClamp, 0.0f, 1.0f, 0.01f);
    this->addSlider("Cull Threshold (dB)", &settings.voiceCullThresholdDb, 0.0f, 30.0f, 0.5f);

    // Hi-Hat / Polyphony sliders
    this->addSlider("Polyphony Crossfade (sec)", &settings.polyphonyCrossfadeTimeoutSec, 0.01f, 2.0f, 0.01f);
    this->addSlider("Pedal Crossfade (sec)", &settings.hihatPedalCrossfadeSec, 0.01f, 1.0f, 0.01f);
    this->addSlider("Pedal Min Remaining (sec)", &settings.hihatPedalMinRemainingSec, 0.01f, 0.5f, 0.01f);
    this->addSlider("Cross-Art Choke Margin (dB)", &settings.hihatCrossArtChokeDbMargin, 0.0f, 20.0f, 0.5f);

    this->loadFromEngine();
}

void EngineSettingsPanel::addSlider(const juce::String& name, float* target, float minVal, float maxVal, float interval) {
    auto entry = std::make_unique<SliderEntry>();
    entry->target = target;

    entry->label.setText(name, juce::dontSendNotification);
    entry->label.setFont(juce::Font(11.0f));
    entry->label.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(entry->label);

    entry->slider.setRange(minVal, maxVal, interval);
    entry->slider.setSliderStyle(juce::Slider::LinearHorizontal);
    entry->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    entry->slider.onValueChange = [this] { this->onSettingChanged(); };
    addAndMakeVisible(entry->slider);

    this->sliders.push_back(std::move(entry));
}

void EngineSettingsPanel::loadFromEngine() {
    for (auto& entry : this->sliders) {
        if (entry->target != nullptr)
            entry->slider.setValue(static_cast<double>(*entry->target), juce::dontSendNotification);
    }
}

void EngineSettingsPanel::onSettingChanged() {
    for (auto& entry : this->sliders) {
        if (entry->target != nullptr)
            *entry->target = static_cast<float>(entry->slider.getValue());
    }
    this->engine.saveSettings();
}

void EngineSettingsPanel::resized() {
    auto area = getLocalBounds().reduced(8);
    const int rowHeight = 24;
    const int groupLabelHeight = 22;
    const int labelWidth = 180;
    const int gap = 4;

    auto placeGroupLabel = [&](juce::Label& groupLabel) {
        area.removeFromTop(gap);
        groupLabel.setBounds(area.removeFromTop(groupLabelHeight));
        area.removeFromTop(gap);
    };

    auto placeSliderRows = [&](int startIdx, int count) {
        for (int idx = startIdx; idx < startIdx + count && idx < static_cast<int>(this->sliders.size()); ++idx) {
            auto row = area.removeFromTop(rowHeight);
            this->sliders[idx]->label.setBounds(row.removeFromLeft(labelWidth));
            row.removeFromLeft(gap);
            this->sliders[idx]->slider.setBounds(row);
            area.removeFromTop(2);
        }
    };

    placeGroupLabel(chokeGroupLabel);
    placeSliderRows(0, 6);

    placeGroupLabel(tailGroupLabel);
    placeSliderRows(6, 6);

    placeGroupLabel(hihatGroupLabel);
    placeSliderRows(12, 4);
}
