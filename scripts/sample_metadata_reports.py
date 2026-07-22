import csv
import math
import os
import re
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Optional


ANALYSIS_VERSION = "todo14-phase6-v1"
MINIMUM_HIT_COUNT = 6
MINIMUM_VELOCITY_COUNT = 2
MINIMUM_AGREEMENT_RATIO = 0.8
CENTER_DEAD_ZONE = 8.0
MINIMUM_SIDE_POSITION = 12.0
MAXIMUM_POSITION_MAD = 12.0

BALANCE_REPORT_COLUMNS = (
    "analysis_version",
    "kit",
    "scope",
    "instrument_folder",
    "articulation_folder",
    "hit_count",
    "velocity_count",
    "median_balance",
    "balance_mad",
    "agreement_ratio",
    "current_balance",
    "recommended_balance",
    "current_folder",
    "recommended_folder",
    "action",
    "reason",
)

ROOM_ORIENTATION_REPORT_COLUMNS = (
    "analysis_version",
    "kit",
    "instrument_folder",
    "articulation_folder",
    "room_mic_code",
    "overhead_mic_codes",
    "hit_count",
    "directional_hit_count",
    "velocity_count",
    "overhead_median_balance",
    "room_median_balance",
    "overhead_balance_mad",
    "room_balance_mad",
    "opposite_ratio",
    "recommended_flip_lr",
    "action",
    "reason",
)

TIMING_REPORT_COLUMNS = (
    "analysis_version",
    "kit",
    "instrument_folder",
    "articulation_folder",
    "mic_code",
    "reference_mic_codes",
    "hit_count",
    "velocity_count",
    "median_offset_ms",
    "offset_mad_ms",
    "minimum_offset_ms",
    "maximum_offset_ms",
    "articulation_count_for_mic",
    "between_articulation_range_ms",
    "classification",
    "recommended_offset_ms",
    "action",
)

BALANCE_TOKEN_PATTERN = re.compile(r"^(?:C0|L(?:[1-9]|[1-9][0-9]|1[01][0-9]|12[0-7])|R(?:[1-9]|[1-9][0-9]|1[01][0-9]|12[0-7]))$")


def median_absolute_deviation(values: list[float]) -> float:
    median_value = float(statistics.median(values))
    return float(statistics.median(abs(value - median_value) for value in values))


def format_balance(balance: Optional[int]) -> str:
    if balance is None:
        return ""
    if balance == 0:
        return "C0"
    return f"L{-balance}" if balance < 0 else f"R{balance}"


def parse_balance_token(token: str) -> Optional[int]:
    if not BALANCE_TOKEN_PATTERN.fullmatch(token):
        return None
    if token == "C0":
        return 0
    value = int(token[1:])
    return -value if token[0] == "L" else value


def instrument_folder_balance(folder_name: str) -> Optional[int]:
    parts = folder_name.split("-")
    return parse_balance_token(parts[2]) if len(parts) >= 3 else None


def articulation_folder_balance(folder_name: str) -> Optional[int]:
    parts = folder_name.split("-")
    return parse_balance_token(parts[-1]) if parts else None


def instrument_folder_with_balance(folder_name: str, balance: int) -> str:
    parts = folder_name.split("-")
    balance_token = format_balance(balance)
    if len(parts) >= 3 and parse_balance_token(parts[2]) is not None:
        parts[2] = balance_token
    elif len(parts) >= 2:
        parts.insert(2, balance_token)
    else:
        return folder_name
    return "-".join(parts)


def articulation_folder_with_balance(folder_name: str, balance: int) -> str:
    parts = folder_name.split("-")
    balance_token = format_balance(balance)
    if parts and parse_balance_token(parts[-1]) is not None:
        parts[-1] = balance_token
    else:
        parts.append(balance_token)
    return "-".join(parts)


def articulation_folder_without_balance(folder_name: str) -> str:
    parts = folder_name.split("-")
    if parts and parse_balance_token(parts[-1]) is not None:
        parts.pop()
    return "-".join(parts)


