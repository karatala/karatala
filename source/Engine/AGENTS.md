# Purpose

- Own realtime drum synthesis, MIDI interpretation, sample-library models, loading, voice lifecycle, routing, and engine settings.

# Ownership

- `InstrumentSynth*` owns per-instrument runtime tables, voices, note/controller handling, and rendering.
- `KitLibModel*` owns parsed library metadata, including decay-envelope metadata.
- `SampleLoader*` owns asynchronous sample loading and publication.
- `DrumKitEngine*` coordinates instrument synths, layouts, persistence, and process-block flow.
- `LayoutManager*` owns kit and mic layout models and parsing.

# Local Contracts

- Code reached from `processBlock`, `renderNextBlock`, `handleMidiEvent`, note-on, controller, or aftertouch paths is realtime code.
- Realtime settings shared with the message thread must use atomics or an immutable snapshot; do not introduce unsynchronized mutable scalar reads/writes.
- Precompute routing, articulation, velocity, variant, and envelope lookup data during load/rebuild work where practical.
- Exact source selection follows named velocity boundaries for both calibrated and uncalibrated articulations. Calibration uses the instance's precomputed 128-value loudness response and preserves each articulation's own hardest measured reference instead of forcing different instruments to one common level.
- An optional per-layer `velocityResponse` replaces the global output-gain response for that destination articulation only. Rebuild its bounded 128-value gain table outside playback and do not change exact named-velocity selection.
- Velocity-response UI reads copied source-articulation measurements on the message thread. User curve presets store only a validated response definition under `velocity_response_presets`; applying one copies the definition into the layer map.
- Normal note selection returns one complete ready logical hit. Reserve voices for all active linked mic entries before starting any of them; never mix mic entries from different velocity or round-robin hits.
- `SampleLoader` cache identity is source content identity plus one resolved typed preprocessing recipe. Preview and progressive playback must use the same recipe path, and audio-changing recipe values must be part of the cache key.
- Hi-hat CC tail transitions use the listening-approved per-voice replacement path. Each mic keeps its own decay-matched offset first. If that target region is silent, a closing transition may try the validated Direct offset and then the Overhead offset; if both reference regions are silent, fade that old mic voice. Do not replace this fallback with unconditional canonical offsets, shared event IDs, atomic all-mic commits, or complementary target ramps without a separate listening comparison.
- Poly aftertouch may use a hidden exact `choke` articulation from the same source instrument and synth layer. Start one ready logical hit per pressure gesture, validate decay-matched post-peak offsets for every target mic before committing, then fade all old note voices. Continuing pressure must not choke the replacement sample. If the target, metadata, audio region, or required idle voices are unavailable, keep the existing pressure-driven fade.
- Every primary or shadow synth layer for one hi-hat slot must observe that slot's note-on articulation rank before rejecting an unrouted note. This lets a tight or pedal note in one layer fade open voices in sibling layers while only the routed layer starts samples.
- Entirely skipped hi-hat transitions caused by the attack guard or temporary voice pressure retry at the existing controller-smoothing interval, even when no newer CC message arrives.
- Entering the most-closed mapped hi-hat controller state may trigger the layout's `pedal` note only when the closure gesture contains an audible open/half tail. Its velocity follows that tail's remaining estimated loudness; silence and equal-POC tight-only playback do not generate an implicit pedal event.
- Instrument-map synth ownership comes from exact destination routes. `noteToArticulation` is only the final MIDI lookup table and must not be used to decide which synth owns a destination articulation.
- Explicit hi-hat destination anchors preserve their expression. Pedal-open-close controller siblings stay inside the resolved source family unless the map explicitly remaps that family.
- Zone-pad preview may start a loaded hi-hat controller sibling directly for a state without its own MIDI note. This preview must not send CC or modify the saved controller state.
- Publish asynchronously loaded buffers before setting their `loaded` flag with release semantics; audio-thread readers check the flag with acquire semantics first.
- Decay metadata is an estimate of sample loudness, not a substitute for attack-envelope or phase information.
- Filename playback timing uses `on<ms>` to advance by skipping the source beginning and `op<ms>` to delay output. Optional `_micCode` suffixes limit an offset to selected mics in a multichannel file. Apply these offsets per runtime sound and preview without changing the shared sample-loader cache key.
- Sample mic identity includes the full mic code, so variants such as `dtm1` and `dtm2` remain separate.
- Mic placement is precomputed per routed sample. Stereo overhead keeps its recorded image and uses only manual layer pan; eligible non-room mono-to-stereo routes use source balance plus manual pan; direct mono-to-mono and room routes ignore layer pan.
- Every mic-layout output has a required unique semantic ID, such as `snare1/dtm` or `aux1/**m`. Saved routes use this ID so repeated codes stay distinct across layout reordering.
- Only mic-layout target output codes may use `*`, independently for type or position. Source mic codes and sample filenames stay concrete. The format remains exact. A present variant is exact, while an omitted variant accepts all variants.
- Pad-popup layer parameters belong to the destination articulation runtime, even when one `InstrumentSynth` contains several articulations.

# Work Guidance

- Keep bounded fixed-size lookup tables for MIDI domains (`0..127`) when they improve hot-path predictability.
- Avoid strings, logging preparation, heap allocation, locks with message-thread contention, and sample-window scans in realtime paths.
- The bounded 1024-sample hi-hat target validation scan is known performance debt retained to preserve accepted sound. Reference-offset validation may repeat it only after the mic-specific region is silent; do not increase the window or run reference scans on the normal successful path.
- Add focused tests for parsers and pure transition-selection math before wiring behavior into voices.

# Verification

- Build `Tests`.
- Run `[DecayEnvelope]` tests for decay metadata changes.
- Add or run focused synth/controller tests for MIDI routing and transition-policy changes.
- Use listening tests for multi-mic hi-hat transitions; unit tests alone cannot validate timbre or phase coherence.

# Child DOX Index
