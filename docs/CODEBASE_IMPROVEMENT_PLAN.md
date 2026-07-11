# Codebase Improvement Plan

Status: proposed, not implemented.

This plan is based on a read-only review. No code was changed, no build was run, and no tests were run.

## Goals

- Reduce repeated helper code.
- Reduce string work in engine runtime code.
- Find safe places to use `StringsDictSingleton` or numeric IDs instead of repeated string compares.
- Reduce allocations in audio-sensitive paths.
- Check struct/class layout for possible RAM savings.
- Keep changes small and verify each step before moving to the next one.

## Success Criteria

- Audio thread does less string lookup, heap allocation, and locking.
- Runtime code uses fixed lookup tables where MIDI values are known to be `0..127`.
- Repeated string normalization and identity matching lives in reusable helpers.
- Any memory-layout change is backed by measured `sizeof`, `alignof`, and member-offset data.
- Focused tests pass for changed behavior when implementation starts.

## Observed Facts

- Realtime engine rules say code reached from `processBlock`, `renderNextBlock`, note handling, controller handling, and aftertouch handling must avoid strings, logging preparation, heap allocation, locks with message-thread contention, and unbounded work.
- `DrumKitEngine::processBlock()` uses `engineGraphLock` with `ScopedTryLock` in [`../source/Engine/DrumKitEngine.cpp`](../source/Engine/DrumKitEngine.cpp).
- `InstrumentSynth::renderNextBlock()` takes `this->lock` while rendering in [`../source/Engine/InstrumentSynth.cpp`](../source/Engine/InstrumentSynth.cpp).
- `InstrumentSynth::startLayerVoices()` also takes `this->lock` during note start in [`../source/Engine/InstrumentSynthController.cpp`](../source/Engine/InstrumentSynthController.cpp).
- `InstrumentSynthVoice::renderNextBlock()` calls `owner->getMicBleedLevel(s->routedMicCode)` while rendering, which uses a `juce::String` key lookup in [`../source/Engine/InstrumentSynth.cpp`](../source/Engine/InstrumentSynth.cpp) and [`../source/Engine/InstrumentSynthLoader.cpp`](../source/Engine/InstrumentSynthLoader.cpp).
- `StringsDictSingleton::nameOf()` locks and scans the whole `nameIdMap` to find a name by ID in [`../source/Config/Helpers.h`](../source/Config/Helpers.h).
- Identity helper logic exists in more than one place, for example `identityKey()` and `candidateMatches()` in [`../source/Engine/DrumKitEngine.cpp`](../source/Engine/DrumKitEngine.cpp) and [`../source/Engine/KitLibModel.cpp`](../source/Engine/KitLibModel.cpp).
- Map/fingerprint join helpers are repeated and partly called out by comments in [`../source/Engine/DrumKitEngineMapHelpers.cpp`](../source/Engine/DrumKitEngineMapHelpers.cpp).
- Many engine and UI paths repeatedly use `trim().toLowerCase()`, `compareIgnoreCase()`, or string-key maps. UI work is less urgent than engine runtime work.

## Plan

### 1. Cache Mic Bleed Lookup For Rendering

Priority: high.

Problem:

- Voice render currently does string-key lookup by mic code.
- This happens in the audio path and may repeat many times while voices render.

Proposed change:

- Add a cached numeric mic identity or cached bleed gain to `SampledSound`.
- Fill it during routing/load, when `routedMicCode` is already known.
- Keep `routedMicCode` only for UI, logs, and serialization.

Verification:

- Add or update a focused test for mic bleed behavior.
- Confirm rendered gain is the same before and after the change.
- Run focused tests when implementation is requested.

### 2. Improve `StringsDictSingleton`

Priority: high.

Problem:

- `getId()` is map-based and locked.
- `nameOf()` is worse because it locks and scans every map entry.

Proposed change:

- Keep the existing `name -> id` map.
- Add an `id -> name` vector for direct reverse lookup.
- Reserve both containers.
- Keep ID `0` as empty.

Verification:

- Add focused tests for empty name, repeated name, different names, and reverse lookup.
- Check that existing ID users still behave the same.

### 3. Replace Runtime String Maps With IDs Where Practical

Priority: high.

Problem:

- Some data is parsed from strings, then repeatedly used as strings later.
- Good candidates are mic bleed levels, mic route overrides, expression point codes, and some slot/articulation matching.

Proposed change:

- Convert strings to `StringsDictSingleton::Id` or compact enum values during parsing/load.
- Keep public config and UI APIs string-based where useful.
- Store both only where UI text is needed.

Verification:

- Add parser tests where map text or YAML behavior changes.
- Compare serialized text before and after where behavior must stay stable.

### 4. Precompute MIDI Controller Lookup Tables

