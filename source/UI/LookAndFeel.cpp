#include "LookAndFeel.h"

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider) {
    const auto outline = slider.findColour(juce::Slider::rotarySliderOutlineColourId);
    const auto fill = slider.findColour(juce::Slider::rotarySliderFillColourId);
    const auto text = slider.findColour(juce::Slider::textBoxTextColourId);

    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(10);
    auto centre = bounds.getCentre();

    const auto tickHeight = 6;
    const auto tickWidth = 40;

    auto radius = juce::jmin(bounds.getWidth() / 2.0f, bounds.getHeight() / 2.0f);

    g.setColour(text);
    if (radius > tickWidth * 2.0f + 10.0f) {
        const auto xLeft = int(bounds.getX());
        const auto xRight = int(bounds.getRight() - tickWidth);
        g.drawFittedText(slider.getTextFromValue(slider.proportionOfLengthToValue(0.5f)), int(centre.getX() - tickWidth / 2), int(bounds.getY()), tickWidth, tickHeight, juce::Justification::centred, 1);
        g.drawFittedText(slider.getTextFromValue(slider.proportionOfLengthToValue(0.375f)), xLeft, int(bounds.getY()), tickWidth, tickHeight, juce::Justification::left, 1);
        g.drawFittedText(slider.getTextFromValue(slider.proportionOfLengthToValue(0.25f)), xLeft, int(juce::jmap(0.33f, float(bounds.getY()), bounds.getBottom() - tickHeight)), tickWidth, tickHeight, juce::Justification::left, 1);
        g.drawFittedText(slider.getTextFromValue(slider.proportionOfLengthToValue(0.125f)), xLeft, int(juce::jmap(0.66f, float(bounds.getY()), bounds.getBottom() - tickHeight)), tickWidth, tickHeight, juce::Justification::left, 1);
        g.drawFittedText(slider.getTextFromValue(slider.proportionOfLengthToValue(0.0f)), xLeft, int(bounds.getBottom() - tickHeight), tickWidth, tickHeight, juce::Justification::left, 1);
        g.drawFittedText(slider.getTextFromValue(slider.proportionOfLengthToValue(0.625f)), xRight, int(bounds.getY()), tickWidth, tickHeight, juce::Justification::right, 1);
        g.drawFittedText(slider.getTextFromValue(slider.proportionOfLengthToValue(0.75f)), xRight, int(juce::jmap(0.33f, float(bounds.getY()), bounds.getBottom() - tickHeight)), tickWidth, tickHeight, juce::Justification::right, 1);
        g.drawFittedText(slider.getTextFromValue(slider.proportionOfLengthToValue(0.875f)), xRight, int(juce::jmap(0.66f, float(bounds.getY()), bounds.getBottom() - tickHeight)), tickWidth, tickHeight, juce::Justification::right, 1);
        g.drawFittedText(slider.getTextFromValue(slider.proportionOfLengthToValue(1.0f)), xRight, int(bounds.getBottom() - tickHeight), tickWidth, tickHeight, juce::Justification::right, 1);

        bounds.removeFromTop(tickHeight + 4.0f);
        bounds.reduce(tickWidth, 0.0f);
    }

    radius = juce::jmin(bounds.getWidth() / 2.0f, bounds.getHeight() / 2.0f);
    centre = bounds.getCentre();

    if (radius > 50.0f) {
        for (int idx = 0; idx < 9; ++idx) {
            const auto angle = juce::jmap(static_cast<float>(idx) / 8.0f, rotaryStartAngle, rotaryEndAngle);
            const auto point = centre.getPointOnCircumference(radius - 2.0f, angle);
            g.fillEllipse(point.getX() - 3, point.getY() - 3, 6, 6);
        }
        radius -= 10.0f;
    }

    auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto lineW = juce::jmin(4.0f, radius * 0.5f);
    auto arcRadius = radius - lineW;

    juce::Path backgroundArc;
    backgroundArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);

    g.setColour(outline);
    g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    auto knobRadius = std::max(radius - 3.0f * lineW, 10.0f);
    {
        juce::Graphics::ScopedSaveState saved(g);
        if (slider.isEnabled()) {
            juce::ColourGradient fillGradient(UITheme::Colours::bgControlRaised,
                                              centre.getX() + lineW * 2.0f,
                                              centre.getY() - lineW * 4.0f,
                                              UITheme::Colours::bgPanel,
                                              centre.getX() + knobRadius,
                                              centre.getY() + knobRadius,
                                              true);
            g.setGradientFill(fillGradient);
        }
        g.fillEllipse(centre.getX() - knobRadius, centre.getY() - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);
    }

    knobRadius = std::max(knobRadius - 4.0f, 10.0f);
    g.setColour(UITheme::Colours::borderStrong);
    g.drawEllipse(centre.getX() - knobRadius, centre.getY() - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f, 1.5f);

    if (slider.isEnabled() && arcRadius > 10.0f) {
        juce::Path valueArc;
        valueArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius, 0.0f, rotaryStartAngle, toAngle, true);

        g.setColour(fill);
        g.strokePath(valueArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    // Indicator dot at the knob edge — uses fill (red) color
    auto dotPos = centre.getPointOnCircumference(knobRadius - lineW * 0.5f, toAngle);
    float dotRadius = juce::jmin(lineW * 1.2f, 4.0f);
    g.setColour(fill);
    g.fillEllipse(dotPos.getX() - dotRadius, dotPos.getY() - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour, bool isMouseOverButton, bool isButtonDown) {
    const bool broadEditActive = static_cast<bool>(button.getProperties().getWithDefault("broadEditMode", false)) && button.getToggleState();
    const auto baseColour = backgroundColour
                                .withMultipliedAlpha(button.isEnabled() ? (broadEditActive ? 1.0f : 0.75f) : 0.3f);

    const auto btnWidth = float(button.getWidth());
    const auto btnHeight = float(button.getHeight());

    if (btnWidth > 0 && btnHeight > 0) {
        const float cornerSize = 1.f;

        juce::Path outlinePath;
        outlinePath.addRoundedRectangle(0.f, 0.f, btnWidth, btnHeight, cornerSize, cornerSize, true, true, true, true);

        g.setGradientFill(juce::ColourGradient(
            baseColour.darker(0.1f), 0.f, btnHeight / 2 - 2, baseColour.darker(0.2f), 0.f, btnHeight / 2 + 2, false
        ));

        g.fillPath(outlinePath);

        if (isButtonDown || isMouseOverButton) {
            g.setColour(baseColour.brighter(isButtonDown ? 0.1f : 0.01f));
            g.fillPath(outlinePath);
        }

        constexpr auto outlineAlpha = 0.05f;

        g.setColour(juce::Colours::white.withAlpha(outlineAlpha));
        g.strokePath(outlinePath, juce::PathStrokeType(1.f));

        g.setColour(juce::Colours::black.withAlpha(outlineAlpha));
        g.strokePath(outlinePath, juce::PathStrokeType(1.f), juce::AffineTransform::translation(0.f, 1.f).scaled(1.f, (btnHeight + 2.f) / btnHeight));

        if (broadEditActive) {
            g.setColour(UITheme::Colours::textPrimary);
            g.strokePath(outlinePath, juce::PathStrokeType(2.0f));
        }
    }
}

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton, bool isButtonDown) {
    const bool dialogCloseButton = button.getProperties().getWithDefault("dialogCloseButton", false);
    if (dialogCloseButton) {
        const auto textColour = button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);
        const float iconSize = static_cast<float>(juce::jmin(button.getWidth(), button.getHeight())) * 0.4f;
        const auto iconBounds = juce::Rectangle<float>(iconSize, iconSize).withCentre(button.getLocalBounds().toFloat().getCentre());
        g.setColour(textColour);
        g.drawLine(iconBounds.getX(), iconBounds.getY(), iconBounds.getRight(), iconBounds.getBottom(), 1.5f);
        g.drawLine(iconBounds.getRight(), iconBounds.getY(), iconBounds.getX(), iconBounds.getBottom(), 1.5f);
        return;
    }

    const bool leftAlignedText = button.getProperties().getWithDefault("leftAlignedText", false);
    if (!leftAlignedText) {
        juce::LookAndFeel_V4::drawButtonText(g, button, isMouseOverButton, isButtonDown);
        return;
    }

    auto font = this->getTextButtonFont(button, button.getHeight());
    g.setFont(font);
    g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

    auto textBounds = button.getLocalBounds().reduced(6, 1);
    g.drawFittedText(button.getButtonText(), textBounds, juce::Justification::centredLeft, 1);
}

