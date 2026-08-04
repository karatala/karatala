# Purpose

- Own repeatable performance measurements for plugin construction and realtime-sensitive code.

# Ownership

- `Benchmarks.cpp` contains benchmark scenarios.
- `Catch2Main.cpp` provides the benchmark runner.

# Local Contracts

- Benchmarks must isolate the behavior being measured and avoid debug logging in timed regions.

# Work Guidance

- Add a controller-transition benchmark before claiming hot-path performance improvements.
- Keep small benchmark setup functions file-local `static`; do not add a benchmark helper namespace.

# Verification

- Build and run the `Benchmarks` target.

# Child DOX Index
