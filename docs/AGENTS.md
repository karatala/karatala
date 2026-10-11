# Purpose

- Own durable engineering plans, audits, UI reviews, implementation designs, and user-facing format references.

# Ownership

- `TODO*_PLAN.md` files describe scoped future or in-progress work.
- `PLUGIN_INSTANCE_STATE_FORMAT.md` defines the current per-instance processor-state container and properties.
- `INSTALL.md` is the single source for download, installation, sample-library selection, and first-use instructions. The root README links to it. CI converts it to the plain-text guide used by ZIPs and both installers. Use absolute web links for references needed outside the repository.
- Sample downloads use kit ZIP assets from `https://github.com/karatala/sample_library/releases/latest`, not automatic source archives. Extract kit folders directly into one library folder. The Default preset requires kits from the full library.
- `SAMPLE_LIBRARY_FORMAT.md` explains library folders, sample names, microphones, and optional control values in simple English. Keep it aligned with library parsing code.
- `default preset.txt` owns the factory Default instrument map. Packaging copies it to `instrument_map_presets/Default.txt`; it requires sounds from several kits in the full sample library and is loaded by the user.
- Performance and decay documents record cross-cutting design constraints.
- `PROJECT_TERMINOLOGY_REVIEW.md` defines preferred project terms and related C++ names.
- `TODO20_SIMPLE_LANGUAGE_AND_LOCALIZATION_PLAN.md` owns the simple-language review, dictionary update plan, and optional translation work.

# Local Contracts

- Keep documents aligned with current filenames, ownership, and implementation status.
- Mark implemented, superseded, or still-open sections explicitly; do not describe stale behavior as current.
- Prefer actionable constraints and verification criteria over speculative diary notes.
- Keep `round robin` and `articulation` as approved technical terms, including their related code names. Explain them in simple words when needed; do not rename them as part of language simplification.

# Work Guidance

- Link to source paths relative to `docs/`.
- Separate measured facts from proposed behavior.

# Verification

- Re-read referenced source and tests before updating implementation-status claims.

# Child DOX Index
