#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ParameterPot : public juce::Component {
public:
    enum class LayoutMode {
        Vertical,
        Horizontal,
    };

    enum class ValueDisplay {
        EditableLabel,
        TextBoxBelow,
    };

    struct LayoutOptions {
        LayoutMode layoutMode = LayoutMode::Vertical;
        ValueDisplay valueDisplay = ValueDisplay::EditableLabel;
        juce::Justification titleJustification = juce::Justification::centred;
        juce::Justification valueJustification = juce::Justification::centred;
        float valueFontHeight = 0.0f;
        int textBoxWidth = 0;
        int textBoxHeight = 0;
    };

    ParameterPot(const juce::String& name,
                 double minVal,
                 double maxVal,
                 double interval,
                 double initialValue,
                 std::function<juce::String(double)> textFromValueFn = nullptr,
                 LayoutOptions options = {});

    void resized() override;

    juce::Slider& getPot() { return this->pot; }
    const juce::Slider& getPot() const { return this->pot; }

    double getValue() const { return this->pot.getValue(); }
    void setValue(double val, juce::NotificationType notification = juce::dontSendNotification) {
        this->pot.setValue(val, notification);
    }

    void setTooltipText(const juce::String& tooltip);

    std::function<void()> onValueChange;
    std::function<void()> onDragEnd;

private:
    void updateValueText();

    LayoutOptions layoutOptions;
    juce::Label titleLabel;
    juce::Label valueLabel;
    juce::Slider pot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterPot)
};