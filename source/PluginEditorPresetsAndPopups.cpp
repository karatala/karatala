#include "PluginEditor.h"
#include "UI/CompactModalDialog.h"
#include "UI/UIComponentStyle.h"
#include "UI/UITheme.h"
#include "Engine/LayoutManager.h"
#include <set>

void PluginEditor::refreshPresetList() {
    std::vector<MidiMapTable::PresetEntry> entries;

    MidiMapTable::PresetEntry defaultEntry;
    auto defaultLayout = KitLayout::getDefault();
    defaultEntry.id = defaultLayout.id;
    defaultEntry.name = defaultLayout.name.isNotEmpty() ? defaultLayout.name : defaultLayout.id;
    defaultEntry.isUser = false;
    entries.push_back(defaultEntry);

    // Factory presets from kit_layouts dir
    auto& engine = this->processorRef.getEngine();
    auto factoryLayouts = engine.getKitLayoutManager()->getLayouts();
    for (const auto& layout : factoryLayouts) {
        if (layout.id == defaultEntry.id) continue;
        MidiMapTable::PresetEntry entry;
        entry.id = layout.id;
        entry.name = layout.name.isNotEmpty() ? layout.name : layout.id;
        entry.isUser = false;
        entries.push_back(entry);
    }

    // User presets from presets dir
    auto userDir = engine.getUserPresetsDir();
    if (userDir.isDirectory()) {
        auto files = userDir.findChildFiles(juce::File::findFiles, false, "*.yml;*.yaml");
        for (const auto& file : files) {
            auto yaml = file.loadFileAsString();
            auto parsed = LayoutTraits<KitLayout>::parseYaml(yaml);
            MidiMapTable::PresetEntry entry;
            entry.id = file.getFileNameWithoutExtension();
            entry.name = parsed.ok() && parsed.value.name.isNotEmpty() ? parsed.value.name : entry.id;
            entry.isUser = true;
            entries.push_back(entry);
        }
    }

    auto* layout = engine.getKitLayout();
    juce::String currentId = layout ? layout->id : juce::String();
    this->midiMapTable.populatePresets(entries, currentId);
}

void PluginEditor::saveLayoutToUserPresets(const juce::String& fileName, const KitLayout& layout) {
    auto userDir = this->processorRef.getEngine().getUserPresetsDir();
    userDir.createDirectory();
    auto file = userDir.getChildFile(fileName + ".yml");
    auto yaml = LayoutTraits<KitLayout>::toYaml(layout);
    file.replaceWithText(yaml);
}

juce::String PluginEditor::makeUniqueCopyName(const juce::String& baseName) {
    auto userDir = this->processorRef.getEngine().getUserPresetsDir();
    int num = 1;
    juce::String candidate = baseName + " Copy";
    while (userDir.getChildFile(candidate + ".yml").exists() ||
           userDir.getChildFile(candidate + ".yaml").exists()) {
        num++;
        candidate = baseName + " Copy " + juce::String(num);
    }
    return candidate;
}

void PluginEditor::handlePresetSave() {
    auto* layout = this->processorRef.getEngine().getKitLayout();
    if (!layout) return;

    auto userDir = this->processorRef.getEngine().getUserPresetsDir();
    bool isUser = userDir.getChildFile(layout->id + ".yml").exists() ||
                  userDir.getChildFile(layout->id + ".yaml").exists();

    if (isUser) {
        this->saveLayoutToUserPresets(layout->id, *layout);
        this->statusBar.setStatusText("Preset saved: " + layout->name);
        this->refreshPresetList();
    } else {
        juce::String defaultName = this->makeUniqueCopyName(layout->name.isNotEmpty() ? layout->name : layout->id);
        CompactModalDialog::Options options;
        options.title = "Save As User Preset";
        options.message = "Enter a preset name.";
        options.confirmText = "Save";
        options.fields.push_back({ "name", "Name", defaultName, 0, {} });
        KitLayout layoutCopy = *layout;
        juce::Component::SafePointer<PluginEditor> safeThis(this);
        CompactModalDialog::show(*this, std::move(options), [safeThis, layoutCopy](const CompactModalDialog& dialog) mutable {
            if (safeThis == nullptr) return juce::String();
            const auto name = dialog.getFieldText("name").trim();
            if (name.isEmpty()) return juce::String("Enter a preset name.");
            const auto fileName = name.replaceCharacters("\\/:|*?<>\"", "_________");
            layoutCopy.name = name;
            layoutCopy.id = fileName;
            safeThis->saveLayoutToUserPresets(fileName, layoutCopy);
            safeThis->processorRef.getEngine().setKitLayoutDirect(layoutCopy);
            safeThis->statusBar.setStatusText("Saved as: " + name);
            safeThis->markDrumPadGridDirty();
            safeThis->refreshMidiMapTableIfNeeded(true);
            safeThis->refreshDrumPadGridIfNeeded();
            return juce::String();
        });
    }
}

