#include "EngineSettingsPanel.h"
#include "../Engine/DrumKitEngine.h"
#include "UIHelpers.h"
#include <cmath>

namespace {

ParameterPot::LayoutOptions makeSettingsPotLayout(bool isInteger) {
    ParameterPot::LayoutOptions options;
    options.valueDisplay = ParameterPot::ValueDisplay::TextBoxBelow;
    options.textBoxWidth = isInteger ? UITheme::Sizes::settingsIntTextBoxW : UITheme::Sizes::settingsTextBoxW;
    options.textBoxHeight = UITheme::Sizes::settingsTextBoxH;
    return options;
}

juce::String millisecondsText(double seconds) {
    return juce::String(seconds * 1000.0, 0) + " ms";
}

double millisecondsValue(const juce::String& text) {
    return text.getDoubleValue() / 1000.0;
}

juce::String percentageText(double value) {
    return juce::String(value * 100.0, 0) + "%";
}

double percentageValue(const juce::String& text) {
    return text.getDoubleValue() / 100.0;
}

} // namespace

EngineSettingsPanel::EngineSettingsPanel(DrumKitEngine& engine)
    : engine(engine) {
    auto& settings = this->engine.engineSettings;

    // Group labels
    this->chokeGroupLabel.setText("Aftertouch Choke", juce::dontSendNotification);
    UIHelpers::configureSectionLabel(this->chokeGroupLabel);
    this->contentComponent.addAndMakeVisible(this->chokeGroupLabel);

    this->tailGroupLabel.setText("Tail Shortening", juce::dontSendNotification);
    UIHelpers::configureSectionLabel(this->tailGroupLabel);
    this->contentComponent.addAndMakeVisible(this->tailGroupLabel);

    this->hihatGroupLabel.setText("Hi-Hat Transitions / Polyphony", juce::dontSendNotification);
    UIHelpers::configureSectionLabel(this->hihatGroupLabel);
    this->contentComponent.addAndMakeVisible(this->hihatGroupLabel);

    this->uiGridGroupLabel.setText("UI / Grid", juce::dontSendNotification);
    UIHelpers::configureSectionLabel(this->uiGridGroupLabel);
    this->contentComponent.addAndMakeVisible(this->uiGridGroupLabel);

    this->outputGroupLabel.setText("Advanced / Output Layout", juce::dontSendNotification);
    UIHelpers::configureSectionLabel(this->outputGroupLabel);
    this->contentComponent.addAndMakeVisible(this->outputGroupLabel);
    UIHelpers::configureComboBox(this->micLayoutCombo, UIHelpers::ComboBoxStyle::LayoutSelector);
    this->contentComponent.addAndMakeVisible(this->micLayoutCombo);
    UIHelpers::configureControlButton(this->saveMicLayoutButton);
    this->contentComponent.addAndMakeVisible(this->saveMicLayoutButton);
    this->micLayoutWarningLabel.setText(
        "This machine-wide setting changes output bus names only for plugin instances created afterward. The current instance cannot be changed.",
        juce::dontSendNotification);
    this->micLayoutWarningLabel.setFont(UITheme::Fonts::small());
    this->micLayoutWarningLabel.setColour(juce::Label::textColourId, UITheme::Colours::textAccent);
    this->micLayoutWarningLabel.setJustificationType(juce::Justification::topLeft);
    this->contentComponent.addAndMakeVisible(this->micLayoutWarningLabel);
    this->saveMicLayoutButton.onClick = [this] {
        const int idx = this->micLayoutCombo.getSelectedId() - 1;
        if (idx >= 0 && idx < static_cast<int>(this->micLayouts.size()))
            this->engine.setGlobalMicLayoutId(this->micLayouts[static_cast<size_t>(idx)].id);
    };

    // ── Aftertouch Choke ──
    this->addPot("Max Duck", "How much to duck the volume on aftertouch press. Higher = more muting, lower = subtler effect.",
                 &settings.chokeMaxDuckDepth, 0.0f, 1.0f, 0.01f);
    this->addPot("Duck Time", "How fast the volume ducks when you press. Lower = instant choke, higher = gradual fade.",
                 &settings.chokeDuckTimeSec, 0.01f, 1.0f, 0.01f);
    this->addPot("Release Time", "How fast volume recovers when you release aftertouch. Lower = snappy, higher = smooth.",
                 &settings.chokeReleaseTimeSec, 0.01f, 0.5f, 0.01f);
    this->addPot("Mute Tail", "Duration of the fade-out tail after full mute. Lower = harder cut, higher = natural ring-out.",
                 &settings.chokeMuteTailSec, 0.01f, 0.5f, 0.01f);
    this->addPot("Release Boost", "Extra volume bump on aftertouch release. Adds 'pop' to choked hits.",
                 &settings.chokeReleaseBoost, 0.0f, 0.5f, 0.01f);
    this->addPot("Mute Threshold", "Aftertouch pressure needed to trigger full mute (as ratio of max). Lower = easier to mute.",
                 &settings.chokeMuteThresholdRatio, 0.0f, 1.0f, 0.01f);
    this->addPot("Min Pressure", "Poly-aftertouch values below this are treated as release, filtering low-level sensor noise.",
                 &settings.polyAftertouchMinPressure, 1, 20, 10);

    // ── Tail Shortening ──
    this->addPot("Load Thresh", "Voice load percentage before tail shortening kicks in. Lower = starts cutting sooner when busy.",
                 &settings.tailShortenLoadThreshold, 0.0f, 1.0f, 0.01f);
    this->addPot("Age Mult", "How much voice age affects shortening priority. Higher = older voices get cut faster.",
                 &settings.tailShortenAgeMultiplier, 0.0f, 0.2f, 0.001f);
    this->addPot("Amp Protect", "Loud voice protection. Voices above this gain level are protected from cutting. Higher = less protection.",
                 &settings.tailShortenAmpProtect, 0.0f, 1.0f, 0.01f);
    this->addPot("Min Factor", "Minimum shortening amount at full load. Lower = more aggressive cutting when CPU is maxed.",
                 &settings.tailShortenMinFactor, 0.0f, 1.0f, 0.01f);
    this->addPot("Voice Clamp", "Per-voice minimum shortening floor. Lower = allows more aggressive per-voice fading.",
                 &settings.voiceTailShortenMinClamp, 0.0f, 1.0f, 0.01f);
    this->addPot("Cull dB", "Cull voices quieter than this vs the new hit (dB). Lower = keep more old voices, higher = cull more.",
                 &settings.voiceCullThresholdDb, 0.0f, 30.0f, 0.5f);

    // ── Hi-Hat / Polyphony ──
    this->addPot("Open Xfade", "Transition time when opening a ringing hi-hat. Higher values are smoother; lower values react faster.",
                 &settings.hihatOpenCrossfadeSec, 0.005f, 0.250f, 0.001f, millisecondsText, millisecondsValue, 0.060);
    this->addPot("Close Xfade", "Transition time when closing a ringing hi-hat. Lower values produce a tighter choke.",
                 &settings.hihatCloseCrossfadeSec, 0.002f, 0.150f, 0.001f, millisecondsText, millisecondsValue, 0.020);
    this->addPot("CC Smooth", "Combines rapid pedal messages before changing the ringing tail. Higher values reduce transition activity but add pedal latency.",
                 &settings.hihatControllerSmoothingSec, 0.0f, 0.100f, 0.001f, millisecondsText, millisecondsValue, 0.015);
    this->addPot("Hysteresis", "Prevents openness from flickering near articulation boundaries. Increase this if the pedal jitters between adjacent sounds.",
                 &settings.hihatBoundaryHysteresisCc, 0, 8, 2);
    this->addPot("Tail Match", "How strongly the replacement follows the current tail loudness. Zero uses a simple fallback; 100% uses decay-envelope matching.",
                 &settings.hihatTailMatchAmount, 0.0f, 1.0f, 0.01f, percentageText, percentageValue, 0.75);
    this->addPot("Match Bias", "Loudness offset applied to matched replacement tails. Negative values make transitions more restrained.",
                 &settings.hihatTailMatchBiasDb, -12.0f, 6.0f, 0.5f, nullptr, nullptr, -3.0);
    this->addPot("Poly Xfade", "Crossfade time when same zone re-triggers. Lower = harder choke, higher = smoother overlap.",
                 &settings.polyphonyCrossfadeTimeoutSec, 0.01f, 2.0f, 0.01f);
    this->addPot("Hit Choke dB", "dB margin for struck-hit cross-articulation choke. Higher values make quiet hits more likely to choke a ringing hat.",
                 &settings.hihatHitChokeDbMargin, 0.0f, 20.0f, 0.5f, nullptr, nullptr, 3.0);
    this->addPot("Attack Guard", "Protects the initial stick attack from being replaced by pedal movement.",
                 &settings.hihatTransitionAttackGuardSec, 0.0f, 0.100f, 0.001f, millisecondsText, millisecondsValue, 0.025);
    this->addPot("Min Remaining", "Skips replacement samples that do not have enough audio left after matching the current decay level.",
                 &settings.hihatTransitionMinRemainingSec, 0.010f, 0.500f, 0.001f, millisecondsText, millisecondsValue, 0.050);
    this->addPot("Silence Floor", "Very quiet historical tails below this level do not create replacement voices.",
                 &settings.hihatTransitionSilenceFloorDb, -90.0f, -36.0f, 1.0f, nullptr, nullptr, -60.0);

    this->addChoice("Missing Data", "Keeps the original tail or uses a simple crossfade when decay metadata is unavailable.",
                    { "Keep Tail", "Simple Fade" }, 1,
                    [&settings] { return static_cast<int>(settings.hihatMissingDecayPolicy.load()) + 1; },
                    [&settings](int selectedId) {
                        settings.hihatMissingDecayPolicy.store(static_cast<HiHatMissingDecayPolicy>(selectedId - 1));
                    });

    // ── UI / Grid ──
    this->addPot("Columns", "Grid columns. 0 - auto detect from the active kit layout.", &settings.padGridColumns, 0, 12);
    this->addPot("Rows", "Grid rows. 0 - auto detect from the active kit layout.", &settings.padGridRows, 0, 8);
    this->addPot("Gap", "", &settings.padGap, 0, 8);

    this->rebuildMicLayouts();
    this->loadFromEngine();
}

