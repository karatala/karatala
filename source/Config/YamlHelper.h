#pragma once


#include <juce_core/juce_core.h>
#include <map>
#include <string>
#include <vector>

/**
 * Minimal YAML-ish parser
 * Supports:
 *   - scalar key: value
 *   - list: - item1
 *           - item2
 *   - nested maps and lists (indentation-based)
 *   - comments (# to end of line)
 *   - inline arrays [1, 2, 3]
 *   - inline maps { code: bow, notes: [28] }
 * 
 * Not supported (will be ignored or partially parsed):
 *   - anchors/aliases (&, *)
 *   - complex types (!!int, !!float, etc.)
 *   - flow syntax except inline arrays and inline maps
 *   - multi-line strings (|, >)
 */
namespace YamlHelper {

    // Simple node abstraction for YAML values
    class YamlNode {
    public:
        enum Type { Null, Scalar, Sequence, Map };

        YamlNode(Type t = Null) : type(t) {}

        Type type;
        juce::String scalarValue;
        std::vector<YamlNode> sequenceValue;
        std::map<juce::String, YamlNode> mapValue;

        bool isScalar() const { return type == Scalar; }
        bool isSequence() const { return type == Sequence; }
        bool isMap() const { return type == Map; }
        bool isNull() const { return type == Null; }

        juce::String asString() const { return scalarValue; }
        int asInt() const { return scalarValue.getIntValue(); }
        double asDouble() const { return scalarValue.getDoubleValue(); }

        const YamlNode& operator[](const juce::String& key) const {
            static YamlNode nullNode;
            auto it = mapValue.find(key);
            return it != mapValue.end() ? it->second : nullNode;
        }

        const YamlNode& operator[](size_t idx) const {
            static YamlNode nullNode;
            return idx < sequenceValue.size() ? sequenceValue[idx] : nullNode;
        }

        size_t size() const {
            if (type == Sequence)
                return sequenceValue.size();
            if (type == Map)
                return mapValue.size();
            return 0;
        }

        std::vector<juce::String> keys() const {
            std::vector<juce::String> out;
            for (const auto& kv : mapValue)
                out.push_back(kv.first);
            return out;
        }
    };

    /**
     * Parses the supported YAML subset into a node tree.
     *
     * Supported forms include scalar maps, indentation-based maps/lists, comments, inline maps, and inline arrays.
     * Unsupported YAML features such as anchors, tags, and multiline strings are not part of this parser contract.
     * Returns the root node. If parsing fails, returns a Null node.
     */
    YamlNode parse(const juce::String& yaml);

}
