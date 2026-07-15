# Purpose

- Own offline sample analysis, filename metadata generation, library conversion, and maintenance tools.

# Ownership

- `analyze_drum_samples*.py` derives loudness/decay metadata consumed by `KitLibModel`.
- `convert-drumgizmo2karatala.py` and conversion helpers generate Karatala library structures.
- HTML/TypeScript utilities support manual sample organization.
- `rename_samples_from_parent_folders.py` generates review-only batch rename plans from library folder names.

# Local Contracts

- Metadata encodings must remain compatible with engine decoders, especially base62 decay deltas and filename parameter syntax.
- Scripts may perform filesystem changes only within explicitly selected input/output locations.
- `analyze_drum_samples.py` is report-only unless `--rename-files` is explicitly supplied. Its JSON report is the complete audit record; CSV output is an optional flat view.
- Analyzer rename mode must preserve metadata it does not own and must block all writes when the complete selected rename plan has parsing, measurement, uniqueness, or destination errors.

# Work Guidance

- Preserve deterministic filenames and document format changes alongside engine parser changes.

# Verification

- Validate generated filenames with `InstrumentSpec::parseSampleFile` tests when metadata syntax changes.

# Child DOX Index
