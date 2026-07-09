# Instrument Map Format And Routing Docs Plan

## Goal

Make the instrument map format easy to understand before adding bleed routing overrides.

The current format mostly lives in tests and parser code. That makes changes risky, because it is not obvious which separator belongs to the whole parameter list and which separator belongs inside one parameter value.

## Current Facts

Source checked:

- `../source/Engine/DrumKitEngineMappingModel.cpp` parses instrument map text.
- `../source/Engine/DrumKitEngine.cpp` serializes normalized instrument map text.
- `../tests/TestInstrumentMap.cpp` has the best current examples.
- `../docs/TODO11_PAD_POPUP_LAYER_PARAMETERS_PLAN.md` documents current pad popup params.

Current line shape:

```text
slot/slotArt:kit/instrument/sourceArt[,sourceArt...][:params]
```

Current examples:

```text
kick1/center:MyKit/kick-24/center
ride1/bow:KitA/ride-20/bow:tuning=-2;attack=50;decay=300
snare1/rim:MixedKit/snare-14/sidestick:tuning=-25;attack=12;decay=450;vol=-3.5;flipLR=1;flipPhase=1;bleed=oh:0.25,room:0.5
```

Current params:

- `tuning=<cents>`
- `attack=<ms>`
- `decay=<ms>`
- `vol=<dB>`
- `pan=<-127..127>`
- `flipLR=0|1|true|false`
- `flipPhase=0|1|true|false`
- `bleed=micCode:level,micCode:level`

Current separator behavior:

- Top-level params use semicolon: `tuning=-2;attack=50;decay=300`.
- Some values use commas inside one value. Example: `bleed=oh:0.25,room:0.5`.
- No backward compatibility is required before release, so comma-as-top-level-param support should be removed.
- Parser should treat semicolon as the main top-level separator.

Clear example:

```text
kick1/center:MyKit/kick-24/center:bleed=dtm:0.5,room:0.8;tuning=-20
```

## Proposed New Format Doc

Add `docs/INSTRUMENT_MAP_FORMAT.md` as the user-facing reference.

It should include:

1. One-line grammar with simple words.
2. Examples for common cases.
3. Parameter table with type, default, and whether it is live or requires slot reload.
4. Separator rule with good and bad examples.
5. Explanation of slot art vs source art.
6. Explanation of layer order: multiple matching lines become layers.
7. Explanation of hihat examples with `bow[open]`, `bow[tight]`, `edge[open]`, and `pedal`.
8. Note that raw map apply should use diff by default, with full rebuild as an explicit button later.

## Proposed Bleed Routing Param

Add a new param:

```text
route=sourceMic:outputMic,sourceMic:outputMic
```

Recommended full example:

```text
kick1/center:MyKit/kick-24/center:bleed=dtm:0.5,room:0.8;route=dtm:room
```

Meaning:

- `bleed=dtm:0.5,room:0.8` changes levels.
- `route=dtm:room` sends source mic `dtm` to output mic `room`.

Rules:

- `sourceMic` is the sample/library mic code before routing.
- `outputMic` is the active mic-layout output code.
- Missing route means automatic routing.
- Unknown route should warn and keep automatic routing.
- `route=` changes require slot reload because sample output channels are assigned when samples are loaded.
- UI should write `;route=...` after `bleed=...` to keep the line readable.

Open naming choice:

- `route=` is short but broad.
- `micRoute=` is clearer and avoids future conflict.

Recommendation: use `micRoute=` unless you strongly prefer shorter map text.

Example with recommended name:

```text
kick1/center:MyKit/kick-24/center:bleed=dtm:0.5,room:0.8;micRoute=dtm:room
```

## Implementation Plan

### 1. Add Format Doc

Create `docs/INSTRUMENT_MAP_FORMAT.md`.

Verification:

- Compare examples against `../tests/TestInstrumentMap.cpp`.
- Keep all examples parseable by current parser, except the clearly marked future `micRoute=` examples.

### 2. Add Doxygen Comments For Public Parser APIs

Add Doxygen comments to parser declarations, not only implementation files.

Target functions:

- `../source/Engine/DrumKitEngine.h`
  - `parseConfigYaml()` - describe settings YAML keys with a short example.
  - `serializeSettingsYaml()` - say it writes the normalized settings YAML.
  - `parseInstrumentMapText()` - describe instrument map line format and params.
  - `serializeInstrumentMap()` - say it normalizes and sorts map text.
- `../source/Engine/DrumKitEngineMappingModel.h`
  - `parseInstrumentMapText()` - repeat the instrument-map grammar because this is the direct parser module.
- `../source/Engine/LayoutManager.h`
  - `LayoutTraits<MicLayout>::parseYaml()` - describe mic layout YAML with one mic example.
  - `LayoutTraits<KitLayout>::parseYaml()` - describe kit layout YAML with one instrument/articulation example.
  - `toYaml()` for each layout type - say it writes normalized YAML.
