#pragma once

#include "InstrumentCatalog.h"
#include "InstrumentType.h"
#include "LayoutManager.h"

#include <map>
#include <set>
#include <vector>

class MicRoutingPlan {
public:
    struct SlotRequest {
        juce::String slotId;
        KitLib::InstrumentType slotType = KitLib::InstrumentType::Unknown;
        DirectMicOutPolicy directPolicy = DirectMicOutPolicy::Shared;
        std::vector<KitLib::MicSpec> directMicSpecs;
    };

    struct Assignment {
        int globalStart = -1;
        int channels = 0;
        juce::String outputName;
        juce::String micCode;
        juce::String outputMicCode;
        juce::String outputId;
        juce::String warning;
        bool applyBalance = false;

        bool matched() const noexcept { return this->globalStart >= 0 && this->channels > 0; }
    };

    static MicRoutingPlan build(const MicLayout& micLayout, const std::vector<SlotRequest>& slotRequests, const InstrumentCatalog& catalog);
    Assignment resolveOverride(const juce::String& slotId, const KitLib::MicSpec& sampleMicSpec, StringsDictSingleton::Id outputKey, KitLib::InstrumentType slotType = KitLib::InstrumentType::Unknown) const;
    Assignment resolveOverride(const juce::String& slotId, const KitLib::MicSpec& sampleMicSpec, const juce::String& outputId, KitLib::InstrumentType slotType = KitLib::InstrumentType::Unknown) const;
    Assignment resolve(const juce::String& slotId, KitLib::InstrumentType slotType, const KitLib::MicSpec& micSpec) const;

    const std::vector<juce::String>& getWarnings() const noexcept { return this->warnings; }
    int getAssignedHostChannelCount() const noexcept { return this->assignedHostChannelCount; }

private:
    struct OutputEntry {
        int index = -1;
        int hostStart = -1;
        int channels = 0;
        juce::String name;
        juce::String code;
        juce::String outputId;
        StringsDictSingleton::Id outputKey = StringsDictSingleton::EMPTY;
        KitLib::MicSpec spec;
        MicInstrumentSelector selector;
    };

    InstrumentCatalog catalog = InstrumentCatalog::makeDefault();
    std::vector<OutputEntry> outputs;
    std::map<juce::String, Assignment> directAssignments;
    std::vector<juce::String> warnings;
    int assignedHostChannelCount = 0;

    static juce::String makeAssignmentKey(const juce::String& slotId, const juce::String& micCode);
    static bool normalMicSpecsMatch(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic);
    static bool isPrivateSelector(const MicInstrumentSelector& selector);
    static bool isGroupSelector(const MicInstrumentSelector& selector);
    void buildOutputs(const MicLayout& micLayout, const InstrumentCatalog& inputCatalog);
    void allocateDirectAssignments(const std::vector<SlotRequest>& slotRequests, const InstrumentCatalog& inputCatalog);
    void allocateDirectForSlot(const SlotRequest& request, bool allowPrivateClaim, std::set<int>& reservedOutputIndices);
    Assignment findPrivateDirectAssignment(const SlotRequest& request, const KitLib::MicSpec& micSpec, std::set<int>& reservedOutputIndices);
    Assignment findGroupDirectAssignment(const SlotRequest& request, const KitLib::MicSpec& micSpec) const;
    static Assignment makeAssignment(const OutputEntry& output, const juce::String& sampleMicCode, bool applyBalance = false);
};
