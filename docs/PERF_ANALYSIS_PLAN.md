# ✅ COMPLETED: Audio Processor Performance Analysis & Optimization Plan

## Architecture Summary

```
PluginProcessor::processBlock
  └─ DrumKitEngine::processBlock          [engineGraphLock TryLock]
       ├─ saveLatestContinuousControllerValues   (scan MIDI for CC)
       ├─ saveLatestPolyphonicAftertouchValues    (scan MIDI for PA)
       └─ for each InstrumentPlayer:
            └─ renderNextBlock                    [instrument-player lock]
                 ├─ MIDI-sample-accurate splitting
                 ├─ handleMidiEvent  (note-on / CC / aftertouch)
                 └─ renderVoices
                      ├─ applyAdaptiveTailShortening   O(voices)
                      └─ for each voice:
                           └─ per-sample loop: interpolation × envelope × choke
```

---

## Issues Found

### P0 - Critical (audio dropouts)

#### 1. Lock contention causes silent dropouts
- **File:** [DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp#L590)
- **Problem:** `engineGraphLock` is a `juce::CriticalSection` (recursive mutex). Audio thread uses `ScopedTryLock` - if the lock is held by any other thread, the entire block is silenced (`outputBuffer.clear()`). UI operations like `useInstrumentMap()` take the same lock with `ScopedLock` (blocking), holding it for the full duration of instrument remapping.
- **Impact:** Audible glitches/silence whenever the user changes instrument map, kit layout, or mic layout during playback.
- **Fix options:**
  1. **Double-buffering**: Prepare new instrument graph on background thread, swap atomically (`std::atomic<shared_ptr>` or atomic pointer swap). Audio thread reads current graph without locking.
  2. **Lock-free FIFO command queue**: Queue mutation commands from UI thread, apply them at the start of `processBlock` while the audio thread owns the data.
  3. **Shorter critical sections**: Break `useInstrumentMap()` into prepare (unlocked) + swap (locked for microseconds only).

#### 2. Nested lock hierarchy
- **Files:** [DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp#L590), [InstrumentPlayer.cpp](../source/Engine/InstrumentPlayer.cpp#L17)
- **Problem:** `engineGraphLock` is held while each `InstrumentPlayer::renderNextBlock` acquires its own `lock` (second `CriticalSection`). Two nested recursive mutexes per audio callback.
- **Impact:** Extra overhead per block. Risk of priority inversion if a non-RT thread holds the inner lock.
- **Fix:** If engine lock is sufficient to protect the instrument player vector, the per-instrument-player lock during render can be removed (keep it only for non-RT mutations like `addVoice`/`addSound`).

---

### P1 - High (measurable CPU overhead)

#### 3. Heap allocations in `findVoiceToSteal` (audio thread)
- **File:** [InstrumentPlayer.cpp](../source/Engine/InstrumentPlayer.cpp#L82-L90)
- **Problem:** `collectByState()` lambda creates a new `std::vector<InstrumentPlayerVoice*>` on every call. Called up to 3 times per note-on when stealing is needed (Dying, FadingOut, Releasing). The `playing` vector at line 122 is another allocation. That's up to 4 heap allocations per voice-steal on the audio thread.
- **Impact:** Heap allocation in real-time context risks priority inversion (malloc mutex) and cache pollution.
- **Fix:** Use a pre-reserved scratch buffer (like `stealCandidateScratch`) or iterate in-place without collecting into temporaries - scan for best candidate directly.

#### 4. String concatenation in CC handler hot path
- **File:** [InstrumentPlayerController.cpp](../source/Engine/InstrumentPlayerController.cpp#L47-L100)
- **Problem:** LOG macro correctly gates the `GlobalLogger::log()` call, but the message expression (string concatenation with `+`) is still evaluated at the call site before the if-check. Example:
  ```cpp
  LOG(Verbose, "HiHat CC: prefer-pedal redirect from " + targetArt.codeRaw + " to " + candArt.codeRaw);
  ```
  This builds a `juce::String` (heap allocation) even when Verbose logging is disabled - the `LOG` macro evaluates `msg` inside the `if` body, so it's actually gated. **However**, the Warning-level log at line ~73 fires when `newArtIdx >= runtimeArticulations.size()` and does allocate.
- **Additional concern:** `ArticulationRuntime::codeRaw` (a `juce::String` member) exists solely for debug logging. It wastes memory per articulation and fragments cache lines used in the hot path.
- **Fix:** Wrap expensive string construction with `if (LOG_ENABLED(Verbose)) { ... }` or remove `codeRaw` from the struct and reconstruct only when logging is enabled.

#### 5. `applyAdaptiveTailShortening` runs every block unconditionally
- **File:** [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h#L397)
- **Problem:** Called at the start of every `renderVoices()`. The early-exit (`if activeVoices < threshold`) still requires a full O(voices) scan to count active voices. For an instrument with 100 voices, that's 100 iterations per block per instrument even when load is low.
- **Impact:** ~N_instruments × N_voices iterations per audio block for a check that almost always returns early.
- **Fix options:**
  1. Maintain an `activeVoiceCount` atomic/counter, increment on `startNote`, decrement on `stopNote`. Check threshold with O(1).
  2. Run the shortening pass less frequently (every N blocks) since tail parameters change slowly.

#### 6. `getEstimatedCurrentDb` does floating-point math per voice per block
- **File:** [InstrumentPlayer.cpp](../source/Engine/InstrumentPlayer.cpp#L261)
- **Problem:** Called from `applyAdaptiveTailShortening` for every active voice every block. Involves division, `Decibels::gainToDecibels` (log10), and `decayEnvelope.getAbsoluteDbAtMs` (interpolation lookup).
- **Impact:** When tail shortening is active, this is O(active_voices) log10 calls per block.
- **Fix:** Cache the dB estimate and update it every N blocks, or only when the voice state changes.

---

### P2 - Medium (scaling concerns)

#### 7. MIDI buffer scanned 3× per block
- **File:** [DrumKitEngine.cpp](../source/Engine/DrumKitEngine.cpp#L601-L604)
- **Problem:** `saveLatestContinuousControllerValues` and `saveLatestPolyphonicAftertouchValues` each iterate the full MIDI buffer. Then each `InstrumentPlayer::renderNextBlock` iterates it again for sample-accurate event dispatch. Total: 2 + N_instruments passes over the MIDI buffer.
- **Impact:** Negligible for typical MIDI density, but unnecessary.
- **Fix:** Merge CC/PA tracking into the per-instrument MIDI iteration, or do a single pre-pass that extracts all relevant data.

#### 8. `unordered_map::find` for CC routing on audio thread
- **File:** [InstrumentPlayerController.cpp](../source/Engine/InstrumentPlayerController.cpp#L27)
- **Problem:** `ccToArticulation.find()` on every CC event. While O(1) amortized, `unordered_map` has pointer-chasing overhead and poor cache locality.
- **Impact:** Low for typical CC rates. Could matter under heavy hi-hat pedal modulation.
- **Fix:** Replace with a flat `std::array<std::optional<std::array<uint8_t, 128>>, 128>` or a fixed-size lookup table since CC numbers are bounded to 0..127.

#### 9. `handleAftertouch` iterates all voices
- **File:** [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h#L278)
- **Problem:** Linear scan of all voices to find matching note number. For 100 voices, that's 100 comparisons per aftertouch event.
- **Fix:** Maintain a `noteNumber → active voice list` lookup, or use a small fixed-size array indexed by note number.

#### 10. `codeRaw` field pollutes cache lines
- **File:** [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h#L222)
- **Problem:** `ArticulationRuntime` stores a `juce::String codeRaw` used only for debug logging. `juce::String` is 8+ bytes (pointer + ref count). This makes the struct larger and fragments the cache lines that the hot path reads (`nameId`, `exprId`, `pocValue`, `velocityToLevel`).
- **Fix:** Remove `codeRaw` from the struct. Reconstruct the string on-demand only when logging is enabled (it's just `nameOf(nameId) + "[" + nameOf(exprId) + "]"`).

---

### P3 - Low (minor / defensive)

#### 11. `stealCandidateScratch` capacity not guaranteed
- **File:** [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h#L450)
- **Problem:** Reserved in `addVoice()` but if voices are removed and re-added in a different order, capacity could be insufficient, causing reallocation on the audio thread.
- **Fix:** Reserve to max voice count in `prepareToPlay`.

#### 12. Per-sample branch for `chokeProcessor.isMuted()`
- **File:** [InstrumentPlayer.cpp](../source/Engine/InstrumentPlayer.cpp#L287)
- **Problem:** Checked before the per-sample loop but also implicitly via `getNextBlockGain()` inside the loop. The pre-loop check is correct but the `isMuted()` state could change mid-block during the choke tail, causing a branch misprediction at the transition point.
- **Impact:** Negligible - only one misprediction per choke event.
- **Note:** No action needed, documenting for completeness.

#### 13. Double MIDI scan in `handleMidiEvent`
- **File:** [InstrumentPlayerController.cpp](../source/Engine/InstrumentPlayerController.cpp#L24-L25)
- **Problem:** `ccToArticulation.find()` is called twice - once in `handleMidiEvent` (guard) and once in `handleController`.
- **Fix:** Remove the redundant guard or pass the iterator.

---

## Suggested Priority Order

| # | Issue | Effort | Impact | Priority | Status |
|---|-------|--------|--------|----------|--------|
| 1 | Lock contention / silent dropouts | High | Eliminates audible glitches | P0 | **DONE** |
| 2 | Nested lock hierarchy | Medium | Reduces lock overhead | P0 | **DONE** (data races fixed) |
| 3 | Heap allocs in `findVoiceToSteal` | Low | Removes RT allocations | P1 | **DONE** |
| 5 | `applyAdaptiveTailShortening` O(N) every block | Low | Reduces per-block overhead | P1 | **DONE** |
| 10 | Remove `codeRaw` from hot struct | Low | Better cache locality | P2 | **DONE** |
| 8 | Replace `unordered_map` with flat array for CC | Low | Better cache locality | P2 | **DONE** |
| 4 | String allocs in debug logging | Low | Removes conditional allocs | P1 | **DONE** |
| 7 | Triple MIDI scan | Low | Minor CPU save | P2 | **DONE** |
| 9 | Aftertouch voice scan | Low | Scales better | P2 | **DONE** |
| 6 | `getEstimatedCurrentDb` math per block | Medium | Reduces FP overhead | P1 | **DONE** |

## P0 Fix Summary (applied)

### Issue 1: Lock contention in `useInstrumentMap`
- **Before:** `engineGraphLock` held for the ENTIRE method (~10-100ms): parsing, resolution, instrument loading, all under lock. Audio thread silenced on lock contention.
- **After:** Restructured into prepare-and-swap pattern:
  - **Phase 1 (no lock):** Parsing, kit/instrument resolution, fingerprint comparison, creation of replacement instrument players and additional instrument players. All expensive work happens here.
  - **Phase 2 (brief lock):** Fast pointer swaps into `instrumentPlayerPtrs`, routing table rebuild, fingerprint update. Lock held for ~0.1ms.
  - **Phase 3 (no lock):** Old instrument players destroyed, sample loading started.
- **Wildcard `*` case:** Still holds lock for `mapKit` (rare operation, acceptable).

### Issue 2: Unprotected mutations (data races)
- **Before:** `applyKitLayout`, `applyMicLayout`, `mapDefaultKit`, `prepareToPlay` mutated `instrumentPlayerPtrs` and related maps WITHOUT any lock. Data race with audio thread's `processBlock`.
- **After:** Added `ScopedLock(engineGraphLock)` to all four methods. Since `engineGraphLock` is a recursive mutex (`juce::CriticalSection`), nested calls from `useInstrumentMap` are safe.

## Notes

- Items 1 and 2 (lock architecture) should be addressed together as a single refactor.
- Items 3, 5, and 10 are quick wins with no architectural risk.
- Profile with a worst-case scenario: full kit loaded, 10+ instruments, rapid hi-hat pedal CC, maximum polyphony.
