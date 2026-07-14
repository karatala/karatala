# TODO16: Derived Sample Cache And Bake Cache Plan

Status: Design plan only. No implementation or verification has started.

## Scope And Ownership

- `source file`: the original audio file in the sample library.
- `source preprocessing recipe`: the complete typed description of fixed transforms applied before a sample buffer is published.
- `derived sample`: immutable audio created from one source file and one preprocessing recipe.
- `RAM cache`: processed buffers held in memory for current playback.
- `persistent derived cache`: optional local disk storage for reusable derived samples.
- `Bake Cache`: a user action that prepares persistent derived samples before they are needed for playback.
- `runtime control`: a value that depends on the note, layer, plugin instance, routing, or current user state and must not be baked into shared source data.

This plan owns persistent derived sample storage, the Bake Cache workflow, cleanup, and multi-instance safety. It consumes the complete RAM-cache recipe and key owned by TODO14. It does not define them a second time.

[TODO14](TODO14_SAMPLE_CALIBRATION_AND_VELOCITY_RESPONSE_PLAN.md) owns filename calibration metadata, fixed trim inputs, the canonical full-file source recipe, the RAM-cache key, and calibrated preload targets. [TODO15](TODO15_FLOATING_SAMPLE_SELECTION_PLAN.md) owns floating logical-hit candidates and their loading priority.

This plan does not change source audio files.

## Goal And Acceptance Criteria

The engine should avoid repeating the same fixed sample processing on every load while keeping live controls correct.

Success means:

- fixed calibration trim is applied once before the derived buffer enters the RAM cache;
- every audio-changing recipe value is part of cache identity;
- equal source identity and equal recipes reuse one derived sample;
- different recipes never share one derived sample;
- a changed source file cannot reuse stale derived audio;
- preview, progressive loading, normal playback, and Bake Cache use the same recipe builder and processing path;
- Bake Cache prepares all required source-recipe pairs for the current instrument map in the background;
- normal playback still works when the persistent cache is disabled, missing, read-only, full, corrupt, or interrupted;
- cancelled or failed work leaves no valid-looking partial entry;
- several plugin instances do not corrupt the same cache;
- the audio thread never reads files, writes files, waits for baking, locks a disk-cache index, or performs unbounded cache work;
- global velocity response and live layer controls remain adjustable without rebuilding derived audio;
- persistent cache size and cleanup are visible and controllable;
- measured loading time and CPU use improve enough to justify the disk cost.

The final phase must record load-time, CPU, disk-size, and playback results. This document does not assume that one disk format is best before measurement.

## Current Facts And Problems

Current sample loading broadly does this on a worker:

1. Read the source audio.
2. Limit the resolved sample length.
3. Apply filename `volumeTrimDb`.
4. Apply the resolved end fade.
5. Insert the processed buffer into the RAM cache.
6. Publish it for playback.

This already places fixed filename trim on the correct side of the RAM-cache boundary.

The current problems are:

- the RAM-cache key uses path, source size, source modification time, and a free-form `preprocessTag`;
- trim, resolved read length, and resolved fade are not complete typed key fields even though they change the buffer;
- queue deduplication uses the same incomplete identity;
- an incomplete key can return audio processed for another request;
- the cache is memory-only, so the same work can return after restart or RAM eviction;
- there is no user action that prepares all current-map derived samples in advance;
- there is no persistent-cache size limit, cleanup policy, or corruption recovery because no persistent cache exists yet;
- progressive loading uses raw velocity labels today, while TODO14 and TODO15 need calibrated logical-hit coverage.

TODO14 Phase 4 fixes the RAM recipe and queue identity before this plan starts. They are listed here because persistent data must reuse the corrected identity exactly.

The current shared immutable-buffer model is useful and should remain.

## Bakeable And Runtime-Only Processing

A transform may be part of a source preprocessing recipe only when it is:

- deterministic;
- fixed for the source request;
- safe to share between every consumer of that exact recipe;
- represented by typed, canonical parameters;
- covered by a schema and algorithm version;
- applied outside the audio thread.

Initial bakeable processing:

- complete source-file decode with its original channel layout;
- resolved full-file read start and length;
- fixed filename calibration trim;
- fixed load-time fade;
- required sample-format conversion already performed by the loader.

