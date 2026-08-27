# Purpose

- Own realtime drum playback, MIDI interpretation, sample-library models, loading, voice lifecycle, routing, and engine settings.

# Ownership

- `InstrumentPlayer*` owns per-instrument runtime tables, voices, loading, routing, parameters, note/controller handling, and rendering. `InstrumentPlayerHiHat.cpp`, `InstrumentPlayerAftertouch.cpp`, and `InstrumentPlayerNotePlayback.cpp` own their named realtime workflows.
- `ArticulationNames.h` owns shared articulation-name normalization and matching.
- `InstrumentType*` owns the narrow shared instrument type and its text conversions.
- `InstrumentCatalog*` owns instrument families, aliases, and mic selector matching.
- `InstrumentMap/` owns instrument-map data, text parsing, normalized serialization, and reusable map operations.
- `ArticulationSlotFallbackRules.h` owns built-in and user-overridden articulation-slot fallback order for whole-instrument map generation.
- `KitLibModel.h` declares parsed library metadata and shared library identity matching. `KitLibValueTypes.cpp`, `KitLibInstrument.cpp`, and `KitLibLibrary.cpp` own value types, instrument/sample parsing, and kit/library scanning.
- `LibraryKitOverrides*` owns sparse user-authored library kit corrections loaded from and saved to each kit's `kit-overrides.yml`.
- `SampleLoader*` owns asynchronous sample loading and publication.
- `DrumKitEngine.cpp` owns core lifecycle and process-block coordination. `DrumKitEngineSettings.cpp`, `DrumKitEnginePreview.cpp`, `DrumKitEngineLibraryKitOverrides.cpp`, and `DrumKitEngineMapApply.cpp` own their named workflows. Instrument-map apply prepares replacement players, value-based parameter updates, fingerprints, and note routes before `commitInstrumentMapApply()` publishes them under `engineGraphLock`.
- `LayoutManager*` owns kit and mic layout models plus generic layout discovery. `LayoutYaml.cpp` owns YAML parsing, validation, and serialization.
- `MicRoutingPlan*` owns resolved output allocation and mic-route lookup.

# Local Contracts

