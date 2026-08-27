# Plugin Instance State Format

## Purpose

This document describes the state that Karatala saves for one plugin instance in a DAW project. The standalone wrapper uses the same JUCE processor-state callbacks.

This is the current implementation contract. It is not a plan for future host automation.

## Container Format

`PluginProcessor` owns one `juce::AudioProcessorValueTreeState`. Its root type is `plugin_params`.

`PluginProcessor::getStateInformation()` copies the complete state, converts it to XML, and passes it to `copyXmlToBinary()`. The plugin host therefore receives a JUCE binary block that contains an XML representation of the `ValueTree`.

The decoded XML has this logical shape:

```xml
<plugin_params kit_layout="..." mic_layout="..." instrument_map="..." instrument_map_preset_ref="..." velocity_response="..." sample_loading_mode="..."/>
```

XML escaping is applied to property values. Consumers must use JUCE state decoding instead of parsing the host chunk as plain XML.

`PluginProcessor::setStateInformation()` performs these steps:

1. Decode the JUCE binary block to XML.
2. Convert the XML to a `ValueTree`.
3. Replace the current `AudioProcessorValueTreeState` state.
4. Call `DrumKitEngine::useState()` to apply the restored values.

The state has no schema-version property. The APVTS parameter layout is currently empty, so the state contains no host-automatable parameter values.

## Current Properties

| Property | Stored value | Initial value | Current restore behavior |
|---|---|---|---|
| `kit_layout` | Kit-layout ID string | Empty string | `DrumKitEngine::useState()` reads it. If it is empty or invalid, the engine keeps an active layout or uses the machine default. Current layout changes do not write the selected ID back to this property. |
| `mic_layout` | Mic-layout ID string | Empty string | The property is included in the saved state, but `DrumKitEngine::useState()` currently ignores it and resolves the mic layout from `config.yaml`. Current layout changes do not write the selected ID back to this property. |
| `instrument_map` | Normalized multiline instrument-map text | Empty string | The engine reads and applies it. If it is invalid, the engine tries the default map. An empty value does not create a default map during state restoration. |
| `instrument_map_preset_ref` | `factory:<filename>` or `user:<filename>` | Empty string | `KitPage` resolves it against the matching preset directory. A valid reference identifies the active preset. An empty or missing reference means that the working map is not linked to a preset. |
| `velocity_response` | Built-in response code or serialized response points | Machine default from `config.yaml` | The engine parses and applies it. An invalid value falls back to the machine default, then to built-in linear response if the machine default is also invalid. |
| `sample_loading_mode` | `global`, `adaptive`, or `full` | `global` | The engine resolves it before applying the restored instrument map. `global` uses the machine default from `config.yaml`. An empty or invalid value uses the machine default. Adaptive initially submits only the configured minimal round robins. Full also submits every remaining logical hit. A changed mode applies on the next kit, map, or instrument reload. |

The complete `instrument_map` syntax is documented in [INSTRUMENT_MAP_FORMAT.md](INSTRUMENT_MAP_FORMAT.md).

Valid `velocity_response` values are:

- `linear`
- `logarithmic`
- `exponential`
- A comma-separated list of increasing `MIDI velocity:dB` points, for example `1:-48,64:-12,127:0`

A point definition must start at velocity 1, end at velocity 127, and end at `0 dB`.

## Property Writers

`instrument_map` is updated when the Raw map is applied, a kit or instrument-map preset is loaded, or an instrument-map editor changes the map.

`instrument_map_preset_ref` is set when an instrument-map preset is loaded or saved as a new preset. Loading a library kit or deleting the active preset clears it. Normal map edits keep the reference so the UI can show the active preset as modified.

`velocity_response` is updated through `PluginProcessor::setVelocityResponseDefinition()`.

`sample_loading_mode` is updated by the Conf page `This project` selector through `PluginProcessor::setSampleLoadingModeOverride()`.

`kit_layout` and `mic_layout` are initialized by `PluginProcessor`, but the current layout-selection paths do not synchronize later changes to these properties.

## Data Not Stored

The current state does not store:

- A baseline used to compare the working map with its source preset.
- Kit-page list selection or scroll positions.
- Temporary linked-edit switches from the pad popup.
- Machine settings such as the sample-library path, default layouts, default sample-loading mode, and initial round-robin count from `config.yaml`.

`KitPage` compares the current normalized `instrument_map` with the referenced preset file. A difference marks the active preset as modified. If there is no reference and the current map does not exactly match a preset, the preset list shows `Current settings*`.

## Rules For New Properties

When a new per-instance property is added:

1. Use a stable property name and a portable serialized value. Do not store an absolute machine path when a stable ID can identify the same resource.
2. Define its initial value before engine state is first applied.
3. Update the property from every operation that changes the owned value.
4. Read and validate it during state restoration.
5. Document its format, fallback behavior, and owner in this file.
6. Add a schema-version property before a format change requires migration rules.

## Implementation Owners

- [`source/PluginProcessor.cpp`](../source/PluginProcessor.cpp) owns state creation and binary save/restore.
- [`source/Engine/DrumKitEngineController.cpp`](../source/Engine/DrumKitEngineController.cpp) applies engine-owned state.
- [`source/PluginEditor.cpp`](../source/PluginEditor.cpp) updates the current `instrument_map` from top-level editor actions.
- [`source/PluginEditorPresetsAndPopups.cpp`](../source/PluginEditorPresetsAndPopups.cpp) updates the current `instrument_map` from editor preset and popup actions.
