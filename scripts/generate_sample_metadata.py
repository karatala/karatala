import argparse
import csv
import hashlib
import math
import os
import shutil
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import numpy as np
import numpy.typing as npt
import pyloudnorm as pyln
import soundfile as sf
from scipy.signal import resample_poly

from sample_metadata_reports import write_phase6_reports


SUPPORTED_AUDIO_EXTENSIONS = (".wav", ".flac", ".aiff", ".mp3", ".ogg")
DECAY_LEVELS_DB = (6, 12, 18, 24, 30, 36, 42, 48, 54, 60)
CSV_COLUMNS = (
    "relative_path",
    "mic_code",
    "content_hash",
    "file_size_bytes",
    "onset_ms",
    "intensity_db",
    "true_peak_db",
    "peak_db",
    "peak_ms",
    "decay_end_ms",
    "stereo_balance",
    "-6db_ms",
    "-12db_ms",
    "-18db_ms",
    "-24db_ms",
    "-30db_ms",
    "-36db_ms",
    "-42db_ms",
    "-48db_ms",
    "-54db_ms",
    "-60db_ms",
)


@dataclass(frozen=True)
class AnalysisSettings:
    intensity_window_ms: int
    balance_window_ms: int
    onset_frame_ms: int
    onset_relative_threshold_db: float
    onset_absolute_threshold_db: float
    decay_window_ms: int
    decay_silence_threshold_db: float
    decay_silence_hold_ms: int
    true_peak_oversample_factor: int


@dataclass(frozen=True)
class MicDescriptor:
    code: str
    channel_format: str
    channel_start: int
    channel_count: int


@dataclass(frozen=True)
class SampleFile:
    path: Path
    relative_path: str
    instrument_folder: str
    articulation_folder: str
    velocity: int
    round_robin: str
    mic_descriptors: tuple[MicDescriptor, ...]
    parser_warnings: tuple[str, ...]


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Measure one kit or sample library and generate one metadata.csv per kit.")
    parser.add_argument("-i", "--input", required=True, help="Kit root or sample-library root")
    parser.add_argument("--intensity-window-ms", type=int, default=400, help="K-weighted intensity window from detected onset")
    parser.add_argument("--balance-window-ms", type=int, default=200, help="Stereo-balance window from detected onset")
    parser.add_argument("--onset-frame-ms", type=int, default=5, help="Non-overlapping onset frame length")
    parser.add_argument("--onset-relative-threshold-db", type=float, default=-30.0, help="Onset threshold relative to the strongest frame")
    parser.add_argument("--onset-absolute-threshold-db", type=float, default=-60.0, help="Minimum absolute onset threshold")
    parser.add_argument("--decay-window-ms", type=int, default=10, help="RMS decay frame length")
    parser.add_argument("--decay-silence-threshold-db", type=float, default=-72.0, help="Absolute decay-end threshold")
    parser.add_argument("--decay-silence-hold-ms", type=int, default=30, help="Time that must remain below the decay threshold")
    parser.add_argument("--true-peak-oversample-factor", type=int, default=4, help="True-peak oversampling factor")
    parser.add_argument("--report-folder", help="Optional external folder for Phase 6 balance, room-orientation, and timing CSV reports")
    return parser.parse_args()


def validate_settings(settings: AnalysisSettings) -> None:
    positive_values = {
        "intensity window": settings.intensity_window_ms,
        "balance window": settings.balance_window_ms,
        "onset frame": settings.onset_frame_ms,
        "decay window": settings.decay_window_ms,
        "decay silence hold": settings.decay_silence_hold_ms,
        "true-peak oversample factor": settings.true_peak_oversample_factor,
    }
    invalid_names = [name for name, value in positive_values.items() if value <= 0]
    if invalid_names:
        raise ValueError("These settings must be positive: " + ", ".join(invalid_names))


def find_audio_files(root_path: Path) -> list[Path]:
    return sorted(path for path in root_path.rglob("*") if path.is_file() and path.suffix.lower() in SUPPORTED_AUDIO_EXTENSIONS)


