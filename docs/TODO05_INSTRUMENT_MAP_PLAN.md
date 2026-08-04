# TODO05: Instrument Map - Implementation Plan

## Summary

Rewrite `parseInstrumentMapText()` and `useInstrumentMap()` in `DrumKitEngine` to support a structured, extensible instrument mapping format with per-slot parameters (tuning, attack, decay, etc.) and proper `ParseResult<>` error reporting. (use DrumKitEngine::parseConfigYaml as an example of style)

## Current State

### What exists
- `parseInstrumentMapText()` - splits lines on first `:` into `vector<pair<String, String>>`. No structured result, no parameter parsing, no `ParseResult<>` wrapping.
- `useInstrumentMap()` - marked `FIXME: not working draft`. Manually re-parses `kitName/instCode` from the pair's RHS. Manually rebuilds MIDI note mappings (duplicating logic from `applyKitLayout`). No parameter application.
- `useState()` - currently skips `useInstrumentMap()` with a `LOG(Warning, "Custom instrument map not supported yet")`.

### What's missing
1. **Structured parse result type** - no `InstrumentMapLayer` struct.
2. **Third field parsing** - the TODO says format should be `<slotId>/<articulationCode>:<KitName>/<InstrumentCode>:<params>` but only the first colon is parsed.
3. **Per-instrument and per articulation parameters** - `InstrumentPlayer` has no instrument-level tuning/attack/decay setters. Voice-level `setAttackTimeSec` / `setReleaseTimeSec` / `pitchRatio` exist but are set per-voice, not per-instrument nor per-articulation.
4. **`LibraryManager::findKit()` exists** but `useInstrumentMap` manually loops over kits instead of using it. `findKit` only matches by `meta.name`, not by `meta.codeRaw` - should be extended or a wrapper used.

---

## Proposed Format

```
# Comment lines start with #
# Empty lines are ignored

# Whole-kit generation writes explicit map layers. The format has no wildcard line.

# Map specific slot to a kit instrument:
kick1/center:MyDrumKit/kick-24/center
snare1/center:MyDrumKit/snare-14/center

# Map with per-slot parameters (key=value, comma-separated):
kick1/center:MyDrumKit/kick-24/center:tuning=-2;attack=50;decay=300
hihat1/bow:MyDrumKit/hihat-14/bow:tuning=15

# Per-articulation library mapping - ride bow from one library instrument, ride edge from another:
ride1/bow:KitA/ride-20/bow
ride1/edge:KitB/ride-18/edge

# Articulation layering - ride bow plays samples from several library instruments simultaneously:
ride1/bow:KitA/ride-20/bow
ride1/bow:KitB/ride-18/bow

# Combined - ride bow is layered, ride edge from KitB only:
ride1/bow:KitA/ride-20/bow
ride1/bow:KitB/ride-18/bow
ride1/edge:KitB/ride-18/edge

# Supported parameters (all optional):
#   tuning*   - tuning cents offset (int, e.g. -2, +15)
#   attack    - attack time in ms (int)
#   hold?     - hold time in ms (int) //TODO?
#   decay     - release/decay time in ms (int)
#   attack_curve - //TODO: maybe define several, but probably later.
#   gain*     - gain in dB (float, e.g. -3, +6)
#   pan*      - pan position (-1.0 left .. +1.0 right)
#   flip*     - flip R/L for stereo mics * applicable only for stereo micCode like XXs (ons, rfs, etc) //TODO details, logic
#   bleed*    - //FIXME: need to think about naming, probably bleed[micCode] like bleed[rfs] bleed[ons] ... then need to define some priority list for detecting which mic is primary (dXX - direct, then oXX - overhead then others?) or just use gain[micCode] instead?
#   curve     - velocity curve //TODO do we even need this here, probably this is to be defined in kitLayout for all instruments? or per articulation curve can be good? maybe for now skip this and if some day will be needed then plan it, so later.
#   solo,mute //TODO: also later, probably not related to this at all, since its temporary runtime thing that we dont need to save as instrument param


all params need to be saved in vst settings chunk.
* - param also can be automated
```

