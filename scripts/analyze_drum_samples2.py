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
    default=-72.0,
    help="Silence threshold in dB (default: -72)"
)

parser.add_argument(
    "-f", "--name-filter",
    help="Only process file names that contain filter regex string"
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
RENAME_FILES: bool = args.rename_files
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


def amplitude_to_decibels(values: npt.NDArray[np.float64]) -> npt.NDArray[np.float64]:
    """Converts amplitude values to decibels (dB) scale."""
    return 20 * np.log10(np.maximum(np.abs(values), 1e-12))


def extract_amplitude_envelope_hilbert(signal: npt.NDArray[np.float64]) -> npt.NDArray[np.float64]:
    """Extracts the amplitude envelope from a signal using the Hilbert transform."""
    return np.abs(hilbert(signal))


def smooth_envelope(envelope: npt.NDArray[np.float64], sample_rate: int, window_ms: float = 5.0) -> npt.NDArray[np.float64]:
    """Smooths the amplitude envelope using a uniform filter."""
    window_size = int(sample_rate * window_ms / 1000)
    if window_size < 1:
        return envelope
    return uniform_filter1d(envelope, size=window_size)


def calculate_rms_envelope(signal: npt.NDArray[np.float64], sample_rate: int) -> Tuple[npt.NDArray[np.float64], int]:
    """Calculates RMS envelope of the signal in time windows."""
    window_size: int = int(sample_rate * RMS_ANALYSIS_WINDOW_MS / 1000)
    padded = np.pad(signal, (0, window_size - len(signal) % window_size), mode="constant")
    reshaped = padded.reshape(-1, window_size)
    rms = np.sqrt(np.mean(reshaped ** 2, axis=1))
    return rms, window_size


def extract_decay_points(envelope: npt.NDArray[np.float64], window_ms: float = 10) -> Tuple[List[float], float, float]:
    """Extracts decay time points at regular dB intervals from the amplitude envelope."""
    env_db = amplitude_to_decibels(envelope)
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

def detect_transient_index_impact(signal: npt.NDArray[np.float64], sample_rate: int) -> int:
    """Detect main drum impact (largest energy slope)."""
    window = int(sample_rate * 0.002)  # 2 ms smoothing
    if window < 1:
        return 0
    energy = uniform_filter1d(signal**2, size=window)
    slope = np.diff(energy)
    # smooth slope to suppress noise
    slope = uniform_filter1d(slope, size=window)
    idx = int(np.argmax(slope))
    return idx


def detect_transient_index(signal: npt.NDArray[np.float64], sample_rate: int) -> int:
    """Detect transient start using energy rise + adaptive threshold."""

    # short window for energy analysis
    window_size = int(sample_rate * 0.002)  # 2 ms
    if window_size < 1:
        return 0
    # smoothed energy
    energy = uniform_filter1d(signal**2, size=window_size)
    # energy derivative (onset strength)
    diff = np.diff(energy)
    # normalize
    diff /= np.max(np.abs(diff)) + 1e-12
    # adaptive threshold
    threshold = np.mean(diff) + 3 * np.std(diff)
    hits = np.where(diff > threshold)[0]
    if len(hits) == 0:
        return int(np.argmax(diff))
    return int(hits[0])


def detect_transient_index_energy(signal: np.ndarray, sample_rate: int) -> int:
    """Detects the sample index where the transient (attack) begins using energy-based method."""
    window_size_sec = 0.002
    frame = int(sample_rate * window_size_sec)
    energy = uniform_filter1d(signal**2, size=frame)
    diff = np.diff(energy)
    idx = np.argmax(diff)
    return int(idx)

def detect_transient_index_stft(signal: npt.NDArray[np.float64], sample_rate: int) -> int:
    """Detects the sample index where the transient (attack) begins."""
    hop_size: int = 256
    stft_magnitude: npt.NDArray[np.float64] = np.abs(
        librosa.stft(signal, n_fft=1024, hop_length=hop_size)
    )
    spectral_flux: npt.NDArray[np.float64] = np.diff(stft_magnitude, axis=1)
    spectral_flux = np.maximum(spectral_flux, 0)
    spectral_flux = np.sum(spectral_flux, axis=0)
    spectral_flux /= np.max(spectral_flux) + 1e-9
    detection_threshold: float = 0.3
    for index, value in enumerate(spectral_flux):
        if value > detection_threshold:
            return index * hop_size
    return 0

def analyze_attack_and_decay(signal: npt.NDArray[np.float64], sample_rate: int) -> Tuple[float, float, int, int]:
    """Analyzes attack time, decay time, peak index, and transient index of audio signal."""

    amplitude_envelope: npt.NDArray[np.float64] = extract_amplitude_envelope_hilbert(signal)
    amplitude_envelope = smooth_envelope(amplitude_envelope, sample_rate)
    amplitude_envelope /= np.max(amplitude_envelope) + 1e-12
    peak_index: int = int(np.argmax(amplitude_envelope))

    
    transient_index = detect_transient_index(signal, sample_rate)
    transient_index_energy = detect_transient_index_energy(signal, sample_rate)
    transient_index_impact = detect_transient_index_impact(signal, sample_rate)
    transient_index_stft = detect_transient_index_stft(signal, sample_rate)
    # find 10% rise
    transient_index_raise: int = 0
    for index in range(peak_index):
        if amplitude_envelope[index] >= 0.1:
            transient_index_raise = index
            break

    # find 90% rise
    attack_end_index: int = peak_index
    for index in range(transient_index, peak_index):
        if amplitude_envelope[index] >= 0.9:
            attack_end_index = index
            break

    transient = round(transient_index / sample_rate * 1000.0, 1)
    transient_energy = round(transient_index_energy / sample_rate * 1000.0, 1)
    transient_impact = round(transient_index_impact / sample_rate * 1000.0, 1)
    transient_stft = round(transient_index_stft / sample_rate * 1000.0, 1)
    transient_raise = round(transient_index_raise / sample_rate * 1000.0, 1)
    attack_end = round(attack_end_index / sample_rate * 1000.0, 1)
    print(f"\tTransient indices: energy={transient_energy}, impact={transient_impact}, stft={transient_stft}, raise={transient_raise}. attack_end={attack_end}")
    
    attack_time_ms: float = (attack_end_index - transient_index) / sample_rate * 1000.0
    silence_threshold_linear: float = 10 ** (SILENCE_THRESHOLD_DB / 20.0)
    decay_index: Optional[int] = None
    for index in range(peak_index, len(amplitude_envelope)):
        if amplitude_envelope[index] < silence_threshold_linear:
            decay_index = index
            break
    if decay_index is None:
        decay_index = len(amplitude_envelope) - 1
    decay_time_ms: float = (decay_index - peak_index) / sample_rate * 1000.0
    return attack_time_ms, decay_time_ms, peak_index, transient_index


def find_dominant_frequency(signal: npt.NDArray[np.float64], sample_rate: int) -> float:
    """Finds the frequency with the highest amplitude in the signal."""
    if len(signal) < 512:
        return 0.0
    window: npt.NDArray[np.float64] = np.hanning(len(signal))
    spectrum: npt.NDArray[np.float64] = np.abs(np.fft.rfft(signal * window))
    spectrum = uniform_filter1d(spectrum, size=3)
    freqs: npt.NDArray[np.float64] = np.fft.rfftfreq(len(signal), 1.0 / sample_rate)
    return float(freqs[int(np.argmax(spectrum))])


def detect_resonance_frequency(signal: npt.NDArray[np.float64], sample_rate: int, start_index: int) -> Optional[float]:
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
    """Encodes time intervals as Base-62 deltas for compact storage, skipping -1 values."""
    valid_times = [t for t in times if t != -1]
    if not valid_times:
        return ""
    deltas: List[float] = [valid_times[0]]
    for i in range(1, len(valid_times)):
        deltas.append(valid_times[i] - valid_times[i - 1])
    return "_".join(
        number_to_base62(int(t))
        for t in deltas
    )


from pathlib import Path
from typing import Tuple

def extract_sample_metadata_from_path(path: str, input_root: str) -> Tuple[str, str, int, str, str]:
    """
    Extract metadata from filename only (no directory dependency, no regex).

    Format:
    <instrumentType>-<size>-<articulation>[optionalExpressionShortName]-<velocity><sampleVariant>-<micChain>-<otherOptionalSampleParams>.<ext>
    """
    fname: str = Path(path).stem
    parts = fname.split("-")
    if len(parts) < 4:
        return fname, "", 0, "", ""
    instrument = f"{parts[0]}-{parts[1]}"
    articulation = parts[2]
    vel_var = parts[3]
    velocity_str = ""
    sample_variant = ""
    for ch in vel_var:
        if ch.isdigit():
            velocity_str += ch
        else:
            sample_variant += ch
    velocity = int(velocity_str) if velocity_str else 0
    mic_chain = parts[4]
    return instrument, articulation, velocity, sample_variant, mic_chain


def construct_new_filename(fileName: str, peak_ms: float, peak_db: float, decay_ms: float, decay_code: str) -> str:
    """Constructs new filename with encoded sample analysis metadata.
    
    Format: <instrumentType>-<size>-<articulation>[optionalExpressionShortName]-<velocity><sampleVariant>-<micChain>-p{peak_db*-10}_{peak_ms}-d{decay}-c{decay_code}.<ext>
    
    Preserves base name (<instrumentType>-<size>-<articulation>[optionalExpressionShortName]-<velocity><sampleVariant>) in correct order.
    Updates existing metadata if file was already renamed.
    
    Example: 043a-ons-a38-d4960-p19-c34_5a_2U_76_6i_em_d4_kY_7K_0.flac
    """
    parts = fileName.rsplit(".", 1)
    name_without_ext = parts[0]
    ext = parts[1] if len(parts) > 1 else ""
    name_parts = name_without_ext.split("-")
    # Base name is always first 5 parts: <instrumentType>-<size>-<articulation>[optionalExpressionShortName]-<velocity><sampleVariant>
    if len(name_parts) >= 5:
        base_name = "-".join(name_parts[:5])
    else:
        base_name = name_without_ext
    # Build optional params with latest values
    decay_ms_int = int(round(decay_ms))
    peak_db_int = int(round(abs(peak_db*10)))
    peak_ms_int = int(round(peak_ms))
    optional_params = f"p{peak_db_int}_{peak_ms_int}-d{decay_ms_int}-c{decay_code}"
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
        if NAME_FILTER_REGEX and not NAME_FILTER_REGEX.search(fname):
            continue
        files.append(os.path.join(root, fname))

files.sort()


start_time: float = time.perf_counter()
total_files: int = len(files)

print(f"Found {total_files} audio files")

for n, path in enumerate(files, start=1):
    print(f"Processing ({n}/{total_files}): {os.path.relpath(path, INPUT_FOLDER)}")

    audio: npt.NDArray[np.float64]
    sample_rate: int
    audio, sample_rate = sf.read(path)

    if audio.ndim > 1: # convert to mono if multi-channel
        audio = np.sqrt(np.mean(audio**2, axis=1)) #np.mean(audio, axis=1)

    meter: pyln.Meter = pyln.Meter(sample_rate)

    try:
        lufs: float = meter.integrated_loudness(audio)
    except ValueError:
        lufs = float("-inf")

    amplitude_envelope: npt.NDArray[np.float64] = extract_amplitude_envelope_hilbert(audio)
    # amplitude_envelope = smooth_envelope(amplitude_envelope, sample_rate)
    # amplitude_envelope /= np.max(amplitude_envelope) + 1e-9
    envelope_db = amplitude_to_decibels(amplitude_envelope)
    amp_peak_db: float = float(np.max(envelope_db))
    amp_peak_ms: float = (np.argmax(envelope_db)) / sample_rate * 1000.0

    attack_ms, rms_decay_ms, peak_idx, transient_idx = analyze_attack_and_decay(audio, sample_rate)

    envelope_rms_db: np.ndarray
    window_rms_ms: int
    envelope_rms_db, window_rms_ms = calculate_rms_envelope(audio, sample_rate)

    rms_decay_points_db, rms_peak_ms, rms_peak_db = extract_decay_points(envelope_rms_db)

    decay_code: str = encode_time_intervals_as_base62(rms_decay_points_db)

    resonance_hz = detect_resonance_frequency(audio, sample_rate, peak_idx + int(0.1 * sample_rate))
    if resonance_hz is None:
        resonance_hz = find_dominant_frequency(audio[peak_idx:], sample_rate)

    length_ms: float = len(audio) / sample_rate * 1000

    instrument, articulation, velocity, sample_variant, mic_chain = extract_sample_metadata_from_path(path, INPUT_FOLDER)

    row: List[float | int | str] = [
        # os.path.relpath(path, INPUT_FOLDER),
        instrument,
        articulation,
        velocity,
        sample_variant,
        mic_chain,
        int(length_ms),
        int(rms_decay_ms),
        int(amp_peak_ms),
        int(rms_peak_ms),
        round(amp_peak_db, 2),
        round(rms_peak_db, 2),
        round(lufs, 2),
        decay_code,
        *[int(x) for x in rms_decay_points_db],
    ]
    # print("  " + "\t".join(str(x) for x in row))

    rows.append(row)

    # Optionally rename file with encoded metadata
    if RENAME_FILES:
        file_dir: str = os.path.dirname(path)
        old_filename: str = os.path.basename(path)
        new_filename: str = construct_new_filename(old_filename, rms_peak_ms, rms_peak_db, rms_decay_ms, decay_code)

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
        "rms_decay_ms",
        "amp_peak_ms",
        "rms_peak_ms",
        "amp_peak_db",
        "rms_peak_db",
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

print(f"\nDone -> {OUTPUT_CSV}")
print(f"Processed {total_files} files in {elapsed:.2f} seconds ({elapsed/60:.2f} minutes)")
