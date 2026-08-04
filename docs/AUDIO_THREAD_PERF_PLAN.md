# Audio Thread Performance Improvements Plan

Analysis of the full `processBlock` call chain:
`PluginProcessor::processBlock` → `DrumKitEngine::processBlock` → `InstrumentPlayer::renderNextBlock` → per-voice `renderNextBlock`, plus MIDI-triggered `handleMidiEvent` → `playNote` / `handleController` → voice start/crossfade paths.

---

## P0 - Heap Allocations on Every Note-On

**Files:** `InstrumentPlayerController.cpp`

### ✅ A. `chooseLogicalHit()` has no per-note allocation

`VelocityLevelRuntime` now owns explicit logical hits. `chooseLogicalHit()` counts ready logical hits and selects one without a temporary container.

### B. `cullMaskedVoices()` - `std::unordered_map` + `std::unordered_set` per note (line 624, 654)

```cpp
std::unordered_map<juce::uint32, float> oldGroupPeakDbByNoteOn;  // hash map, malloc
std::unordered_set<juce::uint32> noteOnTimesToCull;               // hash set, malloc
```

**Fix:** Voice count per instrument is bounded (typically 10-100). Use stack-allocated small buffers:
- A `static thread_local` or pre-allocated array of `{noteOnTime, peakDb}` pairs, scanned linearly (`O(n)` on ~10-50 entries is faster than hash map overhead).
- Or a small fixed-capacity flat map (e.g., `std::array<std::pair<uint32_t, float>, 128>` with a counter).

---

## P1 - Per-Sample Operations That Could Be Per-Block

**File:** `InstrumentPlayer.cpp` (renderNextBlock, line ~304)

### A. `AftertouchChokeProcessor::getNextBlockGain()` called per sample

Currently called inside the inner `for (int i = 0; i < numSamples; ++i)` loop. The choke processor does state-machine transitions with elapsed-time tracking at sample granularity, but the gain changes are smooth and slow (duck/release times are 10s-100s of ms).

**Fix:** Call `getNextBlockGain()` once per `renderNextBlock` call (or once per sub-block), not per sample. Alternatively, batch-advance by `numSamples` and return a single gain value (or a start/end pair for linear interpolation within the block).

### B. `juce::jlimit` on `destCh` per sample per channel

```cpp
int destCh = s->micBufferChannelStart + sc;
if (destCh >= 0 && destCh < destChCount) { ... }
```

This bounds check is fine, but the values are constant for the entire voice render call. Move the validation outside the sample loop and skip the voice entirely if out of range.

### C. Linear interpolation can use pointer arithmetic

Currently uses `buf.getSample(actualSrcCh, idx)` which does a function call. For the inner loop, cache `const float* srcPtr = buf.getReadPointer(actualSrcCh)` before the loop and index directly:
```cpp
float v0 = srcPtr[idx];
float v1 = (idx + 1 < srcLen) ? srcPtr[idx + 1] : 0.0f;
```
This avoids repeated `getReadPointer` + bounds-check overhead per sample.

---

## P2 - Lock Contention

**Files:** `DrumKitEngine.cpp`, `InstrumentPlayer.cpp`, `InstrumentPlayerController.cpp`

### A. `engineGraphLock` TryLock → audio dropout on failure (DrumKitEngine.cpp:690)

If the UI/message thread holds `engineGraphLock` during kit reload or instrument map changes, the audio thread clears the entire buffer - audible dropout.

**Fix options (pick one):**
1. **Double-buffer the instrument graph**: Swap atomically, audio thread always reads the current graph without locking.
2. **Move all graph mutations to a deferred queue**: Audio thread processes pending graph changes at block start from a lock-free FIFO.
3. **Shorten lock hold time**: Profile where `engineGraphLock` is held on the message thread and minimize scope.

### B. `InstrumentPlayer::lock` held for entire `renderNextBlock` (InstrumentPlayer.cpp:14)

The same lock is also acquired in `startVelocityLevelVoices` (line 521). Since `renderNextBlock` already holds it, the re-acquisition is actually a recursive lock on the same thread (through `handleMidiEvent` → `playNote` → `startVelocityLevelVoices`). Verify `juce::CriticalSection` is re-entrant (it is on JUCE), but this means any other thread trying to add/remove voices will block for the entire audio block duration.

**Fix:** Minimize lock scope. Voice rendering itself doesn't need the lock if voice pool is stable. Only lock around voice start/stop mutations, not the full render pass.

---

## P3 - String Operations in Audio Thread

**File:** `InstrumentPlayerController.cpp`

~12 `LOG(Verbose, ...)` calls in the audio thread path with heavy `juce::String` concatenation:

```cpp
LOG(Verbose, "InstrumentPlayer::playNote: note " + juce::String(midiNoteNumber) + ...);
LOG(Verbose, "PedalVoiceStart inst=" + this->getId() + " ch=" + ...);  // 12+ concatenations
LOG(Verbose, "startVoice note " + ... + " file=" + filePath);
```

