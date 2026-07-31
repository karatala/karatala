# Codebase Maintainability Review

Status: static review complete on 2026-07-30. All changes below are suggestions. No source change is implemented by this document.

## Scope and limits

This review covers first-party C++ source, tests, benchmarks, and Python tools. It does not review JUCE, other vendored code, or generated instrument data as normal hand-written source.

No build, test, compiler timing, or audio check was run. File sizes and include paths were checked statically.

The include result measures potential rebuild scope:

- A source file is counted when it can reach a header through project `#include "..."` paths.
- It does not measure compiler time.
- A high count is not automatically a defect. A small and stable shared type can have a high count safely.
- A large or frequently changed header with a high count is a better cleanup target.

This review complements [CODEBASE_IMPROVEMENT_PLAN.md](CODEBASE_IMPROVEMENT_PLAN.md) and its [Workstream 1 audit](CODEBASE_IMPROVEMENT_WORKSTREAM_1_AUDIT.md). Those documents already define the rules for helper extraction, behavior checks, and realtime safety.

## Main result

The main structural problems are:

1. `DrumKitEngine` and some UI files contain too many responsibilities.
2. Some rules are implemented more than once. Instrument-map preset storage already has different behavior in two screens.
3. Broad headers connect unrelated parts of the project. This is usually called transitive include coupling or high header fan-out.
4. Several large implementations are inside headers. A small header edit can therefore make many `.cpp` files parse the same implementation again.
5. The DrumGizmo converter keeps a program inside another program and generates very large Python files that mostly contain data.

Recommended order:

1. Fix small exact duplicates and one clear placeholder.
2. Reduce header coupling.
3. Split large implementation files without behavior changes.
4. Divide the largest functions only after the physical file splits are stable.
5. Simplify the converter and generated file format.

## Measured facts

### Largest reviewed files

| File | Lines | Main concern |
| --- | ---: | --- |
| [DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp) | 3,234 | Engine setup, settings, source edits, graph rebuild, preview, live updates, and map application are together. |
| [convert-drumgizmo2karatala.py](../scripts/convert-drumgizmo2karatala.py) | 2,119 | Current and legacy flows plus an embedded generated runtime are together. |
| [PadContextPopup.cpp](../source/UI/PadContextPopup.cpp) | 1,888 | UI construction, source lookup, routing, linked edits, dialogs, and layout are together. |
| [TestInstrumentSynthVoiceGrouping.cpp](../tests/TestInstrumentSynthVoiceGrouping.cpp) | 1,797 | Several different synth behaviors share one test file. |
| [SourceArticulationOverrideEditor.cpp](../source/UI/SourceArticulationOverrideEditor.cpp) | 1,394 | View, state, persistence, preview, and input are together. |
| [InstrumentSynthController.cpp](../source/Engine/InstrumentSynthController.cpp) | 1,375 | Large realtime controller and aftertouch workflows are together. |
| [InstrumentSynthLoader.cpp](../source/Engine/InstrumentSynthLoader.cpp) | 1,343 | Loading, live updates, parameters, and routing are together. |
| [KitLibModel.cpp](../source/Engine/KitLibModel.cpp) | 1,168 | Model parsing, calibration, sample scanning, and library scanning are together. |
| [InstrumentSynth.h](../source/Engine/InstrumentSynth.h) | 815 | Several public classes, runtime records, and inline logic are in one header. |
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

### 1. Remove logging from the general helper chain

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

### 2. Separate instrument type from the full library model

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

### 3. Move YAML implementation out of `LayoutManager.h`

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

### 4. Remove full engine includes from map helper headers

Evidence:

- [DrumKitEngineMapHelpers.h](../source/Engine/DrumKitEngineMapHelpers.h), [DrumKitEngineMappingModel.h](../source/Engine/DrumKitEngineMappingModel.h), and [DrumKitEngineTextView.h](../source/Engine/DrumKitEngineTextView.h) include all of `DrumKitEngine.h`.
- They need nested map types such as `DrumKitEngine::InstrumentMapEntry`.
- [DrumKitEngine.h](../source/Engine/DrumKitEngine.h) includes the full synth, layout, catalog, routing, loader, settings, and velocity response headers.

Suggestion:

