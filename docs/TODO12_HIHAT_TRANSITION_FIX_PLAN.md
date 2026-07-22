# TODO12: Hi-Hat Transition Correctness and Performance Plan

## Status

Resolved with listening acceptance on June 14, 2026.

- Phases 1 and 2 remain complete.
- Phase 3's group-coherent transaction was rejected: it caused broken splash behavior, harsh opening transitions, ducking, and pedal-like double mutes.
- Phase 3.1 repairs did not restore the accepted sound.
- The runtime now uses the confirmed-good `e6cdaac` per-voice replacement behavior with Phase 4 CC coalescing retained.
- Shared event IDs, atomic all-mic commits, canonical reference-mic matching, transition ramps, generated CC foot layers, and group retries were removed.
- Phase 5 hides `Active Tails`, `Curve`, and `Foot Sample`; `footSampleMode` remains runtime-active for implicit audible-tail closure events, while the other two remain compatibility-only.
- Phase 6 listening acceptance passed after the full Phase 3 group solution was undone.

Phase 1 completion:

- corrected inverse decay-envelope interpolation;
- expanded exact-point, midpoint, round-trip, invalid, and incomplete-envelope tests;
- updated the stale sample-filename parser test to the current full filename contract;
- introduced lock-free atomic storage for UI-exposed engine settings;
- migrated UI, YAML, editor, and realtime consumers to explicit settings loads/stores;
- added focused `EngineSettings` tests;
- verified the full suite: 40 test cases and 323 assertions passed.

Phase 2 completion:

- added allocation-free `HiHatTransitionPolicy` helpers with non-owning `std::span` inputs;
- extracted openness selection with bidirectional hysteresis and MIDI endpoint handling;
- extracted opening/closing direction and decay matching;
- extracted decay-matched offset calculation with explicit failure statuses and missing-data fallback;
- added linear, smooth-step, and equal-power transition gain calculations;
- added independent foot-sample policy decisions;
- delegated the current controller path to the shared hysteresis, direction, and offset helpers;
- added focused policy coverage: 8 test cases and 40 assertions passed.

Rejected Phase 3 implementation history:

- stamped every voice with its runtime articulation index;
- assigned one shared event ID to all microphones started by a sampled hit;
- grouped controller transitions by that event ID and excluded non-playing voices;
- selected only the configured newest audible hit groups;
- resolved exact round-robin variant and routed-mic targets for the complete group;
- reserved every required idle voice before starting targets or fading sources;
- selected one canonical direct/overhead/room reference and applied one millisecond offset to every target mic;
- preserved the event ID and committed the target articulation on every replacement voice;
- added complementary selectable source/target transition ramps;
- added synth-level tests proving complete group commit and unchanged sources when one target mic is missing.

Confirmed Phase 3 regressions:

- opening a ringing closed hi-hat may fail to start the corresponding open sound;
- a transition with no selected group can be reported as successful and cached, preventing a later retry;
- exact variant and routed-mic requirements can reject target groups that the earlier per-voice path could transition;
- pedal and foot-splash voices are not separated from struck-tail morph transactions;
- the foot-sample policy and UI setting exist, but no runtime path currently triggers the foot sample.

Superseded Phase 3.1 repair history:

- added explicit `NoRelevantTail`, `Completed`, and `Skipped` transition results;
- retained skipped eligible transitions as bounded retries and allowed newer CC input to supersede them;
- resolved target articulations from desired openness plus the struck source zone;
- added one common loaded round-robin fallback variant for complete mic groups;
- marked pedal articulations as foot events and excluded their voices from struck-tail morphing;
- connected foot-sample modes to mapped pedal sample triggering;
- added regression coverage for delayed opening retries, zone preservation, coherent fallback, and foot-event separation;
- verified 61 test cases and 436 assertions;
- measured a 128-value controller sweep at approximately `44.6 us` mean in the Debug benchmark.

Phase 3.1 listening follow-up:

