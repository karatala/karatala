# Instrument Map Format

## Purpose

The instrument map tells Karatala which library instrument and articulation each kit slot should play.

Each non-empty, non-comment line maps one slot or one slot articulation to one source. Multiple matching lines become layers.

## Basic Line Shape

```text
slot/slotArt:kit/instrument/sourceArt[:params]
```

Simple examples:

```text
kick1/center:MyKit/kick-24/center
snare1/center:MyKit/snare-14/center
hihat1/bow[open]:KitA/hihat-14/bow[open]:tuning=-2;attack=50;decay=300
```

Parts:

- `slot` is the kit layout slot id, for example `kick1`, `snare1`, `hihat1`, `ride1`, ...
- `slotArt` is the destination pad articulation, for example `center`, `rim`, `bow`, `edge`, `bell`, ...
- `kit` is the source drum kit identity.
- `instrument` is the source instrument identity inside that kit.
- `sourceArt` is the source articulation inside that instrument.
- `params` are optional per-layer settings.

## Slot Art And Source Art

Slot art is what the pad plays. Source art is what the library provides.

Same destination and source art:

```text
snare1/rim:MyKit/snare-14/rim
```

Different destination and source art:

```text
tom2/rim:AasiMonster/tom-X2/center
```

That means: when `tom2/rim` is triggered, play source `center` from `AasiMonster/tom-X2`.

## Layers

If more than one line maps the same slot/art, those lines are layers.

```text
snare1/rim:NakedDrums/snare-X1/center
snare1/rim:NakedDrums/snare-X2/center:tuning=60
```

Both layers are used for `snare1/rim`.

Layer order is the order in the normalized map for that slot/art. UI rows should keep this order.

## Current Params

| Param | Example | Meaning | Current apply behavior |
| --- | --- | --- | --- |
| `tuning` | `tuning=-20` | cents, negative or positive | live for existing layer |
| `attack` | `attack=25` | attack in ms, `-1` means default | live for existing layer |
| `decay` | `decay=250` | decay in ms, `-1` means default | live for existing layer |
| `vol` | `vol=-3.5` | layer gain in dB | live for existing layer |
| `pan` | `pan=-40` | -127 left to +127 right | live for existing layer |
| `velocityResponse` | `velocityResponse=logarithmic` | per-layer MIDI velocity to output dB override | live in the pad popup without reloading samples |
| `flipLR` | `flipLR=1` | swap stereo left/right | live for existing layer |
| `flipPhase` | `flipPhase=1` | invert phase | live for existing layer |
| `bleed` | `bleed=oh:0.25,room:0.5` | mic gain trims from 0.0 to 1.0 | live for existing layer |
| `micRoute` | `micRoute=dtm1:snare1/dtm,ons:overhead/ons` | send source mic to one exact output route | live in the pad popup; raw map apply reloads the slot |

Use `micRoute=dtm1:-` to leave source mic `dtm1` unassigned from every output.

If `velocityResponse` is absent, the layer uses the global response. A layer override can use the stable built-in code `linear`, `logarithmic`, or `exponential`, or a comma-separated list of increasing `velocity:dB` points. A point list must start at velocity 1, end at velocity 127 and `0 dB`, never decrease, and never exceed `0 dB`. It changes output gain only. Exact sample selection still follows the sample filename velocity boundaries.

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

Hihat maps usually need destination zone and expression state.

Common destination examples:

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

Some libraries have richer internal source states, for example:

```text
bow[open]-pdl_opn_cls013
bow[half]-pdl_opn_cls049
bow[tight]-pdl_opn_cls085
```

The map should keep the destination intent clear. Example:

```text
hihat1/bow[open]:TchackpoumDG1.3/hihat-14/bow[open]
hihat1/edge[open]:TchackpoumDG1.3/hihat-14/edge[open]
hihat1/pedal:TchackpoumDG1.3/hihat-14/pedal
```

## Mic Routing Param

Use this when one source mic should go to a different output mic than automatic routing chooses.

Param:

```text
micRoute=sourceMic:outputId,sourceMic:outputId
```

Example:

```text
snare1/center:MyKit/snare-14/center:micRoute=dtm1:snare1/dtm,dtm2:aux1/**m
```

Meaning:

- `dtm1` and `dtm2` are separate source mics.
- `snare1/dtm` is the stable ID of `Snr1 Top` in the default mic layout.
- `aux1/**m` is the stable ID of the mono `Aux1` output.
- `micRoute=dtm1:-` leaves source mic `dtm1` unassigned.

Pad popup routing changes update loaded sample destinations in place. Applying a changed raw map still rebuilds the affected slot.
When a popup mic code is an output-layout alias rather than the library's original source code, full map loading resolves that alias through the source mic's automatic output assignment.
Source variants stay independent. For example, `dtm1` and `dtm2` have separate popup controls and may use `micRoute=dtm1:snare1/dtm,dtm2:-`. A generic layout output code such as `dtm` accepts both variants, while a variant output code such as `dtm1` accepts only that variant. The popup shows output names, but saves each mic-layout output's unique ID so repeated output codes remain separate and layout reordering does not change saved routes.

Only mic-layout target output `code` values may use `*`, independently in the type and position characters. Source mic codes and sample filenames never use `*`. The format stays exact. A present variant must match, while an omitted variant accepts all variants:

- `**m` accepts any mono mic, including numbered variants.
- `**s` accepts any stereo mic.
- `**i` accepts any mid-side mic.
- `*tm` accepts any top-position mono mic.
- `d*m` accepts any direct mono mic position.
- `**m1` accepts any mono mic whose variant is `1`.

## Raw Map Apply

Raw map apply should use diff apply by default.

A full rebuild is still useful as an explicit recovery/debug action, but it should be a separate button or option. Normal edits should rebuild only the affected slot/layers when possible.

## Current Source Of Truth

Current implementation and tests:

- `../source/Engine/DrumKitEngineMappingModel.cpp`
- `../source/Engine/DrumKitEngine.cpp`
- `../tests/TestInstrumentMap.cpp`

Related plans:

- `TODO11_PAD_POPUP_LAYER_PARAMETERS_PLAN.md`
- `PLAN-bleed-routing-combobox.md`
- `PLAN-instrument-map-format-and-routing-docs.md`