1. Move `InstrumentMapEntry` and `InstrumentMap` to a narrow `InstrumentMapTypes.h`.
2. Move source-view records to a narrow source override view header.
3. Move `LayerParameterUpdate` out of the nested synth type if the engine only needs this small update record.
4. Move the `DrumKitEngine` constructor, destructor, and other non-trivial inline methods to `.cpp` files.

Expected result: map parsing, formatting, and UI code can use map data without including the full audio engine.

### 5. Move other complete implementations out of headers

Review these after the first four changes:

- [InstrumentCatalog.h](../source/Engine/InstrumentCatalog.h) is mostly inline parsing and catalog logic.
- [MicRoutingPlan.h](../source/Engine/MicRoutingPlan.h) contains its routing implementation inline.
- [InstrumentSynth.h](../source/Engine/InstrumentSynth.h) contains `SampledSound`, `InstrumentSynthVoice`, `InstrumentSynth`, many runtime records, and non-trivial inline methods.

Move normal implementation to `.cpp` files first. Split public types only when a real caller needs the smaller boundary. Do not add a private implementation pattern to every class without a measured need.

## Priority 1: Consolidate repeated behavior

### 1. Add one instrument-map preset store

Evidence:

- [KitHomePanel.cpp](../source/UI/KitHomePanel.cpp), lines 186-209 and 437-498, scans, saves, overwrites, and deletes presets.
- [KitBrowserComponent.cpp](../source/UI/KitBrowserComponent.cpp), lines 118-148 and 424-487, implements the same storage flow.
- The behavior has already drifted. "Save As" in `KitHomePanel` can replace a same-name file. `KitBrowserComponent` rejects the same case.

Suggestion:

- Add one `InstrumentMapPresetStore` with folder lookup, scan, normalized save, overwrite, and delete operations.
- Decide the same-name rule before extraction.
- Keep dialogs, sorting, selection, filtering, and status text in each UI component.

This is the highest-value duplicate because it already causes different user behavior.

### 2. Consolidate source target resolution

[DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp) repeats the library, kit, instrument, articulation, and write-access lookup in eight source-edit paths around lines 329-769.

Add one small resolver for this source chain. Keep lock ownership and pointer lifetime explicit. Keep the write-access decision explicit at the caller or use a named access policy. Do not return pointers that outlive the active library snapshot.

The manual-gain and playback-offset paths also repeat moved-file recovery and ambiguity checks around lines 626-637 and 690-709. Extract the common sample target resolution, but keep mic-specific validation in the playback-offset path.

### 3. Consolidate source-path articulation lookup in `PadContextPopup`

[PadContextPopup.cpp](../source/UI/PadContextPopup.cpp), lines 262-402, resolves the same source item, kit, instrument, and articulation set twice. Only the final mic filter differs.

Add a private `resolveSourceArticulationsForPath()` operation. Preserve these current rules:

- A specific articulation selects its matching base articulation.
- A source path without an articulation uses all articulations.

### 4. Consolidate exact small copies

Good small candidates:

- The local processing suspension guard appears four times in [PluginEditor.cpp](../source/PluginEditor.cpp), around lines 41, 213, 247, and 374. Define it once and preserve its order relative to the callback lock.
- Strict finite-float parsing is repeated in [VelocityResponse.cpp](../source/Engine/VelocityResponse.cpp), [SourceArticulationOverrideEditor.cpp](../source/UI/SourceArticulationOverrideEditor.cpp), and [VelocityResponseEditor.cpp](../source/UI/VelocityResponseEditor.cpp). Use one strict parser that preserves full-input, range, finite-value, and locale behavior.
- `parseSignedGainDb()` and `parseSignedOffsetMs()` in [KitLibModel.cpp](../source/Engine/KitLibModel.cpp), lines 67-117, can share one signed metadata number parser while retaining clear wrapper names.
- Search term syntax is repeated in [KitBrowserComponent.cpp](../source/UI/KitBrowserComponent.cpp), lines 150-162, and [SourcePickerPopup.cpp](../source/UI/SourcePickerPopup.cpp), lines 295-305. Add one UI search matcher.
- Sample load type groups are repeated in two large switches in [SampleLoader.cpp](../source/Engine/SampleLoader.cpp), lines 53-179. Use one local load-class classifier, but keep the special kick, snare, hi-hat, ride, crash, and tom rules explicit.
- Data path resolution is repeated in [DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp), lines 161-172, and [DrumKitEngineController.cpp](../source/Engine/DrumKitEngineController.cpp), lines 23-30. Use one constructor-independent data-path resolver.
- The same 16-byte BLAKE2b file identity is implemented in [generate_sample_metadata.py](../scripts/generate_sample_metadata.py), lines 232-238, and [apply_kit_overrides.py](../scripts/apply_kit_overrides.py), lines 47-55. Put the standard-library-only algorithm in one script module.

