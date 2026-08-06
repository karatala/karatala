#include "EngineSettingsPanel.h"
#include "../Engine/DrumKitEngine.h"
#include "UIComponentStyle.h"
#include <cmath>

static juce::String millisecondsText(double seconds) { return juce::String(seconds * 1000.0, 0) + " ms"; }
static double millisecondsValue(const juce::String& text) { return text.getDoubleValue() / 1000.0; }
static juce::String percentageText(double value) { return juce::String(value * 100.0, 0) + "%"; }
static double percentageValue(const juce::String& text) { return text.getDoubleValue() / 100.0; }

EngineSettingsPanel::EngineSettingsPanel(DrumKitEngine& drumKitEngine)
    : engine(drumKitEngine) {
    auto& settings = this->engine.engineSettings;

    // Group labels
    this->generalGroupLabel.setText("General", juce::dontSendNotification);
    UIComponentStyle::configureSectionLabel(this->generalGroupLabel);
    this->contentComponent.addAndMakeVisible(this->generalGroupLabel);

    this->libraryPathLabel.setText("Sample library", juce::dontSendNotification);
    UIComponentStyle::configureCaptionLabel(this->libraryPathLabel, juce::Justification::centredLeft);
    this->contentComponent.addAndMakeVisible(this->libraryPathLabel);
    UIComponentStyle::configureMonoTextEditor(this->libraryPathEditor, UITheme::Colours::textSecondary, false);
    UIComponentStyle::configureBorderlessTextEditor(this->libraryPathEditor);
    this->libraryPathEditor.setReadOnly(true);
    this->libraryPathEditor.setScrollbarsShown(false);
    this->contentComponent.addAndMakeVisible(this->libraryPathEditor);
    UIComponentStyle::configureControlButton(this->browseLibraryButton);
    this->browseLibraryButton.onClick = [this] { this->chooseLibraryPath(); };
    this->contentComponent.addAndMakeVisible(this->browseLibraryButton);

    this->globalVelocityResponseLabel.setText("Global Velocity-to-dB Curve", juce::dontSendNotification);
    UIComponentStyle::configureCaptionLabel(this->globalVelocityResponseLabel, juce::Justification::centredLeft);
    this->contentComponent.addAndMakeVisible(this->globalVelocityResponseLabel);
    UIComponentStyle::configureComboBox(this->globalVelocityResponseCombo);
    this->globalVelocityResponseCombo.onChange = [this] { this->applyGlobalVelocityResponseChoice(); };
    this->contentComponent.addAndMakeVisible(this->globalVelocityResponseCombo);

    this->kitLayoutLabel.setText("Default kit layout", juce::dontSendNotification);
    UIComponentStyle::configureCaptionLabel(this->kitLayoutLabel, juce::Justification::centredLeft);
    this->contentComponent.addAndMakeVisible(this->kitLayoutLabel);
    UIComponentStyle::configureComboBox(this->kitLayoutCombo, UIComponentStyle::ComboBoxStyle::LayoutSelector);
    this->contentComponent.addAndMakeVisible(this->kitLayoutCombo);

    this->micLayoutLabel.setText("Default mic layout", juce::dontSendNotification);
    UIComponentStyle::configureCaptionLabel(this->micLayoutLabel, juce::Justification::centredLeft);
    this->contentComponent.addAndMakeVisible(this->micLayoutLabel);
    UIComponentStyle::configureComboBox(this->micLayoutCombo, UIComponentStyle::ComboBoxStyle::LayoutSelector);
    this->contentComponent.addAndMakeVisible(this->micLayoutCombo);

    UIComponentStyle::configureControlButton(this->saveLayoutDefaultsButton);
    this->saveLayoutDefaultsButton.onClick = [this] { this->saveLayoutDefaults(); };
    this->contentComponent.addAndMakeVisible(this->saveLayoutDefaultsButton);
    this->layoutDefaultsWarningLabel.setText("Layout defaults apply to plugin instances created afterward. The current instance does not change.", juce::dontSendNotification);
    this->layoutDefaultsWarningLabel.setFont(UITheme::Fonts::small());
    this->layoutDefaultsWarningLabel.setColour(juce::Label::textColourId, UITheme::Colours::textAccent);
    this->layoutDefaultsWarningLabel.setJustificationType(juce::Justification::topLeft);
    this->contentComponent.addAndMakeVisible(this->layoutDefaultsWarningLabel);

    const double cacheLimitMegabytes = static_cast<double>(this->engine.getGlobalSampleLoaderCacheLimitMegabytes());
    this->cacheLimitControl = std::make_unique<CompactParameterControl>("Sample cache", 1.0, juce::jmax(65536.0, cacheLimitMegabytes), 1.0, cacheLimitMegabytes, [](double value) {
        return juce::String(static_cast<juce::int64>(value)) + " MB";
    });
    this->cacheLimitControl->setInteraction(1024.0, 64.0, 16.0);
    this->cacheLimitControl->setTooltip("Maximum memory used by the loaded sample cache.");
    this->cacheLimitControl->onDragEnd = [this] {
        this->engine.setGlobalSampleLoaderCacheLimitMegabytes(static_cast<uint64_t>(this->cacheLimitControl->getValue()));
    };
    this->contentComponent.addAndMakeVisible(*this->cacheLimitControl);

    this->chokeGroupLabel.setText("Aftertouch Choke", juce::dontSendNotification);
    UIComponentStyle::configureSectionLabel(this->chokeGroupLabel);
    this->contentComponent.addAndMakeVisible(this->chokeGroupLabel);

    this->tailGroupLabel.setText("Tail Shortening", juce::dontSendNotification);
    UIComponentStyle::configureSectionLabel(this->tailGroupLabel);
    this->contentComponent.addAndMakeVisible(this->tailGroupLabel);

    this->hihatGroupLabel.setText("Hi-Hat Transitions / Polyphony", juce::dontSendNotification);
    UIComponentStyle::configureSectionLabel(this->hihatGroupLabel);
    this->contentComponent.addAndMakeVisible(this->hihatGroupLabel);

    this->sampleSelectionGroupLabel.setText("Floating Sample Selection", juce::dontSendNotification);
    UIComponentStyle::configureSectionLabel(this->sampleSelectionGroupLabel);
    this->contentComponent.addAndMakeVisible(this->sampleSelectionGroupLabel);

    this->uiGridGroupLabel.setText("UI / Grid", juce::dontSendNotification);
    UIComponentStyle::configureSectionLabel(this->uiGridGroupLabel);
    this->contentComponent.addAndMakeVisible(this->uiGridGroupLabel);

    // -- Aftertouch Choke --
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

    // -- Tail Shortening --
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

    // -- Hi-Hat / Polyphony --
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
    this->hihatChoiceCount = static_cast<int>(this->choices.size());

    // -- Floating Sample Selection --
    this->sampleSelectionChoiceStart = static_cast<int>(this->choices.size());
    this->addChoice("Min Velocity Levels", "Floating selection is used only when an articulation has at least this many velocity levels and no more than two hits per level.",
                    { "2", "3", "4", "5" }, 1,
                    [&settings] { return juce::jlimit(2, 5, settings.floatingSelectionMinimumVelocityLevels.load()) - 1; },
                    [&settings](int selectedId) { settings.floatingSelectionMinimumVelocityLevels.store(selectedId + 1); });
    this->sampleSelectionChoiceCount = static_cast<int>(this->choices.size()) - this->sampleSelectionChoiceStart;

    // -- UI / Grid --
    this->addPot("Columns", "Grid columns. 0 - auto detect from the active kit layout.", &settings.padGridColumns, 0, 12);
    this->addPot("Rows", "Grid rows. 0 - auto detect from the active kit layout.", &settings.padGridRows, 0, 8);
    this->addPot("Gap", "", &settings.padGap, 0, 8);
    this->uiChoiceStart = static_cast<int>(this->choices.size());

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
                       static_cast<double>(minVal), static_cast<double>(maxVal), static_cast<double>(interval),
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
                       static_cast<double>(minVal), static_cast<double>(maxVal), 1.0,
                       nullptr, nullptr, defaultOverride);
}

