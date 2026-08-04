# Codebase Maintainability Review

Status: static review complete. Project decisions updated on 2026-07-31. Phases 1 to 3, the production part of Phase 4, and Phase 4b were implemented on 2026-08-04. Completed work is marked with ✅.

## Scope and limits

This review covers first-party C++ source, tests, benchmarks, and Python tools. It does not review JUCE, other vendored code, or generated instrument data as normal hand-written source.

File sizes and include paths were checked statically. The measured facts below are the baseline before implementation unless a row is marked as updated.

The include result measures potential rebuild scope:

- A source file is counted when it can reach a header through project `#include "..."` paths.
- It does not measure compiler time.
- A high count is not automatically a defect. A small and stable shared type can have a high count safely.
- A large or frequently changed header with a high count is a better cleanup target.

This review complements [CODEBASE_IMPROVEMENT_PLAN.md](CODEBASE_IMPROVEMENT_PLAN.md) and its [Workstream 1 audit](CODEBASE_IMPROVEMENT_WORKSTREAM_1_AUDIT.md). Those documents already define the rules for helper extraction, behavior checks, and realtime safety.

## Main result

The main structural problems are:

1. `DrumKitEngine` and some UI files contain too many responsibilities.
2. ✅ The repeated preset behavior was consolidated in the single `KitPage`, and the experimental old Kit page was removed.
3. Broad headers connect unrelated parts of the project. This is usually called transitive include coupling or high header fan-out.
4. Several large implementations are inside headers. A small header edit can therefore make many `.cpp` files parse the same implementation again.
5. Helper files and helper namespaces have unclear ownership in several folders. Moving all of them to one common folder would hide the problem instead of fixing it.
6. The DrumGizmo converter keeps a program inside another program and generates very large Python files that mostly contain data. This is accepted temporary code and has the lowest cleanup priority.

Recommended order:

1. Fix small exact duplicates and one clear placeholder.
2. Reduce header coupling.
3. Organize helpers by feature and responsibility.
4. ✅ Keep useful complete-kit and preset behavior in `KitPage`, with layout defaults in Conf and per-slot assignment in the pad popup.
5. Split large implementation files without behavior changes.
6. Divide the largest functions only after the physical file splits are stable.
7. Verify the DrumGizmo conversion result, then archive or remove the temporary converter files.

## Measured facts

### Largest reviewed files

| File | Lines | Main concern |
| --- | ---: | --- |
| [DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp) | 3,234 | Engine setup, settings, library edits, graph rebuild, preview, live updates, and map application are together. |
| [convert-drumgizmo2karatala.py](../scripts/convert-drumgizmo2karatala.py) | 2,119 | Current and legacy flows plus an embedded generated runtime are together. |
| [PadContextPopup.cpp](../source/UI/PadContextPopup.cpp) | 1,888 | UI construction, library lookup, routing, linked edits, dialogs, and layout are together. |
| [TestInstrumentPlayer.cpp](../tests/TestInstrumentPlayer.cpp) | 1,806 | Several different instrument player behaviors still share one test file. The old voice-grouping-only name was corrected during Phase 4b. |
| [LibraryArticulationEditor.cpp](../source/UI/LibraryArticulationEditor.cpp) | 1,394 | View, state, persistence, preview, and input are together. |
| [InstrumentPlayerController.cpp](../source/Engine/InstrumentPlayerController.cpp) | 1,375 | Large realtime controller and aftertouch workflows are together. |
| [InstrumentPlayerLoader.cpp](../source/Engine/InstrumentPlayerLoader.cpp) | 1,343 | Loading, live updates, parameters, and routing are together. |
| `KitLibModel.cpp` (baseline file, now removed) | 1,168 | Model parsing, calibration, sample scanning, and library scanning were together. |
| [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h) | 815 | Several public classes, runtime records, and inline logic are in one header. |
| [KitLibModel.h](../source/Engine/KitLibModel.h) | 608 | Seven linked model types and several shared enums are in one header. |
| [LayoutManager.h](../source/Engine/LayoutManager.h) | 543 | About 340 lines of YAML parsing, validation, and writing are in a header. |

