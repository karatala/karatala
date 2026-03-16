import os
import csv
import argparse
import textwrap
import numpy as np
import numpy.typing as npt
import soundfile as sf
import librosa
import pyloudnorm as pyln
import re
import time
from typing import List, Tuple, Optional
from scipy.signal import hilbert
from scipy.ndimage import uniform_filter1d
from pathlib import Path


SUPPORTED_AUDIO_EXTENSIONS: Tuple[str, ...] = (
    ".wav", ".bwf", ".flac", ".aif", ".aiff",
    ".ogg", ".m4a", ".mp3", ".wmv", ".asf", ".wm", ".wma"
)

RESONANCE_MIN_HZ: float = 80.0
RESONANCE_MAX_HZ: float = 18000.0
RESONANCE_WINDOW_MS: float = 200.0
RMS_ANALYSIS_WINDOW_MS: int = 10

BASE62_DIGIT_MAP: str = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


# =========================
# USER SETTINGS
# =========================

parser = argparse.ArgumentParser(
    description="Batch analyze drum one-shot samples (resonance-based)",
    formatter_class=argparse.RawTextHelpFormatter,
    epilog=textwrap.dedent("""
Example:
  python .\\scripts\\analyze_drum_samples.py -i C:\\Users\\Public\\Documents\\Karatala\\sample_library
  python analyze_drum_samples.py -i "./samples" -o results.csv -f 127a-

If required packages are missing, install them with:
  pip install numpy librosa soundfile pyloudnorm scipy
""")
)

parser.add_argument(
    "-i", "--input-folder",
    required=True,
    help="Folder containing audio files"
)

parser.add_argument(
    "-o", "--output-csv",
    default="drum_analysis.csv",
    help="Output CSV file name (default: drum_analysis.csv)"
)

parser.add_argument(
    "--silence-db",
    type=float,
    default=-60.0,
    help="Silence threshold in dB (default: -60)"
)

parser.add_argument(
    "-f", "--name-filter",
    help="Only process file names that contain filter string"
)

parser.add_argument(
    "--rename-files",
    action="store_true",
    help="Rename files to include encoded metadata (attack, decay, peak, decay_code)"
)

args: argparse.Namespace = parser.parse_args()

INPUT_FOLDER: str = os.path.abspath(args.input_folder)
OUTPUT_CSV: str = args.output_csv
SILENCE_THRESHOLD_DB: float = args.silence_db
NAME_FILTER: Optional[str] = args.name_filter
RENAME_FILES: bool = args.rename_files


# =========================
# HELPERS
# =========================

def number_to_base62(number: int) -> str:
    """Converts an integer to Base-62 representation."""
    result: str = ""
    while number > 0:
        result = BASE62_DIGIT_MAP[number % 62] + result
        number //= 62
    return result or "0"


def convert_linear_amplitude_to_db(
    amplitude_linear: npt.NDArray[np.float64]
) -> npt.NDArray[np.float64]:
    """
    Converts linear amplitude values to decibels.

    Uses 20 * log10 because input is amplitude (not power).

    Output
    ------
    amplitude_db
    """
    return 20 * np.log10(np.maximum(np.abs(amplitude_linear), 1e-12))

def compute_amplitude_envelope_via_hilbert(
    audio_waveform_float: npt.NDArray[np.float64]
) -> npt.NDArray[np.float64]:
    """
    Computes instantaneous amplitude envelope of a mono audio waveform.

    Input
    -----
    audio_waveform_float
        Mono PCM waveform.
        Range typically [-1.0, 1.0].

    Output
    ------
    amplitude_envelope_linear
        Instantaneous amplitude magnitude (linear scale).
        Same length as input waveform.
        Range: [0.0 .. ~1.0]
    """
    analytic_signal = hilbert(audio_waveform_float)
    amplitude_envelope_linear = np.abs(analytic_signal)
    return amplitude_envelope_linear


def smooth_amplitude_envelope(
    amplitude_envelope_linear: npt.NDArray[np.float64],
    sample_rate_hz: int,
    smoothing_window_ms: float = 5.0
) -> npt.NDArray[np.float64]:
    """
    Applies moving average smoothing to amplitude envelope.

    smoothing_window_ms
        Window length in milliseconds.

    Returns
    -------
    amplitude_envelope_smoothed_linear
    """
    smoothing_window_samples = int(sample_rate_hz * smoothing_window_ms / 1000)
    if smoothing_window_samples < 1:
        return amplitude_envelope_linear
    return uniform_filter1d(
        amplitude_envelope_linear,
        size=smoothing_window_samples
    )

