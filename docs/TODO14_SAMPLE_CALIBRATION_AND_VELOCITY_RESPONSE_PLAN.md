# TODO14: Sample Calibration And Velocity Response Plan

Status: Phase 1 is complete. Phase 2, Phase 3, Phase 5, Phase 5B, and Phase 6 implementation is complete. Phase 4 core implementation and its first listening correction are complete, but clear load-complete status and the remaining fixture, library, C++, UI, and listening verification are pending.

## Scope And Terminology

- `layer`: one source instrument articulation, with its own parameters, assigned to a destination pad articulation through the pad popup or Instrument tab.
- `sample velocity level`: one recorded sample group such as `043a` or `127a`. This is not a mapped layer.
- `logical hit`: one named velocity and round-robin variant, including every linked mic file or channel range that belongs to that hit.
- `logical-hit pre-trim intensity`: the strength of one logical hit in source-intensity dB, measured from the chosen reference mic before calibration gain or other source preprocessing.
- `raw velocity median`: the robust measured median of the logical-hit pre-trim intensities at one named velocity.
- `fitted source value`: the monotonic value fitted from raw velocity medians and used for analysis, preload priority, and future floating selection. Exact mode still follows named velocity boundaries.
- `global loudness response`: the user-controlled target loudness for MIDI velocities 1 to 127.
- `source response profile`: the stable monotonic relation between named velocities and their fitted source values for one articulation.
- `sample calibration trim`: fixed gain derived from one raw velocity median and applied before cache publication.
- `source preprocessing recipe`: every fixed audio transform resolved before a sample buffer enters the cache.
- `layer velocity response`: an optional MIDI-velocity-to-output-gain override saved for one map layer; otherwise the layer uses the global response.
- `source balance`: detected recorded position using the existing folder codes `L1..L127`, `C0`, and `R1..R127`.
- `pan`: the existing manual layer placement offset.

Do not use one generic curve for both sample selection and output loudness. They are separate problems.

This plan owns analysis, per-kit `metadata.csv`, the global loudness response, exact logical-hit selection, the source preprocessing recipe contract, per-layer overrides, and report-only balance and timing work. [TODO15](TODO15_FLOATING_SAMPLE_SELECTION_PLAN.md) only adds optional floating selection for dense, low-round-robin libraries. [TODO16](TODO16_DERIVED_SAMPLE_CACHE_AND_BAKE_CACHE_PLAN.md) only adds persistent derived sample storage and the Bake Cache workflow after the RAM-cache recipe contract is correct.

Velocity and loudness calibration are the first release. Pan recommendations and timing reports follow after the core works. Runtime timing correction and source audio rewriting need a later plan after listening review.

## Goal And Acceptance Criteria

For every calibrated source articulation:

- The median response at MIDI velocity 127 reaches that articulation's own hardest measured reference level.
- Median responses at lower velocities follow the global response or that layer's response override.
- Incorrect source velocity names do not create large loudness jumps.
- Recorded soft and hard samples are still selected for their timbre.
- Selection returns one complete logical hit, not an unrelated velocity and round robin for each mic.
- All active mic entries for one note use the same named velocity and round-robin variant, or none of that hit starts.
- Every layer in a layered pad keeps its own automatic calibration and manual override.
- Existing `vol` remains a creative layer mix control, shown as `Mix Gain` in the UI.

The articulation reference is measured before `Mix Gain`, bleed, routing, DAW faders, and the sum of several layers. Those later changes can change final output level.

Success means:

- calibrated raw velocity medians stay within an agreed analysis and listening tolerance at velocity 127;
- useful lower raw velocity medians stay within an agreed tolerance of the global response;
- sample changes reduce level steps to the agreed tolerance;
- the generator backs up the old per-kit `metadata.csv` and replaces it only after the complete new file is ready;
- the generator never renames or modifies sample audio files;
- manual changes affect only the chosen primary or shadow layer;
- manual response changes rebuild lookup tables without reloading sample audio;
- `Reload All Kits` rereads per-kit metadata and rebuilds the affected runtime data;
- two different source preprocessing recipes never share one cached buffer;
- early loading covers calibrated target intensities instead of trusting raw filename velocity labels;
- kit parsing or measurement failure leaves the existing `metadata.csv` and its backup unchanged.

Phase 1 approved the initial metric, common reference experiment, trim limits, headroom, and tolerances recorded below. The Phase 4 listening correction supersedes the common-reference and measured exact-selection decisions.

## Plan Decisions

These choices are part of this plan:

