#pragma once

#include "../Config/Helpers.h"
#include "../Config/Parsing.h"
#include "InstrumentType.h"
#include "KitLibModel.h"
#include "RoutingTypes.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <juce_core/juce_core.h>
#include <map>
#include <optional>
#include <vector>

struct KitLayout {
    using MidiNotes = std::vector<uint8_t>;
    using ExpressionPointMap = std::map<juce::String, std::map<juce::String, uint8_t>>;

    static bool isSupportedExpressionPointCode(const juce::String& codeRaw) {
        const auto code = codeRaw.trim().toLowerCase();
        return code == "pdl_opn_cls" || code == "zon_ctr_rim" || code == "zon_bow_edg";
    }

    static std::vector<juce::String> getExpressionPointLabels(const juce::String& codeRaw) {
        const auto code = codeRaw.trim().toLowerCase();
        if (code == "pdl_opn_cls") return { "open", "half", "close", "tight", "pedal" };
        if (code == "zon_ctr_rim") return { "center", "rim" };
        if (code == "zon_bow_edg") return { "bow", "edge" };
        return {};
    }

    static ExpressionPointMap getDefaultExpressionPoints() {
        return {
            { "pdl_opn_cls", { { "open", uint8_t{24} }, { "half", uint8_t{52} }, { "close", uint8_t{78} }, { "tight", uint8_t{85} }, { "pedal", uint8_t{85} } } },
            { "zon_ctr_rim", { { "center", uint8_t{24} }, { "rim", uint8_t{85} } } },
            { "zon_bow_edg", { { "bow", uint8_t{24} }, { "edge", uint8_t{85} } } },
        };
    }

    static juce::String expressionPointLabelForArticulation(const KitLib::ArticulationSpec& artSpec, KitLib::ArticulationSpec::ExpressionDimension::Type type) {
        if (type == KitLib::ArticulationSpec::ExpressionDimension::Type::PedalOpenClose) {
            if (artSpec.exprId != 0)
                return StringsDictSingleton::nameOf(artSpec.exprId).trim().toLowerCase();
            return StringsDictSingleton::nameOf(artSpec.nameId).trim().toLowerCase();
        }
        if (type == KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneCenterRim
         || type == KitLib::ArticulationSpec::ExpressionDimension::Type::ZoneBowEdge
        ) {
            return StringsDictSingleton::nameOf(artSpec.nameId).trim().toLowerCase();
        }
        return {};
    }

    static std::optional<uint8_t> getExpressionPoint(const ExpressionPointMap& points, const juce::String& codeRaw, const juce::String& label) {
        const auto codeIt = points.find(codeRaw.trim().toLowerCase());
        if (codeIt == points.end())
            return std::nullopt;
        const auto labelIt = codeIt->second.find(label.trim().toLowerCase());
        if (labelIt == codeIt->second.end())
            return std::nullopt;
        return labelIt->second;
    }
    struct InstrumentSlot {
        struct ArticulationSlot {
            juce::String codeRaw; // center, rim, edge, bow, pedal, etc.
            MidiNotes notes; // midi notes triggering this articulation
            StringsDictSingleton::Id exprId = 0;
            StringsDictSingleton::Id nameId = 0;
            // KitLib::ArticulationSpec::ExpressionDimension::Type type = KitLib::ArticulationSpec::ExpressionDimension::Type::Unknown;
            // std::vector<juce::String> chokeWith; // list of choke group ids or instrument ids?
            // int priority = 0;
            //MAYBE: cc_conditions, crossfade?
            explicit ArticulationSlot(juce::String raw) : codeRaw(raw) {
                auto wrapSegm = Parsing::extractWrappedSegment(raw.trim(), "[", "]");
                this->nameId = StringsDictSingleton::getId(wrapSegm.prefix);
                this->exprId = StringsDictSingleton::getId(wrapSegm.wrapped);
            }
        };
        struct ContinuousController {
            int number = -1;
            juce::String codeRaw; // pdl_opn_cls, zon_ctr_rim, zon_bow_edg, ...
            MidiNotes notes;
            
