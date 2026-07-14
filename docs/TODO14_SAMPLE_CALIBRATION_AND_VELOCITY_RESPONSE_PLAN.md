# TODO14: Sample Calibration And Velocity Response Plan

Date: 2026-07-14

Status: Design plan only. No implementation or verification has started.

## Scope And Terminology

- `layer`: one source instrument articulation, with its own parameters, assigned to a destination pad articulation through the pad popup or Instrument tab.
- `sample velocity level`: one recorded sample group such as `043a` or `127a`. This is not a mapped layer.
- `global loudness response`: the user-controlled target loudness for MIDI velocities 1 to 127.
- `source response profile`: measured intensity values for the recorded velocity levels of one source articulation.
- `sample calibration trim`: gain that removes unwanted recording-level differences.
- `layer calibration override`: a manual correction saved for one map layer.
- `source balance`: detected recorded position using the existing folder codes `L1..L127`, `C0`, and `R1..R127`.
- `pan`: the existing manual layer placement offset.

Do not use one generic curve for both sample selection and output loudness. They are separate problems.

This plan makes velocity and loudness calibration the first release. Pan recommendations and timing reports follow after the core works. Runtime timing correction and audio rewriting need a later plan after listening review.

## Goal And Acceptance Criteria

For every calibrated source articulation:

- MIDI velocity 127 reaches one agreed maximum reference level.
- Lower velocities follow one global, user-controlled loudness response.
- Incorrect source velocity names do not create large loudness jumps.
- Recorded soft and hard samples are still selected for their timbre.
- Every layer in a layered pad keeps its own automatic calibration and manual override.
- Existing `vol` remains a creative layer mix control, shown as `Mix Gain` in the UI.

The maximum reference is measured before `Mix Gain`, bleed, routing, DAW faders, and the sum of several layers. Those later changes can change final output level.

Success means:

- calibrated kits stay within an agreed analysis and listening tolerance at velocity 127;
- useful lower velocities stay within an agreed tolerance of the global response;
- sample changes reduce level steps to the agreed tolerance;
- manual changes affect only the chosen primary or shadow layer;
- manual response changes rebuild lookup tables without reloading sample audio;
- library filename trim or preprocessing changes reload only affected library content;
- analyzer report mode never changes files or folders.

Phase 1 must choose the actual metric, reference level, and tolerances. This document does not invent those numbers.

## Plan Decisions

These choices are part of this plan:

- Calibrate every source articulation to one common maximum first. Listening may later justify explicit instrument-family offsets.
- Calibrate every mapped layer independently. Do not reduce gain because a pad has several layers.
- Keep one machine-wide default loudness response, but copy it into plugin instance state so saved sessions remain stable.
- Use the median response of round robins at each recorded velocity first. Report outliers instead of automatically flattening each round robin.
- Use a hybrid metadata design:
  - short whole-file values such as sample trim stay in the sample filename;
  - existing `L/C/R` source balance stays in instrument or articulation folder names;
  - full response profiles and per-mic timing or orientation use a versioned calibration sidecar in the instrument folder.
- Use overhead stereo as the source-balance reference.
- Timing analysis is report-only in this plan. It must separate likely interface latency from natural acoustic delay.
- Do not modify audio files in this plan.

Phase 1 still needs measured answers for:

- the hit loudness metric and analysis window;
- the loudness reference mic and fallback rule;
- maximum target, allowed positive trim, and peak headroom;
- acceptance tolerance;
- source-balance center dead zone and confidence threshold.

## Current Facts And Constraints

- `../source/Engine/KitLibModel.cpp` reads sample velocity from filenames.
- `../source/Engine/InstrumentSynth.h` builds a 128-value table that selects the first recorded velocity greater than or equal to raw MIDI velocity.
- Playback gain is currently raw MIDI velocity divided by 127.
- Preview, voice loudness estimates, voice removal, and hi-hat transitions also depend on this linear gain in several paths.
- One `InstrumentMapEntry` is one layer. Extra matching entries use shadow `InstrumentSynth` objects.
- Current layer params are `tuning`, `attack`, `decay`, `vol`, `pan`, `flipLR`, `flipPhase`, `bleed`, and `micRoute`.
- Sample filename metadata already supports whole-dB `v` gain trim plus `p`, `d`, and `c` peak and decay data.
- Gain trim is applied while loading audio, but effective peak and decay level estimates do not fully include that change.
- The sample-loader cache key does not currently include the gain transform.
- Instrument and articulation folders already parse `L1..L127`, `C0`, and `R1..R127`. Articulation balance wins over instrument balance.
- Current automatic balance mainly affects direct mono mics routed to stereo.
- Current layer pan can affect direct, overhead, and room paths together. This is not selective enough for the requested rule.
- Current whole-layer `flipLR` cannot repair only one reversed room mic.
- The pad popup and Instrument tab can rebuild map text through different paths. New fields must not be lost by an editor that does not display them.
- All runtime velocity and routing data must be precomputed. The audio thread must not parse text, allocate, lock, scan files, or log.

Current `../scripts/analyze_drum_samples.py` has blocking issues:

