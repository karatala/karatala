#include "EngineSettingsPanel.h"
#include "../Engine/DrumKitEngine.h"
#include "UIHelpers.h"

namespace {

ParameterPot::LayoutOptions makeSettingsPotLayout(bool isInteger) {
    ParameterPot::LayoutOptions options;
    options.valueDisplay = ParameterPot::ValueDisplay::TextBoxBelow;
    options.textBoxWidth = isInteger ? UITheme::Sizes::settingsIntTextBoxW : UITheme::Sizes::settingsTextBoxW;
    options.textBoxHeight = UITheme::Sizes::settingsTextBoxH;
    return options;
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

    this->hihatGroupLabel.setText("Hi-Hat / Polyphony", juce::dontSendNotification);
    UIHelpers::configureSectionLabel(this->hihatGroupLabel);
    this->contentComponent.addAndMakeVisible(this->hihatGroupLabel);

    this->uiGridGroupLabel.setText("UI / Grid", juce::dontSendNotification);
    UIHelpers::configureSectionLabel(this->uiGridGroupLabel);
    this->contentComponent.addAndMakeVisible(this->uiGridGroupLabel);

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
    this->addPot("Poly Xfade", "Crossfade time when same zone re-triggers. Lower = harder choke, higher = smoother overlap.",
                 &settings.polyphonyCrossfadeTimeoutSec, 0.01f, 2.0f, 0.01f);
    this->addPot("Pedal Xfade", "Crossfade time for hi-hat pedal transitions. Lower = tighter response, higher = smoother.",
                 &settings.hihatPedalCrossfadeSec, 0.01f, 1.0f, 0.01f);
    this->addPot("Pedal Min", "Min remaining audio before pedal can replace voice. Prevents audible cuts on fast pedaling.",
                 &settings.hihatPedalMinRemainingSec, 0.01f, 0.5f, 0.01f);
    this->addPot("Cross-Art dB", "dB margin for cross-articulation hi-hat choke. Higher = more forgiving, lower = stricter choking.",
                 &settings.hihatCrossArtChokeDbMargin, 0.0f, 20.0f, 0.5f);

    // ── UI / Grid ──
    this->addPot("Columns", "Grid columns. 0 - auto detect from the active kit layout.", &settings.padGridColumns, 0, 12);
    this->addPot("Rows", "Grid rows. 0 - auto detect from the active kit layout.", &settings.padGridRows, 0, 8);
    this->addPot("Gap", "", &settings.padGap, 0, 8);

    this->loadFromEngine();
}

EngineSettingsPanel::~EngineSettingsPanel() {
    this->flushPendingSettingsSave();
}

void EngineSettingsPanel::addPot(const juce::String& name, const juce::String& tooltip, float* target, float minVal, float maxVal, float interval) {
    this->addPotCommon(name,
                       tooltip,
                       target,
                       nullptr,
                       static_cast<double>(minVal),
                       static_cast<double>(maxVal),
                       static_cast<double>(interval),
                       false);
}

void EngineSettingsPanel::addPot(const juce::String& name, const juce::String& tooltip, int* target, int minVal, int maxVal) {
    this->addPotCommon(name,
                       tooltip,
                       nullptr,
                       target,
                       static_cast<double>(minVal),
                       static_cast<double>(maxVal),
                       1.0,
                       true);
}

void EngineSettingsPanel::addPotCommon(const juce::String& name,
                                       const juce::String& tooltip,
                                       float* target,
                                       int* intTarget,
                                       double minVal,
                                       double maxVal,
                                       double interval,
                                       bool isInteger) {
    auto entry = std::make_unique<PotEntry>();
    entry->target = target;
    entry->intTarget = intTarget;
    entry->defaultValue = entry->target != nullptr
        ? static_cast<double>(*entry->target)
        : static_cast<double>(*entry->intTarget);

    entry->parameterPot = std::make_unique<ParameterPot>(name,
                                                         minVal,
                                                         maxVal,
                                                         interval,
                                                         entry->defaultValue,
                                                         nullptr,
                                                         makeSettingsPotLayout(isInteger));
    entry->parameterPot->setTooltipText(tooltip);
    entry->parameterPot->onValueChange = [this] { this->onSettingChanged(); };
    entry->parameterPot->onDragEnd = [this] { this->flushPendingSettingsSave(); };
    this->contentComponent.addAndMakeVisible(*entry->parameterPot);

    UIHelpers::configureControlButton(entry->resetButton,
                                      juce::TextButton::ConnectedOnLeft | juce::TextButton::ConnectedOnRight);
    entry->resetButton.onClick = [entry = entry.get()] {
        entry->parameterPot->setValue(entry->defaultValue, juce::sendNotificationSync);
    };
    this->contentComponent.addAndMakeVisible(entry->resetButton);

    this->pots.push_back(std::move(entry));
}

void EngineSettingsPanel::loadFromEngine() {
    for (auto& entry : this->pots) {
        if (entry->target != nullptr)
            entry->parameterPot->setValue(static_cast<double>(*entry->target), juce::dontSendNotification);
        else if (entry->intTarget != nullptr)
            entry->parameterPot->setValue(static_cast<double>(*entry->intTarget), juce::dontSendNotification);
    }
}

void EngineSettingsPanel::onSettingChanged() {
    bool gridChanged = false;
    for (auto& entry : this->pots) {
        if (entry->target != nullptr)
            *entry->target = static_cast<float>(entry->parameterPot->getValue());
        else if (entry->intTarget != nullptr) {
            int oldVal = *entry->intTarget;
            *entry->intTarget = static_cast<int>(entry->parameterPot->getValue());
            if (oldVal != *entry->intTarget)
                gridChanged = true;
        }
    }

    if (gridChanged && this->onGridSizeChanged)
        this->onGridSizeChanged();

    this->queueSettingsSave();
}

void EngineSettingsPanel::timerCallback() {
    this->flushPendingSettingsSave();
}

void EngineSettingsPanel::queueSettingsSave() {
    this->startTimer(settingsSaveDebounceMs);
}

void EngineSettingsPanel::flushPendingSettingsSave() {
    if (!this->isTimerRunning())
        return;

    this->stopTimer();
    this->engine.saveSettings();
}

void EngineSettingsPanel::resized() {
    this->layoutViewportToBounds();

    const int contentWidth = this->viewport.getWidth() - this->viewport.getScrollBarThickness();

    // Pot grid layout: each pot cell is potCellW x potCellH
    const int potCellW = 80;
    const int potCellH = 90; // pot (50) + label (14) + textbox (14) + reset (12)
    const int groupLabelHeight = 22;
    const int gap = 4;

    const int availableWidth = juce::jmax(contentWidth, 400) - 16; // 8px padding each side
    const int potsPerRow = juce::jmax(1, availableWidth / potCellW);

    // Calculate group row counts
    auto groupRows = [&](int count) { return (count + potsPerRow - 1) / potsPerRow; };

    int totalHeight = 8; // top padding
    totalHeight += groupLabelHeight + gap + groupRows(6) * potCellH + gap; // choke (6 pots)
    totalHeight += groupLabelHeight + gap + groupRows(6) * potCellH + gap; // tail (6 pots)
    totalHeight += groupLabelHeight + gap + groupRows(4) * potCellH + gap; // hihat (4 pots)
    totalHeight += groupLabelHeight + gap + groupRows(3) * potCellH + gap; // grid (3 pots)
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

    placeGroupLabel(this->chokeGroupLabel);
    placePotGrid(0, 6);

    placeGroupLabel(this->tailGroupLabel);
    placePotGrid(6, 6);

    placeGroupLabel(this->hihatGroupLabel);
    placePotGrid(12, 4);

    placeGroupLabel(this->uiGridGroupLabel);
    placePotGrid(16, 3);
}
