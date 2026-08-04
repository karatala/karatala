# TODO17: Kit Browser and Linked Pad Editing Plan

Date: 2026-07-17

Status: In progress - phases 1 and 2 implementation complete, build and manual verification pending

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
2. ✅ Phase 2 implementation:
   - Add a separate Browse page for complete library kits and saved instrument-map presets.
   - Preview instruments and articulations before loading.
   - Load, save, save as, and delete user instrument-map presets.
   - A saved preset contains only the instrument map, including layers and layer settings. It does not contain the kit layout or MIDI notes.
   - Preset entries for instrument slots missing from the active layout are skipped and reported. Automatic remapping remains deferred.
3. Phase 3:
   - Review the experimental Kit page and decide which existing elements should be reused, replaced, or removed.

The browser is implemented as a separate page in phase 2. Review of the older Kit-page controls remains phase 3.

## Goal

Add three connected workflows:

1. A separate kit and preset browser page that shows kit metadata, README content, instruments, articulations, and saved instrument maps.
2. A `Set instrument` action in the pad popup that replaces the whole instrument assigned to that kit-layout slot.
3. Temporary link switches in the pad popup for editing all articulations of the current instrument and the bleed levels of all toms together.

## Decisions and Assumptions

- The instrument-selection mode is opened from a known instrument slot and never offers full-kit loading.
- The complete-kit and instrument-map preset workflow is the separate `Browse` sidebar page.
- The experimental Kit page is unchanged during phases 1 and 2. Reuse of its layout, preset, assignment, and mic-layout controls is deferred to phase 3.
- Selecting a kit or preview pad never changes the loaded instrument map.
- `Load kit`, `Load preset`, and `Set instrument` are explicit actions. Each action applies its change immediately after its button is clicked.
- Kit README content is shown as read-only plain Markdown text with its original line breaks. A full Markdown renderer is not required.
- `Set instrument` reuses the shared large library picker in a whole-instrument mode. The main list contains one row per library instrument. A separate preview list contains that instrument's library articulations. `Use` applies the complete library instrument.
- `Set instrument` has no hidden instrument-slot type filter. All available instruments are shown; category and Types controls are optional manual filters.
- Link switches are temporary editing modes. They reset to off when the pad popup closes and are not saved in plugin state.
- Active link switches use a steady bright-red background and white text. They do not blink, because constant motion would distract from the parameter controls.
- Saved combined presets contain only instrument-map text with its layers and layer settings. They do not contain kit-layout or MIDI-note data.
- Loading from Browse replaces the complete current instrument map. It preserves the active kit layout and MIDI notes.

## Current State

- [`KitHomePanel`](../source/UI/KitHomePanel.h) has a small `Start from library kit` combo and builds a complete draft map with `DrumKitEngine::buildInstrumentMapForKit()`.
- `KitLib::DrumKitSpec::MetaInfo` already contains `name`, `author`, `license`, and the kit root directory.
- `KitLib::DrumKitSpec::readmeMarkdown` stores `README.md` or fallback `readme.md` content loaded during library scanning.
- [`KitBrowserComponent`](../source/UI/KitBrowserComponent.h) is the separate Browse page for library kits and instrument-map presets.
- [`KitPreviewGrid`](../source/UI/KitPreviewGrid.h) shows grouped rectangular articulation pads and previews without changing the map.
- [`SourcePickerPopup`](../source/UI/SourcePickerPopup.h) supports contextual titles, a right-side `×` close button, library filtering, per-articulation preview, articulation-specific `Use`, and a whole-instrument mode with one main-list row per library instrument and a separate library-articulation preview list.
- [`PadContextPopup`](../source/UI/PadContextPopup.h) has all articulation groups for the current instrument slot and updates tuning, attack, decay, gain, pan, phase, L/R, and bleed without reloading samples.
- The pad-popup header contains the slot title, phase 1 link controls, `SET INSTRUMENT`, and MIDI-note controls.
- `DrumKitEngine::applyInstrumentMapSlotChange()` replaces one instrument slot without reloading unrelated slots, while `updateSlotParametersBatch()` applies linked parameter targets under one graph lock.

## ✅ Phase 2 Kit and Preset Browser

The browser is a separate `Browse` sidebar page. The experimental `KitHomePanel` is unchanged and will be reviewed in phase 3.

### Page Layout

Keep the browser and actions visible on one Browse page:

