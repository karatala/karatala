# 🔍 Key insights (high-level patterns)

## 1\. Velocity strongly controls decay _and_ brightness

Across almost all instruments:

-   Higher velocity (127):
    
    -   **Higher peak amplitude** (less negative dB)
    -   **Longer decay tails**
    -   Faster transient onset (earlier peak ms)

Example:

-   _china-X1 edge oni_
    
    -   vel 43 → decay ~4520 ms
    -   vel 127 → decay ~2550 ms BUT reaches lower dB thresholds slower (longer usable tail)

👉 Interpretation:  
Higher velocities = more energy in high frequencies → longer perceptual sustain even if RMS drops faster initially.

* * *

## 2\. Mic/processing variants behave like mix presets

Your mic labels clearly map to tonal/processing differences:

-   **oni / ons / ows** → likely overhead variations
-   **rcm** → room mic
-   **rfs** → far/ambient or processed

Patterns:

### Close / direct (oni, ons)

-   Faster transient (low amp\_peak\_ms)
-   Higher peak dB
-   Shorter decay

### Room (rcm)

-   Slower transient
-   Lower peak
-   Longer mid decay

### Far / diffuse (rfs)

-   Lowest peak levels
-   Longest decay tails
-   More gradual decay curve

👉 This is essentially **built-in multi-mic mixing data**

* * *

## 3\. Cymbals vs drums = completely different decay regimes

### Cymbals (china, crash, ride, splash)

-   Long tails:
    
    -   3s → 12s (!)
-   Smooth decay curves
-   Gradual -6dB → -60dB progression

Example:

-   _ride-X bow 127_
    
    -   total length: **12,134 ms**
    -   \-60 dB reached near end

### Drums (kick, snare, toms)

-   Short, structured decay:
    
    -   200–1500 ms typical
-   Step-like decay (shell + head + room)

👉 This difference is super useful for:

-   classification
-   synthesis modeling
-   envelope shaping

* * *

## 4\. Hi-hats show articulation-dependent envelopes

You have:

-   edge\[close\]
-   edge\[half\]
-   edge\[open\]
-   edge\[tight\]
-   pedal

### Clear pattern:

| Articulation | Decay |
| --- | --- |
| tight | shortest (~200–900 ms) |
| close | short (~200–1200 ms) |
| half | medium (~800–2000 ms) |
| open | long (~1–4 s) |
| pedal | very short/noisy |

👉 This is a clean **continuous openness model**—great for interpolation.

* * *

## 5\. Transient timing is extremely consistent

-   Most **amp\_peak\_ms**:
    
    -   drums: 0–30 ms
    -   cymbals: 20–200 ms

👉 Meaning:

-   Drums = impulse-like
-   Cymbals = build-up energy (complex attack)

