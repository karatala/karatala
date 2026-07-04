#include "UIHelpers.h"

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

void configureToggleButton(juce::TextButton& button, int connectedEdges) {
    button.setClickingTogglesState(true);
    button.setColour(juce::TextButton::buttonColourId, UITheme::Colours::bgControl);
    button.setColour(juce::TextButton::buttonOnColourId, UITheme::Colours::accentWarning);
    button.setColour(juce::TextButton::textColourOffId, UITheme::Colours::textMuted);
    button.setColour(juce::TextButton::textColourOnId, UITheme::Colours::textPrimary);
    button.setConnectedEdges(connectedEdges);
}

void configureTableRowLabel(juce::Label& label,
                            juce::Colour colour,
                            juce::Justification justification) {
    label.setFont(UITheme::Fonts::tableSmall());
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

juce::String sentinelValueText(double value) {
    return value < 0.0 ? "--" : juce::String(static_cast<int>(value));
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
        juce::String kitShort = kitPtr->meta.name;
        if (kitShort.isEmpty()) kitShort = kitPtr->meta.codeRaw;

        std::vector<const KitLib::InstrumentSpec*> instruments;
        for (const auto& [code, instPtr] : kitPtr->getInstrumentSpecsMap())
            instruments.push_back(instPtr.get());
        std::sort(instruments.begin(), instruments.end(), [](const auto* a, const auto* b) {
            return a->meta.codeRaw < b->meta.codeRaw;
        });

        for (const auto* inst : instruments) {
            if (inst->articulationSpecs.empty()) {
                SourceItem item;
                item.kitCodeRaw = kitPtr->meta.codeRaw;
                item.kitDisplayName = kitShort;
                item.instCode = inst->meta.codeRaw;
                item.instType = inst->meta.type;
                addUniqueSourceItem(std::move(item));
            } else {
                for (const auto& artPtr : inst->articulationSpecs) {
                    SourceItem item;
                    item.kitCodeRaw = kitPtr->meta.codeRaw;
                    item.kitDisplayName = kitShort;
                    item.instCode = inst->meta.codeRaw;
                    item.artCode = artPtr->codeRaw;
                    item.instType = inst->meta.type;
                    addUniqueSourceItem(std::move(item));
                }
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
        auto haystack = sourceItems[static_cast<size_t>(idx)].fullPath().toLowerCase();
        if (haystack == needle)
            return idx;
    }

    juce::StringArray needleParts;
    needleParts.addTokens(needle, "/", "");
    needleParts.removeEmptyStrings();
    if (needleParts.size() >= 3) {
        const auto needleBaseArt = needleParts[2].upToFirstOccurrenceOf("[", false, false);
        for (int idx = 0; idx < static_cast<int>(sourceItems.size()); ++idx) {
            const auto& item = sourceItems[static_cast<size_t>(idx)];
            if (item.kitCodeRaw.toLowerCase() != needleParts[0]) continue;
            if (item.instCode.toLowerCase() != needleParts[1]) continue;
            if (item.artCode.toLowerCase().upToFirstOccurrenceOf("[", false, false) == needleBaseArt) return idx;
        }
    }
    return -1;
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
