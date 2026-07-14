# TODO15: Floating Sample Selection Plan

Status: Design plan only. No implementation or verification has started.

## Scope And Ownership

- `layer`: one source instrument articulation, with its own parameters, assigned to a destination pad articulation.
- `named velocity level`: one source velocity label such as `043`.
- `round-robin variant`: one variant such as `a` or `b` inside a named velocity level.
- `logical hit`: one named velocity and round-robin variant, including all linked mic entries.
- `measured intensity`: the logical-hit pre-trim source-intensity dB produced by TODO14.
- `exact selection`: choose one named velocity region, then one variant inside that region.
- `floating selection`: choose one logical hit from nearby measured intensities, even when it belongs to another named velocity level.
- `candidate window`: a small fixed list of logical hits prepared for one `selectionTargetDb` value.
- `complete hit`: a logical hit whose required active mic sample buffers are all ready in one thread-safe readiness snapshot.

This plan changes sample selection only.

[TODO14](TODO14_SAMPLE_CALIBRATION_AND_VELOCITY_RESPONSE_PLAN.md) owns sample analysis, filename metadata, calibration trim, `selectionTargetDb[128]`, the exact logical-hit selector, group voice reservation, and per-layer calibration curves. This plan reuses those contracts and only adds the floating policy. [TODO16](TODO16_DERIVED_SAMPLE_CACHE_AND_BAKE_CACHE_PLAN.md) owns persistent derived sample storage.

## Problem And Goal

Some instruments have many named velocity levels but only one or two round-robin variants at each level. Exact selection can repeat the same hit many times at one MIDI velocity, even though nearby recorded hits have a suitable strength.

Floating selection should use measured hit intensity as the main ordering. It may cross named velocity boundaries inside one articulation to find a close alternative. It must not cross into another articulation.

For a calibrated layer:

- repeated notes can use suitable logical hits from nearby measured intensities;
- the selected hit stays within an agreed distance from the requested target;
- a distant hit cannot win only because it was used less recently;
- all active mic entries from one logical hit start together after TODO14 group reservation, or none starts;
- mic entries from different variants are never combined;
- incomplete candidates are skipped;
- missing, invalid, or conflicting intensity metadata uses exact selection;
- primary and shadow layers keep independent selection history;
- response or mode changes rebuild selection data without reloading audio;
- runtime work is fixed and bounded;
- the audio thread does not allocate, parse, log, lock, access files, or scan the full articulation.

The main reference case is an articulation with many named velocities and no more than two variants at most levels.

## Current Facts And Constraints

- `../source/Engine/InstrumentSynth.h` stores `velocityToLayer[128]`.
- Current exact selection chooses the first named velocity greater than or equal to incoming velocity.
- `../source/Engine/InstrumentSynthController.cpp` normally chooses a ready round-robin variant inside that selected layer, but no-ready and RR-lock paths can return an incomplete variant.
- A logical hit is currently identified by velocity-layer index and variant index.
- Several mic `SampleEntry` objects can share one logical-hit identity.
- Current loaded fallback can search another named velocity when the preferred layer is unavailable.
- Progressive loading favors representative raw velocities and low variant indexes.
- TODO14 changes output gain, so raw MIDI velocity must remain separate from calibrated gain.
- Round-robin lock is used for predictable sample audition and must keep a clear meaning.

The local [DrumGizmo floating selection reference](drumgizmoFloatingVelocityGroups.cpp) treats recorded hits as candidates ordered by measured power. It scores intensity distance, recent use, and a random term. Karatala can reuse that general idea, but it must not copy an unbounded search or unexplained weights into the audio thread.

## Plan Decisions

