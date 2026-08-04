# Codebase Improvement Plan

Status: active review and cleanup plan.

Implemented candidates:

- Mic bleed gain is cached on `SampledSound`, removing a string lookup from voice rendering. Correctness verification is still required.
- `StringsDictSingleton` has direct `id -> name` lookup. Focused correctness verification is still required.

Candidate under review:

- Mic route overrides use cached string IDs in the staged changes. Keep this only if behavior tests pass and a realistic routing-rebuild benchmark shows a useful gain.

Rejected experiment:

- Per-articulation MIDI controller lookup tables were reverted. They added code and memory without a proven real-world benefit.

## Goal

Make the codebase smaller, easier to read, easier to reuse, and faster where performance matters.

The project is unreleased, so internal types and layouts may change. Behavior that already works well must still be protected by tests.

## Priorities

1. Correct audio and mapping behavior.
2. Realtime safety in rendering, note, controller, and aftertouch paths.
3. Simpler code and removal of exact duplication.
4. Lower memory use in types that have many instances.
5. Faster loading, routing rebuilds, and UI work when measurements show a problem.

Do not treat all runtime code as realtime code. Parsing, routing rebuilds, `prepareToPlay()`, and most UI work are setup work. They may allocate and use strings when that keeps the code simpler.

## Rules For Accepting A Change

Every candidate must answer:

- Which thread calls it?
- How often is it called in a realistic project?
- Is the current code wrong, duplicated, difficult to maintain, slow, or memory-heavy?
- Can deletion or simplification solve the problem?
- What test proves behavior stays correct?
- What measurement proves the performance or memory claim?

Accept a change only when its benefit justifies its added code and state.

- Extract a helper only when at least two real users need the same behavior.
- Require an old-versus-new benchmark or full-path before-versus-after measurement for performance claims.
- Require individual type sizes, member offsets, instance counts, and estimated total saving for memory-layout changes.
- Do not introduce strings, allocation, blocking locks, logging preparation, or unbounded work in realtime paths.
- Give cached data one clear owner and rebuild point. Avoid parallel representations that can become inconsistent.

## Verification Strategy

Tests prove behavior. Benchmarks measure cost.

### Tests

For each change:

1. Add or identify a focused test for current behavior.
2. Make the implementation change.
3. Run the focused test.
4. Run the full `Tests` target for shared engine or configuration changes.
5. Use listening checks for changes affecting audio transitions, gain, routing, phase, or voices.

String and ID conversions must cover:

- Empty and repeated values.
- Unknown IDs.
- Case differences.
- Intended whitespace behavior.
- Duplicate keys that normalize to the same value.
- Serialization and parsing round trips.

### Benchmarks

- Compare equivalent old and new logic in the same binary when practical.
- Give both paths identical input and verify identical output outside the timed region.
- Use realistic small, normal, and large data sizes.
- Use Release builds for performance decisions.
- Keep setup outside the timed region unless setup is being measured.
- Record the scenario, result, and date.
- Remove temporary decision benchmarks unless they protect an important performance limit.

Memory layout is not a timing benchmark. Produce a readable size report instead of timing `sizeof` constants.

## Workstream 1: Consolidate Similar Code And Organize Helpers

Priority: high for readability and code size. Performance priority depends on the callers.

The goal is broader than removing byte-for-byte duplicates. Repeated chunks often implement the same operation with small differences, extra normalization, different containers, or old behavior that is no longer part of the contract. These cases should be reviewed together and reduced to one clear implementation where practical.

### Step 1: Enumerate Repeated And Similar Code

Create an audit table containing:

- Code locations and callers.
- The shared operation each chunk appears to perform.
- Important behavior differences and edge cases.
- Calling domain and thread.
- Whether the code should be deleted, merged, parameterized, or left separate.
- Expected code reduction.
- Whether consolidation can also reduce allocation, string work, or repeated scans.

