# UI Redesign Plan: Tabbed Layout with Drum Pads

## Overview

Replace current two-panel layout (reference + instrument map) with a tabbed UI:
- **Left sidebar** — mode selector icons + global controls (velocity, hihat openness, RR lock)
- **Main area** — content depends on selected mode
- **Status bar** — bottom strip showing engine status, last action result, loading progress

## Current State

- `PluginEditor` has: reference panel (left 1/3), instrument map editor (right 2/3), settings toggle, status label, apply button
- `EngineSettingsPanel` — 16 sliders in 3 groups
- `LookAndFeel` — dark theme already in `source/UI/`
- Window size: 1000×580

---

## Layout Structure

```
┌─────────┬──────────────────────────────────────┐
│ SIDEBAR │          MAIN AREA                   │
│ (56px)  │  (content depends on selected mode)  │
│         │                                      │
│ [Zen]   │                                      │
│ [Zoned] │                                      │
│ [Map]   │                                      │
│ [Raw]   │                                      │
│ [⚙]    │                                      │
│         │                                      │
│─────────│                                      │
│ Vel     │                                      │
│ [○]     │                                      │
│ HH      │                                      │
│ [○]     │                                      │
│ RR      │                                      │
│ [○]     │                                      │
│ [🔍]   │                                      │
├─────────┴──────────────────────────────────────┤
│ STATUS BAR (24px)                              │
└────────────────────────────────────────────────┘
```

### Sidebar (56px wide)
- **Top section** — 5 mode buttons (icon-only, square, 44×44px), vertically stacked:
  1. Zen (grid icon)
  2. Zoned (split-grid icon)
  3. Map (table icon)
  4. Raw (code/text icon)
  5. Settings (gear icon)
- **Bottom section** — global controls:
  - Velocity knob (rotary, small, 40px)
  - HiHat Openness knob (rotary, small, 40px)
  - Round-Robin Lock knob (rotary or stepped, values: 0=all, 1=first, 2=second, etc.)
  - Inspect button (🔍, small)
- Active mode button highlighted with accent colour
- Sidebar background slightly darker than main area

### Status Bar (24px tall)
- Left-aligned: last instrument map status text (from `getLastInstrumentMapStatus()`)
- Right-aligned: sample loading progress / voice count / engine info
- Subtle top border line

---

## Mode 1: Zen View (Default)

Grid of drum pads, no padding between them.

```
┌────────┬────────┬────────┬────────┬────────┬────────┐
│ kick1  │ snare1 │ hihat1 │ tom1   │ tom2   │ tom3   │
│        │        │        │        │        │        │
├────────┼────────┼────────┼────────┼────────┼────────┤
│ crash1 │ crash2 │ ride1  │ ride2  │ china1 │ splash1│
│        │        │        │        │        │        │
├────────┼────────┼────────┼────────┼────────┼────────┤
│ perc1  │ perc2  │ perc3  │ perc4  │ perc5  │ perc6  │
│        │        │        │        │        │        │
├────────┼────────┼────────┼────────┼────────┼────────┤
│ aux1   │ aux2   │ aux3   │ aux4   │ aux5   │ aux6   │
│        │        │        │        │        │        │
└────────┴────────┴────────┴────────┴────────┴────────┘
```

- Grid size: **columns × rows** (default 6×4, configurable in Settings mode)
- Pads populated from current `KitLayout::instrumentSlots` in order
- Each pad shows:
  - Instrument slot display name (or id if no display name)
  - Brief type indicator (colour-coded by `InstrumentSpec::Type`: kick=red, snare=orange, hihat=yellow, tom=blue, ride=cyan, crash=purple, etc.)
  - On MIDI note-on: pad flashes/lights up proportional to velocity