- Calibrate each source articulation to its own hardest measured level so natural kick, snare, cymbal, and articulation balance is preserved. Add type or type-plus-articulation targets only after a separate listening comparison.
- Calibrate every mapped layer independently. Do not reduce gain because a pad has several layers.
- Keep one machine-wide default loudness response, but copy it into plugin instance state so saved sessions remain stable.
- Keep the built-in response codes stable: `linear`, `logarithmic`, and `exponential`. For normalized velocity `x = midiVelocity / 127`, their gains are `x`, `ln(1 + (e - 1) * x)`, and `(e^x - 1) / (e - 1)` respectively. Convert gain to dB for the response table and keep velocity 127 at `0 dB`.
- Measure every audio file independently and keep one complete result record per file.
- Group completed file records only in a second pass for logical-hit intensity, round-robin statistics, raw velocity medians, linked-mic comparisons, folder balance, timing, and confidence.
- Keep every logical hit's pre-trim intensity. Use the raw velocity median to fit a stable source profile, but never replace or discard the individual hit values or raw median.
- Write measured sample and mic data into one generated `metadata.csv` in each kit root.
- Let C++ define the required columns and derive logical hits, raw velocity medians, fitted source profiles, and calibration trims when the kit loads.
- Keep sample filenames for identity plus optional explicit `v` manual gain. Do not add generated analysis suffixes, hashes, checksums, or version fields.
- Keep existing `L/C/R` source balance in instrument or articulation folder names.
- Use overhead stereo as the source-balance reference.
- Apply fixed source preprocessing once, in a defined order, before an immutable buffer is published to the RAM cache.
- Require cache identity to use source identity plus the complete typed preprocessing recipe.
- Prioritize preload work by calibrated logical-hit coverage, not only raw named velocities and variant letters.
- Timing and possible room orientation analysis are report-only in this plan.
- Do not modify source audio files in this plan.

The Phase 1 reference pass below provides approved answers for:

- the logical-hit loudness metric and analysis window;
- the loudness reference mic and fallback rule;
- maximum target, allowed positive trim, and peak headroom;
- acceptance tolerance;
- round-robin outlier rules;
- which linked mic entries must be ready before a logical hit is playable;
- representative target intensities for early loading;
- source-balance center dead zone and confidence threshold.

## Baseline Facts And Constraints

- `../source/Engine/KitLibModel.cpp` reads sample velocity from filenames.
- `../source/Engine/InstrumentSynth.h` builds a 128-value table that selects the first recorded velocity greater than or equal to raw MIDI velocity.
- Current note selection resolves a velocity layer first and chooses a round-robin variant inside that layer later.
- One logical hit is identified by velocity layer and variant index, but there is no explicit runtime object that owns all linked mic entries for that hit.
- Normal round robin prefers a fully loaded variant, but the current no-ready and RR-lock paths can return an incomplete variant and start only its loaded entries.
- Playback gain is currently raw MIDI velocity divided by 127.
- Preview, voice loudness estimates, voice removal, and hi-hat transitions also depend on this linear gain in several paths.
- One `InstrumentMapEntry` is one layer. Extra matching entries use shadow `InstrumentSynth` objects.
- Current layer params are `tuning`, `attack`, `decay`, `vol`, `pan`, `flipLR`, `flipPhase`, `bleed`, and `micRoute`.
- Sample filenames still contain explicit whole-dB `v` manual gain plus legacy `p`, `d`, and `c` peak and decay data. New generated analysis moves to `metadata.csv`.
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

Current [`generate_sample_metadata.py`](../scripts/generate_sample_metadata.py) is the developer metadata generator. It keeps the useful Phase 2 measurements, accepts one kit or sample-library root, and writes deterministic per-kit CSV files without renaming samples or producing a separate JSON database.

The older `analyze_drum_samples*.py` experiments are deprecated except for the Phase 1 listening tool.

## Proposed Core Model

Keep these independent values:

| Value | Scope | Purpose |
| --- | --- | --- |
| Global loudness response | plugin instance | raw MIDI velocity to target relative dB |
| Logical-hit pre-trim intensity | logical hit, derived by C++ from measured CSV rows | preserve each recorded hit's original strength |
| Raw velocity median | named velocity, derived by C++ from logical hits | calculate stable trim without flattening round robins |
| Fitted source profile | library articulation runtime | build monotonic source selection across named velocities |
| Sample calibration trim | named velocity, derived by C++ | move the raw velocity median to its articulation's hardest measured reference |
| Source preprocessing recipe | one load request and cached buffer | define every fixed transform applied before cache publication |
| Layer velocity response | instrument map layer | optionally replace the global output-gain response for one layer |

`Mix Gain` is applied after them as a creative offset.

Use one explicit dB model:

```text
hitIntensityDb(articulation, hit)
    = measured logical-hit pre-trim intensity

rawMedianDb(articulation, velocityLevel)
    = median hitIntensityDb for all round robins at that named velocity

fittedSourceDb(articulation, velocityLevel)
    = monotonic fitted source value for that named velocity

articulationReferenceDb
    = fitted source value of the articulation's highest named velocity

calibrationTrimDb(articulation, velocityLevel)
    = safeTrim(articulationReferenceDb - rawMedianDb)

globalTargetDb(midiVelocity)
    = global target output dB, with globalTargetDb(127) = 0 dB

effectiveTargetDb(layer, midiVelocity)
    = layer response override target dB when present, otherwise globalTargetDb

selectionTargetDb(layer, midiVelocity)
    = fittedSourceDb of the exact named velocity region
```

