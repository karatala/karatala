# TODO17: Kit Browser and Linked Pad Editing Plan

Date: 2026-07-17

Status: In progress - phase 1 implementation complete, build and manual verification pending

md plan for next features: new similar to instrument picker component for kit list+preview where we can see more kit info (its Title, Author [license] and readme.md content) and easily preview any instrument with articulations using some tiny pad-grid, pads dont have to be square but just color grouped by instrument, all on single page with ability to fully load selected kit or assign any instrument to some coresponding slot of our kit layout.
then in pad popup need to add 'set instrument' button on left of midi note buttons that opens new instrument picker
and add near set instrument button on left switch-button to link param changes for all articulations of that instrument (tuning, attack, ..., bleed) and if instrument slot is tom then extra switch button to tweak bleed params of all toms. those switch buttons should be very noticeable when active (maybe bright red or even blinking?)

## Clarified Delivery Order

The original paragraph above and the latest user decisions override the older implementation order below.

1. ✅ Phase 1 implementation:
   - Add whole-instrument replacement from the pad popup.
   - Add temporary `LINK ARTS` and `LINK TOMS BLEED` editing.
   - Keep parameter and mic-routing updates field-specific and persist the instrument map immediately.
   - Build and manual verification are still pending because they were not requested.
2. Phase 2:
   - Add a separate browser mode for complete library kits and saved instrument-map presets.
   - Preview instruments and articulations before loading.
   - Load, save, save as, and delete user instrument-map presets.
   - A saved preset contains only the instrument map, including layers and layer settings. It does not contain the kit layout or MIDI notes.
   - Handling preset entries for destination slots missing from the active layout is deferred.
3. Phase 3:
   - Review the experimental Kit tab and decide which existing elements should be reused, replaced, or removed.

The embedded Kit-tab browser described later in this document is deferred to phases 2 and 3. It is not part of phase 1.

## Goal

Add three connected workflows:

1. A kit browser modal component that shows each kit's title, author, license, README content, instruments, and articulations.
2. A `Set instrument` action in the pad popup that replaces the whole instrument assigned to that kit-layout slot.
3. Temporary link switches in the pad popup for editing all articulations of the current instrument and the bleed levels of all toms together.

## Decisions and Assumptions

- The instrument-selection mode is opened from a known destination slot and never offers full-kit loading.
- The complete-kit and instrument-map preset mode is a separate later workflow. Its final placement is not decided.
- The experimental Kit tab is not changed during phase 1. Reuse of its layout, preset, assignment, and mic-layout controls is deferred.
- Selecting a kit or preview pad never changes the loaded instrument map.
- `Load full kit` and `Assign to slot` are explicit actions. Each action applies its change immediately after its button is clicked.
- Kit README content is shown as read-only plain Markdown text with its original line breaks. A full Markdown renderer is not required.
- `Set instrument` reuses the shared large source picker in a whole-instrument mode. The main list contains one row per instrument; a separate preview list contains that instrument's articulations. `Use` applies the parent instrument.
- `Set instrument` has no hidden destination-slot type filter. All available instruments are shown; category and Types controls are optional manual filters.
- Link switches are temporary editing modes. They reset to off when the pad popup closes and are not saved in plugin state.
- Active link switches use a steady bright-red background and white text. They do not blink, because constant motion would distract from the parameter controls.
- Saved combined presets contain only instrument-map text with its layers and layer settings. They do not contain kit-layout or MIDI-note data.

## Current State

- [`KitHomePanel`](../source/UI/KitHomePanel.h) has a small `Start from library kit` combo and builds a complete draft map with `DrumKitEngine::buildInstrumentMapForKit()`.
- `KitLib::DrumKitSpec::MetaInfo` already contains `name`, `author`, `license`, and the kit root directory.
- The library scan does not currently keep the kit's `README.md` content.
- [`SourcePickerPopup`](../source/UI/SourcePickerPopup.h) supports contextual titles, a right-side `×` close button, source filtering, per-articulation preview, articulation-specific `Use`, and a whole-instrument mode with one main-list row per instrument and a separate articulation preview list.
- [`PadContextPopup`](../source/UI/PadContextPopup.h) has all articulation groups for the current destination slot and updates tuning, attack, decay, gain, pan, phase, L/R, and bleed without reloading samples.
- The pad-popup header contains the slot title, phase 1 link controls, `SET INSTRUMENT`, and MIDI-note controls.
- `DrumKitEngine::applyInstrumentMapSlotChange()` replaces one destination slot without reloading unrelated slots, while `updateSlotParametersBatch()` applies linked parameter targets under one graph lock.

