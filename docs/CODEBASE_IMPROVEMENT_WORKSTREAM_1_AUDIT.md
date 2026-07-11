# Workstream 1 Audit

Status: active.

This audit records repeated and similar code before any helper moves. Callers below are setup, model, mapping, or UI work unless the row says otherwise. None of the accepted first-pass candidates run in the audio render path.

| Locations and callers | Shared operation | Important differences | Domain and thread | Decision | Expected reduction | Test need |
| --- | --- | --- | --- | --- | --- | --- |
| `source/Engine/DrumKitEngine.cpp`: map normalization, ambiguity messages, map application. `source/Engine/KitLibModel.cpp`: model identity generation and resolution. | Make a normalized identity key and test whether an ID is in a candidate list. | The functions are exact duplicates. Empty requested IDs do not match. Candidate creation has extra deduplication rules and stays in the model. | Engine setup and model parsing on the message or loader thread. | Merge the two small primitives into `source/Engine/helpers/InstrumentMapIdentity.h`. | Remove about 16 duplicated lines. | Cover empty IDs, case, whitespace, duplicate normalized candidates, and ambiguous resolution through public model APIs. |
| `source/Engine/DrumKitEngine.cpp`: `joinNames()` and the local lambda in `serializeInstrumentMap()`. | Trim names, skip empty values, and join with commas. | The instrument-map format now supports one destination and one source articulation per line. Multi-articulation values are obsolete. | Instrument-map serialization on the message thread. | Delete the local lambda and then remove obsolete multi-articulation joining after parser and serialization checks show that one value is the only supported format. Do not extract a generic join helper. | At least 8 lines after the obsolete path is removed. | Add parse and serialization coverage for one articulation and reject or normalize multiple values according to the current parser contract. |
| `source/Engine/DrumKitEngine.cpp`: kit, instrument, and articulation match counting and error descriptions. `source/Engine/KitLibModel.cpp`: three resolve-and-count loops. | Find a unique identity candidate and report zero, one, or many matches. | Collection ownership and result types differ. Engine code creates user-facing error messages; model code returns pointers or strings. | Mapping setup and model parsing. | Leave separate for now. Share only the small identity primitives first. | None in first pass. | Existing identity tests plus ambiguity tests. |
| `source/Engine/DrumKitEngineMapHelpers.cpp`: entry and reload fingerprint functions. `source/Engine/DrumKitEngine.cpp`: resolved-entry lambdas. | Build a stable fingerprint from entry fields and source content. | Reload fingerprints intentionally omit parameter-only fields. Resolved fingerprints intentionally append source content. | Mapping setup. | Leave separate. A boolean or callback option would hide important rules. | None. | Existing fingerprint and map-application tests. |
| `source/Engine/DrumKitEngine.cpp`: sorted serialization and parameter formatting. `source/Engine/DrumKitEngineMapHelpers.cpp`: sorted bleed and route formatting. | Sort text keys and write stable map text. | Delimiters and field formatting are part of different text contracts. | Mapping setup and serialization. | Leave local until a concrete format becomes exactly shared. | None. | Existing serialization round-trip tests. |
| `source/Engine/InstrumentCatalog.h`, `LayoutManager.h`, `RoutingTypes.h`, `MicRoutingPlan.h`, and instrument-map files. | Trim and lowercase lookup keys. | Some keys must remain display text; some are only map keys; some are in realtime-adjacent types. | Mixed setup, UI, and realtime-adjacent domains. | Defer to Workstream 2 string and ID audit. | None. | Workstream 2 audit. |
| `source/Engine/HiHatTransitionPolicy.cpp`, `InstrumentSynthController.cpp`, `MidiCrosstalkFilter.cpp`, `LayoutManager.h`, and `DrumKitEngine.h`. | Clamp or validate MIDI values in the `0..127` range. | Policy code clamps by design while layout parsing reports invalid input. | Realtime controller code and configuration parsing. | Leave separate. Shared code would blur clamp versus validation behavior. | None. | Existing MIDI and parser tests. |

## Helper Inventory And Proposed Ownership

- Existing engine helpers are mixed between `source/Engine/` header files, local namespaces, and `DrumKitEngineMapHelpers.*`.
- No `source/Engine/helpers/` folder exists today.
- The first durable helper should be `source/Engine/helpers/InstrumentMapIdentity.h`. It will own only pure identity-key and candidate-matching primitives used by the model and instrument-map engine code.
- `DrumKitEngineMapHelpers.*` remains the owner of stateful instrument-map rebuild helpers. No files move during the identity merge.

## First Accepted Change

Move only normalized identity-key and candidate-match functions to the named engine helper. This is a direct merge of exact duplicates. The caller behavior, text formats, and realtime code remain unchanged.