def convert_multichannel_audio_to_mono_rms(
    audio_waveform: npt.NDArray[np.float64]
) -> npt.NDArray[np.float64]:
    """
    Converts multi-channel audio to mono using RMS energy.

    Returns
    -------
    mono_waveform_float
    """
    if audio_waveform.ndim == 1:
        return audio_waveform
    mono_waveform_float = np.sqrt(np.mean(audio_waveform ** 2, axis=1))
    return mono_waveform_float

# def compute_rms_envelope_per_window(signal: npt.NDArray[np.float64], sample_rate: int) -> Tuple[npt.NDArray[np.float64], int]:
#     """Calculates RMS envelope of the signal in time windows."""
#     window_size: int = int(sample_rate * RMS_ANALYSIS_WINDOW_MS / 1000)
#     padded = np.pad(signal, (0, window_size - len(signal) % window_size), mode="constant")
#     reshaped = padded.reshape(-1, window_size)
#     rms = np.sqrt(np.mean(reshaped ** 2, axis=1))
#     return rms, window_size


def compute_rms_envelope_per_window(
    audio_waveform_float: npt.NDArray[np.float64],
    sample_rate_hz: int
) -> Tuple[npt.NDArray[np.float64], int]:
    """
    Computes RMS amplitude per analysis window.

    Returns
    -------
    rms_envelope_linear
        RMS value per analysis frame (linear scale)

    analysis_window_samples
        Window size used for RMS calculation
    """
    analysis_window_samples = int(sample_rate_hz * RMS_ANALYSIS_WINDOW_MS / 1000)
    padded = np.pad(
        audio_waveform_float,
        (0, analysis_window_samples - len(audio_waveform_float) % analysis_window_samples),
        mode="constant"
    )
    reshaped = padded.reshape(-1, analysis_window_samples)
    rms_envelope_linear = np.sqrt(np.mean(reshaped ** 2, axis=1))
    return rms_envelope_linear, analysis_window_samples


def extract_decay_points(envelope: npt.NDArray[np.float64], window_ms: float = 10) -> Tuple[List[float], float, float]:
    """Extracts decay time points at regular dB intervals from the amplitude envelope."""
    env_db = convert_linear_amplitude_to_db(envelope)
    peak_idx = int(np.argmax(env_db))
    peak_db = float(env_db[peak_idx])
    decay_levels = np.array([-6,-12,-18,-24,-30,-36,-42,-48,-54,-60])
    targets = peak_db + decay_levels
    tail = env_db[peak_idx:]
    decay_times: List[float] = []
    for target in targets:
        hits = np.where(tail <= target)[0]
        if len(hits) > 0:
            idx = hits[0]
            decay_times.append((idx + peak_idx) * window_ms)
    peak_time = peak_idx * window_ms
    return decay_times, peak_time, peak_db


def compute_lufs_momentary_envelope(
    audio_waveform: npt.NDArray[np.float64],
    sample_rate_hz: int,
    window_ms: float = 400.0,
    hop_ms: float = 10.0
) -> Tuple[npt.NDArray[np.float64], float]:
    """
    Computes LUFS-M (momentary loudness) envelope.

    Parameters
    ----------
    audio_waveform
        Mono audio waveform.

    window_ms
        LUFS-M integration window (ITU standard = 400 ms).

    hop_ms
        Step between measurements.

    Returns
    -------
    lufs_values
        LUFS-M loudness values.

    hop_ms
        Time resolution of envelope.
    """
    meter = pyln.Meter(sample_rate_hz)
    window_samples = int(sample_rate_hz * window_ms / 1000)
    hop_samples = int(sample_rate_hz * hop_ms / 1000)
    lufs_values = []
    for start in range(0, len(audio_waveform) - window_samples, hop_samples):
        segment = audio_waveform[start:start + window_samples]
        try:
            loudness = meter.integrated_loudness(segment)
        except ValueError:
            loudness = -np.inf
        lufs_values.append(loudness)
    return np.array(lufs_values), hop_ms


