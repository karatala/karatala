#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/LayoutManager.h"
#include "ParameterSlider.h"
#include "SourceItem.h"
#include "UITheme.h"

namespace UIHelpers {

enum class ComboBoxStyle {
    Standard,
    LayoutSelector,
    MidiControllerList
};

void configureComboBox(juce::ComboBox& comboBox, ComboBoxStyle style = ComboBoxStyle::Standard);

void configureCaptionLabel(juce::Label& label,
                           juce::Justification justification = juce::Justification::centred,
                           juce::Colour colour = UITheme::Colours::textSecondary);

void configureValueLabel(juce::Label& label,
                         juce::Justification justification = juce::Justification::centred,
                         float fontHeight = 0.0f);

void configureSectionLabel(juce::Label& label);

void configureRotarySlider(juce::Slider& slider,
                           juce::Slider::TextEntryBoxPosition textBoxPosition,
                           int textBoxWidth,
                           int textBoxHeight);

void configureControlButton(juce::TextButton& button, int connectedEdges = 0);

void configureDialogCloseButton(juce::TextButton& button);

void configureSettingsButton(juce::TextButton& button);

void configureCategoryButton(juce::TextButton& button);

void configureToggleButton(juce::TextButton& button, int connectedEdges = 0);

void configureTableHeaderLabel(juce::Label& label);

void configureTableRowLabel(juce::Label& label,
                            juce::Colour colour = UITheme::Colours::textPrimary,
                            juce::Justification justification = juce::Justification::centredLeft);

void configureMonoTextEditor(juce::TextEditor& editor,
                             juce::Colour textColour = UITheme::Colours::textPrimary,
                             bool multiline = true);

void configureBorderlessTextEditor(juce::TextEditor& editor);

juce::String sentinelValueText(double value);

juce::String getMidiControllerItemText(int controllerNumber);

void applyMidiNoteToSlot(KitLayout::MidiNotes& notes, int slotIdx, int note);

std::vector<SourceItem> buildSourceItems(const KitLib::LibraryManager* libraryManager);

int findSourceIndex(const std::vector<SourceItem>& sourceItems, const juce::String& searchPath);

bool usesLegacyArticulationSlotExpressionFilter(const std::vector<juce::String>& articulationSlotFilters, const juce::String& articulationSlotCode);

juce::String resolveLegacyHihatSourcePath(const std::vector<SourceItem>& sourceItems, const juce::String& sourcePath, const juce::String& articulationSlotCode);

std::unique_ptr<juce::Slider> createParamSlider(
    double minVal,
    double maxVal,
    double interval,
    double initialValue,
    double defaultValue,
    double wheelStep,
    double fineWheelStep = 0.0,
    int textBoxWidth = UITheme::Sizes::sliderTextBoxW,
    int textBoxHeight = UITheme::Sizes::sliderTextBoxH,
    std::function<juce::String(double)> textFromValueFn = nullptr);

std::unique_ptr<juce::Slider> createCompactPot(
    double minVal,
    double maxVal,
    double interval,
    double initialValue,
    double defaultValue,
    double wheelStep,
    double fineWheelStep = 0.0,
    std::function<juce::String(double)> textFromValueFn = nullptr);

} // namespace UIHelpers
