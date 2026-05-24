#include "EngineSettingsPanel.h"
#include "../Engine/DrumKitEngine.h"

EngineSettingsPanel::EngineSettingsPanel(DrumKitEngine& engine)
    : engine(engine) {
    auto& settings = this->engine.engineSettings;

    this->viewport.setViewedComponent(&this->contentComponent, false);
    this->viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(this->viewport);

    // Group labels
    chokeGroupLabel.setText("Aftertouch Choke", juce::dontSendNotification);
    chokeGroupLabel.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    this->contentComponent.addAndMakeVisible(chokeGroupLabel);

    tailGroupLabel.setText("Tail Shortening", juce::dontSendNotification);
    tailGroupLabel.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    this->contentComponent.addAndMakeVisible(tailGroupLabel);

    hihatGroupLabel.setText("Hi-Hat / Polyphony", juce::dontSendNotification);
    hihatGroupLabel.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    this->contentComponent.addAndMakeVisible(hihatGroupLabel);

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

    // UI / Grid
    uiGridGroupLabel.setText("UI / Grid", juce::dontSendNotification);
    uiGridGroupLabel.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    this->contentComponent.addAndMakeVisible(uiGridGroupLabel);

    this->addIntSlider("Pad Grid Columns", &settings.padGridColumns, 1, 12);
    this->addIntSlider("Pad Grid Rows", &settings.padGridRows, 1, 8);
    this->addIntSlider("Pad Gap", &settings.padGap, 0, 8);

    this->loadFromEngine();
}

void EngineSettingsPanel::addSlider(const juce::String& name, float* target, float minVal, float maxVal, float interval) {
    auto entry = std::make_unique<SliderEntry>();
    entry->target = target;
    entry->defaultValue = static_cast<double>(*target);

    entry->label.setText(name, juce::dontSendNotification);
    entry->label.setFont(juce::Font(juce::FontOptions(11.0f)));
    entry->label.setJustificationType(juce::Justification::centredRight);
    this->contentComponent.addAndMakeVisible(entry->label);

    entry->slider.setRange(minVal, maxVal, interval);
    entry->slider.setSliderStyle(juce::Slider::LinearHorizontal);
    entry->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    entry->slider.onValueChange = [this] { this->onSettingChanged(); };
    this->contentComponent.addAndMakeVisible(entry->slider);

    entry->resetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    entry->resetButton.onClick = [entry = entry.get()] {
        entry->slider.setValue(entry->defaultValue, juce::sendNotificationSync);
    };
    this->contentComponent.addAndMakeVisible(entry->resetButton);

    this->sliders.push_back(std::move(entry));
}

void EngineSettingsPanel::addIntSlider(const juce::String& name, int* target, int minVal, int maxVal) {
    auto entry = std::make_unique<SliderEntry>();
    entry->intTarget = target;
    entry->defaultValue = static_cast<double>(*target);

    entry->label.setText(name, juce::dontSendNotification);
    entry->label.setFont(juce::Font(juce::FontOptions(11.0f)));
    entry->label.setJustificationType(juce::Justification::centredRight);
    this->contentComponent.addAndMakeVisible(entry->label);

    entry->slider.setRange(static_cast<double>(minVal), static_cast<double>(maxVal), 1.0);
    entry->slider.setSliderStyle(juce::Slider::LinearHorizontal);
    entry->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    entry->slider.onValueChange = [this] { this->onSettingChanged(); };
    this->contentComponent.addAndMakeVisible(entry->slider);

    entry->resetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
    entry->resetButton.onClick = [entry = entry.get()] {
        entry->slider.setValue(entry->defaultValue, juce::sendNotificationSync);
    };
    this->contentComponent.addAndMakeVisible(entry->resetButton);

    this->sliders.push_back(std::move(entry));
}

void EngineSettingsPanel::loadFromEngine() {
    for (auto& entry : this->sliders) {
        if (entry->target != nullptr)
            entry->slider.setValue(static_cast<double>(*entry->target), juce::dontSendNotification);
        else if (entry->intTarget != nullptr)
            entry->slider.setValue(static_cast<double>(*entry->intTarget), juce::dontSendNotification);
    }
}

void EngineSettingsPanel::onSettingChanged() {
    bool gridChanged = false;
    for (auto& entry : this->sliders) {
        if (entry->target != nullptr)
            *entry->target = static_cast<float>(entry->slider.getValue());
        else if (entry->intTarget != nullptr) {
            int oldVal = *entry->intTarget;
            *entry->intTarget = static_cast<int>(entry->slider.getValue());
            if (oldVal != *entry->intTarget)
                gridChanged = true;
        }
    }
    this->engine.saveSettings();
    if (gridChanged && this->onGridSizeChanged)
        this->onGridSizeChanged();
}

void EngineSettingsPanel::resized() {
    this->viewport.setBounds(getLocalBounds());

    const int contentWidth = this->viewport.getWidth() - this->viewport.getScrollBarThickness();
    const int rowHeight = 24;
    const int groupLabelHeight = 22;
    const int labelWidth = 180;
    const int resetBtnWidth = 22;
    const int gap = 4;

    // Calculate total height
    int totalHeight = 8; // top padding
    auto countGroupHeight = [&](int sliderCount) {
        totalHeight += gap + groupLabelHeight + gap;
        totalHeight += sliderCount * (rowHeight + 2);
    };
    countGroupHeight(6);  // choke
    countGroupHeight(6);  // tail
    countGroupHeight(4);  // hihat
    countGroupHeight(3);  // grid
    totalHeight += 8; // bottom padding

    this->contentComponent.setBounds(0, 0, juce::jmax(contentWidth, 400), totalHeight);

    auto area = this->contentComponent.getLocalBounds().reduced(8);

    auto placeGroupLabel = [&](juce::Label& groupLabel) {
        area.removeFromTop(gap);
        groupLabel.setBounds(area.removeFromTop(groupLabelHeight));
        area.removeFromTop(gap);
    };

    auto placeSliderRows = [&](int startIdx, int count) {
        for (int idx = startIdx; idx < startIdx + count && idx < static_cast<int>(this->sliders.size()); ++idx) {
            auto row = area.removeFromTop(rowHeight);
            auto resetArea = row.removeFromRight(resetBtnWidth);
            row.removeFromRight(2);
            this->sliders[idx]->label.setBounds(row.removeFromLeft(labelWidth));
            row.removeFromLeft(gap);
            this->sliders[idx]->slider.setBounds(row);
            this->sliders[idx]->resetButton.setBounds(resetArea.reduced(1));
            area.removeFromTop(2);
        }
    };

    placeGroupLabel(chokeGroupLabel);
    placeSliderRows(0, 6);

    placeGroupLabel(tailGroupLabel);
    placeSliderRows(6, 6);

    placeGroupLabel(hihatGroupLabel);
    placeSliderRows(12, 4);

    placeGroupLabel(uiGridGroupLabel);
    placeSliderRows(16, 2);
}
