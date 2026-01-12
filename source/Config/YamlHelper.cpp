#include "YamlHelper.h"

using namespace juce;

namespace YamlHelper {

    static int getIndent(const String& line) {
        int i = 0;
        while (i < line.length() && line[i] == ' ')
            i++;
        return i;
    }

    static String removeComment(const String& line) {
        // Remove a comment starting at an unquoted '#' while preserving leading indentation
        bool inQuote = false;
        juce_wchar quoteChar = 0;
        int pos = -1;
        for (int i = 0; i < line.length(); ++i) {
            juce_wchar c = line[i];
            if ((c == '"' || c == '\'') && (i == 0 || line[i - 1] != '\\')) {
                if (!inQuote) {
                    inQuote = true;
                    quoteChar = c;
                } else if (quoteChar == c) {
                    inQuote = false;
                    quoteChar = 0;
                }
            }
            if (c == '#' && !inQuote) {
                pos = i;
                break;
            }
        }

        if (pos < 0)
            return line;

        // Preserve leading indentation but remove the comment and trailing whitespace.
        String out = line.substring(0, pos);
        int end = out.length();
        while (end > 0 && out[end - 1] == ' ')
            --end;
        return out.substring(0, end);
    }

    static String stripSurroundingQuotes(const String& s) {
        String t = s.trim();
        if (t.length() >= 2) {
            juce_wchar first = t[0];
            juce_wchar last = t[t.length() - 1];
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                return t.substring(1, t.length() - 1);
            }
        }
        return t;
    }

    static bool tryParseKeyValue(const String& line, String& outKey, String& outValue) {
        auto pos = line.indexOfChar(':');
        if (pos < 0)
            return false;
        outKey = line.substring(0, pos).trim();
        outValue = line.substring(pos + 1).trim();
        return true;
    }

    static bool startsWithDash(const String& line) {
        auto t = line.trimStart();
        return t.startsWithChar('-');
    }

    static String stripDashAndTrim(const String& line) {
        auto t = line.trimStart();
        if (t.startsWithChar('-')) {
            t = t.substring(1).trimStart();
        }
        return t;
    }

    // Parse an inline array like "[1, 2, 3]" or "[a, b, c]"
    static YamlNode parseInlineArray(const String& s) {
        YamlNode result(YamlNode::Sequence);
        String str = s.trim();
        if (!str.startsWithChar('[') || !str.endsWithChar(']')) {
            return result; // not an array
        }
        str = str.substring(1, str.length() - 1).trim();
        if (str.isEmpty())
            return result;

        StringArray parts;
        parts.addTokens(str, ",", "\"");
        for (auto& p : parts) {
            p = p.trim();
            if (!p.isEmpty()) {
                YamlNode item(YamlNode::Scalar);
                item.scalarValue = stripSurroundingQuotes(p);
                result.sequenceValue.push_back(item);
            }
        }
        return result;
    }

    // Forward declaration for recursive parsing
    static YamlNode parseBlock(const StringArray& lines, int& idx, int minIndent);

    /**
     * Parse a map starting at idx.
     * Reads lines at the current indentation until a line with lower indentation is found.
     */
    static YamlNode parseMap(const StringArray& lines, int& idx, int indent) {
        YamlNode result(YamlNode::Map);

        while (idx < lines.size()) {
            String line = lines[idx];
            int currentIndent = getIndent(line);

            // stop if we hit a line with less indentation (belongs to parent)
            if (currentIndent < indent)
                break;

            // skip empty lines
            if (line.trim().isEmpty()) {
                idx++;
                continue;
            }

            // if more indentation, it's a nested structure (shouldn't happen at map level)
            if (currentIndent > indent) {
                idx++;
                continue;
            }

            // parse key: value at this indentation
            String trimmed = line.trim();
            String key, value;

            if (!tryParseKeyValue(trimmed, key, value)) {
                idx++;
                continue;
            }

            // check if value starts a nested list or map
            if (value.isEmpty() || value == "-") {
                // nested list
                idx++;
                result.mapValue[key] = parseBlock(lines, idx, indent + 2);
            } else if (value.startsWithChar('[')) {
                // inline array
                result.mapValue[key] = parseInlineArray(value);
                idx++;
            } else {
                // scalar value
                YamlNode scalarNode(YamlNode::Scalar);
                scalarNode.scalarValue = stripSurroundingQuotes(value);
                result.mapValue[key] = scalarNode;
                idx++;
            }
        }

        return result;
    }

    /**
     * Parse a list (sequence) starting at idx.
     * Items are preceded by '-'.
     */
    static YamlNode parseList(const StringArray& lines, int& idx, int indent) {
        YamlNode result(YamlNode::Sequence);

        while (idx < lines.size()) {
            String line = lines[idx];
            int currentIndent = getIndent(line);

            // stop if indentation is less than expected
            if (currentIndent < indent)
                break;

            // skip empty lines
            if (line.trim().isEmpty()) {
                idx++;
                continue;
            }

            String trimmed = line.trim();

            // check for list item
            if (!startsWithDash(trimmed)) {
                // not a list item, stop
                break;
            }

            String itemStr = stripDashAndTrim(trimmed);

            // check if this item is a map (has key: value)
            String itemKey, itemValue;
            if (tryParseKeyValue(itemStr, itemKey, itemValue)) {
                // map item
                idx++;
                YamlNode itemMap(YamlNode::Map);
                // set the first key/value
                if (itemValue.startsWithChar('[')) {
                    itemMap.mapValue[itemKey] = parseInlineArray(itemValue);
                } else {
                    YamlNode n(YamlNode::Scalar);
                    n.scalarValue = stripSurroundingQuotes(itemValue);
                    itemMap.mapValue[itemKey] = n;
                }

                // read subsequent indented lines as children of this map
                int nextIndent = indent + 2;
                while (idx < lines.size()) {
                    String nextLine = lines[idx];
                    int nextLineIndent = getIndent(nextLine);

                    if (nextLineIndent <= indent || nextLine.trim().isEmpty())
                        break;
                    if (nextLineIndent == indent && startsWithDash(nextLine.trim()))
                        break;

                    if (nextLineIndent >= nextIndent) {
                        String nextTrimmed = nextLine.trim();

                        // if this is a nested list item (starts with '-') attach to the last key
                        if (startsWithDash(nextTrimmed)) {
                            if (!itemMap.mapValue.empty()) {
                                juce::String lastKey = itemMap.keys().back();
                                auto nestedList = parseList(lines, idx, nextIndent);
                                if (!nestedList.isNull()) {
                                    itemMap.mapValue[lastKey] = nestedList;
                                }
                                continue;
                            } else {
                                // no preceding key to attach to: treat as scalar list entry
                                auto nestedList = parseList(lines, idx, nextIndent);
                                if (!nestedList.isNull()) {
                                    // push this nested list as a scalar to the sequence
                                    result.sequenceValue.push_back(nestedList);
                                }
                                continue;
                            }
                        }

                        String nk, nv;
                        if (tryParseKeyValue(nextTrimmed, nk, nv)) {
                            // consume this child line
                            idx++;
                            if (nv.isEmpty()) {
                                // nested block under this key
                                itemMap.mapValue[nk] = parseBlock(lines, idx, nextIndent);
                            } else if (nv.startsWithChar('[')) {
                                itemMap.mapValue[nk] = parseInlineArray(nv);
                            } else {
                                YamlNode nn(YamlNode::Scalar);
                                nn.scalarValue = stripSurroundingQuotes(nv);
                                itemMap.mapValue[nk] = nn;
                            }
                        } else {
                            idx++;
                        }
                    } else {
                        break;
                    }
                }

                result.sequenceValue.push_back(itemMap);
            } else {
                // scalar item
                YamlNode scalarItem(YamlNode::Scalar);
                scalarItem.scalarValue = stripSurroundingQuotes(itemStr);
                result.sequenceValue.push_back(scalarItem);
                idx++;
            }
        }

        return result;
    }

    /**
     * Parse a block (could be a map or list depending on first non-empty line).
     */
    static YamlNode parseBlock(const StringArray& lines, int& idx, int minIndent) {
        // skip empty lines to find first content
        while (idx < lines.size() && lines[idx].trim().isEmpty()) {
            idx++;
        }

        if (idx >= lines.size()) {
            return YamlNode(YamlNode::Null);
        }

        String firstLine = lines[idx];
        if (startsWithDash(firstLine.trim())) {
            return parseList(lines, idx, minIndent);
        } else {
            return parseMap(lines, idx, minIndent);
        }
    }

    YamlNode parse(const String& yaml) {
        StringArray lines;
        lines.addLines(yaml);

        // preprocess: remove comments and empty lines
        for (int i = lines.size() - 1; i >= 0; --i) {
            String ln = removeComment(lines[i]);
            if (ln.trim().isEmpty()) {
                lines.remove(i);
            } else {
                lines.set(i, ln);
            }
        }

        if (lines.isEmpty()) {
            return YamlNode(YamlNode::Null);
        }

        int idx = 0;
        YamlNode result = parseBlock(lines, idx, 0);

        return result;
    }

}
