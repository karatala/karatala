#include "MicRoutingPlan.h"

#include <utility>

MicRoutingPlan MicRoutingPlan::build(const MicLayout& micLayout, const std::vector<SlotRequest>& slotRequests, const InstrumentCatalog& catalog) {
    MicRoutingPlan plan;
    plan.buildOutputs(micLayout, catalog);
    plan.allocateDirectAssignments(slotRequests, catalog);
    return plan;
}

MicRoutingPlan::Assignment MicRoutingPlan::resolveOverride(const juce::String& slotId, const KitLib::MicSpec& sampleMicSpec, StringsDictSingleton::Id outputKey, KitLib::InstrumentType slotType) const {
    if (outputKey == StringsDictSingleton::EMPTY) return {};

    const OutputEntry* bestOutput = nullptr;
    MicSelectorPriority bestPriority = MicSelectorPriority::None;
    for (const auto& output : this->outputs) {
        if (output.outputKey != outputKey) continue;
        const auto priority = matchMicInstrumentSelector(output.selector, slotId, slotType, this->catalog);
        if (priority == MicSelectorPriority::None) continue;
        if (bestOutput == nullptr || static_cast<int>(priority) > static_cast<int>(bestPriority)) {
            bestOutput = &output;
            bestPriority = priority;
        }
    }
    if (bestOutput != nullptr) {
        Assignment assignment;
        assignment.globalStart = bestOutput->hostStart;
        assignment.channels = bestOutput->channels;
        assignment.outputName = bestOutput->name;
        assignment.micCode = sampleMicSpec.raw;
        assignment.outputMicCode = bestOutput->code;
        assignment.outputId = bestOutput->outputId;
        assignment.applyBalance = sampleMicSpec.type == KitLib::MicSpec::Type::Direct && bestOutput->channels == 2;
        return assignment;
    }

    const auto requestedOutputId = StringsDictSingleton::nameOf(outputKey);
    Assignment skipped;
    skipped.channels = juce::jmax(1, sampleMicSpec.channels);
    skipped.micCode = sampleMicSpec.raw;
    skipped.warning = "Mic route override ignored for slot '" + slotId + "': output '" + requestedOutputId + "' was not found";
    return skipped;
}

MicRoutingPlan::Assignment MicRoutingPlan::resolveOverride(const juce::String& slotId, const KitLib::MicSpec& sampleMicSpec, const juce::String& outputId, KitLib::InstrumentType slotType) const {
    return this->resolveOverride(slotId, sampleMicSpec, StringsDictSingleton::getId(outputId.trim().toLowerCase()), slotType);
}

MicRoutingPlan::Assignment MicRoutingPlan::resolve(const juce::String& slotId, KitLib::InstrumentType slotType, const KitLib::MicSpec& micSpec) const {
    if (micSpec.type == KitLib::MicSpec::Type::Direct) {
        const auto key = makeAssignmentKey(slotId, micSpec.raw);
        auto it = this->directAssignments.find(key);
        if (it != this->directAssignments.end()) return it->second;

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
        assignment.outputMicCode = output.code;
        assignment.outputId = output.outputId;
        return assignment;
    }

    Assignment skipped;
    skipped.channels = juce::jmax(1, micSpec.channels);
    skipped.micCode = micSpec.raw;
    skipped.warning = "No routing-plan assignment for mic '" + micSpec.raw + "' in slot '" + slotId + "'";
    return skipped;
}

juce::String MicRoutingPlan::makeAssignmentKey(const juce::String& slotId, const juce::String& micCode) {
    return slotId.trim().toLowerCase() + "|" + micCode.trim().toLowerCase();
}

bool MicRoutingPlan::normalMicSpecsMatch(const KitLib::MicSpec& sampleMic, const KitLib::MicSpec& outputMic) {
    return micSpecsMatchOutput(sampleMic, outputMic);
}

bool MicRoutingPlan::isPrivateSelector(const MicInstrumentSelector& selector) {
    return selector.kind == MicInstrumentSelector::Kind::ExactSlotId
        || selector.kind == MicInstrumentSelector::Kind::WildcardPrefix
        || selector.kind == MicInstrumentSelector::Kind::CatalogKind
        || selector.kind == MicInstrumentSelector::Kind::CommaList
        || selector.kind == MicInstrumentSelector::Kind::CatalogFamily;
}

bool MicRoutingPlan::isGroupSelector(const MicInstrumentSelector& selector) {
    return selector.kind == MicInstrumentSelector::Kind::CatalogKind
        || selector.kind == MicInstrumentSelector::Kind::CommaList
        || selector.kind == MicInstrumentSelector::Kind::CatalogFamily;
}

