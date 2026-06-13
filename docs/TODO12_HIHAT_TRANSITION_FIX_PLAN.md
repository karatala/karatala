# TODO12: Hi-Hat Transition Correctness and Performance Plan

## Status

In progress. Phases 1 and 2 were completed on June 13, 2026. Phases 3-6 remain proposed.

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
- extracted opening/closing direction, bounded eligible-group ranking, and canonical reference-mic selection;
- extracted decay-matched offset calculation with explicit failure statuses and missing-data fallback;
- added linear, smooth-step, and equal-power transition gain calculations;
- added independent foot-sample policy decisions;
- delegated the current controller path to the shared hysteresis, direction, and offset helpers;
- added focused policy coverage: 8 test cases and 40 assertions passed.

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

### P1: Independent Per-Mic Offsets Break Multi-Mic Coherence

Calculating a separate envelope-matched offset for each mic can change direct/overhead/room timing and phase relationships.

Required correction:

- select one canonical reference voice per hit group;
- prefer a direct mic, then overhead, then room;
- compute one source level and one target offset from that reference;
- apply the same source-time offset to every replacement in the group;
- preserve any existing per-mic sample alignment metadata rather than deriving new offsets from each mic envelope.

Per-mic envelope data may validate target availability, but it must not independently retime the recording.

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
3. Choose one reference mic.
4. Calculate one decay-matched target offset.
5. Validate target buffers and reserve idle voices.
6. Start all replacement voices with the shared offset.
7. Start complementary source/target gain ramps.
8. Commit the group target articulation.

If steps 2-5 fail, leave the source group unchanged.

### Transition Curve

Use equal-power gain ramps by default:

```text
sourceGain = cos(progress * pi / 2)
targetGain = sin(progress * pi / 2)
```

This avoids the level dip of two linear ramps. Linear and smooth-step curves may remain selectable for tuning.

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

1. Stamp or retain runtime articulation index directly on each voice.
2. Group active voices by `noteOnTime`.
3. Transition only eligible groups.
4. Build and validate the complete target group before mutating source voices.
5. Apply one canonical offset to all target mics.
6. Commit articulation state per group.

Exit criteria:

- a transition cannot partially replace a mic group;
- no fading/dying voice creates another replacement;
- room/direct timing relationships remain unchanged.

### Phase 4: Realtime Work Reduction

1. Precompute target entry lookup tables during load/rebuild.
2. Replace target-buffer RMS scans with load-time validation metadata.
3. Coalesce CC updates.
4. Bound active tail groups.
5. Guard verbose formatting.
6. Add counters for processed, coalesced, skipped, and completed transitions without formatting strings on the audio thread.

Exit criteria:

- no sample-window scan occurs in `handleController()`;
- no heap allocation occurs during a controller transition;
- worst-case work is bounded by configured active groups and mic count.

### Phase 5: UI and Persistence

Implement the settings contract in [TODO13_HIHAT_TRANSITION_UI_SETTINGS_PLAN.md](TODO13_HIHAT_TRANSITION_UI_SETTINGS_PLAN.md).

Exit criteria:

- changes are audible without restarting;
- values persist to YAML;
- old configuration files load with documented defaults;
- UI changes are race-free.

### Phase 6: Listening and Performance Validation

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

## Tests to Add

Suggested files:

- `tests/TestDecayEnvelope.cpp`
- `tests/TestHiHatTransitionPolicy.cpp`
- `tests/TestInstrumentSynthController.cpp`

Required cases:

1. Decay lookup and inverse lookup round-trip.
2. Articulation boundary hysteresis in both directions.
3. CC coalescing retains the latest value.
4. Only the newest configured hit groups are selected.
5. Fading and dying groups are excluded.
6. Complete mic-group reservation succeeds atomically.
7. Partial target availability leaves the source group unchanged.
8. Canonical offset is shared across direct, overhead, and room voices.
9. Equal-power midpoint gains are approximately `0.707`.
10. Opening and closing use different durations.
11. Missing metadata follows the configured fallback.
12. Foot sample policy does not replace a struck tail with `pedal`.

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
| `source/Engine/InstrumentSynth.cpp` | Transition gain ramp rendering and audible-level cache |
| `source/Engine/DrumKitEngine.cpp` | YAML load/save and settings publication |
| `source/UI/EngineSettingsPanel.cpp` | Live controls through the thread-safe settings API |
| `tests/` | Decay, policy, controller, and group-coherence tests |
| `benchmarks/Benchmarks.cpp` | Controller-transition performance coverage |

## Out of Scope

- Physical modeling of continuous cymbal aperture.
- Time-stretching or spectral morphing between recordings.
- Automatic generation of missing openness articulations.
- Foot-splash velocity inference beyond a separately approved policy.
- Changes to sample filename metadata encoding.
