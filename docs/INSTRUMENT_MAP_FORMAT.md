# Instrument Map Format

## Purpose

The instrument map tells Karatala which library instrument and library articulation each kit-layout slot plays.

Each non-empty, non-comment line is one map layer. It maps one required articulation slot to one required library articulation.

## Basic Line Shape

```text
instrumentSlot/articulationSlot:libraryKit/libraryInstrument/libraryArticulation[:params]
```

Simple examples:

```text
kick1/center:MyKit/kick-24/center
snare1/center:MyKit/snare-14/center
hihat1/bow[open]:KitA/hihat-14/bow[open]:tuning=-2;attack=50;decay=300
```

Parts:

- `instrumentSlot` is the kit-layout instrument-slot ID, for example `kick1`, `snare1`, `hihat1`, or `ride1`.
- `articulationSlot` is required. It is the kit-layout articulation-slot code, for example `center`, `rim`, `bow`, `edge`, or `bell`.
- `libraryKit` is the complete library-kit identity.
- `libraryInstrument` is the complete library-instrument identity inside that library kit.
- `libraryArticulation` is required. It is the library-articulation identity inside that library instrument.
- `params` are optional map-layer settings.

## Articulation Slot And Library Articulation

The articulation slot is what the e-drum kit triggers. The library articulation is the recorded content that Karatala plays.

Matching articulation-slot and library-articulation codes:

```text
snare1/rim:MyKit/snare-14/rim
```

Different articulation-slot and library-articulation codes:

```text
tom2/rim:AasiMonster/tom-X2/center
```

This line plays library articulation `center` from `AasiMonster/tom-X2` when articulation slot `tom2/rim` is triggered.

## Map Layers

If more than one line maps the same instrument slot and articulation slot, those lines are map layers.

```text
snare1/rim:NakedDrums/snare-X1/center
snare1/rim:NakedDrums/snare-X2/center:tuning=60
```

Both map layers are used for `snare1/rim`.

Map-layer order is the line order in the normalized map for that instrument slot and articulation slot. UI rows must keep this order.

## Whole-Instrument Selection

When the user selects one complete library instrument:

- remove all existing map layers for the selected instrument slot;
- create one map layer for each library articulation;
- use the matching articulation slot when an exact match is available;
- otherwise use the first available articulation slot from the configured fallback order for the instrument type and show a warning;
- skip the library articulation with a warning when no configured fallback slot exists in the kit layout;
- preserve every map layer for all other instrument slots.

Built-in fallback orders can be replaced per instrument type in `config.yaml`:

```yaml
articulationFallback:
  kick: [center, head]
  snare: [center, rim, sidestick]
  tom: [center, rim]
  hihat: [bow, edge, pedal]
  crash: [edge, bow, bell]
  splash: [edge, bow, bell]
  china: [edge, bow, bell]
  ride: [bow, edge, bell]
```

The first available articulation slot wins. A user list replaces the built-in list for that type. An empty list disables fallback for that type. Exact matches do not use fallback and do not produce a warning.

## Current Params

| Param | Example | Meaning | Current apply behavior |
| --- | --- | --- | --- |
| `tuning` | `tuning=-20` | cents, negative or positive | live for the existing map layer |
| `attack` | `attack=25` | attack in ms, `-1` means default | live for the existing map layer |
| `decay` | `decay=250` | decay in ms, `-1` means default | live for the existing map layer |
| `vol` | `vol=-3.5` | map-layer gain in dB | live for the existing map layer |
| `pan` | `pan=-40` | -127 left to +127 right | live for the existing map layer |
| `velocityResponse` | `velocityResponse=logarithmic` | map-layer MIDI velocity to output dB override | live in the pad popup without reloading samples |
| `flipLR` | `flipLR=1` | swap stereo left/right | live for the existing map layer |
| `flipPhase` | `flipPhase=1` | invert phase | live for the existing map layer |
| `bleed` | `bleed=oh:0.25,room:0.5` | mic gain trims from 0.0 to 1.0 | live for the existing map layer |
| `micRoute` | `micRoute=dtm1:snare1/dtm,ons:overhead/ons` | send a mic to one exact output route | live in the pad popup; raw map apply reloads the instrument slot |

Use `micRoute=dtm1:-` to leave mic `dtm1` unassigned from every output.

If `velocityResponse` is absent, the map layer uses the global response. A map-layer override can use the stable built-in code `linear`, `logarithmic`, or `exponential`, or a comma-separated list of increasing `velocity:dB` points. A point list must start at velocity 1, end at velocity 127 and `0 dB`, never decrease, and never exceed `0 dB`. It changes output gain only. Exact sample selection still follows the sample filename velocity boundaries.

