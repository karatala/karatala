# ✅ COMPLETED: Custom Mic Routing and Balance Metadata

## Summary

Add explicit kit-slot instrument kinds, direct-mic output policy, shared/spare direct outputs, and balance-aware stereo placement.

This document is the source of truth. Avoid repeating rules later in the file; link back to these sections instead.

## Entity Map

```text
+------------------+        +-----------------------+
| Kit Layout       |        | Library               |
|------------------|        |-----------------------|
| instrument slots |        | kits                  |
| articulations    |        | library instruments   |
| MIDI notes/CCs   |        | articulations         |
| type/kind        |        | sample files          |
| balance          |        | vel/rr/mic codes      |
| directMicOuts    |        | d/o/r mic metadata    |
+--------+---------+        +----------+------------+
         |                             |
         +-------------+---------------+
                       |
                       v
+---------------------------------------------------+
| Instrument Map                                    |
|---------------------------------------------------|
| slot/articulation -> library instrument/layer(s)  |
| per-layer params: tuning, gain, flipLR, bleed     |
+--------------------------+------------------------+
                           |
                           v
+------------------+   +--------------------------------+
| Mic Layout       |   | Instrument Catalog / Config    |
|------------------|   |--------------------------------|
| host output order|   | kind -> family                 |
| normal mic outs  |   | kind -> directMicOuts default  |
| selector mic outs|   | aliases and user extensions    |
| group/direct outs|   +----------------+---------------+
+--------+---------+                    |
         +---------------+--------------+
                         |
                         v
+---------------------------------------------------+
| Routing Plan                                      |
|---------------------------------------------------|
| sample mic code -> host output channels           |
| direct mic d* -> own/auto/shared assignments      |
| warnings for skipped or ambiguous mic codes       |
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

- Give custom instrument slots like octobans a predictable way to use direct mics from layered source instruments.
- Preserve separate direct mic codes instead of combining all direct mics into one output.
- Prevent direct mics from unrelated source instruments from being routed through another instrument's dedicated processing.
- Describe the intended type and stereo balance of a kit-layout instrument slot explicitly.
- Read balance metadata from library articulation folders and automatically orient stereo sources to the correct side.
- Keep routing-plan construction and sample loading outside the realtime audio path.

## Implementation Stages

Use this order so each stage leaves the tree buildable and gives later routing work typed data instead of ad-hoc strings.

### Stage 1: Model And Parser Foundation

- Add shared `DirectMicOutPolicy` and balance parsing/formatting helpers.
- Extend concrete instrument kind parsing so kit slots and library instruments can represent kinds listed in [Instrument Catalog](#instrument-catalog).
- Parse and serialize kit-layout slot `type`, `balance`, and optional `directMicOuts`.
- Keep deriving `type` from the slot ID when absent and ignore the draft-only `position` field.
- Parse library articulation-folder balance suffixes before expression dimensions, store optional balance metadata, and flag conflicting merged folders.
- Keep mic-layout missing `code` validation aligned with [Mic Layout](#mic-layout).
- Add focused parser tests for kit layout, mic layout, instrument kinds, and library balance suffixes.

### Stage 2: Catalog And Selector Semantics

- Add the global instrument catalog model with kind aliases, families, and default direct-mic policies.
- Parse `instrumentCatalog` from global config and preserve existing defaults when omitted.
- Add mic-layout selector parsing for exact slots, wildcards, kinds, comma lists, families, and empty common selectors.
- Add compatibility helpers for full mic-code matching, including variant, channel count, and format.

### Stage 3: Immutable Routing Plan

- Build one immutable routing plan after the instrument map and library layers are resolved.
- Allocate `own` direct outputs before `auto`, then shared/group fallbacks in mic-layout order.
- Share private claims by slot plus full direct mic code, and emit diagnostics for skipped direct mic codes.
- Make primary and shadow synths consume the same routing plan.

### Stage 4: Balance-Aware Rendering

- Apply slot/layer balance to shared/group direct outputs.
- Pan mono direct mics into stereo group outputs and pan stereo pairs while preserving width.
- Preserve existing `flipLR` as the runtime authority and apply it before balance panning.

### Stage 5: Automatic Orientation

- Infer default `flipLR` when selected library articulation balance and kit-slot balance are known and opposite.
- Avoid inference for center, unknown, or conflicting library balance.
- Preserve manual `flipLR` edits across refresh/reload.

### Stage 6: UI And Persistence

- Show and edit slot `type`, `balance`, and direct mic output override.
- Show per-layer `flipLR`, effective direct mic routing, and direct mic diagnostics.
- Keep routing plans derived at runtime and out of persisted YAML.

### Stage 7: Verification Pass

- Build `Tests`.
- Run focused parser/routing/orientation tests, then the full Catch2 suite for cross-domain changes.
- Do UI smoke verification for source selection, resizing, and preset save/reload.

## Direct Mic Output Policy

This setting controls only KitLib direct mic codes, meaning mic codes whose type starts with `d`.

It does not control overhead `o*` or room `r*` mic codes. Those continue to follow normal mic-layout routing.

Use simple UI names:

- **Own Direct**: try to route direct mics to this slot's own/separate output first. If no own output is available, fall back to Shared Group.
- **Auto Direct**: try to route direct mics to a spare own/separate output after all Own Direct slots have been allocated. If no spare output is available, fall back to Shared Group.
- **Shared Group**: route direct mics straight to shared stereo direct output.

The UI uses the mic layout `name` as the group label; no extra shared-group labels are possible.

Config values:

```yaml
directMicOuts: own
directMicOuts: auto
directMicOuts: shared
```

When `directMicOuts` is absent, treat it as `shared`.

Avoid the word `aux` for an instrument family. `aux` is an output/routing concept.

## Kit Layout

Each `KitLayout::InstrumentSlot` may define:

```yaml
instruments:
  - id: drum1
    display: Octoban 1
    type: octoban
    balance: L48
    directMicOuts: auto
    articulations:
      - { notes: [50], code: center }