- it analyzes every file separately instead of grouping one hit across mic files;
- it converts multichannel audio to a positive RMS mono signal before LUFS analysis, losing pan, phase, sign, and timing;
- full-file integrated LUFS is not a stable hit-loudness value for both short drums and long cymbals;
- its filename parser differs from the engine parser;
- rename mode can drop existing optional metadata and does not validate all collisions first;
- after rename, it writes the new path into the CSV `instr` column;
- decay analysis can encode padded silence as valid points;
- it does not calculate true peak, source balance, mic timing, or calibration profiles.

## Proposed Core Model

Keep four independent values:

| Value | Scope | Purpose |
| --- | --- | --- |
| Global loudness response | plugin instance | raw MIDI velocity to target relative dB |
| Source response profile | library articulation | target intensity to recorded sample selection |
| Sample calibration trim | sample group | remove recording-level differences |
| Layer calibration override | instrument map layer | correct automatic selection or residual gain manually |

`Mix Gain` is applied after them as a creative offset.

### Playback flow

For every mapped layer:

1. Keep raw MIDI velocity for crosstalk and other MIDI input rules.
2. Read target relative dB from the global 128-value loudness table.
3. Use target relative dB and the source response profile to select a recorded sample velocity level.
4. Play the sample with its calibration trim.
5. Apply the global target gain instead of the old `velocity / 127` gain.
6. Apply any manual layer residual correction, then `Mix Gain`, bleed, envelopes, choke, and routing gains.

The source profile changes timbre selection. The global response controls output loudness.

Build outside the audio thread:

- one global 128-value gain table;
- one 128-value sample-selection table for each loaded layer articulation;
- one 128-value manual correction table when needed.

Interpolation of loudness and gain correction happens in dB. Table output used for rendering is linear gain.

### Source profile and trim

The analyzer groups one logical hit by:

```text
kit + instrument + articulation + named velocity + round robin
```

It then finds the linked mic files and channel ranges for that hit.

- Measure source response before calibration gain.
- Aggregate each recorded velocity across round robins with a robust median.
- Fit a monotonic profile while keeping original velocity order as a strong hint.
- Report strong inversions for review instead of silently reordering samples only by loudness.
- Calculate one velocity-level trim from the chosen reference mic.
- Apply the same trim to all linked mic files so direct, overhead, and room balance is preserved.
- Report unusual round robins, noise risk, and peak-headroom risk.

If an articulation has too little valid data, use a clearly marked instrument-level profile only when it is compatible. Otherwise mark the layer `Uncalibrated` and use original velocity selection.

### Metadata ownership

Recommended runtime ownership:

| Location | Data |
| --- | --- |
| Sample filename | scalar trim, peak, and decay values |
| Instrument or articulation folder | existing source balance code only |
| Versioned instrument calibration sidecar | source profiles, confidence, metric version, source audio fingerprints, per-mic timing, and per-mic orientation |
| Instrument map | manual override for one mapped layer |
| Analyzer report | raw measurements, warnings, proposed changes, and original paths |

The exact sidecar name and schema are chosen in Phase 3. Stable articulation keys must use the current library identity rules and must not rely on an ambiguous short name such as `center`.

The format must distinguish measured, proposed, runtime-applied, and audio-baked values. This prevents double correction after a later audio-editing tool is added.

Keep the existing filename `v` purpose, but add enough decimal precision for calibration. The final spelling is chosen with parser fixtures before any library rename.

Include the sidecar and its source audio fingerprints in library staleness and content-fingerprint checks.

### Global and layer controls

- Store the global response as a small set of monotonic MIDI-to-dB points with velocity 127 fixed at `0 dB` relative.
- Build the full table outside playback.
- Add a simple global editor with reset and machine-default actions.
- Add `Automatic` and `Manual` modes per layer. `Uncalibrated` is a status, not a user mode.
- Manual mode starts from library calibration and can adjust sample selection plus a small residual dB curve.
- Reset removes the map override and returns to library calibration.
- Keep `Mix Gain` separate.
- Manual changes use the parameter-only update path and do not reload samples.
- A changed filename trim or sidecar requires a targeted library reload.

Both layer editors must share map-entry serialization or preserve fields they do not show. Primary and shadow synths each own their effective tables.

### Consistent loudness estimates

The same effective gain must be used by:

- normal notes and previews;
- voice culling and stealing;
- adaptive tail shortening;
- hi-hat transition loudness matching;
- implicit hi-hat pedal-event velocity.

Do not redesign the accepted hi-hat transition policy. Change only its loudness input, then use focused tests and listening review.

### Source balance, room orientation, and timing

Pan analysis uses real stereo overhead channels, a fixed post-onset window, a center dead zone, and a confidence value.

- Write instrument balance only when its articulations agree.
- Otherwise write articulation balance.
- Preserve existing balance when confidence is too low.
- Treat source balance as descriptive. Do not apply it twice to an overhead recording that already contains that position.
- Preserve overhead stereo placement after any required source-to-slot orientation.
- Apply manual layer pan as an overhead offset.
- Pan mono mics only when routed to an approved stereo group or aux.
- Leave direct mono mics on their own outputs for DAW panning.
- Skip automatic room pan.