```text
┌ Kit list ───────┬ Kit information ─────────┬ Instrument preview ─────────────┐
│ Search          │ Title                    │ Kick  [center] [edge]           │
│ Kit A           │ Author [License]         │ Snare [center] [rim] [side]     │
│ Kit B           │                          │ Tom 1 [center] [rim]            │
│ Kit C           │ README.md content        │ Ride  [bow] [bell] [edge]       │
│                 │                          │                                 │
└─────────────────┴──────────────────────────┴─────────────────────────────────┘

[Load kit]                              [Save] [Save as] [Delete]
```

- Give the kit list about 25 percent of the browser width.
- Give the metadata and README area about 30 percent.
- Give the instrument preview area the remaining width.
- Use local scrolling for long kit lists, long README files, and large instrument collections.
- Keep the component usable when the Browse page is narrow.

### Kit List and Information

- Add a text filter above the kit list.
- Match title, author, license, and raw folder name.
- Reuse the library picker's search rules, including `-word` exclusions.
- Show a clear empty-library message with a hint to check the library path.
- Show:
  - Title from `kit.meta.name`, with `codeRaw` as fallback.
  - Author, or `Unknown author` when empty.
  - License in brackets, or `No license information` when empty.
  - Read-only `README.md` content, or `No README.md found` when empty.

### Tiny Instrument and Articulation Pads

Create a library-kit preview grid instead of reusing `DrumPadGrid`, because `DrumPadGrid` represents kit-layout instrument slots rather than library instruments.

- Show one compact group for each library instrument.
- Show the instrument name once in the group header.
- Show one small rectangular pad for every articulation.
- Pads do not need a square aspect ratio.
- All articulation pads in one instrument group use the same instrument-type colour.
- Adjacent instrument groups have visible spacing, so colour does not become the only grouping cue.
- Clicking an articulation pad calls `DrumKitEngine::previewSourceItem()`.
- Use horizontal click position for preview velocity, matching the shared library picker.
- Flash only the clicked pad while its preview starts.

### Full-Kit Load

`Load full kit` must:

1. Use the currently selected kit layout.
2. Build the complete map with `DrumKitEngine::buildInstrumentMapForKit()`.
3. Replace the current instrument assignments only after the explicit button click.
4. Apply the complete map once.
5. Refresh plugin state, Raw map text, the Instrument page, and the pad grid.
6. Report skipped layout slots when the selected kit has no compatible instrument.

Keep current automatic matching rules, preferred library identities, and automatic L/R orientation.

### Instrument-Map Presets

- Show factory and user presets in a separate section.
- Show the preset map as read-only text and resolve its instruments and articulations into the same preview grid.
- `Load preset` replaces the complete current instrument map.
- `Save` replaces the selected user preset with the current map.
- `Save as` creates a new user preset and rejects an existing name.
- `Delete` is available only for user presets and requires confirmation.
- Preserve the active kit layout and MIDI notes for every preset action.

### Superseded Browser Assignment Draft

Single-slot replacement is handled by `Set instrument` from the pad popup. It is intentionally not part of the complete-kit and preset browser.

## Pad Popup: Set Instrument

### Header Layout

Use this left-to-right order:

```text
[Slot title]  [Link arts]  [LINK TOMS BLEED]  [Set instrument]  [MIDI:] [note buttons]
```

- `LINK TOMS BLEED` is visible only when the instrument slot type is `Tom`.
- Keep the MIDI-note controls right-aligned when enough width is available.
- On a narrow popup, reduce the slot-title width before hiding or clipping action controls.
- Add concise tooltips for both link switches.

### Whole-Instrument Picker Mode

Add a whole-instrument selection mode to `SourcePickerPopup`:

- Keep the existing 80-percent modal size, dark outside overlay, text/category/type filtering, preview, arrow keys, Enter, and outside-click dismissal.
- Add a fixed title row with a `×` close button on the right.
- Use only the slot display name for whole-instrument selection, such as `Tom 1`.
- Use `Kick / center` for articulation selection and append ` (map layer X)` only for map layer 2 and higher.
- Show each available parent instrument once in the main list, regardless of the instrument slot type.
- Show the selected instrument's articulations in a separate preview-only list.
- In whole-instrument mode, `Use` returns the selected item's kit and parent instrument. Preview-articulation selection never changes which instrument `Use` returns.
- The normal map-layer library picker remains articulation-specific and keeps its multi-select articulation filters and current behavior.

When `Set instrument` is confirmed:

1. Remove every current map layer for this instrument slot.
2. Add one map layer with a complete slot and library path for each library articulation in the selected library instrument. Use an exact articulation-slot match first, then the configured instrument-type fallback order. Show a warning for every fallback or skipped library articulation.
3. Reapply only this slot.
4. Rebuild all pad-popup articulation rows and bleed controls from the applied map.
5. Close the picker and preview the current pad articulation when a compatible library articulation exists.