void PluginEditor::handlePresetClone() {
    auto* layout = this->processorRef.getEngine().getKitLayout();
    if (!layout) return;

    juce::String baseName = layout->name.isNotEmpty() ? layout->name : layout->id;
    juce::String defaultName = this->makeUniqueCopyName(baseName);
    CompactModalDialog::Options options;
    options.title = "Clone Preset";
    options.message = "Enter a name for the copy.";
    options.confirmText = "Clone";
    options.fields.push_back({ "name", "Name", defaultName, 0, {} });
    KitLayout layoutCopy = *layout;
    juce::Component::SafePointer<PluginEditor> safeThis(this);
    CompactModalDialog::show(*this, std::move(options), [safeThis, layoutCopy](const CompactModalDialog& dialog) mutable {
        if (safeThis == nullptr) return juce::String();
        const auto name = dialog.getFieldText("name").trim();
        if (name.isEmpty()) return juce::String("Enter a preset name.");
        const auto fileName = name.replaceCharacters("\\/:|*?<>\"", "_________");
        layoutCopy.name = name;
        layoutCopy.id = fileName;
        safeThis->saveLayoutToUserPresets(fileName, layoutCopy);
        safeThis->processorRef.getEngine().setKitLayoutDirect(layoutCopy);
        safeThis->statusBar.setStatusText("Cloned as: " + name);
        safeThis->markDrumPadGridDirty();
        safeThis->refreshMidiMapTableIfNeeded(true);
        safeThis->refreshDrumPadGridIfNeeded();
        return juce::String();
    });
}

void PluginEditor::handlePresetDelete() {
    auto* layout = this->processorRef.getEngine().getKitLayout();
    if (!layout) return;

    auto userDir = this->processorRef.getEngine().getUserPresetsDir();
    auto file = userDir.getChildFile(layout->id + ".yml");
    if (!file.exists()) file = userDir.getChildFile(layout->id + ".yaml");
    if (!file.exists()) return;

    juce::String presetName = layout->name.isNotEmpty() ? layout->name : layout->id;
    CompactModalDialog::Options options;
    options.title = "Delete Preset";
    options.message = "Delete \"" + presetName + "\"? This cannot be undone.";
    options.confirmText = "Delete";
    options.destructive = true;
    juce::Component::SafePointer<PluginEditor> safeThis(this);
    CompactModalDialog::show(*this, std::move(options), [safeThis, file](const CompactModalDialog&) {
        if (safeThis == nullptr) return juce::String();
        if (!file.deleteFile()) return juce::String("Could not delete this preset.");
        safeThis->statusBar.setStatusText("Preset deleted");
        safeThis->refreshPresetList();
        return juce::String();
    });
}


void PluginEditor::showCrosstalkPopup() {
    this->dismissCrosstalkPopup();
    this->dismissPadContextPopup();
    this->processorRef.setMidiLearnCallback(nullptr);

    this->crosstalkPopup = std::make_unique<CrosstalkCalibrationPopup>(this->processorRef.getEngine());
    this->crosstalkPopup->onCaptureStateChanged = [this](bool enabled) {
        this->processorRef.setMidiCaptureEnabled(enabled);
    };
    this->crosstalkPopup->onMidiPoll = [this](int& noteNumber, int& velocity) {
        const bool received = this->processorRef.popCapturedMidiNote(noteNumber, velocity);
        if (received)
            this->flashPadForMidiNote(noteNumber, static_cast<float>(velocity));
        return received;
    };
    this->crosstalkPopup->onStatusMessage = [this](const juce::String& message) {
        this->statusBar.setStatusText(message);
    };
    this->crosstalkPopup->onDismiss = [this] {
        juce::MessageManager::callAsync([this] { this->dismissCrosstalkPopup(); });
    };

    const auto availableBounds = this->getLocalBounds().reduced(32, 24);
    this->crosstalkPopup->setBounds(this->crosstalkPopup->getPreferredBounds(availableBounds));

    this->crosstalkPopupOverlay = std::make_unique<DismissOverlay>();
    this->crosstalkPopupOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] { this->dismissCrosstalkPopup(); });
    };
    this->crosstalkPopupOverlay->setBounds(this->getLocalBounds());
    this->addAndMakeVisible(*this->crosstalkPopupOverlay);
    this->crosstalkPopupOverlay->toFront(false);

    this->addAndMakeVisible(*this->crosstalkPopup);
    this->crosstalkPopup->toFront(true);
}