- Add a separate per-layer `selectionMode` field with `Automatic`, `Exact`, and `Floating` choices. Do not reuse TODO14's `calibrationMode` field.
- `Automatic` enables floating selection only for a reviewed dense-velocity, low-round-robin topology with complete valid intensity metadata.
- `Exact` preserves current named-velocity behavior.
- `Floating` is an explicit manual choice, but invalid metadata still falls back safely to exact selection.
- Determine the automatic topology thresholds from library inspection and listening in Phase 1. Do not guess them in this document.
- Use TODO14 logical-hit pre-trim intensity. Do not use post-trim peak, raw filename velocity, or output gain as a substitute.
- Select one complete logical hit, identified by named velocity and variant.
- Keep the individual intensity of every hit. TODO14 raw medians produce trims and fitted source values guide the exact target region. Neither replaces hit-level intensity.
- Precompute a fixed candidate window for each MIDI target outside playback.
- Keep exact lookup beside floating windows as the fallback path.
- Keep selection history inside each mapped layer runtime. Do not share history between primary and shadow layers or between plugin instances.
- Use a hard intensity eligibility limit before diversity and randomness are scored.
- Keep the first version's weights internal and reviewed. Add UI only if real use shows a need.
- Keep RR lock on the exact path for predictable audit behavior.

## TODO14 Filename Intensity Contract

TODO14 must provide one logical-hit pre-trim intensity for every calibrated hit and one `selectionTargetDb[128]` table for every effective mapped layer.

- Every separate mic file belonging to one logical hit contains the same logical-hit intensity.
- A multichannel file naturally gives all of its mic ranges the same hit identity and intensity.
- The value is measured before filename calibration trim.
- The value and `selectionTargetDb` use the same source-intensity dB unit.
- The value remains separate from named velocity, raw velocity median, fitted source value, peak and decay data, global output gain, layer correction, and `Mix Gain`.
- Linked files with missing or conflicting shared values invalidate floating selection for that articulation.
- The runtime reconstructs the value from filename metadata. No calibration sidecar is required.

The exact compact filename code, range, precision, metric version, and fallback rules are finalized in TODO14 Phase 3.

For `midiVelocity`, floating distance is:

```text
abs(logicalHitIntensityDb - selectionTargetDb[midiVelocity])
```

Never compare a floating hit directly with raw MIDI velocity, post-trim peak, playback gain, or `Mix Gain`.

## Proposed Runtime Model

Build immutable logical-hit descriptions outside playback. Each description contains:

```text
LogicalHitRuntime
  velocity-layer index
  variant index
  measured pre-trim intensity
  required active SampleEntry references
  stable tie order
```

Keep changing buffer readiness in a separate atomic or immutable snapshot. Do not place mutable readiness inside an otherwise immutable logical-hit description.

Each mapped layer owns mutable selection state:

```text
LogicalHitSelectionState
  recent-use history
  random-generator state
```

Selection returns one structured result:

```text
HitSelectionResult
  validity
  logical-hit index
  velocity-layer index
  variant index
  requested selection target dB
```

Voice start iterates only the precomputed active mic entries of that logical hit.

Immutable library descriptions may be shared. Mutable history must not be shared.

## Bounded Candidate Windows

Build candidate windows after TODO14 logical-hit grouping and active mic routing are known. Rebuild them when `selectionTargetDb[128]` or selection mode changes. Do not reload sample audio for this rebuild.

For every MIDI velocity from 1 to 127:

1. Read `selectionTargetDb[midiVelocity]` from TODO14.
2. Order valid logical hits by measured-intensity distance.
3. Reject hits outside the agreed hard distance limit.
4. Keep a fixed maximum number of nearby hits.
5. Always include the closest valid hit.
6. Include hits below and above the target when available.
7. Allow hits from neighboring named velocity levels.
8. Keep stable ordering for equal values.
9. Store logical-hit indexes in a fixed-capacity array plus count.

Choose the capacity from corpus review and performance measurement. It must cover dense one-variant and two-variant layouts without creating an unbounded runtime scan.

Do not fill a window only with variants from one named level when equally close neighboring levels offer useful alternatives.

Publish rebuilt windows through immutable runtime data or the existing safe graph-update path.

## Real-Time Selection

For a floating note:

1. Keep raw MIDI velocity separately from output gain.
2. Read the precomputed candidate window built for `selectionTargetDb[midiVelocity]`.
3. Skip candidates whose complete logical hit is not ready.
4. Score the remaining candidates.
5. Choose one result with stable tie handling.
6. Use TODO14's fixed-size preflight and group voice reservation.
7. Start every required active mic entry, or start none.
8. Update history only after the complete hit starts.
9. Apply TODO14 calibrated output gain and the normal layer runtime controls.

The score uses three parts:

- measured-intensity distance as the main cost;
- a recent-use penalty to reduce immediate repetition;
- a small random term to avoid a fixed pattern.

Distance eligibility is checked before scoring. Diversity and randomness cannot make a clearly wrong intensity eligible.

Use audio sample position for recent-use timing. Do not use wall-clock time.

Use a small synth-local random state with no lock. Automated checks must be able to inject a fixed seed or fixed random values.

If no floating candidate is ready, use one complete exact fallback. If no complete hit is ready, or complete voice reservation fails, return no selection instead of playing only part of the mic set.

## Exact Fallback And RR Lock

Calibrated exact selection remains:

```text
selectionTargetDb[midiVelocity]
-> precomputed named velocity region
-> one ready variant
-> one complete logical hit
-> all active linked mic entries
```

Missing or conflicting calibration uses the legacy exact region lookup instead:

```text
raw MIDI velocity
-> current velocityToLayer region
-> one complete ready variant
-> one complete logical hit
```

RR lock uses the active exact region lookup. Lock `1` means the first variant, lock `2` means the second variant, and so on. The implementation phase must define the fallback when a locked variant is incomplete or unavailable.

RR lock does not consume floating history or random state.

## Calibrated Progressive Loading

Floating selection needs early loading based on candidate coverage, not raw filename labels.

Recommended order:

1. Closest complete hits for representative target intensities of main articulations.
2. Closest complete hits for representative targets of other articulations.
3. Remaining hits used by candidate windows, ordered by best rank and coverage.
4. Hits outside active windows and extra alternatives.

Every active mic entry of one logical hit gets the same logical priority. A hit becomes ready only after all required entries are ready.

Lower-ranked complete candidates may play while preferred hits are still loading. Already loaded audio is not removed when a table rebuild changes future priority.

All mapped hits must still load eventually.

Persistent buffers, cache keys, and Bake Cache behavior remain owned by TODO16.

## Layer, Preview, And Hi-Hat Rules

- Store the per-layer selection mode in the instrument map.
- Treat a mode change as a parameter-only update when source and routing are unchanged.
- Primary and shadow layers each own independent history and random state.
- Changing one mapped source resets only that layer's selection history.
- Preview and normal playback use the same effective selection mode unless an explicit audit preview mode is later approved.
- Each started voice keeps raw MIDI velocity, requested selection target dB, effective output gain, and selected logical-hit identity.
- Do not reconstruct MIDI velocity from nonlinear output gain.
- A hi-hat replacement selects one complete target logical hit and reuses that result for all replacement mic voices.
- Do not run independent selection for each active mic voice.
- Keep TODO14's accepted hi-hat transition behavior apart from the required loudness and logical-hit inputs.

## Implementation Phases

### Phase 1: Confirm policy and reference cases

- Inspect real library topologies and choose the automatic-mode thresholds.
- Choose candidate capacity, hard intensity limit, initial score weights, and recent-use history length.
- Decide normal-session random seeding, preview audit behavior, RR-lock fallback, and hi-hat history behavior.

Verify later: record decisions and keep reference fixtures for dense one-variant, dense two-variant, and normal round-robin articulations.

### Phase 2: Add floating eligibility to TODO14 logical hits

- Consume TODO14 immutable logical-hit descriptions, readiness snapshots, and structured results.
- Validate that every eligible hit has one source-intensity dB value.
- Calculate automatic-mode eligibility from the reviewed topology rules.
- Add floating-policy state without changing exact audible behavior.

Verify later: mono, multichannel, separate-mic, missing, conflicting, topology eligibility, and exact round-robin fixtures.

### Phase 3: Build bounded candidate data