This action intentionally replaces previous layers and their per-layer parameter overrides for the slot.

## Pad Popup: Linked Parameter Editing

### Exact Link Behavior

| Switch | Available on | Changed values | Targets |
| --- | --- | --- | --- |
| `Link arts` | Every slot | Tuning, attack, decay, gain, pan, L/R flip, phase flip, bleed level, and mic routing | The same layer number across every articulation of the current instrument slot |
| `LINK TOMS BLEED` | Tom slots only | The changed bleed mic code and level or routing destination | Every mapped layer and articulation in every instrument slot whose kit-layout type is `Tom` |

Additional rules:

- Layer numbers are matched inside each articulation slot. Editing layer 1 changes layer 1 in the linked articulations. Editing layer 2 changes only linked articulations that also have layer 2.
- Missing layers are skipped. Linking never creates a new layer.
- A wildcard map layer is updated once, even when it serves several articulation slots.
- Bleed linking matches the stable mic code, not the display name or output name.
- A target map layer without that mic code is skipped.
- Linked routing copies `Auto`, `Unassigned`, or the selected stable output ID. A target without that compatible output choice is skipped.
- If both switches are active, use the union of their targets and update each map layer only once.
- Apply only the parameter that the user changed. For example, moving tuning must not copy attack, gain, or bleed from the edited row.
- Library selection, map-layer add or remove actions, and MIDI notes are not linked.
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

1. Resolve the edited map layer and the linked target map layers.
2. Update the matching visible controls without firing their callbacks again.
3. Apply a batch of `LayerParameterUpdate` values or mic-route updates under one engine graph lock.
4. Serialize the changed full map once.
5. Notify the editor once so plugin state is persisted.

The linked path must remain field-specific. It must not reload sample files or rebuild unrelated instrument players.

## Implementation Plan

### ✅ 1. Load Kit README Metadata

- Add a `readmeMarkdown` field to `KitLib::DrumKitSpec`.
- During the existing library scan, load `README.md`, with `readme.md` as a fallback.
- Read the file once during scanning. Do not perform file I/O from `paint()` or `resized()`.
- Keep missing or unreadable README files as empty content and continue scanning the kit.

Files:

- [`source/Engine/KitLibModel.h`](../source/Engine/KitLibModel.h)
- [`source/Engine/KitLibModel.cpp`](../source/Engine/KitLibModel.cpp)

### ✅ 2. Add Shared Instrument-Assignment Helpers

- Add focused helpers in `DrumKitEngineMapHelpers.*` for:
  - ✅ Building one map layer for each library articulation.
  - ✅ Applying automatic orientation to each new map layer.
  - ✅ Replacing one instrument slot while preserving all other map layers.
  - Compatible instrument-slot listing was removed from this phase because Browse does not assign one instrument.
- Reuse the slot replacement helpers from the engine and `PadContextPopup`.

Files:

- [`source/Engine/DrumKitEngineMapHelpers.h`](../source/Engine/DrumKitEngineMapHelpers.h)
- [`source/Engine/DrumKitEngineMapHelpers.cpp`](../source/Engine/DrumKitEngineMapHelpers.cpp)
- [`source/Engine/DrumKitEngine.h`](../source/Engine/DrumKitEngine.h)
- [`source/Engine/DrumKitEngine.cpp`](../source/Engine/DrumKitEngine.cpp)

### ✅ 3. Build the Kit and Preset Browser

- Add `KitBrowserComponent` for kit selection, metadata, README display, and actions.
- Add a small library-kit preview component that groups rectangular articulation pads by instrument.
- Reuse `UITheme` instrument colours and library-preview callbacks.
- Keep browser state as stable kit/instrument identities rather than raw pointers across library reloads.

Files:

- [`source/UI/KitBrowserComponent.h`](../source/UI/KitBrowserComponent.h) and [`source/UI/KitBrowserComponent.cpp`](../source/UI/KitBrowserComponent.cpp)
- [`source/UI/KitPreviewGrid.h`](../source/UI/KitPreviewGrid.h) and [`source/UI/KitPreviewGrid.cpp`](../source/UI/KitPreviewGrid.cpp)
- [`source/UI/UITheme.h`](../source/UI/UITheme.h)

### ✅ 4. Add Separate Browse Integration