The seven generated `instruments_py/*/convert_*.py` files contain 723,764 lines and use about 22 MB. Most lines create operation records. They are generated data expressed as Python code, not seven separate hand-written implementations.

### Static include reach

The scan found 46 production `.cpp` files and 17 test or benchmark `.cpp` files.

| Header | Direct production users | Production files reached transitively | Test or benchmark files reached transitively |
| --- | ---: | ---: | ---: |
| [Helpers.h](../source/Config/Helpers.h) | 10 | 42 of 46 | 13 of 17 |
| [PluginLogger.h](../source/Config/PluginLogger.h) | 4 | 42 of 46 | 13 of 17 |
| [RoutingTypes.h](../source/Engine/RoutingTypes.h) | 6 | 40 of 46 | 13 of 17 |
| [KitLibModel.h](../source/Engine/KitLibModel.h) | 13 | 39 of 46 | 13 of 17 |
| [LayoutManager.h](../source/Engine/LayoutManager.h) | 12 | 31 of 46 | 11 of 17 |
| [UITheme.h](../source/UI/UITheme.h) | 26 | 26 of 46 | 2 of 17 |

`RoutingTypes.h` is a valid candidate for a small, stable shared header. The more useful targets are `Helpers.h`, `PluginLogger.h`, `KitLibModel.h`, `LayoutManager.h`, and `UITheme.h`.

## Priority 1: Reduce include coupling

### 1. ✅ Remove logging from the general helper chain

Evidence:

- [Helpers.h](../source/Config/Helpers.h), line 4, includes the complete logger header.
- `Helpers.h` does not use the logger.
- [PluginLogger.h](../source/Config/PluginLogger.h) contains about 300 lines of thread, queue, file, timestamp, and logging implementation.
- Many files use `LOG` only because another header includes `PluginLogger.h` first. [PluginProcessor.h](../source/PluginProcessor.h), for example, uses `LOG` in inline code without a direct logger include.

Suggestion:

1. Add direct logger includes only to files that use logging.
2. Remove `PluginLogger.h` from `Helpers.h`.
3. Move logger thread and file implementation to `PluginLogger.cpp`.
4. If settings need only `DebugLevel`, put that enum and its text conversion declarations in a small `LogLevel.h`.
5. Add the standard-library includes that `Helpers.h` uses directly. Do not depend on JUCE or logger headers to provide them.

Expected result: a logger implementation change no longer reaches almost every production file through `Helpers.h`.

### 2. ✅ Separate instrument type from the full library model

Evidence:

- [UITheme.h](../source/UI/UITheme.h), line 4, includes all of `KitLibModel.h`.
- The only model item used by this theme header is `KitLib::InstrumentSpec::Type`.
- `UITheme.h` is included directly by 26 production files.
- Layout and catalog headers also use the same nested type.

Suggestion:

1. Move the instrument type enum and its text conversions to a narrow `InstrumentType.h`.
2. Let `UITheme.h`, layout types, the catalog, and the full model include that narrow header.
3. Keep model scanning, samples, overrides, and library ownership out of the type header.

Expected result: a library model edit does not force most UI files to parse the model chain.

### 3. ✅ Move YAML implementation out of `LayoutManager.h`

Evidence:

- [LayoutManager.h](../source/Engine/LayoutManager.h) is 543 lines.
- YAML parsing, validation, and writing occupy most of lines 151-490.
- Twelve source or test files include it directly.

Suggestion:

1. Keep layout data declarations and the generic manager template in headers.
2. Keep only declarations for the explicit `LayoutTraits<KitLayout>` and `LayoutTraits<MicLayout>` specializations.
3. Move those specialization definitions and non-template layout methods to `LayoutYaml.cpp` and suitable layout implementation files.
4. Consider a narrow `LayoutTypes.h` only after the implementation move proves useful.

Expected result: a YAML parser change has a smaller potential rebuild scope.

### 4. ✅ Remove full engine includes from map helper headers

Evidence:

- Before Phase 2, `DrumKitEngineMapHelpers.h`, `DrumKitEngineMappingModel.h`, and `DrumKitEngineTextView.h` included all of `DrumKitEngine.h`.
- They need nested map types such as `DrumKitEngine::InstrumentMapLayer`.
- [DrumKitEngine.h](../source/Engine/DrumKitEngine.h) includes the full instrument player, layout, catalog, routing, loader, settings, and velocity response headers.