Search for more than matching function names. Include repeated loops, normalization, sorting, joining, lookup, validation, conversion, fingerprint, and error-formatting patterns.

Initial candidates:

- `identityKey()` and `candidateMatches()` in `source/Engine/DrumKitEngine.cpp` and `source/Engine/KitLibModel.cpp`.
- Repeated identity candidate counting and resolving for kits, instruments, and articulations.
- Repeated normalized string lookup and case-insensitive vector membership.
- Repeated stable sorting and fingerprint formatting for map layers.
- Repeated map and route parameter formatting.
- Repeated MIDI value validation and fixed-domain lookup initialization.
- Name joining and articulation-filter loops in the instrument-map code.

Before extracting a helper, confirm that the behavior is still required. The current instrument map format uses one articulation slot and one library articulation per line.

Remove obsolete multi-articulation joining instead of making it reusable.

### Step 2: Design The Shared Operation

For each accepted candidate:

1. Write the common behavior in simple terms.
2. Decide which differences are real domain rules and which are accidental duplication.
3. Prefer one concrete function over a configurable generic framework.
4. Prefer deletion when one caller represents obsolete behavior.
5. Keep one-use logic local when extraction would only move code without making it clearer.
6. If the operation is performance-sensitive, compare the existing variants and keep the simplest efficient algorithm.

Do not add boolean switches or callbacks only to force unrelated code through one helper. Separate domain rules may share a small primitive without sharing the whole workflow.

### Step 3: Organize Helper Files

Inventory existing `*Helper*` files, free functions, local lambdas, and helper namespaces before moving them. Record their purpose, owners, callers, and dependencies.

Target organization:

- Use feature folders such as `source/Engine/InstrumentMap/` and `source/UI/Controls/` when several files share one responsibility.
- Use [PROJECT_TERMINOLOGY_REVIEW.md](PROJECT_TERMINOLOGY_REVIEW.md) for project terms and related C++ names. Do not create `source/UI/Source/`.
- Keep a helper in its owning `.cpp` file when it has one caller.
- Keep small cross-feature primitives in the narrowest owning domain and give them a responsibility-based name.
- Do not create one `source/helpers/` folder. It would mix unrelated engine, UI, and configuration code.
- Do not create `Engine/helpers`, `UI/helpers`, or `Config/helpers` as general dumping grounds.

Organization rules:

- Name files by responsibility, such as `InstrumentMap.h` or `MicRouting.h`, not only `Helpers.h`.
- Start a feature with one cohesive `.h` and `.cpp` pair. Add another file only for a separate, substantial responsibility.
- Prefer feature namespaces such as `InstrumentMap` or `ArticulationNames` over public `FooHelpers` namespaces.
- Keep one-use private `.cpp` helpers as file-local `static` functions, or private static class methods when class ownership is clear.
- When a complete implementation belongs to one feature namespace, keep the complete `.cpp` inside that namespace.
- Do not add a private namespace block only to group helpers beside class or global implementations.
- Use one clear named namespace that matches the owned domain.
- Keep helper dependencies one-way. Low-level helpers must not include large owner classes only for convenience.
- Prefer pure functions with explicit inputs and outputs.
- Keep state and ownership in the main class when the helper does not own that state.
- Do not create one large utility namespace or dumping-ground file.
- Do not move helpers only to satisfy folder shape. Move them when ownership and reuse are clear.
- Update the applicable DOX hierarchy when helper folders become durable ownership boundaries.

Move helpers by concern in small changes. Do not combine a large file move, behavior change, and optimization in one diff.

### Verification

- Add or identify focused tests before merging similar behavior that currently differs.
- Run identity, map parsing, mapping, fingerprint, routing, and serialization tests for affected engine helpers.
- Add ambiguity, normalization, ordering, and error-result coverage where missing.
- Compare serialized text before and after instrument-map helper changes.
- Do not add performance benchmarks for pure moves.
- Add an old-versus-new benchmark only when consolidation changes an algorithm used frequently or in realtime code.

