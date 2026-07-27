#!/usr/bin/env python3
"""Create a deterministic review plan from one kit's kit-overrides.yml.

This first version is dry-run only. It never renames folders or files and never
changes audio. The JSON result is intended for review before a later safe apply
workflow is enabled.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from kit_overrides_format import (
    AUDIO_EXTENSIONS,
    articulation_folder_with_balance,
    folder_without_balance,
    instrument_folder_with_balance,
    load_kit_overrides,
    parse_sample_name,
    replace_manual_gain,
    replace_playback_offsets,
    replace_velocity,
    sample_name_text,
)


@dataclass
class PlannedSample:
    source_path: Path
    target_name: str
    changes: dict[str, Any] = field(default_factory=dict)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create a review-only JSON correction plan from kit-overrides.yml.")
    parser.add_argument("kit_root", type=Path, help="One kit root containing instrument folders and kit-overrides.yml")
    parser.add_argument("-o", "--output", type=Path, help="Optional JSON output file. Without it, the plan is printed.")
    return parser.parse_args()


def calculate_content_hash(file_path: Path) -> str:
    digest = hashlib.blake2b(digest_size=16)
    with file_path.open("rb") as input_file:
        while True:
            chunk = input_file.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def find_audio_files(kit_root: Path) -> list[Path]:
    return sorted(file_path for file_path in kit_root.rglob("*") if file_path.is_file() and file_path.suffix.lower() in AUDIO_EXTENSIONS)


def load_metadata(kit_root: Path, warnings: list[str]) -> dict[str, list[dict[str, str]]]:
    metadata_path = kit_root / "metadata.csv"
    if not metadata_path.is_file():
        warnings.append("metadata.csv is missing; clipping and metadata identity checks are limited")
        return {}
    rows_by_path: dict[str, list[dict[str, str]]] = {}
    with metadata_path.open("r", encoding="utf-8-sig", newline="") as metadata_file:
        for row in csv.DictReader(metadata_file):
            relative_path = str(row.get("relative_path", "")).replace("\\", "/")
            if relative_path:
                rows_by_path.setdefault(relative_path, []).append(row)
    return rows_by_path


def find_instrument_folder(kit_root: Path, instrument_code: str) -> Path | None:
    exact_path = kit_root / instrument_code
    if exact_path.is_dir():
        return exact_path
    matches = sorted(child for child in kit_root.iterdir() if child.is_dir() and child.name.casefold() == instrument_code.casefold())
    return matches[0] if len(matches) == 1 else None


def matching_articulation_folders(instrument_folder: Path, articulation_code: str) -> list[Path]:
    matches = []
    requested = articulation_code.casefold()
    for child in sorted(instrument_folder.iterdir()):
        if not child.is_dir():
            continue
        raw_identity = folder_without_balance(child.name)
        base_identity = raw_identity.split("-", 1)[0]
        if raw_identity.casefold() == requested or base_identity.casefold() == requested:
            matches.append(child)
    return matches


def resolve_sample_path(kit_root: Path, stored_relative_path: str, content_hash: str, audio_files: list[Path], hash_cache: dict[Path, str], errors: list[str]) -> Path | None:
    exact_path = kit_root / Path(stored_relative_path)
    if exact_path.is_file():
        if content_hash:
            actual_hash = hash_cache.setdefault(exact_path, calculate_content_hash(exact_path))
            if actual_hash.casefold() != content_hash.casefold():
                errors.append(f"Stale content hash for sample '{stored_relative_path}'")
                return None
        return exact_path
    if not content_hash:
        errors.append(f"Missing sample '{stored_relative_path}' and no contentHash is available")
        return None
    matches = []
    for audio_file in audio_files:
        actual_hash = hash_cache.setdefault(audio_file, calculate_content_hash(audio_file))
        if actual_hash.casefold() == content_hash.casefold():
            matches.append(audio_file)
    if len(matches) == 1:
        return matches[0]
    if not matches:
        errors.append(f"Missing sample '{stored_relative_path}'; contentHash did not match any file")
    else:
        errors.append(f"Ambiguous sample '{stored_relative_path}'; contentHash matched {len(matches)} files")
    return None


def metadata_true_peak(rows_by_path: dict[str, list[dict[str, str]]], relative_path: str) -> float | None:
    peaks = []
    for row in rows_by_path.get(relative_path, []):
        try:
            peaks.append(float(row.get("true_peak_db", "")))
        except (TypeError, ValueError):
            continue
    return max(peaks) if peaks else None


def plan_kit(kit_root: Path) -> dict[str, Any]:
    kit_root = kit_root.resolve()
    warnings: list[str] = []
    errors: list[str] = []
    if not kit_root.is_dir():
        raise ValueError(f"Kit root is not a directory: {kit_root}")

    override_path = kit_root / "kit-overrides.yml"
    overrides = load_kit_overrides(override_path)
    audio_files = find_audio_files(kit_root)
    audio_file_set = set(audio_files)
    metadata_rows = load_metadata(kit_root, warnings)
    hash_cache: dict[Path, str] = {}
    planned_samples = {audio_file: PlannedSample(audio_file, audio_file.name) for audio_file in audio_files}
    instrument_targets: dict[Path, str] = {}
    articulation_targets: dict[Path, str] = {}

    instruments = overrides.get("instruments", {})
    for instrument_code, instrument_override in sorted(instruments.items()):
        instrument_folder = find_instrument_folder(kit_root, instrument_code)
        if instrument_folder is None:
            errors.append(f"Instrument folder was not found: {instrument_code}")
            continue
        if "balance" in instrument_override:
            try:
                instrument_targets[instrument_folder] = instrument_folder_with_balance(instrument_folder.name, instrument_override["balance"])
            except ValueError as error:
                errors.append(str(error))

        articulations = instrument_override.get("articulations", {})
        for articulation_code, articulation_override in sorted(articulations.items()):
            articulation_folders = matching_articulation_folders(instrument_folder, articulation_code)
            if not articulation_folders:
                errors.append(f"Articulation folder was not found: {instrument_code}/{articulation_code}")
                continue
            if "balance" in articulation_override:
                for articulation_folder in articulation_folders:
                    articulation_targets[articulation_folder] = articulation_folder_with_balance(articulation_folder.name, articulation_override["balance"])

            boundaries = {int(named_velocity): int(upper_boundary) for named_velocity, upper_boundary in articulation_override.get("velocityUpperBounds", {}).items()}
            for articulation_folder in articulation_folders:
                parsed_layers: set[int] = set()
                for audio_file in sorted(audio_file_set):
                    if audio_file.parent != articulation_folder:
                        continue
                    try:
                        sample_name = parse_sample_name(audio_file)
                    except ValueError as error:
                        errors.append(f"{audio_file.relative_to(kit_root).as_posix()}: {error}")
                        continue
                    parsed_layers.add(sample_name.velocity)
                    if sample_name.velocity not in boundaries:
                        continue
                    target_velocity = boundaries[sample_name.velocity]
                    updated_name = replace_velocity(sample_name, target_velocity)
                    planned_samples[audio_file].target_name = sample_name_text(updated_name)
                    planned_samples[audio_file].changes["velocity"] = {"from": sample_name.velocity, "to": target_velocity, "roundRobin": sample_name.round_robin}
                stale_layers = sorted(set(boundaries) - parsed_layers)
                for stale_layer in stale_layers:
                    errors.append(f"Named velocity {stale_layer:03d} was not found in {instrument_code}/{articulation_folder.name}")

    for stored_relative_path, sample_override in sorted(overrides.get("samples", {}).items()):
        content_hash = str(sample_override.get("contentHash", ""))
        source_path = resolve_sample_path(kit_root, stored_relative_path, content_hash, audio_files, hash_cache, errors)
        if source_path is None:
            continue
        if source_path not in planned_samples:
            errors.append(f"Resolved sample is outside the scanned kit: {source_path}")
            continue
        try:
            sample_name = parse_sample_name(source_path)
            if "velocity" in planned_samples[source_path].changes:
                sample_name = replace_velocity(sample_name, planned_samples[source_path].changes["velocity"]["to"])
            if "manualGainDb" in sample_override:
                manual_gain_db = float(sample_override["manualGainDb"])
                sample_name = replace_manual_gain(sample_name, manual_gain_db)
                planned_samples[source_path].changes["manualGainDb"] = {"to": manual_gain_db}
                current_relative_path = source_path.relative_to(kit_root).as_posix()
                true_peak_db = metadata_true_peak(metadata_rows, current_relative_path)
                if true_peak_db is None:
                    warnings.append(f"No true-peak metadata for '{current_relative_path}'")
                elif true_peak_db + manual_gain_db > 0.0:
                    warnings.append(f"Potential clipping for '{current_relative_path}': estimated {true_peak_db + manual_gain_db:.4f} dBTP")
            mic_overrides = {str(mic_code).casefold(): float(mic_override["playbackOffsetMs"]) for mic_code, mic_override in sample_override.get("mics", {}).items()}
            unknown_mics = sorted(set(mic_overrides) - set(sample_name.mic_codes))
            if unknown_mics:
                errors.append(f"Sample '{stored_relative_path}' has overrides for unknown mics: {', '.join(unknown_mics)}")
                continue
            if "playbackOffsetMs" in sample_override or mic_overrides:
                whole_file_offset = float(sample_override["playbackOffsetMs"]) if "playbackOffsetMs" in sample_override else None
                sample_name = replace_playback_offsets(sample_name, whole_file_offset, mic_overrides)
                planned_samples[source_path].changes["playbackOffsetMs"] = {"wholeFile": whole_file_offset, "mics": dict(sorted(mic_overrides.items()))}
            planned_samples[source_path].target_name = sample_name_text(sample_name)
            if source_path.relative_to(kit_root).as_posix() != stored_relative_path:
                planned_samples[source_path].changes["recoveredFromStoredPath"] = stored_relative_path
        except ValueError as error:
            errors.append(f"{stored_relative_path}: {error}")

    folder_renames = []
    for source_folder, target_name in sorted({**instrument_targets, **articulation_targets}.items(), key=lambda item: item[0].as_posix()):
        if source_folder.name == target_name:
            continue
        target_folder = source_folder.with_name(target_name)
        if target_folder.exists() and target_folder != source_folder:
            errors.append(f"Folder target already exists: {target_folder.relative_to(kit_root).as_posix()}")
        folder_renames.append({"source": source_folder.relative_to(kit_root).as_posix(), "destination": target_folder.relative_to(kit_root).as_posix()})

    sample_changes = []
    source_paths_by_target: dict[str, list[Path]] = {}
    for source_path, planned_sample in sorted(planned_samples.items(), key=lambda item: item[0].as_posix()):
        relative_parts = list(source_path.relative_to(kit_root).parts)
        if len(relative_parts) != 3:
            errors.append(f"Expected instrument/articulation/file layout: {source_path.relative_to(kit_root).as_posix()}")
            continue
        instrument_folder = source_path.parent.parent
        articulation_folder = source_path.parent
        relative_parts[0] = instrument_targets.get(instrument_folder, relative_parts[0])
        relative_parts[1] = articulation_targets.get(articulation_folder, relative_parts[1])
        relative_parts[2] = planned_sample.target_name
        destination_relative = Path(*relative_parts).as_posix()
        source_relative = source_path.relative_to(kit_root).as_posix()
        source_paths_by_target.setdefault(destination_relative.casefold(), []).append(source_path)
        if destination_relative == source_relative:
            continue
        sample_changes.append({"source": source_relative, "destination": destination_relative, "changes": planned_sample.changes})

    planned_sources = {source_path.relative_to(kit_root).as_posix().casefold() for source_path in planned_samples}
    for destination_key, source_paths in sorted(source_paths_by_target.items()):
        if len(source_paths) > 1:
            errors.append(f"Multiple samples target the same path: {destination_key}")
            continue
        destination_path = kit_root / Path(destination_key)
        if destination_path.exists() and destination_key not in planned_sources:
            errors.append(f"Sample target already exists: {destination_key}")

    return {
        "formatVersion": 1,
        "mode": "dry-run",
        "kitRoot": str(kit_root),
        "overrideFile": str(override_path),
        "readyForReviewedApply": not errors,
        "folderRenames": folder_renames,
        "sampleChanges": sample_changes,
        "warnings": sorted(set(warnings)),
        "errors": sorted(set(errors)),
    }


def main() -> int:
    arguments = parse_arguments()
    try:
        plan = plan_kit(arguments.kit_root)
    except (OSError, ValueError) as error:
        print(f"Could not create plan: {error}")
        return 2

    plan_text = json.dumps(plan, indent=2, sort_keys=True) + "\n"
    if arguments.output is None:
        print(plan_text, end="")
    else:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(plan_text, encoding="utf-8")
        print(f"Wrote dry-run plan: {arguments.output}")
    return 0 if plan["readyForReviewedApply"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
