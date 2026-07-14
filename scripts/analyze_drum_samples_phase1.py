import argparse
import json
import math
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
from scipy.stats import spearmanr


ANALYSIS_VERSION = "todo14-phase1-v1"
SUPPORTED_AUDIO_EXTENSIONS = (".wav", ".bwf", ".flac", ".aif", ".aiff")
REFERENCE_KIT_NAMES = (
    "CrocellKit by Crocell [CC-BY-4.0]",
    "NakedDrums by WilkinsonAudio [CC-BY-4.0]",
    "Salamander by Rytmenpinne [PD]",
    "TchackpoumDG1.3 by Tchackpoum [CC-BY-4.0]",
)
FIXED_WINDOW_MS = (50, 100, 200, 400)
ONSET_FRAME_MS = 5
ONSET_RELATIVE_THRESHOLD_DB = -30.0
ONSET_ABSOLUTE_THRESHOLD_DB = -60.0
SHORT_RMS_WINDOW_MS = 10
SHORT_RMS_SEARCH_MS = 100
TRUE_PEAK_OVERSAMPLE_FACTOR = 4
MAX_VARIANTS_PER_VELOCITY = 3
LISTENING_METRICS = ("k_weighted_50ms_db", "k_weighted_100ms_db", "k_weighted_200ms_db", "rms_100ms_dbfs", "short_rms_peak_10ms_dbfs")
LISTENING_TARGET_DB = -30.0
LISTENING_CLIP_MS = 1000
LISTENING_GAP_MS = 250
LISTENING_SAMPLE_RATE = 48000


@dataclass(frozen=True)
class ReferenceCase:
    name: str
    instrument_types: tuple[str, ...]
    articulation_preferences: tuple[str, ...]


@dataclass(frozen=True)
class MicDescriptor:
    code: str
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
    articulation: str
    velocity: int
    round_robin: str
    mic_descriptors: tuple[MicDescriptor, ...]
    legacy_trim_token: Optional[str]


