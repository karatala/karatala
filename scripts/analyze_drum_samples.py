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


def amplitude_to_decibels(values: npt.NDArray[np.float64]) -> npt.NDArray[np.float64]:
    """Converts amplitude values to decibels (dB) scale."""
    return 20 * np.log10(np.maximum(np.abs(values), 1e-9))


def extract_amplitude_envelope(signal: npt.NDArray[np.float64]) -> npt.NDArray[np.float64]:
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
    decay_times: list[float] = []
    for t in targets:
        idx = np.argmax(tail <= t)
        if tail[idx] <= t:
            decay_times.append((idx + peak_idx) * window_ms)
    peak_time = peak_idx * window_ms
    return decay_times, peak_time, peak_db


def detect_transient_index(signal: npt.NDArray[np.float64]) -> int:
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
    transient_index: int = detect_transient_index(signal)
    amplitude_envelope: npt.NDArray[np.float64] = extract_amplitude_envelope(signal)
    amplitude_envelope = smooth_envelope(amplitude_envelope, sample_rate)
    amplitude_envelope /= np.max(amplitude_envelope) + 1e-9
    peak_index: int = int(np.argmax(amplitude_envelope))
    attack_end_index: int = peak_index
    for index in range(transient_index, peak_index):
        if amplitude_envelope[index] >= 0.9:
            attack_end_index = index
            break
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
    freqs: npt.NDArray[np.float64] = np.fft.rfftfreq(len(signal), 1.0 / sample_rate)
    return float(freqs[int(np.argmax(spectrum))])


def detect_resonance_frequency(signal: npt.NDArray[np.float64], sample_rate: int, start_index: int) -> Optional[float]:
    """Detects the resonant frequency of the signal starting from a given index."""
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


def construct_new_filename(fileName: str, attack_ms: float, decay_ms: float, peak_db: float, decay_code: str) -> str:
    """Constructs new filename with encoded sample analysis metadata.
    
    Format: <velocity><sampleVariant>-<micChain>-a{attack}-d{decay}-p{peak}-c{decay_code}.<ext>
    
    Preserves base name (<velocity><sampleVariant>-<micChain>) in correct order.
    Updates existing metadata if file was already renamed.
    
    Example: 043a-oni-a38-d4960-p19-c34_5a_2U_76_6i_em_d4_kY_7K_0.flac
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
    attack_int = int(round(attack_ms))
    decay_int = int(round(decay_ms))
    peak_int = int(round(abs(peak_db*10)))
    
    optional_params = f"a{attack_int}-d{decay_int}-p{peak_int}-c{decay_code}"
    
    new_name = f"{base_name}-{optional_params}"
    if ext:
        new_name += f".{ext}"
    
    return new_name


def extract_sample_metadata_from_path(path: str, input_root: str) -> Tuple[str, str, int, str, str]:
    """Extracts instrument, articulation, velocity, and other metadata from file path structure."""
    rel: str = os.path.relpath(path, input_root)
    parts = Path(rel).parts
    instrument = ""
    articulation = ""
    velocity = 0
    sample_variant = ""
    mic_chain = ""
    if len(parts) >= 3:
        inst_folder = parts[-3]
        inst_parts = inst_folder.split("-")
        if len(inst_parts) >= 2:
            instrument = "-".join(inst_parts[:2])
        else:
            instrument = inst_folder
    if len(parts) >= 2:
        articulation = parts[-2].split("-")[0]
    fname: str = Path(parts[-1]).stem
    m = re.match(r"(\d+)([a-z]?)-([a-zA-Z0-9_]+)", fname)
    if m:
        velocity = int(m.group(1))
        sample_variant = m.group(2)
        mic_chain = m.group(3)
    return instrument, articulation, velocity, sample_variant, mic_chain

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

    audio: npt.NDArray[np.float64]
    sample_rate: int
    audio, sample_rate = sf.read(path)

    if audio.ndim > 1:
        audio = np.mean(audio, axis=1)

    meter: pyln.Meter = pyln.Meter(sample_rate)

    try:
        lufs: float = meter.integrated_loudness(audio)
    except ValueError:
        lufs = float("-inf")

    peak_db: float = float(np.max(amplitude_to_decibels(audio)))

    attack_ms, decay_ms, peak_idx, transient_idx = analyze_attack_and_decay(audio, sample_rate)

    env: np.ndarray
    win: int
    env, win = calculate_rms_envelope(audio, sample_rate)

    decay_points, peak_time, peak_level_db = extract_decay_points(env)

    decay_code: str = encode_time_intervals_as_base62(decay_points)

    resonance_hz = detect_resonance_frequency(audio, sample_rate, peak_idx)
    if resonance_hz is None:
        resonance_hz = find_dominant_frequency(audio[peak_idx:], sample_rate)

    length_ms: float = len(audio) / sample_rate * 1000

    instrument, articulation, velocity, sample_variant, mic_chain = extract_sample_metadata_from_path(path, INPUT_FOLDER)

    row: List[float | int | str] = [
        os.path.relpath(path, INPUT_FOLDER),
        instrument,
        articulation,
        velocity,
        sample_variant,
        mic_chain,
        round(length_ms, 1),
        round(attack_ms, 1),
        round(decay_ms, 1),
        round(lufs, 2),
        round(peak_db, 2),
        round(peak_level_db, 2),
        round(resonance_hz, 1),
        decay_code,
        *decay_points
    ]
    rows.append(row)

    # Optionally rename file with encoded metadata
    if RENAME_FILES:
        file_dir = os.path.dirname(path)
        old_filename = os.path.basename(path)
        new_filename = construct_new_filename(old_filename, attack_ms, decay_ms, peak_db, decay_code)
        
        if new_filename != old_filename:
            new_path = os.path.join(file_dir, new_filename)
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
        "file",
        "instrument",
        "articulation",
        "velocity",
        "sample_variant",
        "mic_chain",
        "length_ms",
        "attack_ms",
        "decay_ms",
        "lufs",
        "peak_db",
        "peak_level_db",
        "resonance_hz",
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
