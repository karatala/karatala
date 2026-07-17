#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UITheme.h"

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel() {
        this->setColourScheme({
            UITheme::Colours::bgApp, // windowBackground
            UITheme::Colours::bgPanelRaised, // widgetBackground
            UITheme::Colours::bgPanelRaised, // menuBackground
            UITheme::Colours::textPrimary, // outline
            UITheme::Colours::textPrimary, // defaultText
            UITheme::Colours::accent,      // defaultFill
            UITheme::Colours::textPrimary, // highlightedText
            UITheme::Colours::accent,      // highlightedFill
            UITheme::Colours::textPrimary  // menuText
        });

        this->setColour(juce::TabbedComponent::outlineColourId, UITheme::Colours::bgPanelRaised);
        this->setColour(juce::GroupComponent::outlineColourId, UITheme::Colours::borderMid);
        this->setColour(juce::Slider::textBoxOutlineColourId, UITheme::Colours::bgApp);
        this->setColour(juce::TextEditor::highlightColourId, UITheme::Colours::listSelection);
        this->setColour(juce::TextEditor::highlightedTextColourId, UITheme::Colours::textPrimary);
    }

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton, bool isButtonDown) override;

    int getDefaultScrollbarWidth() override;
    bool areScrollbarButtonsVisible() override { return false; }
    void drawScrollbarButton(juce::Graphics& g, juce::ScrollBar& bar,
                             int width, int height, int buttonDirection,
                             bool isScrollbarVertical, bool isMouseOverButton,
                             bool isButtonDown) override { juce::ignoreUnused(g, bar, width, height, buttonDirection, isScrollbarVertical, isMouseOverButton, isButtonDown); }

    void drawCornerResizer(juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& cb) override;
    void positionComboBoxText(juce::ComboBox& cb, juce::Label& labelToPosition) override;
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& scrollbar, int x, int y, int width, int height, bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getComboBoxFont(juce::ComboBox& comboBox) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& cb, juce::Label& label) override;
    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown) override;
};
