# Custom Mic Routing and Balance Metadata

## Summary

Add explicit kit-slot types, type-driven direct-mic routing, auxiliary mic allocation, and balance-aware automatic L/R flipping.

Existing kit layouts and instrument maps remain compatible through ID-derived type fallback and the existing `flipLR` map parameter.

## Editable Entity Diagram

```text
+------------------+        +-----------------------+
| Kit Layout       |        | Library               |
|------------------|        |-----------------------|
| instr slots      |        | kits                  |
| articulations    |        | library instruments   |
| MIDI notes/CCs   |        | articulations         |
| type/kind        |        | sample files          |
| balance          |        | vel/rr/mic codes      |
+--------+---------+        +----------+------------+
         |                             |
         |                             |
         v                             v
+---------------------------------------------------+
| Instrument Map                                    |
|---------------------------------------------------|
| slot/articulation -> library source layer(s)      |
| per-layer params: tuning, gain, flipLR, bleed     |
+--------------------------+------------------------+
                           |
                           |
                           v
+------------------+   +--------------------------------+
| Mic Layout       |   | Instrument Catalog / Config     |
|------------------|   |--------------------------------|
| host output order|   | kind -> family                  |
| normal outputs   |   | kind -> default routing policy  |
| aux pool outputs |   | user-extendable kind aliases    |
| shared aux out   |   +----------------+---------------+
+--------+---------+                    |
         |                              |
         +---------------+--------------+
                         |
                         v
+---------------------------------------------------+
| Routing Plan                                      |
|---------------------------------------------------|
| source mic code -> host output channels           |
| common/dedicated/shared-aux/exclusive-aux claims  |
| warnings for skipped or ambiguous source mics     |
+--------------------------+------------------------+
                           |
                           v
+---------------------------------------------------+
| Instrument Synths                                 |
|---------------------------------------------------|
| primary slot synths + layered shadow synths       |
+---------------------------------------------------+
```

## Goals

- Give custom kit slots such as octobans a predictable way to use direct mics from layered source instruments.
- Preserve separate direct mic codes instead of combining all direct mics into one output.
- Prevent direct mics from unrelated source instruments from being routed through another instrument's dedicated processing.
- Describe the intended type and stereo position of a kit-layout slot explicitly.
- Read balance metadata from library articulation folders and automatically orient stereo sources to the correct side.
- Keep routing-plan construction and sample loading outside the realtime audio path.

## Kit Layout Changes

Extend `KitLayout::InstrumentSlot` with:

- An explicit instrument kind/type.
- An optional signed `balance` value.

Supported YAML fields:

```yaml
instruments:
  - id: drum1
    display: Octoban 1
    type: octoban
    balance: L48
    articulations:
      - { notes: [50], code: center }
```

Treat instrument type as a hierarchical catalog:

- `type` stores the concrete kind, such as `tom`, `octoban`, `ride`, `cowbell`, or `shaker`.
- The instrument catalog resolves each kind to a broader family, such as drum-like, cymbal-like, or accessory/effect-like.
- The same catalog resolves each kind to a default routing policy.
- A generic kind such as `drum`, `cymbal`, or a not-yet-named accessory/effect category remains valid when the slot is intentionally nonspecific.

Initial catalog examples:

```yaml
instrumentCatalog:
  kinds:
    kick:       { family: drum, routing: dedicated }
    tom:        { family: drum, routing: dedicated }
    snare:      { family: drum, routing: dedicated }
    octoban:    { family: drum, routing: shared_aux }
    rototom:    { family: drum, routing: shared_aux }
    timpani:      { family: drum, routing: shared_aux }
    timbale:      { family: drum, routing: shared_aux }
    mridangam:    { family: drum, routing: shared_aux }
    conga:        { family: drum, routing: shared_aux }
    bongo:        { family: drum, routing: shared_aux }
    djembe:       { family: drum, routing: shared_aux }
    cajon:        { family: drum, routing: shared_aux }
    taiko:        { family: drum, routing: shared_aux }
    surdo:        { family: drum, routing: shared_aux }
    talking_drum: { family: drum, routing: shared_aux }

    hihat:      { family: cymbal, routing: dedicated }
    ride:       { family: cymbal, routing: dedicated }
    crash:      { family: cymbal, routing: common }
    splash:     { family: cymbal, routing: common }
    china:      { family: cymbal, routing: common }
    bell:       { family: cymbal, routing: common }
    stack:      { family: cymbal, routing: common }
    chopper:    { family: cymbal, routing: common }
    gong:       { family: cymbal, routing: common }
    karatala:   { family: cymbal, routing: common }


    cowbell:    { family: accessory, routing: shared_aux }
    tambourine: { family: accessory, routing: shared_aux }
    shaker:     { family: accessory, routing: shared_aux }
    maracas:      { family: accessory, routing: shared_aux }
    cabasa:       { family: accessory, routing: shared_aux }
    guiro:        { family: accessory, routing: shared_aux }
    claves:       { family: accessory, routing: shared_aux }
    agogo:        { family: accessory, routing: shared_aux }
    wood_block:   { family: accessory, routing: shared_aux }
    temple_block: { family: accessory, routing: shared_aux }
    triangle:     { family: accessory, routing: shared_aux }
    vibraslap:    { family: accessory, routing: shared_aux }
    castanets:    { family: accessory, routing: shared_aux }
    sleigh_bells: { family: accessory, routing: shared_aux }
    whip:         { family: accessory, routing: shared_aux }
    ratchet:      { family: accessory, routing: shared_aux }

```

