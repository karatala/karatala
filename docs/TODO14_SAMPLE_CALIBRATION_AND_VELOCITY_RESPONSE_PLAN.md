# TODO14: Sample Calibration And Velocity Response Plan

Status: Design plan only. No implementation or verification has started.

## Scope And Terminology

- `layer`: one source instrument articulation, with its own parameters, assigned to a destination pad articulation through the pad popup or Instrument tab.
- `sample velocity level`: one recorded sample group such as `043a` or `127a`. This is not a mapped layer.
- `logical hit`: one named velocity and round-robin variant, including every linked mic file or channel range that belongs to that hit.
- `logical-hit pre-trim intensity`: the strength of one logical hit in source-intensity dB, measured from the chosen reference mic before calibration gain or other source preprocessing.
- `raw velocity median`: the robust measured median of the logical-hit pre-trim intensities at one named velocity.
- `fitted source value`: the monotonic value fitted from raw velocity medians and used only for source selection.
- `global loudness response`: the user-controlled target loudness for MIDI velocities 1 to 127.
- `source response profile`: the stable monotonic relation between named velocities and their fitted source values for one articulation.
- `sample calibration trim`: fixed gain derived from one raw velocity median, repeated in every filename at that named velocity, and applied before cache publication.
- `source preprocessing recipe`: every fixed audio transform resolved before a sample buffer enters the cache.
- `layer calibration override`: a manual correction saved for one map layer.
- `source balance`: detected recorded position using the existing folder codes `L1..L127`, `C0`, and `R1..R127`.
- `pan`: the existing manual layer placement offset.

Do not use one generic curve for both sample selection and output loudness. They are separate problems.

This plan owns analysis, filename calibration metadata, the global loudness response, exact logical-hit selection, the source preprocessing recipe contract, per-layer overrides, and report-only balance and timing work. [TODO15](TODO15_FLOATING_SAMPLE_SELECTION_PLAN.md) only adds optional floating selection for dense, low-round-robin libraries. [TODO16](TODO16_DERIVED_SAMPLE_CACHE_AND_BAKE_CACHE_PLAN.md) only adds persistent derived sample storage and the Bake Cache workflow after the RAM-cache recipe contract is correct.

Velocity and loudness calibration are the first release. Pan recommendations and timing reports follow after the core works. Runtime timing correction and source audio rewriting need a later plan after listening review.

## Goal And Acceptance Criteria

For every calibrated source articulation:

- The median response at MIDI velocity 127 reaches one agreed maximum reference level.
- Median responses at lower velocities follow one global, user-controlled loudness response.
- Incorrect source velocity names do not create large loudness jumps.
- Recorded soft and hard samples are still selected for their timbre.
- Selection returns one complete logical hit, not an unrelated velocity and round robin for each mic.
- All active mic entries for one note use the same named velocity and round-robin variant, or none of that hit starts.
- Every layer in a layered pad keeps its own automatic calibration and manual override.
- Existing `vol` remains a creative layer mix control, shown as `Mix Gain` in the UI.

The maximum reference is measured before `Mix Gain`, bleed, routing, DAW faders, and the sum of several layers. Those later changes can change final output level.

Success means:

- calibrated raw velocity medians stay within an agreed analysis and listening tolerance at velocity 127;
- useful lower raw velocity medians stay within an agreed tolerance of the global response;
- sample changes reduce level steps to the agreed tolerance;
- `--rename-files` writes resolved metadata into every affected sample filename and repeated runs are idempotent;
- conflicting repeated metadata is reported and never resolved by scan order;
- manual changes affect only the chosen primary or shadow layer;
- manual response changes rebuild lookup tables without reloading sample audio;
- library filename trim or preprocessing changes reload only affected library content;
- two different source preprocessing recipes never share one cached buffer;
- early loading covers calibrated target intensities instead of trusting raw filename velocity labels;
- analyzer report mode never changes files or folders.

Phase 1 must choose the actual metric, reference level, and tolerances. This document does not invent those numbers.

## Plan Decisions

These choices are part of this plan:

