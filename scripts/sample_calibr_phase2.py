import argparse
import csv
import json
import math
import re
import statistics
import time
from collections import defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Optional

import numpy as np
import numpy.typing as npt
import pyloudnorm as pyln
import soundfile as sf
from scipy.signal import resample_poly


ANALYSIS_VERSION = "todo14-phase2-v1"
DEFAULT_METRIC_VERSION = "todo14-k-weighted-400ms-v1"
SUPPORTED_AUDIO_EXTENSIONS = (".wav", ".bwf", ".flac", ".aif", ".aiff", ".ogg", ".m4a", ".mp3", ".wmv", ".asf", ".wm", ".wma")
BASE62_DIGIT_MAP = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
DECAY_LEVELS_DB = (6, 12, 18, 24, 30, 36, 42, 48, 54, 60)


@dataclass(frozen=True)
class AnalysisSettings:
    metric_version: str
    intensity_window_ms: int
    balance_window_ms: int
    onset_frame_ms: int
    onset_relative_threshold_db: float
    onset_absolute_threshold_db: float
    decay_window_ms: int
    decay_silence_threshold_db: float
    decay_silence_hold_ms: int
    true_peak_oversample_factor: int
    round_robin_outlier_db: float


@dataclass(frozen=True)
class MicDescriptor:
    code: str
    mic_type: str
    position: str
    channel_format: str
    channel_start: int
    channel_count: int


@dataclass(frozen=True)
class SampleFile:
    path: Path
    relative_path: str
    kit: str
    instrument_folder: str
    articulation_folder: str
    instrument_type: str
    instrument_size: str
    articulation: str
    velocity: int
    round_robin: str
    mic_chain: str
    mic_descriptors: tuple[MicDescriptor, ...]
    base_name_tokens: tuple[str, ...]
    optional_metadata_tokens: tuple[str, ...]
    parser_warnings: tuple[str, ...]


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Analyze drum samples without changing them unless --rename-files is explicitly supplied.")
    parser.add_argument("-i", "--input-folder", required=True, help="Sample-library root containing kit folders")
    parser.add_argument("-o", "--output-json", required=True, help="Complete JSON report path")
    parser.add_argument("--output-csv", help="Optional flat per-file CSV report path")
    parser.add_argument("--name-filter", help="Optional case-insensitive filename regular expression")
    parser.add_argument("--metric-version", default=DEFAULT_METRIC_VERSION, help="Version written into the report settings")
    parser.add_argument("--intensity-window-ms", type=int, default=400, help="K-weighted logical-hit intensity window")
    parser.add_argument("--balance-window-ms", type=int, default=200, help="Stereo source-balance window")
    parser.add_argument("--onset-frame-ms", type=int, default=5, help="Non-overlapping onset frame length")
    parser.add_argument("--onset-relative-threshold-db", type=float, default=-30.0, help="Onset threshold relative to the strongest frame")
    parser.add_argument("--onset-absolute-threshold-db", type=float, default=-60.0, help="Minimum absolute onset threshold")
    parser.add_argument("--decay-window-ms", type=int, default=10, help="RMS decay frame length")
    parser.add_argument("--decay-silence-threshold-db", type=float, default=-72.0, help="Absolute decay end threshold")
    parser.add_argument("--decay-silence-hold-ms", type=int, default=30, help="Time that must remain below the decay threshold")
    parser.add_argument("--true-peak-oversample-factor", type=int, default=4, help="True-peak oversampling factor")
    parser.add_argument("--round-robin-outlier-db", type=float, default=3.0, help="Distance from the raw velocity median that creates an outlier warning")
    parser.add_argument("--rename-files", action="store_true", help="Apply the fully validated p/d/c metadata rename plan")
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
    if settings.round_robin_outlier_db < 0.0:
        raise ValueError("Round-robin outlier threshold must not be negative")


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
        if mic_type not in valid_positions:
            warnings.append(f"Skipping invalid mic code '{mic_code}': type must be d, o, or r")
            continue
        if position not in valid_positions[mic_type]:
            warnings.append(f"Skipping invalid mic code '{mic_code}': position is not valid for type {mic_type}")
            continue
        if channel_format not in "msi":
            warnings.append(f"Skipping invalid mic code '{mic_code}': format must be m, s, or i")
            continue
        channel_count = 1 if channel_format == "m" else 2
        descriptors.append(MicDescriptor(mic_code, mic_type, position, channel_format, channel_start, channel_count))
        channel_start += channel_count
    return tuple(descriptors), warnings


