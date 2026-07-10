# ✅ COMPLETED: Refactoring: Dynamic Output Buses from MicLayout Deeper per-instance VST reload work is tracked in TODO0X.

## Problem

`PluginProcessor` hardcodes 16 stereo output buses ("Out 01"–"Out 16") in its constructor.
`DrumKitEngine::applyMicLayout()` hardcodes `expectedHostChannels = 32` to match.
Neither is derived from the actual `MicLayout` configuration.

The mic layout YAML already defines named mics with channel counts (mono/stereo via mic code).
Output bus names and formats should be generated from this layout so DAW routing labels match the mic names.

Since JUCE requires `BusesProperties` at `AudioProcessor` construction time, and the user confirmed mic layout changes require a restart, this is a constructor-time decision.

---

## Current Flow

```
PluginProcessor()
  ├─ AudioProcessor(BusesProperties with 16 hardcoded stereo outputs)
  ├─ new DrumKitEngine()
  └─ engine->init(treeState)
        ├─ initGlobalSettings()   → reads config.yaml → globalConfig.micLayout
        ├─ initDefaultLayouts()   → registers default MicLayout
        ├─ initMicLayouts()       → scans mic_layouts/ dir
        └─ useState(treeState)    → useMicLayout() → applyMicLayout()
                                       └─ hardcoded expectedHostChannels = 32
```

**Key issue:** The engine resolves the mic layout *after* the processor has already committed its bus topology.

---

## Proposed Flow

```
PluginProcessor()
  ├─ resolvedMicLayout = resolveMicLayout()       ← NEW static/free function
  ├─ AudioProcessor(buildBusesFromMicLayout(resolvedMicLayout))  ← NEW
  ├─ new DrumKitEngine()
  └─ engine->init(treeState, resolvedMicLayout)    ← pass resolved layout
        └─ applyMicLayout() keeps expectedHostChannels = 32 (always fixed)
              If micLayout uses fewer than 32 channels, remaining channels
              become mono AUX buses (AUX 1, AUX 2, …) in buildBusesFromMicLayout()
```

---

## Implementation Steps

### Step 1: Extract mic-layout resolution into a reusable static function

Create a static/free function that replicates the config-loading logic just enough to resolve the active `MicLayout` before the engine is constructed:

```cpp
// In DrumKitEngine.h or a new MicLayoutResolver.h
static MicLayout resolveMicLayoutFromConfig();
```

This function should:
1. Determine `dataPath` (env var `EDRUMS_DATA_PATH` → fallback `commonDocumentsDirectory/PRODUCT_NAME`)
2. Read `dataPath/config.yaml` → parse `micLayout` field
3. If a custom layout name is specified, scan `dataPath/mic_layouts/` and find it
4. Otherwise return `MicLayout::getDefault()`

This duplicates part of `initGlobalSettings()` + `initMicLayouts()` but is intentionally lightweight — it only needs the mic layout, not the full engine init.

### Step 2: Create `buildBusesFromMicLayout()`

A static helper that converts a `MicLayout` into `BusesProperties`, always totalling exactly 32 host channels.
Mic layout entries fill buses first; any remaining channels become mono AUX buses:

```cpp
static constexpr int TOTAL_HOST_CHANNELS = 32;

// In PluginProcessor.cpp or a shared header
static juce::AudioProcessor::BusesProperties buildBusesFromMicLayout(const MicLayout& layout) {
    BusesProperties props;
    int usedChannels = 0;

    for (const auto& mic : layout.mics) {
        int ch = mic.specs.channels;
        if (ch <= 0) ch = 1; // fallback mono
        if (usedChannels + ch > TOTAL_HOST_CHANNELS) break; // safety cap

        juce::String busName = mic.name;
        auto channelSet = (ch == 2)
            ? juce::AudioChannelSet::stereo()
            : juce::AudioChannelSet::mono();

        props = props.withOutput(busName, channelSet, true);
        usedChannels += ch;
    }

    // Pad remaining channels with mono AUX buses
    int auxIndex = 1;
    while (usedChannels < TOTAL_HOST_CHANNELS) {
        props = props.withOutput("AUX " + juce::String(auxIndex++),
                                 juce::AudioChannelSet::mono(), true);
        usedChannels += 1;
    }

    return props;
}
```

This gives each bus the mic's display `name` (e.g. "Overheads", "Kick In", "Snare Top") and the correct channel format.
Unused capacity is exposed as mono AUX buses so the host always sees a fixed 32-channel topology.

### Step 3: Update `PluginProcessor` constructor

