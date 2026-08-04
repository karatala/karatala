# TODO18: Library Articulation And Sample Override Plan

Date: 2026-07-27

Status: In progress. Runtime editing and persistence are implemented for velocity ranges, placement balance, manual gain, and playback offsets. The library-articulation editor now uses a round robin by velocity matrix, mic filters, batch editing, full-hit preview, and solo-mic preview. The earlier library kit override version passed all 177 C++ tests. The current UX and batch-save changes have not been built or tested yet by request. Visual QA, Python verification, and the offline materialization workflow remain.

## Current Implementation Progress

- ✅ Added typed version-1 `kit-overrides.yml` parsing and deterministic atomic saving with a backup.
- ✅ Preserved filename defaults separately and applied saved balance, `manualGainDb`, and playback offsets during normal kit loading.
- ✅ Added editable exact velocity boundaries with live runtime preview, validation, reset actions, and library-wide persistence.
- ✅ Added access to `LibraryArticulationEditor` from exact library-articulation rows in `PadContextPopup`.
- ✅ Added focused parser, writer, model, velocity-table, linked-preview, and saved-reload tests. Before the current editor redesign, the complete C++ suite passed: 177 of 177 tests.
- ✅ Added unique content-hash recovery, external-change conflict protection, full library-value snapshots, balance/gain/offset UI, and live runtime update paths.
- ✅ Added smoothed manual-gain correction, affected-articulation recalculation, targeted recipe loading, safe buffer publication, and pending or failed status display.
- ✅ Added deterministic complete logical-hit preview, including linked mics, without changing performance round robin history.
- ✅ Added explicit `Retry save` and `Reload saved` recovery actions for persistence failures.
- ✅ Replaced the long sample dropdown with a multi-select matrix that has round robin ID rows and velocity level columns, mic filters, a selected-file list, and atomic batch gain and offset editing.
- ✅ Moved sample measurements to selected-file tooltips and added full-hit, solo-mic, and velocity-block hover or click preview rules.
- ✅ Added the first review-only JSON dry-run planner. Python verification is intentionally deferred until the C++ and UI work is stable.
- Remaining: build and C++ verification of the editor redesign, visual and audio QA, offline materialization, and Python verification.

## Goal

Add a library-articulation editor similar to `VelocityResponseEditor`.

The editor must let the user:

- change the MIDI velocity range used by every velocity level of one library articulation;
- preview the result immediately without renaming sample files;
- save sparse manual settings in a portable metadata file inside the drum-kit folder;
- edit placement balance, `manualGainDb`, and `playbackOffsetMs` at their correct scopes;
- keep filename and measurement details available without crowding the main editing controls;
- later use the same saved data to create a reviewed file-rename or audio-processing plan.

The first important result is editable velocity boundaries. Other library kit overrides must use the same typed model and persistence path instead of creating separate formats.

## Scope And Terminology

- `library kit`: one `KitLib::DrumKitSpec` in the sample library.
- `library instrument`: one `KitLib::InstrumentSpec` inside that kit.
- `library articulation`: one `KitLib::ArticulationSpec`, such as `center`, `rim`, `bow[open]`, or `edge[half]`.
- `velocity level`: one recorded velocity group identified by the numeric velocity in sample filenames, such as `043`.
- `round robin ID`: one letter such as `a`, `b`, or `c` that identifies an alternate logical hit at the same velocity level.
- `logical hit`: one recorded hit at one velocity level. It includes one sample for each linked mic.
- `velocity upper boundary`: the highest incoming MIDI velocity that selects one velocity level in exact mode.
- `filename default`: a value parsed from an instrument folder, articulation folder, or sample filename.
- `library kit override`: a sparse user-authored value that replaces one filename default without changing sample files.
- `effective value`: the resolved value used by runtime after filename defaults and library kit overrides are combined.
- `map layer parameter`: a setting in an instrument map layer, such as `Mix Gain`, pan, bleed, or `velocityResponse`.
- `materialize`: explicitly convert saved library kit overrides into reviewed folder renames, sample renames, filename tokens, or processed audio.

This plan edits library behavior. It does not replace map layer parameters.

## Decisions And Assumptions

- Add one sparse `kit-overrides.yml` file in each library kit root.
- Keep generated measurements in `metadata.csv`. Never store user edits in `metadata.csv`, because `generate_sample_metadata.py` replaces that file.
- A missing `kit-overrides.yml` means that all existing filename and folder behavior remains active.
- A present override replaces only its matching library value. Missing fields continue to use their filename or folder defaults.
- Velocity boundaries apply to a complete velocity level. All logical hits and linked mics in that level share the same MIDI range.
- The original filename velocity remains the stable velocity-level identity until an explicit materialization action renames files.
- Exact selection uses the edited boundaries. `velocityResponse` continues to control output gain only.
- Velocity boundary changes, balance changes, and playback-offset changes do not reload sample audio.
- A sample manual-gain edit is heard immediately through a temporary smoothed runtime correction. Only the affected sample preprocessing recipe may reload in the background. The whole kit must not reload.
- Library kit overrides affect every map layer that uses the matching library articulation or sample. The editor must state this clearly.
- The editing plugin instance applies a saved change immediately. Other already-open plugin instances use it after `Reload All Kits` or reopening. File watching is not part of the first version.
- The first version supports velocity upper boundaries, instrument or articulation balance, whole-file `manualGainDb`, whole-file `playbackOffsetMs`, and per-mic `playbackOffsetMs`.
- Potential fields such as sample disable, round robin reassignment, sample `flipLR`, sample phase correction, and destructive start or end trimming stay deferred until their exact runtime and materialization behavior is reviewed.
- Materialization is always a separate explicit workflow. Closing the editor or clicking `Done` never renames folders, renames samples, or modifies audio.