void EngineSettingsPanel::addPotCommon(const juce::String& name,
                                       const juce::String& tooltip,
                                       RealtimeSetting<float>* target,
                                       RealtimeSetting<int>* intTarget,
                                       double minVal,
                                       double maxVal,
                                       double interval,
                                       std::function<juce::String(double)> textFromValueFn,
                                       std::function<double(const juce::String&)> valueFromTextFn,
                                       double defaultValue) {
    auto entry = std::make_unique<ControlEntry>();
    entry->target = target;
    entry->intTarget = intTarget;
    const double currentValue = entry->target != nullptr
        ? static_cast<double>(entry->target->load())
        : static_cast<double>(entry->intTarget->load());
    entry->defaultValue = std::isnan(defaultValue) ? currentValue : defaultValue;
    entry->parameterControl = std::make_unique<CompactParameterControl>(name, minVal, maxVal, interval, currentValue, std::move(textFromValueFn));
    if (valueFromTextFn) entry->parameterControl->valueFromTextFunction = std::move(valueFromTextFn);
    entry->parameterControl->setInteraction(entry->defaultValue, interval);
    entry->parameterControl->setTooltip(tooltip);
    entry->parameterControl->onValueChange = [this] { this->onSettingChanged(); };
    entry->parameterControl->onDragEnd = [this] { this->flushPendingSettingsSave(); };
    this->contentComponent.addAndMakeVisible(*entry->parameterControl);

    this->controls.push_back(std::move(entry));
}