The `LOG` macro does guard with `if (gAppLogLevel >= level)`, so strings are **not built when log level is below Verbose**. However:
- If log level is `Verbose` (e.g., during debugging), every note-on and CC event triggers multiple heap-allocating string builds on the audio thread.
- Some LOG calls use `LOG_ENABLED(Warning)` guard but then build the string inside with `LOG(Warning, ...)` - the string is still built inside the if-block.

**Fix:**
- For hot-path Verbose logs, consider a lock-free ring buffer approach: log fixed-size structs (note, velocity, artIdx) and format strings on a background thread.
- Or: accept current behavior since Verbose is debug-only, but add a comment documenting the audio-thread cost.

---

## P4 - Hi-Hat Pedal Transition CPU Spike

**File:** `InstrumentPlayerController.cpp` (handleController, line ~120-390)

When a CC value changes the hi-hat articulation, the code:
1. Iterates ALL active voices (~50-100) to find mismatched articulations
2. For each mismatched voice, searches `artIdByNameExprId` map (unordered_map lookup)
3. Computes decay envelope offset via `getAbsoluteDbAtMs` and `getMsAtAbsoluteDb`
4. **Reads a 1024-sample RMS window** from the target sample buffer (lines 272-300) - this is a per-voice inner loop scanning 1024 floats per source channel
5. Builds extensive log strings (12+ `juce::String` concatenations per replacement)

On a fast pedal move, multiple CC values arrive per block. Each one re-scans all voices.

**Fix:**
- **Debounce CC transitions**: Already partially done (early-return if artIdx unchanged). Could add rate-limiting - skip if last transition was < N ms ago.
- **Cache RMS validation**: Pre-compute "minimum valid offset" per sample at load time, avoiding the 1024-sample scan at note-on time.
- **Move log string building behind `LOG_ENABLED(Verbose)` guard**: Several log calls inside this function already use `LOG(Verbose, ...)` which is guarded, but the multi-line string concatenation (PedalVoiceStart, transition skipped/partial) could build `juce::String` variables before the LOG call unnecessarily. Ensure expensive variables (`oldArtCode`, `newArtCode`, `sampleName`) are only computed when logging is enabled.

---

## P5 - `updateEstimatedDb()` per Voice per Block

**File:** `InstrumentPlayer.cpp` (line ~363)

Called at the end of every `renderNextBlock` for every active voice. Involves:
- Division: `sourceSamplePosition / srcRate`
- `getAbsoluteDbAtMs()`: linear interpolation on decay envelope array
- `gainToDecibels()`: `log10` computation

**Fix:**
- **Lazy evaluation**: Only recompute when the value is actually needed (by `cullMaskedVoices`, `findVoiceToSteal`, `applyAdaptiveTailShortening`). Add a dirty flag set when `sourceSamplePosition` changes, compute on first read.
- **Reduce frequency**: Update every N blocks instead of every block (e.g., every 4 blocks = ~0.3ms at 512 samples/48kHz - still responsive enough for voice stealing decisions).

---

## P6 - Minor Optimizations

### A. `LogicalHitRuntime::isReady()` scans its samples during `chooseLogicalHit`
The scan is bounded by the mic count of one logical hit. A ready flag can replace repeated scans if profiling shows a useful gain.

### B. `resolveVelocityLevel` fallback path scans all velocity levels
When the preferred velocity level is not loaded, the function scans all velocity levels for the closest match. This is `O(levels)` per note. The impact is low because the level count is small, but the load step can precompute fallback order if profiling shows a useful gain.

### C. `findFreeVoice` → `findVoiceToSteal` linear scan
Already well-optimized (single-pass with state tracking). No action needed.

### D. `applyAdaptiveTailShortening()` iterates all voices twice per render call
First in `renderVoices` (to count active), then in `applyAdaptiveTailShortening` (to apply factors). Could merge into a single pass, but the early-return on load threshold makes the second pass conditional - likely fine.

---

## Summary - Priority Order

| Priority | Issue | Impact | Effort |
|----------|-------|--------|--------|
| **P0-A ✅** | `chooseLogicalHit` temporary vectors | Completed: no note-on allocation | Done |
| **P0-B** | `cullMaskedVoices` hash containers | Heap alloc per note (hi-hat) | Low |
| **P1-A** | Per-sample choke gain | Unnecessary per-sample work | Low |
| **P1-C** | `getSample` in inner loop | Function call overhead | Low |
| **P2-A** | `engineGraphLock` dropout | Audible glitch | High |
| **P4** | Hi-hat CC RMS scan | CPU spike on pedal | Medium |
| **P5** | `updateEstimatedDb` every block | Log10 per voice per block | Low |
| **P1-B** | Constant bounds check in loop | Minor branch overhead | Low |
| **P2-B** | Lock scope too wide | Potential contention | Medium |
| **P3** | Verbose log strings | Debug-mode only cost | Low |
| **P6** | Minor logical-hit and velocity-level scans | Negligible | Low |