## Current Facts And Problems

- [`KitLibModel.cpp`](../source/Engine/KitLibModel.cpp) parses the velocity level and round robin ID from the same filename segment, for example `043a`.
- [`InstrumentPlayerLoader.cpp`](../source/Engine/InstrumentPlayerLoader.cpp) now builds `velocityToLevel[128]` from resolved `selectionUpperVelocity` values. With no override, earlier velocity levels still use their filename velocity and the final velocity level ends at `127`.
- All logical hits inside one velocity level already share that level's current range.
- [`VelocityResponseEditor`](../source/UI/VelocityResponseEditor.h) edits MIDI-velocity-to-output-gain points. It does not edit velocity-level selection ranges.
- Per-map-layer `velocityResponse` is saved in the instrument map and intentionally leaves exact velocity-level selection unchanged.
- [`SampleMetadata`](../source/Engine/SampleMetadata.h) loads generated per-file and per-mic measurements from `metadata.csv`.
- `generate_sample_metadata.py` backs up and replaces `metadata.csv`, so manual fields added there could be lost during regeneration.
- Instrument and articulation folder names provide placement balance through `L1..L127`, `C0`, and `R1..R127`.
- Sample filename `v` provides current whole-file `manualGainDb`.
- Sample filename `on<ms>` advances playback and `op<ms>` delays playback. Optional mic codes limit an offset to selected mics.
- `manualGainDb` participates in sample preprocessing and calibration.
- `playbackOffsetMs` is applied by runtime voices and preview without changing the shared sample-loader cache key.
- `InstrumentMapLayer` owns map layer controls. Library correction data does not belong in the instrument map because the same library articulation or sample may be used by many maps.
- The library-articulation editor now applies balance and playback offsets to following notes without sample reload. Manual gain uses a smoothed temporary correction while changed preprocessing recipes load in the background.
- There is still no safe workflow that turns reviewed overrides into file corrections.

The main problem is that file and folder names currently serve two different jobs:

1. They identify sample files and logical hits.
2. They also define editable playback defaults.

The new file keeps library identity stable while allowing the second job to be overridden.

## Data Ownership And Precedence

Use this ownership:

| Location | Ownership |
| --- | --- |
| Instrument, articulation, and sample names | Library identity and portable defaults |
| `metadata.csv` | Generated measurements and content identity |
| `kit-overrides.yml` | Sparse user-authored library corrections |
| Instrument map | Map layer assignments and creative controls |
| Plugin instance state | Global response, current map, and map controls |
| Runtime snapshot | Fully resolved immutable library values and lookup tables |

Resolve each library value in this order:

```text
effective library value
    = matching kit-overrides.yml value
    else parsed filename or folder value
    else the existing engine default
```

Do not copy library kit overrides into every map layer. Do not save mapped `Mix Gain`, pan, bleed, attack, decay, tuning, or `velocityResponse` in `kit-overrides.yml`.

### Similar Names With Different Meanings

Keep these values separate:

| Value | Scope | Meaning |
| --- | --- | --- |
| Filename velocity | Velocity-level identity | Numeric part of sample filename |
| Velocity upper boundary | Library articulation override | Highest MIDI velocity that selects the velocity level |
| `velocityResponse` | Plugin or map layer | MIDI velocity to output gain |
| `manualGainDb` | Sample-file override | Correct a sample file |
| `Mix Gain` or map `vol` | Map layer | Creative balance in the current map |
| Placement balance | Library instrument or articulation | Recorded left, center, or right placement |
| Map `pan` | Map layer | Creative placement after placement balance |

The UI must use these full labels. A generic `Gain` or `Velocity` label is not enough in this editor.

## Proposed `kit-overrides.yml` Format

Use YAML because the data is sparse and hierarchical. CSV is useful for generated per-mic measurements but is not a good format for library-instrument, library-articulation, velocity-level, sample-file, and mic overrides in one editable document.

Example:

```yaml
version: 1

instruments:
  "snare-X1-C0":
    balance: C0
    articulations:
      "center":
        balance: L12
        velocityUpperBounds:
          "043": 50
          "085": 91

samples:
  "snare-X1-C0/center/snare-X1-center-043a-dtm_ons.flac":
    contentHash: "a9d72b25d56f6f30dc9b7af6f6558191"
    manualGainDb: -1.5
    playbackOffsetMs: -0.35
    mics:
      "dtm":
        playbackOffsetMs: -0.60
      "ons":
        playbackOffsetMs: 0.20
```

Format rules:

- `version` is required and starts at `1`.
- Instrument and articulation keys use exact raw folder identities, not shortest display IDs.
- Sample keys use normalized kit-relative paths with `/`.
- `contentHash` is stored when `metadata.csv` provides it.
- File path is the primary lookup while the file keeps its current name.
- Content hash plus mic identity may recover a moved or renamed file only when the match is unique.
- An ambiguous hash match is ignored with a warning. Never guess.
- Missing keys mean `Use filename or folder value`.
- Reset removes a key. Do not write `null`.
- Numeric zero is a valid explicit override and must not be treated as missing.
- Unknown future keys produce a warning but do not reject known version-1 data.
- Invalid known values are ignored only at their own scope and reported with the full library edit address.
- Saving writes stable key order and normalized numeric values so diffs remain readable.

### Velocity Boundary Representation

Store only upper boundaries keyed by original filename velocity.

For velocity levels `043`, `085`, and `127`, the current default ranges are:

```text
043 -> MIDI 1..43
085 -> MIDI 44..85
127 -> MIDI 86..127
```

The example override changes them to:

```text
043 -> MIDI 1..50
085 -> MIDI 51..91
127 -> MIDI 92..127
```

Rules:

- The first range always starts at MIDI velocity `1`.
- The last range always ends at MIDI velocity `127`.
- The last velocity level's upper boundary is fixed at `127` and does not need to be stored.
- Every earlier velocity level has one editable upper boundary.
- Lower boundaries are derived from the previous upper boundary plus one.
- Resolved upper boundaries must be strictly increasing.
- Every velocity level must receive at least one MIDI velocity.
- An omitted earlier boundary uses its current filename default.
- The complete resolved list is validated after defaults and overrides are combined.
- MIDI velocity `0` remains note-off and is not an editable range.
- A one-level articulation has no editable divider and always covers `1..127`.
- Round robin IDs are displayed for information but do not receive separate ranges.

If the articulation's velocity levels change and a saved filename-velocity key no longer exists, ignore that key with a warning and keep it in the file until the user removes or reviews it. Do not silently attach it to the nearest velocity level.

### Balance Representation

- Instrument balance accepts only `L1..L127`, `C0`, and `R1..R127`.
- Articulation balance uses the same format.
- An articulation override wins over an instrument override.
- An instrument override wins over the instrument folder value.
- Resetting an articulation override returns to the effective instrument value.
- Resetting an instrument override returns to the folder value.
- Balance remains library placement metadata. It is not map pan.

### Manual Gain Representation

- `manualGainDb` is a whole-sample-file correction.
- A saved value replaces the filename `v` value. It is not added to it.
- The UI shows `Filename`, `Override`, and `Effective` values.
- Runtime and calibration calculations use the effective value exactly once.
- The effective value participates in true-peak safety checks.
- Per-mic gain is not part of version 1 because one multichannel sample file may share one preprocessed buffer. Add it only after a clear channel-scoped recipe contract exists.

### Playback Offset Representation

- Negative `playbackOffsetMs` advances by skipping source time.
- Positive `playbackOffsetMs` delays playback.
- A mic override wins over the whole-file override.
- A whole-file override wins over the filename value.
- Resetting a mic override returns to the effective whole-file value.
- Offset edits affect the next note or preview. They do not reposition a voice that is already sounding.
- Offset remains a runtime start rule and does not change cache identity unless a later materialization step bakes it into audio.

## Runtime Model

Add typed library kit override structures:

```text
LibraryKitOverrides
  version
  instrument overrides
  sample overrides

InstrumentOverride
  optional balance
  articulation overrides

ArticulationOverride
  optional balance
  velocity upper boundaries by original filename velocity

SampleOverride
  relative path
  optional content hash
  optional manualGainDb
  optional playbackOffsetMs
  mic overrides
```

Keep parsing, validation, lookup, and writing outside the audio thread.

`DrumKitSpec` should own the parsed library kit overrides for its kit. Runtime objects should receive only resolved typed values. The audio thread must not read YAML, compare paths, look up hashes, or rebuild ranges.

### Stable Edit Address

UI and engine update calls use a value address, not raw `KitLib` or instrument player pointers:

```text
LibraryEditAddress
  kit identity
  exact instrument folder identity
  exact articulation folder identity
  optional original filename velocity
  optional round robin ID
  optional kit-relative sample path
  optional mic code
```

Resolve the address again before every committed update. If the library was reloaded and the address is stale, reject the change and refresh the editor.

### Resolved Velocity Table

For every loaded library articulation:

1. Sort velocity levels by their original filename velocity.
2. Start with current exact-selection defaults.
3. Apply matching saved upper-bound overrides.
4. Validate the complete ordered boundary list.
5. Build the fixed `velocityToLevel[128]` table outside playback.
6. Publish the rebuilt table through the existing safe graph-update path.

Changing one boundary rebuilds only affected library articulations and instrument players. It does not reload sample buffers and does not change round robin counters.

Preview and normal notes must use the same resolved boundary table.

### Resolved Manual Gain

At normal kit load:

1. Resolve filename `manualGainDb`.
2. Replace it with the saved override when present.
3. Use the effective value in logical-hit measurements, calibration, true-peak checks, the sample preprocessing recipe, and cache identity.
4. Apply it exactly once.

For a live editor change:

1. Calculate the difference between the currently published effective sample gain and the new effective sample gain.
2. Apply a short smoothed temporary runtime correction to matching current and new sounds.
3. Rebuild affected measurement, calibration, and loudness data outside playback.
4. Request only affected sample preprocessing recipes in the background.
5. Publish replacement buffers and resolved calibration data safely.
6. Reduce the temporary correction as the new canonical data becomes active so gain is never applied twice.
7. Report loading or failure state in the editor.

If targeted reload fails, keep the previous canonical buffer plus the temporary correction and show that the saved override is active but not yet baked into the current RAM buffer. `Reload All Kits` retries normal canonical loading.

Do not flush the complete cache or reload unrelated instruments.

### Resolved Balance And Playback Offset

- Rebuild library placement values outside playback after a balance edit.
- New notes and previews use the new balance immediately.
- Existing voices may keep their placement for their remaining lifetime in version 1. Do not jump a sounding stereo image.
- Store playback offset in the matching runtime sound through a safe update.
- New notes and previews use the new offset immediately.
- Existing voices keep their current read position.
- Keep offset out of the shared sample buffer and cache key until a separate reviewed bake path exists.

### Persistence And Multiple Instances

- Write `kit-overrides.yml` on the message thread through a temporary sibling file and atomic replacement.
- Keep one `kit-overrides.yml.bak` copy of the last valid file before replacement.
- Debounce drag persistence. Apply drag movement live, but save only on mouse-up, numeric commit, reset, or explicit `Done`.
- Parse the completed temporary file before replacing the active file.
- Keep the previous file when serialization, validation, temporary write, or replacement fails.
- Remember the file fingerprint loaded by the editor.
- If another process changes the file before save, reject the write, show a conflict message, and offer reload. Do not silently overwrite it.
- A read-only kit can be auditioned but cannot be saved. Show `Kit folder is read-only`.
- Do not silently fall back to plugin state or another user folder because the library correction would no longer travel with the kit.

## Library Articulation Editor

The reusable `LibraryArticulationEditor` is available from exact library-articulation rows in the pad popup. Its current UX contract and remaining visual checks are in [`TODO18_LIBRARY_OVERRIDE_EDITOR_UX_REDESIGN_PLAN.md`](TODO18_LIBRARY_OVERRIDE_EDITOR_UX_REDESIGN_PLAN.md).

The editor uses normal text, one balance row, range blocks with exact effective ranges, a multi-select matrix with round robin ID rows and velocity level columns, mic filters, a selected-file list, and three batch fields. The selected-file list shows filenames only. Generated onset, intensity, peak, and recipe status remain available in row tooltips.

Later, the planned kit browser may open this same editor directly for any library articulation. Do not create a second editor for that workflow.

### Velocity Range Interaction

- Draw one horizontal block for each velocity level.
- Use block width to show its effective MIDI range.
- Label each block with its filename velocity and current inclusive MIDI range.
- Drag only dividers between adjacent blocks.
- Clamp a divider so both adjacent velocity levels keep at least one MIDI value.
- Let left and right arrow keys move the selected divider by one.
- Let Shift plus arrow move it by five.
- Keep filename range details in the block tooltip instead of a separate numeric editor and readout row.
- Click a block to preview the MIDI velocity under the pointer.
- Apply a drag live to the current engine instance.
- Commit persistence on mouse-up and preview once after release.
- `Even` distributes MIDI `1-127` across all velocity levels, saves all resulting boundaries in one operation, and previews once.
- `Reset` removes all boundary overrides for the articulation.
- If the library articulation has one velocity level, show `Single level: MIDI 1..127` and no divider.

Do not reuse the vertical dB handles from `VelocityResponseEditor`. Reuse its modal shell, preview behavior, commit callback pattern, theme, keyboard support, and status handling where practical.

### Library Value Selection And Batch Editing