def parse_sample_file(file_path: Path, input_folder: Path) -> SampleFile:
    relative_path = file_path.relative_to(input_folder)
    if len(relative_path.parts) < 4:
        raise ValueError("Expected kit/instrument/articulation/file layout")
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
    parser_warnings = []
    if len(round_robin) == 1 and not "a" <= round_robin <= "z":
        parser_warnings.append(f"Round-robin should be a-z, got '{round_robin}'")
    elif len(round_robin) > 1:
        parser_warnings.append(f"Round-robin should be one letter, got '{round_robin}'")

    mic_descriptors, mic_warnings = parse_mic_chain(name_tokens[4])
    parser_warnings.extend(mic_warnings)
    return SampleFile(
        path=file_path,
        relative_path=relative_path.as_posix(),
        kit=relative_path.parts[0],
        instrument_folder=relative_path.parts[-3],
        articulation_folder=relative_path.parts[-2],
        instrument_type=name_tokens[0],
        instrument_size=name_tokens[1],
        articulation=name_tokens[2],
        velocity=velocity,
        round_robin=round_robin,
        mic_chain=name_tokens[4],
        mic_descriptors=mic_descriptors,
        base_name_tokens=tuple(name_tokens[:5]),
        optional_metadata_tokens=tuple(token for token in name_tokens[5:] if token),
        parser_warnings=tuple(parser_warnings),
    )


def scan_sample_files(input_folder: Path, name_filter: Optional[re.Pattern[str]]) -> tuple[list[SampleFile], list[dict[str, str]], int]:
    sample_files = []
    parse_errors = []
    discovered_audio_count = 0
    for file_path in sorted(input_folder.rglob("*")):
        if not file_path.is_file() or file_path.suffix.lower() not in SUPPORTED_AUDIO_EXTENSIONS:
            continue
        if name_filter is not None and name_filter.search(file_path.name) is None:
            continue
        discovered_audio_count += 1
        try:
            sample_files.append(parse_sample_file(file_path, input_folder))
        except ValueError as error:
            parse_errors.append({"path": file_path.relative_to(input_folder).as_posix(), "error": str(error)})
    return sample_files, parse_errors, discovered_audio_count


def power_to_db(power: float) -> float:
    return 10.0 * math.log10(max(power, 1.0e-24))


def amplitude_to_db(amplitude: float) -> float:
    return 20.0 * math.log10(max(amplitude, 1.0e-12))


def detect_onset_sample(audio_waveform: npt.NDArray[np.float64], sample_rate: int, settings: AnalysisSettings) -> int:
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
    weighted_audio = audio_waveform.copy()
    meter = pyln.Meter(sample_rate)
    for filter_stage in meter._filters.values():
        for channel_idx in range(weighted_audio.shape[1]):
            weighted_audio[:, channel_idx] = filter_stage.apply_filter(weighted_audio[:, channel_idx])
    return weighted_audio


def fixed_window(audio_waveform: npt.NDArray[np.float64], onset_sample: int, sample_rate: int, window_ms: int) -> npt.NDArray[np.float64]:
    window_samples = max(1, int(round(sample_rate * window_ms / 1000.0)))
    return audio_waveform[onset_sample:min(audio_waveform.shape[0], onset_sample + window_samples)]


def rms_db(audio_waveform: npt.NDArray[np.float64]) -> float:
    return power_to_db(float(np.mean(np.square(audio_waveform))))


def k_weighted_db(audio_waveform: npt.NDArray[np.float64]) -> float:
    channel_powers = np.mean(np.square(audio_waveform), axis=0)
    return -0.691 + power_to_db(float(np.sum(channel_powers)))


def true_peak_db(audio_waveform: npt.NDArray[np.float64], oversample_factor: int) -> float:
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