- Calibrate every source articulation to one common maximum first. Listening may later justify explicit instrument-family offsets.
- Calibrate every mapped layer independently. Do not reduce gain because a pad has several layers.
- Keep one machine-wide default loudness response, but copy it into plugin instance state so saved sessions remain stable.
- Measure every audio file independently and keep one complete result record per file.
- Group completed file records only in a second pass for logical-hit intensity, round-robin statistics, raw velocity medians, linked-mic comparisons, folder balance, timing, and confidence.
- Keep every logical hit's pre-trim intensity. Use the raw velocity median to fit a stable source profile, but never replace or discard the individual hit values or raw median.
- Write per-file measurements, repeated velocity-level trim, repeated logical-hit intensity, repeated raw velocity median, and required metric version data into every affected filename.
- Reconstruct core runtime calibration from filenames. A calibration sidecar is not required.
- Keep the analyzer report as an audit artifact, not a playback dependency.
- Keep existing `L/C/R` source balance in instrument or articulation folder names.
- Use overhead stereo as the source-balance reference.
- Apply fixed source preprocessing once, in a defined order, before an immutable buffer is published to the RAM cache.
- Require cache identity to use source identity plus the complete typed preprocessing recipe.
- Prioritize preload work by calibrated logical-hit coverage, not only raw named velocities and variant letters.
- Timing and possible room orientation analysis are report-only in this plan.
- Do not modify source audio files in this plan.

Phase 1 still needs measured answers for:

- the logical-hit loudness metric and analysis window;
- the loudness reference mic and fallback rule;
- maximum target, allowed positive trim, and peak headroom;
- acceptance tolerance;
- round-robin outlier rules;
- which linked mic entries must be ready before a logical hit is playable;
- representative target intensities for early loading;
- source-balance center dead zone and confidence threshold.

## Current Facts And Constraints

- `../source/Engine/KitLibModel.cpp` reads sample velocity from filenames.
- `../source/Engine/InstrumentSynth.h` builds a 128-value table that selects the first recorded velocity greater than or equal to raw MIDI velocity.
- Current note selection resolves a velocity layer first and chooses a round-robin variant inside that layer later.
- One logical hit is identified by velocity layer and variant index, but there is no explicit runtime object that owns all linked mic entries for that hit.
- Normal round robin prefers a fully loaded variant, but the current no-ready and RR-lock paths can return an incomplete variant and start only its loaded entries.
- Playback gain is currently raw MIDI velocity divided by 127.
- Preview, voice loudness estimates, voice removal, and hi-hat transitions also depend on this linear gain in several paths.
- One `InstrumentMapEntry` is one layer. Extra matching entries use shadow `InstrumentSynth` objects.
- Current layer params are `tuning`, `attack`, `decay`, `vol`, `pan`, `flipLR`, `flipPhase`, `bleed`, and `micRoute`.
- Sample filename metadata already supports whole-dB `v` gain trim plus `p`, `d`, and `c` peak and decay data.
- Gain trim is applied while loading audio, before the processed buffer enters the RAM cache.
- Effective peak and decay level estimates do not fully include that gain change.
- The current RAM-cache key uses path, source size, source modification time, and `preprocessTag`.
- The key omits trim, resolved read length, and resolved fade even though these values change cached audio.
- Progressive loading uses fixed raw velocity representatives near `10`, `35`, `65`, `95`, and `120`, and favors low variant indexes early.
- Instrument and articulation folders already parse `L1..L127`, `C0`, and `R1..R127`. Articulation balance wins over instrument balance.
- Current automatic balance mainly affects direct mono mics routed to stereo.
- Current layer pan can affect direct, overhead, and room paths together. This is not selective enough for the requested rule.
- Current whole-layer `flipLR` cannot repair only one reversed room mic.
- The pad popup and Instrument tab can rebuild map text through different paths. New fields must not be lost by an editor that does not display them.
- All runtime velocity and routing data must be precomputed. The audio thread must not parse text, allocate, lock, scan files, or log.

Current `../scripts/analyze_drum_samples.py` has these blocking issues:

- separate file measurement is the correct first step, but there is no second pass for logical hits, round-robin statistics, raw velocity medians, or linked-mic comparisons;
- it converts multichannel audio to a positive RMS mono signal before LUFS analysis, losing pan, phase, sign, and timing;
- full-file integrated LUFS is not a stable hit-loudness value for both short drums and long cymbals;
- its filename parser differs from the engine parser;
- rename mode can drop existing optional metadata and does not validate all collisions first;
- after rename, it writes the new path into the CSV `instr` column;
- decay analysis can encode padded silence as valid points;
- it does not calculate true peak, source balance, mic timing, or calibration profiles.

## Proposed Core Model

Keep these independent values:

| Value | Scope | Purpose |
| --- | --- | --- |
| Global loudness response | plugin instance | raw MIDI velocity to target relative dB |
| Logical-hit pre-trim intensity | logical hit, repeated in linked filenames | preserve each recorded hit's original strength |
| Raw velocity median | named velocity, repeated in filenames | calculate stable trim without flattening round robins |
| Fitted source profile | library articulation runtime | build monotonic source selection across named velocities |
| Sample calibration trim | named velocity, repeated in every related filename | move the raw velocity median to one common calibrated reference |
| Source preprocessing recipe | one load request and cached buffer | define every fixed transform applied before cache publication |
| Layer calibration override | instrument map layer | adjust automatic selection or residual gain manually |

`Mix Gain` is applied after them as a creative offset.

Use one explicit dB model:

```text
hitIntensityDb(articulation, hit)
    = measured logical-hit pre-trim intensity

rawMedianDb(articulation, velocityLevel)
    = median hitIntensityDb for all round robins at that named velocity

fittedSourceDb(articulation, velocityLevel)
    = monotonic fitted source value for that named velocity

commonReferenceDb
    = common calibrated source reference

calibrationTrimDb(articulation, velocityLevel)
    = safeTrim(commonReferenceDb - rawMedianDb)

globalTargetDb(midiVelocity)
    = global target output dB, with globalTargetDb(127) = 0 dB

selectionOffsetDb(layer, midiVelocity)
    = automatic or manual source-selection offset for one mapped layer

selectionTargetDb(layer, midiVelocity)
    = clamp(highestFittedSourceDb + globalTargetDb + selectionOffsetDb,
            lowestFittedSourceDb, highestFittedSourceDb)

residualOutputDb(layer, midiVelocity)
    = optional manual residual output correction for one mapped layer
```

`selectionTargetDb` uses the same source-intensity dB unit as `hitIntensityDb`, `rawMedianDb`, and `fittedSourceDb`. It is never raw MIDI velocity, post-trim peak, or output gain.

`lowestFittedSourceDb` and `highestFittedSourceDb` are the lowest and highest valid fitted source values for the articulation. They do not assume that filename velocity labels describe real loudness correctly.

Exact selection chooses the named velocity whose `fittedSourceDb` is closest to `selectionTargetDb`. TODO15 floating selection chooses a logical hit by comparing `hitIntensityDb` with the same `selectionTargetDb`.

Every filename at one named velocity, across all variants and linked mic files, receives the same `calibrationTrimDb`. The median rendered reference before creative controls is therefore:

```text
commonReferenceDb + globalTargetDb(midiVelocity) + residualOutputDb(layer, midiVelocity)
```

The selected round robin still differs by `hitIntensityDb - rawMedianDb`. Report that difference as an outlier when needed. Do not flatten it automatically.

`safeTrim` applies the Phase 1 positive-gain and headroom rules. If the required correction is unsafe, mark the calibration limited or invalid instead of hiding the error.

In simple terms, `calibrationTrimDb` removes the source recording-level difference, `selectionTargetDb` chooses the suitable recorded timbre for one mapped layer, and `globalTargetDb` sets the predictable note loudness.

### Playback flow

For every mapped layer:

1. Keep raw MIDI velocity for crosstalk and other MIDI input rules.
2. Read target relative dB from the global 128-value loudness table.
3. Read the precomputed `selectionTargetDb` value for this layer and MIDI velocity.
4. Let the active selection policy return one ready logical hit, including its named velocity and round-robin variant.
5. Preflight the complete hit's buffers and reserve all required voice slots. Start none of the hit when the full set cannot start.
6. Start the linked active mic entries owned by that logical hit.
7. Use immutable sample buffers that already received `calibrationTrimDb` through fixed source preprocessing.
8. Apply global target gain instead of the old `velocity / 127` gain.
9. Apply any manual residual correction, then `Mix Gain`, bleed, envelopes, choke, and routing gains.

Do not select a velocity layer and then make unrelated round-robin or mic choices. The selection result identifies one logical hit.

The source profile changes timbre selection. The global response controls output loudness.

Build outside the audio thread:

- one global 128-value gain table;
- one 128-value `selectionTargetDb` table for each loaded layer articulation;
- one `velocityCalibrationTrimDb` entry for each named velocity, reconstructed from the repeated filename values;
- one exact source-region lookup for each loaded layer articulation;
- immutable logical-hit descriptions with linked sample entries;
- a thread-safe readiness snapshot for every logical hit;
- bounded exact-variant eligibility data; TODO15 adds floating candidate windows;
- one 128-value manual correction table when needed.

Exact mode keeps the current named-velocity behavior, then chooses only a fully ready round robin and returns a logical hit. Do not fall back to a variant when only some of its active mic buffers are ready. TODO15 can replace the exact region with a small precomputed candidate window, while keeping the same logical-hit result contract and group voice reservation.

Interpolation of loudness and gain correction happens in dB. Table output used for rendering is linear gain.

### File measurement, logical hits, and source profiles

The analyzer first measures every file independently. It preserves signed real channels and produces one deterministic record with per-file peak, decay, balance, headroom, and trim evidence.

Only after file measurement, a second pass groups records by:

```text
kit + instrument + articulation + named velocity + round robin
```

The second pass finds the linked mic files and channel ranges for one logical hit.

- Derive one logical-hit pre-trim intensity from the agreed reference mic rule.
- Repeat that exact value in every linked filename for the logical hit.
- Keep the individual logical-hit intensity for every round robin.
- Calculate `rawMedianDb` from logical hits that share one articulation and named velocity.
- Repeat that raw median in every filename at that named velocity.
- Fit `fittedSourceDb` from the raw medians while keeping original named-velocity order as a strong hint.
- Calculate `calibrationTrimDb` from `rawMedianDb` and `commonReferenceDb`.
- Repeat the same `calibrationTrimDb` in every filename at that named velocity, across all variants and linked mic files.
- Use the same correction for direct, overhead, and room entries of one hit so their recorded balance is preserved.
- Report strong inversions and round-robin outliers instead of silently flattening or reordering them.
- Keep individual per-file measurements separate from the shared logical-hit intensity, raw velocity median, fitted source profile, and velocity-level trim.
- Report noise risk, clipping risk, and unsafe positive trim.

The group pass compares completed records. It does not mix channel data between files and does not replace any per-file result.

If an articulation has too little valid data, use a clearly marked instrument-level profile only when it is compatible. Otherwise mark the layer `Uncalibrated` and use original exact velocity selection.

### Metadata ownership

Core runtime ownership:

| Location | Data |
| --- | --- |
| Sample filename | per-file peak and decay, shared velocity-level trim, shared logical-hit intensity, shared raw velocity median, metric/reference/schema version, and approved fixed source transforms |
| Instrument or articulation folder | aggregate existing `L/C/R` source balance |
| Instrument map | manual override for one mapped layer |
| Analyzer report | raw file records, grouping evidence, confidence, warnings, timing and orientation evidence, proposed rename plan, and original paths |

The exact compact filename codes and precision are chosen with parser fixtures before any library rename. Keep the existing filename `v` purpose, but add enough decimal precision for calibration.

The loader reconstructs logical hits and raw velocity medians from filename metadata, then builds the monotonic fitted source profile. Repeated logical-hit intensity, raw velocity median, and velocity-level trim values must agree at their defined scopes. A conflict marks the affected calibration invalid instead of using whichever file was scanned first.

The format must distinguish measured, proposed, runtime-applied, and audio-baked values. This prevents double correction after a later audio-editing tool is added.

