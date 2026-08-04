# Workstream 1 Audit

Status: active.

This audit records repeated and similar code before any helper moves. Callers below are setup, model, mapping, or UI work unless the row says otherwise. None of the accepted first-pass candidates run in the audio render path.

| Locations and callers | Shared operation | Important differences | Domain and thread | Decision | Expected reduction | Test need |
| --- | --- | --- | --- | --- | --- | --- |
| `source/Engine/DrumKitEngine.cpp`: map normalization, ambiguity messages, map application. `source/Engine/KitLibModel.cpp`: model identity generation and resolution. | Make a normalized identity key and test whether an ID is in a candidate list. | The functions are exact duplicates. Empty requested IDs do not match. Candidate creation has extra deduplication rules and stays in the model. | Engine setup and model parsing on the message or loader thread. | Merge the primitives when map types move to `InstrumentMap.h` and `InstrumentMap.cpp`. Do not create these files for 16 lines alone. | Remove about 16 duplicated lines. | Cover empty IDs, case, whitespace, duplicate normalized candidates, and ambiguous resolution through public model APIs. |
| `source/Engine/DrumKitEngine.cpp`: `joinNames()` and the local lambda in `serializeInstrumentMap()`. | Trim names, skip empty values, and join with commas. | The instrument map format supports one articulation slot and one library articulation per line. Multi-articulation values are obsolete. | Instrument map serialization on the message thread. | Delete the local lambda. Remove obsolete multi-articulation joining after parser and serialization checks confirm the current format. Do not extract a generic join helper. | At least 8 lines after the obsolete path is removed. | Cover one articulation assignment. Reject or normalize multiple values according to the current parser contract. |
| `source/Engine/DrumKitEngine.cpp`: library kit, instrument, and articulation match counting and error descriptions. `source/Engine/KitLibModel.cpp`: three resolve-and-count loops. | Find a unique identity candidate and report zero, one, or many matches. | Collection ownership and result types differ. Engine code creates user-facing error messages. Model code returns pointers or strings. | Mapping setup and model parsing. | Leave separate for now. Share only the small identity primitives first. | None in first pass. | Existing identity tests plus ambiguity tests. |
| `source/Engine/DrumKitEngineMapHelpers.cpp`: entry and reload fingerprint functions. `source/Engine/DrumKitEngine.cpp`: resolved-entry lambdas. | Build a stable fingerprint from layer fields and library content. | Reload fingerprints omit parameter-only fields. Resolved fingerprints append library content. | Mapping setup. | Leave separate. A boolean or callback option would hide important rules. | None. | Existing fingerprint and map-application tests. |
| `source/Engine/DrumKitEngine.cpp`: sorted serialization and parameter formatting. `source/Engine/DrumKitEngineMapHelpers.cpp`: sorted bleed and route formatting. | Sort text keys and write stable map text. | Delimiters and field formatting are part of different text contracts. | Mapping setup and serialization. | Leave local until a concrete format becomes exactly shared. | None. | Existing serialization round-trip tests. |
| `source/Engine/InstrumentCatalog.h`, `LayoutManager.h`, `RoutingTypes.h`, `MicRoutingPlan.h`, and instrument-map files. | Trim and lowercase lookup keys. | Some keys must remain display text; some are only map keys; some are in realtime-adjacent types. | Mixed setup, UI, and realtime-adjacent domains. | Defer to Workstream 2 string and ID audit. | None. | Workstream 2 audit. |
| `source/Engine/HiHatTransitionPolicy.cpp`, `InstrumentPlayerController.cpp`, `MidiCrosstalkFilter.cpp`, `LayoutManager.h`, and `DrumKitEngine.h`. | Clamp or validate MIDI values in the `0..127` range. | Policy code clamps by design while layout parsing reports invalid input. | Realtime controller code and configuration parsing. | Leave separate. Shared code would blur clamp versus validation behavior. | None. | Existing MIDI and parser tests. |

## Helper Inventory And Proposed Ownership

- Existing engine helpers are mixed between `source/Engine/` header files, local namespaces, and `DrumKitEngineMapHelpers.*`.
- No `source/Engine/InstrumentMap/` feature folder exists today.
- Start the feature with `source/Engine/InstrumentMap/InstrumentMap.h` and `InstrumentMap.cpp` when map types move from `DrumKitEngine`.
- Move the identity-key and candidate-matching operations into this pair during the same change.
- `DrumKitEngineMapHelpers.*` remains the owner of stateful instrument-map rebuild helpers. No files move during the identity merge.

## Accepted Merge Constraint

Do not create a feature pair only for the normalized identity-key and candidate-match functions.

First move the map types. Then merge these exact duplicates into the same pair without changing caller behavior or text formats.
