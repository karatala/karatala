run it from the project root with:

```bash
python3 scripts/analyze_drum_samples_phase1.py --input-folder sample_library --output-json build/todo14_phase1_reference_v5-onset_frame_10ms.json --listening-folder build/todo14_phase1_listening_v5
```

Use new `v5`, `v6`, etc. names so you do not overwrite earlier results.

The tuning constants are near the top of [analyze_drum_samples_phase1.py](<scripts/analyze_drum_samples_phase1.py:19>).

### Compare different windows

```python
LISTENING_PROFILES = (
    ("100ms", "k_weighted_100ms_db", {}),
    ("200ms", "k_weighted_200ms_db", {}),
    ("400ms", "k_weighted_400ms_db", {}),
)
```

Available windows currently come from:

```python
FIXED_WINDOW_MS = (50, 100, 200, 400)
```

You can add another one:

```python
FIXED_WINDOW_MS = (50, 100, 200, 400, 600)

LISTENING_PROFILES = (
    ("600ms", "k_weighted_600ms_db", {}),
)
```

For a 600 ms measurement, increase the listening clip too:

```python
LISTENING_CLIP_MS = 700
```

### Compare ride and cowbell adjustments

Negative values make that family quieter:

```python
LISTENING_PROFILES = (
    ("baseline", "k_weighted_400ms_db", {}),
    ("ride_down_1", "k_weighted_400ms_db", {"ride": -1.0}),
    ("ride_down_2", "k_weighted_400ms_db", {"ride": -2.0}),
    ("ride_cowbell_down", "k_weighted_400ms_db", {"ride": -1.0, "short_percussion": -0.5}),
)
```

`short_percussion` currently represents cowbells and similar short metal percussion in the reference selection.

The script creates one WAV for every profile.

### Change listening speed

```python
LISTENING_CLIP_MS = 500
LISTENING_GAP_MS = 50
```

For example, a faster comparison:

```python
LISTENING_CLIP_MS = 400
LISTENING_GAP_MS = 20
```

### Change playback volume

```python
LISTENING_TARGET_DB = -30.0
```

Try `-26.0` for louder or `-34.0` for quieter. This does not change relative instrument balance. The script may reduce the entire sequence if its peak would exceed `-1 dB`.

### Report-only calibration parameters

These affect calculations in the JSON report, but do not change the listening WAV balance:

```python
COMMON_REFERENCE_DB = -38.0
POSITIVE_TRIM_WARNING_DB = 24.0
POSITIVE_TRIM_CAP_DB = 36.0
MIN_RENDERED_HEADROOM_DB = 1.0
```

After running, open:

- `build/todo14_phase1_listening_v5/` for WAV files
- `manifest.txt` for the sample order
- `build/todo14_phase1_reference_v5.json` for measurements

The temporary Python dependencies may disappear after a restart or system cleanup. The required packages are `numpy`, `soundfile`, `scipy`, and `pyloudnorm`.