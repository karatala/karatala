#include "UIHelpers.h"

#include "../Engine/ArticulationNames.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <unordered_set>

namespace UIHelpers {

void configureComboBox(juce::ComboBox& comboBox, ComboBoxStyle style) {
    comboBox.getProperties().set("squareCorners", true);
    comboBox.getProperties().set("layoutSelector", style == ComboBoxStyle::LayoutSelector);
    comboBox.getProperties().set("midiControllerList", style == ComboBoxStyle::MidiControllerList);
    comboBox.setColour(juce::ComboBox::backgroundColourId, UITheme::Colours::bgControl);
    comboBox.setColour(juce::ComboBox::textColourId, UITheme::Colours::textPrimary);
    comboBox.setColour(juce::ComboBox::arrowColourId, UITheme::Colours::textSecondary);
    comboBox.setColour(juce::ComboBox::outlineColourId, UITheme::Colours::borderSubtle);
}

void configureCaptionLabel(juce::Label& label,
                           juce::Justification justification,
                           juce::Colour colour) {
    label.setFont(UITheme::Fonts::small());
    label.setJustificationType(justification);
    label.setColour(juce::Label::textColourId, colour);
}

void configureValueLabel(juce::Label& label,
                         juce::Justification justification,
                         float fontHeight) {
    auto font = UITheme::Fonts::label();
    if (fontHeight > 0.0f)
        font = font.withHeight(fontHeight);

    label.setFont(font);
    label.setJustificationType(justification);
    label.setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
}

void configureSectionLabel(juce::Label& label) {
    label.setFont(UITheme::Fonts::sectionTitle());
    label.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
}

void configureRotarySlider(juce::Slider& slider,
                           juce::Slider::TextEntryBoxPosition textBoxPosition,
                           int textBoxWidth,
                           int textBoxHeight) {
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(textBoxPosition, false, textBoxWidth, textBoxHeight);
    slider.setColour(juce::Slider::rotarySliderFillColourId, UITheme::Colours::accent);
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, UITheme::Colours::borderMid);
    slider.setColour(juce::Slider::textBoxTextColourId, UITheme::Colours::textSecondary);
    slider.setColour(juce::Slider::textBoxOutlineColourId, UITheme::Colours::transparent);
}

void configureControlButton(juce::TextButton& button, int connectedEdges) {
    button.setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    button.setConnectedEdges(connectedEdges);
}

void configureDialogCloseButton(juce::TextButton& button) {
    UIHelpers::configureControlButton(button);
    button.setButtonText(UITheme::Symbols::times);
    button.getProperties().set("dialogCloseButton", true);
    button.setTooltip("Close");
}

void configureSettingsButton(juce::TextButton& button) {
    UIHelpers::configureControlButton(button);
    button.setButtonText(juce::String());
    button.getProperties().set("settingsButton", true);
}

void configureCategoryButton(juce::TextButton& button) {
    button.setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    button.setColour(juce::TextButton::textColourOffId, UITheme::Colours::textSecondary);
    button.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
}

void configureToggleButton(juce::TextButton& button, int connectedEdges) {
    button.setClickingTogglesState(true);
    button.setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    button.setColour(juce::TextButton::buttonOnColourId, UITheme::Colours::accentWarning);
    button.setColour(juce::TextButton::textColourOffId, UITheme::Colours::textMuted);
    button.setColour(juce::TextButton::textColourOnId, UITheme::Colours::textPrimary);
    button.setConnectedEdges(connectedEdges);
}

void configureTableHeaderLabel(juce::Label& label) {
    label.setFont(UITheme::Fonts::small().boldened());
    label.setColour(juce::Label::textColourId, UITheme::Colours::textMuted);
    label.setJustificationType(juce::Justification::centredLeft);
}

void configureTableRowLabel(juce::Label& label,
                            juce::Colour colour,
                            juce::Justification justification) {
    label.setFont(UITheme::Fonts::normal());
    label.setColour(juce::Label::textColourId, colour);
    label.setJustificationType(justification);
}

void configureMonoTextEditor(juce::TextEditor& editor,
                             juce::Colour textColour,
                             bool multiline) {
    editor.setMultiLine(multiline);
    editor.setScrollbarsShown(true);
    editor.setFont(UITheme::Fonts::mono());
    editor.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgEditor);
    editor.setColour(juce::TextEditor::textColourId, textColour);
}

