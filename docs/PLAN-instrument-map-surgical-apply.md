# ✅ COMPLETED: Instrument Map Surgical Apply Plan

## Problem

Instrument map changes are too broad today. A small UI change can make the engine parse the full map, rebuild more instrument players than needed, and sometimes change routing in a way that fights explicit articulation choices.

Original observed facts:

- Older `InstrumentConfigPanel::applyToEngine()` code generated full map text and called `DrumKitEngine::useInstrumentMap()` for library-assignment changes.
- Knob changes already have a faster path through `DrumKitEngine::updateSlotParameters()`, but the saved map text is still rebuilt later.
- `DrumKitEngine::seedInstrumentMapFingerprints()` stores map-layer fingerprints only, while `useInstrumentMap()` later compares fingerprints that also include library instrument content fingerprints.
- Older `useInstrumentMap()` code used one global additional-player rebuild flag. If any additional instrument player existed, or any instrument slot had multiple map layers, all additional instrument players were rebuilt.
- Explicit instrument map articulation-slot filters are converted into `allowedArticulationNameIds`, `ArticulationNameRemap`, `ArticulationAliasMap`, and additional instrument player note routes. `InstrumentPlayer` then writes `noteToArticulation` while it applies each `ArticulationPlaybackEntry` in `applyArticulationPlaybackEntry()`. These two routing paths can disagree.
- Hihat is partly explicit and partly controller-driven. The articulation-slot note is explicit, for example `hihat1/bow[tight]` or `hihat1/bow[open]`, but `pdl_opn_cls` can still choose `open`, `half`, `close`, or `tight` library-articulation states from the CC value.

## Goal

Make instrument-map editing behave like this:

- Changing one slot should reload only that slot.
- Changing one map layer in one instrument slot should reload only that map layer or instrument slot, not all layered instruments.
- Changing only map layer parameters should not reload samples.
- Explicit map text is the authoritative record of the library articulation played by each articulation slot.
- Runtime routing should use one resolved routing plan, not separate partial decisions in `DrumKitEngine` and `InstrumentPlayer`.
- Hihat routing must keep explicit articulation-slot zones and controller expression state separate. For example, MIDI note 42 can mean articulation slot `bow[tight]`, MIDI note 46 can mean articulation slot `bow[open]`, and CC4 can select the best `pdl_opn_cls` library-articulation expression in that zone.

## Non-Goals

- Do not redesign the UI.
- Do not change the raw instrument-map format unless a small backward-compatible field is clearly needed.
- Do not change hihat transition sound policy.
- Do not add broad caching or background rebuild systems before the simple diff path is fixed.

## Success Criteria

- If the `tom1/rim` library assignment changes, unrelated instrument slots keep their instrument players.
- If tuning, attack, decay, gain, pan, flip, phase, or bleed changes, samples are not reloaded.
- If map text says `tom1/rim:SomeKit/tom-X/center`, the rim note routes to the chosen `center` library articulation because the map asked for it.
- If map text has an explicit library articulation, `InstrumentPlayer` does not silently replace it with a different fallback unless the map generator wrote that fallback into the map text.
- The former simple empty-art fallback is removed. Generated maps use explicit articulation-slot targets.
- Controller-driven hihat routing still keeps expression siblings for open, close, tight, and similar states.
- Hihat map layers can stay explicit while loading more library-articulation states, for example `edge[open]`, `edge[half]`, `edge[close]`, `edge[tight]`, and library-articulation folders with exact `pdl_opn_cls` values.

## Stage 1 - Make Fingerprints Correct

Issue:

- `seedInstrumentMapFingerprints()` uses `buildSlotFingerprintMap()` only.
- `useInstrumentMap()` adds a library-content marker plus `InstrumentSpec::getContentFingerprint()` before comparing.
- This mismatch can make the first apply after UI populate look like all slots changed.

Plan:

1. Add one helper that builds the final slot fingerprint from parsed map plus resolved library instruments.
2. Use that helper in both `seedInstrumentMapFingerprints()` and `useInstrumentMap()`.
3. Include library-content fingerprints in stable map-layer order.
4. Keep unresolved map layers in the fingerprint by their text identity, so errors do not accidentally reuse stale instrument players.

Verify:

- Add or update a focused test where seeding a normalized map and applying the same map returns unchanged.
- Check that changing one library instrument changes only that slot fingerprint.

