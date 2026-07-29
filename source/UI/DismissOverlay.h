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
            const float iconSize = static_cast<float>(juce::jmin(this->getWidth(), this->getHeight())) * 0.45f;
            const auto iconBounds = juce::Rectangle<float>(iconSize, iconSize).withCentre(this->getLocalBounds().toFloat().getCentre());
            graphics.setColour(juce::Colours::white);
            graphics.drawLine(iconBounds.getX(), iconBounds.getY(), iconBounds.getRight(), iconBounds.getBottom(), 3.0f);
            graphics.drawLine(iconBounds.getRight(), iconBounds.getY(), iconBounds.getX(), iconBounds.getBottom(), 3.0f);
        }
    };

    CloseButton closeButton;
    bool darkened = false;
    bool dismissOnBackgroundClick = true;
};
