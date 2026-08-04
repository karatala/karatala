# Purpose

- Own Catch2 verification for shared plugin code and parsers.

# Ownership

- Tests are grouped by subsystem in `Test*.cpp`.
- `helpers/` owns reusable test support.
- A complete test file can use one named feature namespace. Keep a required global friend-access type outside it.

# Local Contracts

- Tests must be deterministic and independent of a user's sample library unless explicitly marked as integration tests.
- Prefer testing pure mapping, parsing, envelope, and policy functions without constructing the full plugin.

# Work Guidance

- Use focused Catch2 tags for new subsystems.
- Cover boundaries and invalid metadata, especially MIDI values `0` and `127`.
- Use file-local `static` functions for small private test helpers. Add shared support only when several test files have substantial common setup.

# Verification

- Build the `Tests` target.
- Run the focused test executable/filter, then the full suite for shared behavior changes.

# Child DOX Index
