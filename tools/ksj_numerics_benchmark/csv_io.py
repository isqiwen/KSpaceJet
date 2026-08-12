from __future__ import annotations

import csv
import math
import pathlib
import statistics


REQUIRED_CSV_FIELDS = (
    "case",
    "comparison_group",
    "backend",
    "role",
    "selected_backend",
    "timing_scope",
    "type",
    "size",
    "iterations",
    "trials",
    "mean_ns",
    "median_ns",
    "stddev_ns",
    "ci95_low_ns",
    "ci95_high_ns",
    "min_ns",
    "max_ns",
    "checksum",
    "abs_tolerance",
    "rel_tolerance",
)
VALID_ROLES = frozenset({"oracle", "reference", "candidate", "policy"})


def read_rows(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as file:
        reader = csv.DictReader(file)
        fieldnames = set(reader.fieldnames or [])
        missing_fields = sorted(set(REQUIRED_CSV_FIELDS) - fieldnames)
        if missing_fields:
            raise ValueError(f"benchmark CSV is missing required columns in {path}: {', '.join(missing_fields)}")
        rows = list(reader)
    for row_number, row in enumerate(rows, start=2):
        for field in ("case", "comparison_group", "backend", "role", "timing_scope", "type"):
            if not isinstance(row.get(field), str) or not row[field].strip():
                raise ValueError(f"benchmark CSV has an empty {field} at {path}:{row_number}")
        role = row["role"]
        if role not in VALID_ROLES:
            raise ValueError(f"benchmark CSV has an invalid role {role!r} at {path}:{row_number}")
        selected_backend = row.get("selected_backend")
        if role == "policy" and (not isinstance(selected_backend, str) or not selected_backend.strip()):
            raise ValueError(f"benchmark CSV policy row is missing selected_backend at {path}:{row_number}")
    return rows


def row_float(row: dict[str, str], field: str) -> float:
    try:
        return float(row[field])
    except (KeyError, TypeError, ValueError):
        return math.nan


ROW_ID_FIELDS = (
    "case",
    "comparison_group",
    "backend",
    "role",
    "selected_backend",
    "timing_scope",
    "type",
    "size",
)


def indexed_rows(rows: list[dict[str, str]]) -> dict[tuple[str, ...], dict[str, str]]:
    indexed: dict[tuple[str, ...], dict[str, str]] = {}
    occurrences: dict[tuple[str, ...], int] = {}
    for row in rows:
        identity = tuple(row.get(field, "") for field in ROW_ID_FIELDS)
        occurrence = occurrences.get(identity, 0)
        occurrences[identity] = occurrence + 1
        indexed[identity + (str(occurrence),)] = row
    return indexed


def student_t_critical_95(sample_count: int) -> float:
    if sample_count <= 1:
        return math.nan
    critical_by_degrees_of_freedom = (
        12.706,
        4.303,
        3.182,
        2.776,
        2.571,
        2.447,
        2.365,
        2.306,
        2.262,
        2.228,
        2.201,
        2.179,
        2.160,
        2.145,
        2.131,
        2.120,
        2.110,
        2.101,
        2.093,
        2.086,
        2.080,
        2.074,
        2.069,
        2.064,
        2.060,
        2.056,
        2.052,
        2.048,
        2.045,
        2.042,
    )
    degrees_of_freedom = sample_count - 1
    if degrees_of_freedom <= len(critical_by_degrees_of_freedom):
        return critical_by_degrees_of_freedom[degrees_of_freedom - 1]
    return 1.96


def aggregate_csv_runs(paths: list[pathlib.Path], output: pathlib.Path) -> None:
    if not paths:
        raise ValueError("at least one benchmark CSV is required")

    runs = [indexed_rows(read_rows(path)) for path in paths]
    expected_keys = set(runs[0])
    for path, run in zip(paths[1:], runs[1:]):
        if set(run) != expected_keys:
            missing = len(expected_keys - set(run))
            extra = len(set(run) - expected_keys)
            raise ValueError(f"benchmark rows differ in {path}: missing={missing} extra={extra}")

    with paths[0].open("r", encoding="utf-8", newline="") as file:
        fieldnames = list(csv.DictReader(file).fieldnames or [])
    for field in ("iterations_max", "process_repetitions"):
        if field not in fieldnames:
            fieldnames.append(field)

    aggregate_rows: list[dict[str, str]] = []
    for key, first in runs[0].items():
        row = dict(first)
        if len(runs) > 1:
            medians = [row_float(run[key], "median_ns") for run in runs]
            if not all(math.isfinite(value) for value in medians):
                raise ValueError(f"non-finite process median for benchmark row {key}")
            mean = statistics.fmean(medians)
            median = statistics.median(medians)
            stddev = statistics.stdev(medians)
            margin = student_t_critical_95(len(medians)) * stddev / math.sqrt(len(medians))
            row.update(
                {
                    "mean_ns": f"{mean:.17g}",
                    "median_ns": f"{median:.17g}",
                    "stddev_ns": f"{stddev:.17g}",
                    "ci95_low_ns": f"{mean - margin:.17g}",
                    "ci95_high_ns": f"{mean + margin:.17g}",
                    "min_ns": f"{min(medians):.17g}",
                    "max_ns": f"{max(medians):.17g}",
                }
            )
        iterations = [int(run[key].get("iterations", "0")) for run in runs]
        row["iterations"] = str(min(iterations))
        row["iterations_max"] = str(max(iterations))
        row["process_repetitions"] = str(len(runs))
        aggregate_rows.append(row)

    with output.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(aggregate_rows)


def observed_sizes(path: pathlib.Path) -> str:
    sizes = sorted({int(row["size"]) for row in read_rows(path)})
    if not sizes:
        raise ValueError(f"benchmark CSV has no rows: {path}")
    return ",".join(str(size) for size in sizes)