def discover_kit_roots(input_path: Path) -> list[Path]:
    audio_files = find_audio_files(input_path)
    if not audio_files:
        raise ValueError(f"No supported audio files found under {input_path}")

    relative_depths = {len(path.relative_to(input_path).parts) for path in audio_files}
    if relative_depths == {3}:
        return [input_path]
    if relative_depths != {4}:
        depths_text = ", ".join(str(depth) for depth in sorted(relative_depths))
        raise ValueError(f"Expected kit/instrument/articulation/file or instrument/articulation/file layout, found path depths: {depths_text}")

    kit_roots = sorted({input_path / path.relative_to(input_path).parts[0] for path in audio_files})
    return [kit_root for kit_root in kit_roots if kit_root.is_dir()]


def parse_mic_chain(mic_chain: str) -> tuple[tuple[MicDescriptor, ...], list[str]]:
    descriptors = []
    warnings = []
    channel_start = 0
    valid_positions = {"d": "ustbio", "o": "nw", "r": "cf"}
    for mic_code in [part for part in mic_chain.split("_") if part]:
        if len(mic_code) < 3 or len(mic_code) > 4:
            warnings.append(f"Skipping invalid mic code '{mic_code}': expected 3 or 4 characters")
            continue
        mic_type = mic_code[0]
        position = mic_code[1]
        channel_format = mic_code[2]
        if mic_type not in valid_positions or position not in valid_positions[mic_type] or channel_format not in "msi":
            warnings.append(f"Skipping invalid mic code '{mic_code}'")
            continue
        channel_count = 1 if channel_format == "m" else 2
        descriptors.append(MicDescriptor(mic_code, channel_format, channel_start, channel_count))
        channel_start += channel_count
    return tuple(descriptors), warnings


def parse_sample_file(file_path: Path, kit_root: Path) -> SampleFile:
    relative_path = file_path.relative_to(kit_root)
    if len(relative_path.parts) != 3:
        raise ValueError("Expected instrument/articulation/file layout")
    name_tokens = file_path.stem.split("-")
    if len(name_tokens) < 5:
        raise ValueError("Filename needs instrument, size, articulation, velocity+RR, and mic chain fields")

    velocity_round_robin = name_tokens[3]
    split_idx = 0
    while split_idx < len(velocity_round_robin) and velocity_round_robin[split_idx].isdigit():
        split_idx += 1
    if split_idx == 0:
        raise ValueError("Velocity+RR field must start with digits")
    velocity = int(velocity_round_robin[:split_idx])
    if velocity < 1 or velocity > 127:
        raise ValueError(f"Velocity must be 1-127, got {velocity}")

    round_robin = velocity_round_robin[split_idx:]
    warnings = []
    if len(round_robin) == 1 and not "a" <= round_robin <= "z":
        warnings.append(f"Round-robin should be a-z, got '{round_robin}'")
    elif len(round_robin) > 1:
        warnings.append(f"Round-robin should be one letter, got '{round_robin}'")

    mic_descriptors, mic_warnings = parse_mic_chain(name_tokens[4])
    warnings.extend(mic_warnings)
    if not mic_descriptors:
        raise ValueError("Filename contains no valid mic codes")
    return SampleFile(file_path, relative_path.as_posix(), relative_path.parts[0], relative_path.parts[1], velocity, round_robin, mic_descriptors, tuple(warnings))


def power_to_db(power: float) -> float:
    return 10.0 * math.log10(max(power, 1.0e-24))


def amplitude_to_db(amplitude: float) -> float:
    return 20.0 * math.log10(max(amplitude, 1.0e-12))


