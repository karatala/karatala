# TODO05: Instrument Map - Implementation Plan

## Summary

Rewrite `parseInstrumentMapText()` and `useInstrumentMap()` in `DrumKitEngine` to support a structured, extensible instrument mapping format with per-slot parameters (tuning, attack, decay, etc.) and proper `ParseResult<>` error reporting. (use DrumKitEngine::parseConfigYaml as an example of style)

## Current State

### What exists
- `parseInstrumentMapText()` - splits lines on first `:` into `vector<pair<String, String>>`. No structured result, no parameter parsing, no `ParseResult<>` wrapping.
- `useInstrumentMap()` - marked `FIXME: not working draft`. Manually re-parses `kitName/instCode` from the pair's RHS. Manually rebuilds MIDI note mappings (duplicating logic from `applyKitLayout`). No parameter application.
- `useState()` - currently skips `useInstrumentMap()` with a `LOG(Warning, "Custom instrument map not supported yet")`.

### What's missing
1. **Structured parse result type** - no `InstrumentMapEntry` struct.
2. **Third field parsing** - the TODO says format should be `<slotId>/<articulationCode>:<KitName>/<InstrumentCode>:<params>` but only the first colon is parsed.
3. **Per-instrument and per articulation parameters** - `InstrumentSynth` has no instrument-level tuning/attack/decay setters. Voice-level `setAttackTimeSec` / `setReleaseTimeSec` / `pitchRatio` exist but are set per-voice, not per-instrument nor per-articulation.
4. **`LibraryManager::findKit()` exists** but `useInstrumentMap` manually loops over kits instead of using it. `findKit` only matches by `meta.name`, not by `meta.codeRaw` - should be extended or a wrapper used.

---

## Proposed Format

```
# Comment lines start with #
# Empty lines are ignored

# Map entire kit to all slots (matched by type, same as mapKit):
*:MyDrumKit

# Map specific slot to a kit instrument:
kick1:MyDrumKit/kick-24
snare1:MyDrumKit/snare-14

# Map with per-slot parameters (key=value, comma-separated):
kick1:MyDrumKit/kick-24:tuning=-2,attack=50,decay=300
hihat1:MyDrumKit/hihat-14:tuning=15

# Per-articulation source routing - ride bow from one instrument, ride edge from another:
ride1/bow:KitA/ride-20/bow
ride1/edge:KitB/ride-18/edge

# Articulation layering - ride bow plays samples from several instruments simultaneously:
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
line        = comment | entry | empty
comment     = '#' <any text>
entry       = slotId ':' target (':' params)?
slotId      = '*' | <identifier matching instrumentSynthById key> ('/' articulation[optionalExpressionShortName])? //TODO: need to think about <optionalExpressionShortName>, do we need it, if yes then what to do for same expr code with different <optionalExpressionCode><value> (according KitLibModel.h)?
target      = kitRef ('/' instCode)? ('/' articulation[optionalExpressionShortName])? //TODO...
kitRef      = <KitName>
instCode    = <instrument code within kit>
params      = param (';' param)*
param       = key '=' value
key         = 'tuning' | 'attack' | 'decay' | 'gain' | 'pan' | ...
value       = <float literal> | <comma-separated identifiers (for art)>
```

---

## Data Structures

### New: `InstrumentMapEntry` (in DrumKitEngine.h or maybe better in KitLibModel.h, or separate header)

```cpp
struct InstrumentMapEntry {
    juce::String slotId;     // "*" means apply kit to all slots
    juce::String kitRef;     // kit name or code
    juce::String instCode;   // instrument code within kit (empty = whole kit)

    // Articulation filter - if non-empty, only load these articulations from the source.
    // Names match ArticulationSpec::nameId (e.g. "bow", "edge", "bell", "center", "rim").
    // If empty, load all articulations from the source (default behavior).
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
    std::vector<InstrumentMapEntry> entries;
};
```

---

## Articulation Routing & Layering Design

### Problem

Users want to mix-and-match articulations from different kit instruments within a single slot. Two distinct use cases:

1. **Per-articulation source routing** - "ride bow from KitA/ride-20, ride edge from KitB/ride-18". Each articulation plays exclusively from its assigned source.
2. **Articulation layering** - "ride bow plays samples from BOTH KitA/ride-20 AND KitB/ride-18 simultaneously". Both sources trigger on the same MIDI note, creating a layered/blended sound. //TODO: also here applies mixing of each layer, like it can be different gain, pan, flip, attack time, maybe even attack curve, //TODO also need to think about velocity layers, for example from velocity 0 to 52 use one articulation, from 52 to 90 - another, etc, together with other possibilities

Additionally: articulations referenced in the map may not exist in the target instrument (e.g. requesting "bell" from an instrument that only has "bow" and "edge"). This must be handled gracefully.

### Current Architecture (relevant constraints)

- `InstrumentSynth` owns all `ArticulationRuntime` objects. `loadInstrument(instSpec)` clears everything and rebuilds from a **single** `InstrumentSpec`.
- `processLayoutArticulations(instSpec)` iterates layout articulations and matches them to the instrument's `ArticulationSpec` list by `(nameId, exprId)`. Falls back via `articulationFallbackIdMap`.
- `instrumentSynthsByNote` is `map<int, vector<InstrumentSynth*>>` - **multiple synths per MIDI note already work**. Each synth independently resolves articulation → velocity layer → sample and renders to its output channels.
- `noteToArticulation[note]` in each InstrumentSynth maps a MIDI note to a single articulation index. This is a 1:1 mapping within one synth.

### Design

#### Per-articulation source routing (within one `InstrumentSynth`)

Modify `processLayoutArticulations` to accept an optional articulation source map:

```cpp
// Maps articulationNameId → InstrumentSpec* to source that articulation from.
// If empty or if artNameId is not in the map, uses the default instSpec.
using ArticulationSourceMap = std::unordered_map<uint32_t, KitLib::InstrumentSpec*>;

void loadInstrument(KitLib::InstrumentSpec* defaultInstrument);
void loadInstrumentWithSources(KitLib::InstrumentSpec* defaultInstrument, 
                               const ArticulationSourceMap& artSources);
```

When processing each layout articulation, `processLayoutArticulations` checks if `artSources` has an override for that `nameId`. If yes, resolve the `ArticulationSpec` from the override `InstrumentSpec` instead of the default.

If the requested articulation doesn't exist in the source instrument:
1. Try fallback via `articulationFallbackIdMap` (within the same source instrument).
2. If still not found, log a warning and skip (the articulation is simply not available - no crash, no sound for that trigger).

#### Articulation layering (multiple sources for same articulation)

Leverage the existing multi-synth-per-note infrastructure:

When `useInstrumentMap` encounters **multiple entries for the same slot with overlapping `art=` filters**, it creates **shadow `InstrumentSynth` instances** for the additional layers:

```
ride1/bow:KitA/ride-20/bow         ← primary slot (ride1), loads bow from KitA
ride1/bow:KitB/ride-18/bow         ← second entry for ride1 with same art → mixes
```

A shadow `InstrumentSynth` is:
- A new `InstrumentSynth` instance that shares the same layout slot definition (same MIDI notes, same articulations).
- Created and owned by `DrumKitEngine` (appended to `instrumentSynthPtrs`).
- Registered in `instrumentSynthsByNote` for the relevant notes, so it triggers alongside the primary.
- **NOT** registered in `instrumentSynthById` (only the primary slot keeps its name - shadows are internal).
- Has its own `loadInstrument` call with its own `InstrumentSpec` source.
- Inherits mic output mapping from the primary slot.
- TODO: make sure to load in cache only actually used for layering articulations/velLayers. also how do we tell difference if instrument is shadow? will it be more optimal to have "shadow" articulations instead of instruments? and that "shadow in kinda sounds not nice, maybe rename it some better way, like "stacked" or idk

This way, when a MIDI note fires, both the primary and shadow synth render independently, producing a layered sound.

#### Processing order in `useInstrumentMap`

1. **Group entries by slotId.** For each slot, the first entry is the primary (calls `loadInstrument` or `loadInstrumentWithSources`). Additional entries with overlapping articulations create shadows.
2. **Build `ArticulationSourceMap`** from entries that have `art=` filters for the same primary slot.
3. **Detect layering:** if two entries for the same slot both claim the same articulation name, the second one becomes a shadow.