### Grammar

```
line                = comment | mapLayer | empty
comment             = '#' <any text>
mapLayer            = instrumentSlot '/' articulationSlot ':' libraryKit '/' libraryInstrument '/' libraryArticulation (':' params)?
instrumentSlot      = <kit-layout instrument-slot ID>
articulationSlot    = <required kit-layout articulation-slot code>
libraryKit          = <library-kit identity>
libraryInstrument   = <library-instrument identity>
libraryArticulation = <required library-articulation identity>
params      = param (';' param)*
param       = key '=' value
key         = 'tuning' | 'attack' | 'decay' | 'gain' | 'pan' | ...
value       = <float literal> | <comma-separated identifiers (for art)>
```

---

## Data Structures

### New: `InstrumentMapLayer` (in DrumKitEngine.h or maybe better in KitLibModel.h, or separate header)

```cpp
struct InstrumentMapLayer {
    juce::String slotId;
    juce::String kitRef;     // library-kit name or code
    juce::String instCode;   // library-instrument code

    // The parser requires one library articulation in each map-layer path.
    // Names match ArticulationSpec::nameId, for example "bow", "edge", "bell", "center", or "rim".
    std::vector<juce::String> articulationFilter;

    // Per-slot overrides (nullopt = use default / don't override)
    std::optional<int> tuningCents;
    std::optional<int> attackTimeMs;
    std::optional<int> decayTimeMs;
    // Future:
    // std::optional<float> gainDb;
    // std::optional<float> pan;
};
```

### New: `InstrumentMap` (result container)

```cpp
struct InstrumentMap {
    std::vector<InstrumentMapLayer> layers;
};
```

---

## Articulation Routing & Layering Design

### Problem

Users want to mix-and-match articulations from different kit instruments within a single slot. Two distinct use cases:

1. **Per-articulation library mapping** - ride bow uses `KitA/ride-20/bow`, while ride edge uses `KitB/ride-18/edge`.
2. **Articulation layering** - ride bow uses both `KitA/ride-20/bow` and `KitB/ride-18/bow`. Both map layers trigger on the same MIDI note.

Additionally: articulations referenced in the map may not exist in the target instrument (e.g. requesting "bell" from an instrument that only has "bow" and "edge"). This must be handled gracefully.

### Current Architecture (relevant constraints)

- `InstrumentPlayer` owns all `ArticulationRuntime` objects. `loadInstrumentWithPlaybackEntries()` clears existing runtime state, stores the routing context and library snapshot, and applies the supplied articulation playback entries.
- `collectResolvedArticulationSlots()` resolves articulation-slot bindings from the kit layout and routing context. `appendArticulationPlaybackEntries()` converts them to `ArticulationPlaybackEntry` records, and `applyArticulationPlaybackEntries()` loads their library articulations.
- `instrumentPlayersByNote` is `map<int, vector<InstrumentPlayer*>>` - **multiple instrument players per MIDI note already work**. Each instrument player independently resolves articulation → velocity level → sample and renders to its output channels.
- `noteToArticulation[note]` in each InstrumentPlayer maps a MIDI note to a single articulation index. This is a 1:1 mapping within one instrument player.

### Design

#### Per-articulation library mapping within one `InstrumentPlayer`

Build explicit articulation playback entries for the instrument player:

```cpp
collectResolvedArticulationSlots(...)
appendArticulationPlaybackEntries(...)
loadInstrumentWithPlaybackEntries(...)
```

When processing each articulation slot, use its `ArticulationPlaybackEntry` to resolve the library articulation.

If the requested articulation doesn't exist in the library instrument:
1. Try fallback via `articulationFallbackIdMap` (within the same library instrument).
2. If still not found, log a warning and skip (the articulation is simply not available - no crash, no sound for that trigger).

#### Articulation layering with multiple map layers for one articulation slot

Leverage the existing multi-instrument player-per-note infrastructure:

When `useInstrumentMap` encounters multiple map layers for the same instrument slot and articulation slot, it creates additional `InstrumentPlayer` instances for the additional map layers:

```
ride1/bow:KitA/ride-20/bow         ← primary slot (ride1), loads bow from KitA
ride1/bow:KitB/ride-18/bow         ← second map layer for ride1 with same art → mixes
```

An additional `InstrumentPlayer` is:
- A new `InstrumentPlayer` instance that shares the same layout slot definition (same MIDI notes, same articulations).
- Created and owned by `DrumKitEngine` (appended to `instrumentPlayerPtrs`).
- Registered in `instrumentPlayersByNote` for the relevant notes, so it triggers alongside the primary.
- **NOT** registered in `instrumentPlayerById` because only the primary instrument player keeps the instrument-slot ID.
- Has its own `loadInstrument` call with its own library instrument.
- Inherits mic output mapping from the primary slot.
- Load only the library articulations and velocity levels used by this map layer.

This way, when a MIDI note fires, both the primary and additional instrument player render independently, producing a layered sound.

#### Processing order in `useInstrumentMap`

1. **Group map layers by instrument-slot ID.** The first matching map layer uses the primary instrument player. Additional overlapping map layers use additional instrument players.
2. **Build `ArticulationPlaybackEntry` records** from map layers for the same primary instrument slot.
3. **Detect layering:** if two map layers for the same instrument slot claim the same articulation slot, the second one uses an additional instrument player.

#### What happens when an articulation doesn't exist

- `collectResolvedArticulationSlots()` and `applyArticulationPlaybackEntries()` handle this. If there is no match or fallback, the articulation slot is skipped. `noteToArticulation[note]` stays at `-1`, and `resolveArticulation()` returns `-1`, so `playNote()` does nothing.
- The instrument map should log a clear warning: `"articulation 'bell' not found in KitA/ride-20 (and no fallback available) - skipping for slot 'ride1'"`.

### Example flow

Map text:
```
ride1/bow:KitA/ride-20/bow
ride1/bow:KitB/ride-18/bow
ride1/edge:KitB/ride-18/edge
```

Processing:
1. Group the three map layers by instrument slot `ride1`.
2. Map layer 1 maps articulation slot `bow` to `KitA/ride-20/bow`.
3. Map layer 2 maps articulation slot `bow` to `KitB/ride-18/bow`.
4. Articulation slot `bow` appears in both map layers, so it is layered.
   - Primary `ride1` loads `bow` from `KitA/ride-20`.
   - Additional instrument player loads `bow` from `KitB/ride-18`.
5. Map layer 3 maps articulation slot `edge` to `KitB/ride-18/edge`.

Result: hitting the ride bow note triggers two instrument players. Hitting edge triggers only the primary with the KitB library articulation. Hitting bell triggers only the primary with the default KitA library articulation.

---

## Implementation Steps

### Step 1: Define `InstrumentMapLayer` and `InstrumentMap`

**File:** `source/Engine/DrumKitEngine.h`

- Add `InstrumentMapLayer` struct (as shown above, including `articulationFilter`).
- Add `InstrumentMap` struct.
- Change `parseInstrumentMapText` signature:
  ```cpp
  static ParseResult<InstrumentMap> parseInstrumentMapText(const juce::String& text);
  ```

### Step 2: Rewrite `parseInstrumentMapText()`

**File:** `source/Engine/DrumKitEngine.cpp`

New logic:
1. Split text into lines. Skip empty lines and `#` comments.
2. Split each line into mapping and optional parameter parts.
3. Parse required `instrumentSlot/articulationSlot` on the left.
4. Require `libraryKit/libraryInstrument/libraryArticulation` on the right.
5. Parse top-level parameters with semicolon separators.
6. Validate known parameter keys and warn on unknown keys.
7. Add one `InstrumentMapLayer` to `InstrumentMap::layers`.
8. Return `ParseResult<InstrumentMap>`. Use `addWarning()` for non-fatal issues and `returnWithError()` when no valid map layer was parsed.

**Edge cases:**
- A missing library articulation is an error for that map layer.
- An omitted articulation slot is valid.
- Whole-instrument assignment expands to one map layer per library articulation.