- Code reached from `processBlock`, `renderNextBlock`, `handleMidiEvent`, note-on, controller, or aftertouch paths is realtime code.
- Realtime settings shared with the message thread must use atomics or an immutable snapshot; do not introduce unsynchronized mutable scalar reads/writes.
- Precompute routing, articulation, velocity, round-robin, and envelope lookup data during load and rebuild work where practical.
- Exact library sample selection follows resolved velocity-level upper boundaries. Filename velocities remain stable sample identity. `kit-overrides.yml` can change only their effective selection ranges. Calibration preserves each library articulation's hardest measured reference.
- Automatic floating selection applies only to calibrated articulations with at least the global `minimumVelocityLevels` setting and no more than two complete logical hits per velocity level. Its bounded candidates come from the exact level and one adjacent level on each side, use measured pre-trim intensity as the main score, and fall back to exact selection when metadata or readiness is insufficient or round-robin lock is active.
- Library kit override precedence is saved `kit-overrides.yml`, then the filename or folder value, then the existing engine default. Keep filename balance, manual gain, and playback offset available separately from their effective values. Effective manual gain participates in calibration, peak safety, preprocessing, and cache identity exactly once.
- Library kit override YAML parsing and atomic file replacement stay outside playback. Reject a save when the loaded override file changed externally. A saved-file reload reapplies the overrides to the library model and matching instrument players. Recover moved sample overrides by content hash only when the match is unique.
- Library-articulation editor batch changes resolve every selected file before saving one complete candidate `kit-overrides.yml`. Publish matching runtime values only after the save succeeds. Do not implement batch editing by repeating single-value saves.
- Live velocity-range dragging rebuilds only matching bounded instrument-player lookup tables under `engineGraphLock`. Persistence happens on commit. Live placement balance and playback offsets affect later notes without a buffer reload.
- A live manual-gain edit recalculates the affected articulation, applies a smoothed correction to old canonical buffers, requests only changed preprocessing recipes, and publishes replacements under `engineGraphLock`. Voices retain a shared pointer to the buffer and canonical gain they started with, so replacing a runtime sound buffer cannot invalidate or double-correct an active voice.
- An optional per-map-layer `velocityResponse` replaces the global output-gain response for that articulation slot only. Rebuild its bounded 128-value gain table outside playback and do not change exact velocity-level selection.
- Velocity-response UI reads copied library-articulation measurements on the message thread. Stable built-in definitions are `linear`, `logarithmic`, and `exponential`. Point definitions preserve each control point and use smooth monotone cubic interpolation in dB without overshoot. User curve presets store only a validated response definition under `velocity_response_presets`. Applying one copies the definition into the map layer. Live global changes rebuild loaded velocity tables under `engineGraphLock`. Map layer overrides remain effective.
- Velocity-response editing previews only the selected map layer. Resolve it with the same instrument slot, articulation playback entry, and map layer order used by parameter updates.
- Normal note selection and selected library preview return one complete ready logical hit. Reserve voices for all samples before starting any of them. Never mix samples from different velocity levels or logical hits. Library preview must not advance the normal round robin counter.
- Kit-page grouped library preview resolves one sample target for every selected map layer outside realtime work, waits for all async loads, and publishes one mixed stereo preview. It must preserve each layer's map gain, sample preprocessing gain, playback offset, and sample rate. It must not change the active instrument map. A newer preview or library-cache invalidation cancels stale grouped preview publication.
- Instrument loading creates configured sounds, groups them into velocity-level logical hits, builds velocity mapping, and assigns deterministic initial stages in stable library order. Stage 1 contains up to the configured `N` hits from the highest velocity level. Stage 2 contains up to `N` hits from every other level. Adaptive submits stages 1 and 2 and keeps stage 3 as dormant prepared work. Full submits stage 3 immediately. The static family order is Kick, Snare, HiHat, crash family, Ride, Tom, other drums, then other percussion. Every active mic file in one hit shares its group target, priority class, static priority, and sequence. File requests remain cache-deduplicated below the group, and a higher group promotes an equivalent queued file without losing callbacks. Preserve source recipe revision, progress, and shared logical-hit readiness in every callback.
- Every runtime articulation owns stable shared atomic sample-load demand state. Note-on, hi-hat controller transition, and aftertouch choke paths may update only its bounded note count, requested velocity-level index, revision, and active target generation. The loader worker performs all queue mutation. It promotes missing minimal work first, activates at most one dormant hit for one handled demand revision, and promotes pending hot-articulation work after the third request. Demand target generations deactivate before runtime articulations are cleared. Dormant progress is registered only during activation and only for the matching progress revision.
- Sample loading uses six priority classes in this order: exact demanded minimal work, newly activated dormant work, hot-articulation work, highest-velocity minimal work, other-velocity minimal work, and Full remainder. Keep files from one logical hit together. At equal priority, dispatch one complete logical hit and then prefer a different target so one dense articulation cannot starve the others. Empty queue polling is 5 ms and must not be replaced with a lock-taking notification from realtime code.
- Each instrument player exposes atomic finished and total initial sample-load request counts. Replacement players invalidate old callback counts by revision. Zone UI captures shared counter sources during grid refresh and reads them without locking the engine graph.
- `SampleLoader` cache identity is sample content identity plus one resolved typed preprocessing recipe. Preview and progressive playback must use the same recipe path. Audio-changing recipe values must be part of the cache key.
- Hi-hat CC tail transitions use the listening-approved per-voice replacement path. Each mic keeps its own decay-matched offset first. If that target region is silent, a closing transition may try the validated Direct offset and then the Overhead offset; if both reference regions are silent, fade that old mic voice. Do not replace this fallback with unconditional canonical offsets, shared event IDs, atomic all-mic commits, or complementary target ramps without a separate listening comparison.
- `transitionHiHatVoices()` preserves voice iteration order and partial-success behavior. Its settings, controller state, and counters stay scalar or fixed-size and must not allocate.
- Poly aftertouch may use a hidden exact `choke` articulation from the same library instrument and map layer. Start one ready logical hit per pressure gesture. Validate decay-matched post-peak offsets for every target mic before committing. Then fade all old note voices.
- Aftertouch choke preparation stores at most 64 target sounds, offsets, and reserved voices in fixed arrays. `commitAftertouchChokePlan()` runs only after every target is ready and audible.
- Every primary or additional instrument player for one hi-hat slot must observe that slot's note-on articulation rank. This lets one map layer fade open voices in sibling layers while only the assigned layer starts samples.
- Entirely skipped hi-hat transitions caused by the attack guard or temporary voice pressure retry at the existing controller-smoothing interval, even when no newer CC message arrives.
- Entering the most-closed mapped hi-hat controller state may trigger the layout's `pedal` note only when the closure gesture contains an audible open/half tail. Its velocity follows that tail's remaining estimated loudness; silence and equal-POC tight-only playback do not generate an implicit pedal event.
- Instrument-player ownership comes from exact articulation playback entries. `noteToArticulation` is only the final MIDI lookup table. Do not use it to select the instrument player for an articulation slot.
- Explicit hi-hat articulation slot anchors preserve their expression. Pedal-open-close controller siblings stay inside the resolved library articulation family unless the map explicitly changes that family.
- Zone-pad preview may start a loaded hi-hat controller sibling directly for a state without its own MIDI note. This preview must not send CC or modify the saved controller state.
- Publish asynchronously loaded buffers before setting their `loaded` flag with release semantics; audio-thread readers check the flag with acquire semantics first.
- Decay metadata is an estimate of sample loudness, not a substitute for attack-envelope or phase information.
- Runtime decay envelopes use the readable peak and decay points from each kit's `metadata.csv`.
- Filename playback timing uses `on<ms>` to advance by skipping the source beginning and `op<ms>` to delay output. Optional `_micCode` suffixes limit an offset to selected mics in a multichannel file. Apply these offsets per runtime sound and preview without changing the shared sample-loader cache key.
- Mic identity includes the full mic code, so variants such as `dtm1` and `dtm2` remain separate.
- Mic placement is precomputed per routed sample. Stereo overhead keeps its recorded image and uses only manual layer pan; eligible non-room mono-to-stereo routes use placement balance plus manual pan; direct mono-to-mono and room routes ignore layer pan.
- Every mic-layout output has a required unique semantic ID, such as `snare1/dtm` or `aux1/**m`. Saved routes use this ID so repeated codes stay distinct across layout reordering.
- Only mic-layout target output codes may use `*`, independently for type or position. Mic codes and sample filenames stay concrete. The format remains exact. A present variant is exact, while an omitted variant accepts all variants.
- Pad-popup layer parameters belong to the articulation slot runtime, even when one `InstrumentPlayer` contains several articulations.
- Every instrument map layer stores one required articulation-slot target and one full `kit/instrument/articulation` library path.
- Whole-instrument slot replacement accepts any available library instrument. It removes every existing map layer for that slot and preserves every other slot. It uses an exact articulation-slot match first. Otherwise, it uses the first available slot from the configured instrument-type fallback order and reports a warning. It skips the library articulation with a warning when no fallback slot is available.
- Linked pad-popup edits resolve map layers before locking. Then they apply each parameter or mic-route batch under one `engineGraphLock`. A mic update changes only that stable mic code and preserves every other mic value.
- Instrument-map apply must not retain pointers to parsed map layers in its commit plan. Retired instrument players stay alive until after `engineGraphLock` is released.

# Work Guidance

- Keep bounded fixed-size lookup tables for MIDI domains (`0..127`) when they improve hot-path predictability.
- Avoid strings, logging preparation, heap allocation, locks with message-thread contention, and sample-window scans in realtime paths.
- The bounded 1024-sample hi-hat target validation scan is known performance debt retained to preserve accepted sound. Reference-offset validation may repeat it only after the mic-specific region is silent; do not increase the window or run reference scans on the normal successful path.
- Add focused tests for parsers and pure transition-selection math before wiring behavior into voices.

# Verification

- Build `Tests`.
- Run `[LibraryKitOverrides]` tests for library kit override parsing, persistence, and exact range behavior.
- Run `[DecayEnvelope]` tests for decay metadata changes.
- Add or run focused instrument-player and controller tests for MIDI routing and transition-policy changes.
- Use listening tests for multi-mic hi-hat transitions; unit tests alone cannot validate timbre or phase coherence.

# Child DOX Index

- `InstrumentMap/AGENTS.md` - instrument-map data and text format ownership