```cpp
PluginProcessor::PluginProcessor()
    : AudioProcessor(buildBusesFromMicLayout(resolveMicLayoutFromConfig()))
    , mAudioFormatManagerPtr(...)
    , mAudioProcessorValueTreeStatePtr(...)
{
    this->drumKitEnginePtr = std::make_unique<DrumKitEngine>();
    this->drumKitEnginePtr->init(*mAudioProcessorValueTreeStatePtr);
}
```

### Step 4: Keep `expectedHostChannels = 32` in `applyMicLayout()`

The hardcoded `const int expectedHostChannels = 32` stays — the total host channel count is always 32.
No change needed here. The AUX padding happens only on the `BusesProperties` side (`buildBusesFromMicLayout`);
the engine's mic-mapping loop already stops after iterating `activeMicLayoutPtr->mics`, so AUX channels
are simply unmapped and receive no audio (silence), which is the desired behavior.

### Step 5: Add `MicLayout::getTotalChannelCount()` convenience method

```cpp
// In LayoutManager.h, inside MicLayout:
int getTotalChannelCount() const {
    int total = 0;
    for (const auto& mic : mics)
        total += std::max(1, mic.specs.channels);
    return total;
}
```

Used by `buildBusesFromMicLayout()` to calculate how many AUX channels to pad (`TOTAL_HOST_CHANNELS - layout.getTotalChannelCount()`).

### Step 6: Update `isBusesLayoutSupported()`

Currently returns `true` for any layout. Should validate that the proposed layout matches the mic layout topology:

```cpp
bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
    // Accept any layout that matches our output bus count
    // (hosts may query various configurations)
    return layouts.outputBuses.size() <= getTotalNumOutputBuses();
}
```

Or keep it permissive if host compatibility is more important than strictness.

---

## Files to Modify

| File | Change |
|------|--------|
| `source/Engine/LayoutManager.h` | Add `MicLayout::getTotalChannelCount()` |
| `source/Engine/DrumKitEngine.h` | Add `static MicLayout resolveMicLayoutFromConfig()` declaration |
| `source/Engine/DrumKitEngine.cpp` | Implement `resolveMicLayoutFromConfig()`, update `applyMicLayout()` to remove hardcoded 32 |
| `source/PluginProcessor.h` | (no changes expected) |
| `source/PluginProcessor.cpp` | Add `buildBusesFromMicLayout()`, update constructor |

---

## Edge Cases & Considerations

1. **Config not found / parse error**: `resolveMicLayoutFromConfig()` falls back to `MicLayout::getDefault()` — same 16-channel default as today.

2. **Mic layout YAML not found**: Same fallback — the default layout uses all 32 channels so no AUX buses are created, matching current behavior.

3. **Zero mics in layout**: All 32 channels become AUX mono buses. No assert needed — the plugin still works, just with no mic-named outputs.

4. **Layout exceeds 32 channels**: `buildBusesFromMicLayout()` caps at `TOTAL_HOST_CHANNELS` and stops adding mic buses. A log warning should be emitted.

5. **Engine init consistency**: The engine's `applyMicLayout()` must produce the same channel mapping as `buildBusesFromMicLayout()` used at construction. Both derive from the same `MicLayout` data and both use 32 total channels, so they stay in sync. AUX channels are simply unmapped in the engine (no `InstrumentSynth` targets them).

6. **Performance**: `resolveMicLayoutFromConfig()` does file I/O (reads config.yaml, scans layout dir) during plugin construction. This is acceptable — plugin construction already performs heavy I/O, and this adds one small YAML parse.

7. **Standalone vs VST3**: Both use the same `PluginProcessor` constructor, so both get dynamic buses. Standalone may handle multi-output differently (typically mixes to stereo) — no special handling needed.

8. **Host bus negotiation**: Some hosts (e.g. REAPER) allow users to disable individual buses. The current `isBusesLayoutSupported` returning `true` already handles this. More restrictive validation is optional.

---

## Testing

- Verify default mic layout produces identical bus topology to current hardcoded 16 stereo outputs (no AUX buses)
- Verify custom mic layout with fewer than 32 channels pads remaining with mono AUX buses
- Verify total channel count is always exactly 32 regardless of mic layout
- Verify custom mic layout with mixed mono/stereo mics produces correct bus names and channel counts
- Verify `applyMicLayout()` channel mapping matches the bus topology (AUX channels stay silent)
- Verify graceful fallback when config.yaml is missing or malformed
- Verify layout exceeding 32 channels is capped and logged