            explicit ContinuousController(int ccNumber, juce::String raw) : number(ccNumber), codeRaw(raw)
                , codeIdx(KitLib::ArticulationSpec::ExpressionDimension::stringToType(raw))
            {}
            KitLib::ArticulationSpec::ExpressionDimension::Type codeIdx = KitLib::ArticulationSpec::ExpressionDimension::Type::Unknown;
        };

        juce::String id; // kick1, snare1, hihat1, ...
        juce::String display;
        KitLib::InstrumentType type = KitLib::InstrumentType::Unknown;
        std::optional<int> balance;
        std::optional<DirectMicOutPolicy> directMicOuts;
        ExpressionPointMap expressionPoints;
        std::vector<ArticulationSlot> articulations;
        std::vector<ContinuousController> controllers;
        // juce::String chokeGroup;
    };

    juce::String id;
    juce::String name;
    juce::String description;
    juce::String vendor;
    juce::String version;
    ExpressionPointMap expressionPoints;

    std::vector<InstrumentSlot> instrumentSlots;

    static KitLayout getDefault();

    std::map<juce::String, InstrumentSlot*> instrumentsById;
    void rebuildIndices() {
        this->instrumentsById.clear();
        for (auto& instrumentSlot : this->instrumentSlots)
            this->instrumentsById[instrumentSlot.id] = &instrumentSlot;
    }
};

struct MicLayout {
    juce::String id;
    juce::String name;
    struct Mic {
        juce::String id;
        juce::String name;
        juce::String instrument;
        juce::String code;

        KitLib::MicSpec specs;
    };
    std::vector<Mic> mics;

    int getTotalChannelCount() const {
        int total = 0;
        for (const auto& mic : this->mics)
            total += std::max(1, mic.specs.channels);
        return total;
    }

    static MicLayout getDefault();

    void rebuildIndices() {
    }
};

//////////////////// Generic manager ////////////////////

template <typename LayoutType>
struct LayoutTraits;

template <>
struct LayoutTraits<MicLayout> {
    /** Parses mic layout YAML. See example in source/Engine/LayoutManager.cpp MicLayout::getDefault() */
    static ParseResult<MicLayout> parseYaml(const juce::String& yaml);
};

template <>
struct LayoutTraits<KitLayout> {
    /** Parses kit layout YAML. See example in source/Engine/LayoutManager.cpp KitLayout::getDefault() */
    static ParseResult<KitLayout> parseYaml(const juce::String& yaml);

    /** Writes kit layout YAML in normalized field order. */
    static juce::String toYaml(const KitLayout& layout);
};

template <typename LayoutType>
class LayoutManager {
public:
    void initDefault() {
        this->defautLayout = LayoutType::getDefault();
    }

    ParseResult<int> scanLayouts(const juce::File& directory) {
        ParseResult<int> result;
        result.value = 0;
        if (!directory.exists() || !directory.isDirectory()) {
            return result.returnWithError("Folder not found: " + directory.getFullPathName());
        }
        layouts.clear();

        const auto files = directory.findChildFiles(juce::File::findFiles, false, "*.yml;*.yaml");

        for (const auto& file : files) {
            const auto yaml = file.loadFileAsString();
            const auto parsed = LayoutTraits<LayoutType>::parseYaml(yaml);
            if (parsed.ok()) {
                auto layout = parsed.value;
                layout.id = file.getFileNameWithoutExtension();
                layout.rebuildIndices();
                layoutsById[layout.id] = layout;
                layouts.push_back(layout);
                result.value++;
            } else {
                result.addWarning(parsed.error() + " " + file.getFullPathName());
            }
        }
        return result;
    }

    std::vector<LayoutType> getLayouts() const {
        return layouts;
    }

    const LayoutType* getLayoutById(const juce::String& id) const {
        auto it = layoutsById.find(id);
        return it != layoutsById.end() ? &it->second : nullptr;
    }
    const LayoutType getDefault() const { return this->defautLayout; }

protected:
    std::vector<LayoutType> layouts;
    std::map<juce::String, LayoutType> layoutsById;
    LayoutType defautLayout;
};

using MicLayoutManager = LayoutManager<MicLayout>;
using KitLayoutManager = LayoutManager<KitLayout>;