### Step 3: Add per-instrument parameter support to `InstrumentPlayer`

**File:** `source/Engine/InstrumentPlayer.h` / `.cpp`

Add instrument-level parameter overrides:
```cpp
// In InstrumentPlayer:
void setTuningOffsetCents(int cents);      // stored, applied to pitchRatio in voice startNote
int getTuningOffsetCents() const;

void setAttackOverrideMs(int ms);          // applied to all voices' attackTimeSec
void setDecayOverrideMs(int ms);           // applied to all voices' releaseTimeSec
void clearParameterOverrides();            // reset to defaults
```

**Implementation notes:**
- `tuningOffsetCents` (int) → stored as member. In voice `startNote()`, multiply `pitchRatio` by `std::pow(2.0, tuningOffsetCents / 1200.0)`.
- `attackOverrideMs` / `decayOverrideMs` → stored as members. Applied in `startSampleVoice()` or via a method that iterates voices. Converted: `seconds = ms / 1000.0f`.
- `clearParameterOverrides()` → called by `unloadInstrument()`.

### Step 4: Add `loadInstrumentWithPlaybackEntries` to `InstrumentPlayer`

**File:** `source/Engine/InstrumentPlayer.h` / `InstrumentPlayerLoader.cpp`

```cpp
void loadInstrumentWithPlaybackEntries(KitLib::InstrumentSpec* defaultInstrument, const RoutingContext& routingContext, std::vector<ArticulationPlaybackEntry> articulationPlaybackEntries);
```

- Use `collectResolvedArticulationSlots()` to resolve the playback entries for each articulation slot.
- Resolve each articulation slot through its `ArticulationPlaybackEntry`.
- If a library articulation is missing, report the map layer and do not apply it.

### Step 5: Rewrite `useInstrumentMap()`

**File:** `source/Engine/DrumKitEngine.cpp`

New logic:
1. Call `parseInstrumentMapText(text)`.
2. If `!parseResult.ok()` - log errors, return false.
3. Log any warnings from `parseResult`.
4. **Group map layers by slotId** (preserve order within each group).
5. For each slot group:
   - **Primary map layer** (first in group):
     - Resolve the library `InstrumentSpec`.
     - Build `ArticulationPlaybackEntry` records from map layers in the group.
     - Call `player->loadInstrumentWithPlaybackEntries(primaryInstrumentSpec, routingContext, articulationPlaybackEntries)`.
     - Apply parameter overrides.
   - **Layered map layers** (same slot, same articulation name as primary):
     - Create additional `InstrumentPlayer` instance.
     - `additionalPlayer->loadInstrument(mapLayerInstrumentSpec)` with only the overlapping library articulations.
     - Register the additional instrument player in `instrumentPlayersByNote` for relevant notes.
     - Apply its own parameter overrides.
6. **Do NOT re-build MIDI note mappings for the primary.** Note mappings are already built by `applyKitLayout()`. Additional instrument players do need note registration.
7. After all map layers applied, trigger `sampleLoaderPtr->getQueue().startProcessing()`.

### Step 6: Enable in `useState()`

**File:** `source/Engine/DrumKitEngine.cpp`

- Uncomment `this->useInstrumentMap(instrumentMap)`.
- Remove the `LOG(Warning, "Custom instrument map not supported yet")` line.
- If `useInstrumentMap` returns false, fall back to `mapDefaultKit()`.

### Step 7: Extend `LibraryManager::findKit`

**File:** `source/Engine/KitLibModel.cpp`

- Also match `kit->meta.codeRaw` in addition to `kit->meta.name`.

### Step 8: UI - Instrument Map Textarea

**File:** `source/PluginEditor.h` / `source/PluginEditor.cpp`

Add a minimal UI so the user can view and edit the instrument map text without reloading the plugin:

- `juce::TextEditor instrumentMapEditor` - multiline, full instrument map text
- `juce::TextButton applyButton` - triggers re-apply: reads text from editor, calls `processor.getEngine().useInstrumentMap(text)`, refreshes status
- `juce::Label statusLabel` - shows parse warnings/errors from the last apply (populated from `ParseResult::error()` / warnings list)
- On editor construction: populate `instrumentMapEditor` from `treeState.state["instrument_map"]`
- On apply: write new text back to `treeState.state["instrument_map"]` so it persists with plugin state

This is intentionally minimal - full per-instrument/per-mic mixer UI is deferred with the APVTS work.

### Step 9: Tests

**File:** `tests/TestInstrumentMap.cpp` (new)

Test cases for `parseInstrumentMapText`:
1. Empty text → error
2. Only comments → error (no map layers)
3. `kick1:Kit/kick/center` → error because the articulation slot is required
4. `kick1/center:Kit/kick/center:tuning=-2;attack=50` → parameters populated
5. Missing library articulation → error for that map layer
6. Unknown parameter key → warning but map layer stays valid
7. Malformed parameter without `=` → warning and parameter skipped
8. Missing library-kit identity → error for that line
9. Multiple map layers → all parsed
10. Two map layers with the same instrument slot and articulation slot → layering detected in `useInstrumentMap`
11. Two map layers with the same instrument slot and different articulation slots → per-articulation mapping

Test cases for `useInstrumentMap` (require mocked/test kit data):
1. Valid map with known kit/instrument → instruments loaded
2. Unknown kit name → warning, slot skipped
3. Unknown instrument code → warning, slot skipped
4. Unknown slot id → warning, map layer skipped
5. Parameters applied → verify tuning, attack, and decay on `InstrumentPlayer`
6. Per-articulation mapping → verify that different library articulations load for different articulation slots
7. Layering → verify that an additional instrument player is created and registered in `instrumentPlayersByNote`
8. Missing library articulation → error identifies the map layer

---

## Sequence Diagram

```
useState(treeState)
  │
  ├─ applyKitLayout()          ← builds slots + MIDI note routing
  ├─ applyMicLayout()          ← assigns output channels
  │
  ├─ instrumentMap empty?
  │   ├─ YES → mapDefaultKit() ← loads first kit's instruments into slots
  │   └─ NO  → useInstrumentMap(text)
  │             │
  │             ├─ parseInstrumentMapText(text) → ParseResult<InstrumentMap>
  │             │   ├─ validates format
  │             │   ├─ parses map layers + params + art filters
  │             │   └─ returns structured result with warnings
  │             │
  │             ├─ group map layers by slotId
  │             │
  │             ├─ for each slot group:
  │             │   ├─ resolve kit (findKit) + instrument (findInstrument)
  │             │   ├─ build ArticulationPlaybackEntry records from map layers
  │             │   ├─ primary: player->loadInstrumentWithPlaybackEntries(...)
  │             │   ├─ for each layered articulation:
  │             │   │   ├─ create additional InstrumentPlayer
  │             │   │   ├─ additionalPlayer->loadInstrument(mapLayerInstrumentSpec)
  │             │   │   └─ register additionalPlayer in instrumentPlayersByNote
  │             │   └─ apply param overrides (tuning, attack, decay)
  │             │
  │             └─ sampleLoader->startProcessing()
```

---

## Files Changed

| File | Change |
|------|--------|
| `source/Engine/DrumKitEngine.h` | Add `InstrumentMapLayer`, `InstrumentMap` structs; update `parseInstrumentMapText` signature |
| `source/Engine/DrumKitEngine.cpp` | Rewrite `parseInstrumentMapText`, `useInstrumentMap`; update `useState`; additional instrument player creation for layering |
| `source/Engine/InstrumentPlayer.h` | Add tuning, attack, and decay override members and setters; declare `ArticulationPlaybackEntry` and `loadInstrumentWithPlaybackEntries` |
| `source/Engine/InstrumentPlayerLoader.cpp` | Implement `loadInstrumentWithPlaybackEntries` and resolve articulation-slot bindings |
| `source/Engine/KitLibModel.cpp` | Extend `findKit` to match `codeRaw` |
| `source/PluginEditor.h` / `.cpp` | Add instrument map textarea, apply button, status label |
| `tests/TestInstrumentMap.cpp` | New test file - parsing, per-articulation routing, layering |
| `cmake/Tests.cmake` | Add new test source |