def calculate_content_hash(file_path: Path) -> str:
    """Hash raw file bytes so the value remains stable when only the path changes."""
    digest = hashlib.blake2b(digest_size=16)
    with file_path.open("rb") as input_file:
        while chunk := input_file.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def detect_onset_sample(audio_waveform: npt.NDArray[np.float64], sample_rate: int, settings: AnalysisSettings) -> int:
    """Find the first short frame that is both audible and close enough to the sample peak."""
    frame_samples = max(1, int(round(sample_rate * settings.onset_frame_ms / 1000.0)))
    frame_powers = []
    for frame_start in range(0, audio_waveform.shape[0], frame_samples):
        frame = audio_waveform[frame_start:frame_start + frame_samples]
        if frame.size > 0:
            frame_powers.append(float(np.mean(np.square(frame))))
    if not frame_powers:
        return 0
    frame_db = np.asarray([power_to_db(frame_power) for frame_power in frame_powers], dtype=np.float64)
    threshold_db = max(float(np.max(frame_db)) + settings.onset_relative_threshold_db, settings.onset_absolute_threshold_db)
    matching_frames = np.flatnonzero(frame_db >= threshold_db)
    return int(matching_frames[0]) * frame_samples if matching_frames.size > 0 else 0


def apply_k_weighting(audio_waveform: npt.NDArray[np.float64], sample_rate: int) -> npt.NDArray[np.float64]:
    """Apply the same signed-channel BS.1770 K weighting used by the Phase 1 comparison."""
    weighted_audio = audio_waveform.copy()
    meter = pyln.Meter(sample_rate)
    for filter_stage in meter._filters.values():
        for channel_idx in range(weighted_audio.shape[1]):
            weighted_audio[:, channel_idx] = filter_stage.apply_filter(weighted_audio[:, channel_idx])
    return weighted_audio


def fixed_window(audio_waveform: npt.NDArray[np.float64], onset_sample: int, sample_rate: int, window_ms: int) -> npt.NDArray[np.float64]:
    window_samples = max(1, int(round(sample_rate * window_ms / 1000.0)))
    return audio_waveform[onset_sample:min(audio_waveform.shape[0], onset_sample + window_samples)]


def k_weighted_db(audio_waveform: npt.NDArray[np.float64]) -> float:
    """Return summed signed-channel K-weighted energy in the Phase 1 dB scale."""
    channel_powers = np.mean(np.square(audio_waveform), axis=0)
    return -0.691 + power_to_db(float(np.sum(channel_powers)))


def true_peak_db(audio_waveform: npt.NDArray[np.float64], oversample_factor: int) -> float:
    """Estimate inter-sample peak after oversampling every decoded mic channel."""
    oversampled_audio = audio_waveform if oversample_factor == 1 else resample_poly(audio_waveform, oversample_factor, 1, axis=0)
    return amplitude_to_db(float(np.max(np.abs(oversampled_audio))))


def decode_mic_audio(audio_waveform: npt.NDArray[np.float64], mic_descriptor: MicDescriptor) -> npt.NDArray[np.float64]:
    channel_end = mic_descriptor.channel_start + mic_descriptor.channel_count
    if channel_end > audio_waveform.shape[1]:
        raise ValueError(f"Mic {mic_descriptor.code} needs channels {mic_descriptor.channel_start}:{channel_end}, but the file has {audio_waveform.shape[1]}")
    mic_audio = audio_waveform[:, mic_descriptor.channel_start:channel_end]
    if mic_descriptor.channel_format != "i":
        return mic_audio
    normalization = math.sqrt(0.5)
    mid_channel = mic_audio[:, 0]
    side_channel = mic_audio[:, 1]
    return np.column_stack(((mid_channel + side_channel) * normalization, (mid_channel - side_channel) * normalization))


def stereo_balance(audio_waveform: npt.NDArray[np.float64]) -> Optional[float]:
    """Map stereo energy balance to the engine pan range from -127 to 127."""
    if audio_waveform.shape[1] != 2:
        return None
    left_power = float(np.mean(np.square(audio_waveform[:, 0])))
    right_power = float(np.mean(np.square(audio_waveform[:, 1])))
    total_power = left_power + right_power
    if total_power <= 1.0e-24:
        return None
    return 127.0 * (right_power - left_power) / total_power


