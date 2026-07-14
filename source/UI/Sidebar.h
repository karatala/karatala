#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ParameterSlider.h"
#include "UITheme.h"

enum class ViewMode {
    Kit = 0,
    Map,
    Zone,
    Instrument,
    Raw,
    Settings,
    Fix,
    Playground
};

class Sidebar : public juce::Component {
public:
    explicit Sidebar();
    ~Sidebar() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    ViewMode getActiveMode() const { return this->activeMode; }
    void setActiveMode(ViewMode mode);

    std::function<void(ViewMode)> onModeChanged;

    ParameterSlider velocityKnob;
    ParameterSlider hihatKnob;
    ParameterSlider rrLockKnob;
    juce::TextButton inspectButton { "?" };

private:
    static constexpr int buttonSize = UITheme::Sizes::modeButton;
    static constexpr int knobSize = UITheme::Sizes::sidebarKnob;

    ViewMode activeMode = ViewMode::Kit;

    struct ModeButton : public juce::TextButton {
        ViewMode mode;
        explicit ModeButton(ViewMode buttonMode, const juce::String& label) : mode(buttonMode) {
            this->setButtonText(label);
            this->setClickingTogglesState(false);
        }
    };

    juce::OwnedArray<ModeButton> modeButtons;

    juce::Label velocityLabel;
    juce::Label hihatLabel;
    juce::Label rrLockLabel;

    void updateButtonStates();

public:
    void updateKnobLabels();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Sidebar)
};