def stereo_balance(audio_waveform: npt.NDArray[np.float64]) -> tuple[Optional[float], Optional[float]]:
    if audio_waveform.shape[1] != 2:
        return None, None
    left_power = float(np.mean(np.square(audio_waveform[:, 0])))
    right_power = float(np.mean(np.square(audio_waveform[:, 1])))
    total_power = left_power + right_power
    if total_power <= 1.0e-24:
        return None, None
    balance_db = power_to_db(right_power) - power_to_db(left_power)
    balance_position = 127.0 * (right_power - left_power) / total_power
    return balance_db, balance_position


def number_to_base62(number: int) -> str:
    result = ""
    remaining_number = max(0, number)
    while remaining_number > 0:
        result = BASE62_DIGIT_MAP[remaining_number % 62] + result
        remaining_number //= 62
    return result or "0"


def encode_decay_points(decay_points_ms: list[float]) -> str:
    rounded_points = [max(0, int(round(point_ms))) for point_ms in decay_points_ms]
    if not rounded_points:
        return ""
    deltas = [rounded_points[0]]
    for point_idx in range(1, len(rounded_points)):
        deltas.append(rounded_points[point_idx] - rounded_points[point_idx - 1])
    return "_".join(number_to_base62(delta) for delta in deltas)


def analyze_decay(audio_waveform: npt.NDArray[np.float64], sample_rate: int, settings: AnalysisSettings) -> dict[str, object]:
    frame_samples = max(1, int(round(sample_rate * settings.decay_window_ms / 1000.0)))
    frame_powers = []
    for frame_start in range(0, audio_waveform.shape[0], frame_samples):
        frame = audio_waveform[frame_start:frame_start + frame_samples]
        if frame.size > 0:
            frame_powers.append(float(np.mean(np.square(frame))))
    envelope_db = np.asarray([power_to_db(frame_power) for frame_power in frame_powers], dtype=np.float64)
    peak_frame_idx = int(np.argmax(envelope_db))
    peak_db = float(envelope_db[peak_frame_idx])
    frame_duration_ms = frame_samples * 1000.0 / sample_rate
    peak_ms = peak_frame_idx * frame_duration_ms

    decay_points_ms = []
    search_frame_idx = peak_frame_idx
    for decay_level_db in DECAY_LEVELS_DB:
        matching_frames = np.flatnonzero(envelope_db[search_frame_idx:] <= peak_db - decay_level_db)
        if matching_frames.size == 0:
            break
        search_frame_idx += int(matching_frames[0])
        decay_points_ms.append(search_frame_idx * frame_duration_ms)

    hold_frames = max(1, int(math.ceil(settings.decay_silence_hold_ms / frame_duration_ms)))
    silence_frame_idx = None
    last_possible_start = len(envelope_db) - hold_frames
    if last_possible_start >= peak_frame_idx:
        for frame_idx in range(peak_frame_idx, last_possible_start + 1):
            if np.all(envelope_db[frame_idx:frame_idx + hold_frames] <= settings.decay_silence_threshold_db):
                silence_frame_idx = frame_idx
                break
    reached_silence = silence_frame_idx is not None
    decay_end_ms = silence_frame_idx * frame_duration_ms if silence_frame_idx is not None else audio_waveform.shape[0] * 1000.0 / sample_rate
    return {
        "window_ms": settings.decay_window_ms,
        "peak_rms_db": round(peak_db, 4),
        "peak_ms": round(peak_ms, 4),
        "decay_end_ms": round(decay_end_ms, 4),
        "reached_silence": reached_silence,
        "decay_points_ms": [round(point_ms, 4) for point_ms in decay_points_ms],
        "decay_code": encode_decay_points(decay_points_ms),
    }


def base_file_record(sample_file: SampleFile) -> dict[str, object]:
    return {
        "path": sample_file.relative_path,
        "kit": sample_file.kit,
        "instrument_folder": sample_file.instrument_folder,
        "articulation_folder": sample_file.articulation_folder,
        "instrument_type": sample_file.instrument_type,
        "instrument_size": sample_file.instrument_size,
        "articulation": sample_file.articulation,
        "velocity": sample_file.velocity,
        "round_robin": sample_file.round_robin,
        "mic_chain": sample_file.mic_chain,
        "base_name_tokens": list(sample_file.base_name_tokens),
        "optional_metadata_tokens": list(sample_file.optional_metadata_tokens),
        "warnings": list(sample_file.parser_warnings),
    }