def logical_hit_key(row: dict[str, object]) -> tuple[str, str, int, str]:
    return str(row["_instrument_folder"]), str(row["_articulation_folder"]), int(row["_velocity"]), str(row["_round_robin"])


def overhead_priority(row: dict[str, object]) -> tuple[int, str, str]:
    mic_code = str(row["mic_code"]).lower()
    if mic_code[:3] == "ons":
        priority = 0
    elif len(mic_code) >= 3 and mic_code[0] == "o" and mic_code[2] == "s":
        priority = 1
    else:
        priority = 2
    return priority, mic_code, str(row["relative_path"])


def choose_overhead_reference(rows: list[dict[str, object]], required_field: str) -> Optional[dict[str, object]]:
    candidates = [
        row for row in rows
        if len(str(row["mic_code"])) >= 3
        and str(row["mic_code"])[0].lower() == "o"
        and str(row["mic_code"])[2].lower() in ("s", "i")
        and row.get(required_field) is not None
    ]
    return min(candidates, key=overhead_priority) if candidates else None


def choose_timing_reference(rows: list[dict[str, object]]) -> Optional[dict[str, object]]:
    candidates = [row for row in rows if row.get("onset_ms") is not None and str(row["mic_code"]).lower().startswith("d")]
    if candidates:
        return min(candidates, key=lambda row: (float(row["onset_ms"]), str(row["mic_code"]), str(row["relative_path"])))
    overhead = choose_overhead_reference(rows, "onset_ms")
    if overhead is not None:
        return overhead
    candidates = [row for row in rows if row.get("onset_ms") is not None]
    return min(candidates, key=lambda row: (float(row["onset_ms"]), str(row["mic_code"]), str(row["relative_path"]))) if candidates else None


def balance_statistics(evidence: list[dict[str, object]]) -> dict[str, object]:
    if not evidence:
        return {
            "hit_count": 0,
            "velocity_count": 0,
            "median_balance": None,
            "balance_mad": None,
            "agreement_ratio": None,
            "recommended_balance": None,
            "reason": "no_stereo_overhead_evidence",
        }
    values = [float(item["balance"]) for item in evidence]
    velocities = {int(item["velocity"]) for item in evidence}
    median_balance = float(statistics.median(values))
    balance_mad = median_absolute_deviation(values)
    if abs(median_balance) <= CENTER_DEAD_ZONE:
        agreement_ratio = sum(abs(value) <= CENTER_DEAD_ZONE for value in values) / len(values)
        recommended_balance = 0 if agreement_ratio >= MINIMUM_AGREEMENT_RATIO and balance_mad <= MAXIMUM_POSITION_MAD else None
    else:
        same_side_count = sum(value < -CENTER_DEAD_ZONE for value in values) if median_balance < 0.0 else sum(value > CENTER_DEAD_ZONE for value in values)
        agreement_ratio = same_side_count / len(values)
        has_confident_side = abs(median_balance) > MINIMUM_SIDE_POSITION and agreement_ratio >= MINIMUM_AGREEMENT_RATIO and balance_mad <= MAXIMUM_POSITION_MAD
        recommended_balance = int(round(max(-127.0, min(127.0, median_balance)))) if has_confident_side else None

    if len(values) < MINIMUM_HIT_COUNT:
        recommended_balance = None
        reason = "fewer_than_6_hits"
    elif len(velocities) < MINIMUM_VELOCITY_COUNT:
        recommended_balance = None
        reason = "fewer_than_2_velocities"
    elif balance_mad > MAXIMUM_POSITION_MAD:
        recommended_balance = None
        reason = "position_mad_above_12"
    elif agreement_ratio < MINIMUM_AGREEMENT_RATIO:
        recommended_balance = None
        reason = "side_or_center_agreement_below_80_percent"
    elif CENTER_DEAD_ZONE < abs(median_balance) <= MINIMUM_SIDE_POSITION:
        recommended_balance = None
        reason = "position_between_center_and_side_thresholds"
    else:
        reason = "high_confidence"

    return {
        "hit_count": len(values),
        "velocity_count": len(velocities),
        "median_balance": median_balance,
        "balance_mad": balance_mad,
        "agreement_ratio": agreement_ratio,
        "recommended_balance": recommended_balance,
        "reason": reason,
    }