- the first listening pass still sounded effectively unchanged with the active NakedDrums mapping;
- source-zone preservation was too strict for kits or remaps without a same-zone target, so mapped-articulation fallback is now allowed;
- plain ringing `pedal` voices may again morph into an opening tail, restoring the splash-like behavior present before Phase 3;
- explicit `pedal[splash]` voices remain independent and are not reused as morph sources;
- `Tail Match = 0%` now bypasses decay-envelope matching instead of failing on missing or unmatched metadata;
- opening transitions now start the decay-matched target at its audible level and release the old tight/pedal source underneath it, avoiding the zero-gain hole introduced by complementary opening ramps;
- CC-generated foot samples no longer run normal hit culling or articulation-order choking, so they cannot re-choke the transition target they accompany;
- tests now model asymmetric zones and pedal samples without decay metadata;
- the current Debug standalone was rebuilt for listening verification;
- the VST3 artifact could not be relinked because its binary is open in a plugin host.

Phase 4 implementation:

- published sample length and usable-audio validation metadata with loaded buffers;
- activated sample-domain coalescing through the existing `controllerSmoothingSec` setting;
- retained only the latest pending CC value and processed it at the next permitted render boundary;
- added lock-free processed, coalesced, skipped, and completed transition counters;
- added controller coalescing coverage and a focused 128-value CC sweep benchmark.

Phase 5 implementation:

- TODO13 added live controls backed by lock-free realtime settings;
- YAML loading, migration, defaults, and serialization cover the transition settings;
- Phase 4 activated the persisted `controllerSmoothingSec` control in the audio-thread scheduler;
- existing configuration files retain documented defaults for missing values;
- legacy group, curve, and generated-foot settings remain parseable for configuration compatibility but are not exposed as active controls.

## Goal

Make CC-driven hi-hat openness transitions sound natural, preserve multi-mic coherence, and keep controller processing bounded and realtime-safe.

The implementation should:

- use decay metadata correctly;
- avoid replacing every historical hi-hat tail;
- preserve the timing relationship between microphones from one sampled hit;
- respond smoothly to rapid pedal movement without starting voices for every CC message;
- expose transition behavior through thread-safe realtime settings;
- remain usable when metadata, target samples, or idle voices are unavailable.

## Current Path

The relevant runtime path is:

`DrumKitEngine::processBlock()` -> `InstrumentSynth::renderNextBlock()` -> `InstrumentSynth::handleMidiEvent()` -> `InstrumentSynth::handleController()`

Primary files:

- [InstrumentSynthController.cpp](../source/Engine/InstrumentSynthController.cpp)
- [InstrumentSynth.cpp](../source/Engine/InstrumentSynth.cpp)
- [InstrumentSynth.h](../source/Engine/InstrumentSynth.h)
- [InstrumentSynthLoader.cpp](../source/Engine/InstrumentSynthLoader.cpp)
- [KitLibModel.cpp](../source/Engine/KitLibModel.cpp)
- [KitLibModel.h](../source/Engine/KitLibModel.h)
- [EngineSettings.h](../source/Engine/EngineSettings.h)
- [DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp)
- [EngineSettingsPanel.cpp](../source/UI/EngineSettingsPanel.cpp)

## Findings

### P0: Inverse Decay Lookup Returns Incorrect Offsets

`DecayEnvelope::getMsAtAbsoluteDb()` calculates a negative dB interval and passes it through `juce::jmax(0.001f, interval)`. The result becomes `0.001f`, interpolation clamps to zero, and the function returns the previous envelope breakpoint.

Observed failures:

- `-23 dB` returns `10 ms` instead of `15 ms`;
- `-29 dB` returns `20 ms` instead of `30 ms`.

Required correction:

```cpp
const float dbInterval = pointRelativeDb - previousRelativeDb;
if (std::abs(dbInterval) < 0.001f)
    return previousMs;

const float interpolation = juce::jlimit(
    0.0f,
    1.0f,
    (targetRelativeDb - previousRelativeDb) / dbInterval);
```

Do not force a signed dB denominator to be positive.

### P0: Realtime Settings Have Unsynchronized Cross-Thread Access

`EngineSettingsPanel` writes fields in `EngineSettings` on the message thread while audio processing reads the same fields. Plain `float` and `int` access is a C++ data race.

Required correction:

- introduce a thread-safe settings API before adding new live controls;
- use atomics for independent scalar settings, or publish one immutable settings snapshot;
- use relaxed atomic loads/stores for independent tuning values unless ordering with other state is required;
- keep YAML serialization and UI defaults outside the audio thread.

Recommended approach:

```cpp
struct RealtimeHiHatSettings {
    std::atomic<float> openCrossfadeSec { 0.060f };
    std::atomic<float> closeCrossfadeSec { 0.020f };
    std::atomic<float> controllerSmoothingSec { 0.015f };
    std::atomic<int> boundaryHysteresis { 2 };
};
```

If settings must change as one coherent group, publish a copied snapshot at block boundaries instead.

### P1: Partial Transitions Are Cached as Complete

When at least one replacement starts, `lastCcArticulation` is updated even if other voices in the sampled hit were skipped. This can leave a partial mic group permanently split between articulations.

Required correction:

- decide transition success per sampled-hit group, not per individual voice;
- commit the target articulation only after the complete eligible mic group starts;
- otherwise leave the group unchanged or retain a bounded retry request;
- never fade source voices until all required replacements for that group have been reserved and validated.

### P1: Every Mismatched Historical Tail Is Replaced

The controller loop considers all active mismatched voices. Old hits, already-fading voices, and multiple historical note groups can all create replacements on one pedal boundary.

Required correction:

- group voices by `noteOnTime`;
- ignore `FadingOut`, `Releasing`, and `Dying` voices;
- rank groups by recency and audible level;
- transition only the configured number of active tail groups;
- default to one newest audible group;
- apply one transition decision to the complete group.

### P1: Multi-Mic Offset Strategy Must Follow Library Timing Metadata

The original Phase 3 assumption was that one shared sample timestamp would best preserve direct/overhead/room coherence. Comparison with the confirmed-good `e6cdaac` implementation and listening feedback showed that assumption is wrong for the active library: microphones have materially different envelope peak timing, and a shared timestamp can place room targets before their useful decay or near silence.

Current correction:

- retain atomic group reservation and commit;
- calculate the current level and matched target offset from each source/target microphone envelope;
- start every target microphone in the same transaction, using its own validated decay position;
- keep exact routed-mic and common-variant selection coherent across the group.

This restores the confirmed-good pre-Phase-3 matching behavior without returning to unbounded historical-voice processing.

### P1: Matching Omits Current Voice Gain State

The current match includes decay-envelope level and velocity but not all active gain shaping.

The audible estimate should include:

- sample decay-envelope level;
- velocity gain;
- instrument volume;
- mic bleed level;
- ADSR or transition-ramp gain;
- aftertouch choke gain;
- any future per-layer gain.

Required correction:

- add a cheap `getCurrentAudibleGain()` or cached dB estimate suitable for transition matching;
- avoid starting replacements for voices already below a configurable silence threshold;
- do not let an old fading voice spawn a replacement at its un-faded envelope level.

### P2: Pedal Articulation Redirection Is Hard-Coded

Controller targets are always redirected to a matching `pedal` articulation when one exists. A pedal sample is a physical foot event and is not necessarily an appropriate replacement for a stick-hit tail.

Required correction:

- retain the struck zone and select the corresponding openness articulation for tail morphing;
- treat pedal chick/splash playback as a separate optional event;
- only trigger a foot sample on closure when the configured policy permits it;
- remove the unconditional `if (true)` policy branch.

### P2: Controller Processing Performs Avoidable Realtime Work

Current work includes:

- scanning all active voices;
- repeatedly searching target samples;
- reading a 1024-sample RMS window for each candidate;
- constructing detailed diagnostic strings;
- potentially repeating the work for several CC messages in one block.

Required correction:

- precompute target sample lookup by articulation, velocity layer, variant, and routed mic;
- precompute sample-validity data during loading;
- remove target-buffer RMS scans from `handleController()`;
- guard all diagnostic formatting with `LOG_ENABLED`;
- coalesce controller events and process the latest desired openness once per configured interval;
- bound the number of hit groups and replacement voices considered per update.

### P2: One Fade Setting Controls Different Physical Actions

`hihatPedalCrossfadeSec` is used for both pedal-driven morphing and hit-driven openness choking.

Required correction:

- separate opening morph time;
- separate closing morph/choke time;
- separate struck-hit choke time;
- keep same-zone retrigger crossfade independent.

The proposed settings are defined in [TODO13_HIHAT_TRANSITION_UI_SETTINGS_PLAN.md](TODO13_HIHAT_TRANSITION_UI_SETTINGS_PLAN.md).

## Target Runtime Model

### Continuous State