def measure_sample_file(sample_file: SampleFile, settings: AnalysisSettings) -> dict[str, object]:
    record = base_file_record(sample_file)
    audio_waveform, sample_rate = sf.read(sample_file.path, dtype="float64", always_2d=True)
    if audio_waveform.shape[0] == 0 or audio_waveform.shape[1] == 0:
        raise ValueError("Audio file contains no samples or channels")
    if not np.all(np.isfinite(audio_waveform)):
        raise ValueError("Audio file contains non-finite sample values")

    expected_channels = sum(mic_descriptor.channel_count for mic_descriptor in sample_file.mic_descriptors)
    if expected_channels != audio_waveform.shape[1]:
        record["warnings"].append(f"Mic chain describes {expected_channels} channels, but the file has {audio_waveform.shape[1]}")

    file_onset_sample = detect_onset_sample(audio_waveform, sample_rate, settings)
    file_decay = analyze_decay(audio_waveform, sample_rate, settings)
    mic_entries = []
    for mic_descriptor in sample_file.mic_descriptors:
        try:
            mic_audio = decode_mic_audio(audio_waveform, mic_descriptor)
        except ValueError as error:
            record["warnings"].append(str(error))
            continue
        mic_onset_sample = detect_onset_sample(mic_audio, sample_rate, settings)
        weighted_audio = apply_k_weighting(mic_audio, sample_rate)
        raw_intensity_window = fixed_window(mic_audio, mic_onset_sample, sample_rate, settings.intensity_window_ms)
        weighted_intensity_window = fixed_window(weighted_audio, mic_onset_sample, sample_rate, settings.intensity_window_ms)
        balance_window = fixed_window(mic_audio, mic_onset_sample, sample_rate, settings.balance_window_ms)
        balance_db, balance_position = stereo_balance(balance_window)
        mic_entries.append({
            "code": mic_descriptor.code,
            "type": mic_descriptor.mic_type,
            "position": mic_descriptor.position,
            "format": mic_descriptor.channel_format,
            "channel_start": mic_descriptor.channel_start,
            "channel_count": mic_descriptor.channel_count,
            "onset_ms": round(mic_onset_sample * 1000.0 / sample_rate, 4),
            "rms_intensity_dbfs": round(rms_db(raw_intensity_window), 4),
            "k_weighted_intensity_db": round(k_weighted_db(weighted_intensity_window), 4),
            "balance_db": None if balance_db is None else round(balance_db, 4),
            "balance_position": None if balance_position is None else round(balance_position, 4),
        })

    record.update({
        "status": "complete",
        "sample_rate": sample_rate,
        "channel_count": audio_waveform.shape[1],
        "frame_count": audio_waveform.shape[0],
        "length_ms": round(audio_waveform.shape[0] * 1000.0 / sample_rate, 4),
        "file_onset_ms": round(file_onset_sample * 1000.0 / sample_rate, 4),
        "file_true_peak_db": round(true_peak_db(audio_waveform, settings.true_peak_oversample_factor), 4),
        "decay": file_decay,
        "mic_entries": mic_entries,
    })
    return record


def logical_hit_key(record: dict[str, object]) -> tuple[str, str, str, int, str]:
    return str(record["kit"]), str(record["instrument_folder"]), str(record["articulation_folder"]), int(record["velocity"]), str(record["round_robin"])


def articulation_key(record: dict[str, object]) -> tuple[str, str, str]:
    return str(record["kit"]), str(record["instrument_folder"]), str(record["articulation_folder"])


def reference_mic_priority(mic_entry: dict[str, object]) -> Optional[int]:
    if mic_entry["code"] == "ons":
        return 0
    if mic_entry["type"] == "o" and mic_entry["position"] == "n" and mic_entry["format"] == "s":
        return 1
    if mic_entry["type"] == "o" and mic_entry["format"] == "s":
        return 2
    if mic_entry["type"] == "o" and mic_entry["format"] == "i":
        return 3
    return None


