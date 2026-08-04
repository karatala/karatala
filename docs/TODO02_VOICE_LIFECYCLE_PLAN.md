# ✅ COMPLETED: TODO02: Voice Lifecycle Unification & Decay-Aware Voice Culling - Detailed Implementation Plan

## Goal
1. **Unify** the scattered voice-stopping mechanisms (`stopNote`, `fadeOutSec`, `setTailShorteningFactor`, `applyAdaptiveTailShortening`, `AftertouchChokeProcessor`) into a coherent, single-responsibility architecture with consistent ADSR handling.
2. **Implement decay-aware voice culling**: when a new note starts and older voices for the same mic group are still playing, use the `DecayEnvelope` data to determine if the old voice is already ~12dB+ below the new hit's peak - if so, immediately stop it instead of wasting CPU rendering inaudible audio.
3. **Fix all `//FIXME ADSR` sites** where ADSR parameters are set inconsistently (different sustain values, different parameter orderings).

---

## Current State Audit

### Voice-Stopping Methods (5 distinct paths)

| Method | Called From | What It Does | ADSR Params Set |
|--------|-----------|--------------|-----------------|
| `stopNote(true)` | `allNotesOff`, `stopVoice` | Sets ADSR to release phase; voice stays active until ADSR inactive | `{attack, 0, 0, releaseTimeSec}` |
| `stopNote(false)` | `startVoice` (if re-used), `renderNextBlock` (end of sample / ADSR done), `chokeProcessor.isMuted()` | Resets ADSR, calls `clearCurrentNote()` - voice freed immediately | ADSR reset (no params) |
| `fadeOutSec(sec)` | `fadeOutExistingVoices`, `handleController` (pedal crossfade) | Sets very short release, triggers `noteOff` - voice fades over `sec` | `{attack, 0, 0, releaseSec}` |
| `setTailShorteningFactor(f)` | `applyAdaptiveTailShortening` | Reduces release time of already-playing voice to shorten tail | Modifies only `p.release` |
| `chokeProcessor` | `renderNextBlock` → `getNextBlockGain()` → ducking/muting | External gain multiplier; when `isMuted()` → `stopNote(false)` | No ADSR change |

### FIXME ADSR Sites (6 locations)

