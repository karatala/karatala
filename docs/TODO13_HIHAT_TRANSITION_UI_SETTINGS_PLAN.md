# ✅ COMPLETED: TODO13: Realtime Hi-Hat Transition UI Settings Plan (Some settings are hidden by design, not incomplete.)

## Status

Implemented, then revised after listening acceptance on June 14, 2026.

Active engine controls:

- separate opening and closing crossfade times;
- boundary hysteresis;
- tail-match amount and bias;
- attack guard;
- minimum remaining tail;
- silence floor;
- missing-decay fallback policy;
- renamed struck-hit choke margin.

Runtime-complete controls:

- controller smoothing drives deferred, sample-domain transition scheduling;
- opening and closing times release each source voice while its decay-matched target starts directly.
- foot-sample mode controls implicit pedal events when an audible open/half tail reaches the most-closed state.

Persisted settings hidden from the UI:

- active-tail group limit;
- transition curve;
- foot-sample mode.

## Goal

Add understandable realtime controls for tuning how existing hi-hat tails move between openness articulations.

The controls must:

- update live without restarting or reloading the instrument;
- remain safe while the audio thread is rendering;
- have useful defaults that do not require technical knowledge;
- persist in `config.yaml`;
- separate opening, closing, struck-hit choking, and foot-sample behavior;
- avoid exposing implementation-only thresholds unless they provide audible value.

## UI Location

Use the existing `Hi-Hat / Polyphony` section in `EngineSettingsPanel`.

Split it visually into:

- `Transitions` for the primary musical controls;
- `Advanced` for matching, resource limits, and fallback policy.

Keep existing unrelated controls:

- `Poly Xfade` remains the same-zone retrigger crossfade;
- `Cross-Art dB` remains hit-driven cross-articulation choke sensitivity, but should be renamed for clarity.

## Primary Controls

| UI label | Setting identifier | Range | Default | Display | Meaning |
|---|---|---:|---:|---|---|
| Open Xfade | `hihatOpenCrossfadeSec` | 0.005-0.250 s | 0.060 s | ms | Time used when moving toward a more open articulation |
| Close Xfade | `hihatCloseCrossfadeSec` | 0.002-0.150 s | 0.020 s | ms | Time used when moving toward a more closed articulation |
| CC Smooth | `hihatControllerSmoothingSec` | 0-0.100 s | 0.015 s | ms | Coalescing/smoothing interval for rapid pedal CC input |
| Hysteresis | `hihatBoundaryHysteresisCc` | 0-8 | 2 | CC units | Extra movement required before crossing back over an articulation boundary |
| Tail Match | `hihatTailMatchAmount` | 0-1 | 0.75 | percent | Strength of decay-envelope offset matching |
| Match Bias | `hihatTailMatchBiasDb` | -12 to +6 dB | -3 dB | dB | Starts the target slightly quieter or louder than an exact envelope match |

### Primary Tooltips

`Open Xfade`

> Transition time when opening the hi-hat while a hit is ringing. Higher values are smoother; lower values react faster.

`Close Xfade`

> Transition time when closing the hi-hat while a hit is ringing. Lower values produce a tighter choke.

`CC Smooth`

> Combines rapid pedal messages before changing the ringing tail. Higher values reduce transition activity but add pedal latency.

`Hysteresis`

> Prevents openness from flickering near articulation boundaries. Increase this if the pedal jitters between adjacent sounds.

`Tail Match`

> How strongly the replacement sample follows the current tail loudness. Zero uses a simple fallback; 100% uses full decay-envelope matching.

`Match Bias`

> Loudness offset applied to matched replacement tails. Negative values make transitions more restrained.

## Advanced Controls

| UI label | Setting identifier | Range/options | Default | Meaning |
|---|---|---|---|---|
| Attack Guard | `hihatTransitionAttackGuardSec` | 0-0.100 s | 0.025 s | Avoid morphing a newly struck hit during its attack |
| Min Remaining | `hihatPedalMinRemainingSec` | 0.010-0.500 s | 0.050 s | Minimum usable target tail required to start a replacement |
| Silence Floor | `hihatTransitionSilenceFloorDb` | -90 to -36 dB | -72 dB | Groups below this level are not transitioned |
| Missing Data | `hihatMissingDecayPolicy` | Keep Tail, Simple Fade | Keep Tail | Behavior when source or target decay metadata is unavailable |

### Advanced Tooltips

