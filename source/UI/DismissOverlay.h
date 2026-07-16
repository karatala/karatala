#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

class DismissOverlay : public juce::Component {
public:
    DismissOverlay() {
        this->closeButton.onClick = [this] {
            if (this->onClick) this->onClick();
        };
        this->addChildComponent(this->closeButton);
    }

    std::function<void()> onClick;

    void setDarkened(bool shouldDarken) {
        this->darkened = shouldDarken;
        this->repaint();
    }

    void setDismissOnBackgroundClick(bool shouldDismiss) { this->dismissOnBackgroundClick = shouldDismiss; }

    void setCloseButtonBounds(juce::Rectangle<int> bounds) {
        this->closeButton.setBounds(bounds);
        this->closeButton.setVisible(!bounds.isEmpty());
    }

    void paint(juce::Graphics& graphics) override {
        if (this->darkened) graphics.fillAll(juce::Colours::black.withAlpha(0.72f));
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (this->dismissOnBackgroundClick && this->onClick) this->onClick();
    }

private:
    class CloseButton : public juce::Button {
    public:
        CloseButton() : juce::Button("Close") {}

        void paintButton(juce::Graphics& graphics, bool isMouseOverButton, bool isButtonDown) override {
            if (isMouseOverButton || isButtonDown) {
                graphics.setColour(juce::Colours::white.withAlpha(isButtonDown ? 0.18f : 0.10f));
                graphics.fillRoundedRectangle(this->getLocalBounds().toFloat(), 4.0f);
            }
            graphics.setColour(juce::Colours::white);
            graphics.setFont(juce::Font(juce::FontOptions(34.0f)));
            graphics.drawText(juce::String::charToString(0x00d7), this->getLocalBounds(), juce::Justification::centred);
        }
    };

    CloseButton closeButton;
    bool darkened = false;
    bool dismissOnBackgroundClick = true;
};