`selectionTargetDb` uses the same source-intensity dB unit as `hitIntensityDb`, `rawMedianDb`, and `fittedSourceDb`. It is never raw MIDI velocity, post-trim peak, or output gain.

Exact selection uses the original named velocity boundaries from sample filenames. It does not use measured dB to replace that timbre choice. TODO15 floating selection may later choose a logical hit by comparing `hitIntensityDb` with `selectionTargetDb`.

Every sample at one named velocity, across all variants and linked mic entries, receives the same derived `calibrationTrimDb`. The median rendered reference before creative controls is therefore:

```text
articulationReferenceDb + effectiveTargetDb(layer, midiVelocity)
```

The selected round robin still differs by `hitIntensityDb - rawMedianDb`. Report that difference as an outlier when needed. Do not flatten it automatically.

`safeTrim` applies the Phase 1 positive-gain and headroom rules. If the required correction is unsafe, mark the calibration limited or invalid instead of hiding the error.

In simple terms, `calibrationTrimDb` evens the recorded velocity levels inside one articulation, named velocity boundaries choose the recorded timbre in exact mode, and the effective global response or layer override controls how MIDI velocity changes note loudness.

### Playback flow

For every mapped layer:

1. Keep raw MIDI velocity for crosstalk and other MIDI input rules.
2. Read target relative dB from the layer's 128-value response override when present, otherwise from the global table.
3. Read the precomputed named velocity region for this layer and MIDI velocity.
4. Let the active selection policy return one ready logical hit, including its named velocity and round-robin variant.
5. Preflight the complete hit's buffers and reserve all required voice slots. Start none of the hit when the full set cannot start.
6. Start the linked active mic entries owned by that logical hit.
7. Use immutable sample buffers that already received `calibrationTrimDb` through fixed source preprocessing.
8. Apply effective target gain instead of the old fixed `velocity / 127` gain.
9. Apply `Mix Gain`, bleed, envelopes, choke, and routing gains.

Do not select a velocity layer and then make unrelated round-robin or mic choices. The selection result identifies one logical hit.

Named velocity boundaries control exact timbre selection. The effective global response or layer override controls output loudness.

Build outside the audio thread:

- one global 128-value gain table;
- one 128-value `selectionTargetDb` table for each loaded layer articulation;
- one `velocityCalibrationTrimDb` entry for each named velocity, derived from measured CSV data;
- one exact source-region lookup for each loaded layer articulation;
- immutable logical-hit descriptions with linked sample entries;
- a thread-safe readiness snapshot for every logical hit;
- bounded exact-variant eligibility data; TODO15 adds floating candidate windows;
- one optional 128-value velocity-response override for each mapped layer.

Exact mode keeps the current named-velocity behavior, then chooses only a fully ready round robin and returns a logical hit. Do not fall back to a variant when only some of its active mic buffers are ready. TODO15 can replace the exact region with a small precomputed candidate window, while keeping the same logical-hit result contract and group voice reservation.

Interpolation of loudness and gain correction happens in dB. Table output used for rendering is linear gain.

### File measurement, logical hits, and source profiles

The generator first measures every file independently. It preserves signed real channels and produces deterministic per-mic rows with the fields required by C++.

Only after file measurement, C++ groups loaded CSV rows by:

```text
kit + instrument + articulation + named velocity + round robin
```

The C++ load pass finds the linked mic files and channel ranges for one logical hit.

- Derive one logical-hit pre-trim intensity from the agreed reference mic rule.
- Keep the individual logical-hit intensity for every round robin.
- Calculate `rawMedianDb` from logical hits that share one articulation and named velocity.
- Fit `fittedSourceDb` from the raw medians while keeping original named-velocity order as a strong hint.
- Calculate `calibrationTrimDb` from `rawMedianDb` and the articulation's hardest fitted level.
- Use the same correction for direct, overhead, and room entries of one hit so their recorded balance is preserved.
- Report strong inversions and round-robin outliers instead of silently flattening or reordering them.
- Keep individual per-file measurements separate from the shared logical-hit intensity, raw velocity median, fitted source profile, and velocity-level trim.
- Report noise risk, clipping risk, and unsafe positive trim.

The C++ group pass compares completed rows. It does not mix channel data between files and does not replace any per-file result.

If an articulation has too little valid data, use a clearly marked instrument-level profile only when it is compatible. Otherwise mark the layer `Uncalibrated` and use original exact velocity selection.

### Metadata ownership

Core runtime ownership:

| Location | Data |
| --- | --- |
| Sample filename | sample identity plus optional explicit `v` manual gain |
| Per-kit `metadata.csv` | generated per-file and per-mic measurements required by C++ |
| Instrument or articulation folder | aggregate existing `L/C/R` source balance |
| Instrument map | manual override for one mapped layer |
| C++ runtime | logical hits, raw velocity medians, fitted source profiles, calibration trims, decay envelopes, and pan decisions |

