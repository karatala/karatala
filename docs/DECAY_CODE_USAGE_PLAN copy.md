# ✅ COMPLETED: Decay Code Usage Plan - Implementation Design (Corrected) (Mostly implemented or superseded by TODO01 / TODO12.)

## 1. Overview

This plan outlines how to integrate the new sample metadata (`attackMs`, `decayMs`, `peakDb`, `decayCode`) extracted by `analyze_drum_samples.py` into the sample playback system to enable:

1. **Per-sample accurate decay timing** – replace generic `maxSampleLengthMs` with sample-specific decay timing.
2. **Predefined decay envelope** – build a fast lookup structure to query expected decay level at any time offset.
3. **Seamless articulation transitions** – enable transitions between articulations (e.g., hihat open → half → close, crash → choke) by aligning decay curves.

Decay envelopes are measured **relative to the peak transient**, not sample start. 

The `peakDb` metadata represents **estimated acoustic peak level relative to the recording reference before normalization**, allowing decay curves from different samples to be mapped into a shared approximate loudness domain.

This metadata is used for **decay curve alignment and articulation matching**, not for runtime gain mixing.

---

# 2. Current Flow

## 2.1 Sample File Parsing (KitLibModel.cpp)

`InstrumentSpec::parseSampleFile()` extracts filename metadata into `SampleFileInfo`.

Result includes:

- velocity
- sampleVariant
- micChain

- **peakMs** rms
- **peakDb** rms
- **decayMs** rms, until -72dB threshold
- **decayCode**

These values are currently parsed but **not used by playback logic**.

---

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

# 3. Decay Code Format

## 3.1 Encoding Process

Decay analysis measures the time required for the signal to reach specific decay levels relative to the peak.


times = absolute ms to reach -6, -12, -18 ... -60 dB


### 3.2 Example


-6 dB @ 100ms
-12 dB @ 250ms
-18 dB @ 420ms
...
-60 dB @ 2100ms


Delta encoding:
[100, 150, 170, ...]

Base62 encoding:
"1W_4g_2s_..."

---

### 3.3 Base62 Digit Map


0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ


---

# 4. Proposed Changes

---

# 4.1 Update `SampledSound` (InstrumentSynth.h)

Decay envelope stores **relative decay offsets from the peak** rather than absolute runtime loudness.