def same_balance_region(first_balance: int, second_balance: int) -> bool:
    if first_balance == 0 or second_balance == 0:
        return first_balance == second_balance
    return (first_balance < 0) == (second_balance < 0)


def make_balance_report(kit_name: str, rows: list[dict[str, object]]) -> list[dict[str, object]]:
    logical_hits = defaultdict(list)
    for row in rows:
        logical_hits[logical_hit_key(row)].append(row)

    articulation_evidence = defaultdict(list)
    for (instrument_folder, articulation_folder, velocity, _round_robin), hit_rows in logical_hits.items():
        overhead = choose_overhead_reference(hit_rows, "stereo_balance")
        if overhead is not None:
            articulation_evidence[(instrument_folder, articulation_folder)].append({"balance": overhead["stereo_balance"], "velocity": velocity})

    all_articulation_keys = sorted({(str(row["_instrument_folder"]), str(row["_articulation_folder"])) for row in rows})
    evidence_by_instrument = defaultdict(list)
    articulation_stats = {}
    for articulation_key in all_articulation_keys:
        evidence = articulation_evidence.get(articulation_key, [])
        articulation_stats[articulation_key] = balance_statistics(evidence)
        evidence_by_instrument[articulation_key[0]].extend(evidence)

    report_rows = []
    for instrument_folder in sorted(evidence_by_instrument):
        instrument_articulation_keys = sorted(key for key in articulation_stats if key[0] == instrument_folder)
        instrument_stats = balance_statistics(evidence_by_instrument[instrument_folder])
        articulation_recommendations = [articulation_stats[key]["recommended_balance"] for key in instrument_articulation_keys]
        all_articulations_confident = bool(articulation_recommendations) and all(balance is not None for balance in articulation_recommendations)
        articulations_agree = all_articulations_confident and all(same_balance_region(int(articulation_recommendations[0]), int(balance)) for balance in articulation_recommendations[1:])
        instrument_recommendation = instrument_stats["recommended_balance"] if articulations_agree else None
        current_instrument_balance = instrument_folder_balance(instrument_folder)
        recommended_instrument_folder = instrument_folder_with_balance(instrument_folder, int(instrument_recommendation)) if instrument_recommendation is not None else instrument_folder
        if instrument_recommendation is None:
            instrument_action = "no_instrument_change"
            if not all_articulations_confident:
                instrument_reason = "one_or_more_articulations_low_confidence"
            elif not articulations_agree:
                instrument_reason = "articulations_do_not_agree"
            else:
                instrument_reason = str(instrument_stats["reason"])
        elif current_instrument_balance == instrument_recommendation:
            instrument_action = "keep_existing"
            instrument_reason = "high_confidence"
        else:
            instrument_action = "review_instrument_folder"
            instrument_reason = "high_confidence"

        report_rows.append({
            "analysis_version": ANALYSIS_VERSION,
            "kit": kit_name,
            "scope": "instrument",
            "instrument_folder": instrument_folder,
            "articulation_folder": "",
            **{key: instrument_stats[key] for key in ("hit_count", "velocity_count", "median_balance", "balance_mad", "agreement_ratio")},
            "current_balance": format_balance(current_instrument_balance),
            "recommended_balance": format_balance(instrument_recommendation),
            "current_folder": instrument_folder,
            "recommended_folder": recommended_instrument_folder,
            "action": instrument_action,
            "reason": instrument_reason,
        })

        for articulation_key in instrument_articulation_keys:
            articulation_folder = articulation_key[1]
            stats = articulation_stats[articulation_key]
            current_articulation_balance = articulation_folder_balance(articulation_folder)
            if instrument_recommendation is not None:
                recommended_balance = instrument_recommendation
                recommended_folder = articulation_folder_without_balance(articulation_folder)
                action = "remove_override_for_instrument_balance" if recommended_folder != articulation_folder else "covered_by_instrument"
                reason = "articulations_agree"
            elif stats["recommended_balance"] is not None:
                recommended_balance = stats["recommended_balance"]
                recommended_folder = articulation_folder_with_balance(articulation_folder, int(recommended_balance))
                action = "keep_existing" if current_articulation_balance == recommended_balance else "review_articulation_folder"
                reason = str(stats["reason"])
            else:
                recommended_balance = None
                recommended_folder = articulation_folder
                action = "no_change"
                reason = str(stats["reason"])
            report_rows.append({
                "analysis_version": ANALYSIS_VERSION,
                "kit": kit_name,
                "scope": "articulation",
                "instrument_folder": instrument_folder,
                "articulation_folder": articulation_folder,
                **{key: stats[key] for key in ("hit_count", "velocity_count", "median_balance", "balance_mad", "agreement_ratio")},
                "current_balance": format_balance(current_articulation_balance if current_articulation_balance is not None else current_instrument_balance),
                "recommended_balance": format_balance(recommended_balance),
                "current_folder": articulation_folder,
                "recommended_folder": recommended_folder,
                "action": action,
                "reason": reason,
            })
    return report_rows


