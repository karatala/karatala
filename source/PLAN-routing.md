# Custom Mic Routing and Balance Metadata

## Summary

Add explicit kit-slot types, type-driven direct-mic routing, auxiliary mic allocation, and balance-aware automatic L/R flipping.

Existing kit layouts and instrument maps remain compatible through ID-derived type fallback and the existing `flipLR` map parameter.

## Goals

- Give custom kit slots such as octobans a predictable way to use direct mics from layered source instruments.
- Preserve separate direct mic codes instead of combining all direct mics into one output.
- Prevent direct mics from unrelated source instruments from being routed through another instrument's dedicated processing.
- Describe the intended type and stereo position of a kit-layout slot explicitly.
- Read balance metadata from library articulation folders and automatically orient stereo sources to the correct side.
- Keep routing-plan construction and sample loading outside the realtime audio path.

## Kit Layout Changes

Extend `KitLayout::InstrumentSlot` with:

- An explicit instrument `type`.
- An optional signed `balance` value.

Supported YAML fields:

```yaml
instruments:
  - id: drum1
    display: Octoban 1
    type: drum
    balance: L48
    articulations:
      - { notes: [50], code: center }
```

Supported instrument types in main 3 groups (drum/cymbal/aux): 

- `drum`
  - `kick`
  - `tom`
  - `snare`
  - `rototom`
  - `octoban`
  - `mridangam`
  - `timpani`
  - `conga`
  - `bongo`
  - `timbale`
  - `djembe`
  - `cajon`
  - `talking_drum`
  - `taiko`
  - `surdo`

- `cymbal`
  - `hihat`
  - `ride`
  - `crash`
  - `splash`
  - `china`
  - `bell`
  - `stack`
  - `chopper`
  - `gong`
  - `karatala`

- `aux`
  - `cowbel`
  - `tambourine`
  - `shaker`
  - `maracas`
  - `cabasa`
  - `guiro`
  - `claves`
  - `agogo`
  - `wood_block`
  - `temple_block`
  - `triangle`
  - `vibraslap`
  - `castanets`
  - `sleigh_bells`
  - `whip`
  - `ratchet`

etc... list can be extended with config

When `type` is absent, derive it from the slot ID after removing trailing digits. This preserves existing layouts.

Balance syntax:

- `C0`: center
- `L1` through `L127`: left, with increasing distance from center
- `R1` through `R127`: right, with increasing distance from center

no need to preserve legacy `position: left` and `position: right`. Serialize new or edited layouts using `balance`.

## Type-Driven Direct Routing

Use the resolved kit-slot type to choose its default direct-mic behavior.

### Dedicated Direct Types

Configurable list of types continue using dedicated direct mic entries that target their slot ID or their type:

- `kick`
- `snare`
- `tom`
- `hihat`
- `ride`

-that is default list that can be extended via config
Exact slot-ID routing remains higher priority than type fallback.

### Auxiliary Direct Type

mic layout should reserve 2 last stereo channels for not determined`drum` and `aux` mics.

`drum`-kinda instrument slots can also use auxiliary outputs for source direct mics.

This supports custom acoustic instruments such as octobans assembled from multiple tom layers while keeping their direct signals separate from normal tom channels and processing.

### Common-Only Types

Other and unknown types use common overhead and room mics but do not automatically receive direct mics.

This includes `aux` by default. Its sources may still be heard through common microphones.

## Mic Layout Changes

Allow mic-layout entries to declare an auxiliary role:

```yaml
mics:
  - name: Aux Direct 1
    role: aux
    code: dtm

  - name: Aux Direct 2
    role: aux
    code: dbm

  - name: Aux Overhead
    role: aux
    code: ons
```

Existing entries without `role` keep their current dedicated or common behavior.

An auxiliary entry provides one globally exclusive host output assignment. It may be claimed by one kit-slot/direct-code pair.

Auxiliary compatibility is based on:

- Full direct mic code, including its variant.
- Channel count and format.

The initial use case is mono direct mics, but routing must not assume that every future auxiliary entry is mono.

## Routing Plan

Replace the duplicated mic-target resolution in `DrumKitEngine::applyMicLayoutUnlocked()` and `MicRoutingBuilder` with one precomputed routing plan.