Implementation status:

- Implemented shared resolved slot fingerprints in `../source/Engine/DrumKitEngineMapHelpers.*`.
- `mapDefaultKit()`, `seedInstrumentMapFingerprints()`, and `useInstrumentMap()` now use matching full fingerprints.

## Stage 2 - Split Full Apply Into a Diff Plan

Current flow:

- `useInstrumentMap()` parses the full map.
- It groups entries by slot.
- It decides `slotChanged`.
- It creates primary replacements and additional-player replacements in the same large function.

Better flow:

1. Parse and normalize map text.
2. Resolve map layers into `ResolvedSlotMap` objects.
3. Compare each `ResolvedSlotMap` with the last applied slot snapshot.
4. Build an `InstrumentMapApplyPlan` with:
   - unchanged slots
   - slots needing parameter-only update
   - slots needing primary reload
   - slots needing additional-player add, additional-player remove, or additional-player reload
   - note-route changes
5. Apply the plan with a short graph lock.

Keep it simple:

- The first implementation can reload a whole changed slot.
- It must not reload unchanged slots.
- Fine per-map-layer reload can come after the per-instrument-slot diff is stable.

Verify:

- One library-assignment change reports one changed instrument slot.
- A one-instrument-slot map-layer add reports one changed instrument slot.
- A parameter-only change reports no sample reload.

Implementation status:

- Started. The engine now keeps separate full-state and reload-only fingerprints.
- Simple primary-slot parameter-only raw-map changes update the existing instrument player instead of loading samples.
- Failed instrument-slot or library-articulation resolution keeps the old route alive and preserves the instrument-slot fingerprint.
- Parameter-only changes with multiple map layers and additional instrument players are still limited by the current global additional-player rebuild and remain Stage 3 work.

## Stage 3 - Replace the Global Additional-Player Rebuild

Issue:

- One global flag controls all additional-player rebuilds.
- If true, all additional instrument players after `primaryInstrumentPlayerCount` are removed.
- This is simple but too expensive.

Plan:

1. Track additional instrument player ownership by slot id.
2. Store enough identity per additional-player to compare:
   - slot id
   - articulation slot filter
   - library kit
   - library instrument
   - library articulation filter
   - parameter values
   - library-content fingerprint
3. On apply, remove additional instrument players only for changed slots.
4. Keep unchanged additional players in `instrumentPlayerPtrs`.
5. Rebuild note routes only for changed slots plus any globally affected controller maps.

Possible simple data shape:

```cpp
struct AppliedShadowRoute {
    juce::String slotId;
    juce::String fingerprint;
    InstrumentPlayer* player = nullptr;
};
```

This can later become stronger, but the first goal is to avoid destroying unrelated additional instrument players.

Verify:

- A map with layered `snare1` and layered `tom1`, then changing `tom1`, keeps `snare1` additional instrument players alive.
- `instrumentPlayersByNote` still includes both primary and additional instrument player routes after a partial apply.

Implementation status:

- `useInstrumentMap()` now tracks which slots already have additional instrument players.
- Changed slots remove and rebuild only their own old additional instrument players.
- Unchanged additional players stay in `instrumentPlayerPtrs` and keep their routes.
- If a primary instrument player is replaced, primary routing is rebuilt and kept additional-player routes are restored.
- Additional map layers now keep per-instrument-player state and reload fingerprints.
- Parameter-only changes on a matching additional map layer update the existing additional instrument player instead of reloading samples.
- Still open: a library-instrument or library-articulation change rebuilds the affected additional instrument player as a whole. This is intentional until stronger map-layer route ownership exists.

## Stage 4 - Make `applyToEngine()` Surgical

Original issue:

- Library picker changes in `InstrumentConfigPanel` called `applyToEngine()`.
- `applyToEngine()` generated full map text and called `useInstrumentMap()` directly.

Plan:

1. Keep full map text generation for persistence and raw map sync.
2. Add an engine API for one UI edit, for example:

```cpp
bool DrumKitEngine::applyInstrumentMapSlotChange(const juce::String& fullMapText, const juce::String& changedSlotId);
```

3. `InstrumentConfigPanel` calls the instrument-slot change API when only one row's library assignment changed.
4. Add a separate API to add or remove a map layer in one instrument slot or articulation-slot group.
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
- `InstrumentConfigPanel` library-assignment changes and remove-map-layer actions pass the affected instrument-slot ID.
- `PadContextPopup` full apply and replace-instrument actions pass the affected slot id.
- Still open: map layer add or remove can use a more explicit instrument-slot and articulation-slot API later, but it now reaches the engine through the slot-hint path.