def choose_reference_mic(records: list[dict[str, object]]) -> Optional[tuple[dict[str, object], dict[str, object]]]:
    candidates = []
    for record in records:
        for mic_entry in record["mic_entries"]:
            priority = reference_mic_priority(mic_entry)
            if priority is not None:
                candidates.append((priority, str(record["path"]), int(mic_entry["channel_start"]), record, mic_entry))
    if not candidates:
        return None
    selected_candidate = min(candidates, key=lambda candidate: (candidate[0], candidate[1], candidate[2]))
    return selected_candidate[3], selected_candidate[4]


def group_logical_hits(file_records: list[dict[str, object]], settings: AnalysisSettings) -> tuple[list[dict[str, object]], list[str]]:
    grouped_records = defaultdict(list)
    for record in file_records:
        if record.get("status") == "complete":
            grouped_records[logical_hit_key(record)].append(record)

    logical_hits = []
    grouping_warnings = []
    for hit_key in sorted(grouped_records):
        linked_records = sorted(grouped_records[hit_key], key=lambda record: str(record["path"]))
        reference_selection = choose_reference_mic(linked_records)
        hit_record = {
            "kit": hit_key[0],
            "instrument_folder": hit_key[1],
            "articulation_folder": hit_key[2],
            "velocity": hit_key[3],
            "round_robin": hit_key[4],
            "linked_files": [record["path"] for record in linked_records],
            "linked_max_true_peak_db": max(float(record["file_true_peak_db"]) for record in linked_records),
            "reference_file": None,
            "reference_mic": None,
            "hit_intensity_db": None,
            "balance_db": None,
            "balance_position": None,
            "timing_evidence": [],
            "raw_velocity_median_db": None,
            "round_robin_delta_db": None,
            "round_robin_outlier": False,
        }
        if reference_selection is None:
            grouping_warnings.append(f"No stereo overhead reference found for {' / '.join(str(value) for value in hit_key)}")
            logical_hits.append(hit_record)
            continue

        reference_record, reference_mic = reference_selection
        reference_onset_ms = float(reference_mic["onset_ms"])
        hit_record["reference_file"] = reference_record["path"]
        hit_record["reference_mic"] = reference_mic["code"]
        hit_record["hit_intensity_db"] = reference_mic["k_weighted_intensity_db"]
        hit_record["balance_db"] = reference_mic["balance_db"]
        hit_record["balance_position"] = reference_mic["balance_position"]
        for linked_record in linked_records:
            for mic_entry in linked_record["mic_entries"]:
                hit_record["timing_evidence"].append({
                    "path": linked_record["path"],
                    "mic": mic_entry["code"],
                    "onset_ms": mic_entry["onset_ms"],
                    "offset_from_reference_ms": round(float(mic_entry["onset_ms"]) - reference_onset_ms, 4),
                })
        logical_hits.append(hit_record)

    velocity_hits = defaultdict(list)
    for hit_record in logical_hits:
        if hit_record["hit_intensity_db"] is not None:
            group_key = articulation_key(hit_record), int(hit_record["velocity"])
            velocity_hits[group_key].append(hit_record)
    for matching_hits in velocity_hits.values():
        raw_velocity_median_db = float(statistics.median(float(hit_record["hit_intensity_db"]) for hit_record in matching_hits))
        for hit_record in matching_hits:
            round_robin_delta_db = float(hit_record["hit_intensity_db"]) - raw_velocity_median_db
            hit_record["raw_velocity_median_db"] = round(raw_velocity_median_db, 4)
            hit_record["round_robin_delta_db"] = round(round_robin_delta_db, 4)
            hit_record["round_robin_outlier"] = abs(round_robin_delta_db) > settings.round_robin_outlier_db
    return logical_hits, grouping_warnings


def proposed_metadata_tokens(record: dict[str, object]) -> tuple[list[str], list[str]]:
    preserved_tokens = [token for token in record["optional_metadata_tokens"] if not str(token).startswith(("p", "d", "c"))]
    warnings = []
    decay = record["decay"]
    peak_rms_db = float(decay["peak_rms_db"])
    if peak_rms_db >= 0.0:
        warnings.append(f"Existing p metadata cannot represent non-negative RMS peak {peak_rms_db:.4f} dB")
        return list(record["optional_metadata_tokens"]), warnings
    peak_token = f"p{int(round(-peak_rms_db * 10.0))}_{int(round(float(decay['peak_ms'])))}"
    decay_token = f"d{int(round(float(decay['decay_end_ms'])))}"
    proposed_tokens = preserved_tokens + [peak_token, decay_token]
    if decay["decay_code"]:
        proposed_tokens.append(f"c{decay['decay_code']}")
    return proposed_tokens, warnings


