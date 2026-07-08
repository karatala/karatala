# Instrument Map Surgical Apply Plan

## Problem

Instrument map changes are too broad today. A small UI change can make the engine parse the full map, rebuild more synths than needed, and sometimes change routing in a way that fights explicit articulation choices.

Original observed facts:

- Older `InstrumentConfigPanel::applyToEngine()` code generated full map text and called `DrumKitEngine::useInstrumentMap()` for source changes.
- Knob changes already have a faster path through `DrumKitEngine::updateSlotParameters()`, but the saved map text is still rebuilt later.
- `DrumKitEngine::seedInstrumentMapFingerprints()` stores map-entry fingerprints only, while `useInstrumentMap()` later compares fingerprints that also include source instrument content fingerprints.
- Older `useInstrumentMap()` code used one `rebuildAllShadows` flag. If any shadow synth existed, or any slot had multiple entries, all old shadow synths were removed and all prepared shadows were rebuilt.
- Explicit instrument map articulation filters are converted into `allowedArticulationNameIds`, `ArticulationNameRemap`, `ArticulationAliasMap`, and shadow synth note routes. `InstrumentSynth` then writes `noteToArticulation` from layout notes during `applyResolvedArticulationBinding()`. These two layers can disagree.
- Hihat is partly explicit and partly controller-driven. The destination note/articulation is explicit, for example `hihat1/bow[tight]` or `hihat1/bow[open]`, but `pdl_opn_cls` can still choose `open`, `half`, `close`, or `tight` source variants at runtime from CC value.

## Goal

Make instrument-map editing behave like this:

- Changing one slot should reload only that slot.
- Changing one layer in one slot should reload only that layer or slot, not all layered instruments.
- Changing only layer parameters should not reload samples.
- Explicit map text should be the source of truth for what source articulation is played by each destination articulation.
- Runtime routing should use one resolved routing plan, not separate partial decisions in `DrumKitEngine` and `InstrumentSynth`.
- Hihat routing should keep explicit destination zones and controller expression state separate. Example: MIDI note 42 can mean destination `bow[tight]`, MIDI note 46 can mean destination `bow[open]`, and CC4 can still select the best `pdl_opn_cls` source expression inside that zone.

## Non-Goals

- Do not redesign the UI.
- Do not change the raw instrument-map format unless a small backward-compatible field is clearly needed.
- Do not change hihat transition sound policy.
- Do not add broad caching or background rebuild systems before the simple diff path is fixed.

## Success Criteria

- If `tom1/rim` source changes, unrelated slots keep their existing primary synths.
- If `tom1/rim` source changes, shadows for unrelated slots are not destroyed and recreated.
- If tuning, attack, decay, gain, pan, flip, phase, or bleed changes, samples are not reloaded.
- If map text says `tom1/rim:SomeKit/tom-X/center`, the rim note routes to the chosen `center` source articulation because the map asked for it.
- If map text has an explicit source articulation, `InstrumentSynth` does not silently replace it with a different fallback unless the map generator wrote that fallback into the map text.
- Existing simple empty-art fallback still works for non-controller slots.
- Controller-driven hihat routing still keeps expression siblings for open, close, tight, and similar states.
- Hihat map entries can stay explicit while still loading richer library states, for example `edge[open]`, `edge[half]`, `edge[close]`, `edge[tight]`, and source folders with exact `pdl_opn_cls` values.

## Stage 1 - Make Fingerprints Correct

Issue:

- `seedInstrumentMapFingerprints()` uses `buildSlotFingerprintMap()` only.
- `useInstrumentMap()` adds `source\n` plus `InstrumentSpec::getContentFingerprint()` before comparing.
- This mismatch can make the first apply after UI populate look like all slots changed.

Plan:

1. Add one helper that builds the final slot fingerprint from parsed map plus resolved source instruments.
2. Use that helper in both `seedInstrumentMapFingerprints()` and `useInstrumentMap()`.
3. Include source content fingerprints in stable entry order.
4. Keep unresolved entries in the fingerprint by their text identity, so errors do not accidentally reuse stale synths.

