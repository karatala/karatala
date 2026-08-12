# Purpose

- Own offline sample analysis, per-kit `metadata.csv` generation, library conversion, and maintenance tools.

# Ownership

- `generate_sample_metadata.py` generates one `metadata.csv` per selected kit.
- `sample_metadata_reports.py` generates optional external Phase 6 balance, room-orientation, and mic-timing CSV reports from completed metadata rows.
- `analyze_drum_samples_phase1.py` remains the listening and metric comparison tool.
- Other `analyze_drum_samples*.py` files are deprecated experiments.
- `convert-drumgizmo2karatala.py` and conversion helpers generate Karatala library structures.
- HTML/TypeScript utilities support manual sample organization.
- `rename_samples_from_parent_folders.py` generates review-only batch rename plans from library folder names.

# Local Contracts

- C++ defines the required `metadata.csv` columns and their playback meaning.
- The metadata generator must not rename or modify sample audio files.
- It may replace only `metadata.csv` and `metadata.csv.bak` inside explicitly selected kit roots, using transient files only for atomic replacement.
- If required Python modules are missing, the metadata generator may install them with the active Python only after explicit interactive confirmation. Non-interactive runs never install modules.
- Phase 6 reports are written only under the explicit `--report-folder`; they never update source folders, audio, or runtime metadata.
- Required CSV rows contain content identity, onset, intensity, true peak, peak and decay, stereo balance, and readable columns from `-6db_ms` through `-60db_ms`.
- DrumGizmo hi-hat velocity comes from sample power. Hi-hat openness remains an expression parameter and does not replace velocity.
- If more than 26 DrumGizmo samples resolve to one velocity, use the nearest velocity with free round-robin capacity before assigning a letter.
- DrumGizmo stereo channels may have unrelated channels between the left and right entries. Pair only one clear name-matched left/right candidate and keep left before right.
- Generated DrumGizmo runtime code must recalculate duplicate targets from the current map. It must keep non-duplicate quarantine reasons as hard blocks and reject invalid current instrument and mic codes.
- DrumGizmo source paths can be relative to the selected source root or absolute. Do not prepend the source root to an absolute path.

# Work Guidance

- Keep output rows deterministic and add a field only when C++ or a defined developer workflow consumes it.
- Keep useful function comments that explain each measurement and output field.

# Verification

- Validate generator output with the matching C++ metadata parser when required columns change.

# Child DOX Index
