#pragma once

#include "../Config/Helpers.h"
#include "../Config/YamlHelper.h"
#include "KitLibModel.h"
#include "RoutingTypes.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <map>
#include <optional>
#include <algorithm>

struct KitLayout {
    using MidiNotes = std::vector<uint8_t>;
    struct InstrumentSlot {
        struct ArticulationTrigger {
            juce::String codeRaw; // center, rim, edge, bow, pedal, etc.
            MidiNotes notes; // midi notes triggering this articulation
            StringsDictSingleton::Id exprId = 0;
            StringsDictSingleton::Id nameId = 0;
            // KitLib::ArticulationSpec::ExpressionDimension::Type type = KitLib::ArticulationSpec::ExpressionDimension::Type::Unknown;
            // std::vector<juce::String> chokeWith; // list of choke group ids or instrument ids?
            // int priority = 0;
            //MAYBE: cc_conditions, crossfade?
            explicit ArticulationTrigger(juce::String codeRaw) : codeRaw(codeRaw) {
                auto wrapSegm = extractWrappedSegment(codeRaw.trim(), "[", "]");
                this->nameId = StringsDictSingleton::getId(wrapSegm.prefix);
                this->exprId = StringsDictSingleton::getId(wrapSegm.wrapped);
            }
        };
        struct ContinuousController {
            int number = -1;
            juce::String codeRaw; // pdl_opn_cls, zon_ctr_rim, zon_bow_edg, ...
            MidiNotes notes;
            
            explicit ContinuousController(int number, juce::String codeRaw) : number(number), codeRaw(codeRaw)
                , codeIdx(KitLib::ArticulationSpec::ExpressionDimension::stringToType(codeRaw))
            {}
            KitLib::ArticulationSpec::ExpressionDimension::Type codeIdx = KitLib::ArticulationSpec::ExpressionDimension::Type::Unknown;
        };

        juce::String id; // kick1, snare1, hihat1, ...
        juce::String display;
        KitLib::InstrumentSpec::Type type = KitLib::InstrumentSpec::Type::Unknown;
        std::optional<int> balance;
        std::optional<DirectMicOutPolicy> directMicOuts;
        std::vector<ArticulationTrigger> articulations;
        std::vector<ContinuousController> controllers;
        // juce::String chokeGroup;
    };

    juce::String id;
    juce::String name;
    juce::String description;
    juce::String vendor;
    juce::String version;

    std::vector<InstrumentSlot> instrumentSlots;

    static KitLayout getDefault();

    std::map<juce::String, InstrumentSlot*> instrumentsById;
    void rebuildIndices() {
        this->instrumentsById.clear();
        for (auto& i : this->instrumentSlots)
            this->instrumentsById[i.id] = &i;
    }
};