An optional generated audit or index file may exist later for faster inspection, but core playback must not require it.

### Source preprocessing and cache identity

Resolve one typed source preprocessing recipe before cache lookup.

Processing order:

1. Parse and validate filename metadata.
2. Resolve defaults into concrete typed values.
3. Build one canonical recipe with an algorithm version.
4. Build the cache key from source identity and the complete recipe.
5. Read or decode the source on a worker.
6. Apply the complete fixed recipe once.
7. Insert and publish the immutable processed buffer.

Cache identity is:

```text
source content identity + complete source preprocessing recipe identity
```

The recipe key includes every audio-changing value and its algorithm version. It must include quantized trim, resolved read length, resolved fade, and any later approved channel swap or timing offset. A free-form `preprocessTag` is not enough.

Keep the current full-file buffer model. One recipe produces one complete multichannel buffer, and mic channel ranges are selected later by `SampleEntry`. Routing and channel-range selection do not create separate cache entries. A later mic-specific transform inside one multichannel file must use a channel-scoped transform list in the full-file recipe and must preserve the published channel layout.

Preview, progressive loading, normal playback, and future cache baking must use the same recipe builder. Calibration trim must not be applied again after a processed buffer is returned.

Global loudness response, manual layer calibration, `Mix Gain`, tuning, envelopes, bleed, pan, routing, choke, aftertouch, and instance effects remain runtime controls. They do not belong in the source recipe.

A changed velocity-level trim or other audio-changing filename token creates a new RAM-cache identity and reloads only affected source content. A changed logical-hit intensity, raw velocity median, fitted-profile algorithm, or manual curve rebuilds selection tables but does not change buffer identity when source content and the audio recipe are unchanged. TODO16 consumes the same identity for persistent disk storage, Bake Cache UI, cleanup, and multi-instance safety.

### Global and layer controls

- Store the global response as a small set of monotonic MIDI-to-dB points with velocity 127 fixed at `0 dB` relative.
- Build the full table outside playback.
- Add a simple global editor with reset and machine-default actions.
- Add `Library` and `Manual` calibration modes per layer. These are separate from TODO15's sample-selection mode. `Uncalibrated` is a status, not a user mode.
- `Library` uses the analyzer-derived profile and zero manual offsets.
- `Manual` starts from library calibration and can adjust `selectionOffsetDb` for source selection plus `residualOutputDb` for a small output correction.
- Reset removes the map override and returns to library calibration.
- Keep `Mix Gain` separate.
- Manual changes use the parameter-only update path and do not reload samples.
- Manual changes do not change the fixed source preprocessing recipe.

Both layer editors must share map-entry serialization or preserve fields they do not show. Primary and shadow synths each own their effective tables.

### Consistent loudness estimates

The same selected logical hit and effective gain must be used by:

- normal notes and previews;
- voice culling and stealing;
- adaptive tail shortening;
- hi-hat transition loudness matching;
- implicit hi-hat pedal-event velocity.

Original pre-trim intensity remains available after buffer preprocessing. Effective peak and decay estimates include fixed trim exactly once.

Do not redesign the accepted hi-hat transition policy. Change only its loudness input and selection identity, then use focused checks and listening review.

### Source balance, room orientation, and timing

Pan analysis uses real signed stereo overhead channels, a fixed post-onset window, a center dead zone, and a confidence value.

- Write instrument balance only when its articulations agree.
- Otherwise write articulation balance.
- Preserve existing balance when confidence is too low.
- Treat folder `L/C/R` as the recorded source position, with `L127..L1`, `C0`, and `R1..R127` mapped to `-127..127`.
- For overhead stereo, preserve the recorded stereo image and do not apply the detected source position again. Apply only the manual layer-pan offset.
- For an eligible non-room mono mic routed to a stereo output or aux, use detected source position plus manual layer-pan offset, clamped to the pan range.
- For a direct mono mic routed to its own mono output, apply no plugin pan and leave placement to the DAW.
- For room mics, apply neither detected source position nor the normal layer-pan offset in the first version.
- For other stereo mic types, preserve the recorded image. Add a manual offset only when their routing mode explicitly allows it.
- Precompute one mic placement mode and effective offset per routed entry outside playback.