Possible later bakeable processing, only after its own review:

- a reviewed channel-scoped timing offset inside the full-file buffer;
- a reviewed channel-scoped swap or orientation correction;
- another small source-intrinsic correction with a stable versioned algorithm.

The following stay at runtime and are not part of a shared derived sample:

- global loudness response;
- exact or floating sample selection;
- manual layer calibration curve and residual correction;
- `Mix Gain`;
- tuning;
- attack and decay envelopes;
- bleed;
- layer pan, phase, and L/R controls;
- mic routing and output selection;
- choke, aftertouch, crosstalk, and hi-hat behavior;
- plugin-instance effects and automation.

The source response curve selects a logical hit. It does not change that hit's audio data and must not be baked into the sample.

If a future effect depends on MIDI velocity, note history, mapped layer, routing, or automation, it remains runtime processing.

## Canonical Source Preprocessing Recipe

Resolve defaults before creating the key. The key must never depend on an omitted value having an implicit meaning.

A typed recipe should include at least:

```text
SourcePreprocessingRecipe
  schema version
  processing algorithm version
  decoded full-file channel layout
  resolved full-file start frame
  resolved frame count
  resolved fade frames and fade algorithm
  quantized calibration trim
  output sample format fields that affect stored data
  reviewed channel-scoped transform list, when added later
```

Use stable integer or enum fields where possible. Do not serialize locale-dependent decimal text into a key.

Cache identity is:

```text
source content identity + canonical source preprocessing recipe identity
```

The source identity must change when the source content changes. TODO14 Phase 4 chooses the practical fingerprint contract and its cost. Path alone is not enough. Size and modification time alone may be used as a fast check only if the design also prevents unsafe stale reuse.

Every processing algorithm change that can alter derived audio increments an owned version. Old entries then become misses and are cleaned later.

A free-form tag may remain for debugging, but it cannot replace typed recipe fields.

Keep one derived entry for the complete multichannel source file. `SampleEntry` chooses mic channel ranges after the shared buffer is loaded. Routing and channel-range selection are not part of the recipe or key. A future channel-scoped transform must preserve the published channel count and indexes so existing mic ranges remain valid.

## Immutable Buffer And Invalidation Rules

- A published derived buffer is immutable.
- Never apply a new transform in place to a buffer that another voice or layer may share.
- A changed recipe creates a new key and a new buffer.
- A changed source identity creates a new key and a new buffer.
- Calibration trim is applied exactly once before publication.
- Runtime gain is applied during voice rendering and is never written back to the shared buffer.
- Old RAM buffers may remain alive while voices still reference them.
- Old disk entries may remain until background cleanup.
- A targeted library reload requests new keys only for affected source content.

This avoids global cache flushes and avoids double correction.

## Lookup And Processing Flow

All entry points use one loader path:

1. Resolve source identity and the canonical recipe outside playback.
2. Check the RAM cache by the complete key.
3. If missing, check the persistent derived cache on a worker.
4. Validate the disk entry before use.
5. If valid, load it into an immutable RAM buffer and publish it.
6. If missing or invalid, decode the source and apply the complete recipe on a worker.
7. Publish the immutable RAM buffer.
8. Optionally write the same result to persistent storage in the background.

A disk-cache failure falls back to normal source loading. It must not make the instrument silent when the source file is valid.

Preview and regular loading may use different request priorities, but they must produce the same key and audio for the same source and recipe.

## Persistent Derived Cache Format And Storage

Store derived data outside the sample library so source folders stay portable and read-only libraries still work.

Each persistent entry must carry enough information to validate:

- full cache key;
- recipe schema and algorithm versions;
- source identity;
- sample rate, channel count, frame count, and stored sample format;
- payload size;
- a checksum or equivalent corruption check.

Choose the stored audio format after measurement:

- raw or lightly packed sample data uses more disk but loads with little CPU;
- lossless compressed data saves disk but still needs decode work;
- reusing the source format may fail to save the fixed processing cost.

Do not choose lossy storage for calibrated source data.

Write safely:

1. Create a temporary entry in the cache location.
2. Write header and payload.
3. Flush and close it as required by the platform contract.
4. Validate the finished temporary entry.
5. Atomically publish it under the final key.

