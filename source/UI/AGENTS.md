# Purpose

- Own the plugin editor's panels, controls, visual theme, responsive layout, and user interaction.

# Ownership

- `UITheme.h` and `LookAndFeel*` own shared visual tokens and JUCE drawing behavior.
- `ThemePlaygroundPanel.*` owns the live component, font, and color inventory used to review theme consistency.
- Reusable controls live in `Parameter*`, `CompactParameterControl`, `HorizontalKnobBrick`, MIDI assignment, popup, and helper classes.
- Page/panel classes compose controls and translate user actions into engine operations.

# Local Contracts

- Reuse `UITheme`, `LookAndFeel`, and `UIHelpers` instead of hard-coded styling.
- Settings shown as live must update audio-thread-visible state through a thread-safe engine API.
- Keep persistence debounced and outside the audio thread.
- Preserve keyboard, tooltip, and resize behavior when adding controls.
- Zone pads use a vertical instrument-name rail, equal rows for normal articulations, and a Hi-Hat grid ordered tight/close/half/open with pedal as a full-width bottom row. Painting and hit testing must share the same cell geometry.
- Pad-popup mic routing shows output names but selects and saves each mic-layout output's stable semantic ID, so repeated output codes remain separate choices across layout reordering.
- Pad-popup layer controls update only their own articulation layer. Removing an unmapped empty row must not reapply the instrument map.

# Work Guidance

- Use existing `ParameterPot` and `ParameterSlider` interaction conventions.
- Keep group counts and pot index ranges in `EngineSettingsPanel::resized()` synchronized with constructor order.
- Keep new UI compatible with the planned theme consolidation: one typeface with normal and small sizes, semantic primary/secondary/danger/success/info colours, and shared control-size tokens instead of component-specific values.

# Verification

- Build `Tests` for shared-source compile coverage.
- Open the standalone/plugin editor and exercise resizing and live control updates for visual changes.

# Child DOX Index
