# TODO19: Adaptive Sample Loading And Dynamic Queue Plan

Date: 2026-08-27  
Status: Approved design. Implementation has not started.

## Goal

Make a mapped kit playable sooner and reduce its initial memory use without doing file work in the audio thread.

The loader must:

- load a small configurable number of complete round robins first;
- load the most useful velocity level before the other levels;
- keep the remaining round robins dormant in Adaptive mode;
- activate dormant work when the matching instrument articulation is played;
- raise the priority of instrument articulations that receive repeated notes;
- preserve Full mode for users who want every mapped sample loaded before normal use;
- store a portable loading-mode override in the plugin instance state.

## Current Facts

- [`SampleLoaderQueue`](../source/Engine/SampleLoader.h) owns four stage deques, but the current `determineStage()` implementation only returns stages 1, 2, and 3.
- [`SampleLoaderQueue::enqueueCandidate()`](../source/Engine/SampleLoader.cpp) queues one resolved audio file at a time. It deduplicates equivalent pending files by cache key and adds another callback to the existing request.
- A duplicate request does not currently raise the priority of the existing queued file.
- The worker always scans stages in order and then selects the lowest fixed instrument-type priority in that stage.
- The current fixed order starts with Kick, Snare, HiHat, Ride, Crash, and Tom.
- The worker sleeps for 100 ms when it finds no queued work. This is too long for late demand loading.
- [`InstrumentPlayer::selectEarlyLoadLogicalHits()`](../source/Engine/InstrumentPlayerLoader.cpp) currently marks up to five representative logical hits per articulation near calibrated targets `-24`, `-12`, `-6`, `-3`, and `0 dB`, or raw velocity targets `10`, `35`, `65`, `95`, and `120` when calibration is unavailable.
- [`InstrumentPlayer::enqueueVelocityLevelSamples()`](../source/Engine/InstrumentPlayerLoader.cpp) submits early hits first and then submits every remaining mapped hit. It registers progress before each file request.
- Playback selects only complete ready logical hits and can fall back to another ready velocity level. It must keep this non-blocking behavior.
- MIDI note handling runs in the audio thread. It must not lock the loader, allocate memory, access files, or call normal queue mutation methods.
- [`TODO04`](TODO04_SAMPLE_LOADING_RUNTIME_PLAN.md) is the historical progressive-playback plan. Its four-stage queue summary is not a complete description of the current queue.
- [`TODO14`](TODO14_SAMPLE_CALIBRATION_AND_VELOCITY_RESPONSE_PLAN.md) owns calibrated logical-hit intensity, complete-hit readiness, and the current representative early-load targets.

## Terms

- `logical hit`: one round robin at one recorded velocity level, with every active linked mic sample required for complete playback.
- `minimal set`: up to `initialRoundRobinsPerVelocity` logical hits from each velocity level of one mapped library articulation.
- `dormant hit`: a mapped logical hit that has a runtime load description but has not been submitted for file loading.
- `demand target`: one exact runtime instrument player, runtime articulation, and source velocity level. A library articulation ID alone is not unique because several slots and map layers can use it.
- `hot articulation`: one runtime instrument-player articulation that has received more than two note requests during the current load generation.

## Approved Loading Modes

### Adaptive

Adaptive is the global default.

- Submit only the minimal set during initial loading.
- Keep all remaining logical hits dormant.
- Activate additional dormant hits as matching notes arrive.
- Do not count dormant hits as active progress work until they are activated.

### Full

- Use the same fast initial stages as Adaptive.
- Submit every remaining logical hit after the minimal set.
- Continue to let note demand raise the priority of pending work.

Full mode changes the amount of submitted work. It must not use a separate sample cache or playback path.

## Priority Model

The queue uses this order from highest to lowest:

1. The minimal work for an exact demand target that is not ready yet.
2. One newly activated dormant logical hit for an exact demand target.
3. Pending work for a hot articulation.
4. Up to `initialRoundRobinsPerVelocity` logical hits from the highest source velocity level of each mapped articulation.
5. Up to `initialRoundRobinsPerVelocity` logical hits from every remaining source velocity level.
6. The remaining logical hits in Full mode only.

Within initial stages, use this default instrument-family order:

1. Kick
2. Snare
3. HiHat
4. Crash family, including Crash, Splash, China, Stack, and related crash-like cymbals
5. Ride
6. Tom
7. Other drums
8. Other percussion and unknown types

Demand priority is scoped to the exact runtime instrument player and articulation. It must not boost every instrument that happens to use the same library articulation name.

When several demand targets are active, dispatch at most one complete logical hit from one target before considering another target at the same priority. This prevents a dense hi-hat stream from permanently blocking kick, snare, or another active articulation.

## Initial Round Robin Selection

