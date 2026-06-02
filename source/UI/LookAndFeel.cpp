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
            const auto angle = juce::jmap(idx / 8.0f, rotaryStartAngle, rotaryEndAngle);
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
    const auto baseColour = backgroundColour
                                .withMultipliedAlpha(button.isEnabled() ? 0.75f : 0.3f);

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
    }
}

int LookAndFeel::getDefaultScrollbarWidth() {
    return 8;
}

void LookAndFeel::drawCornerResizer(juce::Graphics& g, int w, int h, bool /*isMouseOver*/, bool /*isMouseDragging*/) {
    const float lineThickness = juce::jmin(w, h) * 0.05f;
    const juce::Colour col1(this->findColour(juce::ResizableWindow::backgroundColourId).darker(0.2f));
    const juce::Colour col2(this->findColour(juce::ResizableWindow::backgroundColourId).brighter(0.04f));

    for (float idx = 0.8f; idx > 0.2f; idx -= 0.25f) {
        g.setColour(col1);
        g.drawLine(w * idx, h + 1.0f, w + 1.0f, h * idx, lineThickness);

        g.setColour(col2);
        g.drawLine(w * idx + lineThickness, h + 1.0f, w + 1.0f, h * idx + lineThickness, lineThickness);
    }
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/, int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/, juce::ComboBox& cb) {
    const bool enabled = cb.isEnabled();
    const auto bounds = cb.getLocalBounds();

    g.setColour(cb.findColour(juce::TextButton::ColourIds::buttonColourId));
    g.fillRoundedRectangle(bounds.toFloat(), bounds.getHeight() / 2.0f);

    g.setColour(cb.findColour(juce::TextButton::ColourIds::buttonColourId).withAlpha(enabled ? 0.5f : 0.1f));

    constexpr int triHeight = 6;
    constexpr int triWidth = 12;
    constexpr float lineThickness = 1.4f;
    g.drawLine(width - 10 - triWidth, (height - triHeight) / 2.0f, width - 10 - triWidth / 2.0f, (height + triHeight) / 2.0f, lineThickness);
    g.drawLine(width - 10 - triWidth / 2.0f, (height + triHeight) / 2.0f, width - 10.0f, (height - triHeight) / 2.0f, lineThickness);
}

void LookAndFeel::positionComboBoxText(juce::ComboBox& cb, juce::Label& labelToPosition) {
    labelToPosition.setBounds(cb.getLocalBounds().reduced(cb.getHeight() / 2, 0));
}

void LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar& /*scrollbar*/, int x, int y, int width, int height, bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) {
    const bool highlight = isMouseOver || isMouseDown;

    g.setColour(juce::Colour::fromFloatRGBA(1.f, 1.f, 1.f, highlight ? 0.6f : 0.4f));

    if (isScrollbarVertical)
        g.fillRoundedRectangle(float(x), float(y + thumbStartPosition), float(width), float(thumbSize), width / 2.0f);
    else
        g.fillRoundedRectangle(float(x + thumbStartPosition), float(y), float(thumbSize), float(height), height / 2.0f);
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int /*width*/, int /*height*/) {
    g.fillAll(UITheme::Colours::bgPanelRaised);
}

// Returns MIDI note 0-127 if text matches format "127 / G 8", else -1.
static int parseMidiNoteFromItemText(const juce::String& text) {
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || trimmed == "--") return -1;
    // Number is the leading token before " /"
    const auto numStr = trimmed.upToFirstOccurrenceOf(" /", false, false).trim();
    if (numStr.isEmpty() || !numStr.containsOnly("0123456789")) return -1;
    const int note = numStr.getIntValue();
    return (note >= 0 && note <= 127) ? note : -1;
}

// True for MIDI semitones that are black keys: C# D# F# G# A#
static bool isMidiBlackKey(int note) {
    static const bool BK[12] = { false,true,false,true,false,false,true,false,true,false,true,false };
    return BK[note % 12];
}

void LookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& /*shortcutKeyText*/, const juce::Drawable* /*icon*/, const juce::Colour* /*textColour*/) {
    // Piano-key colouring: each octave gets a distinct hue; black keys are darker.
    // 6 hues cycle across the 11 MIDI octaves (0..10).
    static const float OCTAVE_HUES[6] = { 0.00f, 0.12f, 0.35f, 0.55f, 0.67f, 0.80f };

    int midiNote = parseMidiNoteFromItemText(text);
    if (midiNote >= 0) {
        const float hue = OCTAVE_HUES[(midiNote / 12) % 6];
        const bool  bk  = isMidiBlackKey(midiNote);
        g.setColour(juce::Colour::fromHSV(hue, bk ? 0.55f : 0.30f, bk ? 0.13f : 0.22f, 1.0f));
        g.fillRect(area);
    } else {
        g.setColour(UITheme::Colours::bgPanelRaised);
        g.fillRect(area);
    }

    if (isSeparator) {
        g.setColour(UITheme::Colours::textPrimary.withAlpha(0.2f));
        const auto separatorRect = juce::Rectangle<int>(10, area.getHeight() / 2, area.getWidth() - 20, 1);
        g.fillRect(separatorRect);
    } else {
        if (isHighlighted) {
            g.setColour(midiNote >= 0 ? juce::Colour::fromHSV(OCTAVE_HUES[(midiNote / 12) % 6], 0.4f, 0.40f, 1.0f)
                                      : UITheme::Colours::borderMid);
            g.fillRect(area.reduced(1, 1));
        }

        g.setColour(UITheme::Colours::textPrimary.withAlpha(isActive ? 0.85f : 0.25f));
        // Note items: tight left margin (no tick); other items: standard 25 px margin for tick dot
        const int leftPad = (midiNote >= 0) ? 4 : 25;
        g.drawText(text, area.withTrimmedLeft(leftPad).withTrimmedRight(4), juce::Justification::left);

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
            g.fillEllipse(10.0f, (area.getHeight() - 5.0f) / 2.0f, 5.0f, 5.0f);
        }
    }
}

juce::PopupMenu::Options LookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& cb, juce::Label& label) {
    auto options = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(cb, label);

    const bool isMidiGrid = cb.getProperties().getWithDefault("midiNoteGrid", false);
    if (!isMidiGrid)
        return options;

    const int configuredColumns = static_cast<int>(cb.getProperties().getWithDefault("midiNoteGridColumns", 6));
    const int columns = juce::jmax(1, configuredColumns);

    return options.withStandardItemHeight(16)
                  .withMinimumNumColumns(columns)
                  .withMaximumNumColumns(columns);
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
    // For small buttons (like +, x, ●), use a font that actually fits
    // without JUCE's drawFittedText truncating to "..."
    return juce::Font(juce::FontOptions(juce::jmin(14.0f, static_cast<float>(buttonHeight) * 0.7f)));
}
