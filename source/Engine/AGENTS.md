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
- Publish asynchronously loaded buffers before setting their `loaded` flag with release semantics; audio-thread readers check the flag with acquire semantics first.
- Decay metadata is an estimate of sample loudness, not a substitute for attack-envelope or phase information.

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