1. **[InstrumentPlayer.cpp:201](../source/Engine/InstrumentPlayer.cpp#L201)** - `handleController` pedal replacement:
   ```cpp
   newVoice->adsr.setParameters({fadeInSec, 0.0f, 1.0f, newVoice->releaseTimeSec}); //FIXME ADSR
   ```
   **Issue**: Sustain = 1.0f, but `startNote` uses 0.5f and `stopNote`/`fadeOutSec` use 0.0f. Three different sustain values.

2. **[InstrumentPlayer.cpp:503](../source/Engine/InstrumentPlayer.cpp#L503)** - `startNote`:
   ```cpp
   this->adsr.setParameters({ this->attackTimeSec, 0.0f, 0.5f, this->releaseTimeSec }); //FIXME ADSR
   ```
   **Issue**: Sustain = 0.5f. For a one-shot sample player, sustain should arguably be 1.0f (sample plays at full level between attack and release) or the ADSR should be a simpler envelope.

3. **[InstrumentPlayer.cpp:523](../source/Engine/InstrumentPlayer.cpp#L523)** - `fadeOutSec`:
   ```cpp
   this->adsr.setParameters({ this->attackTimeSec, 0.0f, 0.0f, releaseSec }); //FIXME ADSR
   ```
   **Issue**: Sustain = 0.0f, which means ADSR drops to 0 after attack+decay then starts release from 0 - effectively a very different shape. Works by accident because `noteOff()` is called immediately so release starts from wherever the envelope currently is.

4. **[InstrumentPlayer.cpp:530](../source/Engine/InstrumentPlayer.cpp#L530)** - `stopNote(true)`:
   ```cpp
   this->adsr.setParameters({ this->attackTimeSec, 0.0f, 0.0f, this->releaseTimeSec }); //FIXME ADSR
   ```
   Same issue as #3.

5. **[InstrumentPlayer.h:100](../source/Engine/InstrumentPlayer.h#L100)** - `setTailShorteningFactor`:
   ```cpp
   this->adsr.setParameters(p); //FIXME ADSR
   ```
   **Issue**: Modifies only `p.release` but reads existing params which might already be in an inconsistent state from one of the above calls.

6. **[handleController pedal replacement](../source/Engine/InstrumentPlayer.cpp#L201)** - duplicate of #1.

### Root Cause of ADSR Inconsistency
The JUCE `ADSR` envelope is being used as both:
- A **one-shot playback envelope** (attack → sustain-hold → release-on-stop)
- A **fade-out controller** (change params mid-playback to force rapid release)
- A **tail shortener** (change release mid-playback)

These use-cases need different sustain/decay values, leading to the mess.

---

## Current Voice Stealing Logic

### `findVoiceToSteal()` ([InstrumentPlayer.cpp:399](../source/Engine/InstrumentPlayer.cpp#L399))
1. Filters candidates by `hitGroupId == groupId` (same mic buffer channel)
2. Protects the **loudest** voice (highest `getCurrentAmplitude()`)
3. Among non-protected: picks lowest `score = amplitude + ageFactor`
4. Falls back to protected voice if no other candidate

### `fadeOutExistingVoices()` ([InstrumentPlayer.cpp:313](../source/Engine/InstrumentPlayer.cpp#L313))
Called from `startVelocityLevelVoices()` before starting new voices for a note:
- Only fades voices with **same articulation** (same `nameId`/`exprId`) and **same note**
- Only if new velocity >= old velocity - 0.05
- Uses `POLYPHONY_CROSFADE_TIMEOUT_SEC` (100ms)

### `applyAdaptiveTailShortening()` ([InstrumentPlayer.h:354](../source/Engine/InstrumentPlayer.h#L354))
Called every render cycle from `renderVoices()`:
- When active voices exceed `INSTRUMENT_TAIL_SHORTEN_LOAD_THRESHOLD` (50%) of max
- Shortens release of older, quieter voices
- Protects voices above `INSTRUMENT_TAIL_SHORTEN_AMP_PROTECT` (0.6)

### Problems with Current Approach
1. **No cross-articulation awareness**: When playing edge[close] then edge[open] on same note, old voices are not faded because `fadeOutExistingVoices` requires matching `nameId`+`exprId`
2. **No decay-level awareness**: A voice that is already -50dB below its peak wastes CPU rendering inaudible samples
3. **hitGroupId-only stealing**: Only considers mic group, not actual audio level
4. **No mic-coherent stopping**: If a hit starts 3 mic voices (dtm, ons, rfs), stopping decisions are made per-voice independently - could stop one mic but not another

---

## Proposed Architecture

### Part A: Unified Voice Envelope Model

Replace the ad-hoc ADSR parameter patching with a clear, state-driven approach:

```cpp
class InstrumentPlayerVoice {
public:
    // Voice envelope states - explicit, no ambiguity
    enum class EnvelopeState {
        Idle,           // voice not active
        Playing,        // normal playback (attack → hold at full level)
        FadingOut,      // triggered crossfade/replacement (short configurable release)
        Releasing,      // triggered by stopNote(true) (normal release)
        Dying           // below audibility threshold, will be freed next cycle
    };

private:
    EnvelopeState envelopeState = EnvelopeState::Idle;
    float fadeOutTimeSec = 0.0f;  // when FadingOut, how fast to fade
```

**Unified ADSR configuration** - one canonical set of parameters, set once at `startNote()`:
```cpp
void InstrumentPlayerVoice::startNote(int midiNoteNumber, float velocity, SampledSound* sound) {
    this->velocityGain = velocity;
    this->sourceSamplePosition = 0.0;
    this->envelopeState = EnvelopeState::Playing;

    // Canonical ADSR: attack ramp → sustain at 1.0 (full sample level) → release on stop
    this->adsr.setParameters({ this->attackTimeSec, 0.0f, 1.0f, this->releaseTimeSec });
    this->adsr.setSampleRate(this->getSampleRate());
    this->adsr.noteOn();
    this->tailShortenFactor = 1.0f;
    // ...
}
```

**All other methods delegate through state transitions:**

```cpp
void InstrumentPlayerVoice::requestFadeOut(float durationSec) {
    if (this->envelopeState == EnvelopeState::Idle) return;
    this->envelopeState = EnvelopeState::FadingOut;
    const float releaseSec = juce::jmax(0.001f, durationSec);
    // Only modify release, keep sustain at 1.0 so release starts from current level
    juce::ADSR::Parameters p = this->adsr.getParameters();
    p.release = releaseSec;
    this->adsr.setParameters(p);
    this->adsr.noteOff();
}

void InstrumentPlayerVoice::requestRelease() {
    if (this->envelopeState == EnvelopeState::Idle) return;
    this->envelopeState = EnvelopeState::Releasing;
    juce::ADSR::Parameters p = this->adsr.getParameters();
    p.release = this->releaseTimeSec * this->tailShortenFactor;
    this->adsr.setParameters(p);
    this->adsr.noteOff();
}

void InstrumentPlayerVoice::killImmediately() {
    this->envelopeState = EnvelopeState::Idle;
    this->adsr.reset();
    this->clearCurrentNote();
}
```

**Migration of `stopNote(bool)`:**
```cpp
void InstrumentPlayerVoice::stopNote(bool allowTailOff) {
    if (allowTailOff)
        this->requestRelease();
    else
        this->killImmediately();
}
```

**Migration of `fadeOutSec(float)`:**
```cpp
void InstrumentPlayerVoice::fadeOutSec(float sec) {
    this->requestFadeOut(sec);
}
```

This eliminates all `//FIXME ADSR` - there's exactly **one place** ADSR params are fully set (`startNote`), and transitions only modify `release`.

### Part B: Decay-Aware Voice Culling

#### Concept
When a new note triggers, before starting voices, check all active voices for the same instrument. For each old voice, use the `DecayEnvelope` to estimate its current absolute dB level. If it's already `VOICE_CULL_THRESHOLD_DB` (configurable; default ~12dB) below the new hit's peak level, stop the old voice immediately - it's already masked by the new sound.

#### Why This Helps
Consider a drummer playing fast closed hi-hats: each hit starts 3-6 mic voices. Without culling, after 5 hits there are 15-30 active voices, most rendering inaudible tails. With culling, old voices are freed as soon as they're masked, keeping active counts at ~6-9.

#### Per-Voice Estimated Level
Each voice already has:
- `sourceSamplePosition` - current playback position
- `velocityGain` - velocity scaling factor
- `SampledSound* currentlyPlayingSound` → `decayEnvelope` (from TODO01)

Estimated current absolute dB:
```
elapsedMs = (sourceSamplePosition / pitchRatio / sampleRate) * 1000.0
absDb = decayEnvelope.getAbsoluteDbAtMs(elapsedMs) + 20*log10(velocityGain)
```

#### Algorithm (inline in `startVelocityLevelVoices` or new method)

```cpp
void InstrumentPlayer::cullMaskedVoices(int midiNoteNumber, float newVelocity) {
    // Estimate new hit's peak level for comparison
    // We use the loudest mic's peak as reference since that's the dominant audible component
    const float newVelDb = 20.0f * std::log10(juce::jmax(0.001f, newVelocity));

    for (auto* voice : this->voices) {
        if (!voice->isVoiceActive()) continue;
        if (voice->envelopeState == EnvelopeState::Dying) continue;

        auto* oldSound = voice->getCurrentlyPlayingSound();
        if (oldSound == nullptr) continue;

        const auto& oldEnv = oldSound->decayEnvelope;
        if (!oldEnv.isValid()) continue;

        const double oldSrcRate = oldSound->sampleRate > 0.0 ? oldSound->sampleRate : 44100.0;
        const float elapsedMs = static_cast<float>((voice->sourceSamplePosition / oldSrcRate) * 1000.0);
        const float oldAbsDb = oldEnv.getAbsoluteDbAtMs(elapsedMs) + 20.0f * std::log10(juce::jmax(0.001f, voice->velocityGain));

        // New hit peak (estimated from the new articulation's decay envelope peak + velocity)
        // For simplicity, use oldEnv.peakDb as proxy if same instrument
        const float newPeakDb = oldEnv.peakDb + newVelDb; // approximate

        if (oldAbsDb < newPeakDb - VOICE_CULL_THRESHOLD_DB) {
            // Old voice is already masked by new hit - free it
            voice->killImmediately();
        }
    }
}
```

**Tuning constant:**
```cpp
static constexpr float VOICE_CULL_THRESHOLD_DB = 12.0f; // stop voice if this many dB below new peak
```

#### Mic-Coherent Culling
Old voices for the same note-on event share the same `noteOnTime`. When culling one mic voice, cull **all** voices with the same `noteOnTime` to keep mic groups consistent:

```cpp
// After deciding to cull:
const auto targetNoteOnTime = voice->noteOnTime;
for (auto* v : this->voices) {
    if (v->noteOnTime == targetNoteOnTime && v->isVoiceActive()) {
        v->killImmediately();
    }
}
```

### Part C: Improved Voice Stealing

Replace the current amplitude+age scoring with decay-envelope-aware scoring:

```cpp
InstrumentPlayerVoice* InstrumentPlayer::findVoiceToSteal(int midiChannel, int midiNoteNumber, int groupId) const {
    // 1. Prefer voices in Dying state (about to be freed anyway)
    // 2. Prefer voices in FadingOut state
    // 3. Prefer voices in Releasing state
    // 4. Among Playing voices: steal the one with lowest estimated dB from decay envelope
    //    (not just ADSR amplitude, which doesn't understand the sample content)
    // 5. Protect the single loudest Playing voice
}
```

The key insight: `getCurrentAmplitude()` currently returns `lastEnvelopeValue * velocityGain`, which is the ADSR gain - NOT the actual perceived level. With decay data, we can compute true estimated level.

Add to `InstrumentPlayerVoice`:
```cpp
float getEstimatedCurrentDb() const {
    auto* s = this->currentlyPlayingSound;
    if (s == nullptr || !s->decayEnvelope.isValid()) {
        // fallback to current ADSR-based amplitude
        return 20.0f * std::log10(juce::jmax(0.0001f, this->getCurrentAmplitude()));
    }
    const double srcRate = s->sampleRate > 0.0 ? s->sampleRate : 44100.0;
    const float elapsedMs = static_cast<float>((this->sourceSamplePosition / srcRate) * 1000.0);
    return s->decayEnvelope.getAbsoluteDbAtMs(elapsedMs)
         + 20.0f * std::log10(juce::jmax(0.001f, this->velocityGain));
}
```

### Part D: Fix the Open Hi-Hat Choke Problem

From `playNote()`:
```cpp
//FIXME: if playing more closed hihat articulation right after more opened, opened keep sounding.
```

This is because `fadeOutExistingVoices()` only affects voices with **matching** `nameId`/`exprId`. Cross-articulation choke (open → closed) is not handled.

**Fix**: For instrument types that have a natural choke relationship (hi-hat: more-closed chokes more-open), `fadeOutExistingVoices` should also fade out voices from **any** articulation of the same instrument playing on the same note:

```cpp
void InstrumentPlayer::fadeOutExistingVoices(const ArticulationRuntime& art, int midiChannel, int midiNoteNumber, float newVelocity) {
    for (auto* voice : this->voices) {
        if (!voice || !voice->isVoiceActive()) continue;
        if (voice->getCurrentlyPlayingNote() != midiNoteNumber) continue;
        if (!voice->isPlayingChannel(midiChannel)) continue;

        // Same articulation: polyphony crossfade (existing behavior)
        if (voice->artNameId == art.nameId && voice->artExprId == art.exprId) {
            if (newVelocity >= voice->getVelocityGain() - 0.05f)
                voice->requestFadeOut(POLYPHONY_CROSFADE_TIMEOUT_SEC);
            continue;
        }

        // Cross-articulation: for hi-hat, any new hit should choke old articulations
        if (this->type == KitLib::InstrumentSpec::Type::HiHat) {
            voice->requestFadeOut(POLYPHONY_CROSFADE_TIMEOUT_SEC);
        }
    }
}
```

---

## Detailed ADSR Fix Plan

All `//FIXME ADSR` sites should be resolved by the unified model:

| Site | Current | After Unification |
|------|---------|-------------------|
| `startNote` (line 503) | `{attack, 0, 0.5, release}` | `{attack, 0, 1.0, release}` - sustain=1.0 always |
| `fadeOutSec` (line 523) | Full params `{attack, 0, 0, releaseSec}` | `requestFadeOut(sec)` - only modifies `p.release` |
| `stopNote(true)` (line 530) | Full params `{attack, 0, 0, release}` | `requestRelease()` - only modifies `p.release` |
| `setTailShorteningFactor` (line 100) | Modifies `p.release` then sets all | Same - but now safe because sustain is always 1.0 |
| `handleController` replacement (line 201) | `{fadeIn, 0, 1.0, release}` | No manual ADSR set - `startNote` handles it, then `requestFadeOut` for the old voice |

**Key principle**: `adsr.setParameters()` is called in exactly **two** places:
1. `startNote()` - canonical `{attack, 0, 1.0, release}`
2. Transition methods (`requestFadeOut`, `requestRelease`, `setTailShorteningFactor`) - modify only `p.release`

---

## Implementation Steps

### Step 1: Add `EnvelopeState` to `InstrumentPlayerVoice`

**File**: [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h)

Add enum and state field. Add `getEstimatedCurrentDb()`. This is purely additive, no breakage.

### Step 2: Add Transition Methods

**File**: [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h) + [InstrumentPlayer.cpp](../source/Engine/InstrumentPlayer.cpp)

Add `requestFadeOut()`, `requestRelease()`, `killImmediately()`. Keep `stopNote` and `fadeOutSec` as thin wrappers for backward compatibility initially.

### Step 3: Normalize ADSR Parameters

**File**: [InstrumentPlayer.cpp](../source/Engine/InstrumentPlayer.cpp)

- `startNote()`: change sustain from 0.5 to 1.0
- `fadeOutSec()`: delegate to `requestFadeOut()`
- `stopNote(true)`: delegate to `requestRelease()`
- `stopNote(false)`: delegate to `killImmediately()`
- Remove all `//FIXME ADSR` comments after fixing

### Step 4: Fix `setTailShorteningFactor`

**File**: [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h#L92)

Now safe since sustain is consistently 1.0. Only modifies release. Remove `//FIXME ADSR`.

### Step 5: Fix handleController Pedal ADSR

**File**: [InstrumentPlayer.cpp](../source/Engine/InstrumentPlayer.cpp#L201)

Remove manual ADSR `setParameters` call after `startVoice` - `startNote` already sets canonical params. The fade-in is handled by the attack time. Remove `//FIXME ADSR`.

### Step 6: Add `cullMaskedVoices()`

**File**: [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h) (declaration) + [InstrumentPlayer.cpp](../source/Engine/InstrumentPlayer.cpp) (definition)

New method called from `startVelocityLevelVoices()` before allocating new voices. Uses `DecayEnvelope` to check if old voices are already masked.

### Step 7: Mic-Coherent Culling

In `cullMaskedVoices()`: when deciding to cull a voice, also cull all voices sharing the same `noteOnTime` (same hit event = same mic group).

### Step 8: Improve `findVoiceToSteal()`

**File**: [InstrumentPlayer.cpp](../source/Engine/InstrumentPlayer.cpp#L399)

Add envelope-state-aware priority ordering:
1. `Dying` → immediate take
2. `FadingOut` → prefer over playing
3. `Releasing` → prefer over playing
4. `Playing` → use `getEstimatedCurrentDb()` instead of `getCurrentAmplitude()`

### Step 9: Fix Cross-Articulation Choke (Hi-Hat)

**File**: [InstrumentPlayer.cpp](../source/Engine/InstrumentPlayer.cpp#L313)

Expand `fadeOutExistingVoices()` to handle cross-articulation choke for hi-hat type instruments. Remove the `//FIXME` comment in `playNote()`.

### Step 10: Refine `applyAdaptiveTailShortening()`

**File**: [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h#L354)

Now that voices have `EnvelopeState`, skip voices already in `FadingOut`/`Releasing`/`Dying`. Use `getEstimatedCurrentDb()` instead of `getCurrentAmplitude()` for the protection threshold - a voice at 0.6 ADSR amplitude but -50dB decay level should NOT be protected.

---

## File Change Summary

| File | Changes |
|------|---------|
| [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h) | Add `EnvelopeState` enum, state field. Add `requestFadeOut()`, `requestRelease()`, `killImmediately()`, `getEstimatedCurrentDb()`. Fix `setTailShorteningFactor`. |
| [InstrumentPlayer.cpp](../source/Engine/InstrumentPlayer.cpp) | Implement transition methods. Normalize `startNote` ADSR. Redirect `fadeOutSec`/`stopNote`. Add `cullMaskedVoices()`. Fix `fadeOutExistingVoices` for cross-articulation. Improve `findVoiceToSteal`. Remove all `//FIXME ADSR`. |
| [InstrumentPlayer.h](../source/Engine/InstrumentPlayer.h) | Add `VOICE_CULL_THRESHOLD_DB` constant. Refine `applyAdaptiveTailShortening`. |

---

## Dependencies

- **Depends on TODO01**: `DecayEnvelope` struct and `SampledSound.decayEnvelope` must be in place for decay-aware culling (Steps 6-8). Steps 1-5, 9 can proceed independently.
- Steps 1-5 (ADSR unification) have **no dependencies** and can be done first.

---

## Performance Considerations

| Operation | Cost | Frequency | Impact |
|-----------|------|-----------|--------|
| `getEstimatedCurrentDb()` | 1 float division + `getAbsoluteDbAtMs` (≤10 comparisons) + 1 log10 | Per voice per note-on event | Negligible - note-on is rare vs. per-sample |
| `cullMaskedVoices()` | Iterates all voices (typically 10-60) × 1 dB lookup each | Per note-on | Negligible |
| `noteOnTime` matching for mic-coherent cull | Linear scan of voices | Per cull decision | Negligible |
| Saved CPU from removed inaudible voices | Full sample render per voice (~3-6 voices per old hit) | Continuous savings per render block | **Significant gain** - main benefit |

### Pre-calculation at Load Time (from TODO01)
- `DecayEnvelope` decoded from base62 once at instrument load
- Runtime lookups are pure float comparisons with 10-element arrays - no string parsing, no allocation

---

## Testing Strategy

### Unit Tests
1. **`EnvelopeState` transitions**: verify `Playing → FadingOut → Idle`, `Playing → Releasing → Idle`, `Playing → Dying → Idle`
2. **`getEstimatedCurrentDb()`**: with known `DecayEnvelope` + velocity, verify dB at specific positions
3. **ADSR consistency**: verify that After `startNote()`, ADSR params are always `{attack, 0, 1.0, release}` regardless of prior state

### Integration Tests
1. **Culling**: start 3 mic voices for a hi-hat hit, advance time until decay is -15dB below peak, trigger new hit, verify old voices are culled
2. **Cross-articulation choke**: play open hi-hat, then closed hi-hat on same note, verify open voices are faded
3. **Mic coherence**: verify that culling one mic voice of a hit also culls sibling mic voices
4. **Voice stealing**: with full voice pool, verify stealing prefers `Dying` > `FadingOut` > `Releasing` > lowest-dB `Playing`

### Listening Tests
1. Fast hi-hat patterns: verify no voice starvation / clicking from running out of voices
2. Ride cymbal washes: verify natural tail overlap without excessive CPU
3. A/B CPU load comparison: same MIDI pattern with and without decay-aware culling

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Sustain change from 0.5→1.0 alters sound | 0.5 sustain with 0 decay means ADSR immediately drops to 0.5 after attack - current sound was already affected. With sustain=1.0, sample plays at full level as intended. May need minor volume adjustment. |
| Aggressive culling removes audible tails | 12dB threshold is conservative. Tails 12dB below a new transient are psychoacoustically masked. Can tune threshold per-instrument-type if needed. |
| `noteOnTime` grouping assumes all mic voices start together | Currently true - `startVelocityLevelVoices` starts all mic voices for one note in a single loop. If async loading changes this, add an explicit `hitId` field. |
| `EnvelopeState` adds complexity | States are simple and linear (no cycles). Each state has exactly one transition to the next. `renderNextBlock` becomes cleaner with explicit state checks. |
| Samples without decay data skip culling | Graceful degradation - existing behavior preserved. Only samples with metadata benefit. |

---

## Implementation Order (suggested)

1. **Steps 1-2**: Add `EnvelopeState` + transition methods (additive, no behavior change)
2. **Step 3**: Normalize ADSR - fix sustain=0.5→1.0 in `startNote`, rewire `stopNote`/`fadeOutSec`
3. **Step 4-5**: Fix remaining FIXME ADSR sites
4. **Step 9**: Fix cross-articulation hi-hat choke (independent of decay data)
5. **Step 6-7**: Add `cullMaskedVoices()` with mic coherence (requires TODO01 complete)
6. **Step 8**: Improve `findVoiceToSteal` with envelope-state priority
7. **Step 10**: Refine `applyAdaptiveTailShortening` with estimated dB
8. End-to-end testing and tuning
