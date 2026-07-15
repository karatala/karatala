# Purpose

- Own offline sample analysis, per-kit `metadata.csv` generation, library conversion, and maintenance tools.

# Ownership

- `generate_sample_metadata.py` generates one `metadata.csv` per selected kit.
- `analyze_drum_samples_phase1.py` remains the listening and metric comparison tool.
- Other `analyze_drum_samples*.py` files are deprecated experiments.
- `convert-drumgizmo2karatala.py` and conversion helpers generate Karatala library structures.
- HTML/TypeScript utilities support manual sample organization.
- `rename_samples_from_parent_folders.py` generates review-only batch rename plans from library folder names.

# Local Contracts

- C++ defines the required `metadata.csv` columns and their playback meaning.
- The metadata generator must not rename or modify sample audio files.
- It may replace only `metadata.csv` and `metadata.csv.bak` inside explicitly selected kit roots, using transient files only for atomic replacement.
- Required CSV rows contain content identity, onset, intensity, true peak, peak and decay, stereo balance, and readable columns from `-6db_ms` through `-60db_ms`.

# Work Guidance

- Keep output rows deterministic and add a field only when C++ or a defined developer workflow consumes it.
- Keep useful function comments that explain each measurement and output field.

# Verification

- Validate generator output with the matching C++ metadata parser when required columns change.

# Child DOX Index
