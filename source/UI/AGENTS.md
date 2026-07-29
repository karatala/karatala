# Purpose

- Own the plugin editor's panels, controls, visual theme, responsive layout, and user interaction.

# Ownership

- `UITheme.h` and `LookAndFeel*` own shared visual tokens and JUCE drawing behavior.
- `ThemePlaygroundPanel.*` owns the live component, font, and color inventory used to review theme consistency.
- `SourceArticulationOverrideEditor*` owns source-global velocity ranges, source balance, sample manual gain, playback offsets, copied source measurements, and live recipe status.
- Reusable controls live in `Parameter*`, `CompactParameterControl`, `HorizontalKnobBrick`, MIDI assignment, popup, and helper classes.
- Page/panel classes compose controls and translate user actions into engine operations.

# Local Contracts

- Reuse `UITheme`, `LookAndFeel`, and `UIHelpers` instead of hard-coded styling.
- Settings shown as live must update audio-thread-visible state through a thread-safe engine API.
- Keep persistence debounced and outside the audio thread.
- Preserve keyboard, tooltip, and resize behavior when adding controls.
- Zone pads use a vertical instrument-name rail, equal rows for normal articulations, and a Hi-Hat grid ordered tight/close/half/open with pedal as a full-width bottom row. Painting and hit testing must share the same cell geometry. Hi-Hat states without their own MIDI note stay visible and preview the loaded controller sibling directly without sending CC.
- Instrument-map editors preserve exact hi-hat destination expressions such as `bow[open]` and `bow[tight]`. When editing a legacy base destination, they may anchor its source family to the matching destination expression, but an already explicit remap must stay unchanged.
- Pad-popup mic routing shows output names but selects and saves each mic-layout output's stable semantic ID, so repeated output codes remain separate choices across layout reordering.
- Pad-popup routing choices use the engine's mic-output matcher, including independent type and position wildcards such as `**m`, `*tm`, and `d*m`.
- Pad-popup layer controls update only their own articulation layer. Removing an unmapped empty row must not reapply the instrument map.
- The pad popup edits a layer's velocity response through one compact dropdown below Tuning, Attack, and Decay. The dropdown offers Use Global, built-in Linear, Logarithmic, and Exponential curves, every user preset, and Edit curve. An unmatched inline definition displays as `Unnamed layer curve*` but is not a selectable preset. The editor shows copied source measurements, updates the runtime curve without sample reload, and stores reusable curve-only presets separately from kit-layout presets. The Instrument tab shows Use Global, a matching preset name, or Unnamed status and must preserve velocity response, bleed, and mic-route fields through shared map serialization.
- Exact pad-popup source rows expose Source setup as a hover-only gear button in the source card's top-right corner. Its source-articulation editor changes source-global velocity ranges, instrument or articulation balance, whole-file manual gain, and whole-file or per-mic playback offsets. The editor uses normal text, an RR by velocity multi-select matrix, mic filters, a selected-file list, and three batch fields. The matrix opens without a selected or active hit. Clicking `*` inverts every existing matrix cell, clicking an RR letter inverts that row, and clicking a velocity heading inverts that column without preview. Its header and header-styled RR column use centred content. Its five value editors use the standard 20 px control and 14 px vertically centred text style, preserve typed, inherited, and `Mixed` values, and support coarse wheel or fine Shift-wheel changes. Effective velocity ranges use compact two-line blocks with a smaller range line; `Even` distributes the full MIDI range across all named layers in one save, while `Reset` restores filename ranges. Filename range details and generated sample measurements stay available in tooltips. Matrix preview plays the complete linked logical hit, while mic preview solos one exact mic, without advancing normal round robin. A matrix or mic click cancels the queued hover for that same target, so it produces one preview. Successful value-editor commits preview the complete logical hit from the last selected-file row, while range actions preserve their own preview target. Batch values persist through one complete source-kit override save. Save failures keep the editor open and offer retry or explicit reload of saved override values. The editor must not rewrite instrument-map fields.
- The Conf panel General section owns the sample-library path, sample-cache limit, current-instance global velocity response, and machine-wide default kit and mic layout controls. The global-response dropdown offers built-in and user presets, shows `Unnamed global curve*` for an unmatched stored definition, applies live to layers using `Use Global`, and saves only to plugin instance state. Changing the library path reloads the library; layout defaults affect future plugin instances.
- The shared source picker is a centered modal using 80% of its host content area, with a dark click-to-dismiss outside overlay. Its panel has 6 px rounded corners, 4 px inner padding, and no outline border.
- The picker title row shows the destination context and a `×` close button on the right. Whole-instrument context is only the slot display name, such as `Tom 1`. Articulation context is `Kick / center`, with ` (layer X)` only when `X` is greater than 1.
- The picker supports `-word` exclusions, OR-based multi-select base-articulation filters, arrow-key preview, and Enter to use. Opening it for an assigned row filters by the assigned source type before the slot type, then preselects and scrolls to that source without previewing it.
- The shared source picker has an explicit whole-instrument mode. This mode shows each parent instrument once in the main list, shows the selected instrument's articulations in a separate preview-only list, has no hidden destination-slot type restriction, and confirms the parent kit and instrument without a source-articulation filter. Category and Types controls remain available as manual filters.
- Text editors have no normal, focused, or shadow outline.
- Standard buttons, toggles, dropdowns, and menu text use the shared 20 px control height with normal 14 px text.
  The pad-popup source picker card and compact mic-routing dropdown keep their purpose-specific sizes.