Rows are identified by kit-relative sample path plus mic code. The initial CSV contains the measurements required by the planned runtime phases: `content_hash`, `file_size_bytes`, `onset_ms`, `intensity_db`, `true_peak_db`, `peak_db`, `peak_ms`, `decay_end_ms`, `stereo_balance`, and readable decay columns named `-6db_ms`, `-12db_ms`, `-18db_ms`, `-24db_ms`, `-30db_ms`, `-36db_ms`, `-42db_ms`, `-48db_ms`, `-54db_ms`, and `-60db_ms`. A value may be empty when it does not apply, such as stereo balance for a mono mic.

`onset_ms` also provides the timing evidence needed to compare linked mic entries, so a separate timing column is not required initially. Phase 3 C++ derives relative mic timing, logical-hit intensity, velocity medians, calibration trim, and decay envelopes from the measured CSV rows. Phase 6 will use `stereo_balance` for pan decisions.

The readable peak and decay fields replace the runtime use of legacy `p`, `d`, and compressed base62 `c` filename values. Existing `a`, `p`, `d`, and `c` tokens may be ignored when CSV metadata is active. The `v` token remains supported as explicit per-file `manualGainDb` for correcting recording or mic-balance problems. Automatic decimal `calibrationTrimDb` is derived separately by C++. Manual gain is included when calculating effective intensity and true peak, and the final preprocessing gain combines `manualGainDb` with `calibrationTrimDb`.

`content_hash` is a BLAKE2b-128 hash of the complete raw sample file. It detects modified files and can match files that were renamed or moved. `file_size_bytes` provides a quick additional check. Hashes stay in `metadata.csv` and are never added to sample filenames.

Exact measurement methods can still be tuned. Regenerating `metadata.csv` and using `Reload All Kits` applies the new measurements without renaming samples. Missing or invalid metadata disables only the affected calibrated behavior and falls back to current uncalibrated loading. No separate JSON runtime database, schema-version system, file watching, or realtime metadata parsing is required.

### Source preprocessing and cache identity

Resolve one typed source preprocessing recipe before cache lookup.

Processing order:

1. Parse sample identity from the filename and read available per-kit CSV metadata.
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

Global and per-layer velocity responses, `Mix Gain`, tuning, envelopes, bleed, pan, routing, choke, aftertouch, and instance effects remain runtime controls. They do not belong in the source recipe.

A changed derived velocity-level trim or other audio-changing metadata creates a new RAM-cache identity after `Reload All Kits`. A changed logical-hit intensity, raw velocity median, fitted-profile algorithm, or manual curve rebuilds selection tables but does not change buffer identity when source content and the audio recipe are unchanged. TODO16 consumes the same identity for persistent disk storage, Bake Cache UI, cleanup, and multi-instance safety.

### Global and layer controls

- Store the global response as a small set of monotonic MIDI-to-dB points with velocity 127 fixed at `0 dB` relative.
- Build the full table outside playback.
- Add a simple global editor with reset and machine-default actions.
- A layer uses `Use Global` response mode when no map override is present.
- A layer uses its own override when its map entry contains `velocityResponse=linear`, `velocityResponse=logarithmic`, `velocityResponse=exponential`, or a valid point list such as `1:-48,64:-12,127:0`.
- A layer override changes output gain only. Exact named-velocity sample selection remains unchanged.
- Reset to `Use Global` removes the map override.
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

Report-only timing and orientation stay outside playback. If a reviewed value later becomes runtime input, add a C++-owned CSV field and include every audio-changing transform in the source recipe key.

## Implementation Phases

### ✅ Phase 1: Choose metrics and acceptance values

- ✅ Build a small reference corpus with several kits, drums, cymbals, and short percussion.
- ✅ Compare fixed post-onset loudness candidates, short RMS, and true peak by measurement and listening.
- ✅ Choose the reference mic rule, logical-hit intensity metric expressed in dB, initial `commonReferenceDb` experiment, raw velocity-median rule, monotonic-fit rule, outlier rule, trim cap, headroom, and tolerance.
- ✅ Confirm the `selectionTargetDb` equation and clamping behavior on the reference corpus.
- ✅ Choose complete-hit readiness rules and representative calibrated preload targets.
- ✅ Choose the pan dead zone and confidence threshold.

#### Phase 1 reference pass

[`analyze_drum_samples_phase1.py`](../scripts/analyze_drum_samples_phase1.py) is a read-only comparison tool with explicit measurement constants and output paths. It does not rename or rewrite source files.

The final `todo14-phase1-v4` pass measured 184 logical hits from 30 articulations in four kits. It used low, middle, and high named velocities with up to three round robins, covering kicks, snares, toms, closed and open hi-hats, crashes, rides, and cowbells. It measured every selected reference hit from signed stereo channels and checked true peak across every linked file in the selected hit.

| Candidate | Median round-robin MAD | Velocity inversions | Median low-to-high range |
| --- | ---: | ---: | ---: |
| K-weighted 50 ms | 0.255 dB | 4 | 20.26 dB |
| K-weighted 100 ms | 0.329 dB | 5 | 19.78 dB |
| K-weighted 200 ms | 0.281 dB | 8 | 19.59 dB |
| K-weighted 400 ms | 0.315 dB | 8 | 19.54 dB |
| RMS 100 ms | 0.270 dB | 6 | 18.12 dB |
| Peak 10 ms RMS | 0.338 dB | 7 | 20.74 dB |