def make_room_orientation_report(kit_name: str, rows: list[dict[str, object]]) -> list[dict[str, object]]:
    logical_hits = defaultdict(list)
    for row in rows:
        logical_hits[logical_hit_key(row)].append(row)

    evidence_by_room_mic = defaultdict(list)
    for (instrument_folder, articulation_folder, velocity, _round_robin), hit_rows in logical_hits.items():
        overhead = choose_overhead_reference(hit_rows, "stereo_balance")
        if overhead is None:
            continue
        overhead_balance = float(overhead["stereo_balance"])
        for room_row in hit_rows:
            mic_code = str(room_row["mic_code"]).lower()
            if len(mic_code) < 3 or mic_code[0] != "r" or mic_code[2] not in ("s", "i") or room_row.get("stereo_balance") is None:
                continue
            room_balance = float(room_row["stereo_balance"])
            evidence_by_room_mic[(instrument_folder, articulation_folder, mic_code)].append({
                "velocity": velocity,
                "overhead_balance": overhead_balance,
                "room_balance": room_balance,
                "overhead_mic_code": str(overhead["mic_code"]),
                "directional": abs(overhead_balance) > MINIMUM_SIDE_POSITION and abs(room_balance) > MINIMUM_SIDE_POSITION,
                "opposite": (overhead_balance < 0.0) != (room_balance < 0.0),
            })

    report_rows = []
    for (instrument_folder, articulation_folder, room_mic_code), evidence in sorted(evidence_by_room_mic.items()):
        overhead_values = [float(item["overhead_balance"]) for item in evidence]
        room_values = [float(item["room_balance"]) for item in evidence]
        directional_evidence = [item for item in evidence if bool(item["directional"])]
        velocity_count = len({int(item["velocity"]) for item in directional_evidence})
        opposite_ratio = sum(bool(item["opposite"]) for item in directional_evidence) / len(directional_evidence) if directional_evidence else None
        overhead_mad = median_absolute_deviation(overhead_values)
        room_mad = median_absolute_deviation(room_values)
        recommended_flip_lr = None
        if len(directional_evidence) < MINIMUM_HIT_COUNT:
            reason = "fewer_than_6_directional_pairs"
        elif velocity_count < MINIMUM_VELOCITY_COUNT:
            reason = "fewer_than_2_velocities"
        elif overhead_mad > MAXIMUM_POSITION_MAD or room_mad > MAXIMUM_POSITION_MAD:
            reason = "position_mad_above_12"
        elif opposite_ratio is not None and opposite_ratio >= MINIMUM_AGREEMENT_RATIO:
            recommended_flip_lr = 1
            reason = "strong_repeated_opposite_orientation"
        elif opposite_ratio is not None and 1.0 - opposite_ratio >= MINIMUM_AGREEMENT_RATIO:
            recommended_flip_lr = 0
            reason = "strong_repeated_matching_orientation"
        else:
            reason = "orientation_agreement_below_80_percent"

        action = "review_room_lr_swap" if recommended_flip_lr == 1 else "keep_orientation" if recommended_flip_lr == 0 else "no_change"
        report_rows.append({
            "analysis_version": ANALYSIS_VERSION,
            "kit": kit_name,
            "instrument_folder": instrument_folder,
            "articulation_folder": articulation_folder,
            "room_mic_code": room_mic_code,
            "overhead_mic_codes": ";".join(sorted({str(item["overhead_mic_code"]) for item in evidence})),
            "hit_count": len(evidence),
            "directional_hit_count": len(directional_evidence),
            "velocity_count": velocity_count,
            "overhead_median_balance": float(statistics.median(overhead_values)),
            "room_median_balance": float(statistics.median(room_values)),
            "overhead_balance_mad": overhead_mad,
            "room_balance_mad": room_mad,
            "opposite_ratio": opposite_ratio,
            "recommended_flip_lr": recommended_flip_lr,
            "action": action,
            "reason": reason,
        })
    return report_rows


