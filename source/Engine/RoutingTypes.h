#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include <cstdlib>

enum class DirectMicOutPolicy {
    Own,
    Auto,
    Shared,
};

inline std::optional<DirectMicOutPolicy> directMicOutPolicyFromString(const juce::String& raw) {
    const auto value = raw.trim().toLowerCase();
    if (value == "own") return DirectMicOutPolicy::Own;
    if (value == "auto") return DirectMicOutPolicy::Auto;
    if (value == "shared") return DirectMicOutPolicy::Shared;
    return std::nullopt;
}

inline juce::String directMicOutPolicyToString(DirectMicOutPolicy policy) {
    switch (policy) {
        case DirectMicOutPolicy::Own:    return "own";
        case DirectMicOutPolicy::Auto:   return "auto";
        case DirectMicOutPolicy::Shared: return "shared";
    }
    return "shared";
}

inline std::optional<int> parseBalanceValue(const juce::String& raw) {
    const auto value = raw.trim().toUpperCase();
    if (value == "C0") return 0;
    if (value.length() < 2) return std::nullopt;

    const auto side = value[0];
    if (side != 'L' && side != 'R') return std::nullopt;

    const auto amountText = value.substring(1);
    if (amountText.isEmpty()) return std::nullopt;

    for (int i = 0; i < amountText.length(); ++i) {
        const auto c = amountText[i];
        if (c < '0' || c > '9') return std::nullopt;
    }

    const int amount = amountText.getIntValue();
    if (amount < 1 || amount > 127) return std::nullopt;
    return side == 'L' ? -amount : amount;
}

inline juce::String balanceValueToString(int balance) {
    const int clamped = juce::jlimit(-127, 127, balance);
    if (clamped == 0) return "C0";
    return juce::String(clamped < 0 ? "L" : "R") + juce::String(std::abs(clamped));
}