---

## Key Design Decisions

1. **MIDI note mapping stays in `applyKitLayout()`** - The instrument map changes *content* of slots, not *routing*. This avoids duplicating MIDI mapping logic and keeps concerns separated. Additional instrument players created for layering are the only exception - they need note registration.

2. **Parameter overrides are instrument-level, not voice-level** - Tuning/attack/decay are set on `InstrumentPlayer` and propagated to voices at note-on time. This is simpler and matches the mental model ("this snare is tuned down 200 cents").

3. **`ParseResult<>` pattern** - Consistent with the rest of the codebase (`parseConfigYaml`, `parseInfo`, `parseYaml` for layouts).

4. **Explicit whole-kit expansion** - Whole-kit mapping generates explicit map layers with complete library paths.

5. **`std::optional` for params** - Clear distinction between "not specified" (use default) and "specified as 0".

6. **Per-articulation mapping via `ArticulationPlaybackEntry`** - Resolve one library articulation for each articulation slot. Use the default library instrument only when the map layer does not select another one.

7. **Layering via additional `InstrumentPlayer` instances** - Reuses the existing multi-instrument player-per-note infrastructure (`instrumentPlayersByNote` is already a vector). No changes to the real-time playback path. Additional instrument players are "invisible" to the user (not in `instrumentPlayerById`), owned by the engine, and share MIDI routing with the primary slot.

---

## VST Automatable Parameters (APVTS) - Deferred

Full APVTS planning is deferred to a later TODO. Short summary of what was decided so far (for reference when that work starts):

- **Per-instrument params** (tuning, attack, decay, mute, solo) - applied at note-on in `InstrumentPlayer`. IDs `inst01_*`..`inst32_*`, fixed maximum of 32 slots for stable DAW automation lane IDs.
- **Per-mic params** (volume, pan, phase, flipLR, mute) - applied as a post-render pass on host output channels in `processBlock`. IDs `mic01_*`..`mic32_*`. Per-mic not per-instrument because shared mics (Overheads, Room) capture all instruments simultaneously.
- **Instrument map values are defaults**, APVTS params are live state - loading a map sets APVTS values, DAW automation can override at any time.
- Implementation order when the time comes: instrument APVTS first → mic APVTS → UI mixer strips → state save/restore.

---

## Two distinct parameter scopes: Instrument vs Mic (reference)

The architecture has two separate hierarchies that need different parameter scopes:

<!-- SECTION_PLACEHOLDER -->

The architecture has two separate hierarchies that need different parameter scopes:

```
Kit Layout  (variable, defines instruments)      Mic Layout  (static after load, defines output channels)
─────────────────────────────────────────         ─────────────────────────────────────────────────────────
  kick1                                             mic01: Overheads (ons, 2ch, shared by ALL instruments)
  snare1                                            mic02: Hi-Hat (dtm, 1ch, hihat1 only)
  tom1                                              mic03: Ride (dtm, 1ch, ride1 only)
  tom2                                              mic04: Kick In (dim, 1ch, kick1 only)
  hihat1                                            mic05: Kick Out (dom, 1ch, kick1 only)
  ride1                                             mic06: Snare Top (dtm, 1ch, snare1 only)
  crash1                                            mic07: Snare Bottom (dbm, 1ch, snare1 only)
  ...  (25 slots in default layout, unbounded)      mic08..mic12: Toms (dtm, 1ch each)
                                                    mic13: Room Close (rcm, 1ch, shared by ALL)
                                                    mic14: Room Far (rfs, 2ch, shared by ALL)
                                                    ... up to 32 mono host channels total
```

**Instruments** own tuning, attack, decay, mute, solo - properties of "what is being hit".
**Mics** own volume, pan, phase, flipLR - properties of "how it's captured/mixed".

