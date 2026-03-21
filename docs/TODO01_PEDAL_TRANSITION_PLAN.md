# TODO01: Hi-Hat Pedal Transition — Detailed Implementation Plan

## Goal
When the hi-hat pedal CC changes articulation while a sound is already playing (e.g., closed → half-open), start the new articulation's sample **at a time-offset that matches the current RMS level of the old sound**, instead of using a fixed 50ms offset. This produces a seamless, natural transition without an audible attack transient or unnatural choke+fade-in.

---

## Current Behavior (what happens today)

In `InstrumentSynth::handleController()` ([InstrumentSynth.cpp](../source/Engine/InstrumentSynth.cpp#L48)):

1. Pedal CC change detected → maps to new `ArticulationRuntime` index (e.g., `edge[close]` → `edge[half]`)
2. For each active voice whose articulation differs from the target:
   - Old voice fades out in **50ms** (`HIHAT_PEDAL_CROSSFADE_TIMEOUT_SEC`)
   - New voice starts with a **fixed ~50ms sample offset** and a 10ms fade-in ADSR
3. **Problem**: The 50ms offset is arbitrary. The new sample may be at a very different amplitude level than the old one at its current playback position, causing an audible volume discontinuity or unnatural attack.

---

## Data Available

### In Filenames (parsed at scan time, currently **discarded**)
Per-sample metadata parsed in `InstrumentSpec::parseSampleFile()` ([KitLibModel.cpp](../source/Engine/KitLibModel.cpp#L215)):
- `peakDb` (float): RMS peak level in dB (e.g., -15.76)
- `peakMs` (float): time of RMS peak in ms (e.g., 20)
- `decayMs` (float): total decay time to silence in ms
- `decayCode` (juce::String): **base62-encoded relative time intervals** from peak to -60dB in 6dB steps

### In `analysis.csv` (reference data, same format)
```
instr,art,vel,rr,mic, ..., rms_peak_ms, rms_peak_db, rms_decayed_ms -6dB,-12dB,-18dB,-24dB,-30dB,-36dB,-42dB,-48dB,-54dB,-60dB
hihat-X,edge[close],127,a,dtm,  ..., 20, -15.76, 80,120,150,170,190,210,240,270,320,400
hihat-X,edge[half],127,a,dtm,   ..., 50, -22.51, 200,290,440,700,910,1100,1220,1350,1410,1480
hihat-X,edge[open],127,a,dtm,   ..., 40, -22.52, 210,380,590,930,1680,2320,2800,3530,3760,4050
```

### Decay Code Encoding (from `analyze_drum_samples2.py`)
The `decayCode` is underscore-separated base62 values representing **delta intervals** in ms:
```
encode_time_intervals_as_base62([80, 120, 150, 170, 190, 210, 240, 270, 320, 400])
  → deltas = [80, 40, 30, 20, 20, 20, 30, 30, 50, 80]
  → base62 of each delta, joined by "_"
```
These 10 values represent absolute ms from note-on to crossing -6dB, -12dB, ..., -60dB below peak.

**Critical**: The CSV `rms_decayed_ms` columns are **absolute times from note-on** (not from peak). The `decayCode` stores these as **deltas** (first value absolute, subsequent relative to previous).

---

## Algorithm: RMS-Level-Matched Offset

### Working Example
Playing `edge[close]` vel=127 (dtm mic) for 200ms, pedal opens to `edge[half]`:

1. **Old sample elapsed**: 200ms since note-on
2. **Old sample current dB**: From decay table (absolute ms): `{-6dB@80, -12dB@120, -18dB@150, -24dB@170, -30dB@190, -36dB@210, ...}`
   - At 200ms: between -30dB@190 and -36dB@210 → interpolate: -30 + (-6) × (200-190)/(210-190) = **-33dB relative to peak**
   - Absolute dB: peakDb + relativeDb = -15.76 + (-33) = **-48.76 dB**
3. **Target sample**: `edge[half]` vel=127, dtm: peak=-22.51dB, decay table: `{-6dB@200, -12dB@290, ...,-60dB@1480}`
   - Need to find time when target reaches -48.76dB absolute
   - Target relative level needed: -48.76 - (-22.51) = **-26.25dB below target peak**
   - From target decay table: -24dB@700, -30dB@910 → interpolate: 700 + (910-700) × (26.25-24)/(30-24) = 700 + 210 × 0.375 = **~779ms**
4. **Start new sample at offset 779ms** (converted to sample position)

### Pseudocode
```
elapsedMs = (currentSamplePosition / sampleRate) * 1000.0

// 1. Get current absolute dB of old sample
oldRelativeDb = interpolateDecayTable(oldDecayTable, elapsedMs)  // returns dB below old peak (negative)
oldAbsoluteDb = oldPeakDb + oldRelativeDb

// 2. Find offset in new sample where it reaches same absolute dB  
newRelativeDb = oldAbsoluteDb - newPeakDb  // dB below new peak (negative)
newOffsetMs   = inverseDecayLookup(newDecayTable, newRelativeDb) // ms from note-on

// 3. Clamp: don't start past end of sample, don't start before peak
newOffsetMs = clamp(newOffsetMs, newPeakMs, sampleLengthMs)

// 4. Convert to sample position
newVoice->sourceSamplePosition = (newOffsetMs / 1000.0) * srcSampleRate
```

### Edge Cases
- **Old sound louder than new peak**: `newRelativeDb > 0` → start new sample at its peak (offset = peakMs). Apply short crossfade.
- **Old sound quieter than -60dB of new**: offset beyond sample length → don't start replacement (old is already inaudible relative to new)
- **No decay data available**: do **not** fall back to a fixed offset (the old 50ms approach was a complete failure). Instead, skip the replacement entirely — let the old voice continue playing naturally. Log a warning at load time for samples missing decay metadata so the user knows which files need re-analysis.
- **Different mic channels**: each mic has its own decay table. Use the **direct mic** (closest, usually `dtm`, or any other starting with `d`; fallback to **overhead mic** starting with `o`) for the level calculation since it has the cleanest envelope. Apply same time offset to all mic voices.

---

## Implementation Steps

### Step 1: Persist Decay Metadata in `SampleVariantSpec`

**File**: [KitLibModel.h](../source/Engine/KitLibModel.h#L89)

Add fields to `SampleVariantSpec`:
```cpp
// Parsed decay envelope metadata (populated from filename at scan time)
float peakDb = 0.0f;        // RMS peak level in dB
float peakMs = 0.0f;        // time of RMS peak in ms from note-on
float decayMs = 0.0f;       // total decay time in ms
juce::String decayCode;     // raw base62-encoded decay curve
```

**File**: [KitLibModel.cpp](../source/Engine/KitLibModel.cpp#L148) (inside `scanInstrumentSamples`)

After `parseSampleFile()` succeeds and before `getOrAddSampleVariant()`, transfer metadata:
```cpp
// After: velSpec->getOrAddSampleVariant(sampleFile, variantIdx, micDesc, channelCursor);
// Transfer parsed metadata to the sample variant spec
auto* sv = velSpec->getOrAddSampleVariant(sampleFile, variantIdx, micDesc, channelCursor);
sv->peakDb = parsedInfo.value.peakDb;
sv->peakMs = parsedInfo.value.peakMs;
sv->decayMs = parsedInfo.value.decayMs;
sv->decayCode = parsedInfo.value.decayCode;
```

### Step 2: Add Decoded Decay Table Structure

**New file or in KitLibModel.h** — a lightweight struct for the decoded decay envelope:

```cpp
/**
 * Decoded decay envelope for fast runtime lookup.
 * Stores absolute times (ms from note-on) at which the RMS level crosses
 * -6, -12, -18, -24, -30, -36, -42, -48, -54, -60 dB below peak.
 * Populated from decayCode (base62) at load time.
 */
struct DecayEnvelope {
    static constexpr int NUM_POINTS = 10;
    static constexpr float DB_STEP = -6.0f;  // each point is 6dB lower

    float peakDb = 0.0f;   // absolute peak RMS level in dB
    float peakMs = 0.0f;   // time of peak in ms
    // Absolute times (ms from note-on) for each -6dB crossing
    // points[0] = time to -6dB, points[1] = time to -12dB, etc.
    // -1.0f means this level was never reached (sample too short/quiet)
    std::array<float, NUM_POINTS> points = {};
    int validPoints = 0;   // number of valid (non -1) points

    bool isValid() const { return this->validPoints > 0 && this->peakDb < 0.0f; }

    /**
     * Given elapsed ms from note-on, returns current dB level (absolute).
     * Interpolates between decay breakpoints.
     */
    float getAbsoluteDbAtMs(float elapsedMs) const;

    /**
     * Given a target absolute dB level, returns the time (ms from note-on)
     * when the envelope reaches that level. Returns -1 if level is never reached.
     */
    float getMsAtAbsoluteDb(float targetAbsDb) const;

    /**
     * Decode a base62-encoded decay code string into this envelope.
     * Format: underscore-separated base62 deltas.
     * First value = absolute ms to -6dB, subsequent = delta from previous.
     */
    static DecayEnvelope decode(const juce::String& decayCode, float peakDb, float peakMs);
};
```

### Step 3: Base62 Decode Utility

**File**: `KitLibModel.h` or new utility header

```cpp
/**
 * Decode a single base62-encoded number.
 * Character set: 0-9 a-z A-Z (62 characters)
 */
static int base62ToInt(const juce::String& s) {
    static const char* digits = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int result = 0;
    for (int i = 0; i < s.length(); ++i) {
        result *= 62;
        auto c = s[i];
        const char* pos = std::strchr(digits, (char)c);
        if (pos != nullptr)
            result += static_cast<int>(pos - digits);
    }
    return result;
}
```

### Step 4: `DecayEnvelope::decode()` Implementation

```cpp
DecayEnvelope DecayEnvelope::decode(const juce::String& decayCode, float peakDb, float peakMs) {
    DecayEnvelope env;
    env.peakDb = peakDb;
    env.peakMs = peakMs;

    if (decayCode.isEmpty()) return env;

    juce::StringArray parts;
    parts.addTokens(decayCode, "_", "");

    float absoluteMs = 0.0f;
    for (int i = 0; i < parts.size() && i < NUM_POINTS; ++i) {
        int deltaMs = base62ToInt(parts[i]);
        if (i == 0)
            absoluteMs = static_cast<float>(deltaMs);
        else
            absoluteMs += static_cast<float>(deltaMs);
        env.points[i] = absoluteMs;
        env.validPoints = i + 1;
    }
    return env;
}
```

### Step 5: `DecayEnvelope::getAbsoluteDbAtMs()` and `getMsAtAbsoluteDb()`

```cpp
float DecayEnvelope::getAbsoluteDbAtMs(float elapsedMs) const {
    if (!this->isValid()) return -100.0f; // silence

    // Before first point: interpolate from peak (0dB relative) to -6dB
    if (elapsedMs <= this->peakMs) return this->peakDb;  // at or before peak

    // Relative dB = how far below peak
    // this->points[i] corresponds to (i+1)*6 dB below peak
    for (int i = 0; i < this->validPoints; ++i) {
        float pointMs = this->points[i];
        float pointRelDb = static_cast<float>((i + 1)) * DB_STEP; // -6, -12, -18, ...

        if (elapsedMs <= pointMs) {
            // Interpolate between previous point and this point
            float prevMs = (i == 0) ? this->peakMs : this->points[i - 1];
            float prevRelDb = (i == 0) ? 0.0f : static_cast<float>(i) * DB_STEP;
            float t = (elapsedMs - prevMs) / juce::jmax(1.0f, pointMs - prevMs);
            float relDb = prevRelDb + t * (pointRelDb - prevRelDb);
            return this->peakDb + relDb;
        }
    }
    // Beyond last valid point: extrapolate or return floor
    float lastDb = static_cast<float>(this->validPoints) * DB_STEP;
    return this->peakDb + lastDb;
}

float DecayEnvelope::getMsAtAbsoluteDb(float targetAbsDb) const {
    if (!this->isValid()) return -1.0f;

    float targetRelDb = targetAbsDb - this->peakDb; // dB below peak (negative)
    if (targetRelDb >= 0.0f) return this->peakMs;   // louder than peak → at peak time

    for (int i = 0; i < this->validPoints; ++i) {
        float pointRelDb = static_cast<float>(i + 1) * DB_STEP;
        if (targetRelDb >= pointRelDb) {
            // Between previous point and this one
            float prevMs = (i == 0) ? this->peakMs : this->points[i - 1];
            float prevRelDb = (i == 0) ? 0.0f : static_cast<float>(i) * DB_STEP;
            float t = (targetRelDb - prevRelDb) / (pointRelDb - prevRelDb);
            return prevMs + t * (this->points[i] - prevMs);
        }
    }
    return -1.0f; // target level is below our measured floor
}
```

### Step 6: Pre-build DecayEnvelope at Load Time

**Option A: Build on `SampleVariantSpec`** (model layer) — decode during `scanInstrumentSamples()`:
Add `DecayEnvelope decayEnvelope;` to `SampleVariantSpec`. After transferring metadata (Step 1):
```cpp
sv->decayEnvelope = DecayEnvelope::decode(sv->decayCode, sv->peakDb, sv->peakMs);
```

**Option B: Build on `SampledSound`** (runtime layer) — decode during `createVelocityLayers()`:
Add `DecayEnvelope decayEnvelope;` to `SampledSound`. In `createVelocityLayers()`:
```cpp
auto* sound = new SampledSound(instSpec, varSpec.get(), varSpec->sampleFile, notes);
sound->decayEnvelope = DecayEnvelope::decode(varSpec->decayCode, varSpec->peakDb, varSpec->peakMs);
```

**Recommended: Option B** — keeps the model layer clean and puts runtime data where it's consumed. `SampledSound` is already the primary runtime representation that voices use.

### Step 7: Modify `handleController()` for Smart Offset

Replace the fixed-offset block in `handleController()` with decay-level-matched offset calculation.

**File**: [InstrumentSynth.cpp](../source/Engine/InstrumentSynth.cpp#L130-L148)

Replace:
```cpp
// skip initial attack portion of sample (start from small offset)
const double offsetSec = HIHAT_PEDAL_CROSSFADE_TIMEOUT_SEC;
double srcRate = targetSe->sound->sampleRate > 0.0 ? targetSe->sound->sampleRate : 44100.0;
const double offsetSamples = offsetSec * srcRate;
newVoice->sourceSamplePosition = offsetSamples;
```

With:
```cpp
// Calculate time-offset in new sample matching current RMS level of old sound
double srcRate = targetSe->sound->sampleRate > 0.0 ? targetSe->sound->sampleRate : 44100.0;
double oldSrcRate = ss->sampleRate > 0.0 ? ss->sampleRate : 44100.0;
double elapsedMs = (voice->sourceSamplePosition / oldSrcRate) * 1000.0;

const auto& oldEnv = ss->decayEnvelope;
const auto& newEnv = targetSe->sound->decayEnvelope;

if (!oldEnv.isValid() || !newEnv.isValid()) {
    // No decay data — skip replacement entirely; let old voice continue naturally
    continue;
}

float currentAbsDb = oldEnv.getAbsoluteDbAtMs(static_cast<float>(elapsedMs));
// Apply velocity scaling: actual perceived dB = currentAbsDb + 20*log10(velocityGain)
// Since both old and new will be played at same velocity, this cancels out

float targetMs = newEnv.getMsAtAbsoluteDb(currentAbsDb);
double offsetMs;
if (targetMs > 0.0f) {
    offsetMs = static_cast<double>(targetMs);
} else {
    // Old sound is quieter than new sample's -60dB floor
    // → old is negligible relative to new; start from beginning is fine
    offsetMs = 0.0;
}

const double offsetSamples = (offsetMs / 1000.0) * srcRate;
newVoice->sourceSamplePosition = offsetSamples;
```

### Step 8: Crossfade Envelope for the New Voice

Instead of a hard cut-in, use a short volume ramp on the new voice:

```cpp
// Short fade-in on new voice to avoid click
float fadeInSec = 0.005f; // 5ms fade-in
newVoice->adsr.setParameters({ fadeInSec, 0.0f, 1.0f, newVoice->releaseTimeSec });
// Match velocity of old voice
newVoice->setVelocityGain(vel);
```

And for the old voice, instead of a fixed 50ms fade-out, match the crossfade:
```cpp
voice->fadeOutSec(fadeInSec); // old voice fades for same duration as new fades in
```

### Step 9: Multi-Mic Coherence

When replacing voices, multiple voices may be active for different mics of the same note. The offset must be **the same for all mic channels** of a given articulation replacement.

**Approach**: Calculate the offset once using the **direct mic** (type `d` — closest/cleanest), then apply the same `offsetMs` to all mic voices for this replacement.

In `handleController()`, before the voice loop:
```cpp
// Pre-calculate offset based on direct mic (if available)
float precomputedOffsetMs = -1.0f;  // -1 = not yet calculated
```

In the loop, when first computing offset:
```cpp
if (precomputedOffsetMs < 0.0f) {
    // Use this voice's calculation as canonical
    precomputedOffsetMs = static_cast<float>(offsetMs);
} else {
    // Use pre-calculated offset for coherence across mics
    offsetMs = static_cast<double>(precomputedOffsetMs);
}
```

Priority: prefer using a `d` (direct) mic type for the canonical calculation.

---

## File Change Summary

| File | Changes |
|------|---------|
| [KitLibModel.h](../source/Engine/KitLibModel.h) | Add `DecayEnvelope` struct. Add `peakDb/peakMs/decayMs/decayCode` fields to `SampleVariantSpec` |
| [KitLibModel.cpp](../source/Engine/KitLibModel.cpp) | Transfer parsed metadata to `SampleVariantSpec` in `scanInstrumentSamples()` |
| [InstrumentSynth.h](../source/Engine/InstrumentSynth.h) | Add `DecayEnvelope decayEnvelope` to `SampledSound` |
| [InstrumentSynthLoader.cpp](../source/Engine/InstrumentSynthLoader.cpp) | Decode `DecayEnvelope` in `createVelocityLayers()` |
| [InstrumentSynth.cpp](../source/Engine/InstrumentSynth.cpp) | Replace fixed offset with `DecayEnvelope`-based calculation in `handleController()` |
| New: `DecayEnvelope.h/.cpp` or inline in KitLibModel | `DecayEnvelope` implementation (decode, getAbsoluteDbAtMs, getMsAtAbsoluteDb, base62ToInt) |

---

## Testing Strategy

### Unit Tests
1. **`base62ToInt`** round-trip: encode known values in Python, decode in C++, verify match
2. **`DecayEnvelope::decode`**: Feed known decay codes from actual filenames, verify decoded points match CSV data
3. **`getAbsoluteDbAtMs`**: For known decay table, verify interpolation at exact breakpoints and midpoints
4. **`getMsAtAbsoluteDb`**: Inverse of above — verify round-trip consistency
5. **Edge cases**: empty decay code, single-point envelope, level louder than peak, level below -60dB floor

### Integration Tests
1. Load a kit with decay metadata in filenames, verify `SampledSound.decayEnvelope.isValid()` after loading
2. Simulate pedal transition: manually call `handleController()` with voice at known position, verify computed offset matches expected value from CSV data

### Listening Tests
1. Play closed hi-hat, move pedal to half-open at various times (early: 50ms, mid: 200ms, late: 500ms)
2. Compare A/B: fixed 50ms offset vs. decay-matched offset
3. Verify no audible click, pop, or unnatural volume jump at transition point
4. Test with different velocity levels (low velocity samples have less headroom)

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Many samples lack metadata in filename | Skip replacement for those samples (old voice continues). Log warning at load time for files missing decay metadata so user knows which files need re-analysis. |
| Interpolation produces audible artifacts | 5ms crossfade masks small amplitude mismatches. Can tune threshold. |
| Performance: per-voice dB calculation in RT | `DecayEnvelope` lookups are O(10) comparisons. handleController is called per CC change not per sample. Negligible cost. |
| Different articulations have very different timbral character | Even with matched amplitude, timbral change is audible. This is inherent to the effect being simulated - acceptable. |
| Sample offset beyond loaded portion (async loading) | Check `buffer != nullptr` and offset < buffer length before setting position |

---

## Implementation Order (suggested)

1. **Step 3**: `base62ToInt()` utility + unit tests
2. **Step 2**: `DecayEnvelope` struct + `decode()` + unit tests  
3. **Step 5**: `getAbsoluteDbAtMs()` + `getMsAtAbsoluteDb()` + unit tests
4. **Step 1**: Add fields to `SampleVariantSpec`, transfer in `scanInstrumentSamples()`
5. **Step 6**: Add `DecayEnvelope` to `SampledSound`, decode in `createVelocityLayers()`
6. **Step 7**: Modify `handleController()` offset calculation
7. **Step 8**: Crossfade envelope tuning
8. **Step 9**: Multi-mic coherence
9. End-to-end listening tests and tuning
