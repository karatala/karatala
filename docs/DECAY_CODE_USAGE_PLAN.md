# Decay Code Usage Plan - Implementation Design

## 1. Overview

This plan outlines how to integrate the new sample metadata (`peakDb`, `peadMs`, `decayMs`, `decayCode`) extracted by `analyze_drum_samples.py` into the sample playback system to enable:

1. **Per-sample accurate decay timing**: Replace generic `maxSampleLengthMs` with sample-specific decay timing
2. **Predefined decay envelope**: Build a fast lookup structure to query expected dB level at any time offset
3. **Seamless articulation transitions**: Enable crossfading between articulations by matching decay profiles (e.g., hihat open→half→close, crash→choke)

---

## 2. Current Flow

### 2.1 Sample File Parsing (KitLibModel.cpp)
- `InstrumentSpec::parseSampleFile()` → extracts filename metadata into `SampleFileInfo`
- Result includes: velocity, sampleVariant, micChain, **peakDb**, **peakMs**, **decayMs**, **decayCode**
- Currently stored but **not yet used** in `SampleVariantSpec`

### 2.2 SampleLoader Processing
```
File → SampleLoader::LoadJob::runJob()
  ├─ Load audio buffer from disk
  ├─ Trim to maxSampleLengthMs (generic fallback)
  ├─ Apply generic fadeOutMs (linear fade tail)
  └─ Cache + return to audio engine
```

**Current issues:**
- `decayMs` is parsed but ignored
- Hard limit of `maxSampleLengthMs` (default 5000ms) overrides sample-specific timing
- Generic fade-out doesn't respect actual sample envelope
- No way to query current dB level during playback

### 2.3 InstrumentSynthVoice Playback
```
renderNextBlock():
  for each sample:
    ├─ Read interpolated audio from buffer
    ├─ Apply ADSR envelope
    ├─ Apply chokeProcessor gain
    ├─ Write to output (mix across mic channels)
    └─ Check if reached end → stopNote()
```

---

## 3. Decay Code Format (from Python Script)

### 3.1 Encoding Process
```python
def encode_time_intervals_as_base62(times: List[float]) -> str:
    """times = [t1, t2, t3, ...] = absolute times in ms to reach -6, -12, -18... -60 dB"""
    deltas = [times[0], times[1]-times[0], times[2]-times[1], ...]
    return "_".join(number_to_base62(int(delta)) for delta in deltas)
```

### 3.2 Example
```
Sample decay profile from peak:
  -6 dB @ 100ms
  -12 dB @ 250ms  
  -18 dB @ 420ms
  ...
  -60 dB @ 2100ms

Encoded as absolute times: [100, 250, 420, ..., 2100]
Delta encoding: [100, 150, 170, ..., ...]
Base62 encoding: "1W_4g_2s_..." 
```

### 3.3 Base62 Digit Map
```cpp
"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
// 0-9 = 0-9
// a-z = 10-35
// A-Z = 36-61
```

---

## 4. Proposed Changes

### 4.1 Update `SampledSound` (InstrumentSynth.h)

```cpp
class SampledSound {
public:
    // ... existing fields ...
    
    // New decay envelope data (populated after sample load)
    struct DecayPoint {
        float timeMs;      // elapsed ms from peak
        float absoluteDb;  // absolute dB value at this time (peakDb already baked in)
    };
    static constexpr size_t MAX_DECAY_POINTS = 10;  // -6 to -60 dB in 6 dB steps
    std::array<DecayPoint, MAX_DECAY_POINTS> decayEnvelope = {};
    size_t decayPointCount = 0;  // actual number of valid points (0-10)
    
    // Metadata from parsed sample filename
    float peakDb = 0.0f;
    float peakMs = 0.0f;
    float decayMs = 0.0f;
    
    // Helper: decode base62 deltas from decayCode and build decayEnvelope
    // Calculates absolute dB values (peakDb + decay targets) and stores them
    void buildDecayEnvelope(const juce::String& decayCode);
    
    // Helper: get absolute dB level at given time (for seamless transitions)
    float getDecayLevelAtTime(float timeMs) const;
};
```

### 4.2 Update `SampleLoader` (SampleLoader.h/cpp)

**Modify SampleLoader::LoadJob::runJob():**