The exact `identityKey()` and `candidateMatches()` duplicate is already accepted in the [Workstream 1 audit](CODEBASE_IMPROVEMENT_WORKSTREAM_1_AUDIT.md). Implement that small accepted change before creating broader identity helpers.

### Do not merge these only because they look similar

- Instrument-map fingerprints and serialized text have different field and formatting contracts.
- Active voice loops have different filters, order, and realtime mutation rules.
- MIDI clamping and MIDI validation have different behavior.
- The independent audio analysis comparison tool should not share all calculation logic with the production metadata generator. Independent code can detect a production calculation defect.

Avoid a generic helper with many flags or callbacks. Extract one shared primitive when the complete workflows are different.

## Priority 1 and 2: Split large files by responsibility

Start with physical moves of existing member functions. Do not change behavior during the first split.

| Current file | First physical split | Later logic split |
| --- | --- | --- |
| [DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp) | `DrumKitEngineSourceOverrides.cpp`, `DrumKitEngineSettings.cpp`, `DrumKitEnginePreview.cpp`, and `DrumKitEngineMapApply.cpp` | Divide the roughly 706-line `useInstrumentMapInternal()` into preparation without the graph lock and a short locked commit using named plan records. |
| [InstrumentSynthController.cpp](../source/Engine/InstrumentSynthController.cpp) | `InstrumentSynthHiHat.cpp`, `InstrumentSynthAftertouch.cpp`, and a note playback implementation file | Divide the roughly 423-line hi-hat transition into bounded, allocation-free steps. Preserve voice order and accepted sound behavior. |
| [PadContextPopup.cpp](../source/UI/PadContextPopup.cpp) | `PadContextPopupLayers.cpp`, `PadContextPopupRouting.cpp`, and `PadContextPopupDialogs.cpp` | Add one owned linked-edit context for parsed map and projected targets. Never return pointers into temporary projection data. |
| [InstrumentSynthLoader.cpp](../source/Engine/InstrumentSynthLoader.cpp) | Source updates, parameter updates, routing, and sample loading implementation files | Divide `createVelocityLayers()` into sound creation, logical-hit grouping, early-load selection, and queue submission. |
| [KitLibModel.cpp](../source/Engine/KitLibModel.cpp) | Decay, mic, calibration, instrument, kit, and library manager implementation files | Split `KitLibModel.h` later, starting with independent enums and small records. Parent pointers make an immediate full header split risky. |
| [SourceArticulationOverrideEditor.cpp](../source/UI/SourceArticulationOverrideEditor.cpp) | Model, state and persistence, preview, and main component implementation files | Consider a separate selection state model only after the low-risk file split. |
| [PluginEditor.cpp](../source/PluginEditor.cpp) | Preset store and popup implementation files | Keep the editor as the coordinator. Pass directories into stores instead of making stores depend on the full engine. |
| [TestInstrumentSynthVoiceGrouping.cpp](../tests/TestInstrumentSynthVoiceGrouping.cpp) | Shared test support plus routing, hi-hat, rendering, and aftertouch test files | Do this after production boundaries are stable. More test files can make a clean test build slightly slower. |

## Priority 2: Simplify the converter and generated files

Evidence:

- [convert-drumgizmo2karatala.py](../scripts/convert-drumgizmo2karatala.py), lines 1551-1817, contains a generated Python runtime as a large string.
- That runtime closely repeats [drumgizmo_runtime.py](../instruments_py/_common/drumgizmo_runtime.py).
- The old `process_kit()` and `legacy_copy_main()` flow at lines 825-1049 is not called by the current main path inside this repository.
- The seven generated converter files contain 723,764 lines, mostly operation construction.
- No focused test was found for output plan generation, generated runtime behavior, or operation manifest round trips.

Suggestion:

1. Make the normal runtime module the single source of truth. Do not maintain a second program inside a string.
2. Preserve generated output during this first extraction.
3. Add focused tests for output planning, audit schema, and runtime execution.
4. Remove the legacy path after confirming that no external workflow calls it.
5. Replace generated Python operation construction with one data manifest, such as JSON or CSV, read by the shared runtime. Choose one canonical manifest instead of storing the same operation data in both runtime code and audit files.
6. Regenerate the seven converter artifacts. Do not edit them by hand.

Because the application is not released, this can use a clean new generated format without compatibility code.

## Other problems

### Host-facing placeholder parameter

[PluginProcessor.cpp](../source/PluginProcessor.cpp), line 225, defines `kitLayoutChoice` with choices `"default"` and `"FUK U JUCE"`. This is the only repository reference to that parameter ID.

Remove the parameter if it is unused. Otherwise, replace it with the real supported choices and add the code that applies the selection. Do this before a package is shared because a host can expose plugin parameter names and values.

### Stale and deprecated code needs targeted cleanup

The source tree has many `TODO`, `FIXME`, and similar markers. Do not remove them as one broad cleanup. Check each reference after the related file split.

One example shows why this must be careful: a comment says `findArticulationIndex()` is unused, but loader code calls it. In contrast, some old converter and analysis paths appear to have no current caller.

The scripts contract marks several analysis scripts as deprecated experiments. Remove or archive clear backup files such as `analyze_drum_samples~.py` and `_unused.py` only after checking the documented comparison workflow. Do not build production helpers around deprecated copies.

## Brief implementation plan

### Phase 1: Small and exact changes

1. Remove or replace the placeholder plugin parameter.
2. Implement the accepted identity primitive from the existing audit.
3. Define the processing suspension guard once.
4. Decide the preset same-name rule, then add the instrument-map preset store.
5. Consolidate strict float parsing and Python file hashing.

Verification for this phase:

- Add or identify focused tests before changing behavior.
- Compare preset save, overwrite, scan, and delete behavior in both screens.
- Run source identity, map parsing, and serialization tests.
- Run script hash and direct-execution checks.

### Phase 2: Header boundaries

1. Remove the logger include from `Helpers.h` and add direct includes at real users.
2. Move logger implementation to `.cpp`.
3. Extract the narrow instrument type.
4. Move layout YAML implementation out of `LayoutManager.h`.
5. Extract instrument-map types from `DrumKitEngine`.
6. Move routing, catalog, and synth implementation out of headers where practical.

Verification for this phase:

- Run the full test target after each shared header change.
- Compare clean build time and incremental rebuild time before and after.
- Change one representative leaf header and record which `.cpp` files rebuild.
- Confirm that every header includes the types that it uses directly.

### Phase 3: Physical file splits

Move existing member function definitions into the responsibility-based `.cpp` files listed above. Keep public interfaces and behavior unchanged.

Verification for this phase:

- Review each move-only diff for accidental logic edits.
- Run focused tests, then the full test target.
- Do not add benchmarks for a pure file move.

### Phase 4: Divide large workflows

1. Divide instrument-map preparation from its locked commit.
2. Divide hi-hat and aftertouch workflows into bounded realtime-safe steps.
3. Add owned linked-edit state to `PadContextPopup`.
4. Divide velocity-layer construction into named phases.

Verification for this phase:

- Run focused engine, routing, source override, and UI behavior tests.
- Use listening checks for hi-hat, aftertouch, choke, phase, gain, and routing changes.
- Confirm that realtime helpers allocate no memory, perform no blocking I/O, and preserve iteration order.

### Phase 5: Converter format

1. Make the shared runtime canonical.
2. Add generator and manifest tests.
3. Remove the confirmed unused legacy path.
4. Generate data manifests instead of very large Python operation lists.

Verification for this phase:

- Compare operation counts and audit rows before and after.
- Run one small and one large kit conversion.
- Compare generated paths, hashes, channel rules, and audit results.

## Completion criteria

The cleanup is complete when:

- `Helpers.h` no longer brings logging into unrelated files.
- `UITheme.h` no longer includes the complete library model.
- Map helper headers no longer include the complete engine only for nested data types.
- Instrument-map preset storage has one explicit same-name policy.
- Exact repeated parsing and guard logic has one owner.
- The large engine and UI files are separated by responsibility.
- Instrument-map preparation and locked commit are visibly separate.
- The converter has one runtime implementation and one canonical operation manifest.
- Focused tests, the full test target, relevant listening checks, and incremental build measurements pass.
