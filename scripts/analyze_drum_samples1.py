import os
import csv
import argparse
import textwrap
import numpy as np
import soundfile as sf
import librosa
import pyloudnorm as pyln
from scipy.signal import hilbert


SUPPORTED_EXTS = (".wav", ".bwf", ".flac", ".aif", ".aiff", ".ogg", ".m4a", ".mp3", ".wmv", ".asf", ".wm", ".wma")
RESONANCE_MIN_HZ = 80.0
RESONANCE_MAX_HZ = 18000.0
RESONANCE_WINDOW_MS = 200.0

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

parser = argparse.ArgumentParser(
    description="Batch analyze drum one-shot samples (resonance-based)"
)

parser.add_argument(
    "-i", "--input-folder",
    required=True,
    help="Folder containing audio files (.wav, .bwf, .flac, .aif, .aiff, .ogg, .m4a, .mp3, .wmv, .asf, .wm, .wma)"
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
    help="Silence threshold in dB for release detection (default: -75)"
)

args = parser.parse_args()

INPUT_FOLDER = os.path.abspath(args.input_folder)
OUTPUT_CSV = args.output_csv
SILENCE_THRESHOLD_DB = args.silence_db

# =========================
# HELPERS
# =========================

def dbfs(x: np.ndarray) -> np.ndarray:
    return 20 * np.log10(np.maximum(np.abs(x), 1e-9))


def get_envelope(signal: np.ndarray) -> np.ndarray:
    return np.abs(hilbert(signal))


def smooth_envelope(env: np.ndarray, sr: int, window_ms: float = 5.0):
    win = int(sr * window_ms / 1000.0)
    if win < 1:
        return env
    kernel = np.ones(win) / win
    return np.convolve(env, kernel, mode="same")


def detect_transient_sample(signal: np.ndarray, sr: int):
    hop = 256
    n_fft = 1024

    stft = np.abs(librosa.stft(signal, n_fft=n_fft, hop_length=hop))

    flux = np.diff(stft, axis=1)
    flux = np.maximum(flux, 0)
    flux = np.sum(flux, axis=0)

    flux /= np.max(flux) + 1e-9

    thresh = 0.3

    for i, v in enumerate(flux):
        if v > thresh:
            return i * hop

    return 0


def analyze_adsr(signal: np.ndarray, sr: int):

    transient_idx = detect_transient_sample(signal, sr)

    env = get_envelope(signal)
    env = smooth_envelope(env, sr)
    env /= np.max(env) + 1e-9

    peak_idx = int(np.argmax(env))

    attack_end = peak_idx
    for i in range(transient_idx, peak_idx):
        if env[i] >= 0.9:
            attack_end = i
            break

    attack_ms = (attack_end - transient_idx) / sr * 1000.0

    silence_thresh = 10 ** (SILENCE_THRESHOLD_DB / 20.0)

    decay_idx = None
    for i in range(peak_idx, len(env)):
        if env[i] < silence_thresh:
            decay_idx = i
            break

    if decay_idx is None:
        decay_idx = len(env) - 1

    decay_ms = (decay_idx - peak_idx) / sr * 1000.0

    return attack_ms, decay_ms, peak_idx


def dominant_frequency(signal: np.ndarray, sr: int):
    if len(signal) < 512:
        return 0.0

    window = np.hanning(len(signal))
    spectrum = np.abs(np.fft.rfft(signal * window))
    freqs = np.fft.rfftfreq(len(signal), 1.0 / sr)

    return float(freqs[np.argmax(spectrum)])


def detect_resonance_frequency(signal, sr, start_idx):
    n_samples = int(sr * RESONANCE_WINDOW_MS / 1000.0)
    tail = signal[start_idx:start_idx + n_samples]

    if len(tail) < 512:
        return None

    window = np.hanning(len(tail))
    spectrum = np.abs(np.fft.rfft(tail * window))
    freqs = np.fft.rfftfreq(len(tail), 1.0 / sr)

    mask = (freqs >= RESONANCE_MIN_HZ) & (freqs <= RESONANCE_MAX_HZ)

    if not np.any(mask):
        return None

    spectrum = spectrum[mask]
    freqs = freqs[mask]

    return float(freqs[np.argmax(spectrum)])

# =========================
# MAIN
# =========================

files = []

for root, _, filenames in os.walk(INPUT_FOLDER, followlinks=True):
    print(f"[DEBUG] Checking folder: {root}")
    for fname in filenames:
        if fname.lower().endswith(SUPPORTED_EXTS) and "a-" in fname.lower():
            files.append(os.path.join(root, fname))

files.sort()
print(f"Found {len(files)} audio files")

rows = []

for path in files:
    print(f"Processing: {path}")

    audio, sr = sf.read(path)
    if audio.ndim > 1:
        audio = np.mean(audio, axis=1)

    meter = pyln.Meter(sr)

    try:
        lufs = meter.integrated_loudness(audio)
    except ValueError:
        lufs = -np.inf

    peak_db = float(np.max(dbfs(audio)))

    attack_ms, decay_ms, peak_idx = analyze_adsr(audio, sr)

    res_freq_hz = detect_resonance_frequency(audio, sr, peak_idx)
    if res_freq_hz is None:
        res_freq_hz = dominant_frequency(audio[peak_idx:], sr)

    centroid = float(
        np.mean(
            librosa.feature.spectral_centroid(
                y=audio[: int(sr * 0.5)],
                sr=sr
            )
        )
    )

    length_ms = len(audio) / sr * 1000.0

    rows.append([
        os.path.relpath(path, INPUT_FOLDER),
        round(length_ms, 1),
        round(attack_ms, 1),
        round(decay_ms, 1),
        round(lufs, 2),
        round(peak_db, 2),
        round(res_freq_hz, 1),
        round(centroid, 1),
    ])

# =========================
# WRITE CSV
# =========================

with open(OUTPUT_CSV, "w", newline="", encoding="utf-8") as f:
    writer = csv.writer(f)
    writer.writerow([
        "file",
        "Length_ms",
        "Attack_ms",
        "Decay_ms",
        "LUFS",
        "Peak_dBFS",
        "ResonanceFrequency_Hz",
        "SpectralCentroid_Hz",
    ])
    writer.writerows(rows)

print(f"\nDone → {OUTPUT_CSV}")
