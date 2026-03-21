import os
import csv
import argparse
import textwrap
import numpy as np
import numpy.typing as npt
import soundfile as sf
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
  pip install numpy soundfile pyloudnorm scipy
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


def schroeder_decay_curve(
    amplitude_linear: npt.NDArray[np.float64],
    sample_rate: int
) -> tuple[npt.NDArray[np.float64], npt.NDArray[np.float64]]:
    """
    Computes the Schroeder decay curve from a linear amplitude signal.

    Input
    -----
    amplitude_linear
        Linear amplitude values.
    sample_rate
        Sample rate

    Returns
    -------
    time_axis_sec
        Time axis in seconds.
    decay_db
        Decay curve in decibels.
    """
    instantaneous_energy_linear = amplitude_linear ** 2
    # reverse cumulative sum -> Schroeder integration
    schroeder_energy_linear = np.cumsum(instantaneous_energy_linear[::-1])[::-1]
    # normalize to 0 dB
    schroeder_energy_linear /= np.max(schroeder_energy_linear) + 1e-12
    decay_db = 10 * np.log10(schroeder_energy_linear + 1e-12)
    time_axis_sec = np.arange(len(decay_db)) / sample_rate
    return time_axis_sec, decay_db


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
    sample_rate: int,
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
    smoothing_window_samples = int(sample_rate * smoothing_window_ms / 1000)
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