def build_rename_plan(input_folder: Path, file_records: list[dict[str, object]], parse_errors: list[dict[str, str]]) -> tuple[list[dict[str, object]], list[str]]:
    rename_plan = []
    for record in file_records:
        if record.get("status") != "complete":
            rename_plan.append({"source": record["path"], "destination": None, "status": "blocked", "warnings": [record.get("error", "Measurement failed")]})
            continue
        proposed_tokens, proposal_warnings = proposed_metadata_tokens(record)
        proposed_stem = "-".join(list(record["base_name_tokens"]) + proposed_tokens)
        source_path = input_folder / str(record["path"])
        destination_path = source_path.with_name(proposed_stem + source_path.suffix)
        destination_relative = destination_path.relative_to(input_folder).as_posix()
        rename_status = "blocked" if proposal_warnings else ("unchanged" if destination_relative == record["path"] else "planned")
        rename_plan.append({
            "source": record["path"],
            "destination": destination_relative,
            "status": rename_status,
            "warnings": proposal_warnings,
        })
    for parse_error in parse_errors:
        rename_plan.append({"source": parse_error["path"], "destination": None, "status": "blocked", "warnings": [parse_error["error"]]})

    validation_errors = []
    destinations = defaultdict(list)
    for plan_entry in rename_plan:
        if plan_entry["destination"] is not None:
            destinations[str(plan_entry["destination"]).casefold()].append(plan_entry)
    for matching_entries in destinations.values():
        if len(matching_entries) > 1:
            destination = matching_entries[0]["destination"]
            validation_errors.append(f"Multiple files would use destination {destination}")
            for plan_entry in matching_entries:
                plan_entry["status"] = "blocked"
                plan_entry["warnings"].append("Destination is not unique")

    for plan_entry in rename_plan:
        if plan_entry["status"] != "planned":
            continue
        source_path = input_folder / str(plan_entry["source"])
        destination_path = input_folder / str(plan_entry["destination"])
        if destination_path.exists() and destination_path != source_path:
            validation_errors.append(f"Destination already exists: {plan_entry['destination']}")
            plan_entry["status"] = "blocked"
            plan_entry["warnings"].append("Destination already exists")
    return rename_plan, sorted(set(validation_errors))


def apply_rename_plan(input_folder: Path, rename_plan: list[dict[str, object]], validation_errors: list[str]) -> int:
    blocked_entries = [plan_entry for plan_entry in rename_plan if plan_entry["status"] == "blocked"]
    if validation_errors or blocked_entries:
        raise ValueError("Rename plan is blocked; no files were changed")
    planned_entries = [plan_entry for plan_entry in rename_plan if plan_entry["status"] == "planned"]
    for plan_entry in planned_entries:
        source_path = input_folder / str(plan_entry["source"])
        destination_path = input_folder / str(plan_entry["destination"])
        if not source_path.is_file():
            raise ValueError(f"Rename source disappeared; no files were changed: {plan_entry['source']}")
        if destination_path.exists() and destination_path != source_path:
            raise ValueError(f"Rename destination appeared; no files were changed: {plan_entry['destination']}")
    applied_count = 0
    for plan_entry in planned_entries:
        source_path = input_folder / str(plan_entry["source"])
        destination_path = input_folder / str(plan_entry["destination"])
        source_path.rename(destination_path)
        plan_entry["status"] = "applied"
        applied_count += 1
    return applied_count


def finite_median(values: list[float]) -> Optional[float]:
    finite_values = [value for value in values if math.isfinite(value)]
    return float(statistics.median(finite_values)) if finite_values else None


