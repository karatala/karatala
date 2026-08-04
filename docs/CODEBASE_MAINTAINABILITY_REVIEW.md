# Codebase Maintainability Review

Status: static review complete. Project decisions updated on 2026-07-31. Phases 1 and 2 were implemented on 2026-08-04. Completed work is marked with ✅.

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
2. The old Kit page and the new Browse page contain repeated instrument-map preset behavior. The old Kit page will be removed.
3. Broad headers connect unrelated parts of the project. This is usually called transitive include coupling or high header fan-out.
4. Several large implementations are inside headers. A small header edit can therefore make many `.cpp` files parse the same implementation again.
5. Helper files and helper namespaces have unclear ownership in several folders. Moving all of them to one common folder would hide the problem instead of fixing it.
6. The DrumGizmo converter keeps a program inside another program and generates very large Python files that mostly contain data. This is accepted temporary code and has the lowest cleanup priority.

Recommended order:

1. Fix small exact duplicates and one clear placeholder.
2. Reduce header coupling.
3. Organize helpers by feature and responsibility.
4. Move useful Kit page behavior to Browse, remove the old Kit page, and rename Browse to Kit.
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
| [TestInstrumentPlayerVoiceGrouping.cpp](../tests/TestInstrumentPlayerVoiceGrouping.cpp) | 1,797 | Several different instrument player behaviors share one test file. |
| [LibraryArticulationEditor.cpp](../source/UI/LibraryArticulationEditor.cpp) | 1,394 | View, state, persistence, preview, and input are together. |
| [InstrumentPlayerController.cpp](../source/Engine/InstrumentPlayerController.cpp) | 1,375 | Large realtime controller and aftertouch workflows are together. |
| [InstrumentPlayerLoader.cpp](../source/Engine/InstrumentPlayerLoader.cpp) | 1,343 | Loading, live updates, parameters, and routing are together. |
| [KitLibModel.cpp](../source/Engine/KitLibModel.cpp) | 1,168 | Model parsing, calibration, sample scanning, and library scanning are together. |
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

### 1. Consolidate preset behavior in Browse, then remove the old Kit page

Evidence:

- [KitHomePanel.cpp](../source/UI/KitHomePanel.cpp), lines 186-209 and 437-498, scans, saves, overwrites, and deletes presets.
- [KitBrowserComponent.cpp](../source/UI/KitBrowserComponent.cpp), lines 118-148 and 424-487, implements the same storage flow.
- The behavior has already drifted. "Save As" in `KitHomePanel` can replace a same-name file. `KitBrowserComponent` rejects the same case.
- The Kit page was an experiment and is planned for removal. The Browse page is the intended replacement but still needs usage testing.

Suggestion:

- List the useful Kit page elements and rules.
- Migrate only those useful parts to Browse.
- Decide the same-name, overwrite, sorting, and selection rules in Browse.
- Usage-test the completed Browse flow.
- Remove the old Kit page and `KitHomePanel`.
- Rename Browse to Kit after the old page is removed.

Do not create a shared preset store only to support two screens when one screen will be deleted. After migration, keep the file operations in the remaining component if they are small. Extract an `InstrumentMapPresetStore` only if the remaining component becomes too large or another real caller needs the same storage rules.

### 2. Consolidate library item resolution

[DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp) repeats the library kit, instrument, articulation, and write-access lookup in eight library edit paths around lines 329-769.

Add one small resolver for this library item chain. Keep lock ownership and pointer lifetime explicit. Keep the write-access decision at the caller.

Do not return pointers that outlive the active library snapshot.

The manual-gain and playback-offset paths also repeat moved-file recovery and ambiguity checks around lines 626-637 and 690-709. Extract the common sample target resolution, but keep mic-specific validation in the playback-offset path.

### 3. Consolidate library item lookup in `PadContextPopup`

[PadContextPopup.cpp](../source/UI/PadContextPopup.cpp), lines 262-402, resolves the same library item and articulation set twice. Only the final mic filter differs.

Add a private `resolveLibraryArticulationsForPath()` operation. Preserve these current rules:

- A specific articulation selects its matching base articulation.
- A library item without an articulation uses all articulations.

### 4. Consolidate exact small copies

Good small candidates:

- ✅ [PluginEditor.cpp](../source/PluginEditor.cpp) now creates its processing suspension guard through one local operation. The guard remains before the callback lock at all four call sites.
- ✅ The cohesive [Parsing.h](../source/Config/Parsing.h) and [Parsing.cpp](../source/Config/Parsing.cpp) module now owns parse results, wrapped-text parsing, and strict finite-float parsing. The float parser preserves full-input, range, finite-value, and locale behavior.
- `parseSignedGainDb()` and `parseSignedOffsetMs()` in [KitLibModel.cpp](../source/Engine/KitLibModel.cpp), lines 67-117, can share one signed metadata number parser while retaining clear wrapper names.
- Search term syntax is repeated in [KitBrowserComponent.cpp](../source/UI/KitBrowserComponent.cpp), lines 150-162, and [SourcePickerPopup.cpp](../source/UI/SourcePickerPopup.cpp), lines 295-305. Add one UI search matcher.
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

There are now 9 files with `Helper` or `Helpers` in their name. The count is not large, but some files still mix unrelated work or have unclear names.

| Current file or group | Suggested ownership |
| --- | --- |
| [Helpers.h](../source/Config/Helpers.h) | ✅ Parse results and wrapped-text parsing moved to `Config/Parsing`. Continue splitting path resolution, MIDI values, and string IDs by concern. |
| [YamlHelper.h](../source/Config/YamlHelper.h) and [YamlHelper.cpp](../source/Config/YamlHelper.cpp) | This is one cohesive YAML module. It can stay in `Config`. Rename it to `YamlParser` only when related code is already being changed. |
| [DrumKitEngineMapHelpers.h](../source/Engine/DrumKitEngineMapHelpers.h) and its `.cpp` | ✅ Map data, parsing, serialization, and shared identity matching moved to `Engine/InstrumentMap/InstrumentMap.h` and `InstrumentMap.cpp`. The remaining operations no longer include the full engine. |
| [ArticulationNames.h](../source/Engine/ArticulationNames.h) | ✅ Renamed by responsibility and kept as one cohesive `Engine` header. Create an articulation feature folder only if this module grows to several files. |
| `DrumKitEnginePreviewHelpers.h` | ✅ Removed. Its one-use private logic is in `DrumKitEngine.cpp` until the planned engine preview file split. |
| [UIHelpers.h](../source/UI/UIHelpers.h) and its `.cpp` | Split by concern. Control styling belongs in `UI/Controls/`. Keep library item building with `LibraryItem`. MIDI display and assignment operations stay with the MIDI controls. |
| [KitHomePanelHelpers.h](../source/UI/KitHomePanelHelpers.h) | Do not reorganize this file first. Move the required choices and preset rules to Browse, then delete this helper with the old Kit page. |
| `DrumPadHelpers.h` | ✅ Removed. Its private operations are in `DrumPad.cpp`. |
| [ParameterPotHelpers.h](../source/UI/ParameterPotHelpers.h) | Its layout and value conversion rules have several callers. Move it to `UI/Controls/` and rename it to `ParameterPotSupport.h`, or make the operations named static members when they directly belong to `ParameterPot`. |

Suggested feature folders:

```text
source/
  Engine/
    InstrumentMap/
  UI/
    Controls/
```

Create a folder only when several files share one responsibility. Do not create a folder for every small helper.

Keep renamed library UI files in `UI/` during the first change. Review a `UI/Library/` folder only if the group later needs a clear boundary.

### Namespace rule

The number of namespaces is not a problem by itself. Namespaces such as `KitLib`, `UITheme`, `HiHatTransitionPolicy`, and the YAML module have clear ownership.

Folders make files easier to find. Namespaces prevent name conflicts and show code ownership. They solve different problems, so a folder move alone does not require a new namespace hierarchy.

Use these rules:

- Reusable functions should use a feature namespace, such as `InstrumentMap` or `ArticulationNames`.
- Private functions used by one `.cpp` file should use a named `<Feature>Detail` namespace at the top of that file and should have internal linkage. This follows the project rule that forbids nameless namespaces.
- Do not create a public `FooHelpers` namespace only because the file contains free functions.
- Do not add one new top-level namespace to the complete codebase as part of this cleanup. That would create a large diff without solving the current ownership problems.
- Folder and namespace names should normally describe the same feature.

### Safe migration order