```cpp
class SampledSound {
public:

    struct DecayPoint {
        float timeMs;      // milliseconds after peak transient
        float dbOffset;    // -6, -12, -18 ...
    };

    static constexpr size_t MAX_DECAY_POINTS = 10;

    std::array<DecayPoint, MAX_DECAY_POINTS> decayEnvelope = {};
    size_t decayPointCount = 0;


    float decayMs  = 0.0f;  // estimated time to silence
    float peakDb   = 0.0f;  // estimated acoustic rms peak

    void buildDecayEnvelope(const juce::String& decayCode);

    float getDecayDbOffsetAtTime(float timeMs) const;

    float getEstimatedAbsDbAtTime(float timeMs) const
    {
        return peakDb + getDecayDbOffsetAtTime(timeMs);
    }

    float findTimeForAbsDb(float targetAbsDb) const;
};
4.2 Update SampleLoader
Modified LoadJob::runJob()
double effectiveLengthMs = loader.maxSampleLengthMs;
bool hasMetadata = false;

if (sampleFileInfo && sampleFileInfo->decayMs > 0)
{
    effectiveLengthMs = sampleFileInfo->decayMs + 150.0; // safety margin
    hasMetadata = true;

    sound->attackMs = sampleFileInfo->attackMs;
    sound->decayMs  = sampleFileInfo->decayMs;
    sound->peakDb   = sampleFileInfo->peakDb;
}
Trim Samples
maxSamples = sampleRate * effectiveLengthMs
Always Apply Safety Fade

Even metadata-driven samples receive a short fade to avoid trim clicks.

const int fadeSamples = (int)(sampleRate * 0.005); // 5ms fade
const int fadeStart   = std::max(0, samplesToRead - fadeSamples);

for (int ch = 0; ch < channels; ++ch)
{
    buffer->applyGainRamp(
        ch,
        fadeStart,
        samplesToRead - fadeStart,
        1.0f,
        0.0f);
}
Build Decay Envelope
if (hasMetadata)
{
    sound->buildDecayEnvelope(sampleFileInfo->decayCode);
}
4.3 Decoding Decay Code
void SampledSound::buildDecayEnvelope(const juce::String& decayCode)
{
    decayPointCount = 0;

    if (decayCode.isEmpty())
        return;

    const float dbTargets[10] =
    {-6,-12,-18,-24,-30,-36,-42,-48,-54,-60};

    auto tokens =
        juce::StringArray::fromTokens(decayCode,"_","");

    float accumulatedTimeMs = 0;

    for (int i=0;i<tokens.size() && i<MAX_DECAY_POINTS;i++)
    {
        int delta = base62ToInt(tokens[i]);

        accumulatedTimeMs += delta;

        decayEnvelope[i].timeMs  = accumulatedTimeMs;
        decayEnvelope[i].dbOffset = dbTargets[i];

        decayPointCount++;
    }
}
4.4 Query Decay Offset
float SampledSound::getDecayDbOffsetAtTime(float timeMs) const
{
    if (decayPointCount == 0)
        return 0.0f;

    for (size_t i=0;i<decayPointCount;i++)
    {
        if (decayEnvelope[i].timeMs >= timeMs)
        {
            if (i == 0)
                return decayEnvelope[0].dbOffset;

            const auto& p0 = decayEnvelope[i-1];
            const auto& p1 = decayEnvelope[i];

            float dt = p1.timeMs - p0.timeMs;

            if (dt <= 0.001f)
                return p1.dbOffset;

            float alpha =
                (timeMs - p0.timeMs) / dt;

            alpha =
                juce::jlimit(0.0f,1.0f,alpha);

            return p0.dbOffset +
                (p1.dbOffset - p0.dbOffset) * alpha;
        }
    }

    return decayEnvelope[decayPointCount-1].dbOffset;
}
4.5 Find Matching Decay Time for Target Loudness

This function enables articulation matching.

float SampledSound::findTimeForAbsDb(float targetAbsDb) const
{
    for (size_t i = 0; i < decayPointCount; ++i)
    {
        float absDb = peakDb + decayEnvelope[i].dbOffset;

        if (absDb <= targetAbsDb)
        {
            if (i == 0)
                return decayEnvelope[0].timeMs;

            const auto& p0 = decayEnvelope[i - 1];
            const auto& p1 = decayEnvelope[i];

            float abs0 = peakDb + p0.dbOffset;
            float abs1 = peakDb + p1.dbOffset;

            float alpha =
                (targetAbsDb - abs0) /
                (abs1 - abs0);

            return p0.timeMs +
                alpha * (p1.timeMs - p0.timeMs);
        }
    }

    return decayEnvelope[decayPointCount - 1].timeMs;
}
4.6 Query Current Estimated Level in Voice

Decay time must be measured relative to the peak transient.

float InstrumentSynthVoice::getCurrentEstimatedAbsDb() const
{
    if (!currentlyPlayingSound)
        return -120.0f;

    double timeMs =
        (sourceSamplePosition / sampleRate) * 1000.0;

    timeMs -= currentlyPlayingSound->attackMs;

    if (timeMs < 0)
        timeMs = 0;

    return
        currentlyPlayingSound
        ->getEstimatedAbsDbAtTime((float)timeMs);
}
5. Seamless Articulation Mutations

Example: Hihat Open → Close

Transition algorithm:

Query source current decay level

Find equivalent level in target articulation

Start target playback at matching decay offset

Apply short crossfade if needed

sourceAbsDb = sourceVoice.getCurrentEstimatedAbsDb()

targetOffset =
    targetSound.findTimeForAbsDb(sourceAbsDb)

targetVoice.startAtOffset(targetOffset)
6. Cache Key Changes

Cache keys must include trimming parameters.

cacheKey =
filePath
+ decayMs
+ sampleRate

Without this, cached buffers could be reused with incorrect trim lengths.

7. Edge Cases
Samples Without Metadata

Fallback:

maxSampleLengthMs
generic fadeOut

Decay lookup returns 0 dB offset.

Incomplete Decay Codes

Some samples may not reach -60 dB.

Queries beyond last point return final offset.

Very Long Samples

Cymbal tails may exceed 10 seconds.

Metadata-driven trimming ensures buffers remain bounded.

8. Memory Footprint

Per sample:

Decay envelope:
10 × 8 bytes = 80 bytes

Metadata:
3 floats = 12 bytes

Total ≈ ~100 bytes per SampledSound

9. Future Option: Phase-Aligned Decay Mapping (Advanced)

A more advanced articulation transition technique can be implemented later using phase-aligned decay mapping.

Instead of crossfading between articulations, the system:

Finds the current decay loudness of the source articulation

Finds the matching decay position in the target articulation

Starts playback of the target sample at that decay offset

Because both samples represent the same physical instrument state along its decay curve, the transition often becomes perceptually seamless.

Example flow:

sourceAbsDb = sourceVoice.getCurrentEstimatedAbsDb()

targetOffset =
    targetSound.findTimeForAbsDb(sourceAbsDb)

targetVoice.startAtOffset(targetOffset)

Advantages:

No crossfade required

Very natural articulation morphing

Preserves natural decay envelopes

Extremely CPU efficient

Typical use cases:

open hihat → half open

half open → closed

cymbal → choke

ride bow → bell articulation

Crossfades can still be used as a fallback if phase mismatch is audible.

10. References

Python analysis script:

scripts/analyze_drum_samples.py

Relevant functions:

extract_decay_points()

encode_time_intervals_as_base62()

construct_new_filename()

Result

With these changes the system:

✔ Uses peak-aligned decay timing
✔ Supports decay-curve articulation matching
✔ Prevents trim clicks with safety fade
✔ Maintains compact metadata footprint
✔ Enables advanced articulation morphing strategies