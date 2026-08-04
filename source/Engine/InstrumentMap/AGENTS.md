# Purpose

- Own the instrument-map data boundary and text format.

# Ownership

- `InstrumentMap.h` owns map records and public map operations.
- `InstrumentMap.cpp` owns parsing, normalized serialization, and case-insensitive identity matching.

# Local Contracts

- Each valid layer has one articulation-slot target and one full library articulation path.
- Parsing reports malformed lines as structured warnings or errors.
- Serialization has stable slot and articulation order.
- Identity matching trims text, ignores case, and rejects an empty identity.

# Work Guidance

- Keep small map primitives in this cohesive pair. Add another file only for a separate substantial responsibility.
- Keep private implementation in the named `InstrumentMapDetail` namespace.

# Verification

- Run the `[InstrumentMap]` Catch2 tests after map data, parsing, serialization, or identity changes.

# Child DOX Index
