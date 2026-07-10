# ✅ COMPLETED: TODO04: Graceful Playback During Progressive Sample Loading

## Problem Statement

When a kit is loaded, samples are enqueued for asynchronous loading in 4 priority stages via `SampleLoaderQueue`. The runtime (`InstrumentSynth`) immediately becomes playable, but most `SampledSound::buffer` pointers are still `nullptr`. This causes:

1. **Silent notes** — `startLayerVoices()` skips samples with `buffer == nullptr`, so the note produces no sound (or partial sound with only some mics playing).
2. **Warning spam** — `LOG(Warning, "Sample buffer not loaded for ...")` fires on every unloaded sample hit.
3. **Round-robin desync** — `chooseVariantGroup()` counts ALL variant indices (including unloaded ones), so the round-robin counter advances through variants that can't play. This wastes round-robin slots and produces gaps in the expected rotation pattern even after everything is loaded.
4. **Pedal transitions fail silently** — `handleController()` increments `skippedNoTarget` when the replacement sample buffer is null, leaving old voices ringing with no transition.

## Current Architecture (Summary)

### Loading Side (`InstrumentSynthLoader.cpp` → `SampleLoader`)

```
loadInstrument()
  └─ processLayoutArticulations()
       └─ processArticulationSpec()
             └─ createVelocityLayers()
                   ├─ creates SampledSound with buffer=nullptr
                   ├─ adds to synth.sounds + layer.samples
                   └─ enqueues via SampleLoaderQueue::enqueueCandidate()
                         ├─ computes priority (kick=1..cymbal=7)
                         ├─ determines stage (1=main art+representative vel,
                         │     2=other art+representative vel, 3=remaining vel, 4=extra RR)
                         └─ deduplicates by file path (multiple callbacks per file)

DrumKitEngine triggers queue.startProcessing() after all instruments enqueued
  └─ workerLoop() on dedicated thread
       ├─ scans stages 1→4, picks lowest-priority-value candidate
       ├─ respects maxInFlight (default 3)
       └─ dispatches to SampleLoader::requestAsyncLoad()
             ├─ checks in-memory cache
             ├─ on miss: ThreadPool LoadJob reads file, applies trim/fade
             └─ callback posted via MessageManager::callAsync()
                   └─ sets sound->buffer = buf; sound->sampleRate = sr;
```

### Playback Side (`InstrumentSynth.cpp`)

```
playNote()
  ├─ resolveArticulation(note) → artIdx (considers CC state)
  ├─ resolveVelocityLayer(art, vel) → layerIdx
  ├─ chooseVariantGroup(layer) → chosenVariant  ← BUG: counts unloaded variants
  └─ startLayerVoices(art, layer, chosenVariant, ...)
       └─ for each sample in layer:
            ├─ skip if variantIdx != chosenVariant
            ├─ skip if buffer == nullptr  ← SILENT NOTE + WARNING
            └─ start voice
```

### Failure Points Identified

| # | Location | Symptom | Root Cause |
|---|----------|---------|------------|
| 1 | `chooseVariantGroup()` | RR counter advances through unloaded variants | Doesn't filter by load status |
| 2 | `startLayerVoices()` L492-494 | Warning + skip | Selected variant's buffer not loaded yet |
| 3 | `handleController()` L196-198 | `skippedNoTarget++` | Pedal replacement target buffer null |
| 4 | Cross-variant: some mics loaded, others not | Partial playback (e.g. direct mic plays, room mic silent) | Per-file loading order doesn't guarantee all mics of a variant load together |

### Thread Safety Note

`SampledSound::buffer` is a `std::shared_ptr<AudioBuffer<float>>` written on the message thread (via `MessageManager::callAsync`) and read on the audio thread. The `shared_ptr` assignment is *not* atomic under the C++ memory model — this is technically a data race. In practice it works on x86/ARM64 due to pointer-sized atomic stores, but it should be made properly safe.

---

## Proposed Solution

### Core Idea: **Track loaded variant availability per-layer; fall back to any loaded variant**

Instead of treating "which variants exist" as static (all variants in the layer), track which variants are *ready* (have all their mic samples loaded). When the user plays a note, prefer the round-robin variant but **fall back to any loaded variant** rather than producing silence.

