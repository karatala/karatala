"""Shared version-1 kit override parsing and filename helpers.

The parser accepts the map-only YAML shape written by the application. It uses
only the Python standard library so review-plan generation has no package setup.
"""

from __future__ import annotations

import json
import math
import re
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any


AUDIO_EXTENSIONS = {".wav", ".flac", ".aif", ".aiff", ".mp3", ".ogg"}
BALANCE_PATTERN = re.compile(r"^(?:C0|L(?:[1-9]|[1-9][0-9]|1[01][0-9]|12[0-7])|R(?:[1-9]|[1-9][0-9]|1[01][0-9]|12[0-7]))$")
CONTENT_HASH_PATTERN = re.compile(r"^[0-9a-fA-F]{32}$")
VELOCITY_RR_PATTERN = re.compile(r"^([0-9]+)([a-z]*)$")


@dataclass(frozen=True)
class SampleName:
    parts: tuple[str, ...]
    extension: str
    velocity: int
    round_robin: str
    mic_codes: tuple[str, ...]


def strip_yaml_comment(line: str) -> str:
    quote = ""
    escaped = False
    for char_idx, character in enumerate(line):
        if escaped:
            escaped = False
            continue
        if character == "\\" and quote == '"':
            escaped = True
            continue
        if quote:
            if character == quote:
                quote = ""
            continue
        if character in ("'", '"'):
            quote = character
        elif character == "#":
            return line[:char_idx]
    return line


def split_yaml_mapping(line: str) -> tuple[str, str]:
    quote = ""
    escaped = False
    for char_idx, character in enumerate(line):
        if escaped:
            escaped = False
            continue
        if character == "\\" and quote == '"':
            escaped = True
            continue
        if quote:
            if character == quote:
                quote = ""
            continue
        if character in ("'", '"'):
            quote = character
        elif character == ":":
            return line[:char_idx].strip(), line[char_idx + 1:].strip()
    raise ValueError(f"Expected a YAML map entry: {line}")


def decode_yaml_text(value: str) -> str:
    if value.startswith('"'):
        try:
            decoded = json.loads(value)
        except json.JSONDecodeError as error:
            raise ValueError(f"Invalid quoted YAML text: {value}") from error
        if not isinstance(decoded, str):
            raise ValueError(f"Expected quoted text: {value}")
        return decoded
    if value.startswith("'"):
        if len(value) < 2 or not value.endswith("'"):
            raise ValueError(f"Invalid quoted YAML text: {value}")
        return value[1:-1].replace("''", "'")
    return value


def decode_yaml_scalar(value: str) -> Any:
    decoded = decode_yaml_text(value)
    if value.startswith(("'", '"')):
        return decoded
    if re.fullmatch(r"[+-]?[0-9]+", decoded):
        return int(decoded)
    if re.fullmatch(r"[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)", decoded):
        number = float(decoded)
        if not math.isfinite(number):
            raise ValueError(f"Non-finite YAML number: {value}")
        return number
    return decoded


def parse_map_yaml(text: str) -> dict[str, Any]:
    root: dict[str, Any] = {}
    stack: list[tuple[int, dict[str, Any]]] = [(-2, root)]
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        uncommented = strip_yaml_comment(raw_line).rstrip()
        if not uncommented.strip():
            continue
        indentation = len(uncommented) - len(uncommented.lstrip(" "))
        if "\t" in uncommented[:indentation] or indentation % 2 != 0:
            raise ValueError(f"Line {line_number}: indentation must use pairs of spaces")
        key_text, value_text = split_yaml_mapping(uncommented[indentation:])
        key = decode_yaml_text(key_text)
        if not key:
            raise ValueError(f"Line {line_number}: empty map key")
        while stack[-1][0] >= indentation:
            stack.pop()
        parent_indentation, parent = stack[-1]
        if indentation != parent_indentation + 2:
            raise ValueError(f"Line {line_number}: unexpected indentation")
        if key in parent:
            raise ValueError(f"Line {line_number}: duplicate key '{key}'")
        if value_text:
            parent[key] = decode_yaml_scalar(value_text)
        else:
            child: dict[str, Any] = {}
            parent[key] = child
            stack.append((indentation, child))
    return root


def require_map(value: Any, address: str) -> dict[str, Any]:
    if value is None:
        return {}
    if not isinstance(value, dict):
        raise ValueError(f"{address} must be a map")
    return value


def require_known_keys(value: dict[str, Any], known_keys: set[str], address: str) -> None:
    unknown_keys = sorted(set(value) - known_keys)
    if unknown_keys:
        raise ValueError(f"{address} has unsupported keys: {', '.join(unknown_keys)}")


