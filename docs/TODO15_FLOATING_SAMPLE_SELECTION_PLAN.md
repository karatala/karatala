# TODO15: Floating Sample Selection Plan

Status: The initial automatic selector is implemented. Focused tests are written but have not been run. Loading priority, preview, and hi-hat transition integration remain open.

## Scope And Ownership

- `map layer`: one library articulation, with its own parameters, assigned to an articulation slot.
- `velocity level`: one recorded velocity group such as `043`.
- `round robin ID`: one letter such as `a` or `b` that identifies an alternate logical hit at the same velocity level.
- `logical hit`: one recorded hit at one velocity level. It includes all linked mic samples.
- `measured intensity`: the logical-hit pre-trim source-intensity dB produced by TODO14.
- `exact selection`: choose one velocity level, then one logical hit inside that level.
- `floating selection`: choose one logical hit from nearby measured intensities, even when it belongs to another velocity level.
- `candidate window`: a small fixed list of logical hits prepared for one `selectionTargetDb` value.
- `complete hit`: a logical hit whose required active sample buffers are all ready in one thread-safe readiness snapshot.

This plan changes sample selection only.

[TODO14](TODO14_SAMPLE_CALIBRATION_AND_VELOCITY_RESPONSE_PLAN.md) owns sample analysis, filename metadata, calibration trim, `selectionTargetDb[128]`, the exact logical-hit selector, group voice reservation, and map layer calibration curves. This plan reuses those contracts and only adds the floating policy. [TODO16](TODO16_DERIVED_SAMPLE_CACHE_AND_BAKE_CACHE_PLAN.md) owns persistent derived sample storage.

## Problem And Goal

Some instruments have many velocity levels but only one or two logical hits at each level. Exact selection can repeat the same hit many times at one MIDI velocity, even though nearby recorded hits have a suitable strength.

Floating selection should use measured hit intensity as the main ordering. It may cross velocity-level boundaries inside one library articulation to find a close alternative. It must not cross into another library articulation.

For a calibrated map layer:

- repeated notes can use suitable logical hits from nearby measured intensities;
- the selected hit stays within an agreed distance from the requested target;
- a distant hit cannot win only because it was used less recently;
- all active mic entries from one logical hit start together after TODO14 group reservation, or none starts;
- mic entries from different variants are never combined;
- incomplete candidates are skipped;
- missing, invalid, or conflicting intensity metadata uses exact selection;
- primary and additional map layers keep independent selection history;
- calibration or exact-boundary changes rebuild selection data without reloading audio;
- runtime work is fixed and bounded;
- the audio thread does not allocate, parse, log, lock, access files, or scan the full articulation.

The main reference case is a library articulation with many velocity levels and no more than two logical hits at most levels.

## Current Facts And Constraints

- `../source/Engine/InstrumentPlayer.h` stores `velocityToLevel[128]`.
- Current exact selection chooses the first velocity level with a filename velocity greater than or equal to the incoming velocity.
- `../source/Engine/InstrumentPlayerController.cpp` chooses one complete ready logical hit inside the selected velocity level.
- A logical hit is identified by its velocity level and round robin ID.
- Several mic `SampleEntry` objects can share one logical-hit identity.
- Current loaded fallback can search another velocity level when the preferred level is unavailable.
- Progressive loading favors representative raw velocities and low round robin IDs.
- TODO14 changes output gain, so raw MIDI velocity must remain separate from calibrated gain.
- Round-robin lock is used for predictable sample audition and must keep a clear meaning.

The local [DrumGizmo floating selection reference](drumgizmoFloatingVelocityGroups.cpp) treats recorded hits as candidates ordered by measured power. It scores intensity distance, recent use, and a random term. Karatala can reuse that general idea, but it must not copy an unbounded search or unexplained weights into the audio thread.

## Plan Decisions

- The first version is automatic. Do not add a per-map-layer selection mode.
- Enable floating selection only when an articulation has at least the global `minimumVelocityLevels` setting, every velocity level has no more than two logical hits, and every logical hit has valid intensity metadata.
- Store `minimumVelocityLevels` in global engine settings. Allow values from `2` through `5` and use `2` by default.
- Preserve current exact velocity-level behavior for every ineligible articulation and every fallback.
- The first automatic selector uses the exact target velocity level and its nearest recorded level below and above. It does not consider the full articulation during playback.
- Use TODO14 logical-hit pre-trim intensity. Do not use post-trim peak, raw filename velocity, or output gain as a substitute.
- Select one complete logical hit, identified by velocity level and round robin ID.
- Keep the individual intensity of every hit. TODO14 raw medians produce trims and fitted source values guide the exact target region. Neither replaces hit-level intensity.
- Precompute a fixed candidate window for each MIDI target outside playback.
- Keep exact lookup beside floating windows as the fallback path.
- Keep selection history inside each map layer runtime. Do not share history between primary and additional map layers or between plugin instances.
- Use a hard intensity eligibility limit before diversity and randomness are scored. The initial automatic selector keeps candidates no more than `3 dB` farther from the target than the closest candidate.
- Keep the first version's weights internal and reviewed. Add UI only if real use shows a need.
- Keep round robin lock on the exact path for predictable audit behavior.