def make_timing_report(kit_name: str, rows: list[dict[str, object]], onset_frame_ms: int) -> list[dict[str, object]]:
    logical_hits = defaultdict(list)
    for row in rows:
        logical_hits[logical_hit_key(row)].append(row)

    evidence_by_mic = defaultdict(list)
    for (instrument_folder, articulation_folder, velocity, _round_robin), hit_rows in logical_hits.items():
        reference = choose_timing_reference(hit_rows)
        if reference is None:
            continue
        reference_onset_ms = float(reference["onset_ms"])
        for row in hit_rows:
            if row is reference or row.get("onset_ms") is None:
                continue
            mic_code = str(row["mic_code"]).lower()
            evidence_by_mic[(instrument_folder, articulation_folder, mic_code)].append({
                "velocity": velocity,
                "offset_ms": float(row["onset_ms"]) - reference_onset_ms,
                "reference_mic_code": str(reference["mic_code"]).lower(),
            })

    summaries = {}
    for timing_key, evidence in evidence_by_mic.items():
        offsets = [float(item["offset_ms"]) for item in evidence]
        summaries[timing_key] = {
            "hit_count": len(evidence),
            "velocity_count": len({int(item["velocity"]) for item in evidence}),
            "median_offset_ms": float(statistics.median(offsets)),
            "offset_mad_ms": median_absolute_deviation(offsets),
            "minimum_offset_ms": min(offsets),
            "maximum_offset_ms": max(offsets),
            "reference_mic_codes": ";".join(sorted({str(item["reference_mic_code"]) for item in evidence})),
        }

    qualified_by_code = defaultdict(list)
    for timing_key, summary in summaries.items():
        if int(summary["hit_count"]) >= MINIMUM_HIT_COUNT and int(summary["velocity_count"]) >= MINIMUM_VELOCITY_COUNT:
            qualified_by_code[timing_key[2]].append(summary)

    classification_by_code = {}
    for mic_code, mic_summaries in qualified_by_code.items():
        medians = [float(summary["median_offset_ms"]) for summary in mic_summaries]
        articulation_count = len(mic_summaries)
        median_offset_ms = float(statistics.median(medians))
        articulation_range_ms = max(medians) - min(medians)
        maximum_mad_ms = max(float(summary["offset_mad_ms"]) for summary in mic_summaries)
        reference_code_sets = {str(summary["reference_mic_codes"]) for summary in mic_summaries}
        if abs(median_offset_ms) < onset_frame_ms and articulation_range_ms <= onset_frame_ms:
            classification = "near_zero"
            recommended_offset_ms = None
        elif articulation_count >= 3 and len(reference_code_sets) == 1 and articulation_range_ms <= onset_frame_ms and maximum_mad_ms <= onset_frame_ms:
            classification = "possible_technical_offset"
            recommended_offset_ms = -median_offset_ms
        elif mic_code.startswith(("o", "r")) and articulation_count >= 2 and articulation_range_ms > onset_frame_ms:
            classification = "likely_acoustic_variation"
            recommended_offset_ms = None
        else:
            classification = "uncertain"
            recommended_offset_ms = None
        classification_by_code[mic_code] = {
            "articulation_count": articulation_count,
            "articulation_range_ms": articulation_range_ms,
            "classification": classification,
            "recommended_offset_ms": recommended_offset_ms,
        }

    report_rows = []
    for (instrument_folder, articulation_folder, mic_code), summary in sorted(summaries.items()):
        classification = classification_by_code.get(mic_code)
        if int(summary["hit_count"]) < MINIMUM_HIT_COUNT or int(summary["velocity_count"]) < MINIMUM_VELOCITY_COUNT:
            classification_name = "insufficient_data"
            articulation_count = 0
            articulation_range_ms = None
            recommended_offset_ms = None
        elif classification is None:
            classification_name = "uncertain"
            articulation_count = 1
            articulation_range_ms = 0.0
            recommended_offset_ms = None
        else:
            classification_name = str(classification["classification"])
            articulation_count = int(classification["articulation_count"])
            articulation_range_ms = classification["articulation_range_ms"]
            recommended_offset_ms = classification["recommended_offset_ms"]
        report_rows.append({
            "analysis_version": ANALYSIS_VERSION,
            "kit": kit_name,
            "instrument_folder": instrument_folder,
            "articulation_folder": articulation_folder,
            "mic_code": mic_code,
            "reference_mic_codes": summary["reference_mic_codes"],
            "hit_count": summary["hit_count"],
            "velocity_count": summary["velocity_count"],
            "median_offset_ms": summary["median_offset_ms"],
            "offset_mad_ms": summary["offset_mad_ms"],
            "minimum_offset_ms": summary["minimum_offset_ms"],
            "maximum_offset_ms": summary["maximum_offset_ms"],
            "articulation_count_for_mic": articulation_count,
            "between_articulation_range_ms": articulation_range_ms,
            "classification": classification_name,
            "recommended_offset_ms": recommended_offset_ms,
            "action": "review_only",
        })
    return report_rows


