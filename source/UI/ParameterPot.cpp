#include "ParameterPot.h"

#include "UIComponentStyle.h"

ParameterPot::ParameterPot(const juce::String& name,
                           double minVal,
                           double maxVal,
                           double interval,
                           double initialValue,
                           std::function<juce::String(double)> textFromValueFn,
                           std::function<double(const juce::String&)> valueFromTextFn)
    : ParameterPot(name,
                   minVal,
                   maxVal,
                   interval,
                   initialValue,
                   std::move(textFromValueFn),
                   LayoutOptions{},
                   std::move(valueFromTextFn)) {
}

ParameterPot::ParameterPot(const juce::String& name,
                           double minVal,
                           double maxVal,
                           double interval,
                           double initialValue,
                           std::function<juce::String(double)> textFromValueFn,
                           LayoutOptions options,
                           std::function<double(const juce::String&)> valueFromTextFn)
    : layoutOptions(std::move(options)) {
    this->titleLabel.setText(name, juce::dontSendNotification);
    UIComponentStyle::configureCaptionLabel(this->titleLabel,
                                     this->layoutOptions.titleJustification,
                                     UITheme::Colours::textSubtle);
    addAndMakeVisible(this->titleLabel);

    UIComponentStyle::configureRotarySlider(this->pot,
                                     this->layoutOptions.valueDisplay == ValueDisplay::TextBoxBelow ? juce::Slider::TextBoxBelow
                                                                                                      : juce::Slider::NoTextBox,
                                     this->layoutOptions.textBoxWidth,
                                     this->layoutOptions.textBoxHeight);
    this->pot.setRange(minVal, maxVal, interval);
    if (textFromValueFn)
        this->pot.textFromValueFunction = std::move(textFromValueFn);
    if (valueFromTextFn)
        this->pot.valueFromTextFunction = std::move(valueFromTextFn);
    this->pot.setValue(initialValue, juce::dontSendNotification);
    addAndMakeVisible(this->pot);

    if (this->layoutOptions.valueDisplay == ValueDisplay::EditableLabel) {
        UIComponentStyle::configureValueLabel(this->valueLabel,
                                       this->layoutOptions.valueJustification,
                                       this->layoutOptions.valueFontHeight);
        this->valueLabel.setEditable(true);
        addAndMakeVisible(this->valueLabel);
        this->updateValueText();

        this->valueLabel.onTextChange = [this] {
            const double newValue = this->valueLabel.getText().getDoubleValue();
            this->pot.setValue(newValue, juce::sendNotificationSync);
        };
    } else {
        this->pot.updateText();
    }

    this->pot.onValueChange = [this] {
        if (this->layoutOptions.valueDisplay == ValueDisplay::EditableLabel)
            this->updateValueText();
        if (this->onValueChange)
            this->onValueChange();
    };

    this->pot.onDragEnd = [this] {
        if (this->onDragEnd)
            this->onDragEnd();
    };
}

void ParameterPot::resized() {
    auto area = getLocalBounds();

    if (this->layoutOptions.layoutMode == LayoutMode::Horizontal) {
        const int potSize = juce::jmin(area.getHeight(), area.getWidth() * 4 / 5);
        const auto potArea = area.removeFromRight(potSize);
        this->pot.setBounds(potArea);

        if (this->layoutOptions.valueDisplay == ValueDisplay::EditableLabel) {
            auto textArea = area;
            this->titleLabel.setBounds(textArea.removeFromTop(textArea.getHeight() / 3));
            this->valueLabel.setBounds(textArea);
        } else {
            this->titleLabel.setBounds(area);
        }

        return;
    }

    constexpr int titleHeight = 12;
    this->titleLabel.setBounds(area.removeFromTop(titleHeight));

    if (this->layoutOptions.valueDisplay == ValueDisplay::EditableLabel) {
        constexpr int valueHeight = 14;
        this->valueLabel.setBounds(area.removeFromBottom(valueHeight));
        const int knobSize = juce::jmin(area.getWidth(), area.getHeight());
        this->pot.setBounds(area.withSizeKeepingCentre(knobSize, knobSize));
        return;
    }

    this->pot.setBounds(area);
}

void ParameterPot::setTooltipText(const juce::String& tooltip) {
    this->titleLabel.setTooltip(tooltip);
    this->valueLabel.setTooltip(tooltip);
    this->pot.setTooltip(tooltip);
}

void ParameterPot::updateValueText() {
    this->valueLabel.setText(this->pot.getTextFromValue(this->pot.getValue()), juce::dontSendNotification);
}