void configureBorderlessTextEditor(juce::TextEditor& editor) {
    editor.setColour(juce::TextEditor::outlineColourId, UITheme::Colours::transparent);
    editor.setColour(juce::TextEditor::focusedOutlineColourId, UITheme::Colours::transparent);
    editor.setColour(juce::TextEditor::shadowColourId, UITheme::Colours::transparent);
}

juce::String sentinelValueText(double value) {
    return value < 0.0 ? "--" : juce::String(static_cast<int>(value));
}

juce::String getMidiControllerItemText(int controllerNumber) {
    const auto* controllerName = juce::MidiMessage::getControllerName(controllerNumber);
    return controllerName != nullptr
        ? juce::String(controllerNumber) + " / " + juce::String(controllerName)
        : juce::String(controllerNumber);
}

void applyMidiNoteToSlot(KitLayout::MidiNotes& notes, int slotIdx, int note) {
    if (slotIdx < 0)
        return;

    while (static_cast<int>(notes.size()) <= slotIdx)
        notes.push_back(0);

    if (note >= 0 && note <= 127) {
        notes[static_cast<size_t>(slotIdx)] = static_cast<uint8_t>(note);
        return;
    }

    if (slotIdx < static_cast<int>(notes.size()))
        notes.erase(notes.begin() + slotIdx);
}

std::vector<SourceItem> buildSourceItems(const KitLib::LibraryManager* libraryManager) {
    std::vector<SourceItem> sourceItems;
    if (libraryManager == nullptr) return sourceItems;

    std::unordered_set<std::string> seenFullPathKeys;
    auto addUniqueSourceItem = [&sourceItems, &seenFullPathKeys](SourceItem sourceItem) {
        const auto fullPathKey = sourceItem.fullPath().trim().toLowerCase().toStdString();
        if (fullPathKey.empty())
            return;

        if (seenFullPathKeys.insert(fullPathKey).second)
            sourceItems.push_back(std::move(sourceItem));
    };

    for (const auto& kitPtr : libraryManager->getKits()) {
        if (kitPtr == nullptr) continue;
        const auto kitId = libraryManager->getPreferredKitId(*kitPtr);
        juce::String kitShort = kitPtr->meta.name;
        if (kitShort.isEmpty()) kitShort = kitPtr->meta.codeRaw;

        std::vector<const KitLib::InstrumentSpec*> instruments;
        for (const auto& [code, instPtr] : kitPtr->getInstrumentSpecsMap())
            instruments.push_back(instPtr.get());
        std::sort(instruments.begin(), instruments.end(), [](const auto* leftInstrument, const auto* rightInstrument) {
            return leftInstrument->meta.codeRaw < rightInstrument->meta.codeRaw;
        });

        for (const auto* inst : instruments) {
            if (inst->articulationSpecs.empty()) continue;
            for (const auto& artPtr : inst->articulationSpecs) {
                if (artPtr == nullptr) continue;
                SourceItem item;
                item.kitCodeRaw = kitId;
                item.kitDisplayName = kitShort;
                item.instCode = kitPtr->getPreferredInstrumentId(*inst);
                item.artCode = inst->getPreferredArticulationId(*artPtr);
                item.rawKitCode = kitPtr->meta.codeRaw;
                item.rawInstCode = inst->meta.codeRaw;
                item.rawArtCode = artPtr->codeRaw;
                item.instType = inst->meta.type;
                addUniqueSourceItem(std::move(item));
            }
        }
    }
    return sourceItems;
}

int findSourceIndex(const std::vector<SourceItem>& sourceItems, const juce::String& searchPath) {
    auto needle = searchPath.trim().toLowerCase();
    if (needle.isEmpty())
        return -1;
    for (int idx = 0; idx < static_cast<int>(sourceItems.size()); ++idx) {
        const auto& item = sourceItems[static_cast<size_t>(idx)];
        auto haystack = item.fullPath().toLowerCase();
        auto rawHaystack = item.rawFullPath().toLowerCase();
        if (haystack == needle || rawHaystack == needle)
            return idx;
    }

    juce::StringArray needleParts;
    needleParts.addTokens(needle, "/", "");
    needleParts.removeEmptyStrings();
    if (needleParts.size() >= 3) {
        const auto needleBaseArt = ArticulationNames::normalizeArticulationName(needleParts[2]);
        for (int idx = 0; idx < static_cast<int>(sourceItems.size()); ++idx) {
            const auto& item = sourceItems[static_cast<size_t>(idx)];
            if (item.kitCodeRaw.toLowerCase() != needleParts[0] && item.rawKitCode.toLowerCase() != needleParts[0]) continue;
            if (item.instCode.toLowerCase() != needleParts[1] && item.rawInstCode.toLowerCase() != needleParts[1]) continue;
            if (ArticulationNames::normalizeArticulationName(item.artCode) == needleBaseArt || ArticulationNames::normalizeArticulationName(item.rawArtCode) == needleBaseArt) return idx;
        }
    }
    return -1;
}