- Matrix columns are velocity levels and rows are round robin IDs.
- Matrix cells select logical hits. Mic buttons below the matrix filter the selected files.
- The matrix opens without a selected or active logical hit.
- Clicking `*` inverts all existing cells, clicking a round robin ID inverts its row, and clicking a velocity heading inverts its column. These broad selection changes are silent.
- The header and its round robin ID column use centred content.
- The last previewed matrix cell is the active hit and has a separate outline.
- The right list contains the existing combination of selected hits and selected mics.
- Hovering a matrix cell previews its complete logical hit. Hovering a mic button previews only that mic for the active hit.
- File gain, file start offset, and mic offset apply to all selected files through one complete save.
- A field shows one shared override, `Mixed`, or its inherited library value.
- Every value field supports coarse wheel and fine Shift-wheel changes without removing exact typed editing.
- A successful value-field change previews the complete logical hit from the final selected-file row, with selected-hit and active-hit fallbacks when that row is unavailable.
- Generated onset, intensity, peak, content-hash recovery, ambiguity, and recipe status stay in selected-file tooltips.
- Preview never changes the normal performance round robin counter.

### UI Safety

- Show a persistent simple scope note that the library change affects every pad that uses this library instrument.
- Disable editing when the library edit address is ambiguous.
- Disable save, but not preview, for a read-only kit.
- Keep unsaved live changes visible if persistence fails and provide `Retry save` or `Reload saved`.
- Do not confirm normal divider drags or single-value edits.
- Use tooltips that explain negative and positive playback offset.
- Preserve keyboard focus, resizing, and outside-click behavior used by the existing modal editor.

## Offline Correction And Materialization

Add a separate developer workflow after runtime editing is stable. It consumes `kit-overrides.yml`; it does not invent another correction format.

Create `scripts/apply_kit_overrides.py` with review-only behavior by default.

### Correction Plan

The default command produces a deterministic plan and changes nothing.

The plan contains:

- current path and content hash;
- proposed destination path;
- old and new velocity token;
- preserved round robin, mic-chain, and unrelated filename tokens;
- old and new balance folder token;
- old and new manual-gain token;
- old and new playback-offset token;
- optional audio-processing operation;
- collision, clipping, stale-hash, missing-file, and ambiguous-identity warnings.

Materialization mapping:

| Override | Default non-audio materialization |
| --- | --- |
| Velocity upper boundary | Rename every sample in that original velocity level to the new numeric velocity while preserving round robin and mic data |
| Instrument balance | Rename the instrument folder balance token |
| Articulation balance | Rename the articulation folder balance token |
| `manualGainDb` | Replace or add the filename `v` token |
| Whole-file `playbackOffsetMs` | Replace or add the filename `on` or `op` token |
| Per-mic `playbackOffsetMs` | Write the filename offset token with exact mic codes |

Audio processing is a separate explicit mode. Updating filename tokens is the safer default.

### Safe Apply

An explicit apply must:

1. Re-read and validate the kit and override file.
2. Verify paths, hashes, expected tokens, and destination collisions.
3. Create a timestamped backup manifest.
4. Use temporary names to avoid rename cycles.
5. Write processed audio to temporary files when audio materialization was explicitly requested.
6. Check output channel count, sample rate, frame count, peak, and readable audio format.
7. Publish the complete reviewed operation set only after all temporary outputs are ready.
8. Restore original paths when any publish step fails.
9. Regenerate `metadata.csv` only after sample changes succeed.
10. Update content hashes and paths in remaining overrides.
11. Remove only fields that were successfully materialized.
12. Keep a clear final report of applied, skipped, and failed operations.

Never run this workflow automatically from the plugin editor.

Audio materialization must require a separate option and a clipping policy. It may:

- apply manual gain to sample data;
- advance audio by removing leading frames;
- delay audio by inserting silence;
- preserve channel count and sample rate;
- update hashes and regenerate measurements.

Do not overwrite original audio without a recoverable backup. Do not bake balance or velocity range into waveform data.

## Implementation Plan

### ✅ 1. Add Typed Override Model And YAML Parser

- ✅ Add `LibraryKitOverrides.h/.cpp` under `source/Engine`.
- ✅ Define typed instrument, articulation, sample, and mic overrides.
- ✅ Parse `kit-overrides.yml` during the existing kit scan.
- ✅ Add strict known-field validation and full-address warnings.
- ✅ Add exact path lookup.
- ✅ Add unique content-hash recovery.
- ✅ Keep invalid overrides isolated from valid siblings.

Likely files:

- New `source/Engine/LibraryKitOverrides.h`
- New `source/Engine/LibraryKitOverrides.cpp`
- [`source/Engine/KitLibModel.h`](../source/Engine/KitLibModel.h)
- [`source/Engine/KitLibModel.cpp`](../source/Engine/KitLibModel.cpp)

### ✅ 2. Add Deterministic Safe Writer

- ✅ Serialize only explicit overrides.
- ✅ Normalize paths, key ordering, floats, balance tokens, and filename-velocity keys.
- ✅ Validate the generated YAML before atomic replacement.
- ✅ Add atomic replacement, one backup, and read-only save rejection.
- ✅ Add external-change conflict handling.
- ✅ Expose focused add, update, reset, and reset-scope operations for velocity boundaries.
- ✅ Add equivalent update and reset operations for the remaining library values.

Likely files:

- `source/Engine/LibraryKitOverrides.h/.cpp`
- [`source/Engine/DrumKitEngine.h`](../source/Engine/DrumKitEngine.h)
- [`source/Engine/DrumKitEngine.cpp`](../source/Engine/DrumKitEngine.cpp)

### ✅ 3. Resolve Effective Library Values

- ✅ Keep parsed filename and folder defaults available for UI display.
- ✅ Resolve override values without overwriting their defaults.
- ✅ Apply effective balance, manual gain, and playback offset at their correct scopes during normal kit loading.
- ✅ Include effective manual gain in measurement, calibration, peak safety, preprocessing recipe, and cache identity during normal kit loading.
- ✅ Keep generated `metadata.csv` measurements read-only.

Likely files:

- [`source/Engine/KitLibModel.h`](../source/Engine/KitLibModel.h)
- [`source/Engine/KitLibModel.cpp`](../source/Engine/KitLibModel.cpp)
- [`source/Engine/SampleMetadata.h`](../source/Engine/SampleMetadata.h)
- [`source/Engine/InstrumentPlayerLoader.cpp`](../source/Engine/InstrumentPlayerLoader.cpp)
- [`source/Engine/SampleLoader.h`](../source/Engine/SampleLoader.h)
- [`source/Engine/SampleLoader.cpp`](../source/Engine/SampleLoader.cpp)

### ✅ 4. Add Editable Exact Velocity Boundaries

- ✅ Keep original filename velocity as velocity-level identity.
- ✅ Resolve and validate upper boundaries per library articulation.
- ✅ Build exact `velocityToLevel[128]` from resolved boundaries.
- ✅ Rebuild only matching primary and additional instrument player articulation tables.
- ✅ Use resolved velocity-level boundaries for direct preview and the rebuilt table for normal notes.
- ✅ Keep round robin, calibration, response gain, and floating-selection ownership separate.

Likely files:

- [`source/Engine/KitLibModel.h`](../source/Engine/KitLibModel.h)
- [`source/Engine/InstrumentPlayer.h`](../source/Engine/InstrumentPlayer.h)
- [`source/Engine/InstrumentPlayerLoader.cpp`](../source/Engine/InstrumentPlayerLoader.cpp)
- [`source/Engine/InstrumentPlayerController.cpp`](../source/Engine/InstrumentPlayerController.cpp)
- [`source/Engine/DrumKitEngine.h`](../source/Engine/DrumKitEngine.h)
- [`source/Engine/DrumKitEngine.cpp`](../source/Engine/DrumKitEngine.cpp)

### ✅ 5. Add Library Kit Override Snapshot And Update API

- ✅ Return copied velocity-range display data for one exact library articulation.
- ✅ Include filename defaults, explicit overrides, effective values, measurement status, round robin grouping, and writable state.
- ✅ Include filename, explicit override, effective range, and writable state for velocity boundaries.
- ✅ Add value-addressed velocity updates instead of exposing model or instrument player pointers.
- ✅ Apply live velocity changes outside playback and publish bounded runtime data safely.
- ✅ Add targeted sample preprocessing recipe reload for manual gain.
- ✅ Keep one status path for applied, pending reload, save failed, and stale address.

Likely files:

- [`source/Engine/DrumKitEngine.h`](../source/Engine/DrumKitEngine.h)
- [`source/Engine/DrumKitEngine.cpp`](../source/Engine/DrumKitEngine.cpp)
- [`source/Engine/InstrumentPlayer.h`](../source/Engine/InstrumentPlayer.h)
- [`source/Engine/InstrumentPlayerLoader.cpp`](../source/Engine/InstrumentPlayerLoader.cpp)

### ✅ 6. Build `LibraryArticulationEditor`

- ✅ Reuse the existing modal visual language and commit pattern.
- ✅ Add horizontal velocity-level blocks and draggable dividers.
- ✅ Show each velocity level's exact inclusive range in its block and add even distribution, reset, keyboard control, click preview, and release preview.
- ✅ Add instrument and articulation balance controls.
- ✅ Add the matrix with round robin ID rows and velocity level columns, empty initial selection, whole-table, row, and column inversion, mic filters, selected-file list, wheel editing, and batch manual-gain and playback-offset fields.
- ✅ Keep filename range data and generated sample measurements available through tooltips without verbose permanent rows.
- ✅ Show library-wide scope, read-only state, and persistence errors for range editing.
- ✅ Show pending targeted reload state for live manual-gain editing.

Likely files:

- New `source/UI/LibraryArticulationEditor.h`
- New `source/UI/LibraryArticulationEditor.cpp`
- [`source/UI/UITheme.h`](../source/UI/UITheme.h)
- [`source/UI/VelocityResponseEditor.h`](../source/UI/VelocityResponseEditor.h)
- [`source/UI/VelocityResponseEditor.cpp`](../source/UI/VelocityResponseEditor.cpp)

Reuse small helpers from `VelocityResponseEditor` only when they are truly shared. Do not force horizontal range editing into the existing dB editor class.

### ✅ 7. Integrate With Pad Popup And Preview