int LookAndFeel::getDefaultScrollbarWidth() {
    return 8;
}

void LookAndFeel::drawCornerResizer(juce::Graphics& g, int w, int h, bool /*isMouseOver*/, bool /*isMouseDragging*/) {
    const float width = static_cast<float>(w);
    const float height = static_cast<float>(h);
    const float lineThickness = static_cast<float>(juce::jmin(w, h)) * 0.05f;
    const juce::Colour col1(this->findColour(juce::ResizableWindow::backgroundColourId).darker(0.2f));
    const juce::Colour col2(this->findColour(juce::ResizableWindow::backgroundColourId).brighter(0.04f));

    for (float idx = 0.8f; idx > 0.2f; idx -= 0.25f) {
        g.setColour(col1);
        g.drawLine(width * idx, height + 1.0f, width + 1.0f, height * idx, lineThickness);

        g.setColour(col2);
        g.drawLine(width * idx + lineThickness, height + 1.0f, width + 1.0f, height * idx + lineThickness, lineThickness);
    }
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/, int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/, juce::ComboBox& cb) {
    const bool enabled = cb.isEnabled();
    const auto bounds = cb.getLocalBounds();

    g.setColour(cb.findColour(juce::ComboBox::backgroundColourId));
    g.fillRect(bounds);

    const bool compactRoute = cb.getProperties().getWithDefault("compactRoute", false);
    if (compactRoute) {
        g.setColour(cb.findColour(juce::ComboBox::outlineColourId));
        g.drawRect(bounds);
    }

    const bool hideArrow = cb.getProperties().getWithDefault("hideArrow", false);
    if (hideArrow) return;

    g.setColour(cb.findColour(juce::ComboBox::arrowColourId).withAlpha(enabled ? 0.8f : 0.2f));
    constexpr int triHeight = 6;
    constexpr int triWidth = 12;
    constexpr float lineThickness = 1.4f;
    g.drawLine(static_cast<float>(width - 10 - triWidth), static_cast<float>(height - triHeight) / 2.0f,
               static_cast<float>(width - 10 - triWidth / 2), static_cast<float>(height + triHeight) / 2.0f,
               lineThickness);
    g.drawLine(static_cast<float>(width - 10 - triWidth / 2), static_cast<float>(height + triHeight) / 2.0f,
               static_cast<float>(width) - 10.0f, static_cast<float>(height - triHeight) / 2.0f,
               lineThickness);
}