void MicRoutingPlan::buildOutputs(const MicLayout& micLayout, const InstrumentCatalog& inputCatalog) {
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
        output.outputId = mic.id;
        output.outputKey = StringsDictSingleton::getId(output.outputId);
        output.spec = mic.specs;
        output.selector = parseMicInstrumentSelector(mic.instrument, this->catalog);
        this->outputs.push_back(std::move(output));
        cursor += juce::jmax(1, mic.specs.channels);
    }
    this->assignedHostChannelCount = cursor;
}

void MicRoutingPlan::allocateDirectAssignments(const std::vector<SlotRequest>& slotRequests, const InstrumentCatalog& inputCatalog) {
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
    for (const auto* request : own)       this->allocateDirectForSlot(*request, true, reservedOutputIndices);
    for (const auto* request : automatic) this->allocateDirectForSlot(*request, true, reservedOutputIndices);
    for (const auto* request : shared)    this->allocateDirectForSlot(*request, false, reservedOutputIndices);
}

void MicRoutingPlan::allocateDirectForSlot(const SlotRequest& request, bool allowPrivateClaim, std::set<int>& reservedOutputIndices) {
    std::set<juce::String> seenCodes;
    std::map<juce::String, Assignment> sharedVariantAssignments;
    for (const auto& micSpec : request.directMicSpecs) {
        if (micSpec.type != KitLib::MicSpec::Type::Direct) continue;
        if (!seenCodes.insert(micSpec.raw).second) continue;

        const auto variantGroupKey = micSpec.raw.substring(0, 3).toLowerCase();
        auto sharedAssignmentIt = sharedVariantAssignments.find(variantGroupKey);
        Assignment assignment = sharedAssignmentIt != sharedVariantAssignments.end() ? sharedAssignmentIt->second : Assignment {};
        if (assignment.matched()) assignment.micCode = micSpec.raw;
        if (!assignment.matched() && allowPrivateClaim) assignment = this->findPrivateDirectAssignment(request, micSpec, reservedOutputIndices);
        if (!assignment.matched()) assignment = this->findGroupDirectAssignment(request, micSpec);
        if (!assignment.matched()) {
            assignment.channels = juce::jmax(1, micSpec.channels);
            assignment.micCode = micSpec.raw;
            assignment.warning = "Skipping direct mic '" + micSpec.raw + "' for slot '" + request.slotId + "': no compatible direct output";
            this->warnings.push_back(assignment.warning);
        } else {
            const auto parsedOutputMic = KitLib::MicSpec::parseMicCode(assignment.outputMicCode);
            if (parsedOutputMic.ok() && parsedOutputMic.value.variant.isEmpty()) sharedVariantAssignments[variantGroupKey] = assignment;
        }
        this->directAssignments[makeAssignmentKey(request.slotId, micSpec.raw)] = assignment;
    }
}

MicRoutingPlan::Assignment MicRoutingPlan::findPrivateDirectAssignment(const SlotRequest& request, const KitLib::MicSpec& micSpec, std::set<int>& reservedOutputIndices) {
    const OutputEntry* best = nullptr;
    MicSelectorPriority bestPriority = MicSelectorPriority::None;

    for (const auto& output : this->outputs) {
        if (reservedOutputIndices.find(output.index) != reservedOutputIndices.end()) continue;
        if (!isPrivateSelector(output.selector)) continue;
        if (!directMicSpecsMatchExactly(micSpec, output.spec)) continue;

        const auto priority = matchMicInstrumentSelector(output.selector, request.slotId, request.slotType, this->catalog);
        if (priority == MicSelectorPriority::None) continue;
        if (best == nullptr || static_cast<int>(priority) > static_cast<int>(bestPriority)) {
            best = &output;
            bestPriority = priority;
        }
    }

    if (best == nullptr) return {};

    reservedOutputIndices.insert(best->index);
    return makeAssignment(*best, micSpec.raw);
}

MicRoutingPlan::Assignment MicRoutingPlan::findGroupDirectAssignment(const SlotRequest& request, const KitLib::MicSpec& micSpec) const {
    for (const auto& output : this->outputs) {
        if (!isGroupSelector(output.selector)) continue;
        if (!directMicSpecsCanUseGroupOutput(micSpec, output.spec)) continue;
        if (matchMicInstrumentSelector(output.selector, request.slotId, request.slotType, this->catalog) == MicSelectorPriority::None) continue;
        return makeAssignment(output, micSpec.raw, true);
    }
    return {};
}

MicRoutingPlan::Assignment MicRoutingPlan::makeAssignment(const OutputEntry& output, const juce::String& sampleMicCode, bool applyBalance) {
    Assignment assignment;
    assignment.globalStart = output.hostStart;
    assignment.channels = output.channels;
    assignment.outputName = output.name;
    assignment.micCode = sampleMicCode;
    assignment.outputMicCode = output.code;
    assignment.outputId = output.outputId;
    assignment.applyBalance = applyBalance;
    return assignment;
}