`Attack Guard`

> Protects the initial stick attack from being replaced by pedal movement.

`Min Remaining`

> Skips replacement samples that do not have enough audio left after matching the current decay level.

`Silence Floor`

> Stops very quiet historical tails from creating new replacement voices.

`Missing Data`

> Keeps the original tail unchanged or uses a simple crossfade when decay metadata is unavailable.

## Existing Control Changes

### Rename

| Current label | New label | Existing setting |
|---|---|---|
| `Pdl Xfade` | Remove after migration | `hihatPedalCrossfadeSec` |
| `Pdl Min` | `Min Remaining` | `hihatPedalMinRemainingSec` |
| `Cross-Art dB` | `Hit Choke dB` | `hihatCrossArtChokeDbMargin` |

`hihatPedalCrossfadeSec` should migrate into separate opening and closing defaults. It should not remain the runtime source for both behaviors.

### Preserve

`Poly Xfade` remains separate because same-zone note retriggering is not pedal morphing.

## Recommended Initial Layout

Primary row:

1. `Open Xfade`
2. `Close Xfade`
3. `CC Smooth`
4. `Hysteresis`
5. `Tail Match`
6. `Match Bias`

Secondary row:

1. `Poly Xfade`
2. `Hit Choke dB`
3. `Attack Guard`

Advanced expandable row:

1. `Min Remaining`
2. `Silence Floor`
3. `Missing Data`

If the panel does not yet support expandable groups, include the advanced controls in the normal pot grid and preserve the ordering above.

## Control Types

Use:

- `ParameterPot` for seconds, dB, and percentages;
- `ComboBox` for missing-data policy;
- millisecond formatting for time values below one second;
- percentage formatting for `Tail Match`;
- integer formatting for `Hysteresis`.

The panel's layout counts and pot indices must be derived from group data or updated together with constructor ordering. Avoid another set of unrelated hard-coded indices if the panel is being refactored.

## Thread-Safe Runtime Contract

The panel must not retain raw pointers to plain fields read by the audio thread.

Preferred API shape:

```cpp
enum class HiHatTransitionCurve {
    Linear,
    Smooth,
    EqualPower
};

enum class HiHatFootSampleMode {
    Off,
    CloseOnly,
    Always
};

enum class HiHatMissingDecayPolicy {
    KeepTail,
    SimpleFade
};

class DrumKitEngine {
public:
    HiHatTransitionSettings getHiHatTransitionSettings() const;
    void setHiHatTransitionSettings(const HiHatTransitionSettings& settings);
};
```

The message thread edits a local copy and publishes it through the engine API. The audio thread loads atomics or consumes the latest immutable snapshot at a block boundary.

UI callbacks must:

1. read the latest settings snapshot;
2. change only the selected value;
3. clamp it to the documented range;
4. publish the updated snapshot;
5. queue the existing debounced YAML save.

## Engine Semantics

### Open Xfade

Used as the release time for the previous tail when the target `pocValue` represents a more open state. The decay-matched target starts directly instead of fading up from zero.

### Close Xfade

Used when the target represents a more closed state. It may be shorter than the opening time, but must still use a click-free ramp.

### CC Smooth

Defines the minimum processing interval for tail-transition decisions, not MIDI input capture. The latest raw CC value is always retained.

At `0 ms`, process at most once per audio sub-block. Never process multiple complete transitions for the same synth at the same sample position.

### Hysteresis

Apply hysteresis around the selected articulation boundary:

- moving upward must exceed `boundary + hysteresis`;
- moving downward must fall below `boundary - hysteresis`.

Clamp boundaries to MIDI `0..127`.

### Tail Match

Blend between a simple fallback offset and the fully envelope-matched offset:

```text
finalOffsetMs =
    fallbackOffsetMs
    + matchAmount * (matchedOffsetMs - fallbackOffsetMs)
```

The fallback should skip the attack and start no earlier than the target peak. It must not use the old fixed 50 ms behavior unconditionally.

### Match Bias

Apply bias to the desired target level before inverse lookup:

```text
desiredTargetDb = currentAudibleDb + matchBiasDb
```

A default of `-3 dB` makes the introduced target tail slightly quieter and reduces perceived bumps.

### Attack Guard

Do not morph a voice while it is younger than the configured duration. Continue tracking desired openness so the latest state can be applied after the guard expires.

### Hidden Persisted Settings