## Stage 5 - Define One Authoritative Routing Plan

Issue:

- `DrumKitEngine::useInstrumentMap()` decides primary, additional-player, remap, aliases, and allowed articulation names.
- `InstrumentPlayer::collectResolvedArticulationSlots()` resolves specs again from articulation slots.
- `InstrumentPlayer::applyArticulationPlaybackEntry()` writes `noteToArticulation` from the entry's articulation-slot notes.
- Explicit map choices can get mixed with kit-layout fallback and library-articulation fallback.

Plan:

1. Add a small routing-plan type that says exactly what each articulation slot should do.
2. Build this plan in the engine from the map text.
3. Pass this plan into `InstrumentPlayer`.
4. Make `InstrumentPlayer` load samples and build runtime tables from the plan, not from a second interpretation of the map.

Example plan idea:

```cpp
struct ArticulationPlaybackEntry {
    uint32_t articulationSlotNameId = 0;
    uint32_t articulationSlotExpressionId = 0;
    KitLib::InstrumentSpec* libraryInstrument = nullptr;
    KitLib::ArticulationSpec* libraryArticulation = nullptr;
    std::vector<uint8_t> midiNotes;
    bool isExplicitMapRoute = false;
    bool isEmptyArtAlias = false;
};
```

Rules:

- Explicit map route wins.
- UI compatibility fallback is allowed only when generating map text.
- Runtime compatibility fallback is not allowed unless the map text already names that chosen library articulation.
- Empty-art aliases are removed. Every map layer names one explicit articulation slot.
- Controller sibling loading is allowed only inside the same explicit mapped library articulation family.
- For hihat, the explicit articulation slot and controller expression are separate decisions. The map decides the articulation-slot family, for example `bow` or `edge`. The controller decides the expression state, for example `open`, `half`, `close`, or `tight`.

Verify:

- `tom1/rim:SomeKit/tom-X/center` plays library articulation `center` on rim notes.
- If `tom1/rim` is explicitly assigned, generated fallback layers do not override it.
- Hihat `edge[open]`, `edge[close]`, and similar routes still use controller selection.

Implementation status:

- Started. `InstrumentPlayer` now stores explicit articulation-slot route ownership as `(articulationSlotNameId, articulationSlotExpressionId)` pairs.
- Engine-created primary and additional instrument players now get an exact list of articulation slots they are allowed to play.
- When routing a MIDI note, the engine asks the instrument player from that exact list.
- When an instrument player is replaced after a layout change, the new instrument player copies the same allowed-to-play list from the old instrument player.
- This reduces additional-player conflicts where a base-name match such as `bow` could catch `bow[open]` and `bow[tight]` together.
- Fast parameter-update matching uses articulation-slot route ownership instead of MIDI-note guesses.
- Articulation filters now use map filter matching, so `bow[open]` can stay exact while legacy `bow` can still match the bow family.
- Explicit articulation-filter and remap loads now use exact library-articulation resolution. Default full-instrument loads still keep old fallback behavior.
- Explicit library-articulation expressions are carried in a small exact remap, so `center[muff]` stays `center[muff]` during library-articulation lookup.
- Generated fallback layers carry the explicit library-articulation expression, so `center[muff]` stays distinct from plain `center`.
- The instrument player routing context keeps original articulation-slot filter strings, so exact filters like `edge[open]` stay exact when choosing allowed articulation slots.
- `InstrumentPlayer` now keeps a simple route list. Each row says: these MIDI notes for this articulation slot should play this library instrument and library articulation.
- The instrument-map apply path now builds that route list before loading the instrument player, then passes the list into `InstrumentPlayer`.
- When the engine asks whether an instrument player plays an articulation slot, the instrument player checks route ownership. Engine-provided route keys are retained when an instrument player is replaced during layout work.
- Note lookup, sample loading, and hihat pedal/CC choices are now built from that route list.
- `noteToArticulation` is now just the final MIDI-note lookup table made from explicit routes.
- Type-based whole-instrument fallback creates an explicit articulation-slot route before runtime loading. Runtime does not infer a route from an empty articulation slot.
- Extra hi-hat pedal states do not make an instrument player responsible for an articulation-slot note by themselves. Example: `bow[half]` can be available for CC selection after `bow[open]` is routed, but it does not claim the MIDI note alone.
- Map apply decides the route list before instrument player load. Hihat-specific persistence and focused coverage are completed in Stages 6-8.