```

Fields:

- `type`: concrete instrument kind, such as `tom`, `octoban`, `ride`, `cowbell`, or `shaker`.
- `balance`: `C0`, `L1..L127`, or `R1..R127`.
- `directMicOuts`: optional override for direct mic codes only; when absent, use the catalog default for `type`, and if the catalog also omits it then use `shared`.

When `type` is absent, derive it from the slot ID after removing trailing digits. This preserves existing layouts.

Do not preserve or parse the old draft-only `position: left/right` field.

## Instrument Catalog

The global Karatala config owns the kind catalog. It maps a concrete kind to a broad family and a default direct mic output policy.

Suggested root:

```yaml
instrumentCatalog:
  kinds:
    kick:       { family: drum, directMicOuts: own }
    tom:        { family: drum, directMicOuts: own }
    snare:      { family: drum, directMicOuts: own }
    octoban:    { family: drum, directMicOuts: auto }
    rototom:    { family: drum, directMicOuts: auto }
    timbale:    { family: drum, directMicOuts: auto }
    timpani:    { family: drum }
    conga:      { family: drum }
    bongo:      { family: drum }
    djembe:     { family: drum }
    cajon:      { family: drum }
    taiko:      { family: drum }
    surdo:      { family: drum }
    mridangam:  { family: drum }
    talking_drum: { family: drum }

    hihat:      { family: cymbal, directMicOuts: own }
    ride:       { family: cymbal, directMicOuts: own }
    crash:      { family: cymbal }
    splash:     { family: cymbal }
    china:      { family: cymbal }
    bell:       { family: cymbal }
    stack:      { family: cymbal }
    chopper:    { family: cymbal }
    gong:       { family: cymbal }
    karatala:   { family: cymbal }


    cowbell:    { family: other }
    tambourine: { family: other }
    shaker:     { family: other }
    maracas:      { family: other }
    cabasa:       { family: other }
    guiro:        { family: other }
    claves:       { family: other }
    agogo:        { family: other }
    wood_block:   { family: other }
    temple_block: { family: other }
    triangle:     { family: other }
    vibraslap:    { family: other }
    castanets:    { family: other }
    sleigh_bells: { family: other }
    whip:         { family: other }
    ratchet:      { family: other }
  aliases:
    cowbel: cowbell
