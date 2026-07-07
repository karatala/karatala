# Library Snapshot Reload Plan

Status: planned, not implemented.

## Problem

Pressing **Reload Library** can replace `KitLib::LibraryManager` while current `InstrumentSynth` objects still store raw pointers into the old library data.

Example:

- `InstrumentSynth::lastRoutingContext.defaultInstrument`
- `InstrumentSynth::lastRoutingContext.artSources`
- `InstrumentSynth::instrumentSpecstPtr`
- `SampledSound::instrumentSpec`
- `SampledSound::sampleVariantSpec`

After reload, these pointers can point to freed memory. Then a route rebuild can crash when it reads `InstrumentSpec` or `ArticulationSpec`.

Known crash path:

- `../source/PluginEditor.cpp`: `PluginEditor::reloadLibraryFromTroubleshooting`
- `../source/Engine/DrumKitEngineController.cpp`: `DrumKitEngine::reloadAll`
- `../source/Engine/DrumKitEngine.cpp`: `DrumKitEngine::useInstrumentMap`
- `../source/Engine/InstrumentSynthLoader.cpp`: `InstrumentSynth::resolveArticulationSpec`

## Goal

Reload the library without invalidating synths that are still in use.

Also reuse unchanged synths when the map and resolved library content did not change.

## Non-goals

- Do not rewrite the full kit model to use `shared_ptr` for every object.
- Do not hash audio data.
- Do not reload every slot if its map and resolved library content are unchanged.
- Do not change realtime audio behavior.

## Assumptions

- `LibraryManager` owns all `DrumKitSpec`, `InstrumentSpec`, `ArticulationSpec`, `VelocityLayerSpec`, and `SampleVariantSpec` objects.
- Keeping the owning `LibraryManager` alive keeps current raw model pointers valid.
- A metadata fingerprint is enough to detect normal library changes, including added, removed, renamed, or retagged sample files.
- Sample audio content can change without size or timestamp changing, but that is rare and is not handled by this plan.

## Design

### 1. Library snapshots

Change `DrumKitEngine::libraryManagerPtr` from:

```cpp
std::unique_ptr<KitLib::LibraryManager>
```

to:

```cpp
std::shared_ptr<KitLib::LibraryManager>
```

`DrumKitEngine::initSampleLibrary()` creates a new `shared_ptr<LibraryManager>` and scans it before publishing it as the current snapshot.

Old snapshots stay alive while old synths hold them.

### 2. Synth snapshot ownership

Add a member to `InstrumentSynth`:

```cpp
std::shared_ptr<const KitLib::LibraryManager> librarySnapshotPtr;
```

When loading a synth from an instrument, also pass the snapshot that owns that instrument.

Add APIs like:

```cpp
void loadInstrument(KitLib::InstrumentSpec* instSpec, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot);
void loadInstrumentWithSources(KitLib::InstrumentSpec* defaultInstrument, const ArticulationSourceMap& artSources, std::shared_ptr<const KitLib::LibraryManager> librarySnapshot, ...);
```

Keep existing raw pointers for fast access. The snapshot member only owns their lifetime.

When `InstrumentSynth::unloadInstrument()` runs, clear `librarySnapshotPtr`.

### 3. Copy snapshot on layout-only replacement

`../source/Engine/DrumKitEngineMapHelpers.cpp` has `createReplacementSynth()`.

When it copies a loaded synth because only layout data changed, copy the source synth snapshot too. This keeps old raw pointers valid in the replacement.

### 4. Library content fingerprints

Add fingerprint helpers to `KitLib`.

Suggested APIs:

```cpp
juce::String SampleVariantSpec::getContentFingerprint() const;
juce::String VelocityLayerSpec::getContentFingerprint() const;
juce::String ArticulationSpec::getContentFingerprint() const;
juce::String InstrumentSpec::getContentFingerprint() const;
juce::String DrumKitSpec::getContentFingerprint() const;
juce::String LibraryManager::getContentFingerprint() const;
```

Instrument fingerprint should include:

- kit identity
- instrument identity
- articulation code and expression ids
- expression dimensions
- velocity values
- sample file full path
- sample file size
- sample file modification time
- mic spec data
- source channel start/count
- volume trim
- peak/decay metadata

Use sorted order so fingerprints are stable.

### 5. Slot fingerprint includes resolved content

`../source/Engine/DrumKitEngineMapHelpers.cpp` currently builds slot fingerprints from map text only.

Add resolved library content to the slot fingerprint after map entries are resolved.

For each slot, include:

- current map entry fingerprint
- primary source instrument content fingerprint
- extra source instrument content fingerprints from layered articulations
- art remap and alias data that affects routing

This means:

- same map + same resolved library content = reuse synth
- same map + changed source samples/metadata = reload that slot
- changed map = reload that slot

### 6. Use new snapshot during `useInstrumentMap`

In `DrumKitEngine::useInstrumentMap()`:

- capture `auto librarySnapshot = this->libraryManagerPtr;`
- resolve kits and instruments from `librarySnapshot`
- pass `librarySnapshot` into every new `InstrumentSynth`
- build slot fingerprints with resolved content from `librarySnapshot`

This avoids mixing a new snapshot for resolving with an old snapshot for loading.

### 7. Reload Library flow

In `PluginEditor::reloadLibraryFromTroubleshooting()`:

- keep the current high-level flow:
  - `engine.reloadAll()`
  - `engine.useState(...)`
- after `reloadAll()`, `useInstrumentMap()` compares the old slot fingerprints with the new resolved content fingerprints.
- unchanged slots are reused safely because they still own their old snapshot.
- changed slots are replaced and own the new snapshot.

## Success Criteria

- Reload Library no longer crashes when routing rebuild runs after reload.
- Unchanged slots are not reloaded.
- Changed slots are reloaded.
- Old synths can safely finish pending sample callbacks because their old snapshot remains alive.
- No audio-thread locking or file scanning is added.

## Implementation Steps

1. Add snapshot member and accessors to `InstrumentSynth`.
2. Convert `DrumKitEngine::libraryManagerPtr` to `std::shared_ptr`.
3. Pass the current snapshot into all synth load paths.
4. Preserve the snapshot in layout-only replacement synths.
5. Add model fingerprint helpers.
6. Add resolved-content slot fingerprints in `useInstrumentMap()`.
7. Re-check reload paths and default-kit map paths.
8. Add focused tests for fingerprint stability and slot reload decisions.

## Verification Plan

Do not run until requested.

Suggested checks:

- Build `Tests`.
- Add or run tests that prove:
  - same library content gives same fingerprint
  - modified sample metadata gives different fingerprint
  - same map with same content keeps slot unchanged
  - same map with changed resolved content reloads only that slot
- Manual REAPER check:
  - load a kit
  - wait briefly
  - press **Reload Library**
  - confirm no crash
  - confirm changed files reload
  - confirm unchanged slots do not reload

## Open Questions

- Should fingerprints include a cheap whole-library generation number for logs only?
- Should sample content hash be optional for a future deep scan mode?
- Should preview sample jobs also hold a library snapshot, or are their copied file paths enough?