#### What happens when an articulation doesn't exist

- `processLayoutArticulations` already handles this: if no match and no fallback, the layout articulation is simply skipped. `noteToArticulation[note]` stays at -1 for those notes, and `resolveArticulation` returns -1, causing `playNote` to do nothing.
- The instrument map should log a clear warning: `"articulation 'bell' not found in KitA/ride-20 (and no fallback available) - skipping for slot 'ride1'"`.

### Example flow

Map text:
```
ride1/bow:KitA/ride-20/bow
ride1/bow:KitB/ride-18/bow
ride1/edge:KitB/ride-18/edge
```

Processing://FIXME everywhere according new format (instead of art=a,b now /art added to both sides etc)
1. Group by `ride1`: two entries.
2. Entry 1: `art=bow` from `KitA/ride-20`.
3. Entry 2: `art=bow,edge` from `KitB/ride-18`.
4. Articulation `bow` appears in both → **layering**. 
   - Primary `ride1` loads `bow` from `KitA/ride-20`.
   - Shadow synth loads `bow` from `KitB/ride-18`.
5. Articulation `edge` appears only in entry 2.
   - Primary `ride1` loads `edge` from `KitB/ride-18` (via `ArticulationSourceMap`).
6. Other articulations in the layout (e.g. `bell`) not mentioned in any `art=` filter: use `KitA/ride-20` (first entry is the default source).

Result: hitting the ride bow note triggers two synths (layered), hitting edge triggers only the primary (with KitB source), hitting bell triggers only the primary (with KitA source as default).

---

## Implementation Steps

### Step 1: Define `InstrumentMapEntry` and `InstrumentMap`

**File:** `source/Engine/DrumKitEngine.h`

- Add `InstrumentMapEntry` struct (as shown above, including `articulationFilter`).
- Add `InstrumentMap` struct.
- Change `parseInstrumentMapText` signature:
  ```cpp
  static ParseResult<InstrumentMap> parseInstrumentMapText(const juce::String& text);
  ```

### Step 2: Rewrite `parseInstrumentMapText()`

**File:** `source/Engine/DrumKitEngine.cpp`

New logic:
1. Split text into lines, skip empty and `#` comments.
2. Split each line on `:` - expect 2 or 3 segments.
   - Segment 1: `slotId`
   - Segment 2: `kitRef` optionally followed by `/instCode`
   - Segment 3 (optional): comma-separated `key=value` params
3. Parse `kitRef/instCode` by splitting on first `/`.
4. Parse params by splitting on `,`, then each on `=`.
5. Special handling for `art` param: value is comma-separated list of articulation names → populate `articulationFilter`.
6. Validate known param keys; warn on unknown.
7. Populate `InstrumentMapEntry`, push to result.
8. Return `ParseResult<InstrumentMap>` - use `addWarning()` for non-fatal issues, `returnWithError()` only if zero valid entries were parsed.

**Edge cases:**
- Multiple colons: split into at most 3 parts by first two colons.
- Kit names and instrument codes may not contain `:` or `/` (document this).
- `art` param value may itself contain commas - but since it's inside the params segment (after the last `:`), and all other params are `key=value` pairs, we parse `art=bow,edge` by finding the `art=` key and consuming up to the next recognized `key=` or end.

### Step 3: Add per-instrument parameter support to `InstrumentSynth`

**File:** `source/Engine/InstrumentSynth.h` / `.cpp`

Add instrument-level parameter overrides:
```cpp
// In InstrumentSynth:
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

### Step 4: Add `loadInstrumentWithSources` to `InstrumentSynth`

**File:** `source/Engine/InstrumentSynth.h` / `InstrumentSynthLoader.cpp`

```cpp
using ArticulationSourceMap = std::unordered_map<uint32_t, KitLib::InstrumentSpec*>;
void loadInstrumentWithSources(KitLib::InstrumentSpec* defaultInstrument,
                               const ArticulationSourceMap& artSources);