Suggestion:

1. Move `InstrumentMapLayer` and `InstrumentMap` to `Engine/InstrumentMap/InstrumentMap.h`.
2. Move library kit override view records to a narrow header only when this reduces include dependencies.
3. Move `LayerParameterUpdate` out of the nested instrument player type if the engine only needs this small update record.
4. Move the `DrumKitEngine` constructor, destructor, and other non-trivial inline methods to `.cpp` files.

Expected result: map parsing, formatting, and UI code can use map data without including the full audio engine.

### 5. ✅ Move practical complete implementations out of headers

Review these after the first four changes:

- [InstrumentCatalog.h](../source/Engine/InstrumentCatalog.h) is mostly inline parsing and catalog logic.
- [MicRoutingPlan.h](../source/Engine/MicRoutingPlan.h) contains its routing implementation inline.
- [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h) contains `SampledSound`, `InstrumentPlayerVoice`, `InstrumentPlayer`, many runtime records, and non-trivial inline methods.

Move normal implementation to `.cpp` files first. Split public types only when a real caller needs the smaller boundary. Do not add a private implementation pattern to every class without a measured need.

## Priority 1: Consolidate repeated behavior

### 1. Consolidate preset behavior, then remove the old Kit page

Implementation result:

- ✅ [KitPage.cpp](../source/UI/KitPage.cpp) is the single owner of complete-kit loading, preview, and instrument-map preset storage.
- ✅ `Save` overwrites only the selected user preset. `Save As` rejects an existing user preset name case-insensitively across `.map` and `.txt` entries.
- ✅ Factory presets are listed before user presets. Each group is sorted by name. Refresh keeps the selected path when it still exists and otherwise selects the first visible entry.
- ✅ The old draft layout, slot metadata, and mic-layout controls were not copied. Conf already owns machine-wide kit and mic layout defaults. The pad popup owns whole-instrument replacement. The Map page owns active MIDI layout edits.
- ✅ `KitHomePanel`, its helper, the old sidebar route, and the `Browse` route were removed. `KitBrowserComponent` was renamed to `KitPage`.
- Usage testing is still required.

The preset file operations remain private in `KitPage.cpp`. Do not extract an `InstrumentMapPresetStore` unless another real caller needs the same storage rules or this implementation becomes too large.

### 2. Consolidate library item resolution

[DrumKitEngineLibraryKitOverrides.cpp](../source/Engine/DrumKitEngineLibraryKitOverrides.cpp) repeats the library kit, instrument, articulation, and write-access lookup in eight library edit paths.

Add one small resolver for this library item chain. Keep lock ownership and pointer lifetime explicit. Keep the write-access decision at the caller.

Do not return pointers that outlive the active library snapshot.

The manual-gain and playback-offset paths also repeat moved-file recovery and ambiguity checks. Extract the common sample target resolution, but keep mic-specific validation in the playback-offset path.

### 3. Consolidate library item lookup in `PadContextPopup`

[PadContextPopupRouting.cpp](../source/UI/PadContextPopupRouting.cpp) resolves the same library item and articulation set twice. Only the final mic filter differs.

Add a private `resolveLibraryArticulationsForPath()` operation. Preserve these current rules:

- A specific articulation selects its matching base articulation.
- A library item without an articulation uses all articulations.

### 4. Consolidate exact small copies

Good small candidates:

- ✅ [PluginEditor.cpp](../source/PluginEditor.cpp) now creates its processing suspension guard through one local operation. The guard remains before the callback lock at all four call sites.
- ✅ The cohesive [Parsing.h](../source/Config/Parsing.h) and [Parsing.cpp](../source/Config/Parsing.cpp) module now owns parse results, wrapped-text parsing, and strict finite-float parsing. The float parser preserves full-input, range, finite-value, and locale behavior.
- `parseSignedGainDb()` and `parseSignedOffsetMs()` in [KitLibInstrument.cpp](../source/Engine/KitLibInstrument.cpp) can share one signed metadata number parser while retaining clear wrapper names.
- Search term syntax is repeated in [KitPage.cpp](../source/UI/KitPage.cpp) and [SourcePickerPopup.cpp](../source/UI/SourcePickerPopup.cpp). Add one UI search matcher.
- Sample load type groups are repeated in two large switches in [SampleLoader.cpp](../source/Engine/SampleLoader.cpp), lines 53-179. Use one local load-class classifier, but keep the special kick, snare, hi-hat, ride, crash, and tom rules explicit.
- Data path resolution is repeated in [DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp), lines 161-172, and [DrumKitEngineController.cpp](../source/Engine/DrumKitEngineController.cpp), lines 23-30. Use one constructor-independent data-path resolver.
- The same 16-byte BLAKE2b file identity is implemented in [generate_sample_metadata.py](../scripts/generate_sample_metadata.py), lines 232-238, and [apply_kit_overrides.py](../scripts/apply_kit_overrides.py), lines 47-55. Put the standard-library-only algorithm in one script module.

The exact `identityKey()` and `candidateMatches()` merge is accepted in the [Workstream 1 audit](CODEBASE_IMPROVEMENT_WORKSTREAM_1_AUDIT.md).

Merge it when map types move to the `InstrumentMap` feature pair. Do not create an identity-only file or a feature pair for this small merge.

### Do not merge these only because they look similar

- Instrument-map fingerprints and serialized text have different field and formatting contracts.
- Active voice loops have different filters, order, and realtime mutation rules.
- MIDI clamping and MIDI validation have different behavior.
- The independent audio analysis comparison tool should not share all calculation logic with the production metadata generator. Independent code can detect a production calculation defect.

Avoid a generic helper with many flags or callbacks. Extract one shared primitive when the complete workflows are different.

## Priority 1: Organize helper files and namespaces

Use [PROJECT_TERMINOLOGY_REVIEW.md](PROJECT_TERMINOLOGY_REVIEW.md) before naming new feature folders, classes, or namespaces. In particular, do not use bare `Source` as a feature name.

### Main rule

Do not create one `source/helpers/` folder.

A common helper folder usually becomes a place for unrelated code. It also makes ownership less clear and can create more cross-domain includes. Use this order instead:

1. If only one `.cpp` file uses the helper, keep it in that `.cpp` file.
2. If several files in one feature use it, create a named feature module.
3. If several features in one domain use it, place it in that domain with a precise name.
4. Create a precisely named project-wide support module only when engine, UI, and configuration code all need the same small and stable rule.

Do not create a standalone header and implementation pair for one small generic function. Add it to a cohesive existing support module or keep it private in the owning `.cpp` file.

Use names that describe the owner. Prefer `InstrumentMap`, `ArticulationNames`, or `PathResolver` over `Helpers`.

### Current helper inventory

The broad production helper files from this review are now removed or narrowed. Cohesive modules such as `YamlHelper` keep their established names.

| Current file or group | Suggested ownership |
| --- | --- |
| [Helpers.h](../source/Config/Helpers.h) | ✅ Parse results and wrapped-text parsing moved to `Config/Parsing`. Continue splitting path resolution, MIDI values, and string IDs by concern. |
| [YamlHelper.h](../source/Config/YamlHelper.h) and [YamlHelper.cpp](../source/Config/YamlHelper.cpp) | This is one cohesive YAML module. It can stay in `Config`. Rename it to `YamlParser` only when related code is already being changed. |
| `DrumKitEngineMapHelpers.h/.cpp` | ✅ Removed. Map data and text stay in `Engine/InstrumentMap/InstrumentMap.*`; reusable layout/library-aware operations are in `InstrumentMapOperations.*`; player lifecycle helpers are private in `DrumKitEngine.cpp`. |
| [ArticulationNames.h](../source/Engine/ArticulationNames.h) | ✅ Renamed by responsibility and kept as one cohesive `Engine` header. Create an articulation feature folder only if this module grows to several files. |
| `DrumKitEnginePreviewHelpers.h` | ✅ Removed. Its one-use private logic is now in `DrumKitEnginePreview.cpp`. |
| `UIHelpers.h/.cpp` | ✅ Removed. `UIComponentStyle.*` owns styling, `SourceItem.*` owns library source lookup, and concrete controls own MIDI assignment and compact value text. |
| `KitHomePanelHelpers.h` | ✅ Removed with the old Kit page. Its one remaining preset-name operation is private in `KitPage.cpp`. |
| `DrumPadHelpers.h` | ✅ Removed. Its private operations are in `DrumPad.cpp`. |
| `ParameterPotHelpers.h` | ✅ Removed. Its small operations are private in `HorizontalKnobBrick.cpp`, `EngineSettingsPanel.cpp`, and `TroubleshootingPanel.cpp`. |

