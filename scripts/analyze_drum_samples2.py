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
from typing import List, Tuple, Optional
from scipy.signal import hilbert
from scipy.ndimage import uniform_filter1d
from pathlib import Path


SUPPORTED_EXTS: Tuple[str, ...] = (
    ".wav", ".bwf", ".flac", ".aif", ".aiff",
    ".ogg", ".m4a", ".mp3", ".wmv", ".asf", ".wm", ".wma"
)

RESONANCE_MIN_HZ: float = 80.0
RESONANCE_MAX_HZ: float = 18000.0
RESONANCE_WINDOW_MS: float = 200.0
WINDOW_MS: int = 10

BASE62_CHARACTERS: str = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


# =========================
# USER SETTINGS
# =========================

parser = argparse.ArgumentParser(
    description="Batch analyze drum one-shot samples (resonance-based)",
    formatter_class=argparse.RawTextHelpFormatter,
    epilog=textwrap.dedent("""
Example:
  python .\\scripts\\analyze_drum_samples.py -i C:\\Users\\Public\\Documents\\Karatala\\sample_library
  python analyze_drum_samples.py -i "./samples" -o results.csv

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

args: argparse.Namespace = parser.parse_args()

INPUT_FOLDER: str = os.path.abspath(args.input_folder)
OUTPUT_CSV: str = args.output_csv
SILENCE_THRESHOLD_DB: float = args.silence_db
NAME_FILTER: Optional[str] = args.name_filter


# =========================
# HELPERS
# =========================

def convert_number_to_base62(number: int) -> str:
    result: str = ""
    while number > 0:
        result = BASE62_CHARACTERS[number % 62] + result
        number //= 62
    return result or "0"


def dbfs(values: npt.NDArray[np.float64]) -> npt.NDArray[np.float64]:
    return 20 * np.log10(np.maximum(np.abs(values), 1e-9))


def get_amplitude_envelope(signal: npt.NDArray[np.float64]) -> npt.NDArray[np.float64]:
    return np.abs(hilbert(signal))


def smooth_envelope(
    amplitude_envelope: npt.NDArray[np.float64],
    sample_rate: int,
    window_ms: float = 5.0
) -> npt.NDArray[np.float64]:
    window_size: int = int(sample_rate * window_ms / 1000)
    if window_size < 1:
        return amplitude_envelope
    smoothing_kernel: npt.NDArray[np.float64] = np.ones(window_size) / window_size
    return np.convolve(amplitude_envelope, smoothing_kernel, mode="same")


def calculate_rms_envelope(signal: npt.NDArray[np.float64], sample_rate: int) -> Tuple[npt.NDArray[np.float64], int]:
    window_size: int = int(sample_rate * WINDOW_MS / 1000)
    envelope_values: List[float] = []
    for start_idx in range(0, len(signal), window_size):
        chunk: npt.NDArray[np.float64] = signal[start_idx:start_idx + window_size]
        if chunk.size == 0:
            break
        rms_values: float = float(np.sqrt(np.mean(chunk ** 2)))
        envelope_values.append(rms_values)
    return np.array(envelope_values), window_size


def extract_decay_points(envelope: npt.NDArray[np.float64], window_ms: float = 10) -> Tuple[List[float], float, float]:
    envelope_db: npt.NDArray[np.float64] = dbfs(envelope)
    peak_index: int = int(np.argmax(envelope_db))
    peak_level_db: float = float(envelope_db[peak_index])
    decay_levels_db: List[int] = [-6, -12, -18, -24, -30, -36, -42, -48, -54, -60]
    decay_times_ms: List[float] = []
    for level_db in decay_levels_db:
        target_db: float = peak_level_db + level_db
        for idx in range(peak_index, len(envelope_db)):
            if envelope_db[idx] <= target_db:
                decay_times_ms.append(idx * window_ms)
                break
    peak_time_ms: float = peak_index * window_ms
    return decay_times_ms, peak_time_ms, peak_level_db


def detect_transient_sample(signal: npt.NDArray[np.float64], sample_rate: int) -> int:
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


def analyze_adsr(signal: npt.NDArray[np.float64], sample_rate: int) -> Tuple[float, float, int, int]:
    transient_index: int = detect_transient_sample(signal, sample_rate)
    amplitude_envelope: npt.NDArray[np.float64] = get_amplitude_envelope(signal)
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


def dominant_frequency(signal: npt.NDArray[np.float64], sample_rate: int) -> float:
    if len(signal) < 512:
        return 0.0
    window: npt.NDArray[np.float64] = np.hanning(len(signal))
    spectrum: npt.NDArray[np.float64] = np.abs(np.fft.rfft(signal * window))
    freqs: npt.NDArray[np.float64] = np.fft.rfftfreq(len(signal), 1.0 / sample_rate)
    return float(freqs[int(np.argmax(spectrum))])


def detect_resonance_frequency(signal: npt.NDArray[np.float64], sample_rate: int, start_index: int) -> Optional[float]:
    num_samples: int = int(sample_rate * RESONANCE_WINDOW_MS / 1000)
    signal_tail: npt.NDArray[np.float64] = signal[start_index:start_index + num_samples]
    if len(signal_tail) < 512:
        return None
    window: npt.NDArray[np.float64] = np.hanning(len(signal_tail))
    spectrum: npt.NDArray[np.float64] = np.abs(np.fft.rfft(signal_tail * window))
    frequencies: npt.NDArray[np.float64] = np.fft.rfftfreq(len(signal_tail), 1.0 / sample_rate)
    valid_range_mask: npt.NDArray[np.bool_] = (frequencies >= RESONANCE_MIN_HZ) & (frequencies <= RESONANCE_MAX_HZ)
    if not np.any(valid_range_mask):
        return None
    filtered_spectrum = spectrum[valid_range_mask]
    filtered_frequencies = frequencies[valid_range_mask]
    peak_idx: int = int(np.argmax(filtered_spectrum))
    return float(filtered_frequencies[peak_idx])


def encode_relative_times(times: List[float]) -> str:
    if not times:
        return ""
    deltas: List[float] = [times[0]]
    for i in range(1, len(times)):
        deltas.append(times[i] - times[i - 1])
    return "_".join(
        convert_number_to_base62(int(t))
        for t in deltas
    )


def parse_sample_params(path: str, input_root: str) -> Tuple[str, str, int, str, str]:
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
        if not fname_lower.endswith(SUPPORTED_EXTS):
            continue
        if NAME_FILTER and NAME_FILTER not in fname_lower:
            continue
        files.append(os.path.join(root, fname))

files.sort()

print(f"Found {len(files)} audio files")



for path in files:
    print(f"Processing: {path}")

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

    peak_db: float = float(np.max(dbfs(audio)))

    attack_ms, decay_ms, peak_idx, transient_idx = analyze_adsr(audio, sample_rate)

    env: np.ndarray
    win: int
    env, win = calculate_rms_envelope(audio, sample_rate)

    decay_points, peak_time, peak_level_db = extract_decay_points(env)

    decay_code: str = encode_relative_times(decay_points)

    res_freq_hz = detect_resonance_frequency(audio, sample_rate, peak_idx)
    if res_freq_hz is None:
        res_freq_hz = dominant_frequency(audio[peak_idx:], sample_rate)

    centroid_hz: float = float(
        np.mean(
            librosa.feature.spectral_centroid(
                y=audio[: int(sample_rate * 0.5)],
                sr=sample_rate
            )
        )
    )

    length_ms: float = len(audio) / sample_rate * 1000

    instrument, articulation, velocity, sample_variant, mic_chain = parse_sample_params(path, INPUT_FOLDER)

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
        round(res_freq_hz, 1),
        round(centroid_hz, 1),
        decay_code,
        *decay_points
    ]
    rows.append(row)

with open(OUTPUT_CSV, "w", newline="", encoding="utf-8") as f:
    writer = csv.writer(f)
    writer.writerow([
        "file",
        "instrumentId",
        "articulation",
        "velocity",
        "sampleVariant",
        "micChain",
        "Length_ms",
        "Attack_ms",
        "Decay_ms",
        "LUFS",
        "Peak_dBFS",
        "Peak_dB",
        "Resonance_Hz",
        "SpectralCentroid_Hz",
        "Decay_code",
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

print(f"\nDone → {OUTPUT_CSV}")