Cancellation or failure removes the temporary entry. A reader never treats it as valid.

## Bake Cache Workflow

Add a dedicated `Bake Cache` action with clear scope text such as `Current Map`.

The first version should bake the current instrument map only. It should not scan the full library without a separate user request.

The workflow:

1. Snapshot the current map, routing, source identities, and source recipes.
2. Reuse normal logical-hit discovery and load requests.
3. Deduplicate equal source-recipe pairs.
4. Count existing valid disk hits separately from new work.
5. Process missing entries on background workers with bounded concurrency.
6. Keep audio playback work at higher priority.
7. Publish each completed entry atomically.
8. Report final hits, new entries, skipped entries, failures, and disk size.

Suggested controls:

- `Bake Cache` for the current map;
- `Cancel` while baking;
- `Clear Derived Cache`;
- `Open Cache Folder` where supported;
- a size limit and current-size display.

Suggested status:

- source-recipe pairs completed and total;
- cache hits and newly created entries;
- current item or articulation;
- bytes written;
- cancellation or failure summary.

If the map or a source-recipe input changes during baking, the active job continues from its immutable snapshot or is cancelled by an explicit policy. A changed source recipe creates a different key and cannot corrupt the active job. Global response, manual calibration curves, Mix Gain, and other runtime-only changes do not create new keys.

## Progressive Loading Integration

Bake Cache and normal progressive loading must reuse the same request builder, key, decode, process, and validation code.

They differ only in scope and priority:

- normal loading prepares the most useful playable set first;
- TODO14 orders early work by representative calibrated target intensities;
- TODO15 adds candidate-window coverage when floating mode is active;
- Bake Cache prepares every required source-recipe pair in the current map;
- all mapped samples still become available eventually during normal loading.

Do not build a second library scanner only for Bake Cache.

All active mic entries of one logical hit should receive a coherent priority, but each source file still has its own cache key and derived entry.

## Cleanup And Multi-Instance Safety

Persistent storage needs a bounded lifecycle.

- Enforce a user-visible size limit.
- Remove invalid and obsolete-version entries in the background.
- When over limit, remove old unused entries first.
- Never delete an entry while it is being read or published.
- Keep cleanup work off the audio thread.
- Let `Clear Derived Cache` stop or coordinate with active bake jobs safely.
- A failed delete is reported but does not break playback.

Several plugin instances or host processes may request the same key.

- Coordinate work per key, not with one global disk lock held during decode.
- It is acceptable for two processes to calculate the same data if atomic publication keeps one valid result.
- A reader must handle an entry appearing or disappearing between index lookup and open.
- Stale lock or claim data must recover after a crash.
- Cache metadata updates must not require the audio thread to wait.

The exact cross-process mechanism is chosen in Phase 2 after platform review.

## Layer Merging Decision

Do not merge mapped layers in this plan.

Layers can have different:

- named velocity counts and logical-hit intensities;
- round-robin counts and histories;
- active mic files and routes;
- tuning, envelopes, pan, phase, bleed, and `Mix Gain`;
- selection modes and manual calibration;
- choke and hi-hat behavior.

Pre-merging those combinations would create many derived versions, remove independent live control, and may use more disk and preparation time than it saves.

A later benchmark may test a very narrow static case, but layer merging is not an assumed cache feature.

## Implementation Phases

### Phase 1: Adopt the TODO14 recipe for persistent identity

- Require TODO14 Phase 4 and its corrected RAM-cache identity first.
- Reuse the same canonical recipe bytes or digest for persistent identity.
- Add the persistent format version without changing recipe meaning.
- Confirm that preview, progressive loading, normal loading, and Bake Cache request the same key.
- Keep one complete multichannel payload per source-recipe pair.

Verify later: a RAM hit and persistent hit agree on identity; equivalent requests share a key; different trim, length, fade, source version, channel-scoped transform, or algorithm version does not share a key; routing and mic channel-range selection do not create duplicate full-file entries; trim is applied once.

### Phase 2: Add the persistent derived store

- Choose the measured disk format and cache location contract.
- Add entry header, validation, checksum, and version handling.
- Add atomic temporary writes and safe publication.
- Add per-key multi-instance coordination and crash recovery.
- Keep normal source loading as the fallback.

