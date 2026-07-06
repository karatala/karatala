# TODO Instrument Library Identity And Tools Plan

Status: phases 1-3 implemented; phase 4 first UI pass implemented, replacement wizard still open.

## Goal

Make library paths safer to rename and easier to fix.

The instrument map should not depend on full folder names when the extra folder text is only secondary information. It should use the shortest unique identity for kits, instruments, and articulations. Older full paths should still resolve when possible.

This plan also covers helper tools for renaming sample files after parent folders are renamed, balance support, and a small troubleshooting UI for broken instrument-map paths and logs.

## Current Facts

- `../source/Engine/KitLibModel.h` documents this folder shape:
  - `<kitInfo>/<instrumentType>-<size>[-<balance>][-<description>]/<articulation>[expression info]/sample file`
- `../source/Engine/KitLibModel.cpp` currently parses kit folder info into `codeRaw`, `name`, `author`, and `license`.
- `../source/Engine/KitLibModel.cpp` currently parses instrument folder info into `codeRaw`, `name`, `size`, optional `balance`, and `description`.
- Balance is currently parsed from articulation folder suffixes, such as `center-L64` or `bow-R64`, and stored on `ArticulationSpec`.
- `../source/Engine/DrumKitEngineMappingModel.cpp` parses instrument-map text lines like `slot:kit/instrument/articulation:params`.
- `../source/Engine/DrumKitEngine.cpp` applies the parsed map and resolves kit, instrument, and articulation references.
- `../source/Engine/KitLibModel.cpp` currently resolves kits by kit display name or full kit folder name.
- `../source/Engine/KitLibModel.cpp` currently resolves instruments by exact full instrument folder name.
- `../source/Engine/DrumKitEngine.cpp` currently reports missing kits, missing instruments, and unknown source articulations while applying a map.
- `../source/UI/TroubleshootingPanel.*` owns the `Log Level` dropdown, log file buttons, important-log text area, library reload button, and first missing-map recheck action.
- `../source/Config/PluginLogger.h` writes instance logs under the data path `logs` folder and keeps recent `Error`, `Warning`, and `Notice` lines for the troubleshooting UI.

## Proposed Identity Rules

Use the shortest unique identity. If a shorter identity has duplicates, use the next longer identity.

### Kit Identity

Folder format:

`KitName by AuthorName [License]`

Identity order:

1. `KitName`
2. `KitName by AuthorName`
3. `KitName by AuthorName [License]`
4. Full folder name, if different from the generated text

Example:

- `AasiMonster by DIE [CC-BY-4.0]` can be written as `AasiMonster` if no other kit uses that name.
- If another `AasiMonster` exists, write `AasiMonster by DIE`.
- If the name and author still collide, write `AasiMonster by DIE [CC-BY-4.0]`.

### Instrument Identity

Folder format:

`<instrumentType>-<size>[-<balance>][-<description>]`

Identity order:

1. `<instrumentType>-<size>`
2. `<instrumentType>-<size>-<balance>`
3. `<instrumentType>-<size>-<balance>-<description>`
4. Full folder name, if different from the generated text

Examples:

- `tom-10-Sonor` can be written as `tom-10` if unique in the kit.
- `tom-10-L64-Sonor` can be written as `tom-10-L64` if `tom-10` is not unique but `tom-10-L64` is unique.
- Keep the full folder name only when needed.

### Articulation Identity

Folder format:

`<articulation>[optionalExpressionShortName]-<optionalExpressionCode><value>-...`

Identity order:

1. `<articulation>[optionalExpressionShortName]`
2. Full articulation folder name

Examples:

- `center-zon_ctr_rim043` can be written as `center` if only one center articulation exists.
- If both `center-zon_ctr_rim043` and `center-zon_ctr_rim085` exist, use the full articulation folder name.

## Proposed Resolver Behavior

Add reusable identity resolver logic in the library model.

Suggested model additions:

- Store candidate identity strings for each kit, instrument, and articulation.
- Build alias maps after scanning children.
- An alias resolves only when it points to exactly one item.
- Ambiguous aliases do not resolve silently.
- Exact full folder names should always be accepted.
- Matching should be case-insensitive after trimming.

Suggested APIs:

- `DrumKitSpec::getPreferredInstrumentId(const InstrumentSpec&)`
- `DrumKitSpec::findInstrumentById(const juce::String&)`
- `InstrumentSpec::getPreferredArticulationId(const ArticulationSpec&)`
- `InstrumentSpec::findArticulationById(const juce::String&)`
- `LibraryManager::getPreferredKitId(const DrumKitSpec&)`
- `LibraryManager::findKitById(const juce::String&)`

