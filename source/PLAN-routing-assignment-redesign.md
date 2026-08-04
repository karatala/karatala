# ✅ COMPLETED: Routing + Assignment Redesign Plan

## Summary

- Stage the work so the current routing regressions are fixed before the larger UI redesign.
- Keep the immediate focus on preserving explicit note articulation identity for hi-hat and simple art (center/rim bow/edge/bell/etc) slots.
- Redesign the assignment popup around explicit user intent: replace a whole instrument, or assign only one articulation slot or map layer.
- Improve library preview routing after the mapping behavior is stable.

## Stage 1: Routing Regression Fix

- Replace instrument-player-wide CC articulation selection with note-aware and articulation-slot-aware resolution where controller routing needs context. Do not let controller sibling loading overwrite the direct note-to-articulation mapping.
- Resolve a MIDI note from its explicit articulation slot first, then only apply controllers attached to that note.
- For `pdl_opn_cls`, change openness while preserving the played zone where possible:
  - `bow[open] -> bow[half/close/tight]`
  - `edge[open] -> edge[half/close/tight]`
- For `zon_bow_edg` and `zon_ctr_rim`, use the CC value only when a new note starts; do not trigger live articulation transitions for already-ringing voices.
- When a zone CC is sampled at note-on, use it only to select the initial zone articulation, while preserving any already-known expression state where possible:
  - a new hit whose layout note is `bow[tight]` may start as `edge[tight]` if the zone CC says edge at that instant; it must not start as `edge[open]` just because edge/open is the first edge articulation found
  - a new non-expression drum hit may start as `rim` instead of `center` when `zon_ctr_rim` says rim at that instant
- For hi-hat live transitions, only `pdl_opn_cls` may change the sounding articulation after note-on, because pedal openness can move while the note is ringing.
- Keep unsupported controller dimensions such as `zon_lft_rgt` out of implicit expression-point routing for now.
- Ensure empty-articulation inheritance only applies when the articulation slot has no explicit map layers.
- Add temporary routing diagnostics behind a narrow debug helper or flag:
  - note number
  - slot id
  - articulation slot
  - resolved runtime articulation
  - library kit/instrument/articulation
  - whether resolution came from direct mapping, CC, fallback, or alias
- Remove or downgrade temporary diagnostics after the routing issue is confirmed.

## Stage 2: Assignment Popup Semantics

- Add two explicit assignment modes:
  - `Replace whole instrument`: choose a library instrument, clear existing map layers for that instrument slot, then generate one map layer for each library articulation. Use the matching articulation-slot code when one is available.
  - `Assign articulation`: choose library articulations and update only the selected articulation slot.
- Use `Replace whole instrument` as the default when opening from a main/head articulation of a normal head/rim drum.
- Use `Assign articulation` as the default when opening from a specific articulation slot or map layer.
- In replace mode, show instrument-level picker rows instead of every articulation row.
- In replace mode, generate one explicit map layer for each library articulation, such as `tom1/center` and `tom1/rim`. If there is no matching articulation slot, keep the articulation-slot part empty.
- In assign mode, keep articulation-level picker rows.
- For hi-hat library rows, show all available articulations with their full bracketed expression labels, for example `edge[open]`, `edge[half]`, `edge[close]`, `edge[tight]`, and `pedal[splash]`.
- Recommended UI checkbox label: `REPLACE`.
- Secondary/help text: `Replace whole instrument with all matching articulations`.
- Keep full articulation-slot identity internally, for example `bow[tight]`; use base names only for display or grouping when in replace mode.
- Replace fast map layer parameter targeting by slot-only map layer index with a stable target key:
  - slot id
  - articulation-slot filter
  - library kit/instrument/articulation
  - map layer occurrence index within that exact articulation group
- After library selection or knob changes, generated map text and the live instrument-player graph must stay equivalent.

## Stage 3: Preview Routing

- Prefer useful mic choices for library preview:
  - overhead stereo mics first, especially `ons`
  - direct mics for the selected instrument/articulation when available
  - avoid an arbitrary first sample or channel
- Preserve stereo when the selected preview sample is stereo.
- Longer-term preview should route overhead preview through overhead output channels when the active mic layout has them.
- Longer-term preview should route direct preview through compatible direct outputs for the target slot where available.
- Library preview must not change the active instrument map.

## Tests And Verification

- Add focused routing tests:
  - `hihat1` note `42` / `bow[tight]` does not resolve to `open` when CC values are default/zero.
  - `pdl_opn_cls` changes openness for bow and edge notes independently.
  - `zon_bow_edg` preserves openness while changing bow/edge.
  - simple `tom1 center/rim` mapping with explicit rim map layers does not inherit center fallback.
  - empty-art fallback still works for the original `stax1` simple-slot case.
- Add instrument-map/UI model tests where possible:
  - whole-instrument replacement creates one explicit map layer for each library articulation.
  - whole-instrument replacement uses a matching articulation-slot code when one is available.
  - runtime routing does not silently substitute another library articulation unless the map text explicitly requests it.
  - assign-articulation generation removes/replaces only the selected articulation group.
  - knob updates target the same map layer that map regeneration would target.
- Run:
  - `cmake --build T:/source/repos/karatala/build --config Debug --target Tests -j 8`
  - `Tests.exe "[InstrumentMap]"`
  - `Tests.exe "[ExpressionPoints]"`
  - `Tests.exe "[HiHatTransition]"`
  - `Tests.exe "[ControllerPath]"`
  - full `Tests.exe`
- Manual checks:
  - the reported raw map makes `hihat1` note `42` play tight, not open.
  - the first selected `tom1/rim` map layer plays immediately.
  - `tom1/rim` tuning, attack, and decay affect the audible map layer.
  - applying the same raw map after popup edits does not change routing behavior.
  - library preview is useful stereo output and is not left-channel-only for normal stereo samples.

## Assumptions

- Implementation starts with Stage 1 before touching popup redesign, because current routing bugs can invalidate UI conclusions.
- `zon_lft_rgt` remains out of scope for implicit expression-point routing.
- Temporary routing diagnostics are acceptable during the debugging pass, but should not remain noisy by default.
- Whole-instrument replacement follows the library instrument's available articulations. Runtime does not add articulation compatibility.
