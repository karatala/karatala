# ✅ COMPLETED: TODO04: Graceful Playback During Progressive Sample Loading

## Problem Statement

When a kit is loaded, samples are enqueued for asynchronous loading in 4 priority stages via `SampleLoaderQueue`. The runtime (`InstrumentPlayer`) immediately becomes playable, but most `SampledSound::buffer` pointers are still `nullptr`. This causes:

1. **Silent notes** - `startVelocityLevelVoices()` skips samples with `buffer == nullptr`, so the note produces no sound (or partial sound with only some mics playing).
2. **Warning spam** - `LOG(Warning, "Sample buffer not loaded for ...")` fires on every unloaded sample hit.
3. **Round-robin desync** - the old selection code included incomplete logical hits, so the counter could advance to a hit that could not play.
4. **Pedal transitions fail silently** - `handleController()` increments `skippedNoTarget` when the replacement sample buffer is null, leaving old voices ringing with no transition.

## Current Architecture (Summary)

### Loading Side (`InstrumentPlayerLoader.cpp` → `SampleLoader`)

```
loadInstrumentWithPlaybackEntries()
  └─ applyArticulationPlaybackEntries()
       └─ applyArticulationPlaybackEntry()
            └─ processArticulationSpec()
                 └─ createVelocityLevels()
                   ├─ creates SampledSound with buffer=nullptr
                   ├─ adds samples to one logical hit in one velocity level
                   └─ enqueues via SampleLoaderQueue::enqueueCandidate()
                         ├─ computes priority (kick=1..cymbal=7)
                         ├─ determines stage (1=main art+representative vel,
                         │     2=other articulation and representative velocity, 3=remaining velocities, 4=extra round robin IDs)
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

### Playback Side (`InstrumentPlayer.cpp`)

```
playNote()
  ├─ resolveArticulation(note) → artIdx (considers CC state)
  ├─ resolveVelocityLevel(articulation, velocity) → velocityLevelIdx
  ├─ chooseLogicalHit(velocityLevel) → logicalHit
  └─ startVelocityLevelVoices(articulation, velocityLevel, logicalHit, ...)
       ├─ require every sample in the logical hit to be ready
       ├─ reserve one voice for every sample
       └─ start the complete logical hit
