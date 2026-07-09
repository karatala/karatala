# Instrument Map Format

## Purpose

The instrument map tells Karatala which library instrument and articulation each kit slot should play.

Each non-empty, non-comment line maps one slot or one slot articulation to one source. Multiple matching lines become layers.

## Basic Line Shape

```text
slot/slotArt:kit/instrument/sourceArt[,sourceArt...][:params]
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
| `flipLR` | `flipLR=1` | swap stereo left/right | live for existing layer |
| `flipPhase` | `flipPhase=1` | invert phase | live for existing layer |
| `bleed` | `bleed=oh:0.25,room:0.5` | mic gain trims from 0.0 to 1.0 | live for existing layer |


```text
ride1/bow:KitA/ride-20/bow
```

## Param Separators

Top-level params can be separated by semicolon.

This is fine when every value is simple:

```text
ride1/bow:KitA/ride-20/bow:tuning=-2;attack=50;decay=300
```

Some values contain commas. `bleed=` is the important current example:

```text
kick1:MyKit/kick-24:bleed=dtm:0.5,room:0.8
```

When another top-level param follows a comma-list value, prefer semicolon:

```text
kick1:MyKit/kick-24:bleed=dtm:0.5,room:0.8;tuning=-20
```

This is easier to read than mixing every separator as a comma.

Current parser detail: a comma or semicolon starts the next top-level param only when it is followed by a known `key=`.

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

## Planned Mic Routing Param

This is planned, not implemented yet.

Recommended future param:

```text
micRoute=sourceMic:outputMic,sourceMic:outputMic
```

Example:

```text
kick1:MyKit/kick-24:bleed=dtm:0.5,room:0.8;micRoute=dtm:room
```

Meaning:

- `bleed=dtm:0.5,room:0.8` changes mic levels.
- `micRoute=dtm:room` sends source mic `dtm` to output mic `room`.

Routing changes require slot reload because sample output channels are assigned when samples are loaded. They are not live scalar changes like gain or pan.

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