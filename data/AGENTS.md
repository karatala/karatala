# Purpose

- Own bundled runtime data distributed with the application.

# Ownership

- `kit_layouts/` contains MIDI note/controller layout YAML for supported e-drum modules.

# Local Contracts

- Layout IDs remain stable because configuration persists them.
- MIDI notes and controller numbers stay within `0..127`.
- Hi-hat openness mappings use engine-recognized expression codes such as `pdl_opn_cls`.

# Work Guidance

- Keep YAML concise and compatible with `LayoutManager` parsing.

# Verification

- Load changed layouts through layout parsing/runtime initialization tests or the standalone app.

# Child DOX Index