Built-in gains use normalized velocity `x = midiVelocity / 127`:

- `linear`: `x`
- `logarithmic`: `ln(1 + (e - 1) * x)`
- `exponential`: `(e^x - 1) / (e - 1)`

User preset names are not stored in the instrument map. Applying a user preset copies its validated response definition into `velocityResponse`, so deleting or renaming the preset cannot change an existing session.

```text
ride1/bow:KitA/ride-20/bow
```

## Param Separators

Top-level params are separated by semicolon.

This is fine when every value is simple:

```text
ride1/bow:KitA/ride-20/bow:tuning=-2;attack=50;decay=300
```

Some values contain commas. `bleed=` and `micRoute=` are the important examples:

```text
kick1/center:MyKit/kick-24/center:bleed=dtm:0.5,room:0.8
```

When another top-level param follows a comma-list value, use semicolon:

```text
kick1/center:MyKit/kick-24/center:bleed=dtm:0.5,room:0.8;tuning=-20
```

Commas are not top-level separators. They stay inside list values like `bleed=` and `micRoute=`.

## Hihat Notes

Hihat maps usually need an articulation-slot zone and expression state.

Common articulation-slot examples:

```text
hihat1/bow[open]:MyKit/hihat-14/bow[open]
hihat1/bow[tight]:MyKit/hihat-14/bow[tight]
hihat1/edge[open]:MyKit/hihat-14/edge[open]
hihat1/pedal:MyKit/hihat-14/pedal
```

Practical model:

- `bow` and `edge` are zones.
- `[open]`, `[half]`, `[close]`, and `[tight]` are expression states.
- MIDI note 46 often means open bow.
- MIDI note 42 often means tight bow.
- CC4 can move between open, half, close, and tight states while a hihat voice is ringing.

Some libraries have more library-articulation states, for example:

```text
bow[open]-pdl_opn_cls013
bow[half]-pdl_opn_cls049
bow[tight]-pdl_opn_cls085
```

The map must keep the articulation-slot intent clear. Example:

```text
hihat1/bow[open]:TchackpoumDG1.3/hihat-14/bow[open]
hihat1/edge[open]:TchackpoumDG1.3/hihat-14/edge[open]
hihat1/pedal:TchackpoumDG1.3/hihat-14/pedal
```

## Mic Routing Param

Use this when one mic must go to a different output than automatic routing chooses.

Param:

```text
micRoute=micCode:outputId,micCode:outputId
```

Example:

```text
snare1/center:MyKit/snare-14/center:micRoute=dtm1:snare1/dtm,dtm2:aux1/**m
```

Meaning:

- `dtm1` and `dtm2` are separate mic codes.
- `snare1/dtm` is the stable ID of `Snr1 Top` in the default mic layout.
- `aux1/**m` is the stable ID of the mono `Aux1` output.
- `micRoute=dtm1:-` leaves mic `dtm1` unassigned.

Pad popup routing changes update loaded sample destinations in place. Applying a changed raw map still rebuilds the affected slot.
When a popup mic code is an output-layout alias instead of the library's original mic code, full map loading resolves that alias through the mic's automatic output assignment.
Mic-code variants stay independent. For example, `dtm1` and `dtm2` have separate popup controls and can use `micRoute=dtm1:snare1/dtm,dtm2:-`. A generic layout output code such as `dtm` accepts both variants. A variant output code such as `dtm1` accepts only that variant. The popup shows output names, but saves each mic-layout output's unique ID. This keeps repeated output codes separate and prevents layout reordering from changing saved routes.

Only mic-layout output `code` values can use `*`, independently in the type and position characters. Mic codes and sample filenames never use `*`. The format stays exact. A present variant must match, while an omitted variant accepts all variants:

- `**m` accepts any mono mic, including numbered variants.
- `**s` accepts any stereo mic.
- `**i` accepts any mid-side mic.
- `*tm` accepts any top-position mono mic.
- `d*m` accepts any direct mono mic position.
- `**m1` accepts any mono mic whose variant is `1`.

## Raw Map Apply

Raw map apply should use diff apply by default.

A full rebuild is still useful as an explicit recovery or debug action, but it must be a separate button or option. Normal edits must rebuild only the affected instrument slot and map layers when possible.

## Current References

Current implementation and tests:

- `../source/Engine/DrumKitEngineMappingModel.cpp`
- `../source/Engine/DrumKitEngine.cpp`
- `../tests/TestInstrumentMap.cpp`

Related plans:

- `TODO11_PAD_POPUP_LAYER_PARAMETERS_PLAN.md`
- `PLAN-bleed-routing-combobox.md`
- `PLAN-instrument-map-format-and-routing-docs.md`