```

- Modify `processLayoutArticulations` to accept an optional `ArticulationSourceMap`.
- For each layout articulation, check `artSources[nameId]` first; fall back to `defaultInstrument`.
- If an articulation isn't found in its designated source (including fallbacks), log a warning and skip.

### Step 5: Rewrite `useInstrumentMap()`

**File:** `source/Engine/DrumKitEngine.cpp`

New logic:
1. Call `parseInstrumentMapText(text)`.
2. If `!parseResult.ok()` - log errors, return false.
3. Log any warnings from `parseResult`.
4. **Group entries by slotId** (preserve order within each group).
5. For each slot group:
   - **If `slotId == "*"`**: find kit, call `this->mapKit(*kitSpec)`. No per-articulation routing for wildcard.
   - **Primary entry** (first in group):
     - Resolve the `InstrumentSpec` source.
     - Build `ArticulationSourceMap` from entries in the group that specify `art=` filters and target different instruments.
     - Call `slot->loadInstrumentWithSources(primaryInstSpec, artSourceMap)`.
     - Apply parameter overrides.
   - **Layered entries** (same slot, same articulation name as primary):
     - Create shadow `InstrumentSynth` instance.
     - `shadow->loadInstrument(layerInstSpec)` with only the overlapping articulations filtered.
     - Register shadow in `instrumentSynthsByNote` for relevant notes.
     - Apply its own parameter overrides.
6. **Do NOT re-build MIDI note mappings for the primary.** Note mappings are already built by `applyKitLayout()`. Shadow synths do need note registration.
7. After all entries applied, trigger `sampleLoaderPtr->getQueue().startProcessing()`.

### Step 6: Enable in `useState()`

**File:** `source/Engine/DrumKitEngine.cpp`

- Uncomment `this->useInstrumentMap(instrumentMap)`.
- Remove the `LOG(Warning, "Custom instrument map not supported yet")` line.
- If `useInstrumentMap` returns false, fall back to `mapDefaultKit()`.

### Step 7: Extend `LibraryManager::findKit`

**File:** `source/Engine/KitLibModel.cpp`

- Also match `kit->meta.codeRaw` in addition to `kit->meta.name`.

### Step 8: UI — Instrument Map Textarea

**File:** `source/PluginEditor.h` / `source/PluginEditor.cpp`

Add a minimal UI so the user can view and edit the instrument map text without reloading the plugin:

- `juce::TextEditor instrumentMapEditor` — multiline, full instrument map text
- `juce::TextButton applyButton` — triggers re-apply: reads text from editor, calls `processor.getEngine().useInstrumentMap(text)`, refreshes status
- `juce::Label statusLabel` — shows parse warnings/errors from the last apply (populated from `ParseResult::error()` / warnings list)
- On editor construction: populate `instrumentMapEditor` from `treeState.state["instrument_map"]`
- On apply: write new text back to `treeState.state["instrument_map"]` so it persists with plugin state

This is intentionally minimal — full per-instrument/per-mic mixer UI is deferred with the APVTS work.

### Step 9: Tests

**File:** `tests/TestInstrumentMap.cpp` (new)

Test cases for `parseInstrumentMapText`:
1. Empty text → error
2. Only comments → error (no entries)
3. Simple `slotId:kitName/instCode` → 1 entry parsed correctly
4. With params `slotId:kit/inst:tuning=-2,attack=50` → params populated
5. Wildcard `*:kitName` → slotId is `*`, instCode empty
6. Unknown param key → warning but entry still valid
7. Malformed param (no `=`) → warning, param skipped
8. Missing kit ref → error for that line, warning in result
9. Multiple entries → all parsed
10. `art=bow,edge` → `articulationFilter` = `["bow", "edge"]`
11. Two entries same slot, same articulaton → layering detected (in `useInstrumentMap`)
12. Two entries same slot, different articulations → per-articulation routing (in `useInstrumentMap`)

Test cases for `useInstrumentMap` (require mocked/test kit data):
1. Valid map with known kit/instrument → instruments loaded
2. Unknown kit name → warning, slot skipped
3. Unknown instrument code → warning, slot skipped
4. Unknown slot id → warning, entry skipped
5. Wildcard entry → `mapKit` called
6. Parameters applied → verify tuning/attack/decay on InstrumentSynth
7. Per-articulation routing → verify different sources loaded for different articulations
8. Layering → verify shadow synth created and registered in `instrumentSynthsByNote`
9. Missing articulation in source → warning logged, articulation skipped gracefully

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
  │             │   ├─ parses entries + params + art filters
  │             │   └─ returns structured result with warnings
  │             │
  │             ├─ group entries by slotId
  │             │
  │             ├─ for each slot group:
  │             │   ├─ resolve kit (findKit) + instrument (findInstrument)
  │             │   ├─ build ArticulationSourceMap from art= filters
  │             │   ├─ primary: slot->loadInstrumentWithSources(instSpec, artSources)
  │             │   ├─ for each layered articulation:
  │             │   │   ├─ create shadow InstrumentSynth
  │             │   │   ├─ shadow->loadInstrument(layerInstSpec)
  │             │   │   └─ register shadow in instrumentSynthsByNote
  │             │   └─ apply param overrides (tuning, attack, decay)
  │             │
  │             └─ sampleLoader->startProcessing()
```