Measured findings:

- K-weighted 50 ms has the best numeric velocity result, but the first listening pass rejected all windows up to 200 ms for cross-instrument loudness. Rides remained too loud, toms remained too quiet, and the 1000 ms clips with 250 ms gaps were too slow.
- The second listening pass compared K-weighted 100, 200, and 400 ms with 500 ms clips and 50 ms gaps. Listening preferred 400 ms. Toms no longer needed a reported correction, while rides remained slightly loud and cowbells remained a little loud.
- The third listening pass compared light, medium, and strong ride and cowbell offsets. The approved first version keeps the 400 ms metric without any ride, cowbell, or other instrument-family offset.
- Linked-file true peak was `-9.25 dBTP` at the median, `+0.02 dBTP` at the 90th percentile, and `+1.63 dBTP` at the maximum before any new calibration trim.
- The selected corpus contains 198 linked files with existing legacy `v` trim tokens. Phase 1 ignored those tokens and measured the unmodified audio.
- The first K-weighted 50 ms experiment showed that requiring every post-trim cache buffer to stay below `-1 dBTP` would force `commonReferenceDb` down to `-46.79 dB`. This is too quiet for the intended maximum. The approved rule therefore checks peak safety after the matching global target gain, while allowing immutable floating-point cache buffers to exceed `1.0` internally.
- With the approved `commonReferenceDb = -38.0 dB`, required trim is `-2.59 dB` at the median, `+27.07 dB` at the 90th percentile, and `+41.31 dB` at the maximum across 79 named-velocity groups.
- Ten groups cross the `+24 dB` noise-risk warning. The `+36 dB` cap limits two very quiet Crocell groups: kick velocity 7 is `3.16 dB` short and ride velocity 16 is `5.31 dB` short. They stay explicitly marked `Limited`.
- After calibration trim and the velocity-127 global target, the maximum linked-file true peak among highest named velocities is `-2.20 dBTP`. All reference articulations pass the `1.0 dB` headroom rule.

Approved Phase 1 constants:

- Metric: `todo14-k-weighted-400ms-v1`.
- Onset: non-overlapping 5 ms signed-channel energy frames; first frame at or above the greater of `peak - 30 dB` and `-60 dBFS`.
- Logical-hit intensity: ungated BS.1770 K-weighted energy over 400 ms from detected onset.
- Reference mic order: normal stereo overhead `ons`, another normal overhead stereo, another overhead stereo, decoded overhead mid-side, otherwise `Uncalibrated`.
- Mid-side decoding: energy-preserving `L = (M + S) / sqrt(2)` and `R = (M - S) / sqrt(2)`.
- Raw velocity median: median of all valid logical-hit intensities at the named velocity.
- Monotonic fit: weighted pool-adjacent-violators isotonic fit in original named-velocity order, weighted by valid logical-hit count.
- Instrument-family offsets: none in the first version.
- Common reference experiment: `-38.0 dB`; superseded for runtime calibration by the Phase 4 listening correction.
- Positive-trim noise-risk warning: above `+24.0 dB`.
- Positive-trim cap: `+36.0 dB`; mark the velocity group `Limited` when more gain is required.
- Peak safety: require at least `1.0 dB` headroom after automatic calibration trim and the matching response gain, before `Mix Gain`.
- Default global response: preserve the current linear MIDI gain as `20 * log10(midiVelocity / 127)`, with velocity 127 at `0 dB`.
- Numeric acceptance: recomputed raw medians within `0.25 dB`; median sample-boundary steps within `1.5 dB` after calibration.
- Round-robin outlier: report any hit more than `3.0 dB` from its raw velocity median; do not flatten it.
- Complete-hit readiness: every active routed mic buffer and every required voice slot must be ready before any voice in the hit starts. Inactive or unrouted mic entries do not block it.
- Early-load target intensities relative to the highest fitted value: `-24`, `-12`, `-6`, `-3`, and `0 dB`.
- Source-balance window: signed stereo energy over 200 ms from onset.
- Pan confidence: center dead zone `-8..8`; require at least six hits over at least two named velocities, at least 80% side agreement, median absolute position above 12, and position MAD no greater than 12.

The approved baseline listening sequence is in `build/todo14_phase1_listening_v4/`. It uses K-weighted 400 ms normalization without instrument-family offsets.

These are the current working values, not a permanent file-format contract. The measurement window and other analysis constants can be tuned later, then `metadata.csv` can be regenerated without renaming samples.

Verification result: weighted isotonic fitting plus the clamped `selectionTargetDb` equation produced no decreasing targets and no backward named-velocity choices across all 30 reference articulations and MIDI velocities 1 to 127.

### Phase 2: Make analyzer measurements correct (implementation complete, verification pending)