- Dialog headers use the shared icon-only `×` close-button style. Edit Curve, Source setup, source picker, and other dialogs must not use a text `Done` button.
- `CompactParameterControl` keeps its existing value font. The Instrument tab uses label-free table-row bounds; PadPopup uses 32 px controls with the existing 10 px label.
- Instrument and Map tables use fixed 18 px headers, 22 px rows, shared 90/100 px instrument and articulation columns, and normal 12 px name text.
- Pad-popup instrument and articulation headings use the same normal 12 px name text.
- The pad popup is bottom-docked at 90% of the tab-content height and uses the full tab-content width. Its uncovered top strip is dark, closes the popup when clicked, and contains a large white `×` aligned left. Switching tabs also closes the popup. The popup keeps rounded top corners, square bottom corners, and no outline border.
- Pad-popup layer group backgrounds end at the bottom of `Use Global` and mic-routing controls without extra bottom padding. Source cards use centred vector icons, with Source setup at the top-right and layer removal at the bottom-right. Both icons appear only while their source card is hovered. The `L/R` button must show its full text.
- When a PadPopup child dialog is open, an outside click in either the dialog overlay or the uncovered top strip closes only that dialog. A later outside click can close the PadPopup.
- The pad-popup header order is slot title, `LINK ARTS`, conditional `LINK TOM BLEED`, `SET INSTRUMENT`, and right-aligned MIDI controls. `SET INSTRUMENT` removes every layer for the current slot and installs one complete source instrument through the surgical slot-change path.
- `LINK ARTS` applies tuning, attack, decay, gain, pan, L/R flip, phase flip, one changed bleed mic level, or one changed mic route to the same layer number across the current slot's articulations. Missing layers and missing source mic codes are skipped.
- `LINK TOM BLEED` is available only on Tom slots and applies the changed source mic code's level or route to every mapped layer and articulation in every Tom slot. Routing copies `Auto`, `Unassigned`, or the selected stable output ID; targets without that compatible output choice are skipped. When both links are active, target destination layers are deduplicated.
- Linked changes update only the changed field, update visible controls without recursive callbacks, persist one serialized instrument map, and notify the editor once. Parameter and mic-route runtime batches each use one engine graph lock. Link modes are temporary, reset when the popup closes, and use a steady bright-red fill with white text and a clear border without blinking.

# Work Guidance

- Use existing `ParameterPot` and `ParameterSlider` interaction conventions.
- Keep group counts and pot index ranges in `EngineSettingsPanel::resized()` synchronized with constructor order.
- Keep new UI compatible with the planned theme consolidation: one typeface with normal and small sizes, semantic primary/secondary/danger/success/info colours, and shared control-size tokens instead of component-specific values.

# Verification

- Build `Tests` for shared-source compile coverage.
- Open the standalone/plugin editor and exercise resizing and live control updates for visual changes.

# Child DOX Index