bool usesLegacyArticulationSlotExpressionFilter(const std::vector<juce::String>& articulationSlotFilters, const juce::String& articulationSlotCode) {
    if (!articulationSlotCode.containsChar('[')) return false;
    if (articulationSlotFilters.empty()) return false;

    for (const auto& filter : articulationSlotFilters)
        if (ArticulationNames::articulationCodeMatches(filter, articulationSlotCode)) return false;

    for (const auto& filter : articulationSlotFilters)
        if (ArticulationNames::mapFilterMatchesArticulation(filter, articulationSlotCode)) return true;

    return false;
}

juce::String resolveLegacyHihatSourcePath(const std::vector<SourceItem>& sourceItems, const juce::String& sourcePath, const juce::String& articulationSlotCode) {
    const int expressionStartIdx = articulationSlotCode.indexOfChar('[');
    const int expressionEndIdx = articulationSlotCode.indexOfChar(expressionStartIdx + 1, ']');
    if (expressionStartIdx < 0 || expressionEndIdx <= expressionStartIdx + 1) return sourcePath;

    juce::StringArray sourceParts;
    sourceParts.addTokens(sourcePath.trim(), "/", "");
    sourceParts.removeEmptyStrings();
    if (sourceParts.size() < 3) return sourcePath;

    const auto libraryArticulation = ArticulationNames::baseArticulationName(sourceParts[2]);
    const auto articulationSlotExpression = articulationSlotCode.substring(expressionStartIdx + 1, expressionEndIdx).trim();
    if (libraryArticulation.isEmpty() || articulationSlotExpression.isEmpty()) return sourcePath;

    const auto expressionSourcePath = sourceParts[0] + "/" + sourceParts[1] + "/" + libraryArticulation + "[" + articulationSlotExpression + "]";
    for (const auto& sourceItem : sourceItems) {
        if (sourceItem.fullPath().equalsIgnoreCase(expressionSourcePath) || sourceItem.rawFullPath().equalsIgnoreCase(expressionSourcePath)) return sourceItem.fullPath();
    }

    return sourcePath;
}

std::unique_ptr<juce::Slider> createParamSlider(
    double minVal, 
    double maxVal, 
    double interval,
    double initialValue,
    double defaultValue,
    double wheelStep,
    double fineWheelStep,
    int textBoxWidth, 
    int textBoxHeight,
    std::function<juce::String(double)> textFromValueFn) {

    auto slider = std::make_unique<ParameterSlider>();
    slider->setSliderStyle(juce::Slider::LinearHorizontal);
    slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, textBoxWidth, textBoxHeight);
    slider->setRange(minVal, maxVal, interval);
    slider->setInteraction(defaultValue, wheelStep, fineWheelStep);
    
    if (textFromValueFn) {
        slider->textFromValueFunction = textFromValueFn;
    }
    
    slider->setValue(initialValue, juce::dontSendNotification);
    slider->updateText();
    
    return slider;
}

std::unique_ptr<juce::Slider> createCompactPot(
    double minVal, 
    double maxVal, 
    double interval,
    double initialValue,
    double defaultValue,
    double wheelStep,
    double fineWheelStep,
    std::function<juce::String(double)> textFromValueFn
) {

    auto pot = std::make_unique<ParameterSlider>();
    configureRotarySlider(*pot, juce::Slider::TextBoxBelow, UITheme::Sizes::compactPotTextBoxW, UITheme::Sizes::compactPotTextBoxH);
    pot->setRange(minVal, maxVal, interval);
    pot->setInteraction(defaultValue, wheelStep, fineWheelStep);

    if (textFromValueFn) pot->textFromValueFunction = textFromValueFn;

    pot->setValue(initialValue, juce::dontSendNotification);
    pot->updateText();

    return pot;
}

}  // namespace UIHelpers
