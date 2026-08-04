# Purpose

- Own the instrument-map data boundary and text format.

# Ownership

- `InstrumentMap.h` owns map records and the narrow text API.
- `InstrumentMap.cpp` owns parsing and normalized serialization.
- `InstrumentMapOperations.h/.cpp` owns reusable operations that also need layout or library model types.

# Local Contracts

- Each valid layer has one articulation-slot target and one full library articulation path.
- Parsing reports malformed lines as structured warnings or errors.
- Serialization has stable slot and articulation order.
- Library identity matching belongs to the `KitLib` model API.

# Work Guidance

- Keep each complete implementation file inside the `InstrumentMap` namespace and keep its private functions `static`.
- Do not add a new map file for a small group of one-use functions.

# Verification

- Run the `[InstrumentMap]` Catch2 tests after map data, parsing, serialization, or operation changes.

# Child DOX Index
