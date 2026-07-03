# CC Expression Point UI And Layout Defaults

## Summary

- Add configurable implicit CC expression points for controller-driven articulations (`pdl_opn_cls`, `zon_ctr_rim`, `zon_bow_edg`).
- Keep existing explicit folder suffixes like `edge[open]-pdl_opn_cls024` authoritative.
- Let simplified folders like `edge[open]`, `edge[half]`, `center`, `rim`, `bow`, and `edge` receive CC points from built-in defaults plus optional kit-layout overrides.
- Keep `pedal[splash]` and choke-articulation playback out of this implementation; track them as follow-up features.

## Key Changes

- Add a layout expression profile model:
  - Built-in defaults in code.
  - Optional top-level `KitLayout.expressionPoints`.
  - Optional per-slot `InstrumentSlot.expressionPoints`, overriding top-level values.
- YAML shape:

```yaml
expressionPoints:
  pdl_opn_cls: { open: 24, half: 52, close: 78, tight: 85, pedal: 85 }
  zon_ctr_rim: { center: 24, rim: 85 }
  zon_bow_edg: { bow: 24, edge: 85 }
instruments:
  - id: hihat1
    expressionPoints:
      pdl_opn_cls: { open: 20, half: 50, close: 76, tight: 90, pedal: 90 }
```

- Parser accepts scalar integer values only, clamped to `0..127`; serializer writes only explicit layout values.
- Built-in defaults:
  - `pdl_opn_cls`: `{ open: 24, half: 52, close: 78, tight: 85, pedal: 85 }`
  - `zon_ctr_rim`: `{ center: 24, rim: 85 }`
  - `zon_bow_edg`: `{ bow: 24, edge: 85 }`
  - pdl_opn_cls should have validation open>half>close>tight>=pedal
- Add an engine-side resolver that returns effective expression values:
  - Explicit source folder dimensions win.
  - Otherwise use slot override, then layout override, then built-in default.
  - For `pdl_opn_cls`, match by articulation expression first (`open`, `half`, `close`, `tight`) and by name for `pedal`.
  - For zone controllers, match by articulation name (`center/rim`, `bow/edge`).
- Add layout validation warnings when controller `notes` are empty or reference notes that are not present on any articulation in the same slot.
- Update routing build only, not the audio thread:
  - `collectResolvedLayoutArticulations()` can discover implicit controller siblings.
  - `buildCcMappings()` uses effective values, not only folder-encoded values.
  - hi-hat `pocValue` uses the effective `pdl_opn_cls` value when explicit metadata is absent.
- Preserve current behavior for instruments that already encode CC values in folder names.

## UI Changes

- Add 'curve' button that opens popup "Expression curve points" editor to the MIDI layout tab next to cc dropdown in controller rows, making cc dropdown a little less wide so button fits.
- Show button only for slots/controllers that use known expression dimensions.
- Each controller row shows named point sliders/steppers for its dimension, seeded from effective values.
- Edited values are stored as per-slot layout overrides and trigger `setKitLayoutDirect(..., reapplyCurrentMap=true)`.
- Source picker and instrument-map syntax stay unchanged.

## Tests

- Add layout YAML parse/serialize tests for top-level and per-slot `expressionPoints`.
- Add routing tests:
  - hi-hat folders `edge[open]`, `edge[half]`, `edge[close]`, `edge[tight]`, `pedal` get implicit `pdl_opn_cls` mappings.
  - per-slot layout override changes the generated CC table.
  - explicit folder suffixes override layout defaults.
  - `zon_bow_edg` can route one cymbal note between implicit `bow` and `edge`.
  - existing explicit CC-folder instruments keep current routing.
- Run:
  - `cmake --build T:/source/repos/karatala/build --config Debug --target Tests -j 8`
  - `Tests.exe "[yaml]"`
  - focused synth/controller routing tests
  - full `Tests.exe`

## Assumptions

- "Implicit" means source articulations without expression-dimension suffixes only.
- Built-in defaults are not user config; user-editable persistence is through kit layout YAML.
- V1 UI edits per-slot values, because different pads/controllers may need different calibration.
- Controller rows must explicitly reference the notes they apply to; empty controller note lists are invalid for this feature.
- `pedal[splash]` detection and choke-articulation playback are separate follow-up plans.
