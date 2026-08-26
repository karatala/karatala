#pragma once

#include "AvailableLibraryArticulation.h"
#include "DrumPad.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

class KitPreviewGrid final : public juce::Component {
public:
    struct PreviewTarget {
        AvailableLibraryArticulation libraryArticulation;
        float gainDb = 0.0f;
    };

    struct PreviewInstrument {
        KitLayout::InstrumentSlot slot;
        std::vector<std::vector<PreviewTarget>> articulationTargets;
    };

    KitPreviewGrid() = default;
    ~KitPreviewGrid() override = default;

    void setPreviewInstruments(std::vector<PreviewInstrument> newPreviewInstruments);
    int getPreferredHeight(int availableWidth);
    int getHeightForRows(int rowCount) const;
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setClickVelocity(float velocity);

    std::function<void(const juce::String& slotId, const juce::String& articulationCode, bool useDirectPreview, const std::vector<PreviewTarget>& targets, juce::uint8 velocity)> onPreview;

private:
    static constexpr int padGap = 3;
    static constexpr int preferredPadWidth = 120;
    static constexpr int padHeight = 72;

    std::vector<PreviewInstrument> previewInstruments;
    std::vector<std::unique_ptr<DrumPad>> pads;
    float clickVelocity = 100.0f;

    int getColumnCount(int availableWidth) const;
    void rebuildPads();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitPreviewGrid)
};