Possible room L/R reversal is per mic, not per layer. Report it only when room direction is strongly and repeatedly opposite to overhead. Store a reviewed per-mic orientation in the sidecar instead of using whole-layer `flipLR`.

Timing analysis compares linked hits across many samples. A stable repeated delay for one mic or interface is possible technical latency. A delay that changes by instrument or distance is likely acoustic. Report both, but do not apply timing correction in this plan.

## Implementation Phases

### Phase 1: Choose metric and acceptance values

- Build a small reference corpus with several kits, drums, cymbals, and short percussion.
- Compare fixed post-onset loudness candidates, short RMS, true peak, and listening judgments.
- Choose reference mic rules, target, trim cap, headroom, tolerance, pan dead zone, and confidence.

Verify later: record the chosen values and listening result in this plan.

### Phase 2: Make analyzer output correct and safe

- Separate parsing, measurement, grouping, reporting, and apply logic.
- Preserve signed channels and decode mic channel ranges.
- Match the engine filename parser.
- Group logical hits and add onset, fixed-window loudness, true peak, channel balance, and corrected decay measurement.
- Make report-only mode the default.
- Preserve all metadata and validate all rename collisions before any write.
- Fix the CSV and decay problems listed above.

Verify later: mono, stereo, separate-file, and multichannel fixtures; no-write report mode; parser round-trip; deterministic output; collision protection.

### Phase 3: Define metadata and generate calibration

- Finalize decimal sample trim and the versioned sidecar schema.
- Add Python schema validation and C++ parsing.
- Add source audio fingerprints and stale-calibration warnings.
- Fit monotonic source profiles and calculate safe trim recommendations.
- Add explicit metadata apply mode with complete rollback on a failed group.
- Include calibration in library content fingerprints.

Verify later: Python-to-engine round-trip; missing, invalid, and stale fallback; repeated apply without duplicate correction; reference-corpus predictions within Phase 1 tolerance.

### Phase 4: Add global runtime response

- Persist global curve points in instance state and machine defaults.
- Build global gain and per-layer selection tables outside playback.
- Replace raw linear velocity gain without multiplying both systems.
- Include preprocessing in sample cache identity.
- Update preview, voice management, and hi-hat loudness estimates.

Verify later: deterministic monotonic tables; velocity 127 at `0 dB` relative; sample-boundary levels within tolerance; focused gain-consumer tests; hi-hat listening comparison.

### Phase 5: Add manual per-layer controls

- Add map fields for mode, manual source response, and residual correction.
- Extend parser, serializer, fingerprints, and parameter-only updates.
- Add detailed pad-popup controls and compact Instrument-tab status.
- Fix shared serialization before either editor can save the new fields.
- Address layers by destination articulation, source, and layer occurrence.

Verify later: independent primary and shadow edits; two articulations in one slot; no sample reload; save and reload; reset to automatic calibration.

### Phase 6: Add balance and timing reports

- Add high-confidence overhead balance recommendations and reviewed folder updates.
- Precompute mic-aware placement modes for overhead and approved mono-to-stereo routes.
- Add per-mic room-orientation recommendations.
- Add report-only mic timing analysis that separates stable technical offsets from likely acoustic delay.

Verify later: known left, center, right, mirrored, and delayed fixtures; low-confidence no-change behavior; direct and room routing exclusions; listening check for mic-aware pan.

Runtime timing correction, audio gain baking, offset baking, and channel swapping are deferred to a later plan.

## Dependencies, Likely Areas, And Non-Goals

Update `INSTRUMENT_MAP_FORMAT.md` when final map fields are chosen. Relevant existing plans are:

- `TODO11_PAD_POPUP_LAYER_PARAMETERS_PLAN.md` for current layer controls;
- `PLAN-instrument-map-surgical-apply.md` for no-reload parameter updates and shadow layers;
- `TODO10_UI_REVIEW_REFACTOR_PLAN.md` for layer-address and editor-serialization risks;
- `TODO05_INSTRUMENT_MAP_PLAN.md` as history only, not current behavior.

Likely implementation areas:

- analyzer: `../scripts/analyze_drum_samples.py`;
- library and loading: `../source/Engine/KitLibModel.*`, `InstrumentSynthLoader.cpp`, and `SampleLoader.*`;
- runtime and maps: `../source/Engine/InstrumentSynth*`, `DrumKitEngine*`, and mapping helpers;
- UI: `../source/UI/PadContextPopup.*`, `InstrumentConfigPanel.*`, and `EngineSettingsPanel.*`;
- focused parser, map, synth, Python, and listening checks.

Non-goals:

- no output-bus redesign;
- no calibrated velocity in crosstalk filtering;
- no automatic layer-count gain reduction;
- no independent normalization of every mic;
- no automatic room panning or phase alignment;
- no whole-layer flip used to repair one mic;
- no audio-file modification;
- no new realtime parsing, allocation, file I/O, or unbounded work.

The rough task text in `../README.md` should be replaced with a link to this plan after the user accepts the design. Preserve unrelated README changes.

No build, tests, analyzer run, or listening check was performed while writing this plan.
