# Purpose

- Own the plugin editor's panels, controls, visual theme, responsive layout, and user interaction.

# Ownership

- `UITheme.h` and `LookAndFeel*` own shared visual tokens and JUCE drawing behavior.
- Reusable controls live in `Parameter*`, `HorizontalKnobBrick`, MIDI assignment, popup, and helper classes.
- Page/panel classes compose controls and translate user actions into engine operations.

# Local Contracts

- Reuse `UITheme`, `LookAndFeel`, and `UIHelpers` instead of hard-coded styling.
- Settings shown as live must update audio-thread-visible state through a thread-safe engine API.
- Keep persistence debounced and outside the audio thread.
- Preserve keyboard, tooltip, and resize behavior when adding controls.

# Work Guidance

- Use existing `ParameterPot` and `ParameterSlider` interaction conventions.
- Keep group counts and pot index ranges in `EngineSettingsPanel::resized()` synchronized with constructor order.

# Verification

- Build `Tests` for shared-source compile coverage.
- Open the standalone/plugin editor and exercise resizing and live control updates for visual changes.

# Child DOX Index

