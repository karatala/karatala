# Purpose

- Own configuration and parsing support plus application logging shared by the engine and UI.

# Ownership

- `YamlHelper*` owns the lightweight YAML representation and parser behavior.
- `Parsing*` owns shared parse results, wrapped-text parsing, and strict finite-number parsing.
- `LogLevel.h` owns log-level values and text conversion declarations.
- `PluginLogger.*` owns global logging, the writer thread, file output, and logging macros.
- `Helpers.h` owns the remaining small configuration-oriented helpers while they are split by responsibility.

# Local Contracts

- Parsing returns structured results with warnings/errors instead of silently accepting malformed required data.
- Quoted YAML map keys may contain `:`. Key/value splitting must use only an unquoted colon so portable file paths and identities round-trip safely.
- Logging used from realtime paths must not perform avoidable formatting or allocation when its level is disabled.
- Human-readable log lines use local `HH:MM:SS.mmm` timestamps without a date or timezone.

# Work Guidance

- Keep YAML behavior compatible with existing configuration and layout files.

# Verification

- Run the `[Parsing]` Catch2 tests after shared parsing changes.
- Run the `[yaml]` Catch2 tests after parser changes.

# Child DOX Index