struct MicLayout {
    juce::String id;
    juce::String name;
    struct Mic {
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
    static ParseResult<MicLayout> parseYaml(const juce::String& yaml) {
        ParseResult<MicLayout> result;
        MicLayout layout;
        const auto root = YamlHelper::parse(yaml);
        if (root.isNull())
            return result.returnWithError("Unable to parse empty yaml");
        layout.id = root["id"].asString();
        layout.name = root["name"].asString();
        const auto& arr = root["mics"];
        if (arr.isSequence()) {
            for (size_t i = 0; i < arr.size(); ++i) {
                const auto& it = arr[i];
                MicLayout::Mic mic;
                mic.name = it["name"].asString();
                mic.instrument = it["instrument"].asString();
                mic.code = it["code"].asString();
                if (mic.code.isNotEmpty()) {
                    auto parsed = KitLib::MicSpec::parseMicCode(mic.code);
                    if (parsed.ok()) {
                        mic.specs = parsed.value;
                    } else {
                        result.addWarning("mics: invalid mic code for " + mic.name + ": " + parsed.error());
                    }
                    layout.mics.push_back(mic);
                } else
                    result.addWarning("mics: code is empty for " + mic.name);
            }
        }
        if (layout.mics.empty()) {
            return result.returnWithError("mics array is empty");
        }
        result.value = layout;
        return result;
    }
};

template <>
struct LayoutTraits<KitLayout> {
    static ParseResult<KitLayout> parseYaml(const juce::String& yaml) {
        ParseResult<KitLayout> result;

        const auto root = YamlHelper::parse(yaml);
        if (root.isNull())
            return result.returnWithError("Unable to parse empty yaml");

        KitLayout layout;
        layout.id = root["id"].asString();
        layout.name = root["name"].asString();
        layout.description = root["description"].asString();
        layout.vendor = root["vendor"].asString();
        layout.version = root["version"].asString();

        const auto& instsNode = root["instruments"];
        if (instsNode.isSequence()) {
            for (size_t i = 0; i < instsNode.size(); ++i) {
                const auto& instItem = instsNode[i];
                layout.instrumentSlots.emplace_back();
                auto& inst = layout.instrumentSlots.back();
                

                inst.id = instItem["id"].asString();
                inst.display = instItem["display"].asString();

                const auto typeText = instItem["type"].asString().trim();
                if (typeText.isNotEmpty()) {
                    inst.type = KitLib::InstrumentSpec::typeFromString(typeText);
                    if (inst.type == KitLib::InstrumentSpec::Type::Unknown)
                        result.addWarning("instruments: unknown type '" + typeText + "' for instrument " + inst.id);
                } else {
                    inst.type = KitLib::InstrumentSpec::typeFromString(inst.id.removeCharacters("0123456789"));
                }

                const auto balanceText = instItem["balance"].asString().trim();
                if (balanceText.isNotEmpty()) {
                    auto parsedBalance = parseBalanceValue(balanceText);
                    if (parsedBalance.has_value())
                        inst.balance = *parsedBalance;
                    else
                        result.addWarning("instruments: invalid balance '" + balanceText + "' for instrument " + inst.id);
                }

                const auto directMicOutsText = instItem["directMicOuts"].asString().trim();
                if (directMicOutsText.isNotEmpty()) {
                    auto parsedPolicy = directMicOutPolicyFromString(directMicOutsText);
                    if (parsedPolicy.has_value())
                        inst.directMicOuts = *parsedPolicy;
                    else
                        result.addWarning("instruments: invalid directMicOuts '" + directMicOutsText + "' for instrument " + inst.id);
                }

                const auto& artsNode = instItem["articulations"];
                if (artsNode.isSequence()) {
                    for (size_t j = 0; j < artsNode.size(); ++j) {
                        const auto& item = artsNode[j];
                        KitLayout::InstrumentSlot::ArticulationTrigger art(item["code"].asString());

                        const auto& notesNode = item["notes"];
                        if (notesNode.isSequence()) {
                            for (size_t k = 0; k < notesNode.size(); ++k) {
                                if (!isValidMidiValue(notesNode[k].asInt())) result.addWarning("articulations: invalid midi note " + juce::String(notesNode[k].asInt()) + " for articulation " + art.codeRaw + " in instrument " + inst.id);
                                else art.notes.push_back(static_cast<uint8_t>(notesNode[k].asInt()));
                            }
                        } else if (notesNode.isScalar() && !notesNode.asString().isEmpty()) {
                            if (!isValidMidiValue(notesNode.asInt())) result.addWarning("articulations: invalid midi note " + juce::String(notesNode.asInt()) + " for articulation " + art.codeRaw + " in instrument " + inst.id);
                            else art.notes.push_back(static_cast<uint8_t>(notesNode.asInt()));
                        }

                        inst.articulations.push_back(art);
                    }
                }

                const auto& ctrlsNode = instItem["controllers"];
                if (ctrlsNode.isSequence()) {
                    for (size_t j = 0; j < ctrlsNode.size(); ++j) {
                        const auto& item = ctrlsNode[j];
                        KitLayout::InstrumentSlot::ContinuousController cc(item["number"].asInt(), item["code"].asString());

                        const auto& notesNode = item["notes"];
                        if (notesNode.isSequence()) {
                            for (size_t k = 0; k < notesNode.size(); ++k) {
                                if (!isValidMidiValue(notesNode[k].asInt())) result.addWarning("controllers: invalid midi note " + juce::String(notesNode[k].asInt()) + " for controller " + juce::String(cc.number) + " in instrument " + inst.id);
                                else cc.notes.push_back(static_cast<uint8_t>(notesNode[k].asInt()));
                            }
                        } else if (notesNode.isScalar() && !notesNode.asString().isEmpty()) {
                            if (!isValidMidiValue(notesNode.asInt())) result.addWarning("controllers: invalid midi note " + juce::String(notesNode.asInt()) + " for controller " + juce::String(cc.number) + " in instrument " + inst.id);
                            else cc.notes.push_back(static_cast<uint8_t>(notesNode.asInt()));
                        }

                        inst.controllers.push_back(cc);
                    }
                }
            }
        }

        if (layout.instrumentSlots.empty()) {
            return result.returnWithError("instruments array is empty");
        }
        layout.rebuildIndices();
        result.value = layout;

        return result;
    }

    static juce::String toYaml(const KitLayout& layout) {
        juce::String yaml;
        yaml << "id: " << layout.id << "\n";
        yaml << "name: " << layout.name << "\n";
        if (layout.description.isNotEmpty())
            yaml << "description: " << layout.description << "\n";
        if (layout.vendor.isNotEmpty())
            yaml << "vendor: " << layout.vendor << "\n";
        if (layout.version.isNotEmpty())
            yaml << "version: " << layout.version << "\n";
        yaml << "instruments:\n";
        for (const auto& slot : layout.instrumentSlots) {
            yaml << "  - id: " << slot.id << "\n";
            if (slot.display.isNotEmpty())
                yaml << "    display: " << slot.display << "\n";
            if (slot.type != KitLib::InstrumentSpec::Type::Unknown)
                yaml << "    type: " << KitLib::InstrumentSpec::typeToString(slot.type) << "\n";
            if (slot.balance.has_value())
                yaml << "    balance: " << balanceValueToString(*slot.balance) << "\n";
            if (slot.directMicOuts.has_value())
                yaml << "    directMicOuts: " << directMicOutPolicyToString(*slot.directMicOuts) << "\n";
            if (!slot.articulations.empty()) {
                yaml << "    articulations:\n";
                for (const auto& art : slot.articulations) {
                    yaml << "      - code: " << art.codeRaw << "\n";
                    yaml << "        notes: [";
                    for (size_t i = 0; i < art.notes.size(); ++i) {
                        if (i > 0) yaml << ", ";
                        yaml << static_cast<int>(art.notes[i]);
                    }
                    yaml << "]\n";
                }
            }
            if (!slot.controllers.empty()) {
                yaml << "    controllers:\n";
                for (const auto& cc : slot.controllers) {
                    yaml << "      - number: " << cc.number << "\n";
                    yaml << "        code: " << cc.codeRaw << "\n";
                    yaml << "        notes: [";
                    for (size_t i = 0; i < cc.notes.size(); ++i) {
                        if (i > 0) yaml << ", ";
                        yaml << static_cast<int>(cc.notes[i]);
                    }
                    yaml << "]\n";
                }
            }
        }
        return yaml;
    }
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