- Pads are clickable: sends MIDI note-on (first articulation's first note) at sidebar velocity knob value
- Empty slots (beyond instrument count) shown as dark empty cells
- No gaps/padding between pads — they tile edge-to-edge

### Implementation
- New component: `source/UI/DrumPadGrid.h/.cpp`
- `DrumPadGrid` owns a flat `std::vector<DrumPad>` laid out in grid
- `DrumPad` — single pad component, knows its `KitLayout::InstrumentSlot*`
- `DrumPadGrid` listens to engine for note-on events to trigger flash animations
- Grid dimensions stored in `EngineSettings` or a separate UI config: `int padGridColumns = 6; int padGridRows = 4;`

---

## Mode 2: Zoned View

Same grid layout as Zen, but each pad is horizontally split into articulation zones.

```
┌─────────────────┐
│ kick1           │  ← 1 articulation = no split
│ [center]        │
├─────────────────┤
│ snare1 [center] |  ← 4 articulations = 4 zones
│ [rim]           │
│ [rimshot]       │
│ [xstick]        │
├─────────────────┤
│ hihat1 [edge]   │  ← 2 articulations = 2 zones
│ [bow]           │
├─────────────────┤
│ ride1 [bow]     │  ← 3 articulations = 3 zones
│ [bell]          │
│ [edge]          │
└─────────────────┘
```

- Each pad's vertical height divided by number of articulations
- Zone height can be proportional to articulation "importance" (configurable later; initially equal)
- Each zone shows articulation code name
- Zone click → sends that specific articulation's MIDI note
- Zone flash on corresponding MIDI note-on
- Thin 1px divider between zones within a pad, thicker 1px border between pads

### Implementation
- Reuse `DrumPadGrid` with a render mode flag: `enum class PadMode { Zen, Zoned }`
- `DrumPad::paintZoned()` draws sub-zones
- Articulation list from `KitLayout::InstrumentSlot::articulations`

---

## Mode 3: Map Mode

Table view for MIDI note assignment per articulation.

```
┌────────────┬──────────────┬────────────────────────────────────┐
│ Instrument │ Articulation │ MIDI Note (learn)                  │
├────────────┼──────────────┼────────────────────────────────────┤
│ kick1      │ center       │ [B0 / 35  ▾] (●)  [B1 / 36  ▾] (●) │
├────────────┼──────────────┼────────────────────────────────────┤
│ snare1     │ center       │ [D1 / 38  ▾] (●)  [-- / --  ▾] (●) │
│            │ rim          │ [E1 / 40  ▾] (●)  [-- / --  ▾] (●) │
│            │ rimshot      │ [-- / --  ▾] (●)  [-- / --  ▾] (●) │
│            │ xstick       │ [C#1 / 37 ▾] (●)  [-- / --  ▾] (●) │
├────────────┼──────────────┼────────────────────────────────────┤
│ hihat1     │ bow          │ [F#1 / 42 ▾] (●)  [-- / --  ▾] (●) │
│            │ bell         │ [-- / --  ▾] (●)  [-- / --  ▾] (●) │
│            │ edge         │ [A#1 / 46 ▾] (●)  [-- / --  ▾] (●) │
│            │ CC: pedal    │ [CC 4     ▾] (●)  [CC 1     ▾] (●) │
└────────────┴──────────────┴────────────────────────────────────┘
```

- Scrollable table, full width of main area
- Columns:
  1. **Instrument** — slot id/display name (merged cells or first-row-only for each instrument)
  2. **Articulation** — articulation code; CC controllers shown separately with "CC:" prefix
  3. **MIDI Note** — 2 ComboBox dropdowns showing `NoteName/NoteNumber` (e.g. "C3 / 60"), all 128 MIDI notes + "-- / --" for unassigned eachwith a red circle button for MIDI learn
- Rows grouped by instrument with alternating row background for readability
- Changes apply immediately to users version of `KitLayout` and engine (no separate "Apply" button) but also saved to `config.yaml` for persistence
- Auto-apply on dropdown change
- reset to defaults option (revert to default MIDI notes based on current `KitLayout`)

### Implementation
- New component: `source/UI/MidiMapTable.h/.cpp`
- `MidiMapTable` reads from active `KitLayout`
- MIDI learn: engine calls back when note received; `MidiMapTable` has an `activeLearningRow` index
- ComboBox items: generate from MIDI note names (C-2..G8 / 0..127)
- Changes write back to `KitLayout::InstrumentSlot::ArticulationTrigger::notes`

---

## Mode 4: Raw Mode

Current instrument map + reference panels moved here verbatim.

```
┌─────────────────────────┬──────────────────────────┐
│ Reference               │ Instrument Map           │
│ (read-only text)        │ (editable text area)     │
│                         │                          │
│ aasimonster2/kick-22:   │ kick1:aasimonster2/...   │
│   center [36,35]        │ snare1:aasimonster2/...  │
│   ...                   │ hihat1:aasimonster2/...  │
│                         │                          │
│                         │ [Apply instrument map]   │
└─────────────────────────┴──────────────────────────┘
```

- Left 1/3: `referencePanel` (TextEditor, read-only, monospaced)
- Right 2/3: `instrumentMapEditor` (TextEditor, editable) + `applyButton`
- Exactly the current layout, just relocated into this mode tab
- No changes to functionality

### Implementation
- Move existing `referencePanel`, `referencePanelLabel`, `instrumentMapEditor`, `instrumentMapLabel`, `applyButton` to be visible only in Raw mode
- They remain owned by `PluginEditor`, just shown/hidden based on mode

---

## Mode 5: Instrument Config (Inst)

Visual editor for instrument slot → sample library mapping, per-slot articulation selection, tuning, attack/decay, and layer configuration. GUI version of manual text editing in Raw mode.

### Layout

```
┌─ kick1 ───────────────────────────────────────────────────────────────────────┐
│ ┌─ center ────────────────────────────────────────────────────────────────┐   │
│ │ Inst: [ aasimonster2/kick-22/center  ▾]  Tuning: [═══○═══] 0 cents  [x] │   │
│ │ Attack: [═══○═══] -- ms  Decay:  [═══○═══] -- ms OtherParam: ...        │   │
│ │ [add layer]                                                             │   │
│ └─────────────────────────────────────────────────────────────────────────┘   |
└───────────────────────────────────────────────────────────────────────────────┘
┌─ snare1 ──────────────────────────────────────────────────────────────────────┐                                                                     │
│ ┌─ center ────────────────────────────────────────────────────────────────┐   │
│ │ Inst: [ nakeddrums/snare-X1/center  ▾]   Tuning: [═══○═══] 0 cents  [x] │   │
│ │ Attack: [═══○═══] -- ms  Decay:  [═══○═══] 50 ms OtherParam: ...        │   │
│ │                                                                         │   │
│ │ Inst: [ aasimonster2/snare-14/center  ▾] Tuning: [════○══] 12 cents [x] │   │
│ │ Attack: [═══○═══] 20 ms  Decay:  [═══○═══] -- ms OtherParam: ...        │   │
│ │ [add layer]                                                             │   │
│ └─────────────────────────────────────────────────────────────────────────┘   |
│ ┌─ crosstick ─────────────────────────────────────────────────────────────┐   │
│ │ Inst: [ nakeddrums/snare-X1/xstick  ▾]   Tuning: [═══○═══] 0 cents  [x] │   │
│ │ Attack: [═══○═══] -- ms  Decay:  [═══○═══] 0 ms  OtherParam: ...        │   │
│ │ [add layer]                                                             │   │
│ └─────────────────────────────────────────────────────────────────────────┘   |
...other articulations
└───────────────────────────────────────────────────────────────────────────────┘
... other instrument slots
│                                                                         │
│ ... (scrollable)                                                        │
└─────────────────────────────────────────────────────────────────────────┘
```

### Per-Slot Card Contents

Each instrument slot from `KitLayout::instrumentSlots` gets a collapsible card:

1. **Header** — slot id (bold), instrument type color indicator
2. **Kit dropdown** — lists all kits/instruments/articulations from `LibraryManager::getKits()` by `DrumKitSpec::meta.codeRaw`, `InstrumentSpec::Type` etc. Dropdown items show `kitRef/instCode/artCode` (e.g. "aasimonster2/kick-22/center") ordered by instrument relevance, then articulation (matching type first, then others)
4. **Tuning slider** — integer, range -2400..+2400 cents, default 0, interval 10. Maps to `InstrumentMapEntry::tuningCents`
5. **Attack slider** — integer, range 0..500 ms, interval 10, default unset (--). Maps to `InstrumentMapEntry::attackTimeMs`
6. **Decay slider** — integer, range 0..5000 ms, interval 10, default unset (--). Maps to `InstrumentMapEntry::decayTimeMs`
7. **Add layer button** — adds another set of dropdown + params for layering multiple samples on the same slot/articulation with different settings (e.g. multiple kick samples layered together)

### Behaviour

- On any change → rebuild `InstrumentMap` entries from UI state → call `useInstrumentMap()` with generated text → update status bar
- "Unset" values for tuning/attack/decay: slider at a sentinel position or a checkbox "override" toggle next to each
- Cards are scrollable in a `juce::Viewport`
- Each card has a subtle border matching `LookAndFeel::darkerGrey`

### Data Flow

```
KitLayout::instrumentSlots  →  populate cards (one per slot)
                                    ↓
LibraryManager::getKits()   →  kit dropdown items
Kit::instrumentSpecsMap     →  instrument dropdown items  
InstrumentSpec::articulationSpecs → articulation checkboxes
                                    ↓
User edits card             →  generate InstrumentMapEntry
                                    ↓
All entries                 →  serialize to instrument map text
                                    ↓
                            →  DrumKitEngine::useInstrumentMap(text)
                            →  statusBar.setStatusText(...)
                            →  drumPadGrid.populate(...)
```

### Implementation

- New component: `source/UI/InstrumentConfigPanel.h/.cpp`
- `InstrumentConfigPanel` contains a `juce::Viewport` with a content component
- Each slot card: `InstrumentSlotCard` (inner struct/class) owning the dropdowns, sliders, toggles
- Card reads from current `InstrumentMapEntry` (parsed from existing instrument map text) for initial values
- Card writes back by generating map text line: `slotId:kitRef/instCode:tuning=X,attack=Y,decay=Z,art=a,b,c`
- Needs references to: `DrumKitEngine` (for `LibraryManager`, `KitLayout`, `useInstrumentMap`)

### New Files

```
source/UI/
    ├── InstrumentConfigPanel.h/.cpp  ← NEW: scrollable list of per-slot cards
```

### Sidebar Addition

- Add new mode button between Map and Raw: "Inst" (instrument icon)
- `ViewMode::Instrument` added to enum after `Map`
- Total modes: Zen, Zoned, Map, **Inst**, Raw, Settings (6 buttons)

---

## Mode 6: Settings

All engine settings in a clean two-column layout with grouped sections.

```
┌─────────────────────────────────────────────────┐
│  ┌─ Aftertouch Choke ──────────────────────┐    │
│  │ Max Duck Depth        [═══════○═══] 0.70 │   │
│  │ Duck Time (sec)       [═══○═══════] 0.15 │   │
│  │ Release Time (sec)    [═○═════════] 0.05 │   │
│  │ Mute Tail (sec)       [═══════○═══] 0.30 │   │
│  │ Release Boost         [══○════════] 4.0  │   │
│  │ Mute Threshold Ratio  [══════○════] 0.01 │   │
│  └──────────────────────────────────────────┘   │
│  ┌─ Tail Shortening ──────────────────────┐     │
│  │ ...                                     │    │
│  └─────────────────────────────────────────┘    │
│  ┌─ Hi-Hat / Polyphony ───────────────────┐     │
│  │ ...                                     │    │
│  └─────────────────────────────────────────┘    │
│  ┌─ UI / Grid ────────────────────────────┐     │
│  │ Pad Grid Columns     [═══○═══════] 6    │    │
│  │ Pad Grid Rows        [═══○═══════] 4    │    │
│  └─────────────────────────────────────────┘    │
└─────────────────────────────────────────────────┘
```

- Existing `EngineSettingsPanel` content restructured into grouped sections
- Each section: group label (bold, slightly larger font) + rows of `label | slider | value`
- Two-column layout if window is wide enough (>800px main area), single column otherwise
- New section **"UI / Grid"** with:
  - `Pad Grid Columns` (int slider, 1–12, default 6)
  - `Pad Grid Rows` (int slider, 1–8, default 4)
- All sliders are linear (horizontal), matching current style
- Grouped with visual border or subtle background colour per group

### Implementation
- Refactor `EngineSettingsPanel` to support grouped sections with headers
- Add grid size settings to `EngineSettings` (or separate `UISettings`)
- Grid size changes trigger `DrumPadGrid::resized()` and repopulate pads

---

## Sidebar Controls Detail

### Velocity Knob
- Rotary knob, range 1–127, default 100
- Used when clicking pads to set note-on velocity
- Label "Vel" above knob

### HiHat Openness Knob
- Rotary knob, range 0–127, default 0
- Sends CC4 value to engine when changed (or when pad clicked)
- Label "HH" above knob

### Round-Robin Lock Knob
- Stepped rotary or small integer spinner, range 0–8
- 0 = all (normal RR cycling), 1 = always first variant, 2 = always second, etc.
- Engine uses this to override `SampleSelector` round-robin index
- Label "RR" above knob

---

## File Structure (new/modified)

```
source/
├── PluginEditor.h/.cpp          ← MODIFIED: new layout, sidebar, mode switching
├── UI/
|   ├── EngineSettingsPanel.h/.cpp   ← MODIFIED: grouped sections, grid size settings, moved to UI folder
│   ├── LookAndFeel.h/.cpp       ← existing, minor additions for new components
│   ├── Sidebar.h/.cpp           ← NEW: sidebar with mode buttons + knobs
│   ├── DrumPadGrid.h/.cpp       ← NEW: zen/zoned pad grid
│   ├── DrumPad.h/.cpp           ← NEW: single pad component
│   ├── MidiMapTable.h/.cpp      ← NEW: map mode table
│   └── StatusBar.h/.cpp         ← NEW: bottom status bar
```

---

## Implementation Order

### Phase 1: Shell — sidebar + mode switching + status bar
1. Create `Sidebar` component with 5 mode buttons (text labels initially, icons later)
2. Create `StatusBar` component
3. Refactor `PluginEditor` to new layout: sidebar | main area | status bar
4. Implement mode switching: show/hide components based on active mode
5. Move existing components to Raw mode (mode 4)
6. Move `EngineSettingsPanel` to Settings mode (mode 5)
7. Verify build + existing functionality preserved

### Phase 2: Zen view (mode 1)
1. Create `DrumPad` component — displays slot name, type colour, handles click
2. Create `DrumPadGrid` — arranges pads in NxM grid, reads from `KitLayout`
3. Wire pad click → send MIDI note-on via engine at sidebar velocity
4. Wire engine note-on callback → pad flash animation (simple alpha fade)
5. Set Zen as default mode

### Phase 3: Zoned view (mode 2)
1. Add `PadMode::Zoned` to `DrumPad` — horizontal split by articulations
2. Each zone clickable with its own MIDI note
3. Zone labels showing articulation codes

### Phase 4: Map mode (mode 3)
1. Create `MidiMapTable` component with scrollable rows
2. Populate from active `KitLayout` articulations
3. MIDI note ComboBox dropdowns (128 notes + unassigned)
4. MIDI learn button per row — listen for next note, assign, deactivate
5. Apply changes back to `KitLayout`

### Phase 5: Sidebar knobs + polish
1. Add velocity, hihat openness, RR lock knobs to sidebar
2. Wire knobs to engine (CC4, velocity override, RR lock)
3. Add RR lock support to `InstrumentSynth`/`SampleSelector`
4. Visual polish: pad colours by instrument type, hover states, flash animations
5. Replace mode button text with icons (SVG or path-drawn)

### Phase 6: Settings improvements
1. Restructure `EngineSettingsPanel` into grouped sections with headers
2. Add UI/Grid section with columns/rows controls
3. Two-column responsive layout

---

## Data Flow

```
KitLayout (slot definitions, articulations, MIDI notes)
    ↓
DrumPadGrid reads slots → creates DrumPad per slot
    ↓
DrumPad click → PluginProcessor injects MIDI note-on → DrumKitEngine
    ↓
DrumKitEngine note-on callback → DrumPadGrid → DrumPad flash

MidiMapTable reads/writes KitLayout articulation notes
    ↓
Changes → rebuild engine note→synth mapping

Sidebar::velocityKnob → stored value used by DrumPad on click
Sidebar::hihatKnob → CC4 sent to engine
Sidebar::rrLockKnob → engine RR override
```

---

## Notes

- Window size stays 1000×580 (or slightly wider to accommodate sidebar)
- All new components follow existing `this->` convention and coding style from AGENTS.md
- No one-letter variables; use descriptive names
- Pad grid rebuilds when KitLayout changes (kit switch, instrument map apply)
- MIDI learn requires engine → UI callback mechanism (juce::AsyncUpdater or similar)
- Clicking pads in plugin UI should work both in standalone and VST3 hosts