Build the plan after the instrument map and all source layers have been resolved, because auxiliary allocation depends on the direct mic codes used by those sources.

The routing plan maps:

```text
slot + synth layer + source mic code -> host channel range
```

Both primary and shadow synths consume this plan during sample loading. They must not independently reconstruct auxiliary routing.

### Allocation Order

Allocate auxiliary outputs deterministically in this order:

1. Kit-layout slot order.
2. Instrument-map entry order within the slot.
3. Source articulation and mic discovery order.
4. Mic-layout auxiliary entry order.

### Allocation Rules

- Common overhead and room mics continue routing to every applicable synth.
- Dedicated direct mics continue routing by exact slot ID, then legacy type fallback.
- A `drum`-kinda slot requests one auxiliary output for every distinct direct mic code used by its mapped sources.
- Multiple layers in the same slot using the same direct mic code reuse the same auxiliary output.
- Different direct mic codes receive different auxiliary outputs.
- An auxiliary output cannot be claimed by another slot or direct code.
- A source direct mic is skipped when no compatible free auxiliary output exists.
- Skipping one direct mic does not reject the instrument map or silence common microphones.

### Warnings and Status

Collect routing diagnostics while constructing the plan.

Report:

- Slot and source path.
- Direct mic code that could not be routed.
- Required channel count or format.
- Available auxiliary entries, where useful.

The instrument configuration UI should show these warnings without requiring users to inspect logs.

## Library Balance Metadata

Support balance as an optional suffix in articulation folder names:

```text
center-C0
center-L36
bow-R127
bow[half]-pdl_opn_cls043-L64
```

Balance suffixes are metadata, not expression dimensions. Detect and remove them from the expression-dimension parsing path before processing dimension codes.

Store balance as a signed integer:

- `-127` through `-1`: left
- `0`: center
- `1` through `127`: right
- No value: unknown

If folders merged into the same articulation contain conflicting balance values, add a parser warning and mark the articulation balance as unknown for automatic orientation.

Keep the full magnitude for future pan support. Version 1 only uses the sign.

## Automatic L/R Orientation

When selecting or automatically assigning a source:

- If the kit slot and source articulation are on opposite sides, enable `flipLR`.
- If both are on the same side, disable the automatic flip.
- If either side is centered, unknown, or conflicting, do not automatically flip.

This is a selection-time default. The existing instrument-map `flipLR` value remains the persisted and runtime authority.

Manual changes after source selection must not be overwritten during refresh or reload.

For instrument-level source selections that include several articulations:

- Use automatic orientation only when all relevant source articulations have the same known side.
- Treat mixed or unknown source balances as unknown and leave `flipLR` unchanged.

## Instrument Configuration UI

Add one slot-level type selector to the instrument configuration view.

The selector:

- Displays the resolved type for every slot.
- Updates the active `KitLayout`.
- Reapplies routing when the type changes.
- Uses the existing kit-layout Save or Save As flow for persistence.
- Does not silently overwrite factory layout files.

For each source layer, display:

- Source balance, such as `L36`, `C0`, or `R127`.
- Current `L/R` flip state.
- Resolved direct routing, such as `dtm -> Aux Direct 1`.
- A warning when a direct mic is omitted because no auxiliary output is available.

Add a compact `L/R` toggle to the main instrument configuration rows. The existing pad context popup toggle remains available and must use the same state.

Apply automatic flip behavior consistently when selecting a source from either:

- The main instrument configuration source picker.
- The pad context source picker.
- Automatic kit-to-layout mapping.

## Persistence

Kit-layout YAML owns:

- Slot `type`.
- Slot `balance`.

Instrument-map text continues to own:

- Per-layer `flipLR`.
- Tuning, attack, decay, gain, phase, and bleed overrides.

The routing plan is derived runtime state and is not persisted.

Saving or cloning a user kit-layout preset must round-trip the new slot fields.

## Public Model Changes

### `KitLayout::InstrumentSlot`

Add:

```cpp
KitLib::InstrumentSpec::Type type;
std::optional<int> balance;
```

### `KitLib::ArticulationSpec`