void PluginEditor::dismissCrosstalkPopup() {
    if (this->crosstalkPopup) {
        this->crosstalkPopup->cancelAndRestore();
        this->processorRef.setMidiCaptureEnabled(false);
    }
    if (this->crosstalkPopupOverlay) {
        this->removeChildComponent(this->crosstalkPopupOverlay.get());
        this->crosstalkPopupOverlay.reset();
    }
    if (this->crosstalkPopup) {
        this->removeChildComponent(this->crosstalkPopup.get());
        this->crosstalkPopup.reset();
    }
}


void PluginEditor::dismissPadContextPopup() {
    if (this->padContextPopup) {
        this->padContextPopup->flushMapText();
        // Stop MIDI learn when closing popup
        this->processorRef.setMidiLearnCallback(nullptr);
    }

    if (this->padPopupOverlay) {
        removeChildComponent(this->padPopupOverlay.get());
        this->padPopupOverlay.reset();
    }
    if (this->padContextPopup) {
        removeChildComponent(this->padContextPopup.get());
        this->padContextPopup.reset();
    }
}

void PluginEditor::showPadContextPopup(const KitLayout::InstrumentSlot* slot, int artIdx, juce::Point<int> /*screenPos*/) {
    this->dismissPadContextPopup();
    if (!slot)
        return;

    // Pass the saved instrument map from ValueTreeState so layers are correctly restored
    auto savedMapText = this->processorRef.getValueTreeState().state["instrument_map"].toString();
    this->padContextPopup = std::make_unique<PadContextPopup>(this->processorRef.getEngine(), *slot, artIdx, savedMapText);
    this->padContextPopup->setPreviewVelocityProvider([this]() {
        return static_cast<int>(this->sidebar.velocityKnob.getValue());
    });
    this->padContextPopup->onDismiss = [this] {
        juce::MessageManager::callAsync([this] { this->dismissPadContextPopup(); });
    };
    this->padContextPopup->onMapTextChanged = [this](const juce::String& mapText) {
        this->processorRef.getValueTreeState().state.setProperty("instrument_map", mapText, nullptr);
        this->instrumentMapEditor.setText(mapText, false);
        this->statusBar.setStatusText(this->processorRef.getEngine().getLastInstrumentMapStatus());
        this->markInstrumentConfigDirty();
    };
    this->padContextPopup->onStatusMessage = [this](const juce::String& message) {
        this->statusBar.setStatusText(message);
    };
    this->padContextPopup->onInstrumentChanged = [this] {
        this->markDrumPadGridDirty();
        this->markInstrumentConfigDirty();
        this->refreshDrumPadGridIfNeeded();
    };
    this->padContextPopup->onMidiMappingChanged = [this] {
        auto* layout = this->processorRef.getEngine().getKitLayout();
        if (layout) {
            layout->rebuildIndices();
            this->processorRef.getEngine().rebuildMidiNoteRoutingFromLayout();
        }
        this->statusBar.setStatusText("MIDI mapping updated");
        this->markMidiMapTableDirty();
    };
    this->padContextPopup->onMidiLearnStateChanged = [this](bool learning) {
        if (learning) {
            this->processorRef.setMidiLearnCallback([this](int noteNumber) {
                this->flashPadForMidiNote(noteNumber, 127.0f);
                if (this->padContextPopup)
                    this->padContextPopup->midiLearnNoteReceived(noteNumber);
            });
        } else {
            this->processorRef.setMidiLearnCallback(nullptr);
        }
    };

    this->padPopupOverlay = std::make_unique<DismissOverlay>();
    this->padPopupOverlay->setDarkened(true);
    this->padPopupOverlay->setDismissOnBackgroundClick(true);
    this->padPopupOverlay->onClick = [this] {
        juce::MessageManager::callAsync([this] {
            if (this->padContextPopup && this->padContextPopup->dismissActiveDialog()) return;
            if (this->padContextPopup) {
                this->padContextPopup->flushMapText();
                if (this->padContextPopup->onDismiss) {
                    this->padContextPopup->onDismiss();
                    return;
                }
            }
            this->dismissPadContextPopup();
        });
    };
    this->addAndMakeVisible(*this->padPopupOverlay);
    this->padPopupOverlay->toFront(false);

    this->addAndMakeVisible(*this->padContextPopup);
    this->padContextPopup->toFront(true);
    this->resized();
}
