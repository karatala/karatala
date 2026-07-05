#include "ParameterSlider.h"

void ParameterSlider::setInteraction(double defaultValue, double newWheelStep, double newFineWheelStep) {
    this->wheelStep = std::abs(newWheelStep);
    this->fineWheelStep = std::abs(newFineWheelStep);
    this->setDoubleClickReturnValue(false, defaultValue, juce::ModifierKeys::noModifiers);
}

void ParameterSlider::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isRightButtonDown()) {
        if (this->onDragStart)
            this->onDragStart();
        this->setValue(this->getDoubleClickReturnValue(), juce::sendNotificationSync);
        if (this->onDragEnd)
            this->onDragEnd();
        return;
    }

    juce::Slider::mouseDown(event);
}

void ParameterSlider::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (!this->isEnabled() || !this->isScrollWheelEnabled() || event.mods.isAnyMouseButtonDown()) {
        juce::Component::mouseWheelMove(event, wheel);
        return;
    }

    const double step = event.mods.isShiftDown() && this->fineWheelStep > 0.0
        ? this->fineWheelStep
        : this->wheelStep;
    if (step <= 0.0) {
        juce::Slider::mouseWheelMove(event, wheel);
        return;
    }

    double wheelAmount = std::abs(wheel.deltaX) > std::abs(wheel.deltaY)
        ? -static_cast<double>(wheel.deltaX)
        : static_cast<double>(wheel.deltaY);
    if (wheel.isReversed)
        wheelAmount = -wheelAmount;
    if (juce::approximatelyEqual(wheelAmount, 0.0))
        return;

    if (this->onDragStart)
        this->onDragStart();
    this->setValue(this->getValue() + (wheelAmount < 0.0 ? -step : step), juce::sendNotificationSync);
    if (this->onDragEnd)
        this->onDragEnd();
}