def analyze_decay(audio_waveform: npt.NDArray[np.float64], sample_rate: int, settings: AnalysisSettings) -> dict[str, object]:
    """Measure RMS peak and absolute file times where the envelope crosses each 6 dB step."""
    frame_samples = max(1, int(round(sample_rate * settings.decay_window_ms / 1000.0)))
    frame_powers = []
    for frame_start in range(0, audio_waveform.shape[0], frame_samples):
        frame = audio_waveform[frame_start:frame_start + frame_samples]
        if frame.size > 0:
            frame_powers.append(float(np.mean(np.square(frame))))
    if not frame_powers:
        raise ValueError("Audio file contains no decay frames")

    envelope_db = np.asarray([power_to_db(frame_power) for frame_power in frame_powers], dtype=np.float64)
    peak_frame_idx = int(np.argmax(envelope_db))
    peak_db = float(envelope_db[peak_frame_idx])
    frame_duration_ms = frame_samples * 1000.0 / sample_rate
    decay_points: dict[int, Optional[float]] = {decay_level_db: None for decay_level_db in DECAY_LEVELS_DB}
    search_frame_idx = peak_frame_idx
    for decay_level_db in DECAY_LEVELS_DB:
        matching_frames = np.flatnonzero(envelope_db[search_frame_idx:] <= peak_db - decay_level_db)
        if matching_frames.size == 0:
            break
        search_frame_idx += int(matching_frames[0])
        decay_points[decay_level_db] = round(search_frame_idx * frame_duration_ms, 4)

    hold_frames = max(1, int(math.ceil(settings.decay_silence_hold_ms / frame_duration_ms)))
    silence_frame_idx = None
    for frame_idx in range(peak_frame_idx, max(peak_frame_idx, len(envelope_db) - hold_frames) + 1):
        if frame_idx + hold_frames <= len(envelope_db) and np.all(envelope_db[frame_idx:frame_idx + hold_frames] <= settings.decay_silence_threshold_db):
            silence_frame_idx = frame_idx
            break
    decay_end_ms = audio_waveform.shape[0] * 1000.0 / sample_rate if silence_frame_idx is None else silence_frame_idx * frame_duration_ms
    return {
        "peak_db": round(peak_db, 4),
        "peak_ms": round(peak_frame_idx * frame_duration_ms, 4),
        "decay_end_ms": round(decay_end_ms, 4),
        "decay_points": decay_points,
    }


def measure_sample_file(sample_file: SampleFile, settings: AnalysisSettings) -> list[dict[str, object]]:
    """Decode one source file once and return one independent metadata row for each mic entry."""
    audio_waveform, sample_rate = sf.read(sample_file.path, dtype="float64", always_2d=True)
    if audio_waveform.shape[0] == 0 or audio_waveform.shape[1] == 0:
        raise ValueError("Audio file contains no samples or channels")
    if not np.all(np.isfinite(audio_waveform)):
        raise ValueError("Audio file contains non-finite sample values")

    expected_channels = sum(mic_descriptor.channel_count for mic_descriptor in sample_file.mic_descriptors)
    if expected_channels != audio_waveform.shape[1]:
        raise ValueError(f"Mic chain describes {expected_channels} channels, but the file has {audio_waveform.shape[1]}")

    content_hash = calculate_content_hash(sample_file.path)
    file_size_bytes = sample_file.path.stat().st_size
    rows = []
    for mic_descriptor in sample_file.mic_descriptors:
        mic_audio = decode_mic_audio(audio_waveform, mic_descriptor)
        onset_sample = detect_onset_sample(mic_audio, sample_rate, settings)
        weighted_audio = apply_k_weighting(mic_audio, sample_rate)
        intensity_window = fixed_window(weighted_audio, onset_sample, sample_rate, settings.intensity_window_ms)
        balance_window = fixed_window(mic_audio, onset_sample, sample_rate, settings.balance_window_ms)
        balance = stereo_balance(balance_window)
        decay = analyze_decay(mic_audio, sample_rate, settings)
        row = {
            "relative_path": sample_file.relative_path,
            "mic_code": mic_descriptor.code,
            "content_hash": content_hash,
            "file_size_bytes": file_size_bytes,
            "onset_ms": round(onset_sample * 1000.0 / sample_rate, 4),
            "intensity_db": round(k_weighted_db(intensity_window), 4),
            "true_peak_db": round(true_peak_db(mic_audio, settings.true_peak_oversample_factor), 4),
            "peak_db": decay["peak_db"],
            "peak_ms": decay["peak_ms"],
            "decay_end_ms": decay["decay_end_ms"],
            "stereo_balance": None if balance is None else round(balance, 4),
            "_instrument_folder": sample_file.instrument_folder,
            "_articulation_folder": sample_file.articulation_folder,
            "_velocity": sample_file.velocity,
            "_round_robin": sample_file.round_robin,
        }
        for decay_level_db in DECAY_LEVELS_DB:
            row[f"-{decay_level_db}db_ms"] = decay["decay_points"][decay_level_db]
        rows.append(row)
    return rows