An instrument can be captured by many mics (kick1 has Kick In + Kick Out + Overheads + Room). A shared mic (Overheads) captures ALL instruments. So vol/pan/flip are per-mic-slot, not per-instrument.

### What should NOT be an APVTS parameter

| Thing | Why not |
|-------|---------|
| Instrument map text | Formatted string, no meaningful automation curve. Stored in ValueTree state. |
| Kit layout selection | Structural change (rebuilds all slots). ValueTree state or non-automatable `AudioParameterChoice`. |
| Mic layout selection | Structural change (rebuilds output routing). Same. |
| Articulation library mapping | Part of instrument-map configuration, not realtime behavior. |

### Per-Instrument APVTS parameters

Applied at note-on time inside `InstrumentPlayer` / voice `startNote()`. The kit layout defines instrument slots; the count is variable but we use a fixed maximum for stable parameter IDs.

| Parameter | ID pattern | Type | Range | Default | Notes |
|-----------|-----------|------|-------|---------|-------|
| **Tuning** | `inst{NN}_tuning` | `AudioParameterInt` | -2400..+2400 cents | 0 | `pitchRatio *= pow(2, cents/1200)`. ±2400 = ±2 octaves. |
| **Attack** | `inst{NN}_attack` | `AudioParameterInt` | 0..500 ms | 1 | Overrides voice `attackTimeSec`. |
| **Decay** | `inst{NN}_decay` | `AudioParameterInt` | 10..5000 ms | 100 | Overrides voice `releaseTimeSec`. |
| **Mute** | `inst{NN}_mute` | `AudioParameterBool` | on/off | off | Silences the instrument. |
| **Solo** | `inst{NN}_solo` | `AudioParameterBool` | on/off | off | If any solo is on, only soloed instruments play. |

**ID convention:** `inst01`..`inst32` (numeric, stable across layout changes). Display name maps to current slot name (e.g. `inst01` → "Kick").

**Maximum:** 32 instrument parameter groups. With 5 params each = **160 instrument parameters**.

### Per-Mic APVTS parameters

Applied as per-channel processing on the host output buffer. The mic layout is static after plugin load and maps directly to the 32 mono host output channels. Each mic-layout entry gets a parameter group.

| Parameter | ID pattern | Type | Range | Default | Notes |
|-----------|-----------|------|-------|---------|-------|
| **Volume** | `mic{NN}_volume` | `AudioParameterFloat` | -60..+12 dB | 0 | Gain multiplier applied to the host output channel(s) for this mic. Shared mics (Overheads) affect all instruments equally. |
| **Pan** | `mic{NN}_pan` | `AudioParameterFloat` | -1.0..+1.0 | 0.0 | For mono mics: constant-power pan to stereo. For stereo mics: L/R balance. |
| **Phase** | `mic{NN}_phase` | `AudioParameterBool` | on/off | off | Multiplies channel output by -1. |
| **Flip L/R** | `mic{NN}_flipLR` | `AudioParameterBool` | on/off | off | Swaps L and R of stereo mic pairs (e.g. `ons`, `rfs`). Only meaningful for stereo mics. |
| **Mute** | `mic{NN}_mute` | `AudioParameterBool` | on/off | off | Silences this mic channel. |

**ID convention:** `mic01`..`mic32` (indexed by mic-layout entry order = host channel assignment order). Display name maps to mic name (e.g. `mic01` → "Overheads").

**Maximum:** 32 mic parameter groups. With 5 params each = **160 mic parameters**.

### Where mic params are applied in the rendering pipeline

Mic params should be applied as a **post-render pass** on the host output buffer, not inside voice rendering. This is because:
- Voices render via `addSample(destCh, ...)` directly into the output buffer
- A shared mic (Overheads) receives `addSample` from ALL instruments - the gain must be applied once after all instruments have rendered, not per-voice
- This keeps voice code simple and avoids duplicating mic-level logic across instruments