- `initialRoundRobinsPerVelocity` defaults to `2` and accepts values from `1` through `26`.
- `highest source velocity level` means the last available library velocity level. It does not require a filename value of `127`.
- Select logical hits in stable library order. Do not use random selection while building the queue.
- When a velocity level has fewer hits than the configured count, select all of its hits.
- Every active mic entry of one selected logical hit has the same queue priority.
- Keep a logical hit as the dispatch unit even when its files are deduplicated independently by cache key.
- This staged model replaces the current five representative early-hit markers after its playback quality is verified. Do not keep both systems as competing priority sources.

## Note Demand Behavior

For each resolved note-on:

1. Resolve the runtime articulation and exact source velocity level without changing round robin or floating-selection history.
2. Increment a bounded note counter for that runtime articulation.
3. Store the most recently requested source velocity level.
4. If its minimal set is not ready or pending at demand priority, raise that minimal work first.
5. Otherwise, activate one next dormant logical hit from that source velocity level.
6. On the third note, mark the runtime articulation hot and raise its pending work above static preload stages.
7. Play the current note only from already ready logical hits. Never wait for the demand load.

Repeated notes must coalesce. A fast note stream must not add duplicate queue records for the same logical hit or create unbounded demand events.

The demand counter resets when that instrument player is rebuilt. A time window is not required in the first implementation because the counter only affects the finite loading work of the current runtime generation.

Controller-driven hi-hat targets and aftertouch transition targets need the same bounded demand signal when they can select a different runtime articulation without a new note-on. Critical transition targets must not depend only on the raw MIDI note number.

## Real-Time Contract

The audio thread may only update preallocated lock-free demand state. It must not:

- acquire the queue mutex;
- create a `juce::String`, vector, callback, load request, or file object;
- notify through a path that can take an operating-system lock;
- scan all queued or dormant files;
- call `SampleLoader::requestAsyncLoad()`.

Each runtime articulation should own or reference a stable shared demand state with atomic fields for note count, requested velocity-level index, demand revision, and active generation. Queue-owned background work reads these fields and performs all normal queue mutation.

The queue worker can check demand state through a short timed wait. Replace the current 100 ms empty-queue sleep so new demand is normally observed within 10 ms without a callback from the audio thread.

Retired instrument players must deactivate their demand generation. Shared demand state and existing weak sound references must keep late queue work and callbacks safe after a map change.

## Queue And Cache Contract

- Represent priority and dormant state at logical-hit level.
- Keep file loading and RAM-cache identity at resolved sample-recipe level.
- One logical-hit load group contains all active linked mic file requests and their publication callbacks.
- A logical hit becomes ready only after all required active entries finish successfully.
- If a high-priority logical hit references a file that is already queued at lower priority, raise the existing unique file request and attach the new callback.
- If a requested file is already in the RAM cache, publish it through the normal asynchronous callback path.
- A cache hit must not create another read job.
- Queue cancellation and map replacement must not publish an old source recipe into a rebuilt sound. Preserve source recipe revision checks.
- Full and Adaptive modes must share cache entries when their resolved source and preprocessing recipe are equal.

The first implementation should preserve the existing `maxInFlight` limit. Queue policy and file-read concurrency are separate settings.

## Settings And Persistence

Add machine defaults under `config.yaml`:

```yaml
engine:
  sample_loading:
    mode: adaptive
    initialRoundRobinsPerVelocity: 2
```

The Conf page General section shows:

- `Default sample loading`: `Adaptive` or `Full`.
- `This project`: `Use global`, `Adaptive`, or `Full`.
- `Initial round robins`: compact integer control, default `3`.

Store the project override in the plugin instance state as:

```text
sample_loading_mode = global | adaptive | full
```

`global` is the initial value. The project does not override `initialRoundRobinsPerVelocity` in the first implementation.

Resolve the effective mode before applying the restored instrument map. Document the property in [`PLUGIN_INSTANCE_STATE_FORMAT.md`](PLUGIN_INSTANCE_STATE_FORMAT.md) when it is implemented.

For the first implementation, a changed mode applies to the next kit, preset, instrument-map, or instrument reload. It does not unload buffers from the current runtime. Live Adaptive-to-Full activation can be added later if use shows that it is necessary.

## Progress UI

- Initial Adaptive progress counts only the minimal submitted file requests.
- Dormant logical hits do not keep the pad progress indicator permanently visible.
- Activating a dormant hit registers its file requests at that time. The existing per-pad progress can then reappear and finish again.
- Full mode registers the remaining file requests when the full-load stage is submitted.
- Shared cache files can complete several logical-hit subscriptions, but each instrument player's progress state must finish its own registered callbacks.

## Benefits

- The first playable sound for each articulation needs fewer file reads.
- Large kits and multi-instance projects keep less sample data referenced by active players.
- Frequently played instruments naturally become more complete.
- Rare instruments do not consume full round-robin memory only because they are mapped.
- Full mode keeps predictable complete loading for editing, export preparation, and machines with sufficient memory.

## Costs And Risks

