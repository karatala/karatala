# TODO10: UI Review, Refactor, and Performance Plan

Date: 2026-05-30

## Goal

Review the current UI implementation end to end, then make the next UI pass safer by separating real problems from already-completed older TODO items. The plan focuses on:

- Correctness and usability problems in the current UI.
- Performance improvements for repeated UI edits, page switches, library picking, previews, and settings changes.
- Reducing repeated code by extracting shared components and helpers.
- Introducing common theme variables for colours, fonts, spacing, control sizes, and instrument type colours.

## Current UI Surface Reviewed

- Main integration: `source/PluginEditor.h`, `source/PluginEditor.cpp`
- Pad components: `source/UI/DrumPad.*`, `source/UI/DrumPadGrid.*`
- Instrument editing: `source/UI/InstrumentConfigPanel.*`, `source/UI/PadContextPopup.*`, `source/UI/SourcePickerPopup.*`
- MIDI editing: `source/UI/MidiMapTable.*`, `source/UI/MidiNotePicker.*`
- Global UI: `source/UI/Sidebar.*`, `source/UI/StatusBar.*`, `source/UI/EngineSettingsPanel.*`
- Controls and style helpers: `source/UI/LabeledPot.*`, `source/UI/HorizontalKnobBrick.*`, `source/UI/LookAndFeel.*`, `source/UI/UITypes.h`, `source/UI/UIHelpers.cpp`
- Prior docs: `docs/UI_REDESIGN_PLAN.md`, `docs/TODO06_UI_IMPROVEMENTS_PLAN.md`, `docs/TODO07_UI_OPTIMISATIONS.md`

## Current State Notes

Several older UI TODOs are already partly implemented and should not be repeated blindly:

- `SourcePickerPopup` is already extracted and shared by `PadContextPopup` and `InstrumentConfigPanel`.
- `SourceItem`, `buildSourceItems`, `findSourceIndex`, and `createParamSlider` already exist in `UITypes.h` / `UIHelpers.cpp`.
- Attack/decay sentinel values now use `-1.0` in the current slider and pot setup.
- Instrument config slider text boxes are already `40x20` via `UIHelpers::createParamSlider`.
- The Settings page is already scrollable and has reset buttons.
- Sidebar knob labels already show `HH`, `VL`, and `RR` values.
- Pad grid spacing already uses integer remainder distribution and configurable `padGap`.

The next pass should update or replace stale parts of `TODO06` and `TODO07` rather than implementing them exactly as written.

## High Priority Problems

### 1. Instrument config add-layer path is half-refactored

`InstrumentConfigPanel::addRow` contains the normal row setup, but the `addButton->onClick` lambda manually creates a second kind of row inline. That inline path duplicates setup for labels, library button, sliders, add button, and remove button. It also leaves the new row's `addButton` with a no-op lambda, so adding a third map layer from a newly created map layer will not work.

Files:

- `source/UI/InstrumentConfigPanel.cpp`
- `source/UI/InstrumentConfigPanel.h`

Plan:

- Change `addRow` to accept an optional insertion index and optional label visibility hints.
- Make every add-layer action call the same row factory path.
- Remove the inline `new TableRow` setup block.
- Ensure add and remove behavior works from every layer row.
- Add a small regression test if UI model logic can be separated from JUCE components; otherwise use a manual smoke checklist.

### 2. Fast parameter updates can target the wrong layer

`DrumKitEngine::updateSlotParameters` accepts `slotId`, `artName`, and `layerIndex`, but currently ignores `artName` and counts instrument player layers only by `slotId`. `PadContextPopup` computes layer index within the popup's current articulation, while `InstrumentConfigPanel` computes layer index across selected rows for the whole slot. These assumptions can diverge for slots with multiple articulations or layered additional instrument players.

Files:

- `source/Engine/DrumKitEngine.h`
- `source/Engine/DrumKitEngine.cpp`
- `source/UI/PadContextPopup.cpp`
- `source/UI/InstrumentConfigPanel.cpp`

Plan:

- Define the canonical identity for an editable map layer: instrument-slot ID, articulation-slot filter, library path, and map layer ordinal within that exact instrument-slot and articulation-slot group.
- Make `updateSlotParameters` use the articulation-slot filter, or replace `layerIndex` with a stable map-layer ID generated from the parsed map.
- Keep `PadContextPopup` and `InstrumentConfigPanel` using the same layer-index helper.
- Verify with a slot that has at least two articulations and multiple layers on one articulation.