def read_previous_file_identities(metadata_path: Path) -> dict[str, tuple[str, int]]:
    if not metadata_path.is_file():
        return {}
    identities = {}
    try:
        with metadata_path.open("r", newline="", encoding="utf-8-sig") as input_file:
            for row in csv.DictReader(input_file):
                relative_path = str(row.get("relative_path", ""))
                content_hash = str(row.get("content_hash", ""))
                file_size_text = str(row.get("file_size_bytes", ""))
                if relative_path and content_hash and file_size_text.isdigit():
                    identities[relative_path] = (content_hash, int(file_size_text))
    except (OSError, csv.Error):
        return {}
    return identities


def summarize_identity_changes(previous: dict[str, tuple[str, int]], rows: list[dict[str, object]]) -> list[str]:
    current = {str(row["relative_path"]): (str(row["content_hash"]), int(row["file_size_bytes"])) for row in rows}
    messages = []
    for relative_path in sorted(previous.keys() & current.keys()):
        if previous[relative_path] != current[relative_path]:
            messages.append(f"Modified: {relative_path}")

    removed_paths = sorted(previous.keys() - current.keys())
    added_paths = sorted(current.keys() - previous.keys())
    removed_by_hash: dict[tuple[str, int], list[str]] = {}
    added_by_hash: dict[tuple[str, int], list[str]] = {}
    for relative_path in removed_paths:
        removed_by_hash.setdefault(previous[relative_path], []).append(relative_path)
    for relative_path in added_paths:
        added_by_hash.setdefault(current[relative_path], []).append(relative_path)

    matched_removed = set()
    matched_added = set()
    for identity in sorted(removed_by_hash.keys() & added_by_hash.keys()):
        old_paths = removed_by_hash[identity]
        new_paths = added_by_hash[identity]
        if len(old_paths) == 1 and len(new_paths) == 1:
            messages.append(f"Renamed or moved: {old_paths[0]} -> {new_paths[0]}")
            matched_removed.add(old_paths[0])
            matched_added.add(new_paths[0])
    messages.extend(f"Removed: {relative_path}" for relative_path in removed_paths if relative_path not in matched_removed)
    messages.extend(f"Added: {relative_path}" for relative_path in added_paths if relative_path not in matched_added)
    return messages


def format_csv_value(value: object) -> object:
    if isinstance(value, float) and math.isfinite(value) and value.is_integer():
        return int(value)
    return value


def write_metadata_csv(kit_root: Path, rows: list[dict[str, object]]) -> None:
    metadata_path = kit_root / "metadata.csv"
    backup_path = kit_root / "metadata.csv.bak"
    temporary_path = kit_root / ".metadata.csv.tmp"
    backup_temporary_path = kit_root / ".metadata.csv.bak.tmp"
    try:
        with temporary_path.open("w", newline="", encoding="utf-8") as output_file:
            writer = csv.DictWriter(output_file, fieldnames=CSV_COLUMNS)
            writer.writeheader()
            writer.writerows({column_name: format_csv_value(row.get(column_name, "")) for column_name in CSV_COLUMNS} for row in rows)
            output_file.flush()
            os.fsync(output_file.fileno())
        if metadata_path.is_file():
            shutil.copy2(metadata_path, backup_temporary_path)
            os.replace(backup_temporary_path, backup_path)
        os.replace(temporary_path, metadata_path)
    finally:
        temporary_path.unlink(missing_ok=True)
        backup_temporary_path.unlink(missing_ok=True)