```

`other` is the name for the third family. It is imperfect but clearer than `percussion` and avoids overloading `aux`.

Catalog changes should require only a routing rebuild unless the implementation discovers that source-picker filtering also needs the updated family list.

## Mic Layout

Mic layout is the only source of truth for output order and which slots/kinds/families may use each output. Do not hardcode channels in engine logic.

Use `instrument` as a selector. No extra `role` field is needed.

Example:

```yaml
mics:
  - id: overhead/ons
    name: Overheads
    code: ons

  - id: tom1/dtm
    name: Tom 1
    instrument: tom1
    code: dtm

  - id: percussion/dts
    name: Percussion Direct
    instrument: tambourine,cowbell,vibraslap
    code: dts

  - id: other/dts
    name: Other Direct
    instrument: other
    code: dts

  - id: drum/dts
    name: Other Drums
    instrument: drum
    code: dts

  - id: rototom/dts
    name: Rototoms
    instrument: rototom*
    code: dts
```

Each mic item must have a unique non-empty `id` and a non-empty `code`. Missing or duplicate IDs invalidate the layout. If `code` is missing, warn and skip that mic item. Empty `instrument` is valid and means a normal common mic for all instruments.

Selector meanings:

- **Private selectors**: exact slot id, for example `tom1`; wildcard prefix ending in `*`, for example `rototom*`, when it matches concrete slot ids/kinds.
- **Group selectors**: catalog kind, for example `rototom` or `cowbell`; comma list, for example `tambourine,cowbell,vibraslap`; catalog family, for example `drum`, `cymbal`, or `other`.
- **Common selector**: empty `instrument`, current normal common mic behavior.

Selector match priority for private/direct claims:

1. Exact slot id.
2. Wildcard prefix.
3. Catalog kind.
4. Comma list.
5. Catalog family.
6. Empty common selector.

Exact catalog kind beats a comma-list group when both match. Shared/group fallback uses mic-layout order after filtering compatible group candidates.

Direct output compatibility is based on full direct mic code, including variant, channel count, and format. Group outputs may accept any compatible `d*` direct mic code selected by `instrument`; mono direct mics can be panned into stereo selected group outputs.

Use KitLib mic naming: sample files contain mic codes like `dim`, `dom`, `dts`, `ons`, `rfs`, `dtm1`; do not introduce a separate "source mic" term.

## Routing Rules

Build one immutable routing plan after the instrument map and library layers are resolved. Both primary and shadow synths consume this plan; they must not reconstruct routing independently.

| `directMicOuts` | Behavior |
| --- | --- |
| `own` | For `d*` mic codes, claim the best matching compatible private selector output. Allocate these before all `auto` slots. If no private match is available, fall back to the first compatible group selector output in mic-layout order. |
| `auto` | For `d*` mic codes, claim the best matching compatible private selector output only after all `own` slots have been allocated. If no private match is available, fall back to the first compatible group selector output in mic-layout order. |
| `shared` | For `d*` mic codes, use the first compatible group selector output in mic-layout order and do not reserve a private claim. This is the default when unspecified. |

Additional rules:

- Overhead `o*` and room `r*` normal mics continue routing as they do today.
- Allocation priority is `own` first, then `auto`, then `shared`.
- For private claims, exact slot id beats wildcard, kind, comma-list, and family selectors.
- Private claims are exclusive by slot plus full direct mic code.
- Multiple layers in the same slot using the same direct mic code reuse the same private claim.
- Different direct mic codes use different private claims when compatible selected outputs exist.
- `own` and `auto` never reject the whole instrument map because no private match is available; they fall back to the first compatible group selector output in mic-layout order.
- If no compatible private or group selector output can match a direct mic code, skip that direct mic code and show a warning.
- Routing-plan work happens outside the realtime audio thread.

## Balance And Stereo Placement

Balance syntax:

- `C0`: center
- `L1..L127`: left
- `R1..R127`: right

Runtime representation:

- `-127..-1`: left
- `0`: center
- `1..127`: right
- missing: unknown

Use balance for shared/group direct outputs:

- Mono direct mic: pan into the selected stereo group output according to slot/layer balance.
- Stereo direct mic: pan the stereo pair according to slot/layer balance while preserving stereo width.
- `flipLR` flips the stereo pair before balance panning.

## Library Balance Metadata

Support balance as an optional articulation-folder suffix:

```text
center-C0
center-L36
bow-R127
bow[half]-pdl_opn_cls043-L64
```

Balance suffixes are metadata, not expression dimensions. Remove the balance suffix before expression-dimension parsing.

If folders merged into the same articulation contain conflicting balance values, warn and treat the articulation balance as unknown for automatic orientation.

## Automatic L/R Orientation

When selecting or automatically assigning a library layer:

- If kit-slot balance and library-articulation balance are on opposite sides, enable `flipLR`.
- If both sides match, disable the automatic flip.
- If either side is centered, unknown, or conflicting, do not infer a flip.

This is a selection-time default. Existing instrument-map `flipLR` remains the runtime authority, and manual user changes must not be overwritten by refresh/reload.

For an instrument-level selection that includes many articulations, infer orientation only when all relevant library articulations have the same known side.

## UI

Instrument configuration should show:

- Slot `type`.
- Slot `balance`.
- Slot direct mic output policy: Own Direct, Auto Direct, or Shared Group.
- Per-layer `flipLR`.
- Per-layer effective direct mic routing, for example `dtm -> Tom 1` or `dts -> Other Direct`.
- Warnings when a direct mic code cannot be routed by any selected output.

Users should be able to edit:

- Slot type.
- Slot balance.
- Slot direct mic output override.
- Per-layer `flipLR`.

Shared Group balance controls:

- Mono layers: balance is a normal pan control.
- Stereo layers: balance pans the pair while preserving width.

Persistence:

- Kit layout owns slot `type`, `balance`, and optional `directMicOuts`.
- Instrument map owns per-layer `flipLR`, tuning, gain, attack, decay, phase, and bleed.
- Routing plan is derived runtime state and is not persisted.

## Public Model Changes

`KitLayout::InstrumentSlot`:

```cpp
KitLib::InstrumentSpec::Type type;
std::optional<int> balance;
std::optional<DirectMicOutPolicy> directMicOuts;
```

`KitLib::ArticulationSpec`:

- Optional signed balance metadata.
- Conflict/validity state for parser warnings and UI display.

`MicLayout::Mic`:

- Instrument selector parser for exact ids, wildcard prefixes, catalog kinds, comma lists, catalog families, and empty common selectors; missing mic `code` validation.

Routing plan:

- Common assignments.
- Own direct assignments.
- Auto direct assignments.
- Shared direct assignments.
- Private claims.
- Unmatched direct mic-code diagnostics.

## Test Plan

- Kit layout parsing: explicit type, derived type, `directMicOuts` override, balance, invalid values, YAML round trip.
- Library parsing: balance suffixes, expression dimensions with balance suffixes, conflicting balance values.
- Routing: current common overhead/room behavior, missing mic `code` warning, Own Direct private claim before Auto Direct, Auto Direct private claim after Own Direct, group fallback by mic-layout order, unmatched direct mic warning, primary/shadow synth consistency.
- Orientation: opposite-side auto `flipLR`, matching-side no flip, unknown/center no inference, manual override preserved.
- UI smoke test: type/balance/directMicOuts edits, source selection, resize, preset save/reload.
- Build `Tests` and run the focused parser/routing tests plus the full Catch2 suite for cross-domain changes.