EngineSettingsPanel::~EngineSettingsPanel() {
    this->flushPendingSettingsSave();
}

void EngineSettingsPanel::addPot(const juce::String& name,
                                 const juce::String& tooltip,
                                 RealtimeSetting<float>* target,
                                 float minVal,
                                 float maxVal,
                                 float interval,
                                 std::function<juce::String(double)> textFromValueFn,
                                 std::function<double(const juce::String&)> valueFromTextFn,
                                 double defaultValue) {
    this->addPotCommon(name, tooltip, target, nullptr,
                       static_cast<double>(minVal), static_cast<double>(maxVal), static_cast<double>(interval), false,
                       std::move(textFromValueFn), std::move(valueFromTextFn), defaultValue);
}

void EngineSettingsPanel::addPot(const juce::String& name,
                                 const juce::String& tooltip,
                                 RealtimeSetting<int>* target,
                                 int minVal,
                                 int maxVal,
                                 int defaultValue) {
    const double defaultOverride = defaultValue == std::numeric_limits<int>::min()
        ? std::numeric_limits<double>::quiet_NaN()
        : static_cast<double>(defaultValue);
    this->addPotCommon(name, tooltip, nullptr, target,
                       static_cast<double>(minVal), static_cast<double>(maxVal), 1.0, true,
                       nullptr, nullptr, defaultOverride);
}