Add optional signed balance metadata and a conflict/validity representation suitable for parser warnings and UI display.

### `SourceItem`

Expose the resolved balance needed by source pickers and automatic orientation.

### `MicLayout::Mic`

Add a role enum or equivalent parsed value with at least:

- Normal
- Auxiliary

Unknown roles should produce a warning and behave as normal entries for compatibility.

### Routing Plan

Introduce an immutable, non-realtime routing-plan model shared by engine graph construction and synth sample loading.

It must represent:

- Common assignments.
- Dedicated assignments.
- Auxiliary claims.
- Unmatched source mic diagnostics.

## Compatibility

- Layouts without explicit `type` keep ID-derived behavior.
- Legacy `position` to remove
- Layouts without auxiliary mic entries retain current routing behavior.
- Existing instrument-map syntax remains unchanged.
- Existing explicit `flipLR` values continue to work.
- Existing dedicated `tomN`, `kickN`, and other slot mappings remain valid.
- No routing-plan construction, string handling, allocation, or logging is added to the audio thread.

## Test Plan

### Kit Layout Parsing

- Parse explicit slot types.
- Fall back to ID-derived types.
- Parse `C0`, left, and right balances.
- Parse legacy `position`.
- Prefer `balance` when both fields exist.
- Warn on invalid types and balance values.
- Round-trip type and balance through YAML serialization.

### Library Parsing

- Parse balance suffixes independently from expression dimensions.
- Parse folders containing both balance and expression metadata.
- Preserve articulation names and expressions.
- Detect conflicting balances for a merged articulation.
- Keep missing balance metadata valid.

### Routing

- Preserve existing dedicated slot routing.
- Preserve common overhead and room routing.
- Route a `drum` slot's direct mic through a compatible auxiliary entry.
- Allocate separate auxiliary outputs for `dtm`, `dbm`, `dim`, and other distinct codes.
- Preserve variants such as `dtm1` versus `dtm2`.
- Reuse one auxiliary assignment for repeated layers using the same direct code.
- Prevent two slots from claiming the same auxiliary entry.
- Match compatible channel counts and formats.
- Skip and warn when the auxiliary pool is exhausted.
- Continue routing common mics when a direct mic is skipped.
- Produce identical assignments across repeated rebuilds.
- Apply the same routing to primary and shadow synths.

### Automatic Orientation

- Flip when source and slot balances have opposite signs.
- Do not flip for matching sides.
- Do not infer a flip for centered or unknown balance.
- Do not infer a flip for conflicting source balances.
- Preserve a manual flip after refresh and reload.
- Apply the default from both source picker surfaces.
- Apply the default during automatic kit mapping.

### Verification

- Build the `Tests` target.
- Run focused kit-layout, library parser, instrument-map, and routing tests.
- Run the complete Catch2 suite because routing affects engine and UI integration.
- Open the standalone editor and verify resizing, type editing, source selection, status messages, preset persistence, and layered playback.

## Acceptance Scenarios

### Normal Tom

`tom1` remains type `tom`. A `tom1` mic-layout entry routes its direct mic to the existing Tom 1 host output. Common microphones remain unchanged.

### Layered Octoban

`drum1` is mapped to several tom sources containing `dtm`, `dbm`, and `dim`.

The routing plan claims three compatible auxiliary entries. Every layer using `dtm` shares the first claim, while `dbm` and `dim` receive separate outputs. No source is sent through normal tom direct outputs.

### Exhausted Auxiliary Pool

`drum2` requires `dtm` and `dbm`, but only one compatible free auxiliary output remains.

The first deterministic claim is routed. The other direct mic is skipped and shown as a warning. Overheads and room mics continue playing.

### Opposite-Side Cymbal

A kit slot has `balance: L80`, and the selected source articulation has `R64`.

Source selection enables `flipLR`. The user can turn it off manually, and that choice persists in the instrument map.

## Deferred Work

- Deriving pan from balance magnitude.
- Per-layer manual auxiliary-output selection.
- Dynamically creating host output buses.
- Sharing an auxiliary output when the pool is exhausted.
- Rejecting complete instrument maps because of missing auxiliary capacity.