```

### Failure Points Identified

| # | Location | Symptom | Root Cause |
|---|----------|---------|------------|
| 1 | `chooseLogicalHit()` | Round robin counter advances through incomplete logical hits | Does not filter by load status |
| 2 | `startVelocityLevelVoices()` L492-494 | Warning + skip | A sample in the selected logical hit is not loaded |
| 3 | `handleController()` L196-198 | `skippedNoTarget++` | Pedal replacement target buffer null |
| 4 | Some samples in one logical hit load before others | Partial playback, for example direct mic without room mic | Per-file loading order does not make the complete logical hit ready at one time |

### Thread Safety Note

`SampledSound::buffer` is a `std::shared_ptr<AudioBuffer<float>>` written on the message thread (via `MessageManager::callAsync`) and read on the audio thread. The `shared_ptr` assignment is *not* atomic under the C++ memory model - this is technically a data race. In practice it works on x86/ARM64 due to pointer-sized atomic stores, but it should be made properly safe.

---

## Implemented Solution

### Core Idea: Track complete logical-hit availability per velocity level

Each velocity level owns explicit logical hits. A logical hit is ready only when all of its samples are ready. Note selection advances round robin only across ready logical hits.

### ✅ Phase 1: Load-aware logical-hit selection

#### 1.1 Add per-sample load-tracking flag

In `SampledSound`, add an atomic flag:

```cpp
class SampledSound {
public:
    // ... existing members ...
    std::atomic<bool> loaded{false};  // set to true when buffer is assigned
};
```

In the loading callback (InstrumentPlayerLoader.cpp L212):
```cpp
[sound](std::shared_ptr<juce::AudioBuffer<float>> buf, double sr) {
    sound->buffer = buf;
    sound->sampleRate = sr;
    sound->loaded.store(true, std::memory_order_release);
}
```

#### 1.2 Add logical-hit readiness

```cpp
bool LogicalHitRuntime::isReady() const {
    for (const size_t sampleEntryIdx : this->sampleEntryIndexes) {
        if (!this->velocityLevel->samples[sampleEntryIdx].sound->loaded.load(std::memory_order_acquire)) return false;
    }
    return true;
}
```

#### 1.3 Rewrite `chooseLogicalHit()` to be load-aware

`chooseLogicalHit()` counts ready logical hits, advances the round-robin counter once, and returns one complete ready logical hit. It returns `nullptr` when none are ready.

#### 1.4 Downgrade the log in `startVelocityLevelVoices()`

Change the `buffer == nullptr` log from `Warning` to `Debug` (or `Verbose`), since after Phase 1 this should be extremely rare (only if literally nothing has loaded yet):

```cpp
if (se.sound->buffer == nullptr) {
    LOG(Debug, "Sample buffer not loaded ...");
    continue;
}
```

### ✅ Phase 2: Velocity-level fallback

If the exact velocity level has no ready logical hit, use the nearest velocity level that has one.

#### 2.1 Add `resolveVelocityLevelWithFallback()`

```cpp
int InstrumentPlayer::resolveVelocityLevel(const ArticulationRuntime& art, juce::uint8 velocity) {
    int preferred = art.velocityToLevel[(int)velocity];
    if (preferred >= 0 && preferred < (int)art.velocityLevels.size() && this->hasReadyLogicalHit(art.velocityLevels[preferred]))
        return preferred;

    // Fallback: find the nearest velocity level with a ready logical hit.
    int bestLevel = -1;
    int bestDist = INT_MAX;
    for (int levelIdx = 0; levelIdx < (int)art.velocityLevels.size(); ++levelIdx) {
        if (!this->hasReadyLogicalHit(art.velocityLevels[levelIdx])) continue;
        int dist = std::abs((int)art.velocityLevels[levelIdx].velocity - (int)velocity);
        if (dist < bestDist) {
            bestDist = dist;
            bestLevel = levelIdx;
        }
    }
    return bestLevel;
}
```

### ✅ Phase 3: Safe buffer publication

Use the `loaded` flag as the publication boundary for the shared buffer pointer:

- The `loaded` atomic flag acts as a **release/acquire fence** around the buffer pointer.
- Writer on the message thread sets `buffer`, then calls `loaded.store(true, release)`.
- Reader on the audio thread calls `loaded.load(acquire)` before it reads `buffer`.
- The acquire/release pair makes the buffer write visible before playback uses it.

The audio thread must check `loaded` before it accesses `buffer`:

```cpp
if (!se.sound->loaded.load(std::memory_order_acquire)) continue;
// buffer is guaranteed non-null and fully written here
```

### ✅ Phase 4: Pedal-transition awareness

Apply the same rule in `handleController()`. A transition can use only a complete ready target logical hit.

---

## Implementation Order & Effort

| Phase | Description | Risk | Effort | Impact |
|-------|-------------|------|--------|--------|
| **1 ✅** | Load-aware logical-hit selection | Low | ~1h | Prevents partial logical-hit playback |
| **2 ✅** | Velocity-level fallback | Low | ~30min | Handles very early playback |
| **3 ✅** | Safe buffer publication | Low | ~30min | Removes the publication data race |
| **4 ✅** | Pedal-transition fallback | Medium | ~1h | Improves hi-hat behavior during loading |

### Files Changed

| File | Changes |
|------|---------|
| [source/Engine/InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h) | Add `loaded` state and explicit logical-hit runtime data |
| [source/Engine/InstrumentPlayerController.cpp](../source/Engine/InstrumentPlayerController.cpp) | Select complete ready logical hits and use velocity-level fallback |
| [source/Engine/InstrumentPlayerLoader.cpp](../source/Engine/InstrumentPlayerLoader.cpp) | Set `loaded` flag in loading callback |

### What This Does NOT Change

- **Loading priority/staging logic** - already well-designed (kick first, main articulations first, representative velocities first). No changes needed.
- **SampleLoaderQueue architecture** - works correctly; the problem is purely on the playback/runtime side.
- **Memory layout** - `SampleEntry`, `VelocityLevelRuntime`, `ArticulationRuntime` structs unchanged (only `SampledSound` gets one atomic bool).

---

## Testing Strategy

1. **Unit test**: Create one velocity level with three logical hits where only round robin B is ready. Verify that `chooseLogicalHit()` returns B until another complete logical hit is ready.
2. **Unit test**: Create an articulation with five velocity levels where only level 2 has a ready logical hit. Verify that velocity 10 resolves to level 2.
3. **Integration**: Load a kit, immediately play notes rapidly - verify no `Warning` logs about unloaded buffers and notes produce sound from the earliest loaded samples.
4. **Manual**: Play hi-hat with pedal CC while kit is still loading - verify transitions work with whatever is available rather than silently skipping.