### 3. Drum pad flash feedback is currently incomplete

`DrumPadGrid::flashPad` is still a no-op, and `DrumPad::flash` sets `flashAlpha` without any fade-out timer. This means the planned visual feedback on incoming or injected MIDI is not actually wired through.

Files:

- `source/UI/DrumPad.h`
- `source/UI/DrumPad.cpp`
- `source/UI/DrumPadGrid.h`
- `source/UI/DrumPadGrid.cpp`
- `source/PluginEditor.cpp`

Plan:

- Track each pad's slot id inside `DrumPadGrid` and implement `flashPad(slotId, velocity)`.
- Add a small `juce::Timer` based fade in `DrumPad`, with a fixed 200-300 ms decay.
- Wire UI-injected MIDI and incoming MIDI learn paths only where appropriate, so pad feedback does not require rebuilding the grid.

### 4. Pad context library picker positioning is fragile

`PadContextPopup::showSourcePicker` creates the picker as a child of the popup and comments that it may overflow beyond popup bounds. In practice, this can be clipped or awkward because the overlay only covers the popup local bounds. The picker can also open downward even when the row is near the bottom of the editor.

Files:

- `source/UI/PadContextPopup.cpp`
- `source/PluginEditor.cpp`

Plan:

- Host nested popups in the editor-level overlay, or use `juce::CallOutBox`/a shared popup host component.
- Clamp picker bounds against the editor viewport, not only the parent popup.
- Reuse the same popup host logic for `InstrumentConfigPanel` and `PadContextPopup`.

### 5. Gain and mic bleed edits do not have the same immediate-audition model

Tuning, attack, and decay use `updateSlotParameters` for immediate feedback. Gain and mic bleed only mark the map text dirty, and flip buttons call a full `useInstrumentMap`. This makes the popup feel inconsistent: some edits are heard immediately, others are only reflected on full map application or popup close.

Files:

- `source/UI/PadContextPopup.cpp`
- `source/Engine/DrumKitEngine.h`
- `source/Engine/DrumKitEngine.cpp`
- `source/Engine/InstrumentPlayer.*`

Plan:

- Decide which parameters can be applied atomically without sample reload: gain, phase, LR flip, and mic bleed should be reviewed.
- Extend the atomic update API only for parameters that are safe to change live.
- Keep library-assignment changes on the full map path because they can require sample loading.
- Debounce preview playback so slider/pot drags do not trigger repeated previews.

## Performance Plan

### 1. Debounce settings persistence

`EngineSettingsPanel::onSettingChanged` writes `config.yaml` through `engine.saveSettings()` on every pot value change. Rotary dragging can produce many disk writes.

Plan:

- Update in-memory settings immediately.
- Save on `onDragEnd`, or use a short dirty timer such as 300-500 ms.
- Keep grid size changes immediate because layout feedback is useful.
- Consider saving RR lock from the sidebar too, or explicitly treat it as session-only.

### 2. Reduce full map text generation during slider drags

`InstrumentConfigPanel::applyRowParametersFast` uses `updateSlotParameters`, but still calls `generateMapText` and pushes `onMapTextChanged` on every value change. For large maps this repeatedly walks every row and writes ValueTree state text.

Plan:

- Apply atomic engine updates immediately for audible feedback.
- Mark the row/table dirty during drag.
- Flush map text on drag end, library-assignment change, page switch, or a debounce timer.
- Share dirty/flush behavior with `PadContextPopup::flushMapText`.

### 3. Avoid rebuilding complete UI component trees on common page changes

`PluginEditor` repopulates `DrumPadGrid`, `MidiMapTable`, and `InstrumentConfigPanel` in several page-change and map-change paths. `DrumPadGrid::populate` clears and recreates every pad component.

Plan:

- Add dirty flags for layout changes, instrument map changes, and MIDI map changes.
- Update existing pad components in place when only pad mode, velocity, or gap changes.
- Rebuild tables only when their backing layout/map actually changed.
- Keep the first pass conservative: do not cache stale `KitLayout` pointers across a layout replacement unless ownership is clear.

### 4. Debounce library picker filtering

`SourcePickerPopup::rebuildFiltered` scans all library items and repaints the list on each text edit. This is fine for small libraries, but can become sluggish with large kits.

Plan:

- Cache lowercase display text in `SourceItem` or a parallel view model.
- Add a 75-125 ms debounce timer for filter text changes.
- Avoid `listBox.repaint()` when `updateContent()` is enough.
- Keep related-type filtering synchronous because it is cheap and user initiated.

