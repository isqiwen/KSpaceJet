from __future__ import annotations

import math
import pathlib

from .csv_io import read_rows, row_float
from .models import GateConfig, GateIssue, Winner


def row_key(row: dict[str, str], include_backend: bool = False) -> tuple[str, ...]:
    key = (
        row.get("comparison_group", ""),
        row.get("type", ""),
        row.get("size", ""),
        row.get("timing_scope", "unspecified"),
    )
    if include_backend:
        return key + (row.get("backend", ""), row.get("role", ""))
    return key


def display_key(row: dict[str, str]) -> str:
    group = row.get("comparison_group") or row.get("case", "")
    return f"{group}/{row.get('type', '')}/size={row.get('size', '')}/scope={row.get('timing_scope', 'unspecified')}"


def confidence_separated(faster: dict[str, str], slower: dict[str, str]) -> bool:
    return row_float(faster, "ci95_high_ns") < row_float(slower, "ci95_low_ns")


def default_tolerances(type_name: str, config: GateConfig) -> tuple[float, float]:
    if "double" in type_name:
        return config.double_abs_tolerance, config.double_rel_tolerance
    return config.float_abs_tolerance, config.float_rel_tolerance


def row_tolerances(row: dict[str, str], config: GateConfig) -> tuple[float, float]:
    default_abs, default_rel = default_tolerances(row.get("type", ""), config)
    absolute = row_float(row, "abs_tolerance")
    relative = row_float(row, "rel_tolerance")
    return (
        absolute if math.isfinite(absolute) and absolute >= 0.0 else default_abs,
        relative if math.isfinite(relative) and relative >= 0.0 else default_rel,
    )


def checksums_equal(reference: dict[str, str], actual: dict[str, str], config: GateConfig) -> tuple[bool, str]:
    reference_value = row_float(reference, "checksum")
    actual_value = row_float(actual, "checksum")
    if not math.isfinite(reference_value) or not math.isfinite(actual_value):
        return False, f"non-finite checksum reference={reference_value} actual={actual_value}"

    reference_abs, reference_rel = row_tolerances(reference, config)
    actual_abs, actual_rel = row_tolerances(actual, config)
    absolute_tolerance = max(reference_abs, actual_abs)
    relative_tolerance = max(reference_rel, actual_rel)
    difference = abs(reference_value - actual_value)
    allowed = absolute_tolerance + relative_tolerance * max(abs(reference_value), abs(actual_value))
    return (
        difference <= allowed,
        f"reference={reference_value:.12g} actual={actual_value:.12g} diff={difference:.6g} allowed={allowed:.6g}",
    )


def grouped_comparisons(rows: list[dict[str, str]], include_scope: bool) -> dict[tuple[str, ...], list[dict[str, str]]]:
    groups: dict[tuple[str, ...], list[dict[str, str]]] = {}
    for row in rows:
        key = (
            row["comparison_group"],
            row.get("type", ""),
            row.get("size", ""),
        )
        if include_scope:
            key += (row.get("timing_scope", "unspecified"),)
        groups.setdefault(key, []).append(row)
    return groups


def evaluate_correctness(module: str, rows: list[dict[str, str]], config: GateConfig) -> list[GateIssue]:
    issues: list[GateIssue] = []
    for grouped_rows in grouped_comparisons(rows, include_scope=False).values():
        reference = next(
            (row for row in grouped_rows if row.get("role") == "oracle"),
            next((row for row in grouped_rows if row.get("role") == "reference"), grouped_rows[0]),
        )
        for row in grouped_rows:
            equal, detail = checksums_equal(reference, row, config)
            if not equal:
                issues.append(
                    GateIssue(
                        module,
                        "correctness",
                        "failure",
                        display_key(row),
                        f"{row.get('backend')} differs from {reference.get('backend')}: {detail}",
                    )
                )
    return issues