- ✅ Add access to `LibraryArticulationEditor` from exact library-articulation map layers.
- ✅ Open the editor with a stable library edit address.
- ✅ Disable the action for unresolved or ambiguous library articulations.
- ✅ Keep the editor open across live range changes and refresh copied library data after commit.
- ✅ Preserve all instrument-map fields because this workflow does not rewrite the map.
- ✅ Preview the selected file and mic without changing normal round robin history.
- ✅ Preview the complete linked logical hit without changing normal round robin history.

Likely files:

- [`source/UI/PadContextPopup.h`](../source/UI/PadContextPopup.h)
- [`source/UI/PadContextPopup.cpp`](../source/UI/PadContextPopup.cpp)
- [`source/PluginEditor.h`](../source/PluginEditor.h)
- [`source/PluginEditor.cpp`](../source/PluginEditor.cpp)

### 8. Add Offline Correction Planner

- ✅ Add deterministic dry-run output first.
- ✅ Parse filenames through rules matching `KitLibModel`.
- Validate all operations before any write.
- Add explicit rename-token materialization.
- Add optional audio materialization only after rename-only behavior is verified.
- Update remaining override identities and regenerate `metadata.csv` after successful sample-file changes.

Likely files:

- New `scripts/apply_kit_overrides.py`
- Shared or focused parser helpers under `scripts/`
- [`scripts/generate_sample_metadata.py`](../scripts/generate_sample_metadata.py)

Do not add rename or audio modification behavior to `generate_sample_metadata.py`.

### 9. Update Durable Contracts And Documentation

After implementation:

- ✅ Document `kit-overrides.yml` ownership in `source/Engine/AGENTS.md`.
- ✅ Document the library-articulation editor and its library-wide scope in `source/UI/AGENTS.md`.
- Document the offline correction tool and safety rules in `scripts/AGENTS.md`.
- ✅ Update TODO14 metadata ownership so generated measurements and user overrides remain clearly separate.
- ✅ Update TODO16 cache rules for effective manual gain and live targeted replacement.
- ✅ Mark completed sections in this plan with `✅`.
- When implementation and all required verification are complete, change the main heading to start with `# ✅ COMPLETED:`.

## Automated Verification

### Parser And Writer

Add focused tests for:

- missing override file;
- valid version-1 file;
- unsupported version;
- invalid balance, gain, offset, path, hash, and velocity boundary;
- partial valid data beside one invalid field;
- deterministic ordering and numeric formatting;
- zero as an explicit value;
- reset removing a key;
- atomic replacement failure preserving the old file;
- backup behavior;
- read-only behavior;
- external-change conflict detection;
- exact path match;
- unique content-hash recovery;
- ambiguous hash recovery being rejected;
- stale instrument, articulation, velocity level, sample, and mic addresses.

### Velocity Selection

Add focused engine tests for:

- default boundaries matching current exact selection;
- edited two-level, three-level, and dense library-articulation ranges;
- final velocity level always ending at `127`;
- every velocity level keeping at least one MIDI value;
- one-level library articulation covering `1..127`;
- all logical hits in one velocity level sharing one range;
- linked mic entries remaining in one logical hit;
- reset returning to filename defaults;
- preview and normal notes choosing the same velocity level;
- primary and additional map layers receiving the updated library-articulation table;
- unrelated library articulations remaining unchanged;
- `velocityResponse` gain remaining independent from velocity-level selection boundaries;
- round robin counters and round robin lock behavior remaining unchanged.

### Library Values And Runtime

Add focused tests for:

- instrument balance precedence;
- articulation balance precedence;
- filename, override, and effective values remaining available;
- effective whole-file gain replacing rather than adding to filename gain;
- gain and calibration being applied exactly once;
- cache identity changing when canonical effective gain changes;
- live temporary gain correction avoiding a double gain during buffer replacement;
- true-peak warnings using effective gain;
- whole-file and per-mic offset precedence;
- negative advance and positive delay;
- next-note and preview offset updates without cache reload;
- unrelated sample files and instrument players not reloading.

### Offline Tool

Use temporary fixture kits to verify:

- dry run changes nothing;
- velocity renames preserve the round robin ID, mic chain, extension, and unrelated tokens;
- balance folder renames preserve other identity parts;
- gain and offset token replacement is idempotent;
- rename cycles use safe temporary names;
- collision, missing file, stale hash, and ambiguous hash stop apply;
- failed apply restores original paths;
- audio processing is impossible without its explicit option;
- processed output preserves required format properties;
- clipping policy is enforced;
- metadata regeneration happens only after successful sample-file changes;
- successfully materialized fields are removed while remaining overrides stay valid.

## Manual Verification

### Velocity Ranges

- Open articulations with one, two, three, and many velocity levels.
- Confirm the graph matches filename defaults before any edit.
- Drag each divider and hear the selected timbre change on the next preview.
- Use keyboard and numeric editing for exact boundaries.
- Confirm low and high MIDI edges select the expected velocity level.
- Confirm round robin IDs still cycle inside the chosen velocity level.
- Save, close, reopen, reload all kits, and confirm the same ranges.
- Reset one divider and all dividers separately.
- Confirm editing one articulation does not change another articulation.