## Deferred Target Kit and Preset Browser

This section is an older design draft. Review it against the clarified delivery order before implementing phases 2 or 3. In particular, embedding it directly in `KitHomePanel` is no longer decided.

### Page Layout

Keep the browser and actions visible in the same scrollable Kit tab:

```text
Kit layout and default controls

┌ Kit list ───────┬ Kit information ─────────┬ Instrument preview ─────────────┐
│ Search          │ Title                    │ Kick  [center] [edge]           │
│ Kit A           │ Author [License]         │ Snare [center] [rim] [side]     │
│ Kit B           │                          │ Tom 1 [center] [rim]            │
│ Kit C           │ README.md content        │ Ride  [bow] [bell] [edge]       │
│                 │                          │                                 │
└─────────────────┴──────────────────────────┴─────────────────────────────────┘

[Load full kit]       Selected instrument: [Assign to: compatible slot] [Assign]

Existing preset, slot-assignment, and mic-layout controls
```

- Give the kit list about 25 percent of the browser width.
- Give the metadata and README area about 30 percent.
- Give the instrument preview area the remaining width.
- Use local scrolling for long kit lists, long README files, and large instrument collections.
- Keep the component usable when the main Kit tab viewport is narrow. Columns may stack vertically below a practical minimum width.

### Kit List and Information

- Add a text filter above the kit list.
- Match title, author, license, and raw folder name.
- Reuse the instrument picker's search rules, including `-word` exclusions.
- Show a clear empty-library message with a hint to check the library path.
- Show:
  - Title from `kit.meta.name`, with `codeRaw` as fallback.
  - Author, or `Unknown author` when empty.
  - License in brackets, or `No license information` when empty.
  - Read-only `README.md` content, or `No README.md found` when empty.

### Tiny Instrument and Articulation Pads

Create a source-kit preview grid instead of reusing `DrumPadGrid`, because `DrumPadGrid` represents destination kit-layout slots rather than source-library instruments.

- Show one compact group for each source instrument.
- Show the instrument name once in the group header.
- Show one small rectangular pad for every articulation.
- Pads do not need a square aspect ratio.
- All articulation pads in one instrument group use the same instrument-type colour.
- Adjacent instrument groups have visible spacing, so colour does not become the only grouping cue.
- Clicking an articulation pad calls `DrumKitEngine::previewSourceItem()`.
- Use horizontal click position for preview velocity, matching the shared source picker.
- Flash only the clicked pad while its preview starts.
- Selecting any articulation pad also selects its parent instrument for the `Assign to slot` action.

### Full-Kit Load

`Load full kit` must:

1. Use the currently selected kit layout.
2. Build the complete map with `DrumKitEngine::buildInstrumentMapForKit()`.
3. Replace the current instrument assignments only after the explicit button click.
4. Apply the complete map once.
5. Refresh the draft map, slot-assignment rows, Instrument tab, and pad grid.
6. Report skipped layout slots when the selected kit has no compatible instrument.

Keep current automatic matching rules, preferred library identities, and automatic L/R orientation.

### Assign One Source Instrument

For the instrument selected in the preview grid:

- Show destination slots whose type matches the source instrument type.
- Allow every slot when the destination type is `Unknown`.
- Show all matching choices when a layout has several slots of the same type, such as `tom1`, `tom2`, and `tom3`.
- Disable `Assign` and show a short reason when no compatible slot exists.
- Replace all existing map entries for the chosen destination slot.
- Add one instrument-level entry with no source-articulation filter, so the engine can use all compatible articulations from that source instrument.
- Preserve every other destination slot and the active kit layout.
- Apply only the changed slot through the surgical slot-change path.
- Refresh the browser selection, assignment table, Instrument tab, and pad grid after success.

The full-kit and single-instrument actions must use shared map-building helpers. Do not duplicate identity or automatic-orientation logic in UI code.

## Pad Popup: Set Instrument

### Header Layout

Use this left-to-right order:

```text
[Slot title]  [Link arts]  [LINK TOMS BLEED]  [Set instrument]  [MIDI:] [note buttons]
```