- The first notes can repeat only the initial round robins until demand work finishes.
- A note can still be silent when no complete logical hit for its articulation is ready. The note must never block for disk access.
- Demand loading can cause disk activity during playback.
- The current file-level queue needs a logical-hit grouping layer and dynamic priority updates.
- Cache-key deduplication must support priority promotion without losing callbacks.
- Hi-hat controller siblings, aftertouch targets, shadow players, and additional map layers increase the number of exact runtime demand targets.
- Very frequent demand needs coalescing and fair dispatch to prevent queue starvation.
- Changing from Full to Adaptive cannot reclaim buffers that the current players already own without rebuilding them.

## Complexity Assessment

| Area | Complexity | Reason |
|---|---|---|
| Global settings and Conf controls | Low | Existing YAML and compact-control patterns can be reused. |
| Plugin-state override | Low | One stable string property with 3 validated values. |
| Static `N`-RR staging | Medium | It replaces current representative-hit selection and must preserve complete mic groups. |
| Dormant logical-hit storage | Medium | Runtime load descriptions must remain valid without submitting file work. |
| Dynamic priority and duplicate promotion | Medium to high | Current queue deduplication does not change the priority of an existing file. |
| Real-time demand signal | High | Note, CC, and aftertouch paths must stay bounded and lock-free. |
| Reload and lifetime safety | High | Old runtime generations and late callbacks must not affect new players. |

This is not a small queue-order change. Implement it in separate stages so static loading remains usable after each stage.

## Implementation Stages

### Stage 1: Settings And State

- Add global mode and initial RR count parsing, serialization, defaults, accessors, and Conf controls.
- Add the `sample_loading_mode` plugin-state property and resolve the effective mode before instrument-map application.
- Update `PLUGIN_INSTANCE_STATE_FORMAT.md`.

### Stage 2: Complete-Hit Queue Model

- Add a stable runtime target identity for one instrument-player articulation.
- Submit complete logical-hit groups with file-level cache deduplication below them.
- Add priority promotion for an already queued cache key.
- Preserve callback subscription, recipe revision, readiness, and progress behavior.

### Stage 3: Static Minimal And Full Stages

- Replace representative early-hit selection with highest-level `N`, remaining-level `N`, and full-remainder stages.
- Apply the approved instrument-family order.
- Keep Adaptive and Full identical through the end of the minimal stages.

### Stage 4: Adaptive Dormant Work

- Keep non-minimal logical-hit descriptions dormant in Adaptive mode.
- Register load progress only when dormant work becomes active.
- Confirm that minimal completion hides progress and later demand makes it visible again.

### Stage 5: Real-Time Demand Priority

- Add preallocated per-runtime-articulation demand state.
- Record exact note, controller transition, and aftertouch targets without queue locks or allocation.
- Let the background worker promote minimal work, activate one extra hit, and mark the articulation hot on its third request.
- Replace the 100 ms idle polling delay with bounded low-latency demand checks.

### Stage 6: Verification And Documentation

- Update the current loading description after implementation. Keep TODO04 as the historical progressive-playback plan.
- Add queue-order, cache-deduplication, state, progress, reload, and real-time-bound tests.
- Build and run tests only when requested.
- Perform manual listening with large kick, snare, hi-hat, crash, ride, and tom libraries in both modes.

## Acceptance Checks

- Adaptive initially submits no more than `N` logical hits per available velocity level.
- The highest available source velocity level is submitted before other levels at equal demand and instrument priority.
- Full eventually submits every mapped complete logical hit.
- Adaptive does not submit a remaining dormant hit until matching demand arrives.
- One note first promotes missing minimal work. Later notes activate at most one next dormant hit for the requested source velocity level.
- The third note makes only the exact runtime instrument-player articulation hot.
- A hot hi-hat does not permanently starve another demanded articulation.
- Note, CC, and aftertouch audio paths do not lock, allocate, access files, or mutate normal queue containers.
- A high-priority duplicate raises an existing queued file request and preserves every callback.
- Every logical hit starts all required active mic entries or none.
- Current playback continues to use ready fallback and never waits for a load.
- Adaptive project state restores independently of the machine default. `Use global` follows the current machine setting.
- Dormant work does not hold initial progress below completion. Demand work makes progress visible again.
- Replacing a map while work is queued does not publish stale data or access retired runtime objects.

## Related Documents (might be outdated, rely on the code)

- [TODO04: Graceful Playback During Progressive Sample Loading](TODO04_SAMPLE_LOADING_RUNTIME_PLAN.md)
- [TODO14: Sample Calibration And Velocity Response](TODO14_SAMPLE_CALIBRATION_AND_VELOCITY_RESPONSE_PLAN.md)
- [TODO15: Floating Sample Selection](TODO15_FLOATING_SAMPLE_SELECTION_PLAN.md)
- [TODO16: Derived Sample Cache And Bake Cache](TODO16_DERIVED_SAMPLE_CACHE_AND_BAKE_CACHE_PLAN.md)
- [Plugin Instance State Format](PLUGIN_INSTANCE_STATE_FORMAT.md)
- [Project Terminology Review](PROJECT_TERMINOLOGY_REVIEW.md)
