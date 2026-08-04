#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ParameterSlider.h"
#include "UITheme.h"

enum class PageId {
    Kit = 0,
    Browse,
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

    PageId getActivePageId() const { return this->activePageId; }
    void setActivePageId(PageId pageId);

    std::function<void(PageId)> onPageChanged;

    ParameterSlider velocityKnob;
    ParameterSlider hihatKnob;
    ParameterSlider rrLockKnob;

private:
    static constexpr int buttonSize = UITheme::Sizes::pageButton;
    static constexpr int knobSize = UITheme::Sizes::sidebarKnob;

    PageId activePageId = PageId::Kit;

    struct PageButton : public juce::TextButton {
        PageId pageId;
        explicit PageButton(PageId buttonPageId, const juce::String& label) : pageId(buttonPageId) {
            this->setButtonText(label);
            this->setClickingTogglesState(false);
        }
    };

    juce::OwnedArray<PageButton> pageButtons;

    juce::Label velocityLabel;
    juce::Label hihatLabel;
    juce::Label rrLockLabel;

    void updateButtonStates();

public:
    void updateKnobLabels();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Sidebar)
};
