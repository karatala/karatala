# ✅ COMPLETED: TODO11: Pad Popup Layer Parameters Plan

Date: 2026-06-05

## Goal

Finish the existing `PadContextPopup` controls so `Gain`, `Flip L/R`, `Flip Phase`, and per-mic bleed are real per-layer instrument-map parameters and are applied to the corresponding `InstrumentSynth` layer.

Keep the current map syntax:

- `vol=<dB>`
- `flipLR=1`
- `flipPhase=1`
- `bleed=micCode:0..1,micCode:0..1`

## Current State

- `PadContextPopup` already shows Gain, Flip L/R, Flip Phase, and per-mic bleed pots.
- `PadContextPopup::generateFullMapText()` already serializes `vol`, `flipLR`, `flipPhase`, and `bleed`.
- `DrumKitEngineMappingModel::parseInstrumentMapText()` already parses those fields into `InstrumentMapEntry`.
- `DrumKitEngine::useInstrumentMap()` only applies tuning, attack, and decay to synths.
- `DrumKitEngine::updateSlotParameters()` only supports tuning, attack, and decay.
- `InstrumentSynth` has runtime state for tuning, attack, and decay, but not gain, LR flip, phase flip, or bleed.
- Current bleed controls are based on the pad slot mic list, but different loaded layers can have different routable source mics.

## Implementation Plan

### 1. Extend Layer Parameter Model

Add a small typed update struct in `DrumKitEngine`, for example `LayerParameterUpdate`, containing optional values for:

- tuning cents
- attack ms
- decay ms
- volume dB
- flip L/R
- flip phase
- mic bleed levels

Replace or overload `updateSlotParameters()` so `PadContextPopup` can update all live-safe layer controls through one API. Keep `InstrumentConfigPanel` working by adapting its current tuning/attack/decay call to the new struct.

### 2. Add Runtime State To InstrumentSynth

Extend `InstrumentSynth` with setters/getters for:

- `volumeDb`, default `0.0`
- `flipLR`, default `false`
- `flipPhase`, default `false`
- `micBleedLevels`, default empty meaning all routed mics are `1.0`

Update synth copy/clear behavior so replacement synths preserve or reset these values consistently with existing tuning/attack/decay behavior.

### 3. Apply Instrument Map Entries Fully

Update `DrumKitEngine::useInstrumentMap()` so `applyOverrides()` applies all parsed layer parameters, not only tuning/attack/decay.

Include `vol`, `flipLR`, `flipPhase`, and `bleed` in `makeInstrumentMapEntryFingerprint()` so changes made in the raw map editor are detected and applied.

Treat gain, flip, phase, and bleed as layer-separating overrides when deciding whether later entries need shadow synths.

### 4. Apply Audio Behavior During Rendering

Apply the new runtime parameters during voice rendering:

- Gain: convert dB to linear and multiply the rendered sample.
- Flip Phase: multiply by `-1`.
- Flip L/R: for stereo routed sample variants, swap the two routed channels.
- Bleed: find the routed mic code for the sample variant and multiply that mic by the configured `0..1` value.

Mono mics are unaffected by L/R flip. Missing bleed values default to `1.0`.

### 5. Make Bleed Pots Per Layer

Replace the popup’s slot-wide mic list with a per-layer routable mic resolver.

For each layer, show only active mic-layout mics that can actually receive sample variants for that selected source/articulation. Use the active mic layout `mic.code` as the saved map key and `mic.name` as the display label.

When the source changes, rebuild that row’s bleed pots from the newly routable mic set and preserve values for matching mic codes.

## Tests

Extend `tests/TestInstrumentMap.cpp` to verify parsing of:

- `vol`
- `flipLR`
- `flipPhase`
- `bleed=micCode:level,micCode:level`

Add focused behavior coverage where practical for:

- instrument-map fingerprints changing when gain/flip/phase/bleed changes
- layer parameter updates applying the new values
- bleed values clamping or defaulting safely

Run the existing test target from `build`.

## Manual Verification

Verify in the plugin UI:

- Pad popup gain changes are audible immediately and persist to `instrument_map`.
- Flip phase audibly inverts the layer and persists.
- Flip L/R swaps stereo mic channels and persists.
- Bleed pots scale only their matching routed mic for that layer.
- Different layers can show different bleed pot sets when their selected source/articulation exposes different routable mics.
- Source changes still use full `useInstrumentMap()` and can reload samples.

## Assumptions

- These controls remain instrument-map layer settings, not host-automatable APVTS plugin parameters.
- Bleed pots are mic gain trims: `100%` is unchanged, `0%` mutes that mic for that layer.
- Source changes continue to use full map application because they may require sample loading.