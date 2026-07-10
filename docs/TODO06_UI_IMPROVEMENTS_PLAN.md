# ✅ COMPLETED: TODO06: UI Improvements Plan (Mostly superseded by TODO10 and UI_REVIEW_PLAN)

## 1. Compact Instrument Config Layout

**Goal:** Remove per-articulation cards, flatten to to table with one row per articulation directly under the slot header. See screenshot: slot header row contains slot name, then each articulation is a single row with art label, source button, tuning/attack/decay — all inline.

### Current Layout
```
┌─ kick ─────────────────────────────────────────────┐
│ center                                             │
│   [NakedDrums/kick-22/center] ── T[slider] A[slider] D[slider] │
│   +                                                │
└────────────────────────────────────────────────────┘
```

### Target Layout
```
┌──────────────────────────────────────────────────────────────────────────────────────┐
| slot           | instrument                    | Tuning  | Attack   | Decay    |     |
---------------------------------------------------------------------------------------
| kick / center   | [NakedDrums/kick-22/center]  | [══] 0c | [══] -10 | [══] -10 | +   |
| snare1 / center | [NakedDrums/snare-X1/center] | [══] 0c | [══] -10 | [══] -10 | +   |
| snare1 / rim    | [NakedDrums/snare-X1/rim]    | [══] 0c | [══] -10 | [══] -10 | + x | < - multi-layer row with remove button
| snare1 / rim    | [NakedDrums/snare-X2/rim]    | [══] 0c | [══] -10 | [══] -10 | + x | < - multi-layer row with remove button
... more rows for each articulation/layer ...
---------------------------------------------------------------------------------------
```

### Changes
- Remove `ArticulationCard` nesting — each articulation becomes a direct row under its `SlotCard`
- Slot header row shows slot name; subsequent rows show: art label (compact, colored), source button, T/A/D sliders, [x] remove, [+] add layer
- Slot card border only around the group of rows belonging to one slot
- No separate art header/section — art name is an inline label at row start
- Row height: ~22px (same as current layer height)
- Slot header: show only slot name, ~18px
- Multi-layer: additional rows under the same art label (art label only on first row)

### Files
- `source/UI/InstrumentConfigPanel.h` — flatten structure, remove ArticulationCard or make it a simple row
- `source/UI/InstrumentConfigPanel.cpp` — update layout, populate, generateMapText

---

## 2. Source Picker with Preview & Confirm

**Goal:** Clicking a row in the source picker popup plays a preview of that instrument/articulation. A checkmark button confirms selection (instead of single-click select). Click position X-axis determines velocity.

### Behavior
- Popup rows: click plays preview at velocity proportional to X position (left=soft, right=hard)
- Row shows instrument name + small [✓] confirm button on right
- Clicking [✓] applies the selection (current single-click behavior moves here)
- Clicking the row text area = preview only (no selection change)
- Preview: send a MIDI note-on to the engine for the source instrument's articulation
- ESC or click-outside dismisses without changing

### Preview Mechanism
- Need a `previewSound(instSpec, artSpec, velocity)` method on the engine
- Or simpler: temporarily map the slot to the preview source, send note-on, then restore
- Simplest: engine exposes `playPreview(KitLib::InstrumentSpec*, artNameId, velocity)` that plays directly without affecting the current mapping

### Changes
- `source/Engine/DrumKitEngine.h/.cpp` — add `playPreview()` method
- `source/UI/InstrumentConfigPanel.h/.cpp` — update SourcePickerPopup: split click into preview vs confirm, add confirm button per row, compute velocity from mouse X

---

## 3. Settings View Scrollable + Reset Buttons

**Goal:** Settings view content in a Viewport (scrollable). Each slider gets a small [x] button to reset to factory default.

### Changes
- Wrap Settings content in `juce::Viewport`
- Each slider row: `[label] [slider] [x]`
- [x] button resets the associated parameter to its default value
- Default values stored as constants or retrieved from `AudioProcessorValueTreeState` parameter ranges

### Files
- `source/UI/SettingsPanel.h/.cpp` (or wherever settings view is implemented)

---

## 4. Sidebar Knob Value Display

**Goal:** vel, hh, rr knobs on sidebar show their current value as small text after knob label like HH 78, VL 127, RR 3 (rr * if default (all rr)). The text updates in real time as the knob is turned.
knob should have clear accent color (some reddish already used in design) indicator line.

### Changes
- Add value display to the sidebar knob components
- Update display on value change
- Use accent color for the knob indicator line
- reorder knobs (hh, vl, rr)
- move ? inspector button before knobs and make it visible only in debug mode
- make settings button 'Conf' instead of gear emoji
 
### Files
- `source/UI/Sidebar.h/.cpp`

---

## 5. Zone View Pad Context Menu

**Goal:** Right-click on a pad in Zone view opens a popup with pad parameters — simplified visual design combining info from MIDI map and instrument config.

### Popup Content
- Slot name, articulation name
- Current source instrument (with change button → opens source picker)
- MIDI note number
- Tuning / Attack / Decay sliders
- Preview button (plays the articulation)
- Apply / Close buttons

### Design
- Rounded rect popup overlay (similar to source picker style)
- Darker background, compact layout
- Dismiss on click-outside or close button

### Changes
- `source/UI/DrumPadGrid.h/.cpp` — add right-click handler, create popup component
- New component or inline class for the pad params popup
- Wire to engine for preview and parameter changes

---

## Implementation Order

1. **Compact Instrument Config** — most impactful visual change, simplifies code
2. **Sidebar Knob Values** — small, quick win
3. **Settings Scrollable + Reset** — straightforward
4. **Source Picker Preview** — requires engine preview API
5. **Zone Pad Context Menu** — most complex, depends on preview API from #4