- Add fixed-capacity candidate windows to articulation runtime data.
- Build them from TODO14 `selectionTargetDb[128]`.
- Preserve exact lookup beside them.
- Publish response and mode changes without sample reload.

Verify later: range boundaries, equal intensities, sparse sets, dense sets, manual response changes, and deterministic tie order.

### Phase 4: Add floating selection

- Add hard eligibility and bounded scoring.
- Add per-layer recent-use and random state.
- Return one logical-hit result and start only its entries.
- Reuse TODO14 all-or-nothing buffer preflight and group voice reservation.
- Use exact complete-hit fallback when needed.

Verify later: fixed seeds, recent-use avoidance, distance protection, group reservation failure, no allocation, no full scan, and no partial or mixed mic variants.

### Phase 5: Update loading priority

- Calculate load priority from target coverage and candidate rank.
- Queue all active mic entries of one hit with one logical priority.
- Publish readiness only after the complete hit is ready.
- Keep existing source-buffer deduplication.

Verify later: first playable data covers calibrated intensities, fallback changes naturally as preferred hits arrive, and all hits eventually load.

### Phase 6: Integrate map, layers, preview, and hi-hat

- Add and serialize the per-layer mode.
- Preserve independent primary and shadow state.
- Preserve exact RR-lock audit behavior.
- Store raw velocity and selected-hit data on voices.
- Update preview and hi-hat replacement to select one complete logical hit.

Verify later: layered slots, locked audition, preview, pedal transitions, save and reload, and no sample reload for a mode-only change.

## Verification Matrix

Focused checks should cover:

- one level with several variants;
- many levels with one variant each;
- many levels with two variants each;
- irregular and equal measured intensities;
- requests below and above the measured range;
- missing and conflicting intensity metadata;
- partially loaded candidate sets;
- one complete exact fallback;
- several mic files for one hit;
- one multichannel hit;
- fixed random sequences and stable ties;
- RR lock inside and outside available variants;
- independent primary and shadow history;
- response and mode changes without sample reload;
- calibrated loading order;
- hi-hat transitions between different velocity layouts;
- preview and normal playback using the intended selector.

Performance review must confirm fixed candidate count and no real-time allocation, parsing, logging, file access, locking, or full-articulation scan.

Listening review must compare exact, automatic, and floating behavior on drums, cymbals, hi-hats, and short percussion.

## Dependencies And Non-Goals

Related plans:

- [TODO14 sample calibration and velocity response](TODO14_SAMPLE_CALIBRATION_AND_VELOCITY_RESPONSE_PLAN.md), whose filename, `selectionTargetDb`, exact logical-hit, readiness, and group-reservation contracts must exist before Phase 2
- [TODO16 derived sample cache and Bake Cache](TODO16_DERIVED_SAMPLE_CACHE_AND_BAKE_CACHE_PLAN.md)
- [TODO04 sample loading runtime](TODO04_SAMPLE_LOADING_RUNTIME_PLAN.md)
- [TODO12 hi-hat transition](TODO12_HIHAT_TRANSITION_FIX_PLAN.md)
- [Audio thread performance](AUDIO_THREAD_PERF_PLAN.md)

Likely implementation areas:

- `../source/Engine/KitLibModel.*`
- `../source/Engine/InstrumentSynth.h`
- `../source/Engine/InstrumentSynthLoader.cpp`
- `../source/Engine/InstrumentSynthController.cpp`
- `../source/Engine/SampleLoader.*`
- instrument-map parsing and shared layer serialization
- focused parser, selector, loading, layering, preview, and hi-hat checks

Non-goals:

- no new loudness metric or global response;
- no second implementation of TODO14 exact logical-hit selection or group voice reservation;
- no audio baking;
- no pan, timing, phase, or room-orientation work;
- no selection across different articulations;
- no automatic gain reduction for layered pads;
- no shared history between mapped layers;
- no unbounded outward search;
- no output-bus redesign;
- no required scoring-weight UI in the first version.

No build, tests, analyzer run, or listening check was performed while writing this plan.
