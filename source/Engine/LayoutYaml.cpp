#include "LayoutManager.h"

#include "../Config/YamlHelper.h"
#include <array>
#include <set>

static std::optional<uint8_t> parseExpressionPointValue(const YamlHelper::YamlNode& node, ParseResult<KitLayout>& result, const juce::String& context, const juce::String& code, const juce::String& label) {
    if (!node.isScalar()) {
        result.addWarning(context + ": expressionPoints." + code + "." + label + " must be a scalar integer from 0 to 127");
        return std::nullopt;
    }

    const auto valueText = node.asString().trim();
    if (valueText.isEmpty() || !valueText.containsOnly("0123456789")) {
        result.addWarning(context + ": expressionPoints." + code + "." + label + " must be an integer from 0 to 127; got '" + node.asString() + "'");
        return std::nullopt;
    }

    const auto value = valueText.getLargeIntValue();
    if (value < 0 || value > 127) {
        result.addWarning(context + ": expressionPoints." + code + "." + label + " must be between 0 and 127; got " + valueText);
        return std::nullopt;
    }

    return static_cast<uint8_t>(value);
}

static KitLayout::ExpressionPointMap parseExpressionPointsNode(const YamlHelper::YamlNode& node, ParseResult<KitLayout>& result, const juce::String& context) {
    KitLayout::ExpressionPointMap points;
    if (node.isNull())
        return points;
    if (!node.isMap()) {
        result.addWarning(context + ": expressionPoints must be a map");
        return points;
    }

    for (const auto& codeKey : node.keys()) {
        const auto code = codeKey.trim().toLowerCase();
        if (!KitLayout::isSupportedExpressionPointCode(code)) {
            result.addWarning(context + ": unsupported expressionPoints code '" + codeKey + "'");
            continue;
        }

        const auto& codeNode = node[codeKey];
        if (!codeNode.isMap()) {
            result.addWarning(context + ": expressionPoints." + codeKey + " must be a map");
            continue;
        }

        const auto labels = KitLayout::getExpressionPointLabels(code);
        for (const auto& labelKey : codeNode.keys()) {
            const auto label = labelKey.trim().toLowerCase();
            if (!vectorContains(labels, label)) {
                result.addWarning(context + ": unsupported expressionPoints label '" + labelKey + "' for " + code);
                continue;
            }

            const auto& valueNode = codeNode[labelKey];
            if (const auto value = parseExpressionPointValue(valueNode, result, context, code, label); value.has_value())
                points[code][label] = *value;
        }
    }
    return points;
}

static void validateExpressionPoints(const KitLayout::ExpressionPointMap& points, ParseResult<KitLayout>& result, const juce::String& context) {
    auto pdlIt = points.find("pdl_opn_cls");
    if (pdlIt == points.end())
        return;

    const auto& pdl = pdlIt->second;
    auto getValue = [&pdl](const juce::String& label) -> std::optional<int> {
        auto it = pdl.find(label);
        if (it == pdl.end()) return std::nullopt;
        return static_cast<int>(it->second);
    };
    auto warnIfNotAscending = [&](const juce::String& lowerLabel, const juce::String& higherLabel) {
        const auto lower = getValue(lowerLabel);
        const auto higher = getValue(higherLabel);
        if (lower.has_value() && higher.has_value() && !(*lower < *higher))
            result.addWarning(context + ": pdl_opn_cls values should satisfy open < half < close < tight; got " + lowerLabel + "=" + juce::String(*lower) + ", " + higherLabel + "=" + juce::String(*higher));
    };

    warnIfNotAscending("open", "half");
    warnIfNotAscending("half", "close");
    warnIfNotAscending("close", "tight");

    const auto tight = getValue("tight");
    const auto pedal = getValue("pedal");
    if (tight.has_value() && pedal.has_value() && *pedal > *tight)
        result.addWarning(context + ": pdl_opn_cls pedal should be <= tight; got pedal=" + juce::String(*pedal) + ", tight=" + juce::String(*tight));
}

static void validateControllerNotes(const KitLayout::InstrumentSlot& slot, ParseResult<KitLayout>& result) {
    std::array<bool, 128> articulationNotes {};
    for (const auto& art : slot.articulations) {
        for (const auto note : art.notes) {
            if (note < articulationNotes.size())
                articulationNotes[static_cast<size_t>(note)] = true;
        }
    }

    for (const auto& cc : slot.controllers) {
        if (cc.notes.empty()) {
            result.addWarning("controllers: notes are required for controller " + cc.codeRaw + " in instrument " + slot.id);
            continue;
        }
        for (const auto note : cc.notes) {
            if (note >= articulationNotes.size() || !articulationNotes[static_cast<size_t>(note)])
                result.addWarning("controllers: note " + juce::String(static_cast<int>(note)) + " for controller " + cc.codeRaw + " in instrument " + slot.id + " is not assigned to any articulation");
        }
    }
}