- ✅ Separate parsing, file measurement, grouping, and reporting.
- ✅ Preserve signed channels and decode mic channel ranges.
- ✅ Match the engine filename parser.
- ✅ Measure every file first, then build logical hits from completed records.
- ✅ Calculate and retain each logical-hit pre-trim intensity.
- ✅ Calculate raw velocity medians without replacing individual hit values.
- ✅ Add onset, fixed-window loudness, true peak, channel balance, corrected decay, and timing evidence.
- ✅ Correct decay measurement and CSV path output in the prototype.

Implementation result: [`generate_sample_metadata.py`](../scripts/generate_sample_metadata.py) contains the Phase 2 measurements used by the Phase 3 generator. Measurement settings are command-line options so later listening experiments do not require code edits.

Verify later: mono, stereo, mid-side, separate-file, and multichannel fixtures; deterministic measurements; linked-hit grouping; no sample-file changes.

### ✅ Phase 3: Generate and load per-kit `metadata.csv` (implementation complete, verification pending)

- ✅ Define the required CSV columns and their runtime meaning in C++.
- ✅ Rename the prototype to `generate_sample_metadata.py` and remove JSON, report, and sample-rename behavior.
- ✅ Accept one kit root or one sample-library root and write deterministic per-mic rows sorted by relative path and mic code.
- ✅ Measure onset, K-weighted intensity, true peak, RMS peak and decay, stereo balance, file size, and BLAKE2b-128 content hash.
- ✅ Store readable decay columns from `-6db_ms` through `-60db_ms` instead of compressed base62 codes.
- ✅ Back up the current `metadata.csv` to `metadata.csv.bak` and replace the CSV only after the complete kit measurement succeeds.
- ✅ Load and match rows outside the audio thread by kit-relative path plus mic code.
- ✅ Keep filename `v` as `manualGainDb`; include it in effective intensity and peak calculations.
- ✅ Ignore legacy filename `p`, `d`, and `c` values when a matching CSV row is active.
- ✅ Reconstruct logical hits and velocity medians, fit monotonic source profiles, and calculate safe shared velocity-level calibration trims in C++.
- ✅ Use the existing `Reload All Kits` path to reread metadata and rebuild loaded kit state.
- ✅ Keep current uncalibrated behavior when a CSV, row, or required feature field is missing or invalid.
- ✅ Do not rename samples or add generated analysis data to sample filenames.

Verify later: deterministic generation; Python-to-C++ row matching; backup and replacement behavior; failed-kit protection; missing and invalid fallback; repeated generation; modified-file and rename detection; reference-corpus predictions within Phase 1 tolerance.

### Phase 4: Add calibrated exact runtime and complete RAM-cache identity (core implementation and listening correction complete, loading status and verification pending)

- ✅ Add explicit immutable logical-hit runtime descriptions and separate thread-safe readiness snapshots.
- ✅ Persist global curve points in instance state and machine defaults.
- ✅ Build global gain, `selectionTargetDb`, `velocityCalibrationTrimDb`, exact source-region, and per-layer correction tables outside playback.
- ✅ Make selection return one logical hit and start only its linked active mic entries.
- ✅ Remove any fallback that chooses a variant when only part of its active mic set is ready.
- ✅ Let loaded fallback search only complete logical hits.
- ✅ Preflight buffers and reserve all required voice slots as one fixed-size operation before starting a hit.
- ✅ Start all required voices or start none. Never replace one missing mic entry with data from another logical hit.
- ✅ Replace raw linear velocity gain without multiplying both systems.
- ✅ Resolve fixed source preprocessing before RAM-cache publication.
- ✅ Define the source content identity used by the canonical RAM-cache key.
- ✅ Replace the incomplete cache tag contract with one canonical typed recipe and complete key.
- ✅ Use the same recipe builder for preview, progressive loading, and normal playback.
- ✅ Derive early-load priority from representative calibrated target intensities and complete logical hits.
- ✅ Update preview, voice management, and hi-hat loudness estimates.
- ✅ Restore named velocity boundaries for normal playback, loaded fallback, and preview after measured-dB exact selection chose wrong timbres.
- ✅ Replace the common cross-instrument reference with each articulation's own hardest fitted level after listening found the snare too soft and quiet and the kick too loud.
- ✅ Apply fixed calibration gain and the final fade in one loader pass, and increment the preprocessing recipe version.
- Add clear debug status when all queued sample loads have finished. Report enough counts to distinguish loaded, failed, and still-pending work so listening can start from a known fully loaded state.

For each representative target, load one close logical hit and all its active linked mic entries in the same stage. Do not prefer variants `a` and `b` only because their indexes are small. Load every remaining mapped hit later.

Verify later: deterministic monotonic tables; the `articulationReferenceDb + effectiveTargetDb` median equation within tolerance for non-limited layers; velocity 127 at `0 dB` relative; named velocity boundary selection; group voice reservation; no partial or mixed mic variants; different recipes never share buffers; equivalent recipes do share buffers; calibrated first-playable coverage; every mapped sample eventually loads; clear all-loaded status and counts; hi-hat listening comparison.