- `LINK TOMS BLEED` is visible only when the destination slot type is `Tom`.
- Keep the MIDI-note controls right-aligned when enough width is available.
- On a narrow popup, reduce the slot-title width before hiding or clipping action controls.
- Add concise tooltips for both link switches.

### Whole-Instrument Picker Mode

Add a whole-instrument selection mode to `SourcePickerPopup`:

- Keep the existing 80-percent modal size, dark outside overlay, text/category/type filtering, preview, arrow keys, Enter, and outside-click dismissal.
- Add a fixed title row with a `×` close button on the right.
- Use only the slot display name for whole-instrument selection, such as `Tom 1`.
- Use `Kick / center` for articulation selection and append ` (layer X)` only for layer 2 and higher.
- Show each available parent instrument once in the main list, regardless of the destination slot type.
- Show the selected instrument's articulations in a separate preview-only list.
- In whole-instrument mode, `Use` returns the selected item's kit and parent instrument. Preview-articulation selection never changes which instrument `Use` returns.
- The normal layer-source picker mode remains articulation-specific and keeps its multi-select articulation filters and current behavior.

When `Set instrument` is confirmed:

1. Remove every current map entry for this destination slot.
2. Add one instrument-level entry for the selected kit and instrument.
3. Reapply only this slot.
4. Rebuild all pad-popup articulation rows and bleed controls from the applied map.
5. Close the picker and preview the current pad articulation when a compatible source articulation exists.

This action intentionally replaces previous layers and their per-layer parameter overrides for the slot.

## Pad Popup: Linked Parameter Editing

### Exact Link Behavior

| Switch | Available on | Changed values | Targets |
| --- | --- | --- | --- |
| `Link arts` | Every slot | Tuning, attack, decay, gain, pan, L/R flip, phase flip, bleed level, and mic routing | The same layer number across every articulation of the current destination slot |
| `LINK TOMS BLEED` | Tom slots only | The changed bleed mic code and level or routing destination | Every mapped layer and articulation in every destination slot whose kit-layout type is `Tom` |

Additional rules:

- Layer numbers are matched inside each destination articulation. Editing layer 1 changes layer 1 in the linked articulations. Editing layer 2 changes only linked articulations that also have layer 2.
- Missing layers are skipped. Linking never creates a new layer.
- A wildcard instrument-level map entry is updated once, even when it serves several destination articulations.
- Bleed linking matches the stable source mic code, not the display name or output name.
- A target source without that mic code is skipped.
- Linked routing copies `Auto`, `Unassigned`, or the selected stable output ID. A target without that compatible output choice is skipped.
- If both switches are active, use the union of their targets and update each map entry only once.
- Apply only the parameter that the user changed. For example, moving tuning must not copy attack, gain, or bleed from the source row.
- Source selection, layer add/remove actions, and MIDI notes are not linked.
- Every linked change is audible immediately and is also written to the saved instrument-map text.

### Active Appearance

- Add a shared bright-red semantic colour for dangerous or broad linked-edit modes.
- Active state uses bright red fill, white text, and a clear on-state border.
- Inactive state uses the normal dark toggle style.
- Use explicit labels such as `LINK ARTS` and `LINK TOMS BLEED`.
- Do not use animation or blinking.

### Safe Update Path

Refactor the pad-popup parameter callbacks so they report one changed field instead of always copying the full row.

For each edit:

1. Resolve the source map entry and the linked target entries.
2. Update the matching visible controls without firing their callbacks again.
3. Apply a batch of `LayerParameterUpdate` values or mic-route updates under one engine graph lock.
4. Serialize the changed full map once.
5. Notify the editor once so plugin state is persisted.

The linked path must remain field-specific. It must not reload sample files or rebuild unrelated synths.

## Implementation Plan

### Deferred 1. Load Kit README Metadata

- Add a `readmeMarkdown` field to `KitLib::DrumKitSpec`.
- During the existing library scan, load `README.md`, with `readme.md` as a fallback.
- Read the file once during scanning. Do not perform file I/O from `paint()` or `resized()`.
- Keep missing or unreadable README files as empty content and continue scanning the kit.

Files:

- [`source/Engine/KitLibModel.h`](../source/Engine/KitLibModel.h)
- [`source/Engine/KitLibModel.cpp`](../source/Engine/KitLibModel.cpp)

