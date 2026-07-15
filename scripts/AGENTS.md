# Purpose

- Own offline sample analysis, per-kit `metadata.csv` generation, library conversion, and maintenance tools.

# Ownership

- `sample_calibr_phase2.py` is the current metadata-generator prototype and is planned to become `generate_sample_metadata.py`.
- `analyze_drum_samples_phase1.py` remains the listening and metric comparison tool.
- Other `analyze_drum_samples*.py` files are deprecated experiments.
- `convert-drumgizmo2karatala.py` and conversion helpers generate Karatala library structures.
- HTML/TypeScript utilities support manual sample organization.
- `rename_samples_from_parent_folders.py` generates review-only batch rename plans from library folder names.

# Local Contracts

- C++ defines the required `metadata.csv` columns and their playback meaning.
- The metadata generator must not rename or modify sample audio files.
- It may replace only `metadata.csv` and `metadata.csv.bak` inside explicitly selected kit roots.
- Decay points use readable CSV columns from `minus_6_db_ms` through `minus_60_db_ms`, not compressed filename codes.

# Work Guidance

- Keep output rows deterministic and add a measured field only when C++ has a defined consumer.
- Keep useful function comments that explain each measurement and output field.

# Verification

- Validate generator output with the matching C++ metadata parser when required columns change.

# Child DOX Index