Possible room L/R reversal is per mic, not per layer. Report it only when room direction is strongly and repeatedly opposite to overhead. Do not use whole-layer `flipLR` to repair one mic.

Timing analysis compares linked hits across many samples. A stable repeated delay for one mic or interface is possible technical latency. A delay that changes by instrument or distance is likely acoustic. Report both, but do not apply timing correction in this plan.

Report-only timing and orientation stay in the analyzer report. If a reviewed value later becomes runtime input, write it into every affected filename and include every audio-changing transform in the source recipe key.

## Implementation Phases

### Phase 1: Choose metrics and acceptance values

- Build a small reference corpus with several kits, drums, cymbals, and short percussion.
- Compare fixed post-onset loudness candidates, short RMS, true peak, and listening judgments.
- Choose the reference mic rule, logical-hit intensity metric expressed in dB, `commonReferenceDb`, raw velocity-median rule, monotonic-fit rule, outlier rule, trim cap, headroom, and tolerance.
- Confirm the `selectionTargetDb` equation and clamping behavior on the reference corpus.
- Choose complete-hit readiness rules and representative calibrated preload targets.
- Choose the pan dead zone and confidence threshold.

Verify later: record the chosen values and listening result in this plan.

### Phase 2: Make analyzer output correct and safe

- Separate parsing, file measurement, grouping, reporting, and apply logic.
- Preserve signed channels and decode mic channel ranges.
- Match the engine filename parser.
- Measure every file first, then build logical hits from completed records.
- Calculate and retain each logical-hit pre-trim intensity.
- Calculate raw velocity medians without replacing individual hit values.
- Add onset, fixed-window loudness, true peak, channel balance, corrected decay, and timing evidence.
- Make report-only mode the default and generate a complete no-write rename plan.
- Preserve all metadata and validate all rename collisions before any write.
- Fix the CSV and decay problems listed above.

Verify later: mono, stereo, separate-file, and multichannel fixtures; no-write report mode; deterministic output; parser round-trip; linked-hit grouping; collision protection.

### Phase 3: Define filename metadata and generate calibration

- Finalize compact filename codes for logical-hit intensity, raw velocity median, decimal trim, metric/reference/schema version, and approved transforms.
- Add Python schema validation and C++ parsing.
- Write shared values into every affected filename and validate that repeated values agree.
- Reject mixed metric or common-reference versions inside one calibrated articulation.
- Reconstruct logical hits and raw velocity medians from filenames, then fit source profiles in C++.
- Fit monotonic source profiles and calculate safe shared velocity-level trim recommendations.
- Make `--rename-files` transactional at the planned rename-set level: prevalidate all paths, use temporary names where needed, roll back on failure, and remain idempotent.
- Add stale-analysis warnings to the analyzer report without making the report a runtime dependency.

Verify later: Python-to-engine round-trip; missing, invalid, and conflicting fallback; repeated apply without duplicate correction; reference-corpus predictions within Phase 1 tolerance.

### Phase 4: Add calibrated exact runtime and complete RAM-cache identity

- Add explicit immutable logical-hit runtime descriptions and separate thread-safe readiness snapshots.
- Persist global curve points in instance state and machine defaults.
- Build global gain, `selectionTargetDb`, `velocityCalibrationTrimDb`, exact source-region, and per-layer correction tables outside playback.
- Make selection return one logical hit and start only its linked active mic entries.
- Remove any fallback that chooses a variant when only part of its active mic set is ready.
- Let loaded fallback search only complete logical hits.
- Preflight buffers and reserve all required voice slots as one fixed-size operation before starting a hit.
- Start all required voices or start none. Never replace one missing mic entry with data from another logical hit.
- Replace raw linear velocity gain without multiplying both systems.
- Resolve fixed source preprocessing before RAM-cache publication.
- Define the source content identity used by the canonical RAM-cache key.
- Replace the incomplete cache tag contract with one canonical typed recipe and complete key.
- Use the same recipe builder for preview, progressive loading, and normal playback.
- Derive early-load priority from representative calibrated target intensities and complete logical hits.
- Update preview, voice management, and hi-hat loudness estimates.