```cpp
JobStatus runJob() override {
    // ... load audio format reader ...
    
    // CHANGE 1: Use decayMs instead of maxSampleLengthMs if available
    double effectiveLengthMs = loader.maxSampleLengthMs;  // fallback
    bool hasMetadata = false;
    if (sampleFileInfo && sampleFileInfo->decayMs > 0) {
        effectiveLengthMs = sampleFileInfo->decayMs;
        hasMetadata = true;
        // Also populate SampledSound metadata
        this->sound->decayMs = sampleFileInfo->decayMs;
        this->sound->peakDb = sampleFileInfo->peakDb;
    }
    
    const int maxSamples = (int)(sampleRate * (effectiveLengthMs * 0.001));
    const int samplesToRead = std::min((int64)reader->lengthInSamples, maxSamples);
    
    // ... read buffer ...
    
    // CHANGE 2: Skip generic fade-out if decayMs was used
    if (!hasMetadata) {
        // Apply generic fade-out for samples without metadata
        const int fadeSamples = (int)(sampleRate * (loader.fadeOutMs * 0.001));
        const int fadeStart = std::max(0, samplesToRead - fadeSamples);
        for (int ch = 0; ch < channels; ++ch) {
            buffer->applyGainRamp(ch, fadeStart, samplesToRead - fadeStart, 1.0f, 0.0f);
        }
    }
    // else: decay is natural from audio; no artificial fade needed
    
    // CHANGE 3: Build decay envelope from decayCode if available
    if (hasMetadata && sampleFileInfo) {
        this->sound->buildDecayEnvelope(sampleFileInfo->decayCode);
    }
    
    // ... cache and return ...
}
```

### 4.3 Decode Decay Code in `SampledSound`

**Add to SampledSound class:**

```cpp
void SampledSound::buildDecayEnvelope(const juce::String& decayCode) {
    if (decayCode.isEmpty()) {
        this->decayPointCount = 0;
        return;
    }
    
    this->decayPointCount = 0;
    
    // Parse base62 deltas from decayCode
    const char* BASE62_DIGITS = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    
    auto base62ToInt = [BASE62_DIGITS](const juce::String& s) -> int {
        int result = 0;
        for (char c : s.toStdString()) {
            const char* pos = strchr(BASE62_DIGITS, c);
            if (pos) {
                result = result * 62 + (pos - BASE62_DIGITS);
            }
        }
        return result;
    };
    
    auto tokens = juce::StringArray::fromTokens(decayCode, "_", "");
    const float dBTargets[] = {-6, -12, -18, -24, -30, -36, -42, -48, -54, -60};
    
    float accumulatedTimeMs = 0.0f;
    for (size_t i = 0; i < tokens.size() && i < this->MAX_DECAY_POINTS; ++i) {
        int delta = base62ToInt(tokens[i]);
        accumulatedTimeMs += delta;
        
        this->decayEnvelope[i].timeMs = accumulatedTimeMs;
        this->decayEnvelope[i].absoluteDb = this->peakDb + dBTargets[i];
        this->decayPointCount++;
    }
}

float SampledSound::getDecayLevelAtTime(float timeMs) const {
    if (this->decayPointCount == 0) return this->peakDb;  // no decay info; return peak
    
    // Linear search is fine for max 10 points
    for (size_t i = 0; i < this->decayPointCount; ++i) {
        if (this->decayEnvelope[i].timeMs >= timeMs) {
            if (i == 0) return this->decayEnvelope[0].absoluteDb;
            
            // Linear interpolation between adjacent absolute dB values
            const auto& p0 = this->decayEnvelope[i - 1];
            const auto& p1 = this->decayEnvelope[i];
            
            float alpha = (timeMs - p0.timeMs) / (p1.timeMs - p0.timeMs);
            alpha = juce::jlimit(0.0f, 1.0f, alpha);
            
            return p0.absoluteDb + (p1.absoluteDb - p0.absoluteDb) * alpha;
        }
    }
    
    // Beyond last point, return last dB value
    return this->decayEnvelope[this->decayPointCount - 1].absoluteDb;
}
```

### 4.4 Query Current Level in `InstrumentSynthVoice` (for articulation transitions)

**Add to InstrumentSynthVoice class:**

```cpp
class InstrumentSynthVoice {
public:
    // ... existing fields ...
    
    // Get current sounding level in dB (for seamless transitions)
    float getCurrentSoundingLevelDb() const {
        if (!this->currentlyPlayingSound || this->currentlyPlayingSound->decayPointCount == 0) {
            return this->currentlyPlayingSound ? this->currentlyPlayingSound->peakDb : 0.0f;
        }
        
        // Calculate time offset from peak in milliseconds
        double timeOffsetMs = this->sourceSamplePosition / (this->sampleRate / 1000.0);
        
        // getDecayLevelAtTime returns absolute dB (no arithmetic needed)
        float decayDbLevel = this->currentlyPlayingSound->getDecayLevelAtTime(timeOffsetMs);
        
        // Apply velocity and envelope gains (convert to linear, apply ADSR)
        float linearLevel = std::pow(10.0f, decayDbLevel / 20.0f);
        linearLevel *= this->velocityGain * this->lastEnvelopeValue;
        
        return 20.0f * std::log10(linearLevel + 1e-6f);  // back to dB
    }
};
```

---

## 5. Use Case: Seamless Articulation Mutations

### 5.1 Example: Hihat Open → Half → Close

**Goal:** Transition from open hihat to closed with no clicks/artifacts