### ✅ Phase 5: Add per-layer velocity response overrides (implementation complete, verification pending)

- ✅ Add optional per-layer `velocityResponse`; absence means `Use Global`, while a valid definition means a layer override.
- ✅ Extend parser, serializer, fingerprints, and parameter-only runtime updates.
- ✅ Keep exact named-velocity sample selection unchanged.
- ✅ Add per-layer response editing in the pad popup and compact named status in the Instrument tab.
- ✅ Make the Instrument tab use shared map serialization and preserve bleed, mic routes, and velocity response values it does not edit.
- ✅ Address live changes by destination articulation, source, and layer occurrence.

Verify later: independent primary and shadow edits; two articulations in one slot; no sample reload for live popup changes; raw-map load; save and reload; invalid curve feedback; reset to `Use Global`; editor layout and focus behavior.

### ✅ Phase 5B: Replace the temporary text field with a visual velocity-response editor (implementation complete, verification pending)

Keep the Phase 5 engine behavior and `velocityResponse` map format. Replace only the temporary text input in the pad popup.

Use one custom JUCE `Component` that paints the complete chart and handles its own mouse interaction. Do not create a separate JUCE `Slider` child for every velocity level. One painted component makes it simpler to show the curve, recorded sample levels, measured dB markers, and selection state in the same area.

#### User flow and chart

- ✅ Use one compact dropdown below Tuning, Attack, and Decay. It contains `Use Global`, built-in and user presets, and `Edit curve...`. An unmatched inline definition is displayed as `Unnamed layer curve*` but is not a selectable preset.
- ✅ Open the editor as a child overlay inside the plugin window, not as a native desktop window.
- ✅ Show the current destination articulation and source at the top so the user knows which layer is being edited.
- ✅ Use MIDI velocity `1..127` on the horizontal axis and relative output dB on the vertical axis.
- ✅ Draw a labelled column for every recorded sample velocity available in this source articulation. Lightly show the MIDI range that selects each sample level in exact mode.
- ✅ Draw the effective 128-value response as the main line.
- ✅ At each recorded level, show a fixed marker for the fitted sample level relative to the articulation's hardest fitted level.
- ✅ Show a short selected-level readout such as `Velocity 64 | Curve -12.0 dB | Sample -18.3 dB | Trim +6.3 dB`. Keep raw median and calibration status in the tooltip instead of adding more permanent chart lines.
- ✅ When calibration metadata is missing, still show and edit the response, but label sample dB and trim as unavailable.

#### Editing and validation

- ✅ In `Use Global` mode the chart is read-only. `Create Layer Curve` copies the current global curve into this layer.
- ✅ For a layer curve, create vertical drag handles at velocity `1`, every recorded sample level, velocity `127`, and any extra points present in the loaded curve or preset.
- ✅ Keep handle velocities fixed. The user changes only dB, so editing the response cannot change recorded sample selection.
- ✅ Keep velocity `127` fixed at `0 dB`.
- ✅ Clamp vertical dragging between the neighbouring handles and at or below `0 dB`, so the curve cannot become decreasing or invalid.
- ✅ Provide one numeric dB field for the selected handle for exact edits. Validate the final definition with the existing `VelocityResponse` parser and keep the last valid curve when text input is invalid.
- ✅ Update the existing 128-value runtime table while dragging, without reloading samples. Commit map text once on mouse-up or numeric edit completion.
- ✅ Provide a `Preview` action that plays the selected MIDI velocity through the existing layer preview path. Do not repeatedly trigger samples for every mouse-move event.
- ✅ Resetting the layer to `Use Global` removes the map override exactly as Phase 5 does now.

#### Curve presets

- ✅ Add a preset selector with built-in `Linear`, `Logarithmic`, and `Exponential` curves plus saved user curves.
- ✅ Add `Save As`. Confirm before replacing a user preset with the same name. Preset delete, rename, import, and export are not required in this phase.
- ✅ Store user curves in a separate `velocity_response_presets` directory, not in the existing kit-layout preset directory.
- ✅ Keep each preset file simple:

```yaml
name: Soft response
velocityResponse: "1:-48,64:-12,127:0"
```

- ✅ Do not add a preset schema or version system. Validate the stored definition with the same engine parser.
- ✅ A preset stores only the universal MIDI `1..127` response curve, not kit, instrument, articulation, or sample-level data, so it can be loaded for any layer.
- ✅ When a preset lacks a point at one of the current articulation's sample velocities, calculate that handle from the preset curve. Adding this handle must not change the loaded curve shape.
- ✅ Applying a preset copies its curve into the layer's map entry. The saved session must not depend on the preset file remaining on disk.
- ✅ File loading or validation failure leaves the current curve unchanged and shows a short error in the editor.

#### Engine and UI boundary

