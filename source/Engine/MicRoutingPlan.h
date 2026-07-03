#pragma once

#include "InstrumentCatalog.h"
#include "LayoutManager.h"

#include <map>
#include <set>

class MicRoutingPlan {
public:
    struct SlotRequest {
        juce::String slotId;
        KitLib::InstrumentSpec::Type slotType = KitLib::InstrumentSpec::Type::Unknown;
        DirectMicOutPolicy directPolicy = DirectMicOutPolicy::Shared;
        std::vector<KitLib::MicSpec> directMicSpecs;
    };

    struct Assignment {
        int globalStart = -1;
        int channels = 0;
        juce::String outputName;
        juce::String micCode;
        juce::String warning;
        bool applyBalance = false;

        bool matched() const noexcept { return globalStart >= 0 && channels > 0; }
    };

    static MicRoutingPlan build(const MicLayout& micLayout,
                                const std::vector<SlotRequest>& slotRequests,
                                const InstrumentCatalog& catalog) {
        MicRoutingPlan plan;
        plan.buildOutputs(micLayout, catalog);
        plan.allocateDirectAssignments(slotRequests, catalog);
        return plan;
    }

    Assignment resolve(const juce::String& slotId,
                       KitLib::InstrumentSpec::Type slotType,
                       const KitLib::MicSpec& micSpec) const {
        if (micSpec.type == KitLib::MicSpec::Type::Direct) {
            const auto key = makeAssignmentKey(slotId, micSpec.raw);
            auto it = this->directAssignments.find(key);
            if (it != this->directAssignments.end())
                return it->second;

            Assignment skipped;
            skipped.channels = juce::jmax(1, micSpec.channels);
            skipped.micCode = micSpec.raw;
            skipped.warning = "No routing-plan assignment for direct mic '" + micSpec.raw + "' in slot '" + slotId + "'";
            return skipped;
        }

        for (const auto& output : this->outputs) {
            if (output.spec.type == KitLib::MicSpec::Type::Direct)
                continue;
            if (!normalMicSpecsMatch(micSpec, output.spec))
                continue;
            if (matchMicInstrumentSelector(output.selector, slotId, slotType, this->catalog) == MicSelectorPriority::None)
                continue;

            Assignment assignment;
            assignment.globalStart = output.hostStart;
            assignment.channels = output.channels;
            assignment.outputName = output.name;
            assignment.micCode = output.code;
            return assignment;
        }

        Assignment skipped;
        skipped.channels = juce::jmax(1, micSpec.channels);
        skipped.micCode = micSpec.raw;
        skipped.warning = "No routing-plan assignment for mic '" + micSpec.raw + "' in slot '" + slotId + "'";
        return skipped;
    }

    const std::vector<juce::String>& getWarnings() const noexcept { return this->warnings; }
    int getAssignedHostChannelCount() const noexcept { return this->assignedHostChannelCount; }

private:
    struct OutputEntry {
        int index = -1;
        int hostStart = -1;
        int channels = 0;
        juce::String name;
        juce::String code;
        KitLib::MicSpec spec;
        MicInstrumentSelector selector;
    };

    InstrumentCatalog catalog = InstrumentCatalog::makeDefault();
    std::vector<OutputEntry> outputs;
    std::map<juce::String, Assignment> directAssignments;
    std::vector<juce::String> warnings;
    int assignedHostChannelCount = 0;

    static juce::String makeAssignmentKey(const juce::String& slotId, const juce::String& micCode) {
        return slotId.trim().toLowerCase() + "|" + micCode.trim().toLowerCase();
    }

    static bool normalMicSpecsMatch(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic) {
        return sampleMic.type == outputMic.type
            && sampleMic.position == outputMic.position
            && sampleMic.format == outputMic.format;
    }

    static bool isPrivateSelector(const MicInstrumentSelector& selector) {
        return selector.kind == MicInstrumentSelector::Kind::ExactSlotId
            || selector.kind == MicInstrumentSelector::Kind::WildcardPrefix
            || selector.kind == MicInstrumentSelector::Kind::CatalogKind
            || selector.kind == MicInstrumentSelector::Kind::CommaList
            || selector.kind == MicInstrumentSelector::Kind::CatalogFamily;
    }

    static bool isGroupSelector(const MicInstrumentSelector& selector) {
        return selector.kind == MicInstrumentSelector::Kind::CatalogKind
            || selector.kind == MicInstrumentSelector::Kind::CommaList
            || selector.kind == MicInstrumentSelector::Kind::CatalogFamily;
    }