void EngineSettingsPanel::addChoice(const juce::String& name,
                                    const juce::String& tooltip,
                                    const juce::StringArray& optionLabels,
                                    int defaultSelectedId,
                                    std::function<int()> loadSelectedId,
                                    std::function<void(int)> storeSelectedId) {
    auto entry = std::make_unique<ChoiceEntry>();
    entry->label.setText(name, juce::dontSendNotification);
    entry->label.setTooltip(tooltip);
    UIComponentStyle::configureCaptionLabel(entry->label);
    this->contentComponent.addAndMakeVisible(entry->label);

    UIComponentStyle::configureComboBox(entry->comboBox);
    entry->comboBox.setTooltip(tooltip);
    entry->comboBox.addItemList(optionLabels, 1);
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

    UIComponentStyle::configureControlButton(entry->resetButton, juce::TextButton::ConnectedOnLeft | juce::TextButton::ConnectedOnRight);
    entry->resetButton.onClick = [entryPtr = entry.get()] {
        entryPtr->comboBox.setSelectedId(entryPtr->defaultSelectedId, juce::sendNotificationSync);
    };
    this->contentComponent.addAndMakeVisible(entry->resetButton);
    this->choices.push_back(std::move(entry));
}

void EngineSettingsPanel::loadFromEngine() {
    for (auto& entry : this->controls) {
        if (entry->target != nullptr)
            entry->parameterControl->setValue(static_cast<double>(entry->target->load()), juce::dontSendNotification);
        else if (entry->intTarget != nullptr)
            entry->parameterControl->setValue(static_cast<double>(entry->intTarget->load()), juce::dontSendNotification);
    }
    for (auto& entry : this->choices)
        entry->comboBox.setSelectedId(entry->loadSelectedId(), juce::dontSendNotification);
    this->libraryPathEditor.setText(this->engine.getGlobalLibraryPath().getFullPathName(), false);
    this->cacheLimitControl->setValue(static_cast<double>(this->engine.getGlobalSampleLoaderCacheLimitMegabytes()), juce::dontSendNotification);
    this->rebuildGlobalVelocityResponseChoices();
    this->rebuildLayoutChoices();
}