```cpp
// In DrumKitEngine::processBlock, AFTER all instrumentPlayer->renderNextBlock:
for (int micIdx = 0; micIdx < activeMicCount; ++micIdx) {
    float gain = decibelsToGain(micVolumeParams[micIdx]->get());
    bool phase = micPhaseParams[micIdx]->get();
    if (phase) gain = -gain;
    // apply to host channel(s) for this mic
    for (int ch : micToHostChannels[micIdx]) {
        outputBuffer.applyGain(ch, 0, numSamples, gain);
    }
    // flip L/R for stereo mics
    if (micFlipParams[micIdx]->get() && micToHostChannels[micIdx].size() == 2) {
        // swap channel data
    }
}
```

### Parameter count summary

| Scope | Groups | Params/group | Total |
|-------|--------|-------------|-------|
| Per-instrument | 32 max | 5 | 160 |
| Per-mic | 32 max | 5 | 160 |
| Global | 1 | 2 | 2 |
| **Total** | | | **322** |

Well within normal bounds for a drum VST.

### Global parameters

| Parameter | ID | Type | Range | Default | Notes |
|-----------|-------------|------|-------|---------|-------|
| Master volume | `master_volume` | `AudioParameterFloat` | -60..+12 dB | 0 | Applied to final output buffer after mic processing |
| Master tuning | `master_tuning` | `AudioParameterInt` | -100..+100 cents | 0 | Global tuning offset added to all instrument tuning values |

### Relationship between instrument map params and APVTS params

Instrument map values are **initial/default** values. APVTS parameters are the **live state**.

```
                    Instrument Map (text config)       APVTS (real-time automation)
                    ────────────────────────────       ────────────────────────────
Per-instrument:
    tuning          kick1/center:Kit/kick-24/center:tuning=-200     inst01_tuning = -200
    attack          kick1/center:Kit/kick-24/center:attack=50       inst01_attack = 50
    decay           kick1/center:Kit/kick-24/center:decay=300       inst01_decay = 300

Per-mic (from instrument map):
    gain            kick1/center:Kit/kick-24/center:gain=-3         mic04_volume = -3    (Kick In mic)
    flip            kick1/center:Kit/kick-24/center:flip=1          mic04_flipLR = true

Per-mic (standalone, not from instrument map - configured in UI or via automation only):
    pan                                               mic01_pan = 0.3      (Overheads)
    phase                                             mic07_phase = true   (Snare Bottom)
```

When the instrument map specifies a mic-level param (e.g. `gain=-3` on `kick1`), it needs to resolve which mic(s) are affected. Options:
- `gain=-3` on an instrument → applies to ALL mics assigned to that instrument (Kick In, Kick Out, Overheads, Room)
- `gain[dim]=-3` → applies only to mic with code `dim` (Kick In) for that instrument - the `bleed[micCode]` syntax from the format comments

This per-mic-per-instrument addressing (`gain[micCode]`) is a future extension. For now, instrument-level gain in the map can set a default for the instrument's primary/direct mic.

---

## Risks / Open Questions

- **Parameter application timing**: If instrument map is re-applied after samples are already loaded, parameter overrides must be re-applied. Consider storing overrides persistently on the slot so they survive re-loads.
- **FX chain parameter**: Mentioned in the TODO comment but deferred. Would need a named fx chain registry, which doesn't exist yet.
- **Instrument re-mapping without full reload**: Currently `loadInstrument` does a full teardown+rebuild. If only params change, a lighter-weight path could be added later.
- **Whole-kit mapping**: Generate explicit map layers with complete library paths. Do not use wildcard map lines.
- **Additional instrument-player lifetime**: Remove or replace affected additional instrument players when the instrument map changes or `applyKitLayout` runs again.
- **Additional instrument-player mic routing**: Additional instrument players must inherit the mic output mapping from their primary instrument slot. A mic-layout change must update them too.
- **Articulation expressions**: Each map layer stores one complete library-articulation path. Controller sibling loading can include valid expression states from the same library-articulation family.
- **Voice stealing across map layers**: When an additional instrument player and the primary instrument player play the same note, each instrument player manages its own voice pool. The total voice count is higher. Keep `findVoiceToSteal` independent unless profiling and listening tests show a problem.