```cpp
class ArticulationMutationController {
public:
    void startMutation(InstrumentSynthVoice* sourceVoice, 
                      InstrumentSynthVoice* targetVoice,
                      float crossfadeDurationMs = 50.0f) {
        if (!sourceVoice || !targetVoice) return;
        
        // Find current sounding level of source
        float sourceLevelDb = sourceVoice->getCurrentSoundingLevelDb();
        
        // Find target articulation sample that has similar decay profile
        // at a matching dB level (optional: could use decayCode similarity)
        targetVoice->startNote(targetNote, targetVelocity, targetSound);
        
        // Crossfade: fade out source, fade in target
        // Both follow their natural decay curves
        float sourceFadeStartGain = linearFromDb(sourceLevelDb);
        float targetFadeEndGain = linearFromDb(sourceLevelDb);  // start at same level
        
        sourceVoice->requestFadeOut(crossfadeDurationMs);
        // targetVoice applies matching gain ramp during startup
    }
};
```

### 5.2 Benefits
- **No silence gaps:** Target starts at source's current dB level
- **Natural decay:** Each articulation follows its own pre-analyzed decay profile
- **CPU efficient:** Decay envelope is precomputed; query is O(log 10) lookup + interpolation
- **Per-sample accuracy:** Respects actual recorded transient characteristics

---

## 6. Implementation Steps

1. **Phase 1: Foundation**
   - Add decay fields to `SampledSound`
   - Implement `buildDecayEnvelope()` and `getDecayOffsetAtTime()`
   - Pass `SampleFileInfo` through to `SampleLoader::LoadJob`

2. **Phase 2: SampleLoader Integration**
   - Modify `LoadJob::runJob()` to use `decayMs` instead of `maxSampleLengthMs`
   - Skip generic fade-out when `decayMs` is available
   - Update cache key logic (already considers both parameters)

3. **Phase 3: Voice Query**
   - Add `getCurrentSoundingLevelDb()` to `InstrumentSynthVoice`
   - Integrate with `InstrumentSynthVoice::renderNextBlock()` if needed for monitoring

4. **Phase 4: Articulation Transitions (Optional, Future)**
   - Create `ArticulationMutationController` utility class
   - Implement crossfade logic using current dB level queries
   - Test with hihat and cymbal articulations

---

## 7. Edge Cases & Considerations

### 7.1 Samples Without Metadata
- Fallback to `maxSampleLengthMs` and generic `fadeOutMs`
- `decayPointCount == 0`; queries return `peakDb` immediately (no array access needed)

### 7.2 Incomplete Decay Codes
- Some samples may not reach -60 dB within reasonable time
- Array size is fixed (10 points for -6 to -60 dB)
- Queries beyond last point return last dB offset

### 7.3 Very Short vs. Very Long Samples
- Short attack samples (drums): decayMs may be < 800ms
- Long tail samples (cymbals): decayMs may be > 10 seconds
- No special handling needed; decayMs drives actual trimming

### 7.4 Cache Key Invalidation
- `makeCacheKey()` currently uses `maxSampleLengthMs` and `fadeOutMs`
- If using `decayMs`, cache key should ideally include it (or decode it from filename)
- Current implementation: each sample file cached independently, overrides based on presence

### 7.5 Sample Rate Independence
- Decay times stored in milliseconds (sample-rate agnostic)
- `getDecayOffsetAtTime()` uses `timeMs`, not sample index
- Works correctly across different project/playback sample rates

---

## 8. Data Structure Summary

```cpp
// In SampledSound
static constexpr size_t MAX_DECAY_POINTS = 10;  // -6 to -60 dB in 6 dB steps

struct DecayPoint {
    float timeMs;      // 0..2500 typical range (milliseconds from peak or prev point)
    float absoluteDb;  // absolute dB value (peakDb already baked in during buildDecayEnvelope)
};

std::array<DecayPoint, MAX_DECAY_POINTS> decayEnvelope = {};  // fixed-size array
size_t decayPointCount = 0;  // actual number of populated points (0-10)

float peakDb = 0.0f;
float peakMs = 0.0f;
float decayMs = 0.0f;
```

**Memory footprint per sample:**
- 3 floats = ~12 bytes (peakMs, peakDb, decayMs)
- decayEnvelope array (10 × 8 bytes) = 80 bytes
- decayPointCount (size_t) = 8 bytes
- **Total: ~100 bytes per SampledSound object** (no pointers, no strings, no duplication)

---

## 9. References

- **Python analysis script:** `scripts/analyze_drum_samples.py` 
  - `encode_time_intervals_as_base62()` - how decay_code is built
  - `extract_decay_points()` - extracts -6, -12, ... -60 dB time points
  - `construct_new_filename()` - embeds metadata in filename

- **Current implementation files:**
  - `source/Engine/KitLibModel.h` - `SampleFileInfo` definition
  - `source/Engine/SampleLoader.h/cpp` - sample loading & trimming
  - `source/Engine/InstrumentSynth.h/cpp` - voice rendering

