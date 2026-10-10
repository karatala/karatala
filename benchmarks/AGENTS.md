# Purpose

- Own repeatable performance measurements for plugin construction and realtime-sensitive code.

# Ownership

- `Benchmarks.cpp` contains benchmark scenarios.
- `Catch2Main.cpp` provides the benchmark runner.

# Local Contracts

- Benchmarks must isolate the behavior being measured and avoid debug logging in timed regions.
- `KARATALA_BUILD_BENCHMARKS` defaults to `OFF`. Enable it to create the `Benchmarks` target. Normal CI and release jobs do not build or run benchmarks.
- `.github/workflows/benchmarks.yml` is a manual workflow for Linux, macOS, and Windows. It builds only the benchmark target and its dependencies, runs the benchmark executable, and uploads `benchmark-results.txt`. It does not gate releases.

# Work Guidance

- Add a controller-transition benchmark before claiming hot-path performance improvements.
- Keep small benchmark setup functions file-local `static`; do not add a benchmark helper namespace.

# Verification

- Configure with `-DKARATALA_BUILD_BENCHMARKS=ON`, then build and run the `Benchmarks` target.

# Child DOX Index