Verify:

- Add or update a focused test where seeding a normalized map and applying the same map returns unchanged.
- Check that changing one source instrument changes only that slot fingerprint.

Implementation status:

- Implemented shared resolved slot fingerprints in `../source/Engine/DrumKitEngineMapHelpers.*`.
- `mapDefaultKit()`, `seedInstrumentMapFingerprints()`, and `useInstrumentMap()` now use matching full fingerprints.

## Stage 2 - Split Full Apply Into a Diff Plan

Current flow:

- `useInstrumentMap()` parses the full map.
- It groups entries by slot.
- It decides `slotChanged`.
- It creates primary replacements and shadow replacements in the same large function.

Better flow:

1. Parse and normalize map text.
2. Resolve map entries into `ResolvedSlotMap` objects.
3. Compare each `ResolvedSlotMap` with the last applied slot snapshot.
4. Build an `InstrumentMapApplyPlan` with:
   - unchanged slots
   - slots needing parameter-only update
   - slots needing primary reload
   - slots needing shadow add, shadow remove, or shadow reload
   - note-route changes
5. Apply the plan with a short graph lock.

Keep it simple:

- The first implementation can reload a whole changed slot.
- It must not reload unchanged slots.
- Fine per-layer reload can come after per-slot diff is stable.

Verify:

- A one-slot source change reports one changed slot.
- A one-slot layer add reports one changed slot.
- A parameter-only change reports no sample reload.

Implementation status:

- Started. The engine now keeps separate full-state and reload-only fingerprints.
- Simple primary-slot parameter-only raw-map changes update the existing synth instead of loading samples.
- Failed slot/source-art resolution now keeps the old slot route alive and preserves that slot fingerprint.
- Multi-layer/shadow parameter-only changes are still limited by the current global shadow rebuild and remain Stage 3 work.

## Stage 3 - Replace Global `rebuildAllShadows`

Issue:

- `rebuildAllShadows` is a global bool.
- If true, all shadows after `primaryInstrumentSynthCount` are removed.
- This is simple but too expensive.

Plan:

1. Track shadow synth ownership by slot id.
2. Store enough identity per shadow to compare:
   - slot id
   - destination articulation filter
   - source kit
   - source instrument
   - source articulation filter
   - parameter values
   - source content fingerprint
3. On apply, remove shadows only for changed slots.
4. Keep unchanged shadows in `instrumentSynthPtrs`.
5. Rebuild note routes only for changed slots plus any globally affected controller maps.

Possible simple data shape:

```cpp
struct AppliedShadowRoute {
    juce::String slotId;
    juce::String fingerprint;
    InstrumentSynth* synth = nullptr;
};
```

This can later become stronger, but the first goal is to avoid destroying unrelated shadows.

Verify:

- A map with layered `snare1` and layered `tom1`, then changing `tom1`, keeps `snare1` shadows alive.
- `instrumentSynthsByNote` still includes both primary and shadow synth routes after a partial shadow apply.

Implementation status:

- `useInstrumentMap()` now tracks which slots already have shadow synths.
- Changed slots remove and rebuild only their own old shadows.
- Unchanged shadow slots stay in `instrumentSynthPtrs` and keep their routes.
- If a primary synth is replaced, primary routing is rebuilt and kept shadow routes are restored.
- Shadow layers now keep per-synth state and reload fingerprints.
- Parameter-only changes on a matching shadow layer update the existing shadow synth instead of reloading samples.
- Still open: source or articulation changes still rebuild the affected slot shadow as a whole. This is intentional until stronger per-layer route ownership exists.

## Stage 4 - Make `applyToEngine()` Surgical

Original issue:

- Source picker changes in `InstrumentConfigPanel` called `applyToEngine()`.
- `applyToEngine()` generated full map text and called `useInstrumentMap()` directly.

Plan:

1. Keep full map text generation for persistence and raw map sync.
2. Add an engine API for one UI edit, for example:

```cpp
bool DrumKitEngine::applyInstrumentMapSlotChange(const juce::String& fullMapText, const juce::String& changedSlotId);
```