    void buildOutputs(const MicLayout& micLayout, const InstrumentCatalog& inputCatalog) {
        this->catalog = inputCatalog;
        this->outputs.clear();
        this->warnings.clear();
        this->assignedHostChannelCount = 0;

        int cursor = 0;
        int index = 0;
        for (const auto& mic : micLayout.mics) {
            OutputEntry output;
            output.index = index++;
            output.hostStart = cursor;
            output.channels = juce::jmax(1, mic.specs.channels);
            output.name = mic.name;
            output.code = mic.code;
            output.spec = mic.specs;
            output.selector = parseMicInstrumentSelector(mic.instrument, this->catalog);
            this->outputs.push_back(std::move(output));
            cursor += juce::jmax(1, mic.specs.channels);
        }
        this->assignedHostChannelCount = cursor;
    }

    void allocateDirectAssignments(const std::vector<SlotRequest>& slotRequests, const InstrumentCatalog& inputCatalog) {
        this->catalog = inputCatalog;
        this->directAssignments.clear();

        std::vector<const SlotRequest*> own;
        std::vector<const SlotRequest*> automatic;
        std::vector<const SlotRequest*> shared;
        for (const auto& request : slotRequests) {
            switch (request.directPolicy) {
                case DirectMicOutPolicy::Own:    own.push_back(&request); break;
                case DirectMicOutPolicy::Auto:   automatic.push_back(&request); break;
                case DirectMicOutPolicy::Shared: shared.push_back(&request); break;
            }
        }

        std::set<int> reservedOutputIndices;
        for (const auto* request : own)
            this->allocateDirectForSlot(*request, true, reservedOutputIndices);
        for (const auto* request : automatic)
            this->allocateDirectForSlot(*request, true, reservedOutputIndices);
        for (const auto* request : shared)
            this->allocateDirectForSlot(*request, false, reservedOutputIndices);
    }

    void allocateDirectForSlot(const SlotRequest& request, bool allowPrivateClaim, std::set<int>& reservedOutputIndices) {
        std::set<juce::String> seenCodes;
        for (const auto& micSpec : request.directMicSpecs) {
            if (micSpec.type != KitLib::MicSpec::Type::Direct)
                continue;
            if (!seenCodes.insert(micSpec.raw).second)
                continue;

            Assignment assignment;
            if (allowPrivateClaim)
                assignment = this->findPrivateDirectAssignment(request, micSpec, reservedOutputIndices);
            if (!assignment.matched())
                assignment = this->findGroupDirectAssignment(request, micSpec);
            if (!assignment.matched()) {
                assignment.channels = juce::jmax(1, micSpec.channels);
                assignment.micCode = micSpec.raw;
                assignment.warning = "Skipping direct mic '" + micSpec.raw + "' for slot '" + request.slotId + "': no compatible direct output";
                this->warnings.push_back(assignment.warning);
            }

            this->directAssignments[makeAssignmentKey(request.slotId, micSpec.raw)] = assignment;
        }
    }

    Assignment findPrivateDirectAssignment(const SlotRequest& request, const KitLib::MicSpec& micSpec, std::set<int>& reservedOutputIndices) {
        const OutputEntry* best = nullptr;
        MicSelectorPriority bestPriority = MicSelectorPriority::None;

        for (const auto& output : this->outputs) {
            if (reservedOutputIndices.find(output.index) != reservedOutputIndices.end())
                continue;
            if (!isPrivateSelector(output.selector))
                continue;
            if (!directMicSpecsMatchExactly(micSpec, output.spec))
                continue;

            const auto priority = matchMicInstrumentSelector(output.selector, request.slotId, request.slotType, this->catalog);
            if (priority == MicSelectorPriority::None)
                continue;
            if (best == nullptr || static_cast<int>(priority) > static_cast<int>(bestPriority)) {
                best = &output;
                bestPriority = priority;
            }
        }

        if (best == nullptr)
            return {};

        reservedOutputIndices.insert(best->index);
        return makeAssignment(*best, micSpec.raw);
    }

    Assignment findGroupDirectAssignment(const SlotRequest& request, const KitLib::MicSpec& micSpec) const {
        for (const auto& output : this->outputs) {
            if (!isGroupSelector(output.selector))
                continue;
            if (!directMicSpecsCanUseGroupOutput(micSpec, output.spec))
                continue;
            if (matchMicInstrumentSelector(output.selector, request.slotId, request.slotType, this->catalog) == MicSelectorPriority::None)
                continue;
            return makeAssignment(output, micSpec.raw, true);
        }
        return {};
    }

    static Assignment makeAssignment(const OutputEntry& output, const juce::String& sampleMicCode, bool applyBalance = false) {
        Assignment assignment;
        assignment.globalStart = output.hostStart;
        assignment.channels = output.channels;
        assignment.outputName = output.name;
        assignment.micCode = sampleMicCode;
        assignment.applyBalance = applyBalance;
        return assignment;
    }
};