### 2. Add Shared Instrument-Assignment Helpers

- Add focused helpers in `DrumKitEngineMapHelpers.*` for:
  - ✅ Building one preferred-ID instrument-level map entry.
  - ✅ Applying automatic orientation for that entry.
  - ✅ Replacing one destination slot while preserving all other entries.
  - Listing compatible destination slots from the active kit layout.
- Reuse these helpers from both `KitHomePanel` and `PadContextPopup`.

Files:

- [`source/Engine/DrumKitEngineMapHelpers.h`](../source/Engine/DrumKitEngineMapHelpers.h)
- [`source/Engine/DrumKitEngineMapHelpers.cpp`](../source/Engine/DrumKitEngineMapHelpers.cpp)
- [`source/Engine/DrumKitEngine.h`](../source/Engine/DrumKitEngine.h)
- [`source/Engine/DrumKitEngine.cpp`](../source/Engine/DrumKitEngine.cpp)

### Deferred 3. Build the Kit and Preset Browser

- Add `KitBrowserComponent` for kit selection, metadata, README display, and actions.
- Add a small source-kit preview component that groups rectangular articulation pads by instrument.
- Reuse `UITheme` instrument colours and source-preview callbacks.
- Keep browser state as stable kit/instrument identities rather than raw pointers across library reloads.

Files:

- New `source/UI/KitBrowserComponent.h/.cpp`
- New `source/UI/KitPreviewGrid.h/.cpp`
- [`source/UI/UITheme.h`](../source/UI/UITheme.h)

### Deferred 4. Decide Kit-Tab Integration

- Replace the small library-kit combo with the embedded browser.
- Keep existing layout, preset, assignment, and mic-layout sections.
- Route full-kit load and one-slot assignment through `KitHomePanel` callbacks so editor state and status messages remain consistent.
- Remove the old combo-only draft-generation path after the browser actions cover it.

Files:

- [`source/UI/KitHomePanel.h`](../source/UI/KitHomePanel.h)
- [`source/UI/KitHomePanel.cpp`](../source/UI/KitHomePanel.cpp)
- [`source/PluginEditor.h`](../source/PluginEditor.h)
- [`source/PluginEditor.cpp`](../source/PluginEditor.cpp)

### ✅ 5. Add Whole-Instrument Mode to the Shared Picker

- Add an explicit picker selection mode rather than inferring behavior from which panel opened it.
- Keep the normal articulation-selection callback.
- Add a whole-instrument callback that returns kit and instrument identity.
- Show one main-list row per instrument and a separate preview-only articulation list.
- Show the destination context in a fixed title row with a right-side `×` close button.
- Keep one preview implementation for both modes.

Files:

- [`source/UI/SourcePickerPopup.h`](../source/UI/SourcePickerPopup.h)
- [`source/UI/SourcePickerPopup.cpp`](../source/UI/SourcePickerPopup.cpp)

### ✅ 6. Add Pad-Popup Header Actions

- Add `Link arts`, conditional `LINK TOMS BLEED`, and `Set instrument` controls.
- Lay them out directly to the left of the MIDI label and note buttons.
- Open the shared picker in whole-instrument mode.
- Replace and refresh only the current slot after selection.

Files:

- [`source/UI/PadContextPopup.h`](../source/UI/PadContextPopup.h)
- [`source/UI/PadContextPopup.cpp`](../source/UI/PadContextPopup.cpp)

### ✅ 7. Add Linked Parameter Targeting and Batch Updates

- Introduce a small parameter-field enum for the popup's editable layer values.
- Resolve linked targets by destination slot, articulation, layer number, and map-entry identity.
- Add batch engine update APIs that accept resolved parameter or mic-routing updates and lock the graph once per user edit.
- Update only the changed optional field in each target.
- For bleed, update only the changed mic code while preserving every other bleed level.
- For routing, copy only `Auto`, `Unassigned`, or one compatible stable output ID for the changed mic code.
- Serialize and notify once per user edit.

Files:

- [`source/UI/PadContextPopup.h`](../source/UI/PadContextPopup.h)
- [`source/UI/PadContextPopup.cpp`](../source/UI/PadContextPopup.cpp)
- [`source/Engine/DrumKitEngine.h`](../source/Engine/DrumKitEngine.h)
- [`source/Engine/DrumKitEngine.cpp`](../source/Engine/DrumKitEngine.cpp)
- [`source/Engine/DrumKitEngineMapHelpers.h`](../source/Engine/DrumKitEngineMapHelpers.h)
- [`source/Engine/DrumKitEngineMapHelpers.cpp`](../source/Engine/DrumKitEngineMapHelpers.cpp)