The catalog should live in the global Karatala config YAML. Add or update a config-structure document before implementation so the root key and override behavior are clear.

When `type` is absent, derive it from the slot ID after removing trailing digits. This preserves existing layouts.

Balance syntax:

- `C0`: center
- `L1` through `L127`: left, with increasing distance from center
- `R1` through `R127`: right, with increasing distance from center

Do not preserve or parse the old draft-only `position: left` and `position: right` field. Serialize new or edited layouts using `balance`.

## Type-Driven Direct Routing

Use the resolved kit-slot kind and catalog entry to choose its default routing behavior.

### Dedicated Direct Types

Configurable list of types continue using dedicated direct mic entries that target their slot ID or their type:

- `kick`
- `snare`
- `tom`
- `hihat`
- `ride`

That is the default list and can be extended through the instrument catalog in global config.

Exact slot-ID routing remains higher priority than type fallback.

### Shared Aux Stereo Routing

Some non-dedicated instruments should not claim separate close/direct outputs by default. Instead, they route their source mic signal into one shared stereo aux output.

Examples:

- `octoban` can default to shared aux stereo unless the user opts into exclusive aux-pool routing.
- `cowbell`, `tambourine`, `shaker`, and similar accessory/effect instruments default to shared aux stereo.
- Mono sources are placed into the shared stereo output using the slot/layer balance.
- Stereo sources are sent to the shared stereo output and may be L/R flipped by the existing `flipLR` behavior.

The intended default mic-layout convention is to place two stereo aux outputs at the end of the 32-channel layout, usually channels 29-32. This should be represented explicitly by mic-layout entries rather than hardcoded by channel number.

### Exclusive Aux Pool Routing

A user can opt a slot or source layer into exclusive aux-pool routing when they want separate outputs per source mic code.

This supports custom acoustic instruments such as octobans assembled from multiple tom layers while keeping their direct or close signals separate from normal tom channels and processing.

### Common-Only Types

Other and unknown types use common overhead and room mics but do not automatically receive direct mics.

This remains useful for instruments that should only appear in normal common kit microphones.

## Mic Layout Changes

Allow mic-layout entries to declare auxiliary roles:

```yaml
mics:
  - name: Aux Shared A
    role: aux_shared
    code: ons

  - name: Aux Shared B
    role: aux_shared
    code: ons

  - name: Aux Pool 1
    role: aux_pool
    code: dtm

  - name: Aux Pool 2
    role: aux_pool
    code: dbm
```

Existing entries without `role` keep their current dedicated or common behavior.

An `aux_shared` stereo entry is a shared destination for many non-dedicated instruments. A mono source may be balanced into it; a stereo source may be routed through it.

An `aux_pool` entry provides one globally exclusive host output assignment. It may be claimed by one kit-slot/source-mic-code pair.

Auxiliary compatibility is based on:

- Full source mic code, including its variant.
- Channel count and format.

Auxiliary routing must not assume mono. Percussive/accessory sources may be recorded as stereo.

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
4. Mic-layout `aux_pool` entry order.

### Allocation Rules

- Common overhead and room mics continue routing to every applicable synth.
- Dedicated direct mics continue routing by exact slot ID, then legacy type fallback.
- A `shared_aux_stereo` slot sends eligible source mics to the shared aux stereo destination.
- A mono source in shared aux stereo uses the slot/layer balance to place it in the stereo field.
- A stereo source in shared aux stereo keeps its stereo channels, with optional `flipLR`.
- An `aux_pool` slot requests one exclusive auxiliary output for every distinct source mic code used by its mapped sources.
- Multiple layers in the same slot using the same source mic code reuse the same exclusive auxiliary output.
- Different source mic codes receive different exclusive auxiliary outputs.
- An exclusive auxiliary output cannot be claimed by another slot or source mic code.
- A source mic is routed to shared aux when no compatible free exclusive auxiliary output exists.
- Skipping one source mic does not reject the instrument map or silence common/shared microphones.