## ✅ Stage 6 - Document And Preserve Hihat Expression Routing

Current hihat model:

- Layout/controller data defines `pdl_opn_cls` points. Defaults are `open=24`, `half=52`, `close=78`, `tight=85`, and `pedal=85`.
- Slot or layout expression points can override those values.
- Library-articulation folders can carry exact values, for example `edge[half]-pdl_opn_cls049`.
- Explicit library-articulation folder values must win over layout and default expression points.
- MIDI note 42 and 46 are not enough by themselves. They identify a articulation-slot zone/expression anchor such as `bow[tight]` or `bow[open]`; CC value chooses the best available runtime expression.

Plan:

1. Treat hihat map layers as explicit articulation-slot families plus expression anchors:
   - `hihat1/bow[tight]:Kit/hat/bow[tight]`
   - `hihat1/bow[open]:Kit/hat/bow[open]`
   - `hihat1/edge[open]:Kit/hat/edge[open]`
2. When a library hihat has more articulation states, load controller siblings for the same family only. For example, mapping `edge[open]` can load `edge[half]`, `edge[close]`, and `edge[tight]` if they are valid `pdl_opn_cls` siblings.
3. Do not let a bow mapping silently load edge siblings, or edge mapping silently load bow siblings.
4. Do not let configured hihat fallback copy one zone into another zone without an explicit generated route and warning.
5. Keep exact library-articulation folder `pdl_opn_cls` values above user expression points. Use user expression points only when library-articulation folders have no exact expression value.
6. Store the articulation-slot family and expression anchor in the resolved route plan, so `noteToArticulation` is only the final lookup table.

Verify:

- Note 42 routed as `bow[tight]` chooses tight or close library-articulation states by CC value, not `bow[open]`.
- Note 46 routed as `bow[open]` can choose open, half, close, or tight library-articulation states by CC value.
- `edge[open]` CC selection never returns a `bow[...]` runtime articulation unless the map explicitly remaps edge to bow.
- Exact library-articulation values such as `edge[half]-pdl_opn_cls049` override default `half=52`.
- Instrument-slot and user expression-point overrides change CC thresholds when the library has only simplified names such as `edge[half]`.

Implementation status:

- The Instrument page table preserves exact hi-hat articulation-slot rows such as `bow[open]` and `bow[tight]` instead of collapsing them to `bow`.
- Instrument and Pad editors expand a legacy base articulation slot such as `bow -> edge[close]` to matching explicit anchors such as `bow[open] -> edge[open]` and `bow[tight] -> edge[tight]` when those library-articulation states exist.
- An explicit remap is preserved. For example, `bow[open] -> edge[close]` stays unchanged when the articulation-slot filter is exact.
- Controller siblings stay inside the resolved library-articulation family for `pdl_opn_cls`. A bow-to-edge remap can load `edge[...]` siblings, but it does not load `bow[...]` siblings.
- Exact library-articulation folder expression values take priority over instrument-slot, layout, and default points.
- The bundled default preset stores explicit hihat articulation-slot and library-articulation anchors.

## ✅ Stage 7 - Preserve Incremental Apply Safety And Reduce Note Routing Conflicts

Resolved fragile points:

- Older additional-player routing used `supportsArticulation()` and could match by runtime or library articulation name instead of articulation-slot ownership.
- Older parameter-update matching could use `noteToArticulation` as fallback ownership evidence.
- Aliases can route articulation-slot notes to another library-articulation runtime after bindings are created.
- Controller siblings can contain library articulations that must not claim map-layer-row ownership.

Plan:

1. Add explicit articulation-slot route ownership to `InstrumentPlayer` for engine-created primary and additional instrument players.
2. Replace `supportsArticulation()` use in additional-player route setup with a route-ownership query:

```cpp
bool InstrumentPlayer::routesArticulationSlot(uint32_t articulationSlotNameId, uint32_t articulationSlotExpressionId) const;
```

3. Replace note-route checks that guess from `noteToArticulation` with route-plan checks.
4. Keep `noteToArticulation` as a runtime lookup table only.
5. Do not use `noteToArticulation` as proof of who owns an articulation.