### Phase 1: Loaded-Aware Variant Selection (fixes silence + warnings)

#### 1.1 Add per-sample load-tracking flag

In `SampledSound`, add an atomic flag:

```cpp
class SampledSound {
public:
    // ... existing members ...
    std::atomic<bool> loaded{false};  // set to true when buffer is assigned
};
```

In the loading callback (InstrumentSynthLoader.cpp L212):
```cpp
[sound](std::shared_ptr<juce::AudioBuffer<float>> buf, double sr) {
    sound->buffer = buf;
    sound->sampleRate = sr;
    sound->loaded.store(true, std::memory_order_release);
}
```

#### 1.2 Add variant-readiness check helper

```cpp
// Returns true if ALL samples for this variant in the layer have loaded buffers
bool isVariantReady(const VelocityLayerRuntime& layer, uint8_t variantIdx) {
    for (const auto& se : layer.samples) {
        if (se.variantIdx == variantIdx && se.sound != nullptr) {
            if (!se.sound->loaded.load(std::memory_order_acquire))
                return false;
        }
    }
    return true;
}
```

#### 1.3 Rewrite `chooseVariantGroup()` to be load-aware

```cpp
uint8_t InstrumentSynth::chooseVariantGroup(VelocityLayerRuntime& layer) {
    // Collect all variant indices
    std::vector<uint8_t> allVariants;
    allVariants.reserve(layer.samples.size());
    for (const auto& se : layer.samples) {
        if (std::find(allVariants.begin(), allVariants.end(), se.variantIdx) == allVariants.end())
            allVariants.push_back(se.variantIdx);
    }
    if (allVariants.empty()) return 0;

    // Filter to only loaded variants
    std::vector<uint8_t> readyVariants;
    readyVariants.reserve(allVariants.size());
    for (uint8_t v : allVariants) {
        if (this->isVariantReady(layer, v))
            readyVariants.push_back(v);
    }

    // If nothing loaded yet, return first variant (will hit buffer==nullptr
    // check downstream — unavoidable, but at least RR counter isn't wasted)
    if (readyVariants.empty()) return allVariants[0];

    // Advance RR counter only over ready variants
    if (readyVariants.size() == 1) return readyVariants[0];
    uint32_t rr = layer.roundRobinCounter->fetch_add(1u);
    size_t idx = rr % readyVariants.size();
    return readyVariants[idx];
}
```

**Key benefit**: RR counter only advances over *playable* variants. As more variants load, the pool naturally expands. No silence, no wasted RR slots.

#### 1.4 Downgrade the log in `startLayerVoices()`

Change the `buffer == nullptr` log from `Warning` to `Debug` (or `Verbose`), since after Phase 1 this should be extremely rare (only if literally nothing has loaded yet):

```cpp
if (se.sound->buffer == nullptr) {
    LOG(Debug, "Sample buffer not loaded ...");
    continue;
}
```

### Phase 2: Velocity Layer Fallback (handles very early playback)

If the exact velocity layer has no loaded variants at all, fall back to the nearest loaded layer.

#### 2.1 Add `resolveVelocityLayerWithFallback()`

```cpp
int InstrumentSynth::resolveVelocityLayer(const ArticulationRuntime& art, juce::uint8 velocity) {
    int preferred = art.velocityToLayer[(int)velocity];
    if (preferred >= 0 && preferred < (int)art.layers.size()
        && this->hasAnyLoadedVariant(art.layers[preferred]))
        return preferred;

    // Fallback: find nearest layer with at least one loaded variant
    int bestLayer = -1;
    int bestDist = INT_MAX;
    for (int i = 0; i < (int)art.layers.size(); ++i) {
        if (!this->hasAnyLoadedVariant(art.layers[i])) continue;
        int dist = std::abs((int)art.layers[i].velocity - (int)velocity);
        if (dist < bestDist) {
            bestDist = dist;
            bestLayer = i;
        }
    }
    return bestLayer;  // -1 if nothing loaded at all
}
```

With helper:
```cpp
bool InstrumentSynth::hasAnyLoadedVariant(const VelocityLayerRuntime& layer) const {
    for (const auto& se : layer.samples)
        if (se.sound && se.sound->loaded.load(std::memory_order_acquire))
            return true;
    return false;
}
```