void EngineSettingsPanel::addPotCommon(const juce::String& name,
                                       const juce::String& tooltip,
                                       RealtimeSetting<float>* target,
                                       RealtimeSetting<int>* intTarget,
                                       double minVal,
                                       double maxVal,
                                       double interval,
                                       bool isInteger,
                                       std::function<juce::String(double)> textFromValueFn,
                                       std::function<double(const juce::String&)> valueFromTextFn,
                                       double defaultValue) {
    auto entry = std::make_unique<PotEntry>();
    entry->target = target;
    entry->intTarget = intTarget;
    const double currentValue = entry->target != nullptr
        ? static_cast<double>(entry->target->load())
        : static_cast<double>(entry->intTarget->load());
    entry->defaultValue = std::isnan(defaultValue) ? currentValue : defaultValue;
    entry->parameterPot = std::make_unique<ParameterPot>(
        name, minVal, maxVal, interval, currentValue,
        std::move(textFromValueFn), makeSettingsPotLayout(isInteger), std::move(valueFromTextFn));
    entry->parameterPot->setInteraction(entry->defaultValue, interval);
    entry->parameterPot->setTooltipText(tooltip);
    entry->parameterPot->onValueChange = [this] { this->onSettingChanged(); };
    entry->parameterPot->onDragEnd = [this] { this->flushPendingSettingsSave(); };
    this->contentComponent.addAndMakeVisible(*entry->parameterPot);

    UIHelpers::configureControlButton(entry->resetButton, juce::TextButton::ConnectedOnLeft | juce::TextButton::ConnectedOnRight);
    entry->resetButton.onClick = [entry = entry.get()] {
        entry->parameterPot->setValue(entry->defaultValue, juce::sendNotificationSync);
    };
    this->contentComponent.addAndMakeVisible(entry->resetButton);

    this->pots.push_back(std::move(entry));
}

