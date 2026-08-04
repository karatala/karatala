# Purpose

- Own configuration parsing helpers and application logging utilities shared by the engine.

# Ownership

- `YamlHelper*` owns the lightweight YAML representation and parser behavior.
- `PluginLogger.h` owns log levels and logging macros.
- `Helpers.h` owns small configuration-oriented helpers.

# Local Contracts

- Parsing returns structured results with warnings/errors instead of silently accepting malformed required data.
- Quoted YAML map keys may contain `:`. Key/value splitting must use only an unquoted colon so portable file paths and identities round-trip safely.
- Logging used from realtime paths must not perform avoidable formatting or allocation when its level is disabled.
- Human-readable log lines use local `HH:MM:SS.mmm` timestamps without a date or timezone.

# Work Guidance

- Keep YAML behavior compatible with existing configuration and layout files.

# Verification

- Run the `[yaml]` Catch2 tests after parser changes.

# Child DOX Index
