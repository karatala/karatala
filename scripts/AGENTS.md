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

# Work Guidance

- Preserve deterministic filenames and document format changes alongside engine parser changes.

# Verification

- Validate generated filenames with `InstrumentSpec::parseSampleFile` tests when metadata syntax changes.

# Child DOX Index