Priority: high.

Problem:

- MIDI values are bounded to `0..127`, but some runtime paths still use maps or scans.
- `resolveArticulation()` checks `noteToControllers`, then scans `instrumentSlot.controllers`.
- `selectArticulationForController()` scans runtime articulations.

Proposed change:

- Build fixed tables during instrument load/rebuild:
  - note -> controller list or compact controller mapping
  - controller -> transition capability
  - controller value -> selected articulation
- Use these tables in note and controller handling.

Verification:

- Add focused tests for controller-to-articulation selection.
- Run existing synth/controller tests when implementation is requested.

### 5. Reduce Audio-Thread Locking

Priority: high, but more risky.

Problem:

- `processBlock()` and synth rendering rely on locks.
- The current try-lock can clear output if the graph is busy.
- Regular locks in render code can still block audio if another thread holds them.

Proposed change:

- Move toward immutable render snapshots:
  - Build new synth/routing data off the audio thread.
  - Publish it with an atomic/shared snapshot swap.
  - Let audio read stable arrays and pointers.
- Keep this as a separate change because it can affect many paths.

Verification:

- Add a small stress test or manual scenario for layout/map changes during playback.
- Run full shared-source tests when implementation is requested.
- Do listening checks because audio behavior can change even when tests pass.

### 6. Reduce Allocations In Runtime Structures

Priority: medium.

Problem:

- `VelocityLayerRuntime` stores `roundRobinCounter` as `std::shared_ptr<std::atomic<uint32_t>>`.
- `prepareToPlay()` uses a small `std::unordered_map<uint8_t, int>` while calculating max simultaneous samples.
- Some small runtime maps can probably be fixed arrays.

Proposed change:

- Make `VelocityLayerRuntime` move-only and store `std::atomic<uint32_t>` directly.
- Replace small MIDI/variant maps with arrays or stack buffers where sizes are known.
- Keep behavior unchanged.

Verification:

- Add compile coverage and focused round-robin tests.
- Run focused tests when implementation is requested.

### 7. Measure And Improve Memory Layout

Priority: medium.

Problem:

- Some structs/classes mix pointers, strings, ints, floats, bools, and atomics.
- Reordering may save RAM, but it should be measured first.

Candidate types:

- `SampledSound` in [`../source/Engine/InstrumentSynth.h`](../source/Engine/InstrumentSynth.h)
- `InstrumentSynthVoice` in [`../source/Engine/InstrumentSynth.h`](../source/Engine/InstrumentSynth.h)
- `SampleVariantSpec` in [`../source/Engine/KitLibModel.h`](../source/Engine/KitLibModel.h)
- `MicSpec` in [`../source/Engine/KitLibConsts.h`](../source/Engine/KitLibConsts.h)
- `DrumKitEngine::InstrumentMapEntry` in [`../source/Engine/DrumKitEngine.h`](../source/Engine/DrumKitEngine.h)

Proposed change:

- First add a temporary local size/offset check, or use compiler layout output if available.
- Reorder only fields that are private or safe to move.
- Prefer moving bools and small ints together after larger aligned fields.
- Do not change behavior during this step.

Verification:

- Record before/after sizes in the implementation PR or follow-up note.
- Build only when requested.

### 8. Move Repeated Helpers To Small Helper Files

Priority: medium.

Problem:

- Identity normalization and candidate matching are repeated.
- Map sorting/joining helpers are very similar.
- Instrument map parsing has local helpers that could become reusable if another parser needs them.

Proposed change:

- Add a small identity helper file under `source/Engine` or `source/Config`.
- Add a sorted join helper only if it is used by more than one call site.
- Keep helpers narrow and concrete.

Verification:

- Parser and serialization tests should still pass.
- Diff should show moved logic, not behavior changes.

### 9. UI String Cleanup Later

Priority: low.

Problem:

- UI code has many repeated lowercase/search helpers.
- This is usually not realtime, so it is lower risk and lower priority.

Proposed change:

- Cache normalized source paths and display text where lists are rebuilt.
- Reuse existing `UIHelpers` for source lookup instead of adding local wrappers.

Verification:

- Manual UI check for source search, source picker, and pad popup.

## Suggested Implementation Order

1. Cache mic bleed lookup for rendering.
2. Improve `StringsDictSingleton` reverse lookup.
3. Precompute MIDI/controller runtime tables.
4. Reduce audio-thread locking with render snapshots.
5. Reduce runtime allocations.
6. Measure and reorder memory layout.
7. Move repeated helpers.
8. Clean UI string helpers.

## Notes

- Do not combine all items in one change.
- Start with the smallest high-impact runtime fix.
- For code changes, write focused tests first where practical.
- Build and tests were intentionally not run for this plan.
