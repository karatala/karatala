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
- Hi-hat CC tail transitions use the listening-approved per-voice replacement path. Do not reintroduce shared event IDs, atomic all-mic commits, canonical reference-mic offsets, or complementary target ramps without a separate listening comparison.
- Entirely skipped hi-hat transitions caused by the attack guard or temporary voice pressure retry at the existing controller-smoothing interval, even when no newer CC message arrives.
- Entering the most-closed mapped hi-hat controller state may trigger the layout's `pedal` note only when the closure gesture contains an audible open/half tail. Its velocity follows that tail's remaining estimated loudness; silence and equal-POC tight-only playback do not generate an implicit pedal event.
- Publish asynchronously loaded buffers before setting their `loaded` flag with release semantics; audio-thread readers check the flag with acquire semantics first.
- Decay metadata is an estimate of sample loudness, not a substitute for attack-envelope or phase information.
- Sample mic identity includes the full mic code, so variants such as `dtm1` and `dtm2` remain separate.
- Every mic-layout output has a required unique semantic ID, such as `snare1/dtm` or `aux1/dtm`. Saved routes use this ID so repeated codes stay distinct across layout reordering.

# Work Guidance

- Keep bounded fixed-size lookup tables for MIDI domains (`0..127`) when they improve hot-path predictability.
- Avoid strings, logging preparation, heap allocation, locks with message-thread contention, and sample-window scans in realtime paths.
- The existing bounded 1024-sample hi-hat target validation scan is known performance debt retained to preserve accepted sound; do not expand it.
- Add focused tests for parsers and pure transition-selection math before wiring behavior into voices.

# Verification

- Build `Tests`.
- Run `[DecayEnvelope]` tests for decay metadata changes.
- Add or run focused synth/controller tests for MIDI routing and transition-policy changes.
- Use listening tests for multi-mic hi-hat transitions; unit tests alone cannot validate timbre or phase coherence.

# Child DOX Index