- ✅ Add a read-only snapshot containing each recorded velocity, raw median relative to the hardest fitted level, fitted level relative to the hardest fitted level, calibration trim, and calibration availability.
- ✅ Resolve measurement data from the selected source articulation. Keep Phase 5's destination articulation, source, and layer occurrence identity for applying curve edits. Do not expose mutable runtime objects to the UI.
- ✅ Build or refresh this snapshot outside playback. Preset file I/O also stays on the message thread.
- ✅ Keep exact named-velocity selection, calibration preprocessing, cache identity, and `Mix Gain` unchanged.
- ✅ Keep the component reusable for the later global-response editor, but implement only the pad-popup layer editor in this phase.

Verify later: sparse and dense articulations; articulation without metadata; correct recorded velocity columns and dB readouts; Use Global read-only mode; Create Layer Curve; built-in formulas; unnamed layer-curve status; vertical drag clamping; fixed velocity 127 at `0 dB`; numeric validation; selected-velocity preview; primary and shadow layers; live updates without sample reload; preset save, overwrite, load, invalid-file handling, and session independence; resizing, keyboard focus, tooltips, and visual review.

### ✅ Phase 6: Add balance and timing reports (implementation complete, verification pending)

- ✅ Add high-confidence overhead balance recommendations and reviewed folder updates.
- ✅ Precompute mic-aware placement modes for overhead and approved mono-to-stereo routes.
- ✅ Add per-mic room-orientation recommendations to the report.
- ✅ Add report-only mic timing analysis that separates stable technical offsets from likely acoustic delay.
- ✅ Define reviewed future CSV fields before any orientation or timing transform becomes runtime input.

Implementation result:

- `generate_sample_metadata.py --report-folder <folder>` writes `balance_recommendations.csv`, `room_orientation.csv`, and `mic_timing.csv` under one external subfolder per kit. It still writes only `metadata.csv` and its backup inside the kit.
- Balance reports use the Phase 1 confidence values. They include current and proposed folder names but never rename a folder.
- Room orientation compares each stereo room mic with the selected stereo overhead for matching logical hits. Only strong repeated opposite direction produces `recommended_flip_lr = 1`.
- Timing uses a direct mic as the logical-hit reference when available, then overhead, then the earliest remaining mic. Stable cross-articulation offsets are marked `possible_technical_offset`; changing overhead or room offsets are marked `likely_acoustic_variation`.
- Runtime mic placement is precomputed outside rendering. Stereo overhead uses only manual layer pan, eligible non-room mono-to-stereo uses source balance plus manual pan, direct mono-to-mono ignores plugin pan, room ignores layer pan, and other stereo routes preserve their image unless routing explicitly permits manual pan.
- Report proposal fields are `recommended_flip_lr` and `recommended_offset_ms`. Reserved future runtime CSV fields are `mic_flip_lr` and signed `playback_offset_ms`; they are not written to `metadata.csv` or read by the engine in this phase.

Verify later: known left, center, right, mirrored, and delayed fixtures; low-confidence no-change behavior; direct and room routing exclusions; listening check for mic-aware pan.

Automatic analysis-driven timing correction, offset baking, and channel swapping remain deferred. Explicit filename-authored `on<ms>` advance and `op<ms>` delay corrections are applied at runtime per mic and in preview without changing the shared sample cache.

## Dependencies, Likely Areas, And Non-Goals

Update `INSTRUMENT_MAP_FORMAT.md` when final map fields are chosen. Relevant plans are:

- [TODO15 floating sample selection](TODO15_FLOATING_SAMPLE_SELECTION_PLAN.md) for dense velocity layouts with few round robins;
- [TODO16 derived sample cache and Bake Cache](TODO16_DERIVED_SAMPLE_CACHE_AND_BAKE_CACHE_PLAN.md) for persistent processed buffers and cache controls;
- [TODO11 pad popup layer parameters](TODO11_PAD_POPUP_LAYER_PARAMETERS_PLAN.md) for current layer controls;
- [Instrument map surgical apply](PLAN-instrument-map-surgical-apply.md) for no-reload parameter updates and shadow layers;
- [TODO10 UI review](TODO10_UI_REVIEW_REFACTOR_PLAN.md) for layer-address and editor-serialization risks;
- `TODO05_INSTRUMENT_MAP_PLAN.md` as history only, not current behavior.

Likely implementation areas:

- generator: `../scripts/generate_sample_metadata.py`;
- library and loading: `../source/Engine/KitLibModel.*`, `InstrumentSynthLoader.cpp`, and `SampleLoader.*`;
- runtime and maps: `../source/Engine/InstrumentSynth*`, `DrumKitEngine*`, and mapping helpers;
- UI: `../source/UI/PadContextPopup.*`, `InstrumentConfigPanel.*`, and `EngineSettingsPanel.*`;
- focused parser, map, synth, Python, and listening checks.

Non-goals:

- no sample or folder renaming for generated analysis data;
- no generated analysis, hash, checksum, or version suffixes in sample filenames;
- no separate JSON runtime database;
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

No build or tests were run for Phases 1 through 5B or the Phase 4 listening correction. The read-only Phase 1 analyzer was run. The Phase 2 measurement prototype was not run, and no sample filename or source audio was changed. Phase 2 through Phase 5B verification remains pending.
