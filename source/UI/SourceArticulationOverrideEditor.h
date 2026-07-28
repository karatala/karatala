#pragma once

#include "../Engine/DrumKitEngine.h"
#include "LookAndFeel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class SourceArticulationOverrideEditor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    explicit SourceArticulationOverrideEditor(DrumKitEngine& engine);
    ~SourceArticulationOverrideEditor() override;

    void setSource(const juce::String& kitId, const juce::String& instrumentId, const juce::String& articulationId, const juce::String& displayTitle);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void(int velocity, int namedVelocity, const juce::String& roundRobin, const juce::String& relativePath, const juce::String& micCode)> onPreview;
    std::function<void()> onDone;

private:
    static constexpr int roundRobinColumnId = 1;
    static constexpr int velocityColumnIdBase = 100;

    struct LogicalHitKey {
        int namedVelocity = 0;
        juce::String roundRobin;
    };

    enum class BatchField {
        ManualGain,
        FileOffset,
        MicOffset,
    };

    struct CommonBatchValue {
        bool mixed = false;
        std::optional<float> value;
    };

    class WheelTextEditor : public juce::TextEditor {
    public:
        std::function<void(bool increase, bool fine)> onWheelStep;
        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    };

    class SampleMatrixLookAndFeel : public LookAndFeel {
    public:
        void drawTableHeaderColumn(juce::Graphics& graphics, juce::TableHeaderComponent& header, const juce::String& columnName, int columnId, int width, int height, bool isMouseOver, bool isMouseDown, int columnFlags) override;
    };

    class SampleMatrixModel : public juce::TableListBoxModel {
    public:
        explicit SampleMatrixModel(SourceArticulationOverrideEditor& editorOwner) : owner(editorOwner) {}
        int getNumRows() override;
        void paintRowBackground(juce::Graphics& graphics, int rowNumber, int width, int height, bool rowIsSelected) override;
        void paintCell(juce::Graphics& graphics, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
        void cellClicked(int rowNumber, int columnId, const juce::MouseEvent& event) override;
        juce::String getCellTooltip(int rowNumber, int columnId) override;

    private:
        SourceArticulationOverrideEditor& owner;
    };

    class SelectedSampleListModel : public juce::ListBoxModel {
    public:
        explicit SelectedSampleListModel(SourceArticulationOverrideEditor& editorOwner) : owner(editorOwner) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& graphics, int width, int height, bool rowIsSelected) override;
        juce::String getTooltipForRow(int rowNumber) override;

    private:
        SourceArticulationOverrideEditor& owner;
    };

    DrumKitEngine& engine;
    DrumKitEngine::SourceVelocityRangeInfo rangeInfo;
    DrumKitEngine::SourceVelocityRangeInfo committedRangeInfo;
    int selectedDividerIdx = -1;
    bool draggingDivider = false;
    bool previewPending = false;
    juce::Rectangle<float> rangeBounds;

    std::vector<juce::String> roundRobins;
    std::vector<juce::String> micCodes;
    std::vector<juce::String> selectedMicCodes;
    std::vector<LogicalHitKey> selectedHits;
    std::vector<int> selectedSampleIndices;
    LogicalHitKey activeHit;
    bool hasActiveHit = false;

    LogicalHitKey lastPreviewHit;
    juce::String lastPreviewMicCode;
    int lastPreviewVelocity = 0;
    bool lastPreviewWasMicSolo = false;
    bool hasLastPreview = false;
    juce::String hoverPreviewKey;
    int hoverPreviewGeneration = 0;

    juce::Label titleLabel;
    juce::Label scopeLabel;
    juce::Label balanceSectionLabel;
    juce::Label instrumentLabel;
    juce::Label instrumentNameLabel;
    juce::Label articulationLabel;
    juce::Label articulationNameLabel;
    juce::Label velocityRangesLabel;
    juce::Label sampleOverridesLabel;
    juce::Label selectedSamplesLabel;
    juce::Label micLabel;
    juce::Label statusLabel;
    juce::Label manualGainLabel;
    juce::Label fileOffsetLabel;
    juce::Label micOffsetLabel;
    WheelTextEditor instrumentBalanceEditor;
    WheelTextEditor articulationBalanceEditor;
    WheelTextEditor manualGainEditor;
    WheelTextEditor fileOffsetEditor;
    WheelTextEditor micOffsetEditor;
    juce::TextButton resetInstrumentBalanceButton;
    juce::TextButton resetArticulationBalanceButton;
    juce::TextButton resetManualGainButton;
    juce::TextButton resetFileOffsetButton;
    juce::TextButton resetMicOffsetButton;
    juce::TextButton evenRangesButton { "Even" };
    juce::TextButton resetRangesButton { "Reset" };
    juce::TextButton retrySaveButton { "Retry save" };
    juce::TextButton reloadSavedButton { "Reload saved" };
    juce::TextButton doneButton { "Done" };
    SampleMatrixLookAndFeel sampleMatrixLookAndFeel;
    SampleMatrixModel sampleMatrixModel;
    SelectedSampleListModel selectedSampleListModel;
    juce::TableListBox sampleMatrix;
    juce::ListBox selectedSampleList;
    std::vector<std::unique_ptr<juce::TextButton>> micButtons;
    bool manualGainDirty = false;
    bool fileOffsetDirty = false;
    bool micOffsetDirty = false;
    std::function<void()> retrySaveAction;

    bool applySelectedBoundary(int upperBoundary, bool persist);
    void evenlyDistributeRanges();
    void resetAllBoundaries();
    void restoreCommittedPreview();
    void updateControls();
    void updateBalanceControls();
    void rebuildSampleView(bool initializeSelection);
    void rebuildMatrixColumns();
    void rebuildMicButtons();
    void updateSelectedSamples();
    void updateBatchEditors();
    void updateMicButtonStates();
    void commitInstrumentBalance();
    void commitArticulationBalance();
    void commitManualGain();
    void commitFilePlaybackOffset();
    void commitMicPlaybackOffset();
    bool commitPendingSampleEditors();
    void showPersistenceFailure(const juce::String& errorText, std::function<void()> retryAction);
    void clearPersistenceFailure();
    bool hasPersistenceFailure() const;
    void reloadSavedOverrides();
    void replaceInfo(DrumKitEngine::SourceVelocityRangeInfo updatedInfo, bool initializeSelection = false);
    void timerCallback() override;
    void setStatus(const juce::String& text, bool error = false);

    bool logicalHitExists(const LogicalHitKey& hit) const;
    bool logicalHitExists(int levelIdx, int roundRobinIdx) const;
    bool isLogicalHitSelected(const LogicalHitKey& hit) const;
    bool logicalHitsMatch(const LogicalHitKey& first, const LogicalHitKey& second) const;
    void toggleLogicalHitSelection(int levelIdx, int roundRobinIdx);
    void invertLogicalHitSelection(std::optional<int> levelIdx, std::optional<int> roundRobinIdx = std::nullopt);
    void toggleMicSelection(const juce::String& micCode);
    bool isMicSelected(const juce::String& micCode) const;
    const DrumKitEngine::SourceSampleOverrideInfo* findSample(const LogicalHitKey& hit, const juce::String& micCode = {}) const;
    std::vector<juce::String> getSelectedRelativePaths() const;
    std::vector<DrumKitEngine::SourceSamplePlaybackOffsetTarget> getSelectedPlaybackTargets(bool micScoped) const;
    CommonBatchValue getCommonBatchValue(BatchField field) const;
    bool selectedSamplesAreEditable() const;

    void previewLogicalHit(const LogicalHitKey& hit, int playbackVelocity);
    void previewMic(const LogicalHitKey& hit, const juce::String& micCode, int playbackVelocity);
    void previewVelocityAt(float xPosition);
    void previewChangedValues();
    void replayLastPreview();
    void queueLogicalHitHover(const LogicalHitKey& hit);
    void queueMicHover(const juce::String& micCode);
    void clearHoverPreview();
    void updateHoverTarget(const juce::MouseEvent& event);
    std::optional<LogicalHitKey> getMatrixHitAt(const juce::MouseEvent& event) const;
    juce::String getMicCodeAt(const juce::MouseEvent& event) const;

    int findNearestDivider(float xPosition, float maximumDistance) const;
    int boundaryForX(float xPosition) const;
    float boundaryToX(int upperBoundary) const;
    int getSelectedMinimum() const;
    int getSelectedMaximum() const;
    int getLevelIndexForVelocity(int velocity) const;
    float getBatchWheelBaseValue(BatchField field) const;
    void stepBalanceEditor(WheelTextEditor& editor, const DrumKitEngine::SourceBalanceInfo& balanceInfo, bool increase, bool fine);
    void stepBatchEditor(WheelTextEditor& editor, BatchField field, bool increase, bool fine);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceArticulationOverrideEditor)
};