Verify later: hit, miss, corruption, stale source, interrupted write, read-only location, full disk, and two-process contention.

### Phase 3: Integrate progressive loading

- Route persistent hits and misses through the existing sample-loading path.
- Use TODO14 calibrated representative priorities.
- Use TODO15 candidate coverage when floating selection is active.
- Keep complete logical-hit readiness and eventual full loading.
- Keep playback work above background cache writes.

Verify later: first-playable behavior, complete mic sets, cache hit reuse, cancellation, and no duplicate processing inside one process.

### Phase 4: Add Bake Cache UI

- Add the current-map Bake Cache action and progress.
- Add cancellation and clear controls.
- Display cache size, valid hits, new entries, skipped work, and failures.
- Keep one immutable job snapshot and bounded worker concurrency.

Verify later: start, cancel, restart, map change during bake, close and reopen, clear during idle, and clear coordination during active work.

### Phase 5: Add cleanup and lifecycle rules

- Add size limits and old-entry cleanup.
- Remove invalid and obsolete-version entries safely.
- Handle multiple plugin instances and host processes.
- Add useful diagnostics without realtime logging.

Verify later: limit enforcement, active readers, crash leftovers, stale claims, failed deletes, and concurrent cleanup.

### Phase 6: Measure and decide expansion

- Compare cold source load, RAM hit, persistent hit, and pre-baked current-map load.
- Record CPU time, wall time, peak RAM, disk size, and first-playable time.
- Confirm derived audio matches normal processing within the agreed exact or numeric tolerance.
- Test future fixed offset or channel correction only after TODO14 review approves its metadata and recipe scope.
- Decide whether any additional fixed source transform is worth supporting.

Verify later: keep only features with a clear measured benefit and stable output.

## Verification Matrix

Focused checks should cover:

- positive, negative, zero, and decimal calibration trim;
- different resolved lengths and fades for one source;
- equal canonical recipes built from different input spelling;
- changed source content at the same path;
- preview and normal playback identity;
- progressive and Bake Cache identity;
- no double trim after a persistent hit;
- invalid header, checksum failure, truncated payload, and wrong version;
- cancellation before and during publication;
- read-only and full cache locations;
- size cleanup with active readers;
- two plugin instances requesting one key;
- primary and shadow layers sharing only truly equal source recipes;
- global response and runtime layer changes without a rebake;
- targeted new data after an audio-changing filename trim or recipe change;
- all current-map source-recipe pairs prepared by Bake Cache.

Performance checks must confirm no file I/O, persistent-index lock, allocation, decode, or cache cleanup on the audio thread.

Listening and numeric comparison must confirm that cached and normally processed audio are equivalent.

## Dependencies And Non-Goals

Related plans:

- [TODO14 sample calibration and velocity response](TODO14_SAMPLE_CALIBRATION_AND_VELOCITY_RESPONSE_PLAN.md), whose full-file recipe and corrected RAM-cache key are prerequisites for Phase 1
- [TODO15 floating sample selection](TODO15_FLOATING_SAMPLE_SELECTION_PLAN.md)
- [TODO04 sample loading runtime](TODO04_SAMPLE_LOADING_RUNTIME_PLAN.md)
- [Library snapshot plan](TODO_LIBRARY_SNAPSHOT_PLAN.md)
- [Audio thread performance](AUDIO_THREAD_PERF_PLAN.md)

Likely implementation areas:

- `../source/Engine/SampleLoader.*`
- `../source/Engine/InstrumentSynthLoader.cpp`
- sample request and runtime graph helpers
- plugin settings and cache-location helpers
- a small cache status and Bake Cache UI surface
- focused recipe, loader, disk-store, cancellation, cleanup, and concurrency checks

Non-goals:

- no source audio-file modification;
- no second implementation of TODO14 canonical recipe or RAM-cache key;
- no baking of global velocity response or live layer controls;
- no baking of exact or floating sample selection into audio;
- no layer merging;
- no lossy derived format;
- no full-library scan in the first Bake Cache version;
- no persistent cache requirement for normal playback;
- no audio-thread file I/O, waiting, locks, decode, or cleanup;
- no general offline effects framework.

No build, tests, analyzer run, cache bake, or listening check was performed while writing this plan.