## TODO14 Filename Intensity Contract

TODO14 must provide one logical-hit pre-trim intensity for every calibrated hit and one `selectionTargetDb[128]` table for every effective map layer.

- Every separate mic file belonging to one logical hit contains the same logical-hit intensity.
- A multichannel file naturally gives all of its mic ranges the same hit identity and intensity.
- The value is measured before filename calibration trim.
- The value and `selectionTargetDb` use the same source-intensity dB unit.
- The value remains separate from filename velocity, raw velocity median, fitted source value, peak and decay data, global output gain, map layer correction, and `Mix Gain`.
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
  velocity-level index
  round robin ID
  measured pre-trim intensity
  required active SampleEntry references
  stable tie order
```

Keep changing buffer readiness in a separate atomic or immutable snapshot. Do not place mutable readiness inside an otherwise immutable logical-hit description.

Each map layer owns mutable selection state:

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
  velocity-level index
  round robin ID
  requested selection target dB
```

Voice start iterates only the precomputed active mic entries of that logical hit.

Immutable library descriptions may be shared. Mutable history must not be shared.

## Bounded Candidate Windows

Build candidate windows after TODO14 logical-hit grouping and active mic routing are known. Rebuild them when `selectionTargetDb[128]`, intensity metadata, or exact velocity boundaries change. Do not reload sample audio for this rebuild.

For every MIDI velocity from 1 to 127:

1. Read `selectionTargetDb[midiVelocity]` from TODO14.
2. Collect valid logical hits from the exact target velocity level and its nearest recorded level below and above.
3. Order those hits by measured-intensity distance.
4. Reject hits outside the agreed hard distance limit.
5. Keep a fixed maximum number of nearby hits.
6. Always include the closest valid hit.
7. Include hits below and above the target when available.
8. Keep stable ordering for equal values.
9. Store logical-hit indexes in a fixed-capacity array plus count.

Choose the capacity from corpus review and performance measurement. It must cover dense layouts with one or two logical hits per velocity level without creating an unbounded runtime scan.

Do not fill a window only with logical hits from one velocity level when equally close neighboring levels offer useful alternatives.

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
9. Apply TODO14 calibrated output gain and the normal map layer runtime controls.

The score uses three parts:

- measured-intensity distance as the main cost;
- a recent-use penalty to reduce immediate repetition;
- a small random term to avoid a fixed pattern.

Distance eligibility is checked before scoring. Diversity and randomness cannot make a clearly wrong intensity eligible.

Use audio sample position for recent-use timing. Do not use wall-clock time.

Use a small instrument-player-local random state with no lock. Automated checks must be able to inject a fixed seed or fixed random values.

If no floating candidate is ready, use one complete exact fallback. If no complete hit is ready, or complete voice reservation fails, return no selection instead of playing only part of the mic set.

## Exact Fallback And Round Robin Lock

Calibrated exact selection remains:

```text
selectionTargetDb[midiVelocity]
-> precomputed velocity-level region
-> one ready logical hit
-> one complete logical hit
-> all active linked mic entries
```

Missing or conflicting calibration uses the legacy exact region lookup instead:

```text
raw MIDI velocity
-> current velocityToLevel region
-> one complete ready logical hit
-> one complete logical hit
```

Round robin lock uses the active exact region lookup. Lock `1` means the first round robin ID, lock `2` means the second round robin ID, and so on. The implementation phase must define the fallback when a locked logical hit is incomplete or unavailable.

Round robin lock does not consume floating history or random state.

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

## Map Layer, Preview, And Hi-Hat Rules

- Read the global `minimumVelocityLevels` setting during selection so a setting change applies without sample reload.
- Primary and additional map layers each own independent history and random state.
- Changing one map layer's library assignment resets only that map layer's selection history.
- Keep exact logical-hit preview behavior for predictable sample auditing.
- Each started voice keeps raw MIDI velocity, requested selection target dB, effective output gain, and selected logical hit identity.
- Do not reconstruct MIDI velocity from nonlinear output gain.
- A hi-hat replacement selects one complete target logical hit and reuses that result for all replacement mic voices.
- Do not run independent selection for each active mic voice.
- Keep TODO14's accepted hi-hat transition behavior apart from the required loudness and logical-hit inputs.

## Implementation Phases

### Phase 1: Confirm policy and reference cases

- ✅ Confirm the saved global `minimumVelocityLevels` range `2..5` and default `2` in the Settings panel.
- ✅ Use six candidates, the relative `3 dB` limit, the DrumGizmo-style initial weights, and audio-sample-position history.
- ✅ Keep preview and round-robin lock on the exact path.
- Decide normal-session random seeding and hi-hat history behavior after listening review.