def extract_decay_points_lufs_m(
    lufs_envelope: npt.NDArray[np.float64],
    hop_ms: float
) -> Tuple[List[float], float, float]:
    """
    Extracts decay time points using LUFS-M loudness envelope.

    Parameters
    ----------
    lufs_envelope
        Momentary loudness values in LUFS.

    hop_ms
        Time resolution of envelope.

    Returns
    -------
    decay_times_ms
        Times when signal crosses -6, -12 ... -60 dB relative to peak. 

    peak_time_ms
        Time of loudness peak.

    peak_lufs
        Peak LUFS value.
    """
    peak_idx = int(np.argmax(lufs_envelope))
    peak_lufs = float(lufs_envelope[peak_idx])
    decay_levels = np.array([-6,-12,-18,-24,-30,-36,-42,-48,-54,-60]) #FIXME: should be relative to peak LUFS, not dB. also lufs have different scale, so maybe use some fixed LUFS intervals
    targets = peak_lufs + decay_levels
    tail = lufs_envelope[peak_idx:]
    decay_times = []
    for target in targets:
        hits = np.where(tail <= target)[0]
        if len(hits) > 0:
            idx = hits[0]
            decay_times.append((idx + peak_idx) * hop_ms)
    peak_time_ms = peak_idx * hop_ms
    return decay_times, peak_time_ms, peak_lufs


def compute_decay_time_ms_from_envelope(
    envelope_db: npt.NDArray[np.float64],
    peak_sample_index: int,
    sample_rate_hz: int,
    silence_threshold_db: float
) -> float:
    """
    Measures decay time until envelope falls below silence threshold.

    envelope_db
        Envelope in decibels.

    peak_sample_index
        Index of peak amplitude.

    silence_threshold_db
        Absolute silence threshold (e.g. -60 dB)

    Returns
    -------
    decay_time_ms
    """
    for sample_index in range(peak_sample_index, len(envelope_db)):
        if envelope_db[sample_index] <= silence_threshold_db:
            decay_time_ms = (sample_index - peak_sample_index) / sample_rate_hz * 1000
            return decay_time_ms
    return (len(envelope_db) - peak_sample_index) / sample_rate_hz * 1000


def find_dominant_frequency(signal: npt.NDArray[np.float64], sample_rate: int) -> float:
    """Finds the frequency with the highest amplitude in the signal."""
    if len(signal) < 512:
        return 0.0
    window: npt.NDArray[np.float64] = np.hanning(len(signal))
    spectrum: npt.NDArray[np.float64] = np.abs(np.fft.rfft(signal * window))
    spectrum = uniform_filter1d(spectrum, size=3)
    freqs: npt.NDArray[np.float64] = np.fft.rfftfreq(len(signal), 1.0 / sample_rate)
    return float(freqs[int(np.argmax(spectrum))])


def detect_resonance_frequency(
    audio_waveform: npt.NDArray[np.float64],
    sample_rate_hz: int,
    peak_sample_index: int
) -> Optional[float]:
    """
    Estimates resonant frequency of a drum sample by averaging multiple FFT windows
    after the transient.

    Strategy
    --------
    1. Skip transient region
    2. Take several overlapping windows
    3. Average magnitude spectra
    4. Smooth spectrum
    5. Detect dominant peak

    Returns
    -------
    resonance_frequency_hz
    """
    skip_samples: int = int(sample_rate_hz * 0.015)  # skip 15 ms transient
    start_index: int = peak_sample_index + skip_samples
    window_samples: int = int(sample_rate_hz * RESONANCE_WINDOW_MS / 1000)
    if start_index + window_samples >= len(audio_waveform):
        return None
    step: int = window_samples // 2
    num_windows: int = 4
    spectra: list[npt.NDArray[np.float64]] = []
    for i in range(num_windows):
        pos: int = start_index + i * step
        if pos + window_samples > len(audio_waveform):
            break
        segment: npt.NDArray[np.float64] = audio_waveform[pos:pos + window_samples]
        window: npt.NDArray[np.float64] = np.hanning(len(segment))
        spectrum: npt.NDArray[np.float64] = np.abs(np.fft.rfft(segment * window))
        spectra.append(spectrum)
    if not spectra:
        return None
    avg_spectrum: npt.NDArray[np.float64] = np.mean(spectra, axis=0)
    # spectral smoothing
    avg_spectrum = uniform_filter1d(avg_spectrum, size=5)
    freqs: npt.NDArray[np.float64] = np.fft.rfftfreq(window_samples, 1.0 / sample_rate_hz)
    mask: npt.NDArray[np.bool_] = ((freqs >= RESONANCE_MIN_HZ) & (freqs <= RESONANCE_MAX_HZ))
    if not np.any(mask):
        return None
    spectrum_band: npt.NDArray[np.float64] = avg_spectrum[mask]
    freq_band: npt.NDArray[np.float64] = freqs[mask]
    peak_index: int = int(np.argmax(spectrum_band))
    interp_index: float = refine_peak_index_via_quadratic_interpolation(spectrum_band, peak_index)
    freq_step: float = float(freq_band[1] - freq_band[0])
    resonance_frequency_hz: float = float(freq_band[0] + interp_index * freq_step)
    return resonance_frequency_hz


