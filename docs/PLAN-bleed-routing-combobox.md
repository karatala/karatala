# ✅ COMPLETED: Bleed Routing Combobox Plan

## Goal

The bleed routing combobox now changes and saves the loaded sample output route.

- Bleed level knob stays live and cheap.
- Routing combobox becomes a saved instrument-map setting.
- Routing combobox reroutes loaded samples in place and applies the affected slot only when the mic is not loaded.
- Debug logs show exactly what changed.

## Practical Example

Current bleed level setting:

```text
kick1/center:MyKit/kick-24/center:bleed=dtm:0.5,room:0.8
```

Routing override:

```text
kick1/center:MyKit/kick-24/center:bleed=dtm:0.5,room:0.8;micRoute=dtm:aux1/**m
```

Meaning:

- `bleed=dtm:0.5` means mic `dtm` plays at 50 percent.
- `bleed=room:0.8` means `room` plays at 80 percent.
- `micRoute=dtm:aux1/**m` means mic `dtm` should be written to the mic-layout output whose stable ID is `aux1/**m`.

Why semicolon:

- `bleed=` already uses commas inside its value.
- `;micRoute=...` makes the next top-level param clear.

## Current Facts

- `PadContextPopup::rebuildBleedControls()` creates each bleed pot and routing combo.
- Bleed pot calls `applyLayerParametersFast()` and updates the current `InstrumentPlayer` live.
- Routing combo updates its display, reroutes the loaded layer, and saves the map text.
- `InstrumentMapLayer` stores both `micBleedLevels` and `micRouteOverrides`.
- `MicRoutingBuilder::resolve()` assigns samples to host output channels during load.
- `SampledSound::micBufferChannelStart`, `micBufferChannelCount`, and `routedMicCode` are set during sample routing.
- Output channel assignment is stored in loaded sample data, but an existing loaded assignment can be changed safely under the engine graph lock.

## Map Format Change

Add a new param:

```text
micRoute=micCode:outputId,micCode:outputId
```

Rules:

- `micCode` is the library/mic code before routing.
- `outputId` is the stable ID declared by the mic-layout output, for example `snare1/dtm` or `aux1/**m`.
- Missing entry means automatic routing.
- Output `-` means the mic is unassigned.
- Unknown `outputId` should warn and use automatic routing.
- Unknown `micCode` should warn only when a sample actually tries to use it, or should be ignored during apply if no matching mic exists.
- Serializer should write route pairs in stable sorted order.

Parser examples:

```text
kick1/center:MyKit/kick-24/center:micRoute=dtm:aux1/**m
kick1/center:MyKit/kick-24/center:bleed=dtm:0.5,room:0.8;micRoute=dtm:aux1/**m,dss:aux1/**s
```

## Engine Support

### 1. Parse And Serialize

Add to `DrumKitEngine::InstrumentMapLayer`:

```cpp
std::unordered_map<juce::String, juce::String> micRouteOverrides;
```

Update:

- `DrumKitEngineMappingModel::parseInstrumentMapText()`
  - Add `micRoute` to `knownKeys`.
  - Parse `micCode:outputId` pairs.
  - Store normalized trimmed keys and values.
- `DrumKitEngine::serializeInstrumentMap()`
  - Serialize `micRoute=` after `bleed=`.
  - Sort by mic for stable output.
- `DrumKitEngineMapHelpers::makeInstrumentMapLayerFingerprint()`
  - Include route overrides.
- `DrumKitEngineMapHelpers::makeInstrumentMapLayerReloadFingerprint()`
  - Include route overrides so raw map changes still rebuild the affected slot when needed.

### 2. Pass Overrides to Instrument-Player Loading

Options:

1. Store route overrides in `InstrumentPlayer::RoutingContext`.
2. Store route overrides as a separate `InstrumentPlayer` field set before loading samples.

Recommendation: put it in `RoutingContext`, because route choice is part of load-time routing.

Add:

```cpp
std::unordered_map<juce::String, juce::String> micRouteOverrides;
```

Then copy it wherever routing context is copied or reused.

Apply from map layers:

- The primary instrument player gets the first map layer's route overrides.
- Each additional instrument player gets its own map layer's route overrides.
- Pad popup route changes update loaded sample assignments in place. Raw map route changes remain reload changes.
- Repeated output codes stay as separate destinations through their unique route IDs.
- Full map loading checks the original mic code first, then the mic's resolved output-layout alias.
- Mic-code variants such as `dtm1` and `dtm2` are retained by library loading and use separate popup controls and routes.
- A variant-free output such as `dtm` may receive either variant. A variant-specific output only receives its matching variant.
- The popup shows the resolved output as the default choice instead of adding a separate `Auto` item. Selecting the default output clears the explicit override.
- Output names remain the visible labels. Saved values use unique route IDs, so `Snr1 Top`, `Aux1`, `Aux2`, `Aux3`, and `Aux4` remain separate choices even though the aux outputs use wildcard codes such as `**m`.
- Only mic-layout target output codes allow `*`, independently for type and position. Mic codes and sample filenames stay concrete. Format stays exact, and a present variant must match.

### 3. Use Override In MicRoutingBuilder

In `MicRoutingBuilder::resolve()`:

1. Check if `micSpec.raw` has an override.
2. Find active mic-layout output with code matching the override target.
3. Confirm channel count and compatibility are usable.
4. Return that output assignment.
5. If invalid, log warning and continue to automatic routing.

Example log:

```text
Mic route override: instrumentSlot=kick1 micCode=dtm outputId=aux1/**m hostStart=24 channels=1
Mic route override ignored: instrumentSlot=kick1 micCode=dtm outputId=missing/dtm reason=output not found
```

## Pad Popup Behavior

### 1. Store Combo Choice Per Row

Extend `MapLayerRow::MicBleedControl` with selected route state:

```cpp
juce::String selectedOutputId;
```

Or store enough data in `MicRoutingChoice`:

```cpp
juce::String outputId;
```

The output name is the visible label. `outputId` is the stable saved selection.

### 2. Load Existing Route From Map

When `populateLayers()` reads `InstrumentMapLayer`, pass `mapLayer.micRouteOverrides` into `addMapLayerRow()`.

When `rebuildBleedControls()` builds controls:

- Use existing override for that `micCode` if present.
- Select that combobox item if available.
- If saved override is missing from active mic layout, show warning style and keep it in map until user changes it.

### 3. On Combo Change

When user changes a route combobox:

1. Update that row's route override map.
2. Log old route and new route.
3. Update the loaded layer through the route-specific engine API.
4. Mark map text dirty and save it without applying the full map.
5. Fall back to `applyToEngine()` only when the mic is not already loaded.
6. Play preview note if useful.

Simple log:

```text
Pad popup route: instrumentSlot=kick1 articulationSlot=center mapLayer=1 micCode=dtm old=auto new=room
Pad popup route: instrumentSlot=kick1 articulationSlot=center mapLayer=1 micCode=dtm old=auto new=aux1/**m
```

### 4. Keep Bleed Knob Live

Do not change bleed knob behavior:

- Moving bleed pot calls `applyLayerParametersFast()`.
- It updates current instrument player state live.
- It also marks map text dirty for persistence.
- It does not rebuild the slot.

## Debug Info Plan

Add debug logs only on message-thread changes and load-time routing, not per sample.

### Pad Popup Parameter Change

When `applyLayerParametersFast()` runs, log one compact line:

```text
Pad popup params: instrumentSlot=kick1 articulationSlot=center mapLayer=1 libraryPath=MyKit/kick-24/center tuning=-20 attack=-1 decay=-1 gain=-3.5 pan=0 flipLR=0 flipPhase=0 bleed=dtm:0.50,room:0.80
```

Avoid logging on every mouse movement if it becomes too noisy. If needed, only log on drag end for pots and immediately for toggles.

### Route Combobox Change

```text
Pad popup route: instrumentSlot=kick1 articulationSlot=center mapLayer=1 micCode=dtm old=auto new=room
```

### Slot Apply

```text
Instrument map apply: changedSlot=kick1 reason=pad-popup micRoute
```

### Engine Route Override

```text
Mic route override: instrumentSlot=kick1 micCode=dtm outputId=aux1/**m hostStart=24 channels=1
```

Invalid route:

```text
Mic route override ignored: instrumentSlot=kick1 micCode=dtm outputId=missing/dtm reason=output not found
```

## Tests

### Parser Tests

Add to `tests/TestInstrumentMap.cpp`:

- `micRoute=dtm:aux1/**m` parses.
- `micRoute=dtm:aux1/**m,dss:aux1/**s` parses.
- `bleed=dtm:0.5,room:0.8;micRoute=dtm:aux1/**m` parses correctly.
- Serializer preserves `micRoute` in stable order.
- Unknown/malformed pairs warn or skip, depending on chosen policy.

### Fingerprint Tests

Add focused checks that:

- State fingerprint changes when only `micRoute` changes.
- Reload fingerprint changes when only `micRoute` changes.
- `bleed` still changes state fingerprint but does not need reload fingerprint unless current design says otherwise.

### Routing Tests

If practical, add a small `MicRoutingBuilder` or engine-level test:

- Mic `dtm` normally routes to `dtm` output.
- With `micRoute=dtm:aux1/**m`, it routes to the output whose stable ID is `aux1/**m`.
- Invalid override falls back to automatic routing and warns.

## Manual Verification

1. Open pad popup with a layer that has bleed controls.
2. Move a bleed knob.
   - Sound level changes live.
   - No slot reload should be needed.
   - Map text persists `bleed=`.
3. Change routing combobox.
   - Map text gets `micRoute=`.
   - Slot applies/reloads.
   - Sound moves to selected output.
   - Reopen popup, selection is still there.
4. Try invalid raw map route.
   - Engine warns.
   - Automatic routing still works.
5. Change raw map `micRoute=` and apply.
   - Same behavior as popup.

## Implementation Order

1. Add parser storage, parse, serialize, and tests.
2. Add fingerprints.
3. Add route override field to instrument player routing context.
4. Add `MicRoutingBuilder` override lookup.
5. Wire pad popup combobox to map text and slot apply.
6. Add debug logs.
7. Add `docs/INSTRUMENT_MAP_FORMAT.md` or update it if it already exists.
8. Add Doxygen parser comments from `PLAN-instrument-map-format-and-routing-docs.md`.

## Open Decisions

- Use `micRoute=` or `route=`. Recommendation: `micRoute=`.
- Should route combobox apply immediately on selection, or wait for popup close? Recommendation: immediate apply, because user expects routing choice to be audible.
- Should invalid saved route remain visible? Recommendation: keep it visible with warning style so user can fix it.
- Should debug param logs fire on every pot movement? Recommendation: route/toggle logs immediately, pot logs on drag end if full live logging is too noisy.