### Other Library Kit Overrides

- Confirm the matrix opens with no selected cells or active outline.
- Click `*`, round robin IDs, and velocity headings from empty, mixed, and full selections and confirm only existing cells invert without preview.
- Confirm header labels and round robin IDs are centred and the round robin ID column matches the header style.
- Wheel each value field and use Shift-wheel to confirm coarse and fine steps start from the visible or effective value.
- Select several hits and confirm a saved value previews the complete hit from the final selected-file row, not a previous solo mic.
- Change instrument balance and articulation balance separately.
- Confirm articulation balance wins and reset restores inherited behavior.
- Change manual gain while a sample is sounding and confirm smoothing has no click.
- Confirm targeted reload does not silence or reload unrelated instruments.
- Change whole-file and per-mic playback offsets and preview the next hit.
- Confirm the UI always shows filename, override, and effective values.
- Confirm read-only kits can preview but cannot save.
- Open the same file in another process, change it, and confirm conflict handling prevents silent overwrite.

### Library Scope

- Map the same library articulation to two pads.
- Edit its velocity-level boundaries and confirm both mappings use the change.
- Confirm mapped `Mix Gain`, pan, bleed, tuning, attack, decay, phase, L/R, routing, and `velocityResponse` remain unchanged.
- Open another already-running plugin instance and confirm it changes only after explicit reload or reopening.

### Materialization

- Generate and review a rename-only plan.
- Confirm no files change during plan generation.
- Apply fixture renames and confirm the kit loads with the same effective behavior after the matching overrides are removed.
- Apply explicit audio gain and offset processing only on backed-up fixtures.
- Confirm `metadata.csv` hashes and measurements match the changed files.
- Confirm a forced failure restores the original fixture.

## Dependencies And Related Plans

- [TODO14](TODO14_SAMPLE_CALIBRATION_AND_VELOCITY_RESPONSE_PLAN.md) owns generated measurements, calibration, exact logical hits, `VelocityResponseEditor`, and sample preprocessing.
- [TODO15](TODO15_FLOATING_SAMPLE_SELECTION_PLAN.md) owns optional floating selection. Its candidate windows must rebuild from the edited exact velocity regions when that plan is implemented.
- [TODO16](TODO16_DERIVED_SAMPLE_CACHE_AND_BAKE_CACHE_PLAN.md) owns persistent processed buffers and canonical recipe identity.
- [TODO17](TODO17_KIT_BROWSER_AND_LINKED_PAD_EDITING_PLAN.md) may later open this editor from its library-kit browser.
- [TODO11](TODO11_PAD_POPUP_LAYER_PARAMETERS_PLAN.md) owns map layer controls and must remain separate.
- [Instrument map format](INSTRUMENT_MAP_FORMAT.md) documents map layer fields. `kit-overrides.yml` must not become another instrument-map syntax.

Implementation should start after TODO14's remaining exact-runtime and cache verification is understood, because effective manual gain and targeted buffer replacement depend on that contract. Velocity boundary parsing and UI can be implemented independently of persistent derived cache.

## Non-Goals

- Do not replace `metadata.csv`.
- Do not store generated measurements in `kit-overrides.yml`.
- Do not store library corrections in plugin session state or the instrument map.
- Do not change global or per-map-layer `velocityResponse`.
- Do not change floating-selection scoring in this plan.
- Do not give different ranges to logical hits in the same velocity level.
- Do not auto-detect preferred velocity boundaries in version 1.
- Do not rename or process files when saving from the editor.
- Do not add a general waveform editor.
- Do not add destructive trim, normalization, denoise, or phase processing without a separate reviewed field and explicit materialization policy.
- Do not silently repair stale or ambiguous library identities.

## Completion Criteria

- Every library articulation can expose its effective exact velocity ranges.
- The user can edit boundaries with a graph, keyboard, numeric control, and preview.
- All logical hits and linked mics stay grouped under their velocity level.
- Range changes are audible on the next note without sample reload.
- Placement balance, whole-file manual gain, and whole-file or per-mic playback offsets can be edited at their correct scopes.
- Filename defaults, explicit overrides, and effective values remain visible and separate.
- Overrides persist in deterministic, valid, portable `kit-overrides.yml`.
- Regenerating `metadata.csv` does not remove user overrides.
- Runtime parsing and rebuilding remain outside the audio thread.
- Manual gain is applied exactly once during live editing, canonical reload, preview, and normal playback.
- Changes affect every use of the matching library articulation or sample and do not rewrite instrument maps.
- Read-only, stale, ambiguous, invalid, and conflicting data produce clear safe behavior.
- A dry-run correction plan can convert reviewed overrides into exact proposed renames or processing steps without changing files.
- Explicit materialization is recoverable and leaves updated metadata and remaining overrides consistent.
