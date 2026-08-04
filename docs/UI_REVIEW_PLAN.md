# UI Code Review & Refactoring Plan

> Generated from source code review. Code is authoritative when it conflicts with this document.

---

## 1. Bugs & Correctness Issues

### 1.1 DrumPad: `buildDisplayOrder()` recomputed on every click
- **File**: `DrumPad.cpp` → `getZoneIndexAt()` calls `buildDisplayOrder()` each time
- **Problem**: Every `mouseDown` on the Zoned page allocates a new `std::vector`, sorts it, then discards it. The same data is already computed during `paintZoned()`.
- **Fix**: Cache `displayOrder` as a member, recompute in `setSlot()` / `setPadMode()` only.

### 1.2 DrumPad: flash timer keeps running on invisible/empty pads
- **File**: `DrumPad.cpp` → `flash()` starts timer unconditionally
- **Problem**: If a pad with no slot is flashed (or a pad that's since been recycled), the timer still fires and calls `repaint()`.
- **Fix**: Guard `flash()` with `if (!this->slotPtr) return;`.

### 1.3 ✅ PadContextPopup & MidiMapTable: Duplicate `applyNoteToSlot` logic
- **Files**: `PadContextPopup.cpp:19-30`, `MidiMapTable.cpp:110-122`
- **Problem**: Identical note-slot logic (resize vector, insert/erase, sync buttons) duplicated verbatim. Divergence risk.
- **Completed**: `MidiNoteAssignmentControl::applyNoteToSlot` owns the shared MIDI-note update.

### 1.4 InstrumentConfigPanel: raw `new` for `TableRow`
- **File**: `InstrumentConfigPanel.cpp` → `addRow()` uses `new TableRow()` with `OwnedArray`
- **Problem**: Not a leak (OwnedArray handles it), but inconsistent with the rest of the codebase using `std::make_unique`. Minor style issue.
- **Fix**: Use `std::make_unique<TableRow>()` and `this->tableRows.add(std::move(...))`.

### 1.5 PadContextPopup: `const_cast` on articulation slot
- **File**: `PadContextPopup.cpp:74-80` - `const_cast<...&>(slot.articulations[...])`
- **Problem**: Modifies data through a `const` reference parameter. Caller passes `const InstrumentSlot*` from the layout, and the popup mutates MIDI notes directly through it. Fragile and UB-adjacent if the data is ever truly const.
- **Fix**: Accept a non-const pointer or copy the notes for local editing, then apply back via the engine.

### 1.6 MidiNoteAssignmentControl: unused variable `noteButtonHeight`
- **File**: `MidiNoteAssignmentControl.cpp:38` - computed but only consumed by `juce::ignoreUnused`
- **Fix**: Remove the variable and the `ignoreUnused` call.

---

## 2. Performance Issues

### 2.1 Font objects recreated every call in UITheme::Fonts
- **File**: `UITheme.h` - `label()`, `small()`, `mono()`, etc. return new `juce::Font` each call
- **Problem**: `juce::Font` construction involves heap allocation for the shared internal representation. Called dozens of times per paint cycle across all components.
- **Fix**: Cache as `static const` or `inline const` members instead of inline functions. E.g.:
  ```cpp
  inline const juce::Font label = juce::Font(juce::FontOptions(10.0f));
  ```

### 2.2 MidiMapTable label font set redundantly in `layoutContent()`
- **File**: `InstrumentConfigPanel.cpp` → `layoutContent()` re-sets font and colour on every row's slot/art label during every layout pass.
- **Problem**: Font/colour were already set in `addRow()`. Re-setting on every `resized()` is wasted work.
- **Fix**: Remove the redundant `setFont()` / `setColour()` calls from `layoutContent()`.

### 2.3 MidiMapTable `buildRows()`: repeated font setup for every row
- **File**: `MidiMapTable.cpp` - each row sets identical font (4 lines × N rows).
- **Problem**: Lots of repeated identical work.
- **Fix**: Configure font/colour once via a shared helper, or set on the parent and inherit.

### 2.4 LibraryAssignmentPicker: `cachedLowerDisplayLabels` duplicates strings
- **File**: `LibraryAssignmentPicker.cpp` constructor - lowercases all display labels upfront.
- **Note**: This is actually a good optimization for filtering. No change needed, just noting it's well done.

### 2.5 DrumPadGrid `resized()`: heap-allocates 4 vectors every time
- **File**: `DrumPadGrid.cpp` → `resized()` creates `columnWidths`, `rowHeights`, `columnOffsets`, `rowOffsets` as `std::vector`.
- **Fix**: Use `std::array` or member vectors to avoid repeated allocation. Grid size is bounded (max 12×8).

---

## 3. Reducing Repeated Code

### 3.1 ✅ Extract `configureToggleButton()` helper
- **Files**: `PadContextPopup.cpp` - flipLR and flipPhase buttons have identical 6-line colour setup.
- **Pattern**:
  ```cpp
  button->setClickingTogglesState(true);
  button->setColour(buttonColourId, bgControl);
  button->setColour(buttonOnColourId, accentWarning);
  button->setColour(textColourOffId, textMuted);
  button->setColour(textColourOnId, textPrimary);
  button->setConnectedEdges(...);
  ```
- **Completed**: `UIComponentStyle::configureToggleButton()` owns the shared styling.

### 3.2 ✅ Extract `configureMonoTextEditor()` helper
- **File**: `PluginEditor.cpp` - `referencePanel` and `instrumentMapEditor` share identical 4-line setup (font, bg colour, text colour, multi-line).
- **Completed**: `UIComponentStyle::configureMonoTextEditor()` owns the shared styling.

### 3.3 Consolidate `addPot` / `addIntPot` in EngineSettingsPanel
- **File**: `EngineSettingsPanel.cpp` - 90% identical code between `addPot()` and `addIntPot()`.
- **Fix**: Merge into a single template or use a variant/union for the target pointer. The only difference is `float*` vs `int*` and the interval.

### 3.4 ✅ Deduplicate `applyNoteToSlot` across PadContextPopup and MidiMapTable
- See bug 1.3 above. Both classes have identical implementations.
- **Completed**: The operation is a static method of the concrete `MidiNoteAssignmentControl` owner:
  ```cpp
  void MidiNoteAssignmentControl::applyNoteToSlot(KitLayout::MidiNotes& notes, int slotIdx, int note);
  ```

### 3.5 ✅ Deduplicate `sentinelTextFn` lambda
- **Files**: `InstrumentConfigPanel.cpp:146`, `PadContextPopup.cpp:264`
- **Pattern**: `[](double value) -> juce::String { return value < 0.0 ? "--" : juce::String(static_cast<int>(value)); }`
- **Completed**: `CompactParameterControl::sentinelValueText()` owns this control-specific formatting.

### 3.6 Deduplicate slider `onValueChange` / `onDragEnd` wiring
- **Files**: `InstrumentConfigPanel.cpp` (tuning/attack/decay sliders) and `PadContextPopup.cpp` (tuning/attack/decay/gain pots)
- **Pattern**: Each slider has identical `onValueChange = [this, row] { applyFast(row); }` and `onDragEnd = [this, row] { flush(); preview(); }`.
- **Fix**: Extract a `wireSliderCallbacks()` helper that takes the lambda targets.

### 3.7 `LabeledPot` class is now empty
- **Files**: `LabeledPot.h`, `LabeledPot.cpp`
- **Problem**: `LabeledPot` adds nothing over `ParameterPot` - the constructor just forwards all args. It's a vestigial abstraction.
- **Fix**: Either give it a purpose (maybe force vertical layout) or replace all uses with `ParameterPot` directly and delete the class.

### 3.8 ✅ MidiMapTable row label setup is repeated per row
- **File**: `MidiMapTable.cpp` → `buildRows()` - 4 identical lines per label (setFont, setColour, setJustification).
- **Completed**: `UIComponentStyle::configureTableRowLabel()` owns the shared styling.

---

## 4. Theme & Style Consistency

### 4.1 Move hardcoded sizes to UITheme::Sizes
Several layout constants are defined as local `constexpr` in individual files instead of in `UITheme`:

| Constant | Location | Suggested UITheme name |
|---|---|---|
| `popupPadding = 8` | PadContextPopup.cpp | `Sizes::popupPadding` |
| `potW = 50`, `potH = 56` | PadContextPopup.cpp | `Sizes::potWidth`, `Sizes::potHeight` |
| ✅ `libraryArticulationWidth` | PadContextPopup.cpp | `Sizes::libraryArticulationCardWidth` |
| `layerRowH` | PadContextPopup.cpp | `Sizes::layerRowHeight` |
| `rowHeight = 22` | InstrumentConfigPanel.h | Already uses `Spacing::rowNormal` ✓ |
| `instColWidth = 90` | MidiMapTable.h | `Sizes::midiMapInstCol` |
| `artColWidth = 100` | MidiMapTable.h | `Sizes::midiMapArtCol` |

**Fix**: Consolidate into `UITheme::Sizes` so they can be tuned from one place.

### 4.2 Inconsistent font usage
- `MidiMapTable.cpp` uses `UITheme::Fonts::table().withHeight(11.0f)` in several places instead of defining a dedicated `Fonts::tableSmall()`.
- `StatusBar.cpp` also uses `UITheme::Fonts::table().withHeight(11.0f)`.
- `PadContextPopup.cpp` uses `UITheme::Fonts::labelBold().withHeight(12.0f)`.
- `PluginEditor.cpp` also uses `UITheme::Fonts::labelBold().withHeight(12.0f)`.

**Fix**: Add to `UITheme::Fonts`:
```cpp
inline juce::Font tableSmall() { return table().withHeight(11.0f); }
inline juce::Font labelBoldMedium() { return labelBold().withHeight(12.0f); }
```

### 4.3 Hard-coded colour values outside UITheme
- Confirmed: No hard-coded colour hex values outside `UITheme.h` ✓ (good discipline).
- The `LookAndFeel.cpp` uses some UITheme colours but also calls `brighter()`/`darker()` with magic numbers. This is acceptable for L&F rendering.

### 4.4 `LookAndFeel.h` stores static colours redundantly
- `darkestGrey` and `darkerGrey` duplicate values from `UITheme::Colours`.
- **Fix**: Remove them and use `UITheme::Colours::bgPanelRaised` / `UITheme::Colours::borderMid` directly.

---

## 5. Architecture & Structural Improvements

### 5.1 PluginEditor is a god class
- **File**: `PluginEditor.cpp` - 450 lines, manages 6 pages, popup lifecycle, MIDI learn forwarding, engine callbacks, dirty flags.
- **Problem**: Hard to maintain, violates SRP.
- **Fix (incremental)**:
  1. Extract popup management (show/dismiss/overlay) into a `PopupManager` helper.
  2. Extract MIDI learn wiring into a `MidiLearnCoordinator`.
  3. Extract dirty-flag refresh logic into a `ViewRefreshController`.
  4. Consider giving each page component a common interface: `activate()`, `deactivate()`, and `refresh()`.

### 5.2 DismissOverlay could be more reusable
- **File**: `DismissOverlay.h` - minimal but used identically in `PadContextPopup`, `InstrumentConfigPanel`, and `PluginEditor`.
- **Fix**: Add a static helper `DismissOverlay::show(parent, onDismiss)` that creates, positions, and returns the overlay.

### 5.3 Viewport + ContentComponent pattern is repeated
- `InstrumentConfigPanel`, `EngineSettingsPanel`, and `MidiMapTable` all use:
  ```cpp
  juce::Viewport viewport;
  juce::Component contentComponent;
  viewport.setViewedComponent(&contentComponent, false);
  viewport.setScrollBarsShown(true, false);
  ```
- **Fix**: Create a `ScrollablePanel` base class that owns the viewport + content and provides `layoutContentArea()`.

### 5.4 PadContextPopup: `LayerContentComponent` is a paint-only inner class
- **File**: `PadContextPopup.h` (line ~100+) - inner class just overrides `paint()`.
- **Fix**: Use a lambda-based paintOverChildren or a lightweight `PaintableComponent`.

---


## 7. Proposed Refactoring Phases

### Phase 1: Quick Wins (low risk, high value)
- [ ] 1.1 - Cache `displayOrder` in DrumPad
- [ ] 1.6 - Remove unused `noteButtonHeight`
- [ ] 2.1 - Cache fonts in UITheme
- [ ] 3.5 - Extract `sentinelTextFn`
- [ ] 4.4 - Remove `darkestGrey`/`darkerGrey` from LookAndFeel

### Phase 2: Deduplication (medium risk)
- [ ] 1.3 / 3.4 - Extract shared `applyMidiNoteToSlot()`
- [ ] 3.1 - Extract `configureToggleButton()`
- [ ] 3.2 - Extract `configureMonoTextEditor()`
- [ ] 3.3 - Merge `addPot`/`addIntPot`
- [ ] 3.7 - Evaluate removing `LabeledPot`
- [ ] 3.8 - Extract `configureTableRowLabel()`
- [ ] 4.1 - Move sizes to UITheme
- [ ] 4.2 - Add derived font helpers

### Phase 3: Architecture (higher risk, plan carefully)
- [ ] 5.1 - Split PluginEditor responsibilities
- [ ] 5.3 - Create `ScrollablePanel` base
- [ ] 2.2 / 2.3 - Remove redundant layout-time property setting

### Phase 4: Polish
- [ ] 1.2 - Guard flash on empty pads
- [ ] 1.5 - Fix const_cast in PadContextPopup
- [ ] 2.5 - Use stack arrays in DrumPadGrid::resized()