void EngineSettingsPanel::rebuildGlobalVelocityResponseChoices() {
    const auto presets = this->engine.getVelocityResponsePresets();
    this->globalVelocityResponseDefinitions.clear();
    this->globalVelocityResponseCombo.clear(juce::dontSendNotification);

    int selectedId = 0;
    const auto currentDefinition = this->engine.getVelocityResponse().getDefinition();
    for (int presetIdx = 0; presetIdx < static_cast<int>(presets.size()); ++presetIdx) {
        const auto& preset = presets[static_cast<size_t>(presetIdx)];
        this->globalVelocityResponseDefinitions.push_back(preset.definition);
        this->globalVelocityResponseCombo.addItem(preset.name + (preset.isUser ? "" : " (Built-in)"), presetIdx + 1);
        if (preset.definition == currentDefinition) selectedId = presetIdx + 1;
    }

    if (selectedId > 0) {
        this->globalVelocityResponseCombo.setSelectedId(selectedId, juce::dontSendNotification);
        this->globalVelocityResponseCombo.setTooltip("Controls how MIDI velocity changes output gain for layers using the Global Velocity-to-dB Curve: " + currentDefinition);
    } else {
        this->globalVelocityResponseCombo.setSelectedId(0, juce::dontSendNotification);
        this->globalVelocityResponseCombo.setText("Unnamed global curve*", juce::dontSendNotification);
        this->globalVelocityResponseCombo.setTooltip("Saved with this plugin instance, but not saved as a reusable preset: " + currentDefinition);
    }
}

void EngineSettingsPanel::applyGlobalVelocityResponseChoice() {
    const int responseIdx = this->globalVelocityResponseCombo.getSelectedId() - 1;
    if (responseIdx < 0 || responseIdx >= static_cast<int>(this->globalVelocityResponseDefinitions.size())) return;

    const auto& definition = this->globalVelocityResponseDefinitions[static_cast<size_t>(responseIdx)];
    const bool applied = this->onGlobalVelocityResponseChanged ? this->onGlobalVelocityResponseChanged(definition) : this->engine.setVelocityResponseDefinition(definition);
    if (!applied) {
        this->rebuildGlobalVelocityResponseChoices();
        return;
    }
    this->rebuildGlobalVelocityResponseChoices();
}

void EngineSettingsPanel::rebuildLayoutChoices() {
    this->kitLayouts.clear();
    this->kitLayoutCombo.clear(juce::dontSendNotification);
    this->kitLayouts.push_back(this->engine.getKitLayoutManager()->getDefault());
    for (const auto& layout : this->engine.getKitLayoutManager()->getLayouts())
        this->kitLayouts.push_back(layout);

    const auto userPresetDirectory = this->engine.getUserPresetsDir();
    if (userPresetDirectory.isDirectory()) {
        for (const auto& file : userPresetDirectory.findChildFiles(juce::File::findFiles, false, "*.yml;*.yaml")) {
            auto parsed = LayoutTraits<KitLayout>::parseYaml(file.loadFileAsString());
            if (!parsed.ok()) continue;
            parsed.value.id = file.getFileNameWithoutExtension();
            parsed.value.rebuildIndices();
            this->kitLayouts.push_back(std::move(parsed.value));
        }
    }

    int selectedKitLayoutId = 1;
    for (size_t idx = 0; idx < this->kitLayouts.size(); ++idx) {
        const auto& layout = this->kitLayouts[idx];
        this->kitLayoutCombo.addItem(layout.name.isNotEmpty() ? layout.name : layout.id, static_cast<int>(idx + 1));
        if (layout.id == this->engine.getGlobalKitLayoutId()) selectedKitLayoutId = static_cast<int>(idx + 1);
    }
    this->kitLayoutCombo.setSelectedId(selectedKitLayoutId, juce::dontSendNotification);

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

void EngineSettingsPanel::chooseLibraryPath() {
    this->libraryChooser = std::make_unique<juce::FileChooser>("Choose sample library", this->engine.getGlobalLibraryPath(), juce::String(), true, false, this);
    this->libraryChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& chooser) {
        const auto selectedDirectory = chooser.getResult();
        if (selectedDirectory.isDirectory() && this->engine.setGlobalLibraryPath(selectedDirectory)) {
            this->libraryPathEditor.setText(selectedDirectory.getFullPathName(), false);
            if (this->onLibraryReloadRequested) this->onLibraryReloadRequested();
        }
        this->libraryChooser.reset();
    });
}