def detect_resonance_frequency_autocorrelation(
    audio_waveform: npt.NDArray[np.float64],
    sample_rate_hz: int,
    peak_sample_index: int
) -> Optional[float]:
    """
    Detects resonant frequency using autocorrelation.

    More stable for drums because it finds the fundamental
    even if harmonics are louder.
    """
    skip_samples = int(sample_rate_hz * 0.015)
    start = peak_sample_index + skip_samples
    window_samples = int(sample_rate_hz * RESONANCE_WINDOW_MS / 1000)
    if start + window_samples >= len(audio_waveform):
        return None
    segment = audio_waveform[start:start + window_samples]
    segment = segment * np.hanning(len(segment))
    autocorr = np.correlate(segment, segment, mode="full")
    autocorr = autocorr[len(autocorr)//2:]
    min_lag = int(sample_rate_hz / RESONANCE_MAX_HZ)
    max_lag = int(sample_rate_hz / RESONANCE_MIN_HZ)
    search_region = autocorr[min_lag:max_lag]
    if len(search_region) == 0:
        return None
    lag = np.argmax(search_region) + min_lag
    resonance_frequency_hz = sample_rate_hz / lag
    return float(resonance_frequency_hz)



def detect_resonance_frequency_old(signal: npt.NDArray[np.float64], sample_rate: int, start_index: int) -> Optional[float]:
    """Detects the resonant frequency of the signal starting from a given index."""
    start_index = start_index + int(sample_rate * 0.01) # Skip transient (10 ms)
    window_samples = int(sample_rate * RESONANCE_WINDOW_MS / 1000)
    segment = signal[start_index:start_index + window_samples]
    if len(segment) < 512:
        return None
    window = np.hanning(len(segment))
    spectrum = np.abs(np.fft.rfft(segment * window))
    freqs = np.fft.rfftfreq(len(segment), 1 / sample_rate)
    mask = (freqs >= RESONANCE_MIN_HZ) & (freqs <= RESONANCE_MAX_HZ)
    if not np.any(mask):
        return None
    spec = spectrum[mask]
    freq = freqs[mask]
    peak = int(np.argmax(spec))
    interp = refine_peak_index_via_quadratic_interpolation(spec, peak)
    freq_step = freq[1] - freq[0]
    return float(freq[0] + interp * freq_step)


def refine_peak_index_via_quadratic_interpolation(magnitude: np.ndarray, peak_index: int) -> float:
    """Refines peak frequency index using quadratic interpolation for better accuracy."""
    if peak_index <= 0 or peak_index >= len(magnitude) - 1:
        return float(peak_index)
    left_mag = magnitude[peak_index - 1]
    center_mag = magnitude[peak_index]
    right_mag = magnitude[peak_index + 1]
    denominator = left_mag - 2 * center_mag + right_mag
    if denominator == 0:
        return float(peak_index)
    shift = 0.5 * (left_mag - right_mag) / denominator
    return peak_index + shift


def encode_time_intervals_as_base62(times: List[float]) -> str:
    """Encodes time intervals as Base-62 deltas for compact storage."""
    if not times:
        return ""
    deltas: List[float] = [times[0]]
    for i in range(1, len(times)):
        deltas.append(times[i] - times[i - 1])
    return "_".join(
        number_to_base62(int(t))
        for t in deltas
    )


def extract_sample_metadata_from_path(path: str, input_root: str) -> Tuple[str, str, int, str, str]:
    """Extracts instrument, articulation, velocity, and other metadata from file path structure."""
    rel: str = os.path.relpath(path, input_root)
    parts: Tuple[str, ...] = Path(rel).parts
    instrument: str = ""
    articulation: str = ""
    velocity: int = 0
    sample_variant: str = ""
    mic_chain: str = ""
    if len(parts) >= 3:
        inst_folder: str = parts[-3]
        inst_parts: List[str] = inst_folder.split("-")
        if len(inst_parts) >= 2:
            instrument: str = "-".join(inst_parts[:2])
        else:
            instrument: str = inst_folder
    if len(parts) >= 2:
        articulation: str = parts[-2].split("-")[0]
    fname: str = Path(parts[-1]).stem
    m: Optional[re.Match] = re.match(r"(\d+)([a-z]?)-([a-zA-Z0-9_]+)", fname)
    if m:
        velocity: int = int(m.group(1))
        sample_variant: str = m.group(2)
        mic_chain: str = m.group(3)
    return instrument, articulation, velocity, sample_variant, mic_chain


def construct_new_filename(fileName: str, peak_ms: float, peak_db: float, decay_ms: float, decay_code: str) -> str:
    """Constructs new filename with encoded sample analysis metadata.
    
    Format: <velocity><sampleVariant>-<micChain>-a{attack}-d{decay}-p{peak}-c{decay_code}.<ext>
    
    Preserves base name (<velocity><sampleVariant>-<micChain>) in correct order.
    Updates existing metadata if file was already renamed.
    
    Example: 043a-ons-a38-d4960-p19-c34_5a_2U_76_6i_em_d4_kY_7K_0.flac
    """
    # Extract extension
    parts = fileName.rsplit(".", 1)
    name_without_ext = parts[0]
    ext = parts[1] if len(parts) > 1 else ""
    
    # Split by "-" to identify parts
    name_parts = name_without_ext.split("-")
    
    # Base name is always first 2 parts: <velocity><sampleVariant>-<micChain>
    # Optional parameters start at part index 2 onwards
    if len(name_parts) >= 2:
        base_name = "-".join(name_parts[:2])
    else:
        # Fallback for malformed filenames
        base_name = name_without_ext
    
    # Build optional params with latest values
    decay_ms_int = int(round(decay_ms))
    peak_db_int = int(round(abs(peak_db*10)))
    peak_ms_int = int(round(peak_ms))
    
    optional_params = f"d{decay_ms_int}-p{peak_db_int}_{peak_ms_int}-c{decay_code}"
    
    new_name = f"{base_name}-{optional_params}"
    if ext:
        new_name += f".{ext}"
    
    return new_name

# =========================
# MAIN
# =========================

files: List[str] = []
rows: List[List[float | int | str]] = []

for root, _, filenames in os.walk(INPUT_FOLDER, followlinks=True):
    print(f"[DEBUG] Checking folder: {root}")
    for fname in filenames:
        fname_lower: str = fname.lower()
        if not fname_lower.endswith(SUPPORTED_AUDIO_EXTENSIONS):
            continue
        if NAME_FILTER and NAME_FILTER not in fname_lower:
            continue
        files.append(os.path.join(root, fname))

files.sort()


start_time: float = time.perf_counter()
total_files: int = len(files)

print(f"Found {total_files} audio files")

for n, path in enumerate(files, start=1):
    print(f"Processing ({n}/{total_files}): {path}")

    audio_waveform: npt.NDArray[np.float64]
    sample_rate: int
    audio_waveform, sample_rate = sf.read(path)

    audio_waveform = convert_multichannel_audio_to_mono_rms(audio_waveform)
    amplitude_envelope_linear: npt.NDArray[np.float64] = compute_amplitude_envelope_via_hilbert(audio_waveform)
    # amplitude_envelope = smooth_amplitude_envelope(amplitude_envelope, sample_rate)
    # amplitude_envelope /= np.max(amplitude_envelope) + 1e-9
    amplitude_envelope_db: npt.NDArray[np.float64] = convert_linear_amplitude_to_db(amplitude_envelope_linear)

    peak_sample_index = int(np.argmax(amplitude_envelope_db))

    peak_db: float = float(np.max(amplitude_envelope_db))
    peak_ms: float = peak_sample_index / sample_rate * 1000
    decay_ms: float = compute_decay_time_ms_from_envelope(
        amplitude_envelope_db,
        peak_sample_index,
        sample_rate,
        SILENCE_THRESHOLD_DB
    )

    meter: pyln.Meter = pyln.Meter(sample_rate)
    try:
        lufs: float = meter.integrated_loudness(audio_waveform)
    except ValueError:
        lufs = float("-inf")

    rms_envelope_linear: np.ndarray
    rms_window_samples: int
    rms_envelope_linear, rms_window_samples = compute_rms_envelope_per_window(audio_waveform, sample_rate)

    rms_envelope_db: npt.NDArray[np.float64] = convert_linear_amplitude_to_db(rms_envelope_linear)

    peak_rms_index: int = int(np.argmax(rms_envelope_db))

    peak_rms_db: float = float(rms_envelope_db[peak_rms_index])
    peak_rms_ms: float = peak_rms_index * rms_window_samples / sample_rate * 1000
    decay_rms_ms: float = compute_decay_time_ms_from_envelope(
        rms_envelope_db,
        peak_rms_index,
        sample_rate / rms_window_samples,
        SILENCE_THRESHOLD_DB
    )

    decay_points_db, another_peak_rms_ms, another_peak_rms_db = extract_decay_points(rms_envelope_db) #FIXME: are this returned peak_rms_ms peak_rms_db same as calculated above?
    decay_points_lufs, lufs_peak_rms_ms, lufs_peak_rms_db = extract_decay_points_lufs_m(rms_envelope_linear) #FIXME: input lufs envelope, not rms envelope
    print(f" Peak: {peak_db:.1f} dB at {peak_ms:.1f}ms another_peak_rms_db {another_peak_rms_db:.1f} dB at {another_peak_rms_ms:.1f}ms, LUFS peak: {lufs_peak_rms_db:.1f} dB at {lufs_peak_rms_ms:.1f}ms, Decay: {decay_ms:.1f} ms, Decay points (dB): {decay_points_db}, Decay points (LUFS): {decay_points_lufs}")

    decay_code: str = encode_time_intervals_as_base62(decay_points_db)
    
    length_ms: float = len(audio_waveform) / sample_rate * 1000

    instrument, articulation, velocity, sample_variant, mic_chain = extract_sample_metadata_from_path(path, INPUT_FOLDER)

    row: List[float | int | str] = [
        # os.path.relpath(path, INPUT_FOLDER),
        instrument,
        articulation,
        velocity,
        sample_variant,
        mic_chain,
        round(length_ms, 1),
        round(peak_ms, 1),
        round(peak_rms_ms, 1),
        round(decay_ms, 1),
        round(peak_db, 2),
        round(peak_rms_db, 2),
        round(lufs, 2),
        decay_code,
        *decay_points_db,
        *decay_points_lufs,
    ]
    rows.append(row)

    # Optionally rename file with encoded metadata
    if RENAME_FILES:
        file_dir: str = os.path.dirname(path)
        old_filename: str = os.path.basename(path)
        new_filename: str = construct_new_filename(old_filename, peak_ms, peak_db, decay_code)

        if new_filename != old_filename:
            new_path: str = os.path.join(file_dir, new_filename)
            try:
                os.rename(path, new_path)
                print(f"  Renamed: {old_filename} → {new_filename}")
                # Update the relative path in the CSV to reflect new filename
                row[0] = os.path.relpath(new_path, INPUT_FOLDER)
            except Exception as e:
                print(f"  Warning: Failed to rename file: {e}")
                # Keep original path in CSV if rename fails

with open(OUTPUT_CSV, "w", newline="", encoding="utf-8") as f:
    writer = csv.writer(f)
    writer.writerow([
        # "file",
        "instr",
        "art",
        "vel",
        "rr",
        "mic",
        "length_ms",
        "peak_ms",
        "peak_rms_ms",
        "decay_ms",
        "peak_db",
        "peak_rms_db",
        "lufs",
        "decay_code",
        "-6dB",
        "-12dB",
        "-18dB",
        "-24dB",
        "-30dB",
        "-36dB",
        "-42dB",
        "-48dB",
        "-54dB",
        "-60dB",
    ])
    writer.writerows(rows)

end_time: float = time.perf_counter()
elapsed: float = end_time - start_time

print(f"\nDone → {OUTPUT_CSV}")
print(f"Processed {total_files} files in {elapsed:.2f} seconds ({elapsed/60:.2f} minutes)")