The exact API names can change during implementation if existing style suggests better names.

## Instrument Map Changes

When reading an instrument map:

- Resolve kit, instrument, and articulation references through the new identity resolver.
- Keep support for old full folder names.
- If a short identity is ambiguous, report a clear error with available choices.
- If a path is missing, collect enough data for the UI fixer to suggest replacements.

When writing, generating, changing, or saving an instrument map:

- Use the shortest unique kit identity.
- Use the shortest unique instrument identity inside that kit.
- Use the shortest unique articulation identity inside that instrument.
- Preserve explicit params like `tuning`, `attack`, `decay`, `vol`, `flipLR`, `flipPhase`, and `bleed`.
- Revalidate and normalize resolved old names when the map changed.
- Sort map lines by destination slot so lines for the same instrument/articulation stay together.
- Do not sort or reorder layer order inside the same destination instrument/articulation slot.

Example before rename:

`tom2/rim:aasimonster2/tom-X1/center:tuning=78,attack=3`

Example after identity resolver and folder rename:

`tom2/rim:AasiMonster2/tom-10/center:tuning=78,attack=3`

If `tom-10` is not unique, write the next longer ID, for example:

`tom2/rim:AasiMonster2/tom-10-Sonor/center:tuning=78,attack=3`

## Balance Support

Keep articulation-level balance. It is useful when different articulations from one instrument were recorded from different sides or conditions.

Add instrument-level balance as a fallback:

- Parse optional `<balance>` from instrument folder names, such as `tom-10-L64-Sonor`.
- Store valid balance on `InstrumentSpec::MetaInfo`.
- Use articulation balance first.
- If articulation balance is missing, use instrument balance.
- If a balance slot is only `L`, `R`, or `C`, ignore it and warn that the balance value is missing.
- If a balance-like token is invalid, ignore it and warn. Do not warn for normal descriptions like `Ludwig`.
- If multiple articulation balances conflict, keep current conflict behavior.

This avoids losing detail and supports the new documented folder format.

## Rename Helper Script

Add a Python script under `../scripts/`.

Suggested file:

`../scripts/rename_samples_from_parent_folders.py`

Purpose:

- Help after kit, instrument, or articulation folders were renamed.
- Rename sample files so their prefix follows the parent folder identity.
- Generate a `.bat` file for review instead of renaming directly.

Inputs:

- Root folder to scan.
- Optional output path for the generated `.bat`.
- Optional dry-run output summary.

Behavior:

- Walk kit, instrument, and articulation folders.
- Parse instrument folder into type, size, optional balance, and description.
- Parse articulation folder into base articulation plus optional expression suffixes.
- For each audio file, detect current sample prefix:
  - `<instrumentType>-<size>-<articulation>[optionalExpressionShortName]-...`
- Write a `ren` command only when the expected filename differs.
- Quote paths safely for Windows batch files.
- Do not overwrite existing files.
- Put conflicts, possible invalid name format after rename and skipped files into comments in the `.bat`.

Example output:

```bat
rem Review before running.
rem SKIP "whatever-name.flac" "tom-10-center-whatever-name.flac"
ren "tom-X1-center-127a-dtm.flac" "tom-10-center-127a-dtm.flac"
ren "127b-dtm.flac" "tom-10-center-127b-dtm.flac"
rem SKIP exists: tom-10-center-127c-dtm.flac
```

Verification later:

- Run the script on a small copied fixture.
- Review generated `.bat` output.
- Do not run the generated `.bat` during normal tests.

## Missing Instrument Map Fixer UI

Add a troubleshooting area as a new sidebar 'FIX' page.

Simple first version:

- Top button row: `Important logs:`, `Log level`, `Write Log Files`, `Open Log`, `Logs Folder`, spacer, `Fix Missing Instruments`, and `Reload Library`.
- Large read-only text area below the buttons for recent `Error`, `Warning`, and `Notice` log lines.
- Parse current instrument map text.
- Try to resolve each entry.
- Show 10 missing paths at a time.
- Show the old path, for example `aasimonster2/tom-X1/center`.
- Preselect the best replacement in a dropdown.
- Allow manual replacement selection.
- Confirm updates current map text and moves to the next missing path.
- When finished, apply the updated map.
- `Reload Library` rescans the sample library so folder renames and file changes can be checked again without restarting.

Replacement suggestion rules:

1. Prefer same slot type from the active kit layout.
2. Prefer same parsed instrument type and size.
3. Prefer same articulation base name.
4. Prefer close text match for kit, instrument, and articulation names.
5. Prefer loaded library items with samples.

Needed engine/UI support:

- A reusable resolver result type, for example `Resolved`, `Missing`, or `Ambiguous`.
- A replacement candidate list with display text and target map path.
- A function to rewrite only the selected map entries paths (with same missing path) while preserving params.
- A function to expose recent `Error`, `Warning`, and `Notice` log lines for the Fix page text area.
- A reload action that rescans the library and rechecks the current map.

## Logs UI

Move log controls from the config page to the troubleshooting page.

Controls:

- `Log Level` dropdown.
- `Open Log` button.
- `Logs Folder` button.

Needed support:

- Expose current log file path from `GlobalLogger` or `DrumKitEngine`.
- Expose logs folder path from the data path.
- Buttons should use normal JUCE file/folder launching.
- If log level is `Off` and no log file exists, show a simple status message instead of failing silently.

## Implementation Phases

### Phase 1 - Document And Parser Model

Status: implemented in `../source/Engine/KitLibModel.*`, `../source/Engine/DrumKitEngineMapHelpers.cpp`, and `../tests/TestKitLibParsers.cpp`.

Implemented:

- Updated `../source/Engine/KitLibModel.h` doc comment with identity rules.
- Added parsed instrument balance to metadata, including warnings for bare `L`, `R`, or `C` balance tokens.
- Added identity candidate helpers for kits, instruments, and articulations.
- Added unique-ID lookup and preferred-ID helpers.
- Added instrument balance fallback through `ArticulationSpec::getEffectiveBalance()`.
- Added focused parser/model tests for kit, instrument, articulation identity, duplicate IDs, and balance fallback.

Verification status:

- Not run yet, per user request not to build or run tests unless asked.

### Phase 2 - Instrument Map Resolver

Status: implemented in `../source/Engine/DrumKitEngine.*`, `../source/Engine/DrumKitEngineMapHelpers.cpp`, `../source/Engine/KitLibModel.*`, `../source/UI/*`, and `../tests/TestInstrumentMap.cpp`.

Implemented:

- Map apply, automatic orientation, and preview source resolving use identity lookup.
- Old full folder names stay valid when they resolve uniquely.
- Generated and changed map text is normalized to preferred IDs and sorted by destination slot/art group.
- Stable sorting keeps layer order inside the same slot/art group.
- Missing and ambiguous kit, instrument, and articulation IDs report clearer status text.
- Source pickers keep raw path aliases so old maps still select the right source row.

Verification status:

- Added focused `InstrumentMap` tests for preferred generated IDs and stable slot grouping.
- Not run yet, per user request not to build or run tests unless asked.

### Phase 3 - Rename Script

Status: implemented in `../scripts/rename_samples_from_parent_folders.py`.

Implemented:

- Added the Python script.
- Generates reviewable `.bat` output only.
- Uses parent instrument and articulation folders to build expected sample file prefixes.
- Adds comments for skipped files, existing targets, duplicate planned targets, case-only renames, and invalid folder/file shapes.

Verification status:

- Not run yet, per user request not to build or run tests unless asked.

### Phase 4 - Troubleshooting UI

Status: first pass implemented in `../source/UI/TroubleshootingPanel.*`, `../source/UI/Sidebar.*`, `../source/PluginEditor.*`, `../source/UI/EngineSettingsPanel.cpp`, and `../source/Config/PluginLogger.h`.

Implemented:

- Added `Fix` sidebar page.
- Moved log level from config page to the Fix page.
- Added `Write Log Files`, `Open Log`, `Logs Folder`, `Fix Missing Instruments`, and `Reload Library` controls.
- Added the recent `Error`, `Warning`, and `Notice` log text area.
- Added logger accessors for current log file, logs folder, recent important lines, and session file-writing toggle.
- Added reload library action that rescans library data and reapplies current state.
- Added first missing-map action that normalizes and reapplies the current map, then shows the engine status.

Still open:

- Full missing-map replacement wizard with per-missing-path dropdown suggestions.

Verify later:

- Open UI manually.
- Test missing instrument path replacement.
- Test log level save/load.
- Test open log and logs folder buttons.

## Non-Goals

- Do not change audio-thread behavior.
- Do not remove support for full folder names.
- Do not auto-edit user maps without confirmation in the UI fixer.
- Do not run generated rename batch files from the app.
- Do not move all balance logic to instrument level.

## Resolved Decisions

- Map serialization rewrites to preferred short IDs when a map is generated or changed.
- When a changed map preset is saved, revalidation and sorting should apply.
- Sorting is only for grouping lines by destination slot. It must not reorder layer order inside the same destination instrument/articulation slot.
- Old full names that still resolve can be silently normalized on any map change.
- The missing-map fixer works only on failing entries.
- Kit identity keeps the exact license text from the folder, for example `[CC-BY-SA-whatever]`.