Verify:

- Two instrument players can layer the same articulation-slot note only when the map has multiple map layers for that articulation slot.
- An instrument player does not get an additional-player note route only because it has a library articulation with the same name.

Implementation status:

- Primary and additional-player note routing use exact articulation-slot route ownership.
- Parameter and mic-route updates use articulation-slot route ownership. The old MIDI-note fallback is removed.
- Parameter-only changes on the primary map layer keep the loaded primary instrument player. Additional map layers still change the reload shape when their parameters make them separate instrument players.
- Library-specific parameter updates match the exact library instrument and library articulation from the route plan. Controller siblings do not claim map-layer-row ownership.
- `supportsArticulation()` and the engine ownership checks based on `noteToArticulation` are removed.
- Explicit library-articulation fallback is disabled for explicit articulation-filter and remap loads. Default full-instrument loads still allow fallback.
- Cross-articulation remap can include the library-articulation expression ID, so articulation slot `rim` can resolve to library articulation `center[muff]` instead of plain `center`.
- Alias fallback stores the library-articulation name and expression target. This keeps old fallback but makes expression-specific aliases more precise.
- Articulation-slot route ownership for primary instrument players uses exact map filters when they exist, instead of only base articulation IDs.
- `noteToArticulation` remains only the final runtime MIDI-note lookup table.

## ✅ Stage 8 - Tests Added

Focused tests:

- ✅ Same map after seed does not replace primary instrument players.
- ✅ One library-assignment change replaces only one primary instrument slot.
- ✅ One layered library-assignment change rebuilds only that instrument slot's additional instrument players.
- ✅ Parameter-only update keeps the loaded primary instrument player.
- ✅ Explicit articulation-slot-to-library-articulation remap wins over library-articulation fallback.
- ✅ Empty-art aliases were superseded by required articulation-slot targets and configurable type-based fallback during map generation.
- ✅ Hihat controller sibling routes survive the route-plan path.
- ✅ The explicit hihat articulation-slot family remains stable while CC chooses expression states.
- ✅ Exact hihat library-articulation `pdl_opn_cls` values override layout and default expression points.
- ✅ Hihat user expression-point overrides apply when library-articulation folders do not provide exact values.
- ✅ Legacy hihat library-path text is anchored to the matching articulation-slot expression instead of the first base-name library item.

Test hook:

- `DrumKitEngineTestAccess` reads primary and additional instrument player pointer identity in tests. It adds no audio-path counter or runtime work.
- Focused test execution is intentionally pending because repository instructions require an explicit request before running tests.

## Suggested Implementation Order

1. Fix fingerprint seeding mismatch.
2. Add tests for an unchanged map and one instrument-slot library-assignment change.
3. Extract map resolving into a small resolved-slot model without changing behavior.
4. Replace the global additional-player rebuild with per-instrument-slot rebuild.
5. Add surgical engine API for `InstrumentConfigPanel`.
6. Introduce resolved articulation route plan.
7. Preserve hihat expression routing inside the resolved route plan.
8. Move `InstrumentPlayer` routing to route-plan ownership.

## Files Likely Touched

- `../source/Engine/DrumKitEngine.cpp`
- `../source/Engine/DrumKitEngine.h`
- `../source/Engine/DrumKitEngineMapHelpers.cpp`
- `../source/Engine/DrumKitEngineMapHelpers.h`
- `../source/Engine/InstrumentPlayerLoader.cpp`
- `../source/Engine/InstrumentPlayer.h`
- `../source/UI/InstrumentConfigPanel.cpp`
- `../source/UI/PadContextPopup.cpp`
- `../tests/TestInstrumentMap.cpp`
- `../tests/TestInstrumentPlayerVoiceGrouping.cpp`

## Decisions

- Raw map apply uses the diff plan by default. A full rebuild parameter and Raw page button can be added later.
- Simple parameter-only updates should be live when they are already safe: tuning, gain, pan, attack, decay, flip, phase, and bleed. Anything that needs sample reload or expensive rebuild should wait until drag end or explicit apply.
- Slot changes should let old voices decay after instrument player swap. A later full rebuild or reset action may stop voices immediately.
- Missing library-articulation errors must keep the old instrument slot alive. The failed route must not replace the old route, and status text must identify the instrument slot and articulation slot that were not applied.