### Warnings and Status

Collect routing diagnostics while constructing the plan.

Report:

- Slot and source path.
- Source mic code that could not be routed.
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

Add routing controls for non-dedicated slots:

- Default routing display from the instrument catalog.
- Optional per-slot routing override, at least `shared_aux_stereo`, `aux_pool`, and `common`.
- Balance control for mono sources routed to shared aux stereo.
- For stereo shared-aux sources, keep `flipLR` as the first version's orientation control.

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
- Optional slot routing override, if added in v1.

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
std::optional<RoutingPolicy> routingOverride;
```

### `KitLib::ArticulationSpec`

Add optional signed balance metadata and a conflict/validity representation suitable for parser warnings and UI display.

### `SourceItem`

Expose the resolved balance needed by source pickers and automatic orientation.

### `MicLayout::Mic`

Add a role enum or equivalent parsed value with at least:

- Normal
- AuxShared
- AuxPool

Unknown roles should produce a warning and behave as normal entries for compatibility.

### Routing Plan

Introduce an immutable, non-realtime routing-plan model shared by engine graph construction and synth sample loading.

It must represent:

- Common assignments.
- Dedicated assignments.
- Shared auxiliary assignments.
- Auxiliary claims.
- Unmatched source mic diagnostics.

## Compatibility

- Layouts without explicit `type` keep ID-derived behavior.
- The old draft-only `position` field is removed and not treated as compatibility surface.
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
- Route a shared-aux slot into shared aux stereo by default.
- Balance mono shared-aux sources inside the stereo output.
- Route stereo shared-aux sources as stereo.
- Route an aux-pool slot's source mic through a compatible exclusive auxiliary entry.
- Allocate separate exclusive auxiliary outputs for `dtm`, `dbm`, `dim`, `ons`, and other distinct codes.
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

### Shared Aux Cowbell

`cowbell1` defaults to shared aux stereo.

If the selected cowbell source is mono, the slot balance places it in the shared stereo aux output. If the source is stereo, it routes to the shared stereo aux output and can be flipped with `flipLR`.

### Layered Octoban

`octoban1` is mapped to several tom sources containing `dtm`, `dbm`, and `dim`.

With default shared aux stereo routing, the selected source mics are mixed into the shared aux stereo output, using slot balance and `flipLR` as needed.

With an explicit aux-pool routing override, the routing plan claims three compatible exclusive auxiliary entries. Every layer using `dtm` shares the first claim, while `dbm` and `dim` receive separate outputs. No source is sent through normal tom direct outputs.


### Exhausted Auxiliary Pool

`octoban2` uses aux-pool routing and requires `dtm` and `dbm`, but only one compatible free auxiliary output remains.

The first deterministic claim is routed. The other direct mic is skipped and shown as a warning. Overheads and room mics continue playing.

### Opposite-Side Cymbal

A kit slot has `balance: L80`, and the selected source articulation has `R64`.

Source selection enables `flipLR`. The user can turn it off manually, and that choice persists in the instrument map.

## Deferred Work

- Deriving pan from balance magnitude.
- Per-layer manual exclusive auxiliary-output selection.
- Dynamically creating host output buses.
- Sharing an auxiliary output when the pool is exhausted.
- Rejecting complete instrument maps because of missing auxiliary capacity.

## Open Questions

### Naming The Third Family

`percussion` is too broad because drums are also percussion. Avoid naming this family `aux`, because aux is also a routing/output concept.

Possible names:

- `accessory`: good for cowbell, tambourine, shaker, vibraslap; slightly hardware-like.
- `effect`: good for vibraslap, ratchet, whip; less good for cowbell/tambourine.
- `hand`: good for shaker, maracas, tambourine; wrong for mounted cowbell or blocks.
- `other`: technically clear but not musically helpful.
- `utility`: clear in code, less musical in UI.

Current recommendation: use `accessory` as the catalog family name and keep UI labels customizable later.

### Config Structure

Need a small config-structure document before implementation.

Suggested root placement:

```yaml
instrumentCatalog:
  kinds:
    octoban: { family: drum, routing: shared_aux_stereo }
    cowbell: { family: accessory, routing: shared_aux_stereo }
  aliases:
    cowbel: cowbell
```

Still to decide:

- Whether users can override only the catalog, or also the built-in default dedicated list.
- Whether catalog changes require full library rescan, full kit remap, or only routing rebuild.
- Whether slot-level routing override is stored as `routing:` in kit layout or as a separate `micRouting:` field.