void EngineSettingsPanel::addChoice(const juce::String& name,
                                    const juce::String& tooltip,
                                    const juce::StringArray& items,
                                    int defaultSelectedId,
                                    std::function<int()> loadSelectedId,
                                    std::function<void(int)> storeSelectedId) {
    auto entry = std::make_unique<ChoiceEntry>();
    entry->label.setText(name, juce::dontSendNotification);
    entry->label.setTooltip(tooltip);
    UIHelpers::configureCaptionLabel(entry->label);
    this->contentComponent.addAndMakeVisible(entry->label);

    UIHelpers::configureComboBox(entry->comboBox);
    entry->comboBox.setTooltip(tooltip);
    entry->comboBox.addItemList(items, 1);
    entry->loadSelectedId = std::move(loadSelectedId);
    entry->storeSelectedId = std::move(storeSelectedId);
    entry->defaultSelectedId = defaultSelectedId;
    entry->comboBox.onChange = [this, entryPtr = entry.get()] {
        const int selectedId = entryPtr->comboBox.getSelectedId();
        if (selectedId > 0)
            entryPtr->storeSelectedId(selectedId);
        this->queueSettingsSave();
    };
    this->contentComponent.addAndMakeVisible(entry->comboBox);

    UIHelpers::configureControlButton(entry->resetButton, juce::TextButton::ConnectedOnLeft | juce::TextButton::ConnectedOnRight);
    entry->resetButton.onClick = [entryPtr = entry.get()] {
        entryPtr->comboBox.setSelectedId(entryPtr->defaultSelectedId, juce::sendNotificationSync);
    };
    this->contentComponent.addAndMakeVisible(entry->resetButton);
    this->choices.push_back(std::move(entry));
}

void EngineSettingsPanel::loadFromEngine() {
    for (auto& entry : this->pots) {
        if (entry->target != nullptr)
            entry->parameterPot->setValue(static_cast<double>(entry->target->load()), juce::dontSendNotification);
        else if (entry->intTarget != nullptr)
            entry->parameterPot->setValue(static_cast<double>(entry->intTarget->load()), juce::dontSendNotification);
    }
    for (auto& entry : this->choices)
        entry->comboBox.setSelectedId(entry->loadSelectedId(), juce::dontSendNotification);
    this->rebuildMicLayouts();
}

void EngineSettingsPanel::rebuildMicLayouts() {
    this->micLayouts.clear();
    this->micLayoutCombo.clear(juce::dontSendNotification);
    this->micLayouts.push_back(this->engine.getMicLayoutManager()->getDefault());
    for (const auto& layout : this->engine.getMicLayoutManager()->getLayouts())
        this->micLayouts.push_back(layout);

    int selectedId = 1;
    for (size_t idx = 0; idx < this->micLayouts.size(); ++idx) {
        const auto& layout = this->micLayouts[idx];
        this->micLayoutCombo.addItem(layout.name.isNotEmpty() ? layout.name : layout.id, static_cast<int>(idx + 1));
        if (layout.id == this->engine.getGlobalMicLayoutId())
            selectedId = static_cast<int>(idx + 1);
    }
    this->micLayoutCombo.setSelectedId(selectedId, juce::dontSendNotification);
}

void EngineSettingsPanel::onSettingChanged() {
    bool gridChanged = false;
    for (auto& entry : this->pots) {
        if (entry->target != nullptr)
            entry->target->store(static_cast<float>(entry->parameterPot->getValue()));
        else if (entry->intTarget != nullptr) {
            const int oldValue = entry->intTarget->load();
            const int newValue = static_cast<int>(entry->parameterPot->getValue());
            entry->intTarget->store(newValue);
            if (oldValue != newValue)
                gridChanged = true;
        }
    }
    if (gridChanged && this->onGridSizeChanged) this->onGridSizeChanged();
    this->queueSettingsSave();
}

void EngineSettingsPanel::timerCallback() { this->flushPendingSettingsSave(); }

void EngineSettingsPanel::queueSettingsSave() { this->startTimer(settingsSaveDebounceMs); }

void EngineSettingsPanel::flushPendingSettingsSave() {
    if (!this->isTimerRunning()) return;
    this->stopTimer();
    this->engine.saveSettings();
}

