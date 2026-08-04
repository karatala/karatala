#include "CompactParameterControl.h"

#include "UITheme.h"

#include <utility>

CompactParameterControl::CompactParameterControl(const juce::String& name, double minVal, double maxVal, double interval, double initialValue, std::function<juce::String(double)> textFromValueFn) {
    this->setName(name);
    this->setSliderStyle(juce::Slider::RotaryHorizontalDrag);
    this->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    this->setRange(minVal, maxVal, interval);
    if (textFromValueFn)
        this->textFromValueFunction = std::move(textFromValueFn);
    this->setValue(initialValue, juce::dontSendNotification);
}

juce::String CompactParameterControl::sentinelValueText(double value) {
    return value < 0.0 ? "--" : juce::String(static_cast<int>(value));
}

void CompactParameterControl::setLabelVisible(bool shouldShow) {
    this->showLabel = shouldShow;
    this->repaint();
}

void CompactParameterControl::paint(juce::Graphics& graphics) {
    auto area = this->getLocalBounds();

    if (this->showLabel) {
        graphics.setColour(UITheme::Colours::textSubtle);
        graphics.setFont(UITheme::Fonts::small());
        graphics.drawFittedText(this->getName(), area.removeFromTop(10), juce::Justification::centred, 1);
    }

    auto lineArea = area.removeFromBottom(4).reduced(3, 1);
    constexpr int valueAreaHeight = 16;
    auto valueArea = area.withSizeKeepingCentre(area.getWidth(), juce::jmin(valueAreaHeight, area.getHeight())).reduced(1, 0);

    graphics.setColour(this->isEnabled() ? UITheme::Colours::textSecondary : UITheme::Colours::textMuted);
    graphics.setFont(UITheme::Fonts::labelBoldMedium());
    graphics.drawText(this->getTextFromValue(this->getValue()), valueArea, juce::Justification::centred, false);

    const float lineHeight = 2.0f;
    auto track = lineArea.withHeight(static_cast<int>(lineHeight)).withCentre(lineArea.getCentre()).toFloat();
    graphics.setColour(UITheme::Colours::borderMid);
    graphics.fillRoundedRectangle(track, lineHeight * 0.5f);

    const float proportion = static_cast<float>(juce::jlimit(0.0, 1.0, this->valueToProportionOfLength(this->getValue())));
    auto fill = track.withWidth(track.getWidth() * proportion);
    if (fill.getWidth() > 0.0f) {
        graphics.setColour(this->isEnabled() ? UITheme::Colours::accent : UITheme::Colours::textMuted);
        graphics.fillRoundedRectangle(fill, lineHeight * 0.5f);
    }
}