def format_csv_value(value: object) -> object:
    if value is None:
        return ""
    if isinstance(value, float) and math.isfinite(value):
        return round(value, 4)
    return value


def write_report_csv(output_path: Path, columns: tuple[str, ...], rows: list[dict[str, object]]) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = output_path.with_name("." + output_path.name + ".tmp")
    try:
        with temporary_path.open("w", newline="", encoding="utf-8") as output_file:
            writer = csv.DictWriter(output_file, fieldnames=columns)
            writer.writeheader()
            writer.writerows({column_name: format_csv_value(row.get(column_name)) for column_name in columns} for row in rows)
            output_file.flush()
            os.fsync(output_file.fileno())
        os.replace(temporary_path, output_path)
    finally:
        temporary_path.unlink(missing_ok=True)


def write_phase6_reports(report_root: Path, kit_root: Path, rows: list[dict[str, object]], onset_frame_ms: int) -> list[Path]:
    kit_report_folder = report_root / kit_root.name
    reports = (
        (kit_report_folder / "balance_recommendations.csv", BALANCE_REPORT_COLUMNS, make_balance_report(kit_root.name, rows)),
        (kit_report_folder / "room_orientation.csv", ROOM_ORIENTATION_REPORT_COLUMNS, make_room_orientation_report(kit_root.name, rows)),
        (kit_report_folder / "mic_timing.csv", TIMING_REPORT_COLUMNS, make_timing_report(kit_root.name, rows, onset_frame_ms)),
    )
    for output_path, columns, report_rows in reports:
        write_report_csv(output_path, columns, report_rows)
    return [output_path for output_path, _columns, _report_rows in reports]