def require_finite_number(value: Any, address: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(float(value)):
        raise ValueError(f"{address} must be a finite number")
    return float(value)


def is_safe_relative_path(value: str) -> bool:
    normalized = value.replace("\\", "/")
    path = PurePosixPath(normalized)
    return bool(normalized) and not path.is_absolute() and all(part not in ("", ".", "..") for part in path.parts)


def validate_kit_overrides(root: dict[str, Any]) -> dict[str, Any]:
    require_known_keys(root, {"version", "instruments", "samples"}, "kit-overrides.yml")
    if root.get("version") != 1:
        raise ValueError("kit-overrides.yml requires version: 1")

    instruments = require_map(root.get("instruments"), "instruments")
    for instrument_code, instrument_value in instruments.items():
        instrument = require_map(instrument_value, f"instrument '{instrument_code}'")
        require_known_keys(instrument, {"balance", "articulations"}, f"instrument '{instrument_code}'")
        if "balance" in instrument and not isinstance(instrument["balance"], str):
            raise ValueError(f"instrument '{instrument_code}' balance must be text")
        if "balance" in instrument and not BALANCE_PATTERN.fullmatch(instrument["balance"]):
            raise ValueError(f"instrument '{instrument_code}' has invalid balance")
        articulations = require_map(instrument.get("articulations"), f"instrument '{instrument_code}' articulations")
        for articulation_code, articulation_value in articulations.items():
            address = f"instrument '{instrument_code}', articulation '{articulation_code}'"
            articulation = require_map(articulation_value, address)
            require_known_keys(articulation, {"balance", "velocityUpperBounds"}, address)
            if "balance" in articulation and (not isinstance(articulation["balance"], str) or not BALANCE_PATTERN.fullmatch(articulation["balance"])):
                raise ValueError(f"{address} has invalid balance")
            boundaries = require_map(articulation.get("velocityUpperBounds"), f"{address} velocityUpperBounds")
            for named_velocity, upper_boundary in boundaries.items():
                if not re.fullmatch(r"[0-9]{1,3}", named_velocity):
                    raise ValueError(f"{address} has invalid named velocity '{named_velocity}'")
                if not isinstance(upper_boundary, int) or not 1 <= int(named_velocity) <= 127 or not 1 <= upper_boundary <= 127:
                    raise ValueError(f"{address} has invalid boundary for '{named_velocity}'")

    samples = require_map(root.get("samples"), "samples")
    for relative_path, sample_value in samples.items():
        address = f"sample '{relative_path}'"
        if not is_safe_relative_path(relative_path):
            raise ValueError(f"{address} is not a safe relative path")
        sample = require_map(sample_value, address)
        require_known_keys(sample, {"contentHash", "manualGainDb", "playbackOffsetMs", "mics"}, address)
        if "contentHash" in sample and (not isinstance(sample["contentHash"], str) or not CONTENT_HASH_PATTERN.fullmatch(sample["contentHash"])):
            raise ValueError(f"{address} has invalid contentHash")
        if "manualGainDb" in sample:
            require_finite_number(sample["manualGainDb"], f"{address} manualGainDb")
        if "playbackOffsetMs" in sample:
            require_finite_number(sample["playbackOffsetMs"], f"{address} playbackOffsetMs")
        mics = require_map(sample.get("mics"), f"{address} mics")
        for mic_code, mic_value in mics.items():
            mic_address = f"{address}, mic '{mic_code}'"
            if not mic_code.strip():
                raise ValueError(f"{mic_address} has an empty code")
            mic = require_map(mic_value, mic_address)
            require_known_keys(mic, {"playbackOffsetMs"}, mic_address)
            if "playbackOffsetMs" not in mic:
                raise ValueError(f"{mic_address} has no playbackOffsetMs")
            require_finite_number(mic["playbackOffsetMs"], f"{mic_address} playbackOffsetMs")
    return root


def load_kit_overrides(file_path: Path) -> dict[str, Any]:
    if not file_path.is_file():
        raise ValueError(f"Override file does not exist: {file_path}")
    return validate_kit_overrides(parse_map_yaml(file_path.read_text(encoding="utf-8")))


def parse_sample_name(file_path: Path) -> SampleName:
    parts = tuple(file_path.stem.split("-"))
    if len(parts) < 5:
        raise ValueError("filename needs instrument, size, articulation, velocity+RR, and mic chain fields")
    velocity_match = VELOCITY_RR_PATTERN.fullmatch(parts[3])
    if velocity_match is None:
        raise ValueError("velocity+RR field must start with digits and end with optional a-z letters")
    velocity = int(velocity_match.group(1))
    if not 1 <= velocity <= 127:
        raise ValueError(f"velocity must be 1-127, got {velocity}")
    mic_codes = tuple(mic_code.lower() for mic_code in parts[4].split("_") if mic_code)
    if not mic_codes:
        raise ValueError("mic chain is empty")
    return SampleName(parts, file_path.suffix, velocity, velocity_match.group(2), mic_codes)


def format_number(value: float) -> str:
    normalized = 0.0 if abs(value) < 0.00005 else value
    return f"{normalized:.4f}".rstrip("0").rstrip(".")


def replace_velocity(sample_name: SampleName, velocity: int) -> SampleName:
    parts = list(sample_name.parts)
    parts[3] = f"{velocity:03d}{sample_name.round_robin}"
    return SampleName(tuple(parts), sample_name.extension, velocity, sample_name.round_robin, sample_name.mic_codes)


def replace_manual_gain(sample_name: SampleName, manual_gain_db: float) -> SampleName:
    parts = list(sample_name.parts[:5]) + [part for part in sample_name.parts[5:] if not part.startswith("v")]
    if abs(manual_gain_db) >= 0.00005:
        parts.append(("vn" if manual_gain_db < 0.0 else "vp") + format_number(abs(manual_gain_db)))
    return SampleName(tuple(parts), sample_name.extension, sample_name.velocity, sample_name.round_robin, sample_name.mic_codes)


def parse_offset_token(token: str) -> tuple[float, tuple[str, ...]] | None:
    if not token.startswith("o") or len(token) < 3:
        return None
    fields = token[1:].split("_")
    signed_value = fields[0]
    if len(signed_value) < 2 or signed_value[0] not in ("n", "p"):
        return None
    try:
        magnitude = float(signed_value[1:])
    except ValueError:
        return None
    if not math.isfinite(magnitude) or magnitude < 0.0:
        return None
    value = -magnitude if signed_value[0] == "n" else magnitude
    return value, tuple(field.lower() for field in fields[1:] if field)


def replace_playback_offsets(sample_name: SampleName, whole_file_override_ms: float | None, mic_overrides_ms: dict[str, float]) -> SampleName:
    filename_whole_ms = 0.0
    filename_mic_ms: dict[str, float] = {}
    for token in sample_name.parts[5:]:
        parsed = parse_offset_token(token)
        if parsed is None:
            continue
        offset_ms, mic_codes = parsed
        if mic_codes:
            for mic_code in mic_codes:
                filename_mic_ms[mic_code] = offset_ms
        else:
            filename_whole_ms = offset_ms

    effective_offsets: dict[str, float] = {}
    for mic_code in sample_name.mic_codes:
        filename_value = filename_mic_ms.get(mic_code, filename_whole_ms)
        inherited_value = whole_file_override_ms if whole_file_override_ms is not None else filename_value
        effective_offsets[mic_code] = mic_overrides_ms.get(mic_code, inherited_value)

    parts = list(sample_name.parts[:5]) + [part for part in sample_name.parts[5:] if parse_offset_token(part) is None]
    unique_values = sorted(set(effective_offsets.values()))
    if len(unique_values) == 1:
        only_value = unique_values[0]
        if abs(only_value) >= 0.00005:
            parts.append(("on" if only_value < 0.0 else "op") + format_number(abs(only_value)))
    else:
        for offset_ms in unique_values:
            if abs(offset_ms) < 0.00005:
                continue
            matching_mics = sorted(mic_code for mic_code, value in effective_offsets.items() if value == offset_ms)
            parts.append(("on" if offset_ms < 0.0 else "op") + format_number(abs(offset_ms)) + "_" + "_".join(matching_mics))
    return SampleName(tuple(parts), sample_name.extension, sample_name.velocity, sample_name.round_robin, sample_name.mic_codes)


def sample_name_text(sample_name: SampleName) -> str:
    return "-".join(sample_name.parts) + sample_name.extension


def folder_without_balance(folder_name: str) -> str:
    parts = folder_name.split("-")
    if parts and BALANCE_PATTERN.fullmatch(parts[-1]):
        parts.pop()
    return "-".join(parts)


def instrument_folder_with_balance(folder_name: str, balance: str) -> str:
    parts = folder_name.split("-")
    if len(parts) >= 3 and BALANCE_PATTERN.fullmatch(parts[2]):
        parts[2] = balance
    elif len(parts) >= 2:
        parts.insert(2, balance)
    else:
        raise ValueError(f"Instrument folder must start with <type>-<size>: {folder_name}")
    return "-".join(parts)


def articulation_folder_with_balance(folder_name: str, balance: str) -> str:
    parts = folder_name.split("-")
    if parts and BALANCE_PATTERN.fullmatch(parts[-1]):
        parts[-1] = balance
    else:
        parts.append(balance)
    return "-".join(parts)