void EngineSettingsPanel::saveLayoutDefaults() {
    const int kitLayoutIdx = this->kitLayoutCombo.getSelectedId() - 1;
    const int micLayoutIdx = this->micLayoutCombo.getSelectedId() - 1;
    if (kitLayoutIdx >= 0 && kitLayoutIdx < static_cast<int>(this->kitLayouts.size()))
        this->engine.setGlobalKitLayoutId(this->kitLayouts[static_cast<size_t>(kitLayoutIdx)].id);
    if (micLayoutIdx >= 0 && micLayoutIdx < static_cast<int>(this->micLayouts.size()))
        this->engine.setGlobalMicLayoutId(this->micLayouts[static_cast<size_t>(micLayoutIdx)].id);
}

void EngineSettingsPanel::onSettingChanged() {
    bool gridChanged = false;
    for (auto& entry : this->controls) {
        if (entry->target != nullptr)
            entry->target->store(static_cast<float>(entry->parameterControl->getValue()));
        else if (entry->intTarget != nullptr) {
            const int oldValue = entry->intTarget->load();
            const int newValue = static_cast<int>(entry->parameterControl->getValue());
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

    const int controlCellW = 110;
    const int controlCellH = 44;
    const int choiceCellW = 150;
    const int choiceCellH = 40;
    const int groupLabelHeight = 22;
    const int generalContentHeight = 160;
    const int gap = 4;

    const int availableWidth = juce::jmax(contentWidth, 400) - 16; // 8px padding each side
    const int controlsPerRow = juce::jmax(1, availableWidth / controlCellW);
    const int choicesPerRow = juce::jmax(1, availableWidth / choiceCellW);

    // Calculate group row counts
    auto groupRows = [&](int count) { return (count + controlsPerRow - 1) / controlsPerRow; };
    auto choiceRowsFor = [&](int count) { return count <= 0 ? 0 : (count + choicesPerRow - 1) / choicesPerRow; };
    const int uiChoiceCount = static_cast<int>(this->choices.size()) - this->uiChoiceStart;

    int totalHeight = 8; // top padding
    totalHeight += groupLabelHeight + gap + generalContentHeight + gap;
    totalHeight += groupLabelHeight + gap + groupRows(7) * controlCellH + gap;
    totalHeight += groupLabelHeight + gap + groupRows(6) * controlCellH + gap;
    totalHeight += groupLabelHeight + gap + groupRows(11) * controlCellH + choiceRowsFor(this->hihatChoiceCount) * choiceCellH + gap;
    totalHeight += groupLabelHeight + gap + choiceRowsFor(this->sampleSelectionChoiceCount) * choiceCellH + gap;
    totalHeight += groupLabelHeight + gap + groupRows(3) * controlCellH + choiceRowsFor(uiChoiceCount) * choiceCellH + gap;
    totalHeight += 8; // bottom padding

    this->contentComponent.setBounds(0, 0, juce::jmax(contentWidth, 400), totalHeight);

    auto area = this->contentComponent.getLocalBounds().reduced(8);

    auto placeGroupLabel = [&](juce::Label& groupLabel) {
        groupLabel.setBounds(area.removeFromTop(groupLabelHeight));
        area.removeFromTop(gap);
    };

    auto placeControlGrid = [&](int startIdx, int count) {
        int column = 0;
        int rowStartY = area.getY();
        for (int idx = startIdx; idx < startIdx + count && idx < static_cast<int>(this->controls.size()); ++idx) {
            const int cellX = area.getX() + column * controlCellW;
            auto& entry = this->controls[static_cast<size_t>(idx)];
            entry->parameterControl->setBounds(cellX, rowStartY, controlCellW, controlCellH - 2);

            ++column;
            if (column >= controlsPerRow) {
                column = 0;
                rowStartY += controlCellH;
            }
        }
        const int rowsUsed = groupRows(count);
        area.removeFromTop(rowsUsed * controlCellH + gap);
    };

    auto placeChoiceGrid = [&](int startIdx, int count) {
        int column = 0;
        int rowStartY = area.getY();
        for (int idx = startIdx; idx < startIdx + count && idx < static_cast<int>(this->choices.size()); ++idx) {
            auto& entry = this->choices[static_cast<size_t>(idx)];
            const int cellX = area.getX() + column * choiceCellW;
            entry->label.setBounds(cellX, rowStartY, choiceCellW - 4, 14);
            entry->comboBox.setBounds(cellX, rowStartY + 16, choiceCellW - 24, UITheme::Sizes::standardControlHeight);
            entry->resetButton.setBounds(cellX + choiceCellW - UITheme::Sizes::standardControlHeight, rowStartY + 16, UITheme::Sizes::standardControlHeight, UITheme::Sizes::standardControlHeight);

            ++column;
            if (column >= choicesPerRow) {
                column = 0;
                rowStartY += choiceCellH;
            }
        }
        area.removeFromTop(choiceRowsFor(count) * choiceCellH + gap);
    };

    placeGroupLabel(this->generalGroupLabel);
    auto generalArea = area.removeFromTop(generalContentHeight);
    auto libraryRow = generalArea.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->libraryPathLabel.setBounds(libraryRow.removeFromLeft(110));
    this->browseLibraryButton.setBounds(libraryRow.removeFromRight(86));
    libraryRow.removeFromRight(gap);
    this->libraryPathEditor.setBounds(libraryRow);
    generalArea.removeFromTop(gap);

    auto cacheRow = generalArea.removeFromTop(40);
    cacheRow.removeFromLeft(110);
    this->cacheLimitControl->setBounds(cacheRow.removeFromLeft(170));
    generalArea.removeFromTop(gap);

    auto globalVelocityResponseRow = generalArea.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->globalVelocityResponseLabel.setBounds(globalVelocityResponseRow.removeFromLeft(160));
    this->globalVelocityResponseCombo.setBounds(globalVelocityResponseRow.removeFromLeft(290));
    generalArea.removeFromTop(gap);

    auto kitLayoutRow = generalArea.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->kitLayoutLabel.setBounds(kitLayoutRow.removeFromLeft(110));
    this->kitLayoutCombo.setBounds(kitLayoutRow.removeFromLeft(340));
    generalArea.removeFromTop(gap);

    auto micLayoutRow = generalArea.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->micLayoutLabel.setBounds(micLayoutRow.removeFromLeft(110));
    this->micLayoutCombo.setBounds(micLayoutRow.removeFromLeft(340));
    generalArea.removeFromTop(gap);

    auto defaultsRow = generalArea.removeFromTop(UITheme::Sizes::standardControlHeight);
    defaultsRow.removeFromLeft(110);
    this->saveLayoutDefaultsButton.setBounds(defaultsRow.removeFromLeft(160));
    defaultsRow.removeFromLeft(8);
    this->layoutDefaultsWarningLabel.setBounds(defaultsRow);
    area.removeFromTop(gap);

    placeGroupLabel(this->chokeGroupLabel);
    placeControlGrid(0, 7);

    placeGroupLabel(this->tailGroupLabel);
    placeControlGrid(7, 6);

    placeGroupLabel(this->hihatGroupLabel);
    placeControlGrid(13, 11);
    placeChoiceGrid(0, this->hihatChoiceCount);

    placeGroupLabel(this->sampleSelectionGroupLabel);
    placeChoiceGrid(this->sampleSelectionChoiceStart, this->sampleSelectionChoiceCount);

    placeGroupLabel(this->uiGridGroupLabel);
    placeControlGrid(24, 3);
    placeChoiceGrid(this->uiChoiceStart, uiChoiceCount);
}