Each hi-hat synth keeps:

- latest raw pedal CC value;
- smoothed pedal position in `0.0..1.0`;
- current openness articulation;
- desired openness articulation;
- time of the last processed transition;
- a bounded list of eligible active hit groups;
- any in-progress source/target transition pair.

### Event Coalescing

On each relevant CC event:

1. Store the latest raw value.
2. Apply boundary hysteresis when selecting an articulation.
3. Do not immediately mutate voices if a transition update is already pending.
4. At the next permitted update, process only the latest desired articulation.
5. Skip work if the desired articulation and blend target did not materially change.

The audio thread remains authoritative for voice mutation.

### Hit-Group Transaction

For each eligible `noteOnTime` group:

1. Collect source voices belonging to the group.
2. Resolve the complete target sample group.
3. Choose one reference mic for group-level velocity/layer selection.
4. Calculate a decay-matched target offset for each routed microphone.
5. Validate target buffers and reserve idle voices.
6. Start all replacement voices atomically with their validated mic-specific offsets.
7. Start a complementary ramp for closing, or start the matched target immediately while releasing the source for opening.
8. Commit the group target articulation.

If steps 2-5 fail, leave the source group unchanged.

### Transition Curve

Use equal-power gain ramps by default for closing:

```text
sourceGain = cos(progress * pi / 2)
targetGain = sin(progress * pi / 2)
```

This avoids the level dip of two linear closing ramps. Opening uses an immediate decay-matched target plus a configurable source release because fading the target up from zero caused an audible duck with real cymbal tails.

### Opening and Closing

- Opening: favor a longer morph because the tail gains sustain and spectral complexity.
- Closing: favor a shorter choke-like transition.
- A closure may optionally trigger a pedal sample, but that event is independent from tail replacement.
- Do not synthesize foot-splash behavior from ordinary openness movement without a dedicated velocity/direction policy.

## Implementation Phases

### Phase 1: Correctness Foundation

Status: Complete.

1. Fix inverse decay interpolation.
2. Expand `[DecayEnvelope]` tests with exact points, midpoints, round trips, incomplete envelopes, and below-floor values.
3. Resolve or explicitly baseline the unrelated sample-filename parser failure before using a fully green suite as a gate.
4. Introduce thread-safe realtime settings access.

Exit criteria:

- all decay-envelope tests pass;
- ThreadSanitizer-compatible code has no plain cross-thread settings access;
- no UI control writes directly to audio-thread-read storage.

### Phase 2: Pure Transition Policy

Status: Complete.

Extract non-owning, allocation-free policy helpers for:

- openness selection with hysteresis;
- transition direction;
- eligible hit-group selection;
- reference-mic selection;
- decay-matched offset calculation;
- transition curve gain calculation;
- foot-sample policy.

Exit criteria:

- policy can be tested without an audio device or full plugin instance;
- MIDI boundaries `0` and `127` are covered;
- opening and closing behavior have separate tests.

### Phase 3: Group-Coherent Voice Transition

Status: Rejected and removed after listening comparison. The steps below are retained as implementation history.

1. Stamp or retain runtime articulation index directly on each voice.
2. Group active voices by `noteOnTime`.
3. Transition only eligible groups.
4. Build and validate the complete target group before mutating source voices.
5. Apply one validated decay-matched offset per target mic.
6. Commit articulation state per group.

Exit criteria:

- a transition cannot partially replace a mic group;
- no fading/dying voice creates another replacement;
- direct/overhead/room targets do not land in pre-peak or near-silent regions.
- closed-to-open and open-to-closed transitions remain functional;
- pedal and foot-splash playback remain functional.

### Phase 3.1: Regression Repair

Status: Superseded. These repairs did not restore the accepted sound and were removed with the Phase 3 transaction model.

This phase attempted to restore the musical behavior while retaining shared event IDs and atomic mic-group commits. Listening showed that premise was unsuccessful.

1. Correct transition result semantics.
   - Distinguish `no relevant active tail`, `transition completed`, and `transition skipped`.
   - Cache `lastCcArticulation` only when a group completed or when no relevant active tail exists.
   - Keep a bounded pending request when an eligible group exists but cannot currently transition.