def evaluate_policy_and_winners(
    module: str, rows: list[dict[str, str]], config: GateConfig
) -> tuple[list[GateIssue], list[Winner]]:
    issues: list[GateIssue] = []
    winners: list[Winner] = []
    for grouped_rows in grouped_comparisons(rows, include_scope=True).values():
        correctness_reference = next(
            (row for row in grouped_rows if row.get("role") == "oracle"),
            next((row for row in grouped_rows if row.get("role") == "reference"), grouped_rows[0]),
        )
        candidates = [
            row
            for row in grouped_rows
            if row.get("role") in {"reference", "candidate"}
            and math.isfinite(row_float(row, "median_ns"))
            and checksums_equal(correctness_reference, row, config)[0]
        ]
        candidates.sort(key=lambda row: row_float(row, "median_ns"))
        if not candidates:
            continue

        best = candidates[0]
        if len(candidates) > 1:
            second = candidates[1]
            best_median = row_float(best, "median_ns")
            second_median = row_float(second, "median_ns")
            speedup = (second_median / best_median - 1.0) * 100.0 if best_median > 0.0 else math.inf
            separated = confidence_separated(best, second)
            meaningful = speedup >= config.min_speedup_percent and separated
            winners.append(
                Winner(
                    module,
                    display_key(best),
                    best.get("backend", ""),
                    best_median,
                    speedup,
                    separated,
                    "winner" if meaningful else "tie",
                )
            )

        for policy in (row for row in grouped_rows if row.get("role") == "policy"):
            selected_backend = policy.get("selected_backend", "")
            status = "failure" if config.mode == "policy" else "warning"
            if not selected_backend:
                issues.append(
                    GateIssue(
                        module,
                        "policy",
                        status,
                        display_key(policy),
                        "policy row does not declare selected_backend",
                    )
                )
                continue

            selected_candidate = next(
                (candidate for candidate in candidates if candidate.get("backend") == selected_backend),
                None,
            )
            if selected_candidate is None:
                issues.append(
                    GateIssue(
                        module,
                        "policy",
                        status,
                        display_key(policy),
                        f"policy selected backend {selected_backend}, but no matching candidate row exists",
                    )
                )
                continue

            best_median = row_float(best, "median_ns")
            policy_median = row_float(selected_candidate, "median_ns")
            if best_median <= 0.0 or not math.isfinite(policy_median):
                continue
            gap = (policy_median / best_median - 1.0) * 100.0
            if gap > config.max_policy_gap_percent and confidence_separated(best, selected_candidate):
                issues.append(
                    GateIssue(
                        module,
                        "policy",
                        status,
                        display_key(policy),
                        f"selected backend {selected_backend} is {gap:.2f}% slower than {best.get('backend')} "
                        f"(allowed {config.max_policy_gap_percent:.2f}%)",
                    )
                )
    return issues, winners


def evaluate_regressions(
    module: str,
    current_rows: list[dict[str, str]],
    baseline_rows: list[dict[str, str]],
    config: GateConfig,
) -> list[GateIssue]:
    issues: list[GateIssue] = []
    baseline = {row_key(row, include_backend=True): row for row in baseline_rows}
    for current in current_rows:
        previous = baseline.get(row_key(current, include_backend=True))
        if previous is None:
            continue
        previous_median = row_float(previous, "median_ns")
        current_median = row_float(current, "median_ns")
        if previous_median <= 0.0 or not math.isfinite(current_median):
            continue
        regression = (current_median / previous_median - 1.0) * 100.0
        confident = row_float(previous, "ci95_high_ns") < row_float(current, "ci95_low_ns")
        if regression > config.max_regression_percent and confident:
            status = "failure" if config.mode == "policy" else "warning"
            issues.append(
                GateIssue(
                    module,
                    "regression",
                    status,
                    display_key(current),
                    f"{current.get('backend')} regressed {regression:.2f}% "
                    f"(allowed {config.max_regression_percent:.2f}%)",
                )
            )
    return issues


def evaluate_suite(
    csv_paths: dict[str, pathlib.Path], baseline_dir: pathlib.Path | None, config: GateConfig
) -> tuple[list[GateIssue], list[Winner]]:
    issues: list[GateIssue] = []
    winners: list[Winner] = []
    for module, path in csv_paths.items():
        rows = read_rows(path)
        issues.extend(evaluate_correctness(module, rows, config))
        policy_issues, module_winners = evaluate_policy_and_winners(module, rows, config)
        issues.extend(policy_issues)
        winners.extend(module_winners)
        if baseline_dir is not None:
            baseline_path = baseline_dir / path.name
            if baseline_path.exists():
                issues.extend(evaluate_regressions(module, rows, read_rows(baseline_path), config))
            else:
                issues.append(
                    GateIssue(module, "regression", "warning", path.name, f"baseline CSV is missing: {baseline_path}")
                )
    return issues, winners


def fastest_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    fastest: dict[tuple[str, str, str, str], dict[str, str]] = {}
    for row in rows:
        key = (
            row.get("case", ""),
            row.get("type", ""),
            row.get("size", ""),
            row.get("timing_scope", "unspecified"),
        )
        current = fastest.get(key)
        if current is None or row_float(row, "median_ns") < row_float(current, "median_ns"):
            fastest[key] = row
    return [fastest[key] for key in sorted(fastest)]