void LookAndFeel::positionComboBoxText(juce::ComboBox& cb, juce::Label& labelToPosition) {
    labelToPosition.setFont(this->getComboBoxFont(cb));
    labelToPosition.setJustificationType(juce::Justification::centredLeft);
    const bool compactRoute = cb.getProperties().getWithDefault("compactRoute", false);
    labelToPosition.setBounds(compactRoute ? cb.getLocalBounds().reduced(3, 1) : cb.getLocalBounds().reduced(8, 2).withTrimmedRight(18));
}

void LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar& /*scrollbar*/, int x, int y, int width, int height, bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) {
    const bool highlight = isMouseOver || isMouseDown;

    g.setColour(juce::Colour::fromFloatRGBA(1.f, 1.f, 1.f, highlight ? 0.6f : 0.4f));

    if (isScrollbarVertical)
        g.fillRoundedRectangle(float(x), float(y + thumbStartPosition), float(width), float(thumbSize), static_cast<float>(width) / 2.0f);
    else
        g.fillRoundedRectangle(float(x + thumbStartPosition), float(y), float(thumbSize), float(height), static_cast<float>(height) / 2.0f);
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int /*width*/, int /*height*/) {
    g.fillAll(UITheme::Colours::bgPanelRaised);
}

void LookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& /*shortcutKeyText*/, const juce::Drawable* /*icon*/, const juce::Colour* /*textColour*/) {
    g.setColour(UITheme::Colours::bgPanelRaised);
    g.fillRect(area);

    if (isSeparator) {
        g.setColour(UITheme::Colours::textPrimary.withAlpha(0.2f));
        const auto separatorRect = juce::Rectangle<int>(10, area.getHeight() / 2, area.getWidth() - 20, 1);
        g.fillRect(separatorRect);
    } else {
        if (isHighlighted) {
            g.setColour(UITheme::Colours::borderMid);
            g.fillRect(area.reduced(1, 1));
        }

        g.setFont(this->getPopupMenuFont());
        g.setColour(UITheme::Colours::textPrimary.withAlpha(isActive ? 0.85f : 0.25f));
        g.drawText(text, area.withTrimmedLeft(25).withTrimmedRight(4), juce::Justification::left);

        if (hasSubMenu) {
            constexpr int subTriWidth = 6;
            constexpr int subTriHeight = 8;

            const int x0 = area.getRight() - subTriWidth - 10;
            const int x1 = x0 + subTriWidth;
            const int y0 = area.getY() + area.getHeight() / 2 - subTriHeight / 2;
            const int y1 = y0 + subTriHeight / 2;
            const int y2 = y1 + subTriHeight / 2;

            juce::Path triangle;
            triangle.addTriangle(float(x0), float(y0), float(x0), float(y2), float(x1), float(y1));
            g.fillPath(triangle);
        }

        if (isTicked) {
            g.setColour(UITheme::Colours::accent);
            g.fillEllipse(10.0f, (static_cast<float>(area.getHeight()) - 5.0f) / 2.0f, 5.0f, 5.0f);
        }
    }
}

juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox& comboBox) {
    if (comboBox.getProperties().getWithDefault("compactRoute", false)) return UITheme::Fonts::small();
    return UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight));
}

juce::Font LookAndFeel::getPopupMenuFont() {
    return UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight));
}

juce::PopupMenu::Options LookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& cb, juce::Label& label) {
    auto options = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(cb, label);

    const bool isLayoutSelector = cb.getProperties().getWithDefault("layoutSelector", false);
    if (isLayoutSelector) {
        return juce::PopupMenu::Options()
            .withTargetComponent(cb)
            .withTargetScreenArea(cb.getScreenBounds())
            .withInitiallySelectedItem(cb.getSelectedId())
            .withMinimumWidth(juce::jmax(280, cb.getWidth()))
            .withMaximumNumColumns(1)
            .withStandardItemHeight(juce::jmax(22, label.getHeight()))
            .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards);
    }

    const bool compactRoute = cb.getProperties().getWithDefault("compactRoute", false);
    if (compactRoute) return options.withMinimumWidth(juce::jmax(120, cb.getWidth())).withStandardItemHeight(18).withMaximumNumColumns(1);

    const bool isMidiControllerList = cb.getProperties().getWithDefault("midiControllerList", false);
    if (isMidiControllerList) return options.withStandardItemHeight(18).withMinimumNumColumns(4).withMaximumNumColumns(4);
    return options
        .withMinimumWidth(cb.getWidth())
        .withStandardItemHeight(22)
        .withMaximumNumColumns(1);
}

void LookAndFeel::drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown) {
    const juce::Rectangle<int> activeArea(button.getActiveArea());

    const juce::Colour bkg(UITheme::Colours::borderMid);

    g.setGradientFill(juce::ColourGradient(bkg.brighter(0.1f), 0, float(activeArea.getY()), bkg.darker(0.1f), 0, float(activeArea.getBottom()), false));
    g.fillRect(activeArea);

    g.setColour(button.findColour(juce::TabbedComponent::backgroundColourId).darker(0.3f));
    g.drawRect(activeArea);

    const float alpha = button.isEnabled() ? ((isMouseOver || isMouseDown) ? 1.0f : 0.8f) : 0.3f;
    const juce::Colour col(bkg.contrasting().withMultipliedAlpha(alpha));

    juce::TextLayout textLayout;
    juce::LookAndFeel_V3::createTabTextLayout(button, float(activeArea.getWidth()), float(activeArea.getHeight()), col, textLayout);

    textLayout.draw(g, button.getTextArea().toFloat());
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton& button, int buttonHeight) {
    juce::ignoreUnused(button);
    const auto textHeight = juce::jmin(static_cast<float>(UITheme::Sizes::standardControlTextHeight), static_cast<float>(buttonHeight) * 0.7f);
    return UITheme::Fonts::normal().withHeight(textHeight);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    const auto font = UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight));
    const auto tickWidth = font.getHeight() * 1.1f;

    this->drawTickBox(g, button, 4.0f, (static_cast<float>(button.getHeight()) - tickWidth) * 0.5f, tickWidth, tickWidth,
                      button.getToggleState(), button.isEnabled(), shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    g.setColour(button.findColour(juce::ToggleButton::textColourId));
    g.setFont(font);
    if (!button.isEnabled()) g.setOpacity(0.5f);

    g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(juce::roundToInt(tickWidth) + 10).withTrimmedRight(2), juce::Justification::centredLeft, 10);
}