`maxActiveGroups`, `curve`, and `footSampleMode` remain readable and writable but are not exposed in the UI. `maxActiveGroups` and `curve` are compatibility-only. `footSampleMode = off` disables implicit pedal events; enabled modes allow one only when an audible open/half tail reaches the most-closed state. Event velocity follows the tail's remaining estimated loudness, and silence or tight-only playback does not trigger it.

### Missing Data

- Keep Tail: do not replace the current hit group.
- Simple Fade: use a bounded target offset and configured crossfade without envelope matching.

`Keep Tail` is the safer default.

## YAML Contract

Store values under:

```yaml
engine:
  hihat:
    polyphonyCrossfadeTimeoutSec: 0.3
    hitChokeDbMargin: 3.0
    transition:
      openCrossfadeSec: 0.06
      closeCrossfadeSec: 0.02
      controllerSmoothingSec: 0.015
      boundaryHysteresisCc: 2
      tailMatchAmount: 0.75
      tailMatchBiasDb: -3.0
      attackGuardSec: 0.025
      maxActiveGroups: 1
      curve: equal_power
      footSampleMode: close_only
      minRemainingSec: 0.05
      silenceFloorDb: -72.0
      missingDecayPolicy: keep_tail
```

Accepted enum strings:

- curve: `linear`, `smooth`, `equal_power`;
- foot sample: `off`, `close_only`, `always`;
- missing decay: `keep_tail`, `simple_fade`.

Unknown enum values should produce a warning and use the default.

## Configuration Migration

For existing configurations:

- if `pedalCrossfadeSec` exists and new transition times do not, use it as `openCrossfadeSec`;
- derive `closeCrossfadeSec` as `min(pedalCrossfadeSec, 0.030)`;
- map `crossArtChokeDbMargin` to `hitChokeDbMargin`;
- map `pedalMinRemainingSec` to `transition.minRemainingSec`;
- write only the new keys on the next save.

Do not silently overwrite explicitly configured new keys.

## Default Rationale

- `Open Xfade = 60 ms`: smooth enough to hide timbral changes without feeling disconnected.
- `Close Xfade = 20 ms`: responsive closure with a short click-free choke.
- `CC Smooth = 15 ms`: limits work to roughly 67 transition decisions per second during dense CC input.
- `Hysteresis = 2`: filters common pedal jitter without materially shrinking articulation ranges.
- `Tail Match = 75%`: uses envelope metadata while avoiding over-precise jumps to very late sample offsets.
- `Match Bias = -3 dB`: reduces replacement level bumps.
- `Attack Guard = 25 ms`: protects the main stick transient.
The active defaults were accepted by listening on June 14, 2026 with the restored per-voice runtime. Legacy hidden values are preserved only for configuration compatibility.

## Validation

### Functional

1. Every control updates the current plugin instance immediately.
2. Values survive save/restart.
3. Old YAML keys migrate correctly.
4. Invalid YAML values clamp or fall back with a warning.
5. Reset buttons restore documented defaults.
6. Combo boxes display the persisted enum choice.

### Realtime Safety

1. No control writes plain audio-thread-read fields.
2. No lock is acquired by the audio thread to read a setting.
3. Dragging controls continuously does not allocate or serialize YAML on the audio thread.
4. YAML saves remain debounced on the message thread.

### Listening

1. Opening is smooth at 20, 60, and 150 ms.
2. Closing remains responsive at 5, 20, and 80 ms.
3. Hysteresis prevents boundary chatter.
4. Tail Match and Match Bias produce audible but controlled differences.
5. Foot-splash and tight-to-open transitions retain the accepted behavior.

### UI

1. Labels and values remain readable at the panel's minimum width.
2. Added rows calculate scroll height correctly.
3. Tooltips explain musical results rather than implementation details.
4. Keyboard focus and reset controls remain usable.

## Implementation Order

1. Add thread-safe settings storage and engine setter/getter API.
2. Add YAML parsing, migration, defaults, and serialization.
3. Wire transition policy to the new settings.
4. Add primary controls.
5. Add enum controls and advanced settings.
6. Update panel layout calculations.
7. Add settings/persistence tests.
8. Perform listening validation and tune defaults. Completed for the restored per-voice runtime on June 14, 2026.

## Out of Scope

- Per-instrument overrides in library metadata.
- DAW automation or public plugin parameters for every transition setting.
- Graphical openness curves.
- Pedal calibration and inversion controls.
- Foot-splash gesture recognition.