def compute_rms_envelope_per_window(
    audio_waveform_float: npt.NDArray[np.float64],
    sample_rate: int,
    window_ms: float = 10
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
    analysis_window_samples = int(sample_rate * window_ms / 1000)
    padded = np.pad(audio_waveform_float, (0, analysis_window_samples - len(audio_waveform_float) % analysis_window_samples), mode="constant")
    reshaped = padded.reshape(-1, analysis_window_samples)
    rms_envelope_linear = np.sqrt(np.mean(reshaped ** 2, axis=1))
    return rms_envelope_linear, analysis_window_samples


def extract_decay_points(envelope_db: npt.NDArray[np.float64], window_ms: float = 10) -> Tuple[List[float], float, float]:
    """Extracts decay time points at regular dB intervals from the envelope."""
    peak_idx = int(np.argmax(envelope_db))
    peak_db = float(envelope_db[peak_idx])
    decay_levels = np.array([-6,-12,-18,-24,-30,-36,-42,-48,-54,-60])
    targets = peak_db + decay_levels
    tail = envelope_db[peak_idx:]
    decay_times: List[float] = []
    for target in targets:
        hits = np.where(tail <= target)[0]
        if len(hits) > 0:
            idx = hits[0]
            decay_times.append((idx + peak_idx) * window_ms)
    peak_ms = peak_idx * window_ms
    return decay_times, peak_ms, peak_db



    
def compute_decay_time_ms_from_envelope_frames(
    envelope_db: npt.NDArray[np.float64],
    frame_duration_ms: float,
    decay_range_db: float = 60.0
) -> float:
    """
    Measures decay time relative to the signal's own peak.

    Decay time = time until envelope falls (decay_range_db)
    below its peak level.

    Example:
        If peak = -3 dB and decay_range_db = 60,
        target level = -63 dB.

    Parameters
    ----------
    envelope_db
        Envelope in dB.

    frame_duration_ms
        Duration of each frame in milliseconds.

    decay_range_db
        How far below peak to measure decay (default: 60 dB).

    Returns
    -------
    decay_time_ms
    """

    peak_index = int(np.argmax(envelope_db))
    peak_db = float(envelope_db[peak_index])

    target_db = peak_db - decay_range_db

    tail = envelope_db[peak_index:]

    for i in range(len(tail)):
        if tail[i] <= target_db:
            return i * frame_duration_ms

    return len(tail) * frame_duration_ms


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
    parts = fileName.rsplit(".", 1)
    name_without_ext = parts[0]
    ext = parts[1] if len(parts) > 1 else ""
    name_parts = name_without_ext.split("-")
    # Base name is always first 2 parts: <velocity><sampleVariant>-<micChain>
    if len(name_parts) >= 2:
        base_name = "-".join(name_parts[:2])
    else:
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
    print(f"Processing ({n}/{total_files}): {os.path.relpath(path, INPUT_FOLDER)}")

    audio_waveform: npt.NDArray[np.float64]
    sample_rate: int
    audio_waveform, sample_rate = sf.read(path)

    audio_waveform = convert_multichannel_audio_to_mono_rms(audio_waveform)

    meter: pyln.Meter = pyln.Meter(sample_rate)
    try:
        lufs: float = meter.integrated_loudness(audio_waveform)
    except ValueError:
        lufs = float("-inf")


    amp_envelope_linear: npt.NDArray[np.float64]
    amp_envelope_linear = compute_amplitude_envelope_via_hilbert(audio_waveform)
    amp_envelope_linear = smooth_amplitude_envelope(amp_envelope_linear, sample_rate)
    # amp_envelope = smooth_amplitude_envelope(amp_envelope, sample_rate)
    # amp_envelope /= np.max(amp_envelope) + 1e-9
    amp_envelope_db: npt.NDArray[np.float64] = convert_linear_amplitude_to_db(amp_envelope_linear)

    amp_peak_index = int(np.argmax(amp_envelope_db))

    amp_frame_ms = 1000.0 / sample_rate

    amp_peak_db: float = float(np.max(amp_envelope_db))
    amp_peak_ms: float = amp_peak_index * amp_frame_ms
    amp_decay_ms: float = compute_decay_time_ms_from_envelope_frames(amp_envelope_db, amp_frame_ms, -SILENCE_THRESHOLD_DB)
    
    amp_decay_points_db, _, _ = extract_decay_points(amp_envelope_db, amp_frame_ms)


    rms_window_samples: int
    
    rms_envelope_linear: npt.NDArray[np.float64]
    rms_envelope_linear, rms_window_samples = compute_rms_envelope_per_window(audio_waveform, sample_rate, RMS_ANALYSIS_WINDOW_MS)

    rms_envelope_db: npt.NDArray[np.float64] = convert_linear_amplitude_to_db(rms_envelope_linear)

    rms_peak_index: int = int(np.argmax(rms_envelope_db))

    rms_window_ms: float = rms_window_samples / sample_rate * 1000

    rms_peak_db: float = float(rms_envelope_db[rms_peak_index])
    rms_peak_ms: float = rms_peak_index * rms_window_ms
    rms_decay_ms: float = compute_decay_time_ms_from_envelope_frames(rms_envelope_db, rms_window_ms, -SILENCE_THRESHOLD_DB)

    rms_decay_points_db, _, _ = extract_decay_points(rms_envelope_db, rms_window_ms)
    # print(f"  2Peak: {amp_peak_db:.1f} dB @ {amp_peak_ms:.1f}ms   Decay: {amp_decay_ms:.1f}ms. RMS Peak: {rms_peak_db:.1f} dB @ {rms_peak_ms:.1f}ms   RMS Decay: {rms_decay_ms:.1f}ms")

    # print(f"\n Decay points (dB): {[float(x) for x in amp_decay_points_db]}\n\n")
    # print(f"\n Decay points (dB): {[float(x) for x in rms_decay_points_db]}\n\n")

    decay_code: str = encode_time_intervals_as_base62([float(x) for x in amp_decay_points_db])
    
    length_ms: float = len(audio_waveform) / sample_rate * 1000

    instrument, articulation, velocity, sample_variant, mic_chain = extract_sample_metadata_from_path(path, INPUT_FOLDER)

    row: List[float | int | str] = [
        # os.path.relpath(path, INPUT_FOLDER),
        instrument,
        articulation,
        velocity,
        sample_variant,
        mic_chain,
        round(length_ms, 0),
        round(rms_peak_ms, 0),
        round(rms_decay_ms, 0),
        round(rms_peak_db, 2),
        round(lufs, 2),
        decay_code,
        *[float(x) for x in rms_decay_points_db],
        round(amp_peak_ms, 0),
        round(amp_decay_ms, 0),
        round(amp_peak_db, 2),
        *[round(float(x),0) for x in amp_decay_points_db],
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
        "rms_peak_ms",
        "rms_decay_ms",
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
        "amp_peak_ms",
        "amp_decay_ms",
        "amp_peak_db",
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