1. ✅ Remove one-use helper headers by moving their code into the owning `.cpp`.
2. Remove `KitHomePanelHelpers.h` as part of the Kit page replacement.
3. ✅ Create the `InstrumentMap` feature pair when map types are extracted from `DrumKitEngine`.
4. Split `UIHelpers` when the first related UI component is changed.
5. Split `Config/Helpers.h` one concern at a time. ✅ Parsing support moved first to the cohesive `Config/Parsing` module. Do not create a new general helper file.

This order avoids a large move-only change and keeps each diff connected to a real cleanup.

## Priority 1 and 2: Split large files by responsibility

Start with physical moves of existing member functions. Do not change behavior during the first split.

| Current file | First physical split | Later logic split |
| --- | --- | --- |
| [DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp) | `DrumKitEngineLibraryKitOverrides.cpp`, `DrumKitEngineSettings.cpp`, `DrumKitEnginePreview.cpp`, and `DrumKitEngineMapApply.cpp` | Divide the roughly 706-line `useInstrumentMapInternal()` into preparation without the graph lock and a short locked commit using named plan records. |
| [InstrumentPlayerController.cpp](../source/Engine/InstrumentPlayerController.cpp) | `InstrumentPlayerHiHat.cpp`, `InstrumentPlayerAftertouch.cpp`, and a note playback implementation file | Divide the roughly 423-line hi-hat transition into bounded, allocation-free steps. Preserve voice order and accepted sound behavior. |
| [PadContextPopup.cpp](../source/UI/PadContextPopup.cpp) | `PadContextPopupMapLayers.cpp`, `PadContextPopupRouting.cpp`, and `PadContextPopupDialogs.cpp` | Add one owned linked-edit context for parsed map and projected targets. Never return pointers into temporary projection data. |
| [InstrumentPlayerLoader.cpp](../source/Engine/InstrumentPlayerLoader.cpp) | Library updates, parameter updates, routing, and sample loading implementation files | Divide `createVelocityLevels()` into sound creation, logical-hit grouping, early-load selection, and queue submission. |
| [KitLibModel.cpp](../source/Engine/KitLibModel.cpp) | Decay, mic, calibration, instrument, kit, and library manager implementation files | Split `KitLibModel.h` later, starting with independent enums and small records. Parent pointers make an immediate full header split risky. |
| [LibraryArticulationEditor.cpp](../source/UI/LibraryArticulationEditor.cpp) | Model, state and persistence, preview, and main component implementation files | Consider a separate selection state model only after the low-risk file split. |
| [PluginEditor.cpp](../source/PluginEditor.cpp) | Preset store and popup implementation files | Keep the editor as the coordinator. Pass directories into stores instead of making stores depend on the full engine. |
| [TestInstrumentPlayerVoiceGrouping.cpp](../tests/TestInstrumentPlayerVoiceGrouping.cpp) | Shared test support plus routing, hi-hat, rendering, and aftertouch test files | Do this after production boundaries are stable. More test files can make a clean test build slightly slower. |

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

1. List useful Kit page elements and rules.
2. Migrate them to Browse.
3. Decide and verify one preset save and overwrite policy.
4. Usage-test Browse.
5. Remove the old Kit page and its remaining helper code.
6. Rename Browse to Kit.

Verification for this phase:

- Check preset scan, load, save, overwrite, and delete behavior.
- Check filtering, selection, kit loading, and instrument-map loading.
- Confirm that no old Kit page route or `KitHomePanel` reference remains.

### Phase 4: Physical file splits

Move existing member function definitions into the responsibility-based `.cpp` files listed above. Keep public interfaces and behavior unchanged.

Verification for this phase:

- Review each move-only diff for accidental logic edits.
- Run focused tests, then the full test target.
- Do not add benchmarks for a pure file move.

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
- The useful Kit page behavior exists in the usage-tested Browse replacement.
- The old Kit page and its unused helper code are removed. Browse is renamed to Kit.
- Instrument-map preset storage has one explicit same-name policy in the remaining screen.
- Exact repeated parsing and guard logic has one owner.
- The large engine and UI files are separated by responsibility.
- Instrument-map preparation and locked commit are visibly separate.
- The verified converter and generated operation scripts are archived or removed. If they remain active, they have one runtime implementation and one canonical operation manifest.
- Focused tests, the full test target, relevant listening checks, and incremental build measurements pass.
