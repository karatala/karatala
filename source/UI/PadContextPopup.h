#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/DrumKitEngine.h"
#include "SourceItem.h"
#include "SourcePickerPopup.h"
#include "DismissOverlay.h"
#include "MidiNoteAssignmentControl.h"
#include "CompactParameterControl.h"
#include "LibraryArticulationEditor.h"
#include "VelocityResponseEditor.h"

class PadContextPopup : public juce::Component {
public:
    explicit PadContextPopup(DrumKitEngine& engine, const KitLayout::InstrumentSlot& slot, int artIdx, const juce::String& savedInstrumentMapText = "");
    ~PadContextPopup() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void flushMapText();
    bool dismissActiveDialog();
    void setPreviewVelocityProvider(std::function<int()> provider) { this->previewVelocityProvider = std::move(provider); }

    std::function<void()> onDismiss;
    /** Fires whenever this popup changes the instrument map text. */
    std::function<void(const juce::String&)> onMapTextChanged;
    std::function<void(const juce::String&)> onStatusMessage;
    std::function<void()> onInstrumentChanged;
    /** Fires when MIDI mapping changes (note combos edited). */
    std::function<void()> onMidiMappingChanged;

    /** Called when the user receives a MIDI note while learn is active. */
    void midiLearnNoteReceived(int midiNote);

    /** Callback fired when MIDI learn starts (true) or stops (false). */
    std::function<void(bool)> onMidiLearnStateChanged;

private:
    enum class EditableLayerParameter {
        Tuning,
        Attack,
        Decay,
        Gain,
        Pan,
        FlipLR,
        FlipPhase,
        Bleed,
        VelocityResponse,
    };

    static constexpr int velocityResponseGlobalItemId = 1;
    static constexpr int velocityResponseEditItemId = 3;
    static constexpr int velocityResponsePresetBaseItemId = 100;

    DrumKitEngine& engine;
    juce::String slotId;
    juce::String artName;
    juce::String savedInstrumentMapText;
    std::function<int()> previewVelocityProvider;
    KitLib::InstrumentSpec::Type slotType = KitLib::InstrumentSpec::Type::Unknown;

    // ─── Header ───
    juce::Label slotLabel;
    juce::TextButton linkArtsButton { "LINK ARTS" };
    juce::TextButton linkTomBleedButton { "LINK TOMS BLEED" };
    juce::TextButton setInstrumentButton { "SET INSTRUMENT" };

    // ─── MIDI map (inline in header) ───
    juce::Label     midiHeaderLabel;
    std::unique_ptr<MidiNoteAssignmentControl> midiAssignmentControl;
    KitLayout::MidiNotes* midiNotesPtr = nullptr;
    KitLayout::MidiNotes midiNotesOwned;
    int selectedArticulationIndex = -1;
    int activeLearningSlot = -1;

    void syncNoteButtons();
    void applyNoteToSlot(int slotIdx, int note);
    void syncMidiNotesToActiveLayout();

    std::vector<SourceItem> sourceItems;
    void rebuildSourceItems();
    int findSourceIndex(const juce::String& searchPath) const;

    // ─── Instrument map layers ───
    struct MicRoutingChoice {
        juce::String label;
        juce::String tooltip;
        juce::String outputId;
        int rank = 0;
        bool hasProblem = false;
        bool isCurrent = false;
        bool usesAutomaticRouting = false;
    };
    struct ArticulationGroup {
        juce::String artName;
        juce::Label label;
        juce::TextButton addLayerButton { "+" };
    };
    struct MapLayerRow {
        juce::String artName;
        juce::String sourcePathHint;
        int selectedSourceIndex = -1;
        int mappedLayerIndex = -1;
        std::optional<juce::String> velocityResponse;
        std::unique_ptr<juce::TextButton> sourceButton;
        std::unique_ptr<juce::Label> sourceDetailLabel;
        std::unique_ptr<juce::ComboBox> velocityResponseMode;
        std::unique_ptr<juce::TextButton> libraryArticulationEditorButton;
        std::unique_ptr<CompactParameterControl> tuningPot;
        std::unique_ptr<CompactParameterControl> attackPot;
        std::unique_ptr<CompactParameterControl> decayPot;
        std::unique_ptr<CompactParameterControl> gainPot;
        std::unique_ptr<CompactParameterControl> panPot;
        std::unique_ptr<juce::TextButton> flipLRButton;
        bool flipLRExplicit = false;
        std::unique_ptr<juce::TextButton> flipPhaseButton;
        // Per-mic bleed controls
        struct MicBleedControl {
            juce::String micCode;
            juce::String micName;
            juce::String selectedOutputId;
            std::unique_ptr<CompactParameterControl> bleedPot;
            std::unique_ptr<juce::ComboBox> routingCombo;
            std::vector<MicRoutingChoice> routingChoices;
        };
        std::vector<std::unique_ptr<MicBleedControl>> micBleeds;
        std::unique_ptr<juce::TextButton> removeButton;
        // Cached bounds for painting group backgrounds
        juce::Rectangle<int> mainGroupBounds;
        juce::Rectangle<int> bleedGroupBounds;
    };
    juce::OwnedArray<ArticulationGroup> articulationGroups;
    juce::OwnedArray<MapLayerRow> layerRows;
    std::vector<DrumKitEngine::VelocityResponsePreset> velocityResponsePresets;