def generate_kit_metadata(kit_root: Path, settings: AnalysisSettings, report_folder: Optional[Path]) -> tuple[int, list[str], list[Path]]:
    audio_files = find_audio_files(kit_root)
    parsed_files = []
    errors = []
    for file_path in audio_files:
        try:
            parsed_file = parse_sample_file(file_path, kit_root)
            parsed_files.append(parsed_file)
            for warning in parsed_file.parser_warnings:
                print(f"Warning: {parsed_file.relative_path}: {warning}")
        except ValueError as error:
            errors.append(f"{file_path.relative_to(kit_root).as_posix()}: {error}")

    rows = []
    for file_idx, sample_file in enumerate(parsed_files, start=1):
        print(f"  Measuring {file_idx}/{len(parsed_files)}: {sample_file.relative_path}")
        try:
            rows.extend(measure_sample_file(sample_file, settings))
        except Exception as error:
            errors.append(f"{sample_file.relative_path}: {error}")

    if errors:
        return 0, errors, []
    rows.sort(key=lambda row: (str(row["relative_path"]), str(row["mic_code"])))
    if not rows:
        return 0, ["No metadata rows were generated"], []

    previous_identities = read_previous_file_identities(kit_root / "metadata.csv")
    change_messages = summarize_identity_changes(previous_identities, rows)
    report_paths = write_phase6_reports(report_folder, kit_root, rows, settings.onset_frame_ms) if report_folder is not None else []
    write_metadata_csv(kit_root, rows)
    for message in change_messages:
        print(f"  {message}")
    return len(rows), [], report_paths


def main() -> None:
    arguments = parse_arguments()
    settings = AnalysisSettings(
        intensity_window_ms=arguments.intensity_window_ms,
        balance_window_ms=arguments.balance_window_ms,
        onset_frame_ms=arguments.onset_frame_ms,
        onset_relative_threshold_db=arguments.onset_relative_threshold_db,
        onset_absolute_threshold_db=arguments.onset_absolute_threshold_db,
        decay_window_ms=arguments.decay_window_ms,
        decay_silence_threshold_db=arguments.decay_silence_threshold_db,
        decay_silence_hold_ms=arguments.decay_silence_hold_ms,
        true_peak_oversample_factor=arguments.true_peak_oversample_factor,
    )
    try:
        validate_settings(settings)
        input_path = Path(arguments.input).expanduser().resolve()
        if not input_path.is_dir():
            raise ValueError(f"Input folder does not exist: {input_path}")
        kit_roots = discover_kit_roots(input_path)
        report_folder = Path(arguments.report_folder).expanduser().resolve() if arguments.report_folder else None
    except ValueError as error:
        raise SystemExit(str(error)) from error

    start_time = time.perf_counter()
    failed_kit_count = 0
    total_row_count = 0
    for kit_idx, kit_root in enumerate(kit_roots, start=1):
        print(f"Kit {kit_idx}/{len(kit_roots)}: {kit_root.name}")
        row_count, errors, report_paths = generate_kit_metadata(kit_root, settings, report_folder)
        if errors:
            failed_kit_count += 1
            print(f"  Failed; existing metadata files were kept unchanged:")
            for error in errors:
                print(f"    {error}")
            continue
        total_row_count += row_count
        print(f"  Wrote {row_count} rows to {kit_root / 'metadata.csv'}")
        for report_path in report_paths:
            print(f"  Wrote report {report_path}")

    elapsed_seconds = time.perf_counter() - start_time
    print(f"Finished {len(kit_roots) - failed_kit_count}/{len(kit_roots)} kits, {total_row_count} rows, {elapsed_seconds:.2f} seconds")
    if failed_kit_count:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