- `../source/Engine/InstrumentCatalog.h`
  - `InstrumentCatalog::parseYaml()` - describe catalog YAML keys like `family` and `directMicOuts`.
  - `parseMicInstrumentSelector()` - describe selector examples: empty, exact slot, wildcard, list, catalog family.
- `../source/Engine/KitLibConsts.h`
  - `MicSpec::parseMicCode()` - describe mic code examples like `dtm`, `dsm`, `ons`.
  - `MicSpec::parseMicCodesChain()` - describe chain examples like `dsm_ons`.
- `../source/Engine/KitLibModel.h`
  - `InstrumentSpec::parseInfo()` - describe instrument folder format examples.
  - `InstrumentSpec::parseSampleFile()` - describe sample filename parts.
  - `DrumKitSpec::parseInfo()` - describe kit folder info format.
- `../source/Config/YamlHelper.h`
  - `YamlHelper::parse()` - describe supported YAML subset: maps, lists, inline maps, inline arrays, comments.

Doxygen comment style:

```cpp
/**
 * Parses instrument map text.
 *
 * Format:
 *   slot/slotArt:kit/instrument/sourceArt[,sourceArt...][:params]
 *
 * Examples:
 *   kick1/center:MyKit/kick-24/center
 *   ride1/bow:KitA/ride-20/bow:tuning=-2;attack=50;decay=300
 *   snare1/rim:MixedKit/snare-14/sidestick:vol=-3.5;bleed=oh:0.25,room:0.5
 *
 * Top-level params use semicolon. Commas are allowed inside list values such as bleed.
 */
```

### 3. Remove Old `art=` And Comma Top-Level Support

Remove the old deprecated `art=` parameter completely.

No backward compatibility is needed because the app is not released yet.

Code todos:

- Remove `art` from `knownKeys` in `DrumKitEngineMappingModel::parseInstrumentMapText()`.
- Remove the parser branch that accepts `art=`.
- Remove warnings that tell users to migrate from `art=`.
- Remove tests that expect `art=` to work.
- Update tests so top-level params use semicolon, not comma.
- Keep comma only inside list values like `bleed=oh:0.25,room:0.5` and future `micRoute=dtm:room,dsm:oh`.
- Make serializer write semicolon between top-level params.
- Update Doxygen examples to show semicolon only.

### 4. Add Parser Tests For New Route Param

When implementing `micRoute=`, add tests before engine behavior:

- Parser reads `micRoute=dtm:room`.
- Parser reads multiple routes: `micRoute=dtm:room,dsm:oh`.
- Serializer preserves routes in stable sorted order.
- Reload fingerprint changes when only `micRoute` changes.
- Normal state fingerprint also changes.

### 5. Add Engine Behavior

Add storage:

- `InstrumentMapEntry::micRouteOverrides` as `sourceMic -> outputMic`.
- `InstrumentSynth::RoutingContext` or a small per-synth field with the same map.

Apply behavior:

- `MicRoutingBuilder` checks the route override before automatic routing.
- If override is valid, route the sample mic to that output mic.
- If override is invalid, log warning and use automatic routing.
- Route changes must be in reload fingerprint.

### 6. Add Pad Popup Behavior

Routing combobox behavior:

- On selection, update that row's `micRoute` map.
- Write map text.
- Apply the affected slot so samples get rebuilt with the new output channel.
- Refresh popup route displays after apply.

Debug logs on message thread:

- `Pad popup: slot=kick1 art=center layer=1 gain=-3.5 pan=0 bleed=dtm:0.5`
- `Pad popup route: slot=kick1 art=center layer=1 dtm -> room`
- `Instrument map apply: changed slot=kick1 reason=pad-popup route`

Engine debug logs:

- `Mic route override: slot=kick1 source=dtm output=room hostStart=8 channels=2`
- `Mic route override ignored: slot=kick1 source=dtm output=badRoom reason=output not found`

Do not add logs inside per-sample render loops.

## Verification

Docs-only stage:

- Re-read examples against parser tests.
- No build required unless code comments are added.

Doxygen-comment stage:

- Build `Tests` because headers are touched.

Route implementation stage:

- Run focused `TestInstrumentMap` parser tests.
- Run focused routing tests if added.
- Manual test in UI:
  - Change bleed level - should be live.
  - Change routing combobox - should apply/reload slot and move sound to selected output.
  - Reopen popup - selected route should persist.
  - Raw map with `micRoute=` should apply same as popup.

## Risks

- Routing is not a live scalar like gain or pan. It changes sample output channel assignment, so it needs slot reload.
- `route=` may be too broad as a future keyword. `micRoute=` is clearer.
- Existing maps with unknown params currently warn. Adding `micRoute=` must update `knownKeys`, parser, serializer, fingerprints, and docs together. Before that, remove old `art=` and comma top-level separator support so the parser matches the stricter format.