Durable feature folder:

```text
source/
  Engine/
    InstrumentMap/
```

Create a folder only when several files share one responsibility. Do not create a folder for every small helper.

Keep renamed library UI files in `UI/` during the first change. Review a `UI/Library/` folder only if the group later needs a clear boundary.

### Namespace rule

The number of namespaces is not a problem by itself. Namespaces such as `KitLib`, `UITheme`, `HiHatTransitionPolicy`, and the YAML module have clear ownership.

Folders make files easier to find. Namespaces prevent name conflicts and show code ownership. They solve different problems, so a folder move alone does not require a new namespace hierarchy.

Use these rules:

- Reusable functions should use a feature namespace, such as `InstrumentMap` or `ArticulationNames`.
- Private functions used by one `.cpp` file should be file-local `static` functions, or private static class methods when class ownership is clear.
- When a complete implementation belongs to one feature namespace, keep the complete `.cpp` inside that namespace and keep its private functions `static`.
- Do not mix small private namespace blocks with class or global implementations in the same `.cpp`.
- Extract a separate module only when several related operations have multiple callers or create a durable dependency boundary.
- Do not create a public `FooHelpers` namespace only because the file contains free functions.
- Do not add one new top-level namespace to the complete codebase as part of this cleanup. That would create a large diff without solving the current ownership problems.
- Folder and namespace names should normally describe the same feature.

### Safe migration order

1. ✅ Remove one-use helper headers by moving their code into the owning `.cpp`.
2. ✅ Removed `KitHomePanelHelpers.h` as part of the Kit page replacement.
3. ✅ Create the `InstrumentMap` feature pair when map types are extracted from `DrumKitEngine`.
4. ✅ Split `UIHelpers` into `UIComponentStyle`, `SourceItem`, and concrete control ownership.
5. Split `Config/Helpers.h` one concern at a time. ✅ Parsing support moved first to the cohesive `Config/Parsing` module. Do not create a new general helper file.

This order avoids a large move-only change and keeps each diff connected to a real cleanup.

## Priority 1 and 2: Split large files by responsibility

Start with physical moves of existing member functions. Do not change behavior during the first split.

| Current file | First physical split | Later logic split |
| --- | --- | --- |
| [DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp) | ✅ `DrumKitEngineLibraryKitOverrides.cpp`, `DrumKitEngineSettings.cpp`, `DrumKitEnginePreview.cpp`, and `DrumKitEngineMapApply.cpp` | Divide the roughly 706-line `useInstrumentMapInternal()` into preparation without the graph lock and a short locked commit using named plan records. |
| [InstrumentPlayerController.cpp](../source/Engine/InstrumentPlayerController.cpp) | ✅ `InstrumentPlayerHiHat.cpp`, `InstrumentPlayerAftertouch.cpp`, and `InstrumentPlayerNotePlayback.cpp` | Divide the roughly 423-line hi-hat transition into bounded, allocation-free steps. Preserve voice order and accepted sound behavior. |
| [PadContextPopup.cpp](../source/UI/PadContextPopup.cpp) | ✅ `PadContextPopupMapLayers.cpp`, `PadContextPopupRouting.cpp`, and `PadContextPopupDialogs.cpp` | Add one owned linked-edit context for parsed map and projected targets. Never return pointers into temporary projection data. |
| [InstrumentPlayerLoader.cpp](../source/Engine/InstrumentPlayerLoader.cpp) | ✅ `InstrumentPlayerLibraryUpdates.cpp`, `InstrumentPlayerParameters.cpp`, and `InstrumentPlayerRouting.cpp`; sample loading stays in the narrowed original file | Divide `createVelocityLevels()` into sound creation, logical-hit grouping, early-load selection, and queue submission. |
| `KitLibModel.cpp` (removed) | ✅ `KitLibValueTypes.cpp`, `KitLibInstrument.cpp`, and `KitLibLibrary.cpp` | Keep calibration with the instrument implementation unless it grows into a substantial independent module. Split `KitLibModel.h` later, starting with independent enums and small records. Parent pointers make an immediate full header split risky. |
| [LibraryArticulationEditor.cpp](../source/UI/LibraryArticulationEditor.cpp) | ✅ Main component, `LibraryArticulationEditorState.cpp`, and `LibraryArticulationEditorPreview.cpp` | Consider a separate selection state model only after the low-risk file split. |
| [PluginEditor.cpp](../source/PluginEditor.cpp) | ✅ Main coordinator and `PluginEditorPresetsAndPopups.cpp` | Keep the editor as the coordinator. Split presets and popups only when either becomes a substantial independent module. |
| [TestInstrumentPlayer.cpp](../tests/TestInstrumentPlayer.cpp) | Shared test support plus routing, hi-hat, rendering, and aftertouch test files | The misleading voice-grouping-only name and anonymous namespace are fixed. The behavior split remains after focused test verification. More test files can make a clean test build slightly slower. |