2. Restore closed-to-open tail transitions.
   - Reproduce the failure with a controller-level test.
   - Verify the selected source group, target articulation, velocity layer, offset, and voice reservation independently.
   - Ensure a skipped early request can retry after the attack guard or smoothing interval.
3. Preserve the struck zone during openness changes.
   - Morph `edge` tails through `edge` openness articulations and `bow` tails through `bow` openness articulations.
   - Do not redirect a struck tail to `pedal`, chick, or foot-splash articulations.
4. Add coherent target fallback.
   - Prefer exact variant and routed-mic matches.
   - When an exact round-robin variant is unavailable, select one common loaded fallback variant for the complete mic group.
   - Never fall back independently per microphone or commit a partial group.
5. Separate foot voices from tail morphing.
   - Exclude pedal, chick, and foot-splash events from struck-tail group selection.
   - Pedal voices may choke or finish according to their own event policy but must not become openness-morph sources.
6. Restore independent foot-sample triggering.
   - Use `hihatFootSampleMode` and `HiHatTransitionPolicy::shouldTriggerFootSample()`.
   - Trigger the mapped pedal/chick sample on qualifying closure without replacing the struck tail with that articulation.
   - Restore existing foot-splash playback paths; advanced splash-gesture inference remains out of scope.

Exit criteria:

- a ringing closed `edge` or `bow` hit transitions to its corresponding open articulation;
- an initially skipped transition can retry instead of being cached as complete;
- exact target matching remains preferred, but a coherent common fallback variant can transition the full group;
- pedal/chick and foot-splash voices are never used as struck-tail morph sources;
- foot-sample modes were evaluated during the rejected group-transition implementation;
- regression tests fail on the current Phase 3 behavior and pass after the repair;
- all Phase 3 group-coherence tests continue to pass.

Automated verification:

- focused `[InstrumentSynth][HiHatTransition]`: 11 test cases and 44 assertions passed;
- full suite: 64 test cases and 443 assertions passed;
- focused Debug benchmark remains bounded after the follow-up changes.

### Phase 4: Realtime Work Reduction

Status: Partially retained. Controller coalescing, bounded scheduling, guarded logging, and counters remain; group lookup and load-time transition tables were removed.

1. Precompute target entry lookup tables during load/rebuild.
2. Replace target-buffer RMS scans with load-time validation metadata.
3. Coalesce CC updates.
4. Bound active tail groups.
5. Guard verbose formatting.
6. Add counters for processed, coalesced, skipped, and completed transitions without formatting strings on the audio thread.

Original exit criteria:

- no sample-window scan occurs in `handleController()`;
- no heap allocation occurs during a controller transition;
- worst-case work is bounded by configured active groups and mic count.
- Phase 3.1 retries, fallback selection, and foot triggering remain bounded and allocation-free.

Only controller coalescing, guarded logging, bounded scheduling, and counters remain applicable. The bounded 1024-sample validation scan is retained as known performance debt because removing it changed the accepted sound.

Revalidation:

- focused `[InstrumentSynth][HiHatTransition]`: 11 test cases and 50 assertions passed;
- full suite: 64 test cases and 449 assertions passed;
- Debug 128-value controller sweep: approximately `49.6 us` mean;
- Debug standalone rebuilt successfully for listening;
- VST3 relink was blocked by the plugin binary being open in a host.

### Phase 5: UI and Persistence

Status: Complete for controls that affect the accepted runtime. Inactive Phase 3 controls remain YAML-compatible but are hidden from the UI.

Implement the settings contract in [TODO13_HIHAT_TRANSITION_UI_SETTINGS_PLAN.md](TODO13_HIHAT_TRANSITION_UI_SETTINGS_PLAN.md).

Exit criteria:

- changes are audible without restarting;
- values persist to YAML;
- old configuration files load with documented defaults;
- UI changes are race-free.
- every exposed setting has implemented runtime behavior;
- foot-sample mode changes are audible and follow the documented policy.

Automated verification:

- all transition values survive YAML serialize/parse round trip;
- legacy migration, range clamping, and invalid enum fallback remain covered;
- foot modes `Off`, `Close Only`, and `Always` are covered by policy and synth-level tests;
- UI controls read/write lock-free settings and retain debounced message-thread persistence.

### Phase 6: Listening and Performance Validation

Status: Complete. Listening was accepted on June 14, 2026 after restoring per-voice replacement behavior and retaining the Phase 4 controller scheduler.