### Success Conditions

- Repeated and structurally similar code has been enumerated, not selected ad hoc.
- Obsolete variants are deleted instead of preserved in helpers.
- Main engine and UI folders are less polluted by unrelated helper files.
- Helper folders and namespaces have clear domain ownership.
- Total implementation code is reduced or made clearly easier to follow.
- No generic helper framework is introduced without several concrete users.

## Workstream 2: Audit Strings And IDs

Priority: high on the audio thread, medium during frequent rebuilds, low in UI and parsing.

Rules:

- Keep strings at configuration, YAML, serialization, logging, and UI boundaries.
- Normalize once when the same value is compared many times later.
- Use an enum for a closed set of known values.
- Use `StringsDictSingleton::Id` for stable identity shared across engine structures.
- Never call `StringsDictSingleton::getId()` from realtime code because it locks and may insert.
- Never convert an ID back to a string in realtime code.

Audit method:

1. List `trim().toLowerCase()`, `compareIgnoreCase()`, string-key maps, `getId()`, and `nameOf()` call sites.
2. Record thread, frequency, collection size, and whether text is needed afterward.
3. Leave simple setup and UI code alone unless it is duplicated or measured slow.
4. Change one domain at a time and keep public text formats unchanged.

### Cached Mic Bleed Gain

Required tests:

- Default gain is `1.0`.
- Configured code receives the configured gain.
- Changing bleed updates already loaded sounds.
- Clearing overrides restores the default.
- Different mic codes remain independent.

Benchmark:

- Keep the current string lookup versus cached gain comparison.
- Add a representative render scenario with several active voices and routed microphones.
- Verify both paths produce the same accumulated gain.

### `StringsDictSingleton` Reverse Lookup

Required tests:

- Empty name and ID `0`.
- Repeated name returns the same ID.
- Different names return different IDs.
- Valid and unknown reverse lookup.
- Concurrent reads and insertion remain protected.

Benchmark:

- Compare the old reverse scan with direct lookup using 1, 16, 512, and 4096 names.
- Treat this as setup and logging improvement, not audio-thread improvement.

### Staged Mic Route Override IDs

Risk:

- Routing rebuild is setup work and normal projects may have few overrides.
- The staged code keeps string and ID maps, adding state and rebuild logic.

Required tests:

- No override and exact match.
- Case-insensitive match and intended whitespace behavior.
- Missing target preserves fallback behavior.
- Multiple overrides select correct outputs.
- Duplicate normalized keys are deterministic or rejected.
- Routing and serialization match the string implementation.

Benchmark:

- Compare complete routing rebuilds using string and ID matching.
- Use 0, 4, and 16 overrides, plus a large stress set.
- Include normalization and ID lookup costs.
- Verify identical routing assignments before timing.

Keep the staged change only if the complete realistic rebuild improves enough to justify the duplicate cached map. Otherwise revert it.

Other string candidates must go through the same audit. Do not convert strings only because IDs are available.

## Workstream 3: Audit Allocations And Containers

Priority: high only for realtime allocations or structures with many instances.

Plan:

1. Search realtime paths for temporary strings, vectors, maps, shared ownership, and buffers.
2. Count allocations in note, controller, render, routing rebuild, and load scenarios.
3. Separate realtime allocations from harmless setup allocations.
4. Prefer existing contiguous storage and bounded stack arrays when the domain is fixed and code becomes simpler.
5. Do not replace a small setup container with a large permanent table without measured total benefit.

Low-priority candidates:

- `VelocityLevelRuntime::roundRobinCounter` has a separately allocated shared atomic, but it is created during setup. Measure total layer count and memory before adding custom move logic.
- `prepareToPlay()` uses a small unordered map to count variants. Replace it only if an array is shorter and clearer.

Tests:

- Round-robin order and wrap behavior.
- Layer movement or rebuild preserves intended counter state.
- Voice-count calculation remains identical.

Measure allocation counts for complete operations instead of timing isolated container construction.

## Workstream 4: Measure Memory Layout

Priority: medium after measurement.

Start with high-count or long-lived types:

- `SampledSound` and `InstrumentPlayer::SampleEntry`.
- `KitLib::SampleSpec`.
- `InstrumentPlayer::VelocityLevelRuntime` and `ArticulationRuntime`.
- `InstrumentPlayerVoice`.
- `KitLib::MicSpec`.
- `DrumKitEngine::InstrumentMapLayer`.

The report must contain:

- `sizeof` and `alignof` for every candidate.
- Member sizes and offsets where available.
- Estimated instance count for small and large kits.
- Estimated total RAM per type, including owned heap data where practical.
- Before and after results for any reorder.

Rules:

- Reorder fields only when the report shows a useful saving.
- Prefer savings without custom packing or harder access.
- Do not use packed runtime structs.
- Do not mix layout and behavior changes.
- Use compile-time size checks only for intentional long-term contracts.

Verification:

- Build coverage for affected types.
- Existing model, routing, loading, voice, and serialization tests pass.
- Record actual estimated total saving.

## Workstream 5: Measure Realtime Locking Separately

Priority: high for reliability, but separate from compactness cleanup.

Observed risks:

- `DrumKitEngine::processBlock()` clears output when `engineGraphLock` cannot be acquired.
- `InstrumentPlayer::renderNextBlock()` holds `this->lock` while rendering.
- Note start also takes the instrument-player lock.

Measure before designing a rewrite:

- Failed graph try-lock count.
- Typical and maximum graph lock hold time during map, layout, and routing changes.
- Audio blocks cleared during those operations.
- Threads that can contend for the instrument-player lock.

Verification:

- Add counters or a stress harness for layout and map changes during playback.
- Test object lifetime while audio reads published state.
- Ensure destruction and shared-owner release do not move to the audio thread.
- Run full engine tests and listening checks.

If measurements confirm a problem, create a separate snapshot/publication design. Do not mix it with helper, string, or layout cleanup. An uncontended empty-render benchmark does not measure lock contention.

## Workstream 6: Experimental UI

Priority: low.

- Do not optimize unfinished UI only because it contains repeated strings.
- Reuse `UIComponentStyle` for shared styling. Keep available-library-articulation operations with `AvailableLibraryArticulation` and control operations with their concrete controls.
- Remove clear local duplication during normal UI work.
- Cache search text only when a representative large list shows visible or measured delay.
- Keep experimental UI changes separate from engine cleanup.

## Benchmark File Cleanup

For the current working-tree benchmark additions:

- Keep and extend the mic bleed old-versus-cached comparison.
- Rewrite the dictionary case to compare old scan with direct lookup.
- Rewrite route override measurement around complete realistic routing rebuilds.
- Remove the reverted controller-table benchmark.
- Remove the uncontended empty-lock benchmark.
- Remove isolated setup-container benchmarks until an alternative exists.
- Replace the timed memory-layout total with a per-type size report.
- Remove helper-move benchmarks.
- Keep a UI search benchmark only when UI search is actively optimized.

## Execution Order

1. Add missing correctness tests for the two implemented candidates.
2. Repair benchmarks so they compare equivalent behavior.
3. Use tests and realistic measurement to accept or revert staged route IDs.
4. Merge exact duplicate helpers in small changes.
5. Classify string and ID call sites before converting more strings.
6. Measure allocations and layouts before changing containers or fields.
7. Implement only candidates with a clear code-size, memory, or performance benefit.
8. Measure realtime lock contention and create a separate design if confirmed.
9. Leave experimental UI optimization until behavior is stable or measured slow.

Every changed line should connect to one measured problem or one exact duplication.
