# Project Terms

Status: approved. ✅ means that the name is applied. ⏳ means that the change is deferred.

## Library data

| Term | Meaning |
| --- | --- |
| Library | All recorded drum kits that the application finds on disk. |
| Library kit | One recorded drum kit. C++ type: `KitLib::DrumKitSpec`. |
| Library instrument | One recorded instrument in a library kit. C++ type: `KitLib::InstrumentSpec`. |
| Library articulation | One recorded playing method, such as center, rim, bow, or edge. C++ type: `KitLib::ArticulationSpec`. |
| Velocity level | One recorded velocity group in a library articulation. |
| Logical hit | One recorded hit at one velocity level. It has one round robin ID and one sample for each mic. |
| Round robin ID | The letter that identifies an alternate logical hit at the same velocity level. |
| Sample | Data for one mic in one logical hit. It identifies a sample file and a channel range. |
| Sample file | One audio file on disk. One experimental multichannel file can contain samples for several mics. |
| Mic | One recorded microphone signal, such as direct, overhead, or room. |

The sample model has this structure:

```text
VelocityLevelSpec
  logicalHits: LogicalHitSpec[]

LogicalHitSpec
  roundRobinId
  samples: SampleSpec[]
```

## Kit and mapping

| Term | Meaning |
| --- | --- |
| Kit layout | The user-defined e-drum kit. It defines instrument slots, articulation slots, MIDI notes, and controllers. |
| Instrument slot | One instrument in a kit layout, such as `kick1`, `tom2`, or `crash1`. |
| Articulation slot | One playing method in an instrument slot, such as center, rim, bow, or edge. |
| Instrument map | Assigns library instruments and library articulations to kit layout slots. |
| Map layer | One assignment in an instrument map. It stores one library articulation, its target slot, and its playback settings. |
| Mic layout | The plugin outputs, their order, channel count, and compatible mic rules. |
| Mic route | One assignment from a mic to a mic layout output. |
| Library kit override | Saved changes to balance, velocity ranges, sample gain, or playback timing in one library kit. |

A map layer must store a required articulation-slot target and a complete `kit/instrument/articulation` library path. A whole-instrument selection uses an exact articulation-slot match first. When no exact match exists, it uses the configured fallback order for the instrument type and shows a warning.

Use `articulation slot` for the kit layout target. Use `library articulation` for recorded content. Do not use `source articulation` or `destination articulation` in new names.

## UI and runtime

| Term | Meaning |
| --- | --- |
| Page | Main content selected from the sidebar. |
| Section | A content choice inside a page, such as Library Kits or Presets. |
| Panel | A UI component that implements a page or part of a page. It is not a navigation term. |
| Mode | A behavior state, such as `MicPlacementMode`. It is not a sidebar destination. |
| Library preview | Audition of library content that does not change the instrument map. |
| Map preview | Audition of the current instrument map and its playback settings. |
| Engine | The top-level runtime object. It loads settings and samples, applies maps, and coordinates audio processing. |
| Instrument player | The runtime object that prepares and plays samples for one instrument slot. |
| Articulation playback entry | The runtime assignment from one articulation slot to one library articulation. |
| Voice | One active sample playback. A logical hit normally starts one voice for each sample. |

Use the full names `kit layout preset`, `instrument map preset`, and `velocity response preset`. A mic layout is not a preset.

## C++ rename status

| Old name | Approved name | Status |
| --- | --- | --- |
| No model type for one logical hit | `LogicalHitSpec` | ✅ |
| `SampleVariantSpec` | `SampleSpec` | ✅ |
| `VelocityLayerSpec` | `VelocityLevelSpec` | ✅ |
| `VelocityLayerRuntime` | `VelocityLevelRuntime` | ✅ |
| `VelLayerIdx` | `VelocityLevelIdx` | ✅ |
| `variantIdx` for a logical hit | `roundRobinId` | ✅ |
| `InstrumentMapEntry` | `InstrumentMapLayer` | ✅ |
| `InstrumentMap::entries` | `InstrumentMap::layers` | ✅ |
| `LayerRow` | `MapLayerRow` | ✅ |
| `ArticulationTrigger` | `ArticulationSlot` | ✅ |
| `destinationArticulation` string | `articulationSlotCode` | ✅ |
| `InstrumentSynth` | `InstrumentPlayer` | ✅ |
| `InstrumentSynthVoice` | `InstrumentPlayerVoice` | ✅ |
| `ResolvedArticulationRoute` | `ArticulationPlaybackEntry` | ✅ |
| `KitSourceOverrides` | `LibraryKitOverrides` | ✅ |
| `ViewMode` | `PageId` | ✅ |
| `activeMode` | `activePageId` | ✅ |
| `ModeButton` | `PageButton` | ✅ |
| `SourceMode` in `KitBrowserComponent` | `KitSection` | ✅ |
| `SourceArticulationOverrideEditor` | `LibraryArticulationEditor` | ✅ |
| `KitBrowserComponent` | `KitPage` | ⏳ Do this after the old Kit page is removed. |
| `PageId::Browse` | `PageId::Kit` | ⏳ Do this after the old Kit page is removed. |
| `SourceItem` and `SourcePickerPopup` | Not selected | ⏳ Keep these names until a clear replacement is approved. |

## Use of `source`

Do not use `source` as a general project-domain term. Use the name that states the meaning:

| Old context | Approved name |
| --- | --- |
| `sourceInstrument`, `sourceArticulation` | `libraryInstrument`, `libraryArticulation` |
| `sourceVelocityLayer` | `velocityLevelSpec` |
| `sourceMicCode` | `micCode` |
| `sourceBalance` | `placementBalance` |
| `sourceBalanceResolved` | `balanceResolved` |
| A `sourcePath` that identifies an audio file | `sampleFilePath` |

The phrase `source code` remains valid. `SourceItem`, `SourcePickerPopup`, and their related library-selection path and member names are temporary exceptions until replacement names are approved.

Real audio direction terms remain valid when they refer to a source or destination buffer, channel, voice, playback position, or preprocessing recipe.

## Files and helpers

Do not create `source/UI/Source/` or a general `helpers` folder. Keep a one-use helper in its owning `.cpp` file. Put shared code in the smallest feature that owns its rules.

Use a feature namespace, such as `InstrumentMap`. Use `<Feature>Detail` for private helpers in one `.cpp` file. Do not split a file only because it is long.

| Before | Approved direction |
| --- | --- |
| ✅ `DrumPadHelpers.h` was used only by `DrumPad.cpp`. | Its code is now private in `DrumPad.cpp` under `DrumPadDetail`; the helper header was deleted. |
| ✅ Instrument map types and text operations were spread across engine files. | `source/Engine/InstrumentMap/InstrumentMap.h` and `InstrumentMap.cpp` now own the map data and text boundary. |
| `UIHelpers` contains styling, library list, and MIDI operations. | Move each operation to its owning UI feature when related code changes. |
| The old Kit page and the Browse page repeat preset logic. | Move useful behavior to Browse, remove the old Kit page, then rename Browse to Kit. |