void EngineSettingsPanel::resized() {
    this->layoutViewportToBounds();

    const int contentWidth = this->viewport.getWidth() - this->viewport.getScrollBarThickness();

    // Pot grid layout: each pot cell is potCellW x potCellH
    const int potCellW = 80;
    const int potCellH = 90; // pot (50) + label (14) + textbox (14) + reset (12)
    const int choiceCellW = 150;
    const int choiceCellH = 54;
    const int groupLabelHeight = 22;
    const int gap = 4;

    const int availableWidth = juce::jmax(contentWidth, 400) - 16; // 8px padding each side
    const int potsPerRow = juce::jmax(1, availableWidth / potCellW);
    const int choicesPerRow = juce::jmax(1, availableWidth / choiceCellW);

    // Calculate group row counts
    auto groupRows = [&](int count) { return (count + potsPerRow - 1) / potsPerRow; };

    int totalHeight = 8; // top padding
    totalHeight += groupLabelHeight + gap + groupRows(7) * potCellH + gap; // choke (7 pots)
    totalHeight += groupLabelHeight + gap + groupRows(6) * potCellH + gap; // tail (6 pots)
    const int choiceRows = (static_cast<int>(this->choices.size()) + choicesPerRow - 1) / choicesPerRow;
    totalHeight += groupLabelHeight + gap + groupRows(11) * potCellH + choiceRows * choiceCellH + gap; // hihat
    totalHeight += groupLabelHeight + gap + groupRows(3) * potCellH + gap; // grid (3 pots)
    totalHeight += groupLabelHeight + 64 + gap; // output layout
    totalHeight += 8; // bottom padding

    this->contentComponent.setBounds(0, 0, juce::jmax(contentWidth, 400), totalHeight);

    auto area = this->contentComponent.getLocalBounds().reduced(8);

    auto placeGroupLabel = [&](juce::Label& groupLabel) {
        groupLabel.setBounds(area.removeFromTop(groupLabelHeight));
        area.removeFromTop(gap);
    };

    auto placePotGrid = [&](int startIdx, int count) {
        int col = 0;
        int rowStartY = area.getY();
        for (int idx = startIdx; idx < startIdx + count && idx < static_cast<int>(this->pots.size()); ++idx) {
            int cellX = area.getX() + col * potCellW;
            int cellY = rowStartY;

            auto& entry = this->pots[static_cast<size_t>(idx)];

            entry->parameterPot->setBounds(cellX, cellY, potCellW, potCellH - 2);
            // Reset button at bottom-right corner
            entry->resetButton.setBounds(cellX + potCellW - 18, cellY + potCellH - 16, 16, 14);

            ++col;
            if (col >= potsPerRow) {
                col = 0;
                rowStartY += potCellH;
            }
        }
        int rowsUsed = groupRows(count);
        area.removeFromTop(rowsUsed * potCellH + gap);
    };

    auto placeChoiceGrid = [&] {
        int column = 0;
        int rowStartY = area.getY();
        for (auto& entry : this->choices) {
            const int cellX = area.getX() + column * choiceCellW;
            entry->label.setBounds(cellX, rowStartY, choiceCellW - 4, 14);
            entry->comboBox.setBounds(cellX, rowStartY + 16, choiceCellW - 24, 28);
            entry->resetButton.setBounds(cellX + choiceCellW - 20, rowStartY + 23, 16, 14);

            ++column;
            if (column >= choicesPerRow) {
                column = 0;
                rowStartY += choiceCellH;
            }
        }
        area.removeFromTop(choiceRows * choiceCellH + gap);
    };

    placeGroupLabel(this->chokeGroupLabel);
    placePotGrid(0, 7);

    placeGroupLabel(this->tailGroupLabel);
    placePotGrid(7, 6);

    placeGroupLabel(this->hihatGroupLabel);
    placePotGrid(13, 11);
    placeChoiceGrid();

    placeGroupLabel(this->uiGridGroupLabel);
    placePotGrid(24, 3);

    placeGroupLabel(this->outputGroupLabel);
    auto outputRow = area.removeFromTop(26);
    this->micLayoutCombo.setBounds(outputRow.removeFromLeft(260).reduced(0, 2));
    outputRow.removeFromLeft(6);
    this->saveMicLayoutButton.setBounds(outputRow.removeFromLeft(180).reduced(0, 2));
    this->micLayoutWarningLabel.setBounds(area.removeFromTop(38));
}