The file names above describe responsibility boundaries, not a required file-count target. Do not create separate decay, mic, calibration, kit, or manager implementation files when the result is only a small group of closely related functions. Prefer implementation files of roughly 300 to 800 lines, but allow smaller files when they provide a useful public dependency boundary.

The intended engine structure after the physical split is:

```text
source/Engine/
  DrumKitEngine.h
  DrumKitEngine.cpp
  DrumKitEngineSettings.cpp
  DrumKitEnginePreview.cpp
  DrumKitEngineLibraryKitOverrides.cpp
  DrumKitEngineMapApply.cpp
  InstrumentMap/
    InstrumentMap.h
    InstrumentMap.cpp
    InstrumentMapOperations.h
    InstrumentMapOperations.cpp
  KitLibModel.h
  KitLibValueTypes.cpp
  KitLibInstrument.cpp
  KitLibLibrary.cpp
  SampleMetadata.h
  SampleMetadata.cpp
  InstrumentType.h
  InstrumentType.cpp
```

Keep `InstrumentMap.h` narrow. Put operations that need layout or library model types in `InstrumentMapOperations.h`. Keep player creation, preparation, and reuse private in `DrumKitEngine.cpp`; keep map preparation and commit in `DrumKitEngineMapApply.cpp`.

## Lowest priority: Verify, then archive or remove the converter

Decision: do not spend normal cleanup time on converter structure now. The large generated files are acceptable temporary artifacts because the converter will be archived or removed after its result is fully verified.

Evidence:

- [convert-drumgizmo2karatala.py](../scripts/convert-drumgizmo2karatala.py), lines 1551-1817, contains a generated Python runtime as a large string.
- That runtime closely repeats [drumgizmo_runtime.py](../instruments_py/_common/drumgizmo_runtime.py).
- The old `process_kit()` and `legacy_copy_main()` flow at lines 825-1049 is not called by the current main path inside this repository.
- The seven generated converter files contain 723,764 lines, mostly operation construction.
- No focused test was found for output plan generation or generated runtime behavior.

Suggestion:

1. Freeze converter feature changes unless a verification defect requires a fix.
2. Define the final verification checklist for files, paths, channel mapping, hashes, operation counts, and audit results.
3. Verify at least one small and one large converted kit.
4. Keep the current generated artifacts unchanged while verification is in progress.
5. After the conversion result is accepted, archive or remove the converter, embedded runtime, legacy path, and generated operation scripts together.

If the converter must stay maintained for a longer time, then make the shared runtime canonical and replace Python operation construction with a data manifest. Do not perform that refactor only to delete the result soon afterward.

## Other problems

### ✅ Host-facing placeholder parameter

The unused `kitLayoutChoice` parameter and its placeholder host-facing choice were removed from [PluginProcessor.cpp](../source/PluginProcessor.cpp).

### Stale and deprecated code needs targeted cleanup