def build_summary(
    discovered_audio_count: int,
    parse_errors: list[dict[str, str]],
    file_records: list[dict[str, object]],
    logical_hits: list[dict[str, object]],
    rename_plan: list[dict[str, object]],
) -> dict[str, object]:
    complete_records = [record for record in file_records if record.get("status") == "complete"]
    calibrated_hits = [hit_record for hit_record in logical_hits if hit_record["hit_intensity_db"] is not None]
    hit_intensities = [float(hit_record["hit_intensity_db"]) for hit_record in calibrated_hits]
    return {
        "discovered_audio_file_count": discovered_audio_count,
        "parsed_file_count": len(file_records),
        "parse_error_count": len(parse_errors),
        "measured_file_count": len(complete_records),
        "measurement_error_count": len(file_records) - len(complete_records),
        "logical_hit_count": len(logical_hits),
        "calibrated_logical_hit_count": len(calibrated_hits),
        "uncalibrated_logical_hit_count": len(logical_hits) - len(calibrated_hits),
        "round_robin_outlier_count": sum(1 for hit_record in calibrated_hits if hit_record["round_robin_outlier"]),
        "hit_intensity_median_db": finite_median(hit_intensities),
        "rename_planned_count": sum(1 for plan_entry in rename_plan if plan_entry["status"] == "planned"),
        "rename_applied_count": sum(1 for plan_entry in rename_plan if plan_entry["status"] == "applied"),
        "rename_unchanged_count": sum(1 for plan_entry in rename_plan if plan_entry["status"] == "unchanged"),
        "rename_blocked_count": sum(1 for plan_entry in rename_plan if plan_entry["status"] == "blocked"),
    }


def write_json_report(output_path: Path, report: dict[str, object]) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def write_csv_report(output_path: Path, file_records: list[dict[str, object]], parse_errors: list[dict[str, str]], logical_hits: list[dict[str, object]], rename_plan: list[dict[str, object]]) -> None:
    hit_lookup = {logical_hit_key(hit_record): hit_record for hit_record in logical_hits}
    rename_lookup = {str(plan_entry["source"]): plan_entry for plan_entry in rename_plan}
    field_names = [
        "path", "status", "kit", "instrument", "articulation", "velocity", "round_robin", "mic_chain", "sample_rate", "channels", "length_ms",
        "file_onset_ms", "true_peak_db", "peak_rms_db", "peak_ms", "decay_end_ms", "decay_code", "hit_intensity_db", "raw_velocity_median_db",
        "round_robin_delta_db", "round_robin_outlier", "proposed_path", "warnings", "error",
    ]
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="", encoding="utf-8") as output_file:
        writer = csv.DictWriter(output_file, fieldnames=field_names)
        writer.writeheader()
        for record in file_records:
            hit_record = hit_lookup.get(logical_hit_key(record)) if record.get("status") == "complete" else None
            rename_entry = rename_lookup.get(str(record["path"]))
            decay = record.get("decay", {})
            writer.writerow({
                "path": record["path"],
                "status": record.get("status"),
                "kit": record.get("kit"),
                "instrument": record.get("instrument_folder"),
                "articulation": record.get("articulation_folder"),
                "velocity": record.get("velocity"),
                "round_robin": record.get("round_robin"),
                "mic_chain": record.get("mic_chain"),
                "sample_rate": record.get("sample_rate"),
                "channels": record.get("channel_count"),
                "length_ms": record.get("length_ms"),
                "file_onset_ms": record.get("file_onset_ms"),
                "true_peak_db": record.get("file_true_peak_db"),
                "peak_rms_db": decay.get("peak_rms_db"),
                "peak_ms": decay.get("peak_ms"),
                "decay_end_ms": decay.get("decay_end_ms"),
                "decay_code": decay.get("decay_code"),
                "hit_intensity_db": None if hit_record is None else hit_record["hit_intensity_db"],
                "raw_velocity_median_db": None if hit_record is None else hit_record["raw_velocity_median_db"],
                "round_robin_delta_db": None if hit_record is None else hit_record["round_robin_delta_db"],
                "round_robin_outlier": None if hit_record is None else hit_record["round_robin_outlier"],
                "proposed_path": None if rename_entry is None else rename_entry["destination"],
                "warnings": " | ".join(str(warning) for warning in record.get("warnings", [])),
                "error": record.get("error"),
            })
        for parse_error in parse_errors:
            writer.writerow({"path": parse_error["path"], "status": "parse_error", "error": parse_error["error"]})