### 5. Keep library preview non-blocking

`previewSourceItem` uses async sample loading after selecting a sample, but it may scan instrument samples on the UI thread when articulation specs are empty.

Plan:

- Pre-scan library items when building the library list, or move scan work behind the async loader path.
- Cache preview targets by library-item index and velocity band.
- Keep preview clicks responsive even when the first preview of library content is cold.

## Repeated Code and Component Extraction

### 1. Theme and token layer

Create a dedicated theme module instead of scattering colours, font sizes, row heights, margins, and control sizes through each component.

Proposed files:

- `source/UI/UITheme.h`
- `source/UI/UITheme.cpp` only if non-trivial functions are needed

Suggested shape:

```cpp
namespace UITheme {
namespace Colours {
    const juce::Colour bgApp;
    const juce::Colour bgPanel;
    const juce::Colour bgPanelRaised;
    const juce::Colour borderSubtle;
    const juce::Colour textPrimary;
    const juce::Colour textSecondary;
    const juce::Colour textMuted;
    const juce::Colour accent;
    const juce::Colour accentWarning;
    const juce::Colour midiLearnIdle;
    const juce::Colour midiLearnActive;
    juce::Colour instrumentType(KitLib::InstrumentSpec::Type type);
}

namespace Fonts {
    juce::Font label();
    juce::Font labelBold();
    juce::Font small();
    juce::Font table();
    juce::Font mono();
}

namespace Spacing {
    constexpr int xs = 2;
    constexpr int sm = 4;
    constexpr int md = 8;
    constexpr int rowCompact = 18;
    constexpr int rowNormal = 22;
    constexpr int rowLarge = 26;
}

namespace Sizes {
    constexpr int sidebarWidth = 56;
    constexpr int statusBarHeight = 24;
    constexpr int modeButton = 44;
    constexpr int sidebarKnob = 40;
    constexpr int midiLearnButton = 22;
    constexpr int sliderTextBoxW = 40;
    constexpr int sliderTextBoxH = 20;
}
}
```

Migration order:

1. Add theme constants without changing behavior.
2. Move instrument type colours out of `DrumPad::getColourForType`.
3. Point `LookAndFeel` at `UITheme` colours.
4. Replace repeated `0xff333333`, `0xff888888`, `0xffcccccc`, red/yellow learn colours, and row height constants file by file.
5. Only after the mechanical migration, tune the actual palette.

### 2. Shared knob/pot component

There are three related implementations:

- `LabeledPot`: vertical title/rotary/value component.
- `HorizontalKnobBrick`: horizontal title/value/rotary component, currently unused outside its own files.
- `EngineSettingsPanel::PotEntry` and `Sidebar` create styled rotary sliders manually.
- `UIHelpers::createCompactPot` exists but is not currently used.

Plan:

- Create one reusable component, for example `ParameterPot`, with layout mode `Vertical`, `Horizontal`, and `SidebarCompact`.
- Move text parsing, display formatting, drag callbacks, and slider colour setup into that component.
- Replace `LabeledPot` and `HorizontalKnobBrick` with wrappers or remove the unused one after migration.
- Make `EngineSettingsPanel` use the shared component plus a reset button, instead of duplicating pot styling.
- Make `Sidebar` use the same styling path or a small `SidebarKnob` wrapper.

### 3. Shared MIDI note assignment control

`PadContextPopup` and `MidiMapTable` both manage two note buttons, two learn buttons, note picker launch, vector growth/removal, and learn-state colours.

Plan:

- Extract `MidiNoteAssignmentControl` that owns two note buttons and two learn buttons.
- Give it callbacks for note picker selection and MIDI learn start/stop.
- Keep the data write operation outside the component if direct `KitLayout::MidiNotes*` ownership is too risky.
- Use it in both `PadContextPopup` and `MidiMapTable`.

### 4. Shared popup overlay host

`InstrumentConfigPanel`, `PadContextPopup`, and `PluginEditor` all create small overlay classes with click-to-dismiss behavior.

Plan:

- Create a small `PopupHost` or `DismissOverlay` helper.
- Support editor-level hosting, local hosting, and dismissal callbacks.
- Use it for library pickers and pad context popups.
- This also fixes clipping and z-order edge cases.

### 5. Shared row/button styling helpers

Repeated patterns worth extracting after `UITheme` exists:

- Header label setup.
- Muted label setup.
- Table row label setup.
- Dark connected text button setup.
- Toggle button active/inactive colours.
- MIDI learn button idle/active colours.
- Small remove button setup.