REFERENCE_CASES = (
    ReferenceCase("kick", ("kick",), ("center",)),
    ReferenceCase("snare", ("snare",), ("center",)),
    ReferenceCase("tom", ("tom",), ("center",)),
    ReferenceCase("hihat_closed", ("hihat",), ("edge[close]", "bow[close]", "edge[tight]", "bow[tight]")),
    ReferenceCase("hihat_open", ("hihat",), ("edge[open]", "bow[open]", "edge[half]", "bow[half]")),
    ReferenceCase("crash", ("crash",), ("edge", "bow")),
    ReferenceCase("ride", ("ride",), ("bow", "edge")),
    ReferenceCase("short_percussion", ("cowbell", "cowbel", "bell", "zilbel"), ("center", "edge", "bow", "bell")),
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create a read-only TODO14 Phase 1 calibration metric report.")
    parser.add_argument("-i", "--input-folder", required=True, help="Sample-library root containing kit folders")
    parser.add_argument("-o", "--output-json", required=True, help="Output JSON report path")
    parser.add_argument("--listening-folder", help="Optional folder for normalized listening sequences")
    return parser.parse_args()


def mic_channel_count(mic_code: str) -> int:
    if len(mic_code) < 3:
        return 0
    if mic_code[2] == "m":
        return 1
    if mic_code[2] in ("s", "i"):
        return 2
    return 0


def parse_mic_descriptors(mic_chain: str) -> tuple[MicDescriptor, ...]:
    descriptors = []
    channel_start = 0
    for mic_code in mic_chain.split("_"):
        channel_count = mic_channel_count(mic_code)
        if channel_count == 0:
            continue
        descriptors.append(MicDescriptor(mic_code, channel_start, channel_count))
        channel_start += channel_count
    return tuple(descriptors)


def parse_sample_file(file_path: Path, input_folder: Path) -> Optional[SampleFile]:
    relative_parts = file_path.relative_to(input_folder).parts
    if len(relative_parts) < 4:
        return None

    name_parts = file_path.stem.split("-")
    if len(name_parts) < 5:
        return None

    velocity_round_robin = name_parts[3]
    split_idx = 0
    while split_idx < len(velocity_round_robin) and velocity_round_robin[split_idx].isdigit():
        split_idx += 1
    if split_idx == 0:
        return None

    velocity = int(velocity_round_robin[:split_idx])
    if velocity < 1 or velocity > 127:
        return None

    legacy_trim_token = next((part for part in name_parts[5:] if part.startswith("v")), None)
    return SampleFile(
        path=file_path,
        relative_path=file_path.relative_to(input_folder).as_posix(),
        kit=relative_parts[0],
        instrument_folder=relative_parts[-3],
        articulation_folder=relative_parts[-2],
        instrument_type=name_parts[0].lower(),
        articulation=name_parts[2].lower(),
        velocity=velocity,
        round_robin=velocity_round_robin[split_idx:],
        mic_descriptors=parse_mic_descriptors(name_parts[4].lower()),
        legacy_trim_token=legacy_trim_token,
    )


def scan_sample_files(input_folder: Path) -> tuple[list[SampleFile], list[str]]:
    parsed_files = []
    warnings = []
    for file_path in sorted(input_folder.rglob("*")):
        if not file_path.is_file() or file_path.suffix.lower() not in SUPPORTED_AUDIO_EXTENSIONS:
            continue
        parsed_file = parse_sample_file(file_path, input_folder)
        if parsed_file is None:
            warnings.append(f"Skipped filename that does not match the engine layout: {file_path.relative_to(input_folder).as_posix()}")
            continue
        parsed_files.append(parsed_file)
    return parsed_files, warnings


def logical_hit_key(sample_file: SampleFile) -> tuple[str, str, str, int, str]:
    return sample_file.kit, sample_file.instrument_folder, sample_file.articulation_folder, sample_file.velocity, sample_file.round_robin


def reference_mic_priority(mic_descriptor: MicDescriptor) -> int:
    mic_code = mic_descriptor.code
    if mic_code == "ons":
        return 0
    if mic_code.startswith("on") and mic_code[2] == "s":
        return 1
    if mic_code.startswith("o") and mic_code[2] == "s":
        return 2
    if mic_code.startswith("o") and mic_code[2] == "i":
        return 3
    return 100


def choose_reference_mic(sample_files: list[SampleFile]) -> Optional[tuple[SampleFile, MicDescriptor]]:
    candidates = []
    for sample_file in sample_files:
        for mic_descriptor in sample_file.mic_descriptors:
            priority = reference_mic_priority(mic_descriptor)
            if priority < 100:
                candidates.append((priority, sample_file.relative_path, mic_descriptor.code, sample_file, mic_descriptor))
    if not candidates:
        return None
    candidates.sort(key=lambda candidate: candidate[:3])
    return candidates[0][3], candidates[0][4]


def matches_articulation(articulation: str, preference: str) -> bool:
    return articulation == preference or articulation.startswith(preference + "[")


def select_reference_hits(groups: dict[tuple[str, str, str, int, str], list[SampleFile]]) -> tuple[list[tuple[str, tuple[str, str, str, int, str]]], list[str]]:
    selections = []
    warnings = []

    for kit_name in REFERENCE_KIT_NAMES:
        for reference_case in REFERENCE_CASES:
            identities = {}
            for group_key, sample_files in groups.items():
                first_file = sample_files[0]
                if first_file.kit != kit_name or first_file.instrument_type not in reference_case.instrument_types:
                    continue
                preference_idx = next((preference_idx for preference_idx, preference in enumerate(reference_case.articulation_preferences) if matches_articulation(first_file.articulation, preference)), None)
                if preference_idx is None or choose_reference_mic(sample_files) is None:
                    continue
                identity = first_file.instrument_folder, first_file.articulation_folder
                identities[identity] = min(preference_idx, identities.get(identity, preference_idx))

            if not identities:
                warnings.append(f"No stereo overhead reference found for {kit_name} / {reference_case.name}")
                continue

            selected_identity = min(identities, key=lambda identity: (identities[identity], identity[0].lower(), identity[1].lower()))
            identity_keys = [group_key for group_key, sample_files in groups.items() if sample_files[0].kit == kit_name and (sample_files[0].instrument_folder, sample_files[0].articulation_folder) == selected_identity]
            velocities = sorted({group_key[3] for group_key in identity_keys})
            selected_velocity_indexes = sorted({0, len(velocities) // 2, len(velocities) - 1})
            selected_velocities = {velocities[velocity_idx] for velocity_idx in selected_velocity_indexes}

            for velocity in sorted(selected_velocities):
                velocity_keys = sorted((group_key for group_key in identity_keys if group_key[3] == velocity), key=lambda group_key: group_key[4])
                for group_key in velocity_keys[:MAX_VARIANTS_PER_VELOCITY]:
                    selections.append((reference_case.name, group_key))

    return selections, warnings


def decode_reference_channels(audio_waveform: npt.NDArray[np.float64], mic_descriptor: MicDescriptor) -> npt.NDArray[np.float64]:
    channel_end = mic_descriptor.channel_start + mic_descriptor.channel_count
    if channel_end > audio_waveform.shape[1]:
        raise ValueError(f"Mic {mic_descriptor.code} needs channels {mic_descriptor.channel_start}:{channel_end}, but file has {audio_waveform.shape[1]}")
    reference_audio = audio_waveform[:, mic_descriptor.channel_start:channel_end]
    if mic_descriptor.code[2] != "i":
        return reference_audio
    mid_channel = reference_audio[:, 0]
    side_channel = reference_audio[:, 1]
    normalization = math.sqrt(0.5)
    return np.column_stack(((mid_channel + side_channel) * normalization, (mid_channel - side_channel) * normalization))


def power_to_db(power: float) -> float:
    return 10.0 * math.log10(max(power, 1.0e-24))


def detect_onset_sample(audio_waveform: npt.NDArray[np.float64], sample_rate: int) -> int:
    frame_samples = max(1, int(round(sample_rate * ONSET_FRAME_MS / 1000.0)))
    frame_powers = []
    for frame_start in range(0, audio_waveform.shape[0], frame_samples):
        frame = audio_waveform[frame_start:frame_start + frame_samples]
        if frame.size > 0:
            frame_powers.append(float(np.mean(np.square(frame))))
    if not frame_powers:
        return 0
    frame_db = np.array([power_to_db(frame_power) for frame_power in frame_powers])
    threshold_db = max(float(np.max(frame_db)) + ONSET_RELATIVE_THRESHOLD_DB, ONSET_ABSOLUTE_THRESHOLD_DB)
    onset_frame_idx = int(np.argmax(frame_db >= threshold_db))
    return onset_frame_idx * frame_samples


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


def fixed_window_rms_db(audio_waveform: npt.NDArray[np.float64]) -> float:
    return power_to_db(float(np.mean(np.square(audio_waveform))))


def fixed_window_k_weighted_db(audio_waveform: npt.NDArray[np.float64]) -> float:
    channel_powers = np.mean(np.square(audio_waveform), axis=0)
    return -0.691 + power_to_db(float(np.sum(channel_powers)))


def short_rms_peak_db(audio_waveform: npt.NDArray[np.float64], onset_sample: int, sample_rate: int) -> float:
    window_samples = max(1, int(round(sample_rate * SHORT_RMS_WINDOW_MS / 1000.0)))
    search_samples = max(window_samples, int(round(sample_rate * SHORT_RMS_SEARCH_MS / 1000.0)))
    search_end = min(audio_waveform.shape[0], onset_sample + search_samples)
    peak_power = 0.0
    for window_start in range(onset_sample, search_end, window_samples):
        window = audio_waveform[window_start:min(search_end, window_start + window_samples)]
        if window.size > 0:
            peak_power = max(peak_power, float(np.mean(np.square(window))))
    return power_to_db(peak_power)


def true_peak_db(audio_waveform: npt.NDArray[np.float64]) -> float:
    oversampled = resample_poly(audio_waveform, TRUE_PEAK_OVERSAMPLE_FACTOR, 1, axis=0)
    return 20.0 * math.log10(max(float(np.max(np.abs(oversampled))), 1.0e-12))


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


def analyze_logical_hit(case_name: str, sample_files: list[SampleFile]) -> dict[str, object]:
    reference_selection = choose_reference_mic(sample_files)
    if reference_selection is None:
        raise ValueError("No stereo overhead reference mic")
    reference_file, reference_mic = reference_selection

    reference_waveform, sample_rate = sf.read(reference_file.path, dtype="float64", always_2d=True)
    reference_audio = decode_reference_channels(reference_waveform, reference_mic)
    onset_sample = detect_onset_sample(reference_audio, sample_rate)
    weighted_audio = apply_k_weighting(reference_audio, sample_rate)

    metrics = {"short_rms_peak_10ms_dbfs": round(short_rms_peak_db(reference_audio, onset_sample, sample_rate), 4)}
    for window_ms in FIXED_WINDOW_MS:
        raw_window = fixed_window(reference_audio, onset_sample, sample_rate, window_ms)
        weighted_window = fixed_window(weighted_audio, onset_sample, sample_rate, window_ms)
        metrics[f"rms_{window_ms}ms_dbfs"] = round(fixed_window_rms_db(raw_window), 4)
        metrics[f"k_weighted_{window_ms}ms_db"] = round(fixed_window_k_weighted_db(weighted_window), 4)

    balance_window = fixed_window(reference_audio, onset_sample, sample_rate, 200)
    balance_db, balance_position = stereo_balance(balance_window)
    linked_true_peaks = []
    linked_file_details = []
    for sample_file in sample_files:
        audio_waveform, linked_sample_rate = sf.read(sample_file.path, dtype="float64", always_2d=True)
        linked_true_peak_db = true_peak_db(audio_waveform)
        linked_true_peaks.append(linked_true_peak_db)
        linked_file_details.append({
            "path": sample_file.relative_path,
            "channels": audio_waveform.shape[1],
            "sample_rate": linked_sample_rate,
            "true_peak_db": round(linked_true_peak_db, 4),
            "legacy_trim_token": sample_file.legacy_trim_token,
        })

    first_file = sample_files[0]
    return {
        "case": case_name,
        "kit": first_file.kit,
        "instrument": first_file.instrument_folder,
        "articulation": first_file.articulation_folder,
        "velocity": first_file.velocity,
        "round_robin": first_file.round_robin,
        "reference_file": reference_file.relative_path,
        "reference_mic": reference_mic.code,
        "sample_rate": sample_rate,
        "onset_ms": round(onset_sample * 1000.0 / sample_rate, 4),
        "metrics": metrics,
        "linked_max_true_peak_db": round(max(linked_true_peaks), 4),
        "balance_db": None if balance_db is None else round(balance_db, 4),
        "balance_position": None if balance_position is None else round(balance_position, 4),
        "linked_files": linked_file_details,
    }


def finite_median(values: list[float]) -> Optional[float]:
    finite_values = [value for value in values if math.isfinite(value)]
    if not finite_values:
        return None
    return float(statistics.median(finite_values))


def summarize_metric(records: list[dict[str, object]], metric_name: str) -> dict[str, object]:
    articulation_velocity_values = defaultdict(list)
    for record in records:
        articulation_key = record["kit"], record["instrument"], record["articulation"]
        articulation_velocity_values[(articulation_key, record["velocity"])].append(float(record["metrics"][metric_name]))

    round_robin_mads = []
    velocity_medians = defaultdict(dict)
    for (articulation_key, velocity), values in articulation_velocity_values.items():
        velocity_median = float(statistics.median(values))
        velocity_medians[articulation_key][velocity] = velocity_median
        if len(values) >= 2:
            round_robin_mads.append(float(statistics.median(abs(value - velocity_median) for value in values)))

    correlations = []
    dynamic_ranges = []
    inversion_count = 0
    inversion_magnitudes = []
    highest_velocity_medians = []
    for velocity_values in velocity_medians.values():
        ordered_values = sorted(velocity_values.items())
        if len(ordered_values) < 2:
            continue
        velocities = [velocity for velocity, _ in ordered_values]
        medians = [median for _, median in ordered_values]
        correlation = float(spearmanr(velocities, medians).statistic)
        if math.isfinite(correlation):
            correlations.append(correlation)
        dynamic_ranges.append(medians[-1] - medians[0])
        highest_velocity_medians.append(medians[-1])
        for value_idx in range(1, len(medians)):
            if medians[value_idx] < medians[value_idx - 1]:
                inversion_count += 1
                inversion_magnitudes.append(medians[value_idx - 1] - medians[value_idx])

    return {
        "round_robin_group_count": len(round_robin_mads),
        "multi_velocity_articulation_count": len(dynamic_ranges),
        "median_round_robin_mad_db": finite_median(round_robin_mads),
        "median_velocity_spearman": finite_median(correlations),
        "median_low_to_high_range_db": finite_median(dynamic_ranges),
        "velocity_inversion_count": inversion_count,
        "median_inversion_magnitude_db": finite_median(inversion_magnitudes),
        "highest_velocity_median_p10_db": float(np.percentile(highest_velocity_medians, 10)) if highest_velocity_medians else None,
        "highest_velocity_median_p50_db": finite_median(highest_velocity_medians),
        "highest_velocity_median_p90_db": float(np.percentile(highest_velocity_medians, 90)) if highest_velocity_medians else None,
    }


def summarize_records(records: list[dict[str, object]]) -> dict[str, object]:
    metric_names = sorted(records[0]["metrics"].keys()) if records else []
    balance_positions = [abs(float(record["balance_position"])) for record in records if record["balance_position"] is not None]
    linked_true_peaks = [float(record["linked_max_true_peak_db"]) for record in records]
    legacy_trim_files = sum(1 for record in records for linked_file in record["linked_files"] if linked_file["legacy_trim_token"] is not None)
    return {
        "logical_hit_count": len(records),
        "kit_count": len({record["kit"] for record in records}),
        "articulation_count": len({(record["kit"], record["instrument"], record["articulation"]) for record in records}),
        "legacy_trim_file_count": legacy_trim_files,
        "linked_true_peak_p50_db": finite_median(linked_true_peaks),
        "linked_true_peak_p90_db": float(np.percentile(linked_true_peaks, 90)) if linked_true_peaks else None,
        "linked_true_peak_max_db": max(linked_true_peaks) if linked_true_peaks else None,
        "absolute_balance_position_p50": finite_median(balance_positions),
        "absolute_balance_position_p90": float(np.percentile(balance_positions, 90)) if balance_positions else None,
        "metrics": {metric_name: summarize_metric(records, metric_name) for metric_name in metric_names},
    }


def resample_audio(audio_waveform: npt.NDArray[np.float64], source_sample_rate: int, target_sample_rate: int) -> npt.NDArray[np.float64]:
    if source_sample_rate == target_sample_rate:
        return audio_waveform
    common_divisor = math.gcd(source_sample_rate, target_sample_rate)
    return resample_poly(audio_waveform, target_sample_rate // common_divisor, source_sample_rate // common_divisor, axis=0)


def render_listening_sequences(listening_folder: Path, records: list[dict[str, object]], groups: dict[tuple[str, str, str, int, str], list[SampleFile]]) -> list[str]:
    highest_records = {}
    for record in records:
        articulation_key = record["case"], record["kit"], record["instrument"], record["articulation"]
        current_record = highest_records.get(articulation_key)
        if current_record is None or record["velocity"] > current_record["velocity"] or (record["velocity"] == current_record["velocity"] and record["round_robin"] < current_record["round_robin"]):
            highest_records[articulation_key] = record

    ordered_records = [highest_records[articulation_key] for articulation_key in sorted(highest_records)]
    prepared_clips = []
    clip_samples = int(round(LISTENING_SAMPLE_RATE * LISTENING_CLIP_MS / 1000.0))
    gap_samples = int(round(LISTENING_SAMPLE_RATE * LISTENING_GAP_MS / 1000.0))
    for record in ordered_records:
        group_key = record["kit"], record["instrument"], record["articulation"], record["velocity"], record["round_robin"]
        reference_file, reference_mic = choose_reference_mic(groups[group_key])
        audio_waveform, sample_rate = sf.read(reference_file.path, dtype="float64", always_2d=True)
        reference_audio = decode_reference_channels(audio_waveform, reference_mic)
        onset_sample = detect_onset_sample(reference_audio, sample_rate)
        source_clip_samples = int(round(sample_rate * LISTENING_CLIP_MS / 1000.0))
        source_clip = reference_audio[onset_sample:min(reference_audio.shape[0], onset_sample + source_clip_samples)]
        source_clip = resample_audio(source_clip, sample_rate, LISTENING_SAMPLE_RATE)
        if source_clip.shape[0] < clip_samples:
            source_clip = np.pad(source_clip, ((0, clip_samples - source_clip.shape[0]), (0, 0)))
        prepared_clips.append((record, source_clip[:clip_samples]))

    listening_folder.mkdir(parents=True, exist_ok=True)
    output_files = []
    silence = np.zeros((gap_samples, 2), dtype=np.float64)
    for metric_name in LISTENING_METRICS:
        sequence_parts = []
        for record, source_clip in prepared_clips:
            metric_value = float(record["metrics"][metric_name])
            gain = 10.0 ** ((LISTENING_TARGET_DB - metric_value) / 20.0)
            sequence_parts.extend((source_clip * gain, silence))
        sequence = np.concatenate(sequence_parts, axis=0)
        sequence_peak = float(np.max(np.abs(sequence)))
        if sequence_peak > 0.0:
            sequence *= min(1.0, (10.0 ** (-1.0 / 20.0)) / sequence_peak)
        output_path = listening_folder / f"{metric_name}.wav"
        sf.write(output_path, sequence, LISTENING_SAMPLE_RATE, subtype="PCM_24")
        output_files.append(str(output_path))

    manifest_lines = ["TODO14 Phase 1 listening order", "", "Each WAV uses the same order:"]
    for clip_idx, (record, _) in enumerate(prepared_clips, start=1):
        manifest_lines.append(f"{clip_idx}. {record['case']} | {record['kit']} | {record['instrument']} | {record['articulation']} | velocity {record['velocity']} | RR {record['round_robin']}")
    manifest_path = listening_folder / "manifest.txt"
    manifest_path.write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")
    output_files.append(str(manifest_path))
    return output_files


def main() -> None:
    arguments = parse_arguments()
    input_folder = Path(arguments.input_folder).expanduser().resolve()
    output_json = Path(arguments.output_json).expanduser().resolve()
    listening_folder = Path(arguments.listening_folder).expanduser().resolve() if arguments.listening_folder else None
    if not input_folder.is_dir():
        raise SystemExit(f"Input folder does not exist: {input_folder}")

    start_time = time.perf_counter()
    sample_files, scan_warnings = scan_sample_files(input_folder)
    groups = defaultdict(list)
    for sample_file in sample_files:
        groups[logical_hit_key(sample_file)].append(sample_file)

    selected_hits, selection_warnings = select_reference_hits(groups)
    print(f"Selected {len(selected_hits)} logical hits from {len(sample_files)} parsed files")
    records = []
    analysis_warnings = []
    for hit_idx, (case_name, group_key) in enumerate(selected_hits, start=1):
        print(f"Analyzing {hit_idx}/{len(selected_hits)}: {' / '.join(str(value) for value in group_key)}")
        try:
            records.append(analyze_logical_hit(case_name, groups[group_key]))
        except Exception as error:
            analysis_warnings.append(f"Skipped {' / '.join(str(value) for value in group_key)}: {error}")

    listening_files = render_listening_sequences(listening_folder, records, groups) if listening_folder is not None else []
    report = {
        "analysis_version": ANALYSIS_VERSION,
        "input_folder": str(input_folder),
        "settings": {
            "reference_kits": REFERENCE_KIT_NAMES,
            "reference_cases": [asdict(reference_case) for reference_case in REFERENCE_CASES],
            "fixed_window_ms": FIXED_WINDOW_MS,
            "onset_frame_ms": ONSET_FRAME_MS,
            "onset_relative_threshold_db": ONSET_RELATIVE_THRESHOLD_DB,
            "onset_absolute_threshold_db": ONSET_ABSOLUTE_THRESHOLD_DB,
            "short_rms_window_ms": SHORT_RMS_WINDOW_MS,
            "short_rms_search_ms": SHORT_RMS_SEARCH_MS,
            "true_peak_oversample_factor": TRUE_PEAK_OVERSAMPLE_FACTOR,
            "max_variants_per_velocity": MAX_VARIANTS_PER_VELOCITY,
            "reference_mic_order": ("ons", "normal overhead stereo", "other overhead stereo", "decoded overhead mid-side"),
            "listening_metrics": LISTENING_METRICS,
            "listening_target_db": LISTENING_TARGET_DB,
            "listening_clip_ms": LISTENING_CLIP_MS,
            "listening_gap_ms": LISTENING_GAP_MS,
        },
        "summary": summarize_records(records),
        "records": records,
        "listening_files": listening_files,
        "warnings": scan_warnings + selection_warnings + analysis_warnings,
        "elapsed_seconds": round(time.perf_counter() - start_time, 3),
    }
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Wrote {output_json}")


if __name__ == "__main__":
    main()