The source tree has many `TODO`, `FIXME`, and similar markers. Do not remove them as one broad cleanup. Check each reference after the related file split.

One example shows why this must be careful: a comment says `findArticulationIndex()` is unused, but loader code calls it. In contrast, some old converter and analysis paths appear to have no current caller.

The scripts contract marks several analysis scripts as deprecated experiments. Remove or archive clear backup files such as `analyze_drum_samples~.py` and `_unused.py` only after checking the documented comparison workflow. Do not build production helpers around deprecated copies.

## Brief implementation plan

### Phase 1: Small and exact changes

1. ✅ Removed the unused placeholder plugin parameter.
2. ✅ Defined the processing suspension guard once.
3. ✅ Consolidated strict float parsing.

Verification for this phase:

- ✅ Added focused shared parsing coverage and identified the existing map parsing and serialization tests.
- Run library identity, map parsing, and serialization tests.

### Phase 2: Helper and header boundaries

1. ✅ Removed the logger include from `Helpers.h` and added direct includes at real users.
2. ✅ Moved logger implementation to `PluginLogger.cpp` and log-level declarations to `LogLevel.h`.
3. ✅ Extracted the narrow instrument type.
4. ✅ Moved layout YAML implementation out of `LayoutManager.h`.
5. ✅ Moved instrument map types, parsing, and serialization into `Engine/InstrumentMap/InstrumentMap.h` and `InstrumentMap.cpp`.
6. ✅ Merged the accepted identity functions into the same feature pair.
7. ✅ Removed the one-use preview, map text-view, and drum-pad helper headers.
8. ✅ Moved catalog and mic-routing implementation plus practical non-trivial engine and instrument-player methods out of headers.

Verification for this phase:

- Run the full test target after each shared header change.
- Compare clean build time and incremental rebuild time before and after.
- Change one representative leaf header and record which `.cpp` files rebuild.
- Confirm that every header includes the types that it uses directly.

### Phase 3: Replace the old Kit page

1. ✅ Listed the useful Kit page elements and rules.
2. ✅ Kept complete-kit loading and preset behavior in the replacement. Kept layout defaults in Conf and per-slot assignment in the pad popup.
3. ✅ Defined one preset policy: `Save` overwrites only the selected user preset, and `Save As` rejects a case-insensitive duplicate user name.
4. Usage-test the Kit page.
5. ✅ Removed the old Kit page and its remaining helper code.
6. ✅ Renamed `KitBrowserComponent` to `KitPage` and removed `PageId::Browse`.

Verification for this phase:

- Check preset scan, load, save, overwrite, and delete behavior.
- Check filtering, selection, kit loading, and instrument-map loading.
- ✅ Static search confirms that no old Kit page route or `KitHomePanel` source reference remains.

### Phase 4: Physical file splits

1. ✅ Split `DrumKitEngine`, `InstrumentPlayerController`, `InstrumentPlayerLoader`, and the `KitLibModel` implementation by responsibility.
2. ✅ Split `PadContextPopup` and `LibraryArticulationEditor` by UI responsibility.
3. ✅ Kept `PluginEditor` as the main coordinator and grouped its smaller preset and popup responsibilities in one secondary implementation file.
4. Rename the broad instrument-player test file and remove its anonymous helper namespace. ✅ `TestInstrumentPlayer.cpp` now uses one feature namespace. Divide it by behavior later without changing test logic or creating a large support layer only to reduce line count.

Verification for this phase:

- ✅ CMake source registration uses `GLOB_RECURSE CONFIGURE_DEPENDS`, so the new implementation files are included without a manual source list.
- Review each move-only diff for accidental logic edits.
- Run focused tests, then the full test target.
- Do not add benchmarks for a pure file move.

### ✅ Phase 4b: Namespace and API closeout

Do this after the move-only Phase 4 diff is clear:

1. ✅ Replaced public `FooHelpers` namespaces with the owning feature namespace or concrete type.
2. ✅ Removed private namespace blocks that only grouped file-local helpers. Private helpers are `static`, and complete feature implementations use one namespace context.
3. ✅ Removed public declarations and forwarding wrappers that had no caller outside their implementation file.
4. ✅ Moved reusable map operations to `InstrumentMap/InstrumentMapOperations.h/.cpp` under the `InstrumentMap` namespace.
5. ✅ Kept engine-only map application in `DrumKitEngineMapApply.cpp` and player lifecycle helpers private in `DrumKitEngine.cpp`.
6. ✅ Moved library identity comparison out of `InstrumentMap` and into the existing `KitLib` model API without an identity-only file pair.
7. ✅ Audited all production, test, and benchmark namespace blocks, not only `DrumKitEngine`, `KitLibModel`, and `InstrumentMap`.

Verification for this phase:

- ✅ Searched every exported operation for a caller outside its implementation file.
- ✅ Confirmed that `DrumKitEngineMapHelpers` and broad private `*Helpers` namespaces are gone.
- ✅ Confirmed that no `.cpp` mixes a small private namespace block with class or global implementations.
- ✅ Recorded the final relevant tree below. `InstrumentMap/` contains four cohesive files; no new folder or atomic helper pair was created.

Final relevant ownership tree:

```text
source/
  Config/
    Parsing.h/.cpp
    YamlHelper.h/.cpp
    PluginLogger.h/.cpp
  Engine/
    DrumKitEngine.h
    DrumKitEngine.cpp
    DrumKitEngineSettings.cpp
    DrumKitEnginePreview.cpp
    DrumKitEngineLibraryKitOverrides.cpp
    DrumKitEngineMapApply.cpp
    InstrumentMap/
      InstrumentMap.h/.cpp
      InstrumentMapOperations.h/.cpp
    KitLibModel.h
    KitLibValueTypes.cpp
    KitLibInstrument.cpp
    KitLibLibrary.cpp
    InstrumentType.h/.cpp
    SampleMetadata.h/.cpp
    LibraryKitOverrides.h/.cpp
  UI/
    UITheme.h
    UIComponentStyle.h/.cpp
    SourceItem.h/.cpp
    MidiNoteAssignmentControl.h/.cpp
    CompactParameterControl.h/.cpp
    ParameterPot.h/.cpp
    HorizontalKnobBrick.h/.cpp
tests/
  TestInstrumentMap.cpp
  TestInstrumentPlayer.cpp
  TestSourceItem.cpp
benchmarks/
  Benchmarks.cpp
```

### Phase 5: Divide large workflows

1. Divide instrument-map preparation from its locked commit.
2. Divide hi-hat and aftertouch workflows into bounded realtime-safe steps.
3. Add owned linked-edit state to `PadContextPopup`.
4. Divide velocity-level construction into named phases.

Verification for this phase:

- Run focused engine, routing, library kit override, and UI behavior tests.
- Use listening checks for hi-hat, aftertouch, choke, phase, gain, and routing changes.
- Confirm that realtime helpers allocate no memory, perform no blocking I/O, and preserve iteration order.

### Phase 6: Converter closeout

This is the lowest-priority phase.

1. Complete the final conversion verification.
2. Archive or remove the converter and generated operation scripts.
3. Refactor the converter only if it must remain maintained.

## Completion criteria

The cleanup is complete when:

- `Helpers.h` no longer brings logging into unrelated files.
- `UITheme.h` no longer includes the complete library model.
- Map helper headers no longer include the complete engine only for nested data types.
- Generic helper files have been removed, renamed by responsibility, or placed in a clear feature module.
- Public declarations have a real caller outside their implementation file or are required stable API entry points. Trivial forwarding wrappers are removed.
- One-use private helpers are file-local `static` functions or private static class methods, and complete feature implementations use one namespace context.
- The useful Kit page behavior exists in the usage-tested Browse replacement.
- The old Kit page and its unused helper code are removed. `KitPage` is the single Kit sidebar page.
- Instrument-map preset storage has one explicit same-name policy in the remaining screen.
- Exact repeated parsing and guard logic has one owner.
- The large engine and UI files are separated by responsibility.
- The final relevant `source/` tree is recorded in this review and has no folder or implementation pair created only for one small generic operation.
- Instrument-map preparation and locked commit are visibly separate.
- The verified converter and generated operation scripts are archived or removed. If they remain active, they have one runtime implementation and one canonical operation manifest.
- Focused tests, the full test target, relevant listening checks, and incremental build measurements pass.
