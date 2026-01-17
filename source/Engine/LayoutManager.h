#pragma once

#include "../Config/Helpers.h"
#include "../Config/YamlHelper.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <map>

struct KitLayout {
    struct Instrument {
        struct Articulation {
            juce::String type; // center, rim, edge, bow, pedal, etc.
            juce::String display;
            std::vector<int> notes; // midi notes triggering this articulation

            // std::vector<juce::String> chokeWith; // list of choke group ids or instrument ids
            // int priority = 0;
            //MAYBE: cc_conditions, crossfade?
        };
        struct CC {
            int id = -1; // MIDI CC
            juce::String code; // pedal-open-close, zone-center-rim, zone-bow-edge
            std::vector<int> notes; // midi notes triggering this articulation
        };

        juce::String id; // kick1, snare1, hihat1, ...
        juce::String display;
        juce::String type; // drum, cymbal, snare, hihat
        std::vector<Articulation> articulations;
        std::vector<CC> controllers;

        // juce::String outputGroup;
        // std::vector<juce::String> mics;
        // juce::String chokeGroup;
    };

    juce::String id;
    juce::String name;
    juce::String description;
    juce::String vendor;
    juce::String version;

    std::vector<Instrument> instruments;

    static KitLayout getDefault();

    std::map<juce::String, Instrument*> instrumentsById;
    void rebuildIndices() {
        instrumentsById.clear();
        for (auto& i : instruments)
            instrumentsById[i.id] = &i;
    }
};

struct MicLayout {
    juce::String id;
    juce::String name;
    struct Mic {
        juce::String name;
        juce::String instrument;
        juce::String code;
    };
    std::vector<Mic> mics;

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
        const auto arr = root["mics"];
        if (arr.isSequence()) {
            for (size_t i = 0; i < arr.size(); ++i) {
                const auto& it = arr[i];
                MicLayout::Mic mic;
                mic.name = it["name"].asString();
                mic.instrument = it["instrument"].asString();
                mic.code = it["code"].asString();
                if (mic.code.isNotEmpty())
                    layout.mics.push_back(mic);
                else
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

        const auto& instNode = root["instruments"];
        if (instNode.isSequence()) {
            for (size_t i = 0; i < instNode.size(); ++i) {
                const auto& instItem = instNode[i];
                KitLayout::Instrument inst;

                inst.id = instItem["id"].asString();
                inst.display = instItem["display"].asString();
                inst.type = instItem["type"].asString();
                // inst.outputGroup = instItem["output_group"].asString();

                // const auto& notesNode = instItem["notes"];
                // if (notesNode.isSequence()) {
                //     for (size_t j = 0; j < notesNode.size(); ++j) {
                //         inst.notes.push_back(notesNode[j].asInt());
                //     }
                // } else if (notesNode.isScalar() && !notesNode.asString().isEmpty()) {
                //     inst.notes.push_back(notesNode.asInt());
                // }

                const auto& artsNode = instItem["articulations"];
                if (artsNode.isSequence()) {
                    for (size_t j = 0; j < artsNode.size(); ++j) {
                        const auto& artItem = artsNode[j];
                        KitLayout::Instrument::Articulation art;

                        art.type = artItem["type"].asString();
                        art.display = artItem["display"].asString();

                        const auto& artNotesNode = artItem["notes"];
                        if (artNotesNode.isSequence()) {
                            for (size_t k = 0; k < artNotesNode.size(); ++k) {
                                art.notes.push_back(artNotesNode[k].asInt());
                            }
                        } else if (artNotesNode.isScalar() && !artNotesNode.asString().isEmpty()) {
                            art.notes.push_back(artNotesNode.asInt());
                        }

                        inst.articulations.push_back(art);
                    }
                }

                const auto& ctrlsNode = instItem["controllers"];
                if (ctrlsNode.isSequence()) {
                    for (size_t j = 0; j < ctrlsNode.size(); ++j) {
                        const auto& ctrlItem = ctrlsNode[j];
                        KitLayout::Instrument::CC cd;
                        cd.id = ctrlItem["id"].asInt();
                        cd.code = ctrlItem["code"].asString();
                        inst.controllers.push_back(cd);
                    }
                }

                layout.instruments.push_back(inst);
            }
        }

        if (layout.instruments.empty()) {
            return result.returnWithError("instruments array is empty");
        }
        layout.rebuildIndices();
        result.value = layout;

        return result;
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
        mLayouts.clear();

        const auto files = directory.findChildFiles(juce::File::findFiles, false, "*.yml;*.yaml");

        for (const auto& file : files) {
            const auto yaml = file.loadFileAsString();
            const auto parsed = LayoutTraits<LayoutType>::parseYaml(yaml);
            if (parsed.ok()) {
                auto layout = parsed.value;
                layout.id = file.getFileNameWithoutExtension();
                layout.rebuildIndices();
                layoutsById[layout.id] = layout;
                mLayouts.push_back(layout);
                result.value++;
            } else {
                result.addWarning(parsed.error() + " " + file.getFullPathName());
            }
        }
        return result;
    }

    std::vector<LayoutType> getLayouts() const {
        return mLayouts;
    }

    const LayoutType* getLayoutById(const juce::String& id) const {
        auto it = layoutsById.find(id);
        return it != layoutsById.end() ? &it->second : nullptr;
    }
    const LayoutType getDefault() const { return this->defautLayout; }

protected:
    std::vector<LayoutType> mLayouts;
    std::map<juce::String, LayoutType> layoutsById;
    LayoutType defautLayout;
};

using MicLayoutManager = LayoutManager<MicLayout>;
using KitLayoutManager = LayoutManager<KitLayout>;