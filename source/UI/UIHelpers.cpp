#include "UITypes.h"

#include <unordered_set>

namespace UIHelpers {

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
                    item.artCode = artPtr->codeRaw.upToFirstOccurrenceOf("[", false, false);
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
    return -1;
}

std::unique_ptr<juce::Slider> createParamSlider(
    double minVal, 
    double maxVal, 
    double interval,
    double initialValue,
    int textBoxWidth, 
    int textBoxHeight,
    std::function<juce::String(double)> textFromValueFn) {

    auto slider = std::make_unique<juce::Slider>();
    slider->setSliderStyle(juce::Slider::LinearHorizontal);
    slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, textBoxWidth, textBoxHeight);
    slider->setRange(minVal, maxVal, interval);
    
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
    std::function<juce::String(double)> textFromValueFn) {

    auto pot = std::make_unique<juce::Slider>();
    pot->setSliderStyle(juce::Slider::RotaryVerticalDrag);
    pot->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 38, 12);
    pot->setRange(minVal, maxVal, interval);
    pot->setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::red);
    pot->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff444444));
    pot->setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffcccccc));
    pot->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    if (textFromValueFn)
        pot->textFromValueFunction = textFromValueFn;

    pot->setValue(initialValue, juce::dontSendNotification);
    pot->updateText();

    return pot;
}

}  // namespace UIHelpers