### ✅ 8. Update Durable UI Contracts

After implementation, update [`source/UI/AGENTS.md`](../source/UI/AGENTS.md) with:

- Whole-instrument picker mode.
- Exact linked-edit scope.
- The non-blinking bright-red active state.

Kit-browser ownership remains deferred with the browser itself.

Mark each completed section in this plan with `✅`. When all work and verification are complete, change the main heading to start with `# ✅ COMPLETED:`.

## Automated Verification

Phase 1 focused test cases have been added for whole-slot replacement, cross-type assignment, destination-layer projection, wildcard isolation, linked target deduplication, routing source-path resolution, and single-mic bleed preservation. They have not been run yet.

Extend [`tests/TestKitLibParsers.cpp`](../tests/TestKitLibParsers.cpp):

- Kit metadata still parses title, author, and license.
- Library scanning stores README content.
- A missing README does not reject the kit.

Extend [`tests/TestInstrumentMap.cpp`](../tests/TestInstrumentMap.cpp):

- Full-kit mapping keeps current matching and automatic-orientation behavior.
- Assigning one instrument replaces only the chosen destination slot.
- Other slots and their parameters remain unchanged.
- Compatible-slot listing handles several tom slots and `Unknown` slot types.
- Whole-instrument assignment writes no source-articulation filter.
- `Link arts` updates the same layer number across destination articulations.
- Missing linked layers are skipped.
- A wildcard entry is not duplicated.
- `LINK TOMS BLEED` updates only Tom slots and only the selected mic code.
- Non-Tom slots and unrelated layer parameters remain unchanged.
- Both link switches together deduplicate their target entries.

## Manual Verification

### Kit Browser

- Select kits with and without author, license, and README metadata.
- Filter kits with positive words and `-word` exclusions.
- Preview every visible articulation at low and high click velocities.
- Confirm pads are grouped clearly by instrument even without relying only on colour.
- Load a complete kit and confirm the active layout is preserved.
- Assign different toms from one or several kits to `tom1`, `tom2`, and `tom3`.
- Confirm single-instrument assignment does not reload or change unrelated slots.
- Reload the sample library while the Kit tab is open and confirm browser selection remains safe.

### Pad Popup

- Confirm the header order is link switches, `Set instrument`, MIDI label, and MIDI buttons.
- Confirm `LINK TOMS BLEED` appears only for Tom slots.
- Use `Set instrument`, confirm each instrument appears once, preview several articulations from the separate preview list, and apply the parent instrument.
- Confirm a Kick slot can show and accept a Snare, Tom, or any other available instrument type.
- Confirm whole-instrument and articulation pickers show the correct short context title and close from the right-side `×` button.
- Confirm the picker closes and all articulation rows now use the new instrument.
- Turn on `Link arts` and change each supported parameter.
- Confirm only the same layer number changes in other articulations.
- Turn on `LINK TOMS BLEED` and change direct, overhead, and room bleed values.
- Confirm matching mic codes change across all Tom slots and no other instrument type changes.
- Change `Auto`, `Unassigned`, shared, and slot-specific mic routes with each link mode. Confirm compatible stable output IDs propagate and incompatible targets are skipped.
- Confirm active link switches are immediately visible in bright red and stop being active when the popup is reopened.
- Save and reload plugin state and confirm every linked parameter edit persists.

## Completion Criteria

- The Kit tab shows a searchable kit list, title, author, license, README content, and grouped articulation preview pads on one page.
- Any articulation in the selected kit can be previewed without changing the current map.
- The selected kit can replace the complete current kit explicitly.
- Any selected source instrument can replace one compatible destination slot without changing other slots.
- The pad popup can replace its whole instrument through the shared picker without a hidden destination-type restriction.
- `Link arts` updates the defined parameter and routing set across matching articulation layers.
- `LINK TOMS BLEED` updates matching bleed mic codes and compatible routing across all Tom slots.
- Linked edits are immediate, persisted, field-specific, and do not reload samples.
- Active link modes are highly visible without blinking.