### Phase 3: Atomic buffer pointer (thread safety)

Replace `std::shared_ptr<juce::AudioBuffer<float>> buffer` with a wrapper that uses `std::atomic<...>` or a load/store pair protected by the `loaded` flag introduced in Phase 1.

Since `std::atomic<std::shared_ptr<T>>` requires C++20, and JUCE targets C++17, the simplest correct approach is:

- The `loaded` atomic flag acts as a **release/acquire fence** around the buffer pointer.
- Writer (message thread): sets `buffer`, then `loaded.store(true, release)`.  
- Reader (audio thread): checks `loaded.load(acquire)` first, then reads `buffer`.  
- This is a correct publication pattern — the acquire/release pair guarantees the `buffer` write is visible when `loaded` reads `true`.

This means **no changes to `shared_ptr`** are needed — just discipline: the audio thread must check `loaded` before accessing `buffer`. The null-check in `startLayerVoices()` already exists but should be guarded by the flag:

```cpp
if (!se.sound->loaded.load(std::memory_order_acquire)) continue;
// buffer is guaranteed non-null and fully written here
```

### Phase 4: Pedal Transition Awareness (handleController)

Apply the same pattern in `handleController()`: when searching for `targetSe`, prefer loaded samples. If the target variant/mic isn't loaded, try another loaded variant in the same layer rather than skipping the transition entirely.

```cpp
// existing code finds targetSe by variant + mic match
// ADD: if targetSe->sound->buffer == nullptr, search for alternative
if (targetSe != nullptr && !targetSe->sound->loaded.load(std::memory_order_acquire)) {
    const InstrumentSynth::SampleEntry* fallback = nullptr;
    for (const auto& se : layer.samples) {
        if (se.sound && se.sound->loaded.load(std::memory_order_acquire)
            && se.sound->micBufferChannelStart == voiceMicStart) {
            fallback = &se;
            break;
        }
    }
    if (fallback) targetSe = fallback;
    // else: keep original targetSe, will hit the null-buffer skip below
}
```

---

## Implementation Order & Effort

| Phase | Description | Risk | Effort | Impact |
|-------|-------------|------|--------|--------|
| **1** | Load-aware variant selection | Low | ~1h | Eliminates 95%+ of silence events |
| **2** | Velocity layer fallback | Low | ~30min | Handles edge case of very early playback |
| **3** | Atomic buffer publication | Low | ~30min | Correctness (data race fix) |
| **4** | Pedal transition fallback | Medium | ~1h | Better hi-hat behavior during loading |

### Files Changed

| File | Changes |
|------|---------|
| [source/Engine/InstrumentSynth.h](../source/Engine/InstrumentSynth.h) | Add `loaded` atomic to `SampledSound`; add `isVariantReady()`, `hasAnyLoadedVariant()` helpers |
| [source/Engine/InstrumentSynth.cpp](../source/Engine/InstrumentSynth.cpp) | Rewrite `chooseVariantGroup()`; update `resolveVelocityLayer()`; update `startLayerVoices()` log level; update `handleController()` fallback |
| [source/Engine/InstrumentSynthLoader.cpp](../source/Engine/InstrumentSynthLoader.cpp) | Set `loaded` flag in loading callback |

### What This Does NOT Change

- **Loading priority/staging logic** — already well-designed (kick first, main articulations first, representative velocities first). No changes needed.
- **SampleLoaderQueue architecture** — works correctly; the problem is purely on the playback/runtime side.
- **Memory layout** — `SampleEntry`, `VelocityLayerRuntime`, `ArticulationRuntime` structs unchanged (only `SampledSound` gets one atomic bool).

---

## Testing Strategy

1. **Unit test**: Add test for `chooseVariantGroup()` that creates a layer with 3 variants where only variant B is loaded → verify it always returns B until others load.
2. **Unit test**: `resolveVelocityLayer` fallback — create art with 5 layers, only layer 2 loaded → verify velocity 10 (normally layer 0) resolves to layer 2.
3. **Integration**: Load a kit, immediately play notes rapidly — verify no `Warning` logs about unloaded buffers and notes produce sound from the earliest loaded samples.
4. **Manual**: Play hi-hat with pedal CC while kit is still loading — verify transitions work with whatever is available rather than silently skipping.