- Add a separate `Browse` sidebar page without changing the experimental Kit page.
- Apply complete kit and preset maps through `PluginEditor`, with processing suspended and plugin state updated once.
- Keep the active kit layout and MIDI notes unchanged.
- Skip and report preset entries for slots absent from the active layout. Do not remap them automatically.

Files:

- [`source/UI/Sidebar.h`](../source/UI/Sidebar.h)
- [`source/UI/Sidebar.cpp`](../source/UI/Sidebar.cpp)
- [`source/PluginEditor.h`](../source/PluginEditor.h)
- [`source/PluginEditor.cpp`](../source/PluginEditor.cpp)

### ✅ 5. Add Whole-Instrument Mode to the Shared Picker

- Add an explicit picker selection mode rather than inferring behavior from which panel opened it.
- Keep the normal articulation-selection callback.
- Add a whole-instrument callback that returns kit and instrument identity.
- Show one main-list row per instrument and a separate preview-only articulation list.
- Show the articulation-slot context in a fixed title row with a right-side `×` close button.
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
- Resolve linked targets by instrument slot, articulation slot, map-layer number, and map-layer identity.
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
- Browse ownership, loading scope, preview behavior, and preset contents.

### ✅ 9. Move Developer Actions to the Status Bar

- Remove `UI` and the debug-only `?` inspector action from the sidebar.
- Place both actions at the right side of the status bar.
- Keep Browse and normal work pages in the sidebar.

Files:

- [`source/UI/Sidebar.h`](../source/UI/Sidebar.h)
- [`source/UI/Sidebar.cpp`](../source/UI/Sidebar.cpp)
- [`source/UI/StatusBar.h`](../source/UI/StatusBar.h)
- [`source/UI/StatusBar.cpp`](../source/UI/StatusBar.cpp)

Mark each completed section in this plan with `✅`. When all work and verification are complete, change the main heading to start with `# ✅ COMPLETED:`.

## Automated Verification

Phase 1 focused test cases have been added for whole-slot replacement, cross-type assignment, map-layer projection, wildcard isolation, linked target deduplication, routing library-path resolution, and single-mic bleed preservation. A Phase 2 test verifies library README loading. They have not been run yet.

Extend [`tests/TestKitLibParsers.cpp`](../tests/TestKitLibParsers.cpp):

- Kit metadata still parses title, author, and license.
- Library scanning stores README content.
- A missing README does not reject the kit.

Extend [`tests/TestInstrumentMap.cpp`](../tests/TestInstrumentMap.cpp):

- Full-kit mapping keeps current matching and automatic-orientation behavior.
- Assigning one instrument replaces only the chosen instrument slot.
- Other slots and their parameters remain unchanged.
- Whole-instrument assignment writes one complete slot and library path for each library articulation. It uses exact articulation-slot matches first, then configured instrument-type fallback rules with warnings.
- `Link arts` updates the same layer number across articulation slots.
- Missing linked layers are skipped.
- A wildcard map layer is not duplicated.
- `LINK TOMS BLEED` updates only Tom slots and only the selected mic code.
- Non-Tom slots and unrelated layer parameters remain unchanged.
- Both link switches together deduplicate their target map layers.

## Manual Verification

### Kit Browser

- Select kits with and without author, license, and README metadata.
- Filter kits with positive words and `-word` exclusions.
- Preview every visible articulation at low and high click velocities.
- Confirm pads are grouped clearly by instrument even without relying only on colour.
- Load a complete kit and confirm the active layout is preserved.
- Load, save, save as, and delete user instrument-map presets.
- Confirm preset loading replaces the instrument map but keeps the active kit layout and MIDI notes.
- Confirm missing instrument slots are skipped and reported.
- Reload the sample library while Browse is open and confirm browser selection remains safe.
- Confirm `UI` and the debug-only `?` action are at the right side of the status bar and are absent from the sidebar.

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

- The separate Browse page shows a searchable kit list, title, author, license, README content, and grouped articulation preview pads on one page.
- Any articulation in the selected kit can be previewed without changing the current map.
- The selected kit can replace the complete current kit explicitly.
- Instrument-map presets can be loaded, saved, saved as, and deleted without changing the kit layout or MIDI notes.
- The pad popup can replace its whole instrument through the shared picker without a hidden instrument-slot type restriction.
- `Link arts` updates the defined parameter and routing set across matching articulation layers.
- `LINK TOMS BLEED` updates matching bleed mic codes and compatible routing across all Tom slots.
- Linked edits are immediate, persisted, field-specific, and do not reload samples.
- Active link modes are highly visible without blinking.