Verify later: record decisions and keep reference fixtures for dense articulations with one or two logical hits per velocity level, and for normal round robin articulations.

### ✅ Phase 2: Add floating eligibility to TODO14 logical hits

- ✅ Consume TODO14 logical-hit descriptions, readiness snapshots, and structured results.
- ✅ Validate that every eligible hit has one source-intensity dB value.
- ✅ Calculate automatic eligibility from the saved global threshold and the maximum of two logical hits per velocity level.
- ✅ Add floating-policy state without changing exact fallback behavior.

Verify later: mono, multichannel, separate-mic, missing, conflicting, topology eligibility, and exact round robin fixtures.

### ✅ Phase 3: Build bounded candidate data

- ✅ Add fixed-capacity candidate windows to articulation runtime data.
- ✅ Build them from TODO14 `selectionTargetDb[128]`.
- ✅ Preserve exact lookup beside them.
- ✅ Publish rebuilt calibration and exact-boundary tables without sample reload.

Verify later: range boundaries, equal intensities, sparse sets, dense sets, manual response changes, and deterministic tie order.

### ✅ Phase 4: Add floating selection

- ✅ Add hard eligibility and bounded scoring.
- ✅ Add per-map-layer recent-use and random state.
- ✅ Return one logical-hit result and start only its entries.
- ✅ Reuse TODO14 all-or-nothing buffer preflight and group voice reservation.
- ✅ Use exact complete-hit fallback when needed.

Verify later: fixed seeds, recent-use avoidance, distance protection, group reservation failure, no allocation, no full scan, and no partial or mixed logical hits.

### Phase 5: Update loading priority

- Calculate load priority from target coverage and candidate rank.
- Queue all active mic entries of one hit with one logical priority.
- Publish readiness only after the complete hit is ready.
- Keep existing source-buffer deduplication.

Verify later: first playable data covers calibrated intensities, fallback changes naturally as preferred hits arrive, and all hits eventually load.

### Phase 6: Integrate map layers, preview, and hi-hat

- ✅ Apply the saved global threshold independently to every map-layer runtime.
- ✅ Preserve independent state for each map layer.
- ✅ Preserve exact round-robin-lock audit behavior.
- Store raw velocity and selected-hit data on voices.
- Update preview and hi-hat replacement to select one complete logical hit.

Verify later: layered slots, locked audition, preview, pedal transitions, save and reload, and no sample reload for a threshold-only change.

## Verification Matrix

Focused checks should cover:

- one velocity level with several logical hits;
- many velocity levels with one logical hit each;
- many velocity levels with two logical hits each;
- irregular and equal measured intensities;
- requests below and above the measured range;
- missing and conflicting intensity metadata;
- partially loaded candidate sets;
- one complete exact fallback;
- several mic files for one hit;
- one multichannel hit;
- fixed random sequences and stable ties;
- round robin lock inside and outside available round robin IDs;
- independent history for each map layer;
- calibration, exact-boundary, and global-threshold changes without sample reload;
- calibrated loading order;
- hi-hat transitions between different velocity layouts;
- preview and normal playback using the intended selector.

Performance review must confirm fixed candidate count and no real-time allocation, parsing, logging, file access, locking, or full-articulation scan.

Listening review must compare exact fallback and automatic floating behavior on drums, cymbals, hi-hats, and short percussion.

## Dependencies And Non-Goals

Related plans:

- [TODO14 sample calibration and velocity response](TODO14_SAMPLE_CALIBRATION_AND_VELOCITY_RESPONSE_PLAN.md), whose filename, `selectionTargetDb`, exact logical-hit, readiness, and group-reservation contracts must exist before Phase 2
- [TODO16 derived sample cache and Bake Cache](TODO16_DERIVED_SAMPLE_CACHE_AND_BAKE_CACHE_PLAN.md)
- [TODO04 sample loading runtime](TODO04_SAMPLE_LOADING_RUNTIME_PLAN.md)
- [TODO12 hi-hat transition](TODO12_HIHAT_TRANSITION_FIX_PLAN.md)
- [Audio thread performance](AUDIO_THREAD_PERF_PLAN.md)

Likely implementation areas:

- `../source/Engine/KitLibModel.*`
- `../source/Engine/InstrumentPlayer.h`
- `../source/Engine/InstrumentPlayerLoader.cpp`
- `../source/Engine/InstrumentPlayerController.cpp`
- `../source/Engine/SampleLoader.*`
- instrument-map parsing and shared map layer serialization
- focused parser, selector, loading, layering, preview, and hi-hat checks

Non-goals:

- no new loudness metric or global response;
- no second implementation of TODO14 exact logical-hit selection or group voice reservation;
- no audio baking;
- no pan, timing, phase, or room-orientation work;
- no selection across different articulations;
- no automatic gain reduction for layered pads;
- no shared history between map layers;
- no unbounded outward search;
- no output-bus redesign;
- no required scoring-weight UI in the first version.

No build, tests, analyzer run, or listening check was performed while writing this plan.
