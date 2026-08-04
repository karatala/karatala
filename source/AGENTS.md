# Purpose

- Own the Karatala plugin and standalone application source.

# Ownership

- `PluginProcessor.*` owns the audio-processor entry point and state integration.
- `PluginEditor.*` owns the top-level editor composition.
- `Types.h` owns plugin-wide shared types.
- Domain-specific behavior belongs in the indexed child folders.

# Local Contracts

- Keep audio processing realtime-safe: no blocking I/O, avoid allocation and unbounded work on the audio thread.
- Preserve JUCE processor/editor lifecycle contracts and plugin-host compatibility.
- Keep engine behavior out of UI classes; UI changes engine state through explicit engine/controller APIs.

# Work Guidance

- Follow existing C++20/JUCE patterns and the repository `.clang-format`.
- Treat `CMakeLists.txt` source globs as the source registration mechanism.
- Keep one-use helper functions in their owning `.cpp` file.
- Name shared files and namespaces by responsibility or feature. Do not create general `helpers` folders or public `FooHelpers` namespaces.
- Use a named `<Feature>Detail` namespace with internal linkage for private `.cpp` helpers.
- Create a feature subfolder only when several files share one responsibility.
- Review [`docs/PROJECT_TERMINOLOGY_REVIEW.md`](../docs/PROJECT_TERMINOLOGY_REVIEW.md) before naming new shared modules. Do not use bare `Source` as a feature-folder name.

# Verification

- Build the `Tests` target for shared-source changes.
- Run focused Catch2 tests and broaden to the full suite for cross-domain changes.

# Child DOX Index

- `Engine/AGENTS.md` - runtime audio engine, models, loading, routing, and MIDI behavior
- `UI/AGENTS.md` - editor panels, controls, theme, and layout
- `Config/AGENTS.md` - YAML helpers, logging, and configuration utilities