def main() -> None:
    arguments = parse_arguments()
    settings = AnalysisSettings(
        metric_version=arguments.metric_version,
        intensity_window_ms=arguments.intensity_window_ms,
        balance_window_ms=arguments.balance_window_ms,
        onset_frame_ms=arguments.onset_frame_ms,
        onset_relative_threshold_db=arguments.onset_relative_threshold_db,
        onset_absolute_threshold_db=arguments.onset_absolute_threshold_db,
        decay_window_ms=arguments.decay_window_ms,
        decay_silence_threshold_db=arguments.decay_silence_threshold_db,
        decay_silence_hold_ms=arguments.decay_silence_hold_ms,
        true_peak_oversample_factor=arguments.true_peak_oversample_factor,
        round_robin_outlier_db=arguments.round_robin_outlier_db,
    )
    try:
        validate_settings(settings)
        name_filter = re.compile(arguments.name_filter, re.IGNORECASE) if arguments.name_filter else None
    except (ValueError, re.error) as error:
        raise SystemExit(str(error)) from error

    input_folder = Path(arguments.input_folder).expanduser().resolve()
    output_json = Path(arguments.output_json).expanduser().resolve()
    output_csv = Path(arguments.output_csv).expanduser().resolve() if arguments.output_csv else None
    if not input_folder.is_dir():
        raise SystemExit(f"Input folder does not exist: {input_folder}")

    start_time = time.perf_counter()
    sample_files, parse_errors, discovered_audio_count = scan_sample_files(input_folder, name_filter)
    print(f"Found {discovered_audio_count} matching audio files; {len(sample_files)} filenames parsed")
    file_records = []
    for file_idx, sample_file in enumerate(sample_files, start=1):
        print(f"Measuring {file_idx}/{len(sample_files)}: {sample_file.relative_path}")
        try:
            file_records.append(measure_sample_file(sample_file, settings))
        except Exception as error:
            failed_record = base_file_record(sample_file)
            failed_record.update({"status": "measurement_error", "error": str(error)})
            file_records.append(failed_record)

    logical_hits, grouping_warnings = group_logical_hits(file_records, settings)
    rename_plan, rename_validation_errors = build_rename_plan(input_folder, file_records, parse_errors)
    report = {
        "analysis_version": ANALYSIS_VERSION,
        "input_folder": str(input_folder),
        "settings": asdict(settings),
        "summary": build_summary(discovered_audio_count, parse_errors, file_records, logical_hits, rename_plan),
        "parse_errors": parse_errors,
        "files": file_records,
        "logical_hits": logical_hits,
        "grouping_warnings": grouping_warnings,
        "rename_plan": rename_plan,
        "rename_validation_errors": rename_validation_errors,
        "rename_apply": {"requested": bool(arguments.rename_files), "applied_count": 0},
        "elapsed_seconds": None,
    }

    if arguments.rename_files:
        try:
            report["rename_apply"]["applied_count"] = apply_rename_plan(input_folder, rename_plan, rename_validation_errors)
        except ValueError as error:
            report["rename_apply"]["error"] = str(error)
            report["summary"] = build_summary(discovered_audio_count, parse_errors, file_records, logical_hits, rename_plan)
            report["elapsed_seconds"] = round(time.perf_counter() - start_time, 3)
            write_json_report(output_json, report)
            if output_csv is not None:
                write_csv_report(output_csv, file_records, parse_errors, logical_hits, rename_plan)
            raise SystemExit(str(error)) from error

    report["summary"] = build_summary(discovered_audio_count, parse_errors, file_records, logical_hits, rename_plan)
    report["elapsed_seconds"] = round(time.perf_counter() - start_time, 3)
    write_json_report(output_json, report)
    if output_csv is not None:
        write_csv_report(output_csv, file_records, parse_errors, logical_hits, rename_plan)
    print(f"Wrote {output_json}")
    if output_csv is not None:
        print(f"Wrote {output_csv}")
    if arguments.rename_files:
        print(f"Renamed {report['rename_apply']['applied_count']} files")
    else:
        print("Report-only mode: no sample files were changed")


if __name__ == "__main__":
    main()