3. `InstrumentConfigPanel` calls the slot-change API when only one row source changed.
4. Add a separate API for add or remove layer in one slot or slot/art group.
5. Keep `useInstrumentMap()` for raw map apply, state load, missing-map fixer confirm, and full reset.

Important:

- The engine API should still parse full map text so engine state and saved text cannot drift apart.
- The changed slot id is only a hint that lets the engine skip work after it validates the diff.
- If validation says another slot changed too, the engine should apply both changed slots.

Verify:

- Changing one picker row updates playback and status.
- Raw map apply still supports full rebuild.
- Saved text after a surgical apply is the same as normalized full-map text.

Implementation status:

- Added `DrumKitEngine::applyInstrumentMapSlotChange(fullMapText, changedSlotId)`.
- `useInstrumentMap()` remains the raw/state/full-map API.
- Slot-change applies still parse the full map, but unchanged non-hinted slots can skip resolve/prep work after fingerprint validation.
- `InstrumentConfigPanel` source changes and remove-layer actions pass the affected slot id.
- `PadContextPopup` full apply and replace-instrument actions pass the affected slot id.
- Still open: add/remove layer can use a more explicit slot/art API later, but it now reaches the engine through the slot-hint path.

## Stage 5 - Define One Routing Source Of Truth

Issue:

- `DrumKitEngine::useInstrumentMap()` decides primary, shadow, remap, aliases, and allowed articulation names.
- `InstrumentSynth::collectResolvedLayoutArticulations()` resolves specs again from layout articulations.
- `InstrumentSynth::applyResolvedArticulationBinding()` writes `noteToArticulation` from layout notes.
- Explicit map choices can get mixed with layout fallback and source fallback.

Plan:

1. Add a small routing-plan type that says exactly what each destination articulation should do.
2. Build this plan in the engine from the map text.
3. Pass this plan into `InstrumentSynth`.
4. Make `InstrumentSynth` load samples and build runtime tables from the plan, not from a second interpretation of the map.

Example plan idea:

```cpp
struct ResolvedArticulationRoute {
    uint32_t destinationArtNameId = 0;
    uint32_t destinationExprId = 0;
    KitLib::InstrumentSpec* sourceInstrument = nullptr;
    KitLib::ArticulationSpec* sourceArticulation = nullptr;
    std::vector<uint8_t> midiNotes;
    bool isExplicitMapRoute = false;
    bool isEmptyArtAlias = false;
};
```

Rules:

- Explicit map route wins.
- UI compatibility fallback is allowed only when generating map text.
- Runtime compatibility fallback is not allowed unless the map text already names that chosen source articulation.
- Empty-art alias is allowed only for non-controller slots and only when the destination articulation has no explicit route.
- Controller sibling loading is allowed only inside the same explicit mapped source articulation family.
- For hihat, explicit destination art and controller expression are separate decisions. The map decides destination family, for example `bow` or `edge`. The controller decides expression state, for example `open`, `half`, `close`, or `tight`.

Verify:

- `tom1/rim:SomeKit/tom-X/center` plays `center` source samples on rim notes.
- If `tom1/rim` is explicitly assigned, empty-art fallback does not override it.
- Hihat `edge[open]`, `edge[close]`, and similar routes still use controller selection.

Implementation status:

- Started. `InstrumentSynth` now stores explicit destination-route ownership as `(destinationArtNameId, destinationExprId)` pairs.
- Engine-created primary and shadow synths now get an exact list of pad articulations they are allowed to play.
- When routing a MIDI note, the engine now asks the synth from that exact list first. Old name guessing is only fallback for older synths that do not have the list.
- When a synth is replaced after a layout change, the new synth copies the same allowed-to-play list from the old synth.
- This reduces shadow conflicts where a base-name match such as `bow` could catch `bow[open]` and `bow[tight]` together.
- Fast parameter-update matching now uses destination-route ownership when available, with old note-route matching only as fallback for older synths without ownership.
- Articulation filters now use map filter matching, so `bow[open]` can stay exact while legacy `bow` can still match the bow family.
- Explicit articulation-filter/remap loads now use exact source-art resolution. Default full-instrument loads still keep old fallback behavior.
- Explicit source expressions are now carried in a small exact remap, so `center[muff]` stays `center[muff]` during source-art lookup.
- Empty-art alias fallback now carries source expression when one is explicit, so alias routes can prefer `center[muff]` over plain `center`.
- The synth routing context now keeps original articulation filter strings, so exact filters like `edge[open]` stay exact when choosing allowed layout articulations.
- `InstrumentSynth` now keeps a simple route list. Each row says: these MIDI notes for this pad articulation should play this source instrument/articulation.
- The instrument-map apply path now builds that route list before loading the synth, then passes the list into `InstrumentSynth`.
- When the engine asks "does this synth play this pad articulation?", the synth checks that route list first. Old engine-provided route keys remain only as fallback.
- Note lookup, sample loading, and hihat pedal/CC choices are now built from that route list.
- `noteToArticulation` is now just the final MIDI-note lookup table made from the route list. Empty-art fallback also goes through the same route list.
- Empty-art fallback is skipped when the map already has an explicit route for the same pad articulation. Example: explicit `rim -> center` is not overwritten by fallback.
- Extra hihat pedal states do not make a synth responsible for a pad note by themselves. Example: `bow[half]` can be available for CC selection after `bow[open]` is routed, but it does not claim the MIDI note alone.
- Stage 5 code path is complete enough: map apply now decides the simple route list before synth load. Hihat-specific cleanup remains Stage 6, and tests remain Stage 8.

## Stage 6 - Document And Preserve Hihat Expression Routing

Current hihat model:

- Layout/controller data defines `pdl_opn_cls` points. Defaults are `open=24`, `half=52`, `close=78`, `tight=85`, and `pedal=85`.
- Slot or layout expression points can override those values.
- Source folders can also carry exact values, for example `edge[half]-pdl_opn_cls049`.
- Explicit source folder values should win over layout/default expression points.
- MIDI note 42 and 46 are not enough by themselves. They identify a destination zone/expression anchor such as `bow[tight]` or `bow[open]`; CC value chooses the best available runtime expression.

Plan:

1. Treat hihat map entries as explicit destination families plus expression anchors:
   - `hihat1/bow[tight]:Kit/hat/bow[tight]`
   - `hihat1/bow[open]:Kit/hat/bow[open]`
   - `hihat1/edge[open]:Kit/hat/edge[open]`
2. When a source hihat has richer states, load controller siblings for the same family only. Example: mapping `edge[open]` may load `edge[half]`, `edge[close]`, and `edge[tight]` if they are valid `pdl_opn_cls` siblings.
3. Do not let a bow mapping silently load edge siblings, or edge mapping silently load bow siblings.
4. Do not let hihat empty-art fallback copy one zone into another zone.
5. Keep source folder exact `pdl_opn_cls` values above user expression points. Use user expression points only when source folders have no exact expression dimension value.
6. Store destination family and expression anchor in the resolved route plan, so `noteToArticulation` is only the final lookup table.

Verify:

- Note 42 routed as `bow[tight]` chooses tight/close source states by CC value, not `bow[open]`.
- Note 46 routed as `bow[open]` can choose open/half/close/tight source states by CC value.
- `edge[open]` CC selection never returns a `bow[...]` runtime articulation unless the map explicitly remaps edge to bow.
- Source exact values like `edge[half]-pdl_opn_cls049` override default `half=52`.
- Slot/user expression point overrides change CC thresholds when source has only simplified names like `edge[half]`.

## Stage 7 - Reduce Note Routing Conflicts

Remaining fragile points:

- Older shadow routing used `supportsArticulation()` and could match by runtime/source articulation name instead of destination ownership.
- Parameter-update matching now prefers destination-route ownership when present, but still has fallback note matching for older synths without ownership.
- Aliases can route destination notes to another source runtime art after bindings were already created.
- A full route-plan object is still needed before `noteToArticulation` becomes only a final lookup table.

Plan:

1. Add explicit destination route ownership to `InstrumentSynth`. Implemented for engine-created primary and shadow synths.
2. Replace `supportsArticulation()` use in shadow route setup with a route-ownership query. Implemented when destination ownership exists:

```cpp
bool InstrumentSynth::routesDestinationArticulation(uint32_t destinationArtNameId, uint32_t destinationExprId) const;
```

3. Replace note-route checks that guess from `noteToArticulation` with route-plan checks where possible.
4. Keep `noteToArticulation` as a runtime lookup table only.
5. Do not use `noteToArticulation` as proof of who owns an articulation.

Verify:

- Two synths can layer the same destination note only when the map has multiple entries for that destination.
- A synth does not get a shadow note route just because it has a source articulation with the same name.

Implementation status:

- Started through Stage 5. Destination-route ownership exists and is used by primary/shadow engine note routing when available.
- Parameter-update layer matching now uses destination-route ownership when available. It keeps old note matching only as fallback for synths without ownership.
- Explicit source-art fallback is now disabled for explicit articulation-filter/remap loads. Default full-instrument loads still allow fallback.
- Cross-art remap can now include source expression id, so destination `rim` can resolve to source `center[muff]` instead of plain `center`.
- Alias fallback now stores source name plus expression target. This keeps old fallback but makes expression-specific aliases less blind.
- Destination-route ownership for primary synths now uses exact map filters when they exist, instead of only base articulation ids.
- Implemented for the current map-apply path: "who plays this pad note", final MIDI-note lookup, and empty-art fallback now use the route list. Still open: add tests.

## Stage 8 - Tests To Add

Focused tests:

- Same map after seed does not reload slots.
- One source change reloads only one primary slot.
- One layered source change rebuilds only that slot shadows.
- Parameter-only update does not call `loadInstrumentWithSources()`.
- Explicit destination-to-source art remap wins over source fallback.
- Empty-art alias works for simple slots.
- Empty-art alias does not override explicit art route.
- Hihat controller sibling routes survive the new routing-plan path.
- Hihat explicit destination family remains stable while CC chooses expression states.
- Hihat source exact `pdl_opn_cls` values override layout/default expression points.
- Hihat user expression point overrides apply when source hihat folders do not provide exact values.

Helpful test hook:

- Add a debug-only or test-only counter for synth reloads and shadow rebuilds.
- Keep it out of audio hot paths.

## Suggested Implementation Order

1. Fix fingerprint seeding mismatch.
2. Add tests around unchanged map and one-slot source change.
3. Extract map resolving into a small resolved-slot model without changing behavior.
4. Replace global `rebuildAllShadows` with per-slot shadow rebuild.
5. Add surgical engine API for `InstrumentConfigPanel`.
6. Introduce resolved articulation route plan.
7. Preserve hihat expression routing inside the resolved route plan.
8. Move `InstrumentSynth` routing to route-plan ownership.

## Files Likely Touched

- `../source/Engine/DrumKitEngine.cpp`
- `../source/Engine/DrumKitEngine.h`
- `../source/Engine/DrumKitEngineMapHelpers.cpp`
- `../source/Engine/DrumKitEngineMapHelpers.h`
- `../source/Engine/InstrumentSynthLoader.cpp`
- `../source/Engine/InstrumentSynth.h`
- `../source/UI/InstrumentConfigPanel.cpp`
- `../source/UI/PadContextPopup.cpp`
- `../tests/TestInstrumentMap.cpp`
- `../tests/TestInstrumentSynthVoiceGrouping.cpp`

## Decisions

- Raw map apply uses the diff plan by default. A full rebuild parameter and Raw page button can be added later.
- Simple parameter-only updates should be live when they are already safe: tuning, gain, pan, attack, decay, flip, phase, and bleed. Anything that needs sample reload or expensive rebuild should wait until drag end or explicit apply.
- Slot changes should let old voices decay after synth swap. A later full rebuild or reset action may stop voices immediately.
- Missing source-art errors should keep the old slot alive. The failed new route should not replace the old route, and status text should clearly say which slot/art was not applied.