Test:

- open -> half -> closed and reverse transitions;
- slow sweeps and fast pedal stomps;
- early attack, middle decay, and late tail transitions;
- low and high velocities;
- direct-only and full multi-mic layouts;
- missing decay metadata;
- partially loaded samples;
- voice-pool pressure;
- foot-sample modes.

Acceptance criteria:

- no attack transient appears when morphing an existing tail;
- no obvious level hole at the crossfade midpoint;
- no stereo image or room-delay jump;
- rapid CC input does not multiply voices without bound;
- closure remains responsive at the default settings;
- CPU usage does not spike materially during dense pedal input.

Automated results:

- full suite: 52 test cases and 407 assertions passed after final dead-policy cleanup;
- production-path tests cover independent event IDs, per-voice replacement, missing targets, cache behavior, and CC coalescing;
- Debug 128-value raw CC sweep: approximately `54.7 us` mean;
- Debug 128-message coalesced burst: approximately `11.8 us` mean.

Listening gate:

- accepted on June 14, 2026 after restoring the pre-Phase-3 per-voice transition behavior;
- foot-splash and tight-to-open behavior were confirmed fixed;
- future group-coherent transition work requires a separate listening comparison before replacing this path.

### ✅ Post-Completion Silent Multi-Mic Closing Fallback

Added on July 22, 2026 without replacing the accepted per-voice path:

- each mic first uses its own decay-matched target offset;
- when that target region is silent during closing, the mic tries the Direct offset, then the Overhead offset;
- each reference offset is accepted only when the current target mic has audible content there;
- when the own, Direct, and Overhead regions are all silent, the old open mic voice receives the normal short closing fade instead of continuing to ring;
- focused tests cover Direct fallback for Overhead and Room, Overhead fallback when Direct is absent, and the final fade fallback.

## Tests to Add

Suggested files:

- `tests/TestDecayEnvelope.cpp`
- `tests/TestHiHatTransitionPolicy.cpp`
- `tests/TestInstrumentSynthController.cpp`

Required cases:

1. Decay lookup and inverse lookup round-trip.
2. Articulation boundary hysteresis in both directions.
3. CC coalescing retains the latest value.
4. Independent active voices can transition without sharing an event ID.
5. A missing target leaves the source voice playing and does not cache the request as complete.
6. Opening and closing use different source release durations.
7. Missing metadata follows the configured fallback.
8. CC coalescing retains and eventually applies the latest value.

## Performance Measurement

Add a benchmark that creates a representative hi-hat voice set and processes:

- one articulation boundary;
- a sweep of 128 CC values;
- several CC events within one audio block;
- one, two, and four active tail groups;
- direct, overhead, and room mic groups.

Record:

- transition processing time;
- maximum voices started;
- coalesced event count;
- allocations;
- target lookup count.

## File Change Map

| File | Planned responsibility |
|---|---|
| `source/Engine/KitLibModel.cpp` | Correct inverse decay interpolation |
| `source/Engine/EngineSettings.h` | Thread-safe realtime settings and new transition values |
| `source/Engine/InstrumentSynth.h` | Hit-group transition state and precomputed lookup structures |
| `source/Engine/InstrumentSynthLoader.cpp` | Build target lookup and validation metadata |
| `source/Engine/InstrumentSynthController.cpp` | Coalescing, policy, group transaction, and foot-event separation |
| `source/Engine/InstrumentSynthHiHatTransition.cpp` | Group transition results, zone-preserving target resolution, coherent fallback, and retry behavior |
| `source/Engine/InstrumentSynth.cpp` | Transition gain ramp rendering and audible-level cache |
| `source/Engine/DrumKitEngine.cpp` | YAML load/save and settings publication |
| `source/UI/EngineSettingsPanel.cpp` | Live controls through the thread-safe settings API |
| `tests/` | Decay, policy, controller, and group-coherence tests |
| `benchmarks/Benchmarks.cpp` | Controller-transition performance coverage |

## Out of Scope

- Physical modeling of continuous cymbal aperture.
- Time-stretching or spectral morphing between recordings.
- Automatic generation of missing openness articulations.
- New foot-splash gesture or velocity inference beyond restoring existing mapped playback behavior.
- Changes to sample filename metadata encoding.
