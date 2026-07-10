# Instrument Map Format And Routing Docs Plan

## Goal

Make the instrument map format easy to understand, including bleed routing overrides.

The current format mostly lives in tests and parser code. That makes changes risky, because it is not obvious which separator belongs to the whole parameter list and which separator belongs inside one parameter value.

## Current Facts

Source checked:

- `../source/Engine/DrumKitEngineMappingModel.cpp` parses instrument map text.
- `../source/Engine/DrumKitEngine.cpp` serializes normalized instrument map text.
- `../tests/TestInstrumentMap.cpp` has the best current examples.
- `../docs/TODO11_PAD_POPUP_LAYER_PARAMETERS_PLAN.md` documents current pad popup params.

Current line shape:

```text
slot/slotArt:kit/instrument/sourceArt[:params]
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

## Bleed Routing Param

Implemented param:

```text
micRoute=sourceMic:outputMic,sourceMic:outputMic //FIXME: 'micRoute' > 'send'
```

Example:

```text
kick1/center:MyKit/kick-24/center:bleed=dtm:0.5,room:0.8;micRoute=dtm:room
```

Meaning:

- `bleed=dtm:0.5,room:0.8` changes levels.
- `micRoute=dtm:room` sends source mic `dtm` to output mic `room`.

Rules:

- `sourceMic` is the sample/library mic code before routing.
- `outputMic` is the active mic-layout output code.
- Missing `micRoute` means automatic routing.
- Unknown output mic warns and keeps automatic routing.
- `micRoute` changes require slot reload because sample output channels are assigned when samples are loaded.
- UI writes `;micRoute=...` after `bleed=...` when both are present.

## Implementation Plan

### 1. Add Format Doc

Create `docs/INSTRUMENT_MAP_FORMAT.md`.

Verification:

- Compare examples against `../tests/TestInstrumentMap.cpp`.
- Keep all examples parseable by current parser.

### 2. Add Doxygen Comments For Public Parser APIs

Status: implemented.

Added Doxygen comments to parser declarations, not only implementation files.

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
 * Line format:
 *   slot/slotArt:kit/instrument/sourceArt[:param=value;param=value]
 *
 * Examples:
 *   kick1/center:MyKit/kick-24/center
 *   ride1/bow:KitA/ride-20/bow:tuning=-2;attack=50;decay=300
 *   snare1/rim:MixedKit/snare-14/sidestick:vol=-3.5;bleed=oh:0.25,room:0.5;micRoute=oh:room
 *
 * Top-level params use semicolon. Commas stay inside list values like bleed and micRoute.
 */
```

### 3. Remove Old Source-Art Param And Comma Top-Level Support

Status: implemented for parser, serializer, UI map writers, tests, and parser docs.

No backward compatibility is needed because the app is not released yet.

Implemented rules:

- Source art belongs in the target path: `slot/slotArt:kit/instrument/sourceArt`.
- Top-level params use semicolon, not comma.
- Comma stays only inside list values like `bleed=oh:0.25,room:0.5` and `micRoute=dtm:room,dsm:oh`.
- Unknown params still warn through the normal unknown-param path.
- Serializer and UI map writers write semicolon between top-level params.
- Doxygen examples show semicolon only.

### 4. Add Parser Tests For New Route Param

Status: implemented.

Covered behavior:

- Parser reads `micRoute=dtm:room` and multiple routes like `micRoute=dtm:room,dsm:oh`.
- Serializer preserves routes in stable sorted order.
- Reload fingerprint changes when only `micRoute` changes.
- Normal state fingerprint also changes.
- Routing plan resolves a source mic to an explicit output mic code.

### 5. Add Engine Behavior

Status: implemented.

Implemented behavior:

- `InstrumentMapEntry::micRouteOverrides` stores `sourceMic -> outputMic`.
- `InstrumentSynth::RoutingContext` carries route overrides during load/reload.
- `MicRoutingBuilder` checks route override before automatic routing.
- Valid override routes the sample mic to that output mic.
- Invalid override logs a warning and falls back to automatic routing.
- Route changes are included in reload fingerprint.

### 6. Add Pad Popup Behavior

Status: implemented.

Routing combobox behavior:

- On selection, the row stores a `micRoute` override.
- Popup map text writes `micRoute=sourceMic:outputMic`.
- Route combobox changes apply the affected slot so samples rebuild with the new output channel.
- Bleed knobs remain live scalar changes.

Debug logs on message thread:

- `Pad popup: slot=kick1 art=center layer=1 source=MyKit/kick-24/center gain=-3.5 pan=0 bleed=[dtm:0.500]`
- `Pad popup route: slot=kick1 art=center layer=1 sourceMic=dtm old=auto new=room`
- `Instrument map apply: changed slot=kick1 art=center reason=pad-popup route`

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
- Routing is implemented as `micRoute=` because `route=` was too broad.
- Existing maps with unknown params currently warn.