static void appendExpressionPointsYaml(juce::String& yaml, const KitLayout::ExpressionPointMap& points, const juce::String& indent) {
    if (points.empty())
        return;

    yaml << indent << "expressionPoints:\n";
    for (const auto& [code, labelValues] : points) {
        yaml << indent << "  " << code << ": { ";
        bool first = true;
        std::vector<juce::String> orderedLabels = KitLayout::getExpressionPointLabels(code);
        for (const auto& [label, value] : labelValues) {
            if (!vectorContains(orderedLabels, label)) orderedLabels.push_back(label);
        }
        for (const auto& label : orderedLabels) {
            auto it = labelValues.find(label);
            if (it == labelValues.end()) continue;
            if (!first) yaml << ", ";
            first = false;
            yaml << label << ": " << static_cast<int>(it->second);
        }
        yaml << " }\n";
    }
}

ParseResult<MicLayout> LayoutTraits<MicLayout>::parseYaml(const juce::String& yaml) {
    ParseResult<MicLayout> result;
    MicLayout layout;
    const auto root = YamlHelper::parse(yaml);
    if (root.isNull())
        return result.returnWithError("Unable to parse empty yaml");
    layout.id = root["id"].asString();
    layout.name = root["name"].asString();
    const auto& arr = root["mics"];
    std::set<juce::String> micIds;
    if (arr.isSequence()) {
        for (size_t micIdx = 0; micIdx < arr.size(); ++micIdx) {
            const auto& it = arr[micIdx];
            MicLayout::Mic mic;
            mic.id = it["id"].asString().trim().toLowerCase();
            mic.name = it["name"].asString();
            mic.instrument = it["instrument"].asString();
            mic.code = it["code"].asString();
            if (mic.id.isEmpty()) return result.returnWithError("mics: id is empty for " + mic.name);
            if (mic.code.isNotEmpty()) {
                auto parsed = KitLib::MicSpec::parseMicCode(mic.code);
                if (parsed.ok()) {
                    mic.specs = parsed.value;
                } else {
                    result.addWarning("mics: invalid mic code for " + mic.name + ": " + parsed.error());
                }
                if (!micIds.insert(mic.id).second) return result.returnWithError("mics: duplicate id '" + mic.id + "'");
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

ParseResult<KitLayout> LayoutTraits<KitLayout>::parseYaml(const juce::String& yaml) {
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
    layout.expressionPoints = parseExpressionPointsNode(root["expressionPoints"], result, "layout");
    validateExpressionPoints(layout.expressionPoints, result, "layout");

    const auto& instsNode = root["instruments"];
    if (instsNode.isSequence()) {
        for (size_t instrumentIdx = 0; instrumentIdx < instsNode.size(); ++instrumentIdx) {
            const auto& instNode = instsNode[instrumentIdx];
            layout.instrumentSlots.emplace_back();
            auto& inst = layout.instrumentSlots.back();
            

            inst.id = instNode["id"].asString();
            inst.display = instNode["display"].asString();

            const auto typeText = instNode["type"].asString().trim();
            if (typeText.isNotEmpty()) {
                inst.type = KitLib::instrumentTypeFromString(typeText);
                if (inst.type == KitLib::InstrumentType::Unknown)
                    result.addWarning("instruments: unknown type '" + typeText + "' for instrument " + inst.id);
            } else {
                inst.type = KitLib::instrumentTypeFromString(inst.id.removeCharacters("0123456789"));
            }

            const auto balanceText = instNode["balance"].asString().trim();
            if (balanceText.isNotEmpty()) {
                auto parsedBalance = parseBalanceValue(balanceText);
                if (parsedBalance.has_value())
                    inst.balance = *parsedBalance;
                else
                    result.addWarning("instruments: invalid balance '" + balanceText + "' for instrument " + inst.id);
            }

            const auto directMicOutsText = instNode["directMicOuts"].asString().trim();
            if (directMicOutsText.isNotEmpty()) {
                auto parsedPolicy = directMicOutPolicyFromString(directMicOutsText);
                if (parsedPolicy.has_value())
                    inst.directMicOuts = *parsedPolicy;
                else
                    result.addWarning("instruments: invalid directMicOuts '" + directMicOutsText + "' for instrument " + inst.id);
            }

            inst.expressionPoints = parseExpressionPointsNode(instNode["expressionPoints"], result, "instrument " + inst.id);
            validateExpressionPoints(inst.expressionPoints, result, "instrument " + inst.id);

            const auto& artsNode = instNode["articulations"];
            if (artsNode.isSequence()) {
                for (size_t articulationIdx = 0; articulationIdx < artsNode.size(); ++articulationIdx) {
                    const auto& artNode = artsNode[articulationIdx];
                    KitLayout::InstrumentSlot::ArticulationSlot art(artNode["code"].asString());

                    const auto& notesNode = artNode["notes"];
                    if (notesNode.isSequence()) {
                        for (size_t noteIdx = 0; noteIdx < notesNode.size(); ++noteIdx) {
                            if (!isValidMidiValue(notesNode[noteIdx].asInt())) result.addWarning("articulations: invalid midi note " + juce::String(notesNode[noteIdx].asInt()) + " for articulation " + art.codeRaw + " in instrument " + inst.id);
                            else art.notes.push_back(static_cast<uint8_t>(notesNode[noteIdx].asInt()));
                        }
                    } else if (notesNode.isScalar() && !notesNode.asString().isEmpty()) {
                        if (!isValidMidiValue(notesNode.asInt())) result.addWarning("articulations: invalid midi note " + juce::String(notesNode.asInt()) + " for articulation " + art.codeRaw + " in instrument " + inst.id);
                        else art.notes.push_back(static_cast<uint8_t>(notesNode.asInt()));
                    }

                    inst.articulations.push_back(art);
                }
            }

            const auto& ctrlsNode = instNode["controllers"];
            if (ctrlsNode.isSequence()) {
                for (size_t controllerIdx = 0; controllerIdx < ctrlsNode.size(); ++controllerIdx) {
                    const auto& controllerNode = ctrlsNode[controllerIdx];
                    KitLayout::InstrumentSlot::ContinuousController cc(controllerNode["number"].asInt(), controllerNode["code"].asString());

                    const auto& notesNode = controllerNode["notes"];
                    if (notesNode.isSequence()) {
                        for (size_t noteIdx = 0; noteIdx < notesNode.size(); ++noteIdx) {
                            if (!isValidMidiValue(notesNode[noteIdx].asInt())) result.addWarning("controllers: invalid midi note " + juce::String(notesNode[noteIdx].asInt()) + " for controller " + juce::String(cc.number) + " in instrument " + inst.id);
                            else cc.notes.push_back(static_cast<uint8_t>(notesNode[noteIdx].asInt()));
                        }
                    } else if (notesNode.isScalar() && !notesNode.asString().isEmpty()) {
                        if (!isValidMidiValue(notesNode.asInt())) result.addWarning("controllers: invalid midi note " + juce::String(notesNode.asInt()) + " for controller " + juce::String(cc.number) + " in instrument " + inst.id);
                        else cc.notes.push_back(static_cast<uint8_t>(notesNode.asInt()));
                    }

                    inst.controllers.push_back(cc);
                }
            }
            validateControllerNotes(inst, result);
        }
    }

    if (layout.instrumentSlots.empty()) {
        return result.returnWithError("instruments array is empty");
    }
    layout.rebuildIndices();
    result.value = layout;

    return result;
}

juce::String LayoutTraits<KitLayout>::toYaml(const KitLayout& layout) {
    juce::String yaml;
    yaml << "id: " << layout.id << "\n";
    yaml << "name: " << layout.name << "\n";
    if (layout.description.isNotEmpty())
        yaml << "description: " << layout.description << "\n";
    if (layout.vendor.isNotEmpty())
        yaml << "vendor: " << layout.vendor << "\n";
    if (layout.version.isNotEmpty())
        yaml << "version: " << layout.version << "\n";
    appendExpressionPointsYaml(yaml, layout.expressionPoints, "");
    yaml << "instruments:\n";
    for (const auto& slot : layout.instrumentSlots) {
        yaml << "  - id: " << slot.id << "\n";
        if (slot.display.isNotEmpty())
            yaml << "    display: " << slot.display << "\n";
        if (slot.type != KitLib::InstrumentType::Unknown)
            yaml << "    type: " << KitLib::instrumentTypeToString(slot.type) << "\n";
        if (slot.balance.has_value())
            yaml << "    balance: " << balanceValueToString(*slot.balance) << "\n";
        if (slot.directMicOuts.has_value())
            yaml << "    directMicOuts: " << directMicOutPolicyToString(*slot.directMicOuts) << "\n";
        appendExpressionPointsYaml(yaml, slot.expressionPoints, "    ");
        if (!slot.articulations.empty()) {
            yaml << "    articulations:\n";
            for (const auto& art : slot.articulations) {
                yaml << "      - code: " << art.codeRaw << "\n";
                yaml << "        notes: [";
                for (size_t noteIdx = 0; noteIdx < art.notes.size(); ++noteIdx) {
                    if (noteIdx > 0) yaml << ", ";
                    yaml << static_cast<int>(art.notes[noteIdx]);
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
                for (size_t noteIdx = 0; noteIdx < cc.notes.size(); ++noteIdx) {
                    if (noteIdx > 0) yaml << ", ";
                    yaml << static_cast<int>(cc.notes[noteIdx]);
                }
                yaml << "]\n";
            }
        }
    }
    return yaml;
}
