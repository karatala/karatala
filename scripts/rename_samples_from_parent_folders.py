#!/usr/bin/env python3
"""Generate a review .bat to rename sample files from parent folder names.

This tool does not rename files directly. It scans a Karatala instrument library
and writes Windows batch commands that can be reviewed and run later.

Example:
  python scripts/rename_samples_from_parent_folders.py "t:\\install\\music\\Karatala\\sample_library" -o rename_samples.bat
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


AUDIO_EXTENSIONS: tuple[str, ...] = (
    ".wav",
    ".flac",
    ".aif",
    ".aiff",
    ".ogg",
    ".mp3",
)

VELOCITY_RE = re.compile(r"^(?:00[1-9]|0[1-9][0-9]|1[01][0-9]|12[0-7])(?:[a-z])?$", re.IGNORECASE)


@dataclass(frozen=True)
class InstrumentFolderInfo:
    instrument_type: str
    size: str


@dataclass(frozen=True)
class RenamePlanEntry:
    source_path: Path
    target_name: str
    reason: str


@dataclass(frozen=True)
class SkipEntry:
    source_path: Path
    reason: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a review .bat that renames Karatala sample files from parent folder names."
    )
    parser.add_argument("library_root", type=Path, help="Karatala library root with kit folders")
    parser.add_argument(
        "-o",
        "--output-bat",
        type=Path,
        default=Path("rename_samples_from_parent_folders.bat"),
        help="Generated .bat path. Default: rename_samples_from_parent_folders.bat in the current folder.",
    )
    parser.add_argument(
        "--extensions",
        default=",".join(AUDIO_EXTENSIONS),
        help="Comma-separated audio extensions to scan. Default: common audio extensions.",
    )
    return parser.parse_args()


def parse_instrument_folder(folder_name: str) -> InstrumentFolderInfo | None:
    parts = [part.strip() for part in folder_name.split("-") if part.strip()]
    if len(parts) < 2:
        return None
    return InstrumentFolderInfo(instrument_type=parts[0], size=parts[1])


def articulation_file_token(folder_name: str) -> str:
    parts = [part.strip() for part in folder_name.split("-") if part.strip()]
    return parts[0] if parts else ""


def is_audio_file(file_path: Path, extensions: set[str]) -> bool:
    return file_path.is_file() and file_path.suffix.lower() in extensions


def first_velocity_part_index(stem_parts: list[str]) -> int | None:
    for part_idx, part in enumerate(stem_parts):
        if VELOCITY_RE.match(part):
            return part_idx
    return None


def build_target_name(file_path: Path, expected_prefix: str) -> tuple[str | None, str]:
    if file_path.stem.startswith(expected_prefix + "-"):
        return None, "already matches expected prefix"

    stem_parts = [part for part in file_path.stem.split("-") if part]
    velocity_idx = first_velocity_part_index(stem_parts)
    if velocity_idx is None:
        return None, "could not find velocity token"

    if velocity_idx == 0:
        suffix = file_path.stem
    elif velocity_idx >= 3:
        suffix = "-".join(stem_parts[velocity_idx:])
    else:
        return None, "velocity token appears before a complete old prefix"

    if not suffix:
        return None, "empty filename suffix after prefix removal"

    return expected_prefix + "-" + suffix + file_path.suffix, "replace sample prefix"


def batch_quote(value: str) -> str:
    return '"' + value.replace('"', '""') + '"'


def bat_relative_path(root_path: Path, file_path: Path) -> str:
    return str(file_path.relative_to(root_path)).replace("/", "\\")


def find_audio_files(library_root: Path, extensions: set[str]) -> Iterable[Path]:
    for file_path in sorted(library_root.rglob("*")):
        if is_audio_file(file_path, extensions):
            yield file_path


def plan_renames(library_root: Path, extensions: set[str]) -> tuple[list[RenamePlanEntry], list[SkipEntry]]:
    planned_entries: list[RenamePlanEntry] = []
    skipped_entries: list[SkipEntry] = []
    planned_targets: set[str] = set()

    for file_path in find_audio_files(library_root, extensions):
        relative_parts = file_path.relative_to(library_root).parts
        if len(relative_parts) < 4:
            skipped_entries.append(SkipEntry(file_path, "expected kit/instrument/articulation parent folders"))
            continue

        articulation_dir = file_path.parent
        instrument_dir = articulation_dir.parent

        instrument_info = parse_instrument_folder(instrument_dir.name)
        if instrument_info is None:
            skipped_entries.append(SkipEntry(file_path, "instrument folder must start with <type>-<size>"))
            continue

        articulation_token = articulation_file_token(articulation_dir.name)
        if not articulation_token:
            skipped_entries.append(SkipEntry(file_path, "articulation folder has no articulation token"))
            continue

        expected_prefix = f"{instrument_info.instrument_type}-{instrument_info.size}-{articulation_token}"
        target_name, reason = build_target_name(file_path, expected_prefix)
        if target_name is None:
            skipped_entries.append(SkipEntry(file_path, reason))
            continue

        target_path = file_path.with_name(target_name)
        target_key = str(target_path).casefold()
        source_key = str(file_path).casefold()
        if target_key == source_key:
            skipped_entries.append(SkipEntry(file_path, "target differs only by case; rename manually if needed"))
            continue
        if target_path.exists():
            skipped_entries.append(SkipEntry(file_path, f"target already exists: {target_name}"))
            continue
        if target_key in planned_targets:
            skipped_entries.append(SkipEntry(file_path, f"another planned rename already targets: {target_name}"))
            continue

        planned_targets.add(target_key)
        planned_entries.append(RenamePlanEntry(file_path, target_name, reason))

    return planned_entries, skipped_entries


def write_bat(output_bat: Path, library_root: Path, planned_entries: list[RenamePlanEntry], skipped_entries: list[SkipEntry]) -> None:
    lines: list[str] = []
    lines.append("rem Review this file before running it. It was generated by scripts\\rename_samples_from_parent_folders.py It only renames files inside this library root:")
    lines.append("rem " + str(library_root))
    lines.append("pushd " + batch_quote(str(library_root)))
    lines.append("pause")
    lines.append("if errorlevel 1 exit /b 1")

    for skipped_entry in skipped_entries:
        source_rel = bat_relative_path(library_root, skipped_entry.source_path)
        lines.append("rem SKIP " + batch_quote(source_rel) + " - " + skipped_entry.reason)

    if skipped_entries and planned_entries:
        lines.append("")

    for planned_entry in planned_entries:
        source_rel = bat_relative_path(library_root, planned_entry.source_path)
        lines.append("ren " + batch_quote(source_rel) + " " + batch_quote(planned_entry.target_name))

    lines.append("")
    lines.append("popd")
    lines.append("rem Planned renames: " + str(len(planned_entries)))
    lines.append("rem Skipped files: " + str(len(skipped_entries)))

    output_bat.parent.mkdir(parents=True, exist_ok=True)
    output_bat.write_text("\r\n".join(lines) + "\r\n", encoding="utf-8")


def normalized_extensions(raw_extensions: str) -> set[str]:
    extensions: set[str] = set()
    for raw_extension in raw_extensions.split(","):
        extension = raw_extension.strip().lower()
        if not extension:
            continue
        if not extension.startswith("."):
            extension = "." + extension
        extensions.add(extension)
    return extensions


def main() -> int:
    args = parse_args()
    library_root = args.library_root.resolve()
    if not library_root.is_dir():
        print(f"Library root is not a directory: {library_root}")
        return 2

    extensions = normalized_extensions(args.extensions)
    planned_entries, skipped_entries = plan_renames(library_root, extensions)
    write_bat(args.output_bat, library_root, planned_entries, skipped_entries)

    print(f"Wrote: {args.output_bat}")
    print(f"Planned renames: {len(planned_entries)}")
    print(f"Skipped files: {len(skipped_entries)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