For each representative target, load one close logical hit and all its active linked mic entries in the same stage. Do not prefer variants `a` and `b` only because their indexes are small. Load every remaining mapped hit later.

Verify later: deterministic monotonic tables; the `commonReferenceDb + globalTargetDb + residualOutputDb` median equation within tolerance; velocity 127 at `0 dB` relative; group voice reservation; no partial or mixed mic variants; different recipes never share buffers; equivalent recipes do share buffers; calibrated first-playable coverage; every mapped sample eventually loads; hi-hat listening comparison.

### Phase 5: Add manual per-layer controls

- Add map fields for calibration mode, manual source response, and residual correction.
- Extend parser, serializer, fingerprints, and parameter-only updates.
- Add detailed pad-popup controls and compact Instrument-tab status.
- Fix shared serialization before either editor can save the new fields.
- Address layers by destination articulation, source, and layer occurrence.

Verify later: independent primary and shadow edits; two articulations in one slot; no sample reload; save and reload; reset to library calibration.

### Phase 6: Add balance and timing reports

- Add high-confidence overhead balance recommendations and reviewed folder updates.
- Precompute mic-aware placement modes for overhead and approved mono-to-stereo routes.
- Add per-mic room-orientation recommendations to the report.
- Add report-only mic timing analysis that separates stable technical offsets from likely acoustic delay.
- Define reviewed future filename tokens before any orientation or timing transform becomes runtime input.

Verify later: known left, center, right, mirrored, and delayed fixtures; low-confidence no-change behavior; direct and room routing exclusions; listening check for mic-aware pan.

Runtime timing correction, source audio gain baking, offset baking, and channel swapping are deferred.

## Dependencies, Likely Areas, And Non-Goals

Update `INSTRUMENT_MAP_FORMAT.md` when final map fields are chosen. Relevant plans are:

- [TODO15 floating sample selection](TODO15_FLOATING_SAMPLE_SELECTION_PLAN.md) for dense velocity layouts with few round robins;
- [TODO16 derived sample cache and Bake Cache](TODO16_DERIVED_SAMPLE_CACHE_AND_BAKE_CACHE_PLAN.md) for persistent processed buffers and cache controls;
- [TODO11 pad popup layer parameters](TODO11_PAD_POPUP_LAYER_PARAMETERS_PLAN.md) for current layer controls;
- [Instrument map surgical apply](PLAN-instrument-map-surgical-apply.md) for no-reload parameter updates and shadow layers;
- [TODO10 UI review](TODO10_UI_REVIEW_REFACTOR_PLAN.md) for layer-address and editor-serialization risks;
- `TODO05_INSTRUMENT_MAP_PLAN.md` as history only, not current behavior.

Likely implementation areas:

- analyzer: `../scripts/analyze_drum_samples.py`;
- library and loading: `../source/Engine/KitLibModel.*`, `InstrumentSynthLoader.cpp`, and `SampleLoader.*`;
- runtime and maps: `../source/Engine/InstrumentSynth*`, `DrumKitEngine*`, and mapping helpers;
- UI: `../source/UI/PadContextPopup.*`, `InstrumentConfigPanel.*`, and `EngineSettingsPanel.*`;
- focused parser, map, synth, Python, and listening checks.

Non-goals:

- no mandatory calibration sidecar;
- no loss of individual logical-hit intensity after median calculation;
- no independent mic or round-robin selection after a logical hit is chosen;
- no full floating-selection implementation, because TODO15 owns it;
- no persistent derived disk cache or Bake Cache UI, because TODO16 owns it;
- no layer merging;
- no output-bus redesign;
- no calibrated velocity in crosstalk filtering;
- no automatic layer-count gain reduction;
- no automatic room panning or phase alignment;
- no whole-layer flip used to repair one mic;
- no source audio-file modification;
- no new realtime parsing, allocation, file I/O, locking, or unbounded work.

No build, tests, analyzer run, or listening check was performed while writing this plan.
