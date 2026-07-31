# Project Terminology Review

Status: naming review. No rename or folder move is approved by this document.

## Core terms

| Term | Meaning in this project |
| --- | --- |
| Library | The scanned collection of recorded drum kits on disk. |
| Library kit | One recorded drum kit in the library. C++ type: `KitLib::DrumKitSpec`. |
| Kit layout | Layout for users logical (might be a combination of >1 physical device) e-drum kit MIDI capatibilities assignments of slots, articulations, MIDI notes, and controllers. |
| Instrument slot | One destination place in a kit layout, such as `kick1` or `tom2`. |
| Instrument map | Assignments from destination slots and articulations to recorded library content, including mapped-layer settings. |
| Library instrument | One recorded instrument inside a library kit. C++ type: `KitLib::InstrumentSpec`. |
| Library articulation | One recorded playing method, such as center, rim, bow, or edge. C++ type: `KitLib::ArticulationSpec`. |
| Destination articulation | A playable articulation defined by an instrument slot in the kit layout. |
| Mapping layer | One library articulation assigned to one destination articulation, with tuning, gain, routing, and other settings. |
| Velocity level | One recorded named-velocity group inside a library articulation. Current C++ type: `VelocityLayerSpec`. It is not a mapping layer. |
| Logical hit | One velocity level and round-robin choice with all mic files or channel ranges that belong to that hit. |
| Round robin | An alternate recorded hit at the same named velocity, usually identified by a letter. |
| Sample | One recorded audio file or channel range for one mic entry of a logical hit. |
| Mic layout | The named plugin outputs, their order, channel count, and compatible mic rules. |
| Route | An assignment from a recorded mic code to one mic-layout output. |
| Library override | A saved correction to recorded library data in `kit-overrides.yml`. Current code often calls this a source override. |
| Preview | Audition without changing the instrument map. Qualify it as library preview or mapped preview. |
| Preset | A saved reusable value. Always qualify it as instrument-map preset, velocity-response preset, or kit-layout preset. |
| Engine | The top-level runtime coordinator for layouts, maps, synths, loading, persistence, and audio processing. |
| Instrument synth | Prepared runtime data and playback behavior for mapped instrument content. |
| Voice | One active sample-playback state. A multi-mic logical hit normally uses several voices. |

## Terms that need a naming decision

| Current term | Problem | Alternatives |
| --- | --- | --- |
| Source | It can mean library content, a sample file, mapping input, C++ source code, or the source of a rule. Do not use it alone as a folder name. | Library content, library selection, library reference, sample file, or mapping input. |
| Source item | It identifies a library kit, instrument, and optional articulation. It is not an audio file. | `LibrarySelectionRef`, `LibraryArticulationRef`, `LibraryItem`. |
| Source picker | It selects recorded library content for an instrument map. | `LibraryPicker`, `LibrarySelectionPopup`, `InstrumentLibraryPicker`. |
| Source articulation editor | It edits persistent library corrections, not the mapped destination layer. | `LibraryArticulationEditor`, `SampleLibraryOverrideEditor`, `RecordedArticulationEditor`. |
| Layer | It currently means both a mapped assignment and a recorded velocity group. | Use mapping layer for the assignment and velocity level for recorded samples. |
| Kit | It can mean a library kit, kit layout, loaded instrument map, or UI page. | Always use library kit, kit layout, instrument map, or Kit page. |
| Override | It can mean a library correction, mapping value, routing choice, or velocity-response replacement. | Use library override, layer setting, route assignment, or response override. |

## Folder name review

`source/UI/Source/` is rejected because `Source` does not identify one responsibility.

Candidate structures:

1. `UI/Library/` for all library selection and editing UI. This is short but still broad.
2. `UI/LibrarySelection/` for the picker and references, plus `UI/LibraryEditing/` for persistent library corrections. This is the clearest current option.
3. `UI/InstrumentMapping/` for mapping selection, plus `UI/LibraryOverrides/` for persistent corrections. This is precise but longer.

Select the final names before moving files. Do not create a temporary `UI/Source/` folder.

## Before and after examples

| Scope | Before | After candidate |
| --- | --- | --- |
| One-use drum-pad helper | [DrumPadHelpers.h](../source/UI/DrumPadHelpers.h), namespace `DrumPadHelpers`, functions `buildDisplayOrder()` and `drawPadLabel()` | Move the functions to [DrumPad.cpp](../source/UI/DrumPad.cpp) under `DrumPadDetail`. Remove the helper file. The `DrumPad` class stays. |
| Instrument-map feature | [DrumKitEngineMapHelpers.h](../source/Engine/DrumKitEngineMapHelpers.h) and related `.cpp`, mapping-model, and text-view files, plus nested `DrumKitEngine::InstrumentMapEntry` | Move to `Engine/InstrumentMap/InstrumentMapTypes.h`, `InstrumentMapText.*`, `InstrumentMapProjection.*`, and `InstrumentMapFingerprint.*`. Candidate function rename: `makeInstrumentMapEntryFingerprint()` to `InstrumentMapFingerprint::forEntry()`. |
| Library UI | [SourceItem.h](../source/UI/SourceItem.h), [SourcePickerPopup.h](../source/UI/SourcePickerPopup.h), and [SourceArticulationOverrideEditor.h](../source/UI/SourceArticulationOverrideEditor.h) in the UI root | Candidate split: `LibrarySelection/LibrarySelectionRef.h`, `LibrarySelection/LibraryPickerPopup.*`, and `LibraryEditing/LibraryArticulationEditor.*`. Final names still require approval. |
| Broad UI helper | [UIHelpers.h](../source/UI/UIHelpers.h) owns `configureComboBox()` and `buildSourceItems()` | Move styling to `Controls/ControlStyling::configureComboBox()`. Move library-list building to candidate `LibrarySelection/LibrarySelectionList::build()`. |

## Naming rule

- Use a term from the core table or add a clear qualifier.
- A folder name must state one responsibility.
- A class name must state what the object represents.
- A namespace name must state which feature owns the operation.
- Do not rename many terms in one change. Rename one feature and update its tests and documents together.
