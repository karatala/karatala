# Purpose

- Own installer definitions, distribution metadata, icons, EULA, and packaging resources.

# Ownership

- Platform installer files stay in this directory.
- `resources/` contains user-facing packaged legal material and the generated install guide.
- `install-guide-links.lua` keeps link destinations visible when Pandoc converts the guide to plain text.

# Local Contracts

- Keep product identifiers, names, and icons consistent with root CMake metadata.
- Do not change legal text without explicit user direction.
- Windows and macOS installers include kit layouts and the factory `instrument_map_presets/Default.txt` in the engine's shared data folder: `{commondocs}\Karatala` on Windows and `/Users/Shared/Karatala` on macOS. The factory preset source is `docs/default preset.txt`; copy its content unchanged. That folder must allow users to create settings and presets. Do not supply or replace `config.yaml`; the app creates it on first use.
- Windows layout installation keeps existing files and leaves layouts in place on uninstall. Do not create a plugin-folder link to an unrelated data directory.
- CI creates a ZIP for every platform with only the packaged app/plugin folders, `data/kit_layouts`, `data/instrument_map_presets/Default.txt`, and `README.txt`. macOS ZIP creation must preserve app bundle structure and executable permissions.
- `docs/INSTALL.md` owns the installation steps. Before packaging, CI generates the ignored `resources/README.txt` with Pandoc and `install-guide-links.lua`. ZIPs and both installers use this generated file; Windows also shows it through `InfoBeforeFile`. Do not maintain a second guide in this folder. Include download links, manual plugin and layout destinations, sample-library selection, exact UI labels, and unsigned-download help in simple English in the Markdown source.

# Work Guidance

# Verification

- Run the relevant platform packaging workflow when packaging behavior changes.

# Child DOX Index