---

## Files Changed

| File | Change |
|------|--------|
| `source/Engine/DrumKitEngine.h` | Add `InstrumentMapEntry`, `InstrumentMap` structs; update `parseInstrumentMapText` signature |
| `source/Engine/DrumKitEngine.cpp` | Rewrite `parseInstrumentMapText`, `useInstrumentMap`; update `useState`; shadow synth creation for layering |
| `source/Engine/InstrumentSynth.h` | Add tuning/attack/decay override members and setters; add `ArticulationSourceMap` typedef; declare `loadInstrumentWithSources` |
| `source/Engine/InstrumentSynthLoader.cpp` | Implement `loadInstrumentWithSources`; modify `processLayoutArticulations` to accept articulation source overrides |
| `source/Engine/KitLibModel.cpp` | Extend `findKit` to match `codeRaw` |
| `source/PluginEditor.h` / `.cpp` | Add instrument map textarea, apply button, status label |
| `tests/TestInstrumentMap.cpp` | New test file - parsing, per-articulation routing, layering |
| `cmake/Tests.cmake` | Add new test source |

---

## Key Design Decisions

1. **MIDI note mapping stays in `applyKitLayout()`** - The instrument map changes *content* of slots, not *routing*. This avoids duplicating MIDI mapping logic and keeps concerns separated. Shadow synths created for layering are the only exception - they need note registration.

2. **Parameter overrides are instrument-level, not voice-level** - Tuning/attack/decay are set on `InstrumentSynth` and propagated to voices at note-on time. This is simpler and matches the mental model ("this snare is tuned down 200 cents").

3. **`ParseResult<>` pattern** - Consistent with the rest of the codebase (`parseConfigYaml`, `parseInfo`, `parseYaml` for layouts).

4. **Wildcard `*` for whole-kit** - Cleaner than overloading empty instCode to mean "apply whole kit". The `*` is explicit.

5. **`std::optional` for params** - Clear distinction between "not specified" (use default) and "specified as 0".

6. **Per-articulation routing via `ArticulationSourceMap`** - Modify `processLayoutArticulations` to look up each articulation's source from a map, falling back to the default instrument. This is minimally invasive - the existing loading pipeline is preserved, just with a configurable source per articulation.

7. **Layering via shadow `InstrumentSynth` instances** - Reuses the existing multi-synth-per-note infrastructure (`instrumentSynthsByNote` is already a vector). No changes to the real-time playback path. Shadow synths are "invisible" to the user (not in `instrumentSynthById`), owned by the engine, and share MIDI routing with the primary slot.

---

## VST Automatable Parameters (APVTS) — Deferred

Full APVTS planning is deferred to a later TODO. Short summary of what was decided so far (for reference when that work starts):

- **Per-instrument params** (tuning, attack, decay, mute, solo) — applied at note-on in `InstrumentSynth`. IDs `inst01_*`..`inst32_*`, fixed maximum of 32 slots for stable DAW automation lane IDs.
- **Per-mic params** (volume, pan, phase, flipLR, mute) — applied as a post-render pass on host output channels in `processBlock`. IDs `mic01_*`..`mic32_*`. Per-mic not per-instrument because shared mics (Overheads, Room) capture all instruments simultaneously.
- **Instrument map values are defaults**, APVTS params are live state — loading a map sets APVTS values, DAW automation can override at any time.
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