Keep these as tiny helper functions. Avoid adding a large style abstraction until the theme tokens exist.

### 6. Split `UITypes.h`

`UITypes.h` currently mixes `SourceItem` with UI helper declarations and includes engine headers. As the UI grows, split it into clearer files.

Plan:

- `SourceItem.h`: temporary library-item model name and display helpers.
- `UIHelpers.h/.cpp`: small setup helpers and factories.
- `UITheme.h`: tokens and palette.
- Keep include dependencies narrow to reduce rebuild cost.

## Phased Implementation Order

### Phase 1: Fix current behavior bugs

- Refactor `InstrumentConfigPanel::addRow` so all add-layer paths use one row factory.
- Fix new layer add buttons so they work recursively.
- Correct atomic parameter addressing so `slotId`, `artName`, and layer identity agree between UI and engine.
- Implement `DrumPadGrid::flashPad` and `DrumPad` fade-out.
- Fix pad context nested library picker clipping and placement.

Verification:

- Add one layer from an original row, then add another from the new row.
- Edit tuning/attack/decay on multiple articulations of the same slot and verify the intended layer changes.
- Click pads and verify visual flash decays.
- Open the library picker from top and bottom popup rows.

### Phase 2: Add theme tokens without visual redesign

- Add `UITheme.h` with current colours, fonts, spacing, and sizes.
- Migrate `DrumPad`, `Sidebar`, `StatusBar`, `SourcePickerPopup`, and `LookAndFeel` first.
- Migrate larger files (`InstrumentConfigPanel`, `PadContextPopup`, `MidiMapTable`, `EngineSettingsPanel`) one at a time.
- Do not tune the palette while migrating; keep output visually equivalent.

Verification:

- Confirm no component changes size unexpectedly.
- Compare core pages: Zen, Zoned, Map, Inst, Raw, Conf.

### Phase 3: Extract shared components

- Extract `ParameterPot` or consolidate `LabeledPot` / `HorizontalKnobBrick` / `createCompactPot`.
- Extract `MidiNoteAssignmentControl`.
- Extract `DismissOverlay` / `PopupHost`.
- Add small style helpers for labels and buttons.
- Split `UITypes.h` into narrower headers.

Verification:

- Confirm all callbacks still fire exactly once.
- Confirm MIDI learn state clears when popups close.
- Confirm reset buttons and sidebar controls preserve existing behavior.

### Phase 4: Performance improvements

- Debounce settings saves.
- Dirty/debounce map text generation from `InstrumentConfigPanel` sliders.
- Avoid unnecessary grid and table rebuilds on page switches.
- Debounce library picker filtering and cache lowercase search text.
- Review the cold library-preview path for UI-thread scanning.

Verification:

- Drag settings pots and verify config saves once after the drag or debounce.
- Drag tuning/attack/decay and verify audio responds without ValueTree/map text churn on every tick.
- Switch pages repeatedly and verify no visible lag or stale tables.
- Type rapidly in the library filter on a large library.

### Phase 5: Polish and consistency

- Center and fit pad text consistently in Zen and Zoned pages.
- Add empty or disabled states for unavailable pads, missing library content, and empty library-picker filters.
- Give MIDI learn active state stronger contrast than only red/yellow text colour.
- Make popup dimensions responsive to editor size and content.
- Review labels for consistent abbreviations: `HH`, `VL`, `RR`, `Inst`, `Conf`, `Zone`.

Verification:

- Manual pass at minimum editor size and the default `1000x580` size.
- Check all visible text fits inside controls.
- Check keyboard/mouse interactions in each page.

## Suggested Acceptance Checklist

- No stale TODO item from `TODO06` or `TODO07` is implemented without checking current code first.
- New UI constants come from `UITheme` instead of new hardcoded hex colours or ad hoc sizes.
- Repeated control setup is moved only when at least two current users adopt the helper/component.
- Slider and pot drags do not write config files or full map text on every tiny movement.
- Library-assignment changes still use the full map path when sample loading is required.
- Atomic parameter changes update the correct slot/art/layer combination.
- UI remains buildable after each phase; user can run the CMake build manually per repo preference.

## Deferred / Out of Scope For This Plan

- A full visual redesign of the product.
- Large engine ownership refactors beyond what is needed for correct UI parameter addressing.
- Replacing JUCE controls wholesale.
- Adding a new UI testing framework before the behavior bugs and theme migration are stabilized.