    void addArticulationGroup(const juce::String& articulationName);
    MapLayerRow* addMapLayerRow(const juce::String& articulationName, const juce::String& sourceFullPath, int tuningCents, int attackMs, int decayMs,
                          float volumeDb, int pan, bool flipLR, bool flipLRExplicit, bool flipPhase,
                          const std::optional<juce::String>& velocityResponse,
                          const std::unordered_map<juce::String, float>& micBleedLevels,
                          const std::unordered_map<juce::String, juce::String>& micRouteOverrides, int mappedLayerIndex);
    juce::uint8 getPreviewVelocity() const;
    int getLayerIndex(const MapLayerRow* layer) const;
    int findVelocityResponsePresetIndex(const std::optional<juce::String>& definition) const;
    void rebuildVelocityResponseChoices(MapLayerRow* layer);
    void refreshVelocityResponseChoices();
    void commitVelocityResponse(MapLayerRow* layer);
    void updateVelocityResponseDisplay(MapLayerRow* layer);
    void applyLayerParameterChange(MapLayerRow* layer, EditableLayerParameter parameter, const juce::String& micCode = {});
    void updateSourceCardButtonVisibility();
    void updateSourceDisplay(MapLayerRow* layer);
    void rebuildBleedControls(MapLayerRow* layer, const std::unordered_map<juce::String, float>& seedLevels = {}, const std::unordered_map<juce::String, juce::String>& seedRoutes = {});
    void applyRoutingComboChange(MapLayerRow* layer, MapLayerRow::MicBleedControl* bleedCtrl);
    bool applyToEngine();
    void refreshLayerMappings();
    void rebuildMapLayerRows();
    juce::String generateFullMapText() const;
    void populateLayers();
    std::vector<std::pair<juce::String, juce::String>> getRelevantMicsForSourcePath(const juce::String& sourcePath) const;
    std::vector<std::pair<juce::String, juce::String>> getRelevantMicsForSourceIndex(int sourceIndex) const;
    std::vector<KitLib::MicSpec> getDirectMicSpecsForSourcePath(const juce::String& sourcePath) const;
    const MicLayout::Mic* findLayoutMicForSlot(const KitLayout::InstrumentSlot& slot, const juce::String& micCode) const;
    struct MicRoutingDisplay {
        juce::String routeText;
        juce::String tooltip;
        juce::String outputId;
        bool hasProblem = false;
    };
    MicRoutingDisplay buildMicRoutingDisplay(const juce::String& sourcePath, const juce::String& micCode, const juce::String& targetSlotId) const;
    std::vector<MicRoutingChoice> buildMicRoutingChoices(const juce::String& sourcePath, const juce::String& micCode, const juce::String& targetSlotId) const;
    static int findRoutingChoiceIndex(const std::vector<MicRoutingChoice>& choices, const juce::String& outputId);
    void updateRoutingComboDisplay(MapLayerRow::MicBleedControl& bleedCtrl) const;
    bool mapTextDirty = false;
    bool suppressParameterCallbacks = false;

    std::unique_ptr<DismissOverlay> pickerOverlay;
    std::unique_ptr<SourcePickerPopup> activePicker;
    std::unique_ptr<DismissOverlay> velocityResponseOverlay;
    std::unique_ptr<VelocityResponseEditor> activeVelocityResponseEditor;
    std::unique_ptr<DismissOverlay> libraryArticulationOverlay;
    std::unique_ptr<LibraryArticulationEditor> activeLibraryArticulationEditor;

    // ─── Scrollable layer area ───
    struct LayerContentComponent : public juce::Component {
        juce::OwnedArray<MapLayerRow>* layerRowsPtr = nullptr;
        void paint(juce::Graphics& g) override;
    };
    LayerContentComponent layerContent;
    juce::Viewport layerViewport;

    void showSourcePicker(MapLayerRow* layer);
    void showInstrumentPicker();
    void dismissSourcePicker();
    void layoutSourcePicker();
    void showVelocityResponseEditor(MapLayerRow* layer);
    void dismissVelocityResponseEditor();
    void layoutVelocityResponseEditor();
    void showLibraryArticulationEditor(MapLayerRow* layer);
    void dismissLibraryArticulationEditor();
    void layoutLibraryArticulationEditor();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadContextPopup)
};