**Instruments** own tuning, attack, decay, mute, solo — properties of "what is being hit".
**Mics** own volume, pan, phase, flipLR — properties of "how it's captured/mixed".

An instrument can be captured by many mics (kick1 has Kick In + Kick Out + Overheads + Room). A shared mic (Overheads) captures ALL instruments. So vol/pan/flip are per-mic-slot, not per-instrument.

### What should NOT be an APVTS parameter

| Thing | Why not |
|-------|---------|
| Instrument map text | Formatted string, no meaningful automation curve. Stored in ValueTree state. |
| Kit layout selection | Structural change (rebuilds all slots). ValueTree state or non-automatable `AudioParameterChoice`. |
| Mic layout selection | Structural change (rebuilds output routing). Same. |
| Articulation source routing | Part of instrument map config, not real-time. |

### Per-Instrument APVTS parameters

Applied at note-on time inside `InstrumentSynth` / voice `startNote()`. The kit layout defines instrument slots; the count is variable but we use a fixed maximum for stable parameter IDs.

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
- A shared mic (Overheads) receives `addSample` from ALL instruments — the gain must be applied once after all instruments have rendered, not per-voice
- This keeps voice code simple and avoids duplicating mic-level logic across instruments

```cpp
// In DrumKitEngine::processBlock, AFTER all instrumentSynth->renderNextBlock:
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
    tuning          kick1:Kit/kick-24:tuning=-200     inst01_tuning = -200
    attack          kick1:Kit/kick-24:attack=50       inst01_attack = 50
    decay           kick1:Kit/kick-24:decay=300       inst01_decay = 300

Per-mic (from instrument map):
    gain            kick1:Kit/kick-24:gain=-3         mic04_volume = -3    (Kick In mic)
    flip            kick1:Kit/kick-24:flip=1          mic04_flipLR = true

Per-mic (standalone, not from instrument map — configured in UI or via automation only):
    pan                                               mic01_pan = 0.3      (Overheads)
    phase                                             mic07_phase = true   (Snare Bottom)
```

When the instrument map specifies a mic-level param (e.g. `gain=-3` on `kick1`), it needs to resolve which mic(s) are affected. Options:
- `gain=-3` on an instrument → applies to ALL mics assigned to that instrument (Kick In, Kick Out, Overheads, Room)
- `gain[dim]=-3` → applies only to mic with code `dim` (Kick In) for that instrument — the `bleed[micCode]` syntax from the format comments

This per-mic-per-instrument addressing (`gain[micCode]`) is a future extension. For now, instrument-level gain in the map can set a default for the instrument's primary/direct mic.

---

## Risks / Open Questions

- **Parameter application timing**: If instrument map is re-applied after samples are already loaded, parameter overrides must be re-applied. Consider storing overrides persistently on the slot so they survive re-loads.
- **FX chain parameter**: Mentioned in the TODO comment but deferred. Would need a named fx chain registry, which doesn't exist yet.
- **Instrument re-mapping without full reload**: Currently `loadInstrument` does a full teardown+rebuild. If only params change, a lighter-weight path could be added later.
- **Interaction with `mapKit`**: If the map contains `*:kit` followed by `snare1:otherKit/snare`, the wildcard maps everything and then the specific entry overrides one slot. Order matters - document this.
- **Shadow synth lifetime**: Shadow synths must be cleaned up when the instrument map changes or `applyKitLayout` is called again. Need a `clearShadowSynths()` step at the start of `useInstrumentMap`.
- **Shadow synth mic routing**: Shadow synths should inherit the mic output mapping from their primary slot. If the mic layout changes after layering is configured, shadows must be updated too.
- **Articulation filter + expression dimensions**: The `art=bow` filter matches by `nameId` only. An instrument with `bow[open]`, `bow[half]`, `bow[closed]` would have all three loaded when `art=bow` is specified. This is the desired behavior - expression variants of the same articulation stay together.
- **Voice stealing across layers**: When a shadow synth and primary synth both play on the same note, voice stealing is independent per synth. This is correct — each synth manages its own voice pool. However, the total voice count across layered synths is higher. Consider if `findVoiceToSteal` needs awareness of sibling layers (likely not — keep it simple for now).