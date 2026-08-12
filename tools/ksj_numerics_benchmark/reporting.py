from __future__ import annotations

from dataclasses import asdict
import datetime as dt
import json
import pathlib
import platform
import shutil

from .csv_io import read_rows, row_float
from .evaluation import fastest_rows
from .models import GateConfig, GateIssue, Winner


def write_report(
    csv_paths: dict[str, pathlib.Path],
    sizes_by_name: dict[str, str],
    out_dir: pathlib.Path,
    iterations: int,
    trials: int,
    process_repetitions: int,
    min_sample_time_us: int,
    gate_config: GateConfig,
    issues: list[GateIssue],
    winners: list[Winner],
    cpu_affinity: tuple[int, ...] | None,
    backend_threads: int,
) -> pathlib.Path:
    report = out_dir / "benchmark_report.md"
    failures = sum(issue.status == "failure" for issue in issues)
    warnings = sum(issue.status == "warning" for issue in issues)
    lines = [
        "# KSpaceJet Numerics Benchmark Report",
        "",
        f"- Iterations: `{iterations}`",
        f"- Minimum sample time: `{min_sample_time_us} us`",
        f"- Trials: `{trials}`",
        f"- Independent processes: `{process_repetitions}`",
        f"- CPU affinity: `{'unavailable' if cpu_affinity is None else ','.join(map(str, cpu_affinity))}`",
        f"- Backend threads per process: `{backend_threads}`",
        f"- Gate mode: `{gate_config.mode}`",
        f"- Minimum meaningful speedup: `{gate_config.min_speedup_percent:.2f}%`",
        f"- Maximum policy gap: `{gate_config.max_policy_gap_percent:.2f}%`",
        f"- Maximum baseline regression: `{gate_config.max_regression_percent:.2f}%`",
        (
            "- Timing ranking uses median; confidence separation uses the 95% Student-t interval across independent "
            "process medians."
            if process_repetitions > 1
            else "- Timing ranking uses median; confidence separation uses the benchmark trial 95% Student-t interval."
        ),
        "- Performance comparisons are restricted to rows sharing an explicit comparison group and timing scope.",
        "",
        "## Gate Summary",
        "",
        f"- Failures: `{failures}`",
        f"- Warnings: `{warnings}`",
        "",
    ]
    if issues:
        lines.extend(
            [
                "| status | module | category | key | detail |",
                "| --- | --- | --- | --- | --- |",
            ]
        )
        lines.extend(
            f"| {issue.status} | {issue.module} | {issue.category} | {issue.key} | {issue.message} |"
            for issue in issues
        )
        lines.append("")
    else:
        lines.extend(["All enabled gates passed.", ""])

    lines.extend(
        [
            "## Meaningful Winners",
            "",
            "| module | key | backend | median ns | advantage | confidence | decision |",
            "| --- | --- | --- | ---: | ---: | --- | --- |",
        ]
    )
    for winner in winners:
        lines.append(
            f"| {winner.module} | {winner.key} | {winner.backend} | {winner.median_ns:.3f} | "
            f"{winner.speedup_percent:.2f}% | {'separated' if winner.confidence_separated else 'overlap'} | "
            f"{winner.status} |"
        )
    if not winners:
        lines.append("| - | No explicitly grouped comparisons were emitted. | - | - | - | - | - |")
    lines.append("")

    lines.extend(["## Observed Rows", ""])
    for name, path in csv_paths.items():
        rows = read_rows(path)
        lines.extend(
            [
                f"### {name}",
                "",
                f"- Sizes: `{sizes_by_name[name]}`",
                "",
                "| case | type | size | scope | lowest observed backend | median ns | 95% CI ns |",
                "| --- | --- | ---: | --- | --- | ---: | --- |",
            ]
        )
        for row in fastest_rows(rows):
            lines.append(
                f"| {row.get('case')} | {row.get('type')} | {row.get('size')} | {row.get('timing_scope')} | "
                f"{row.get('backend')} | {row_float(row, 'median_ns'):.3f} | "
                f"[{row_float(row, 'ci95_low_ns'):.3f}, {row_float(row, 'ci95_high_ns'):.3f}] |"
            )
        lines.append("")

    lines.extend(
        [
            "## Policy Guidance",
            "",
            "Every row has an explicit comparison group, timing scope, and role. Policy changes require the full "
            "production sweep, retained CSV/baseline, and an archived machine profile.",
            "",
        ]
    )
    report.write_text("\n".join(lines), encoding="utf-8")
    return report


def write_gate_json(
    out_dir: pathlib.Path,
    config: GateConfig,
    issues: list[GateIssue],
    winners: list[Winner],
    cpu_affinity: tuple[int, ...] | None,
    measurement_config: dict[str, int],
) -> pathlib.Path:
    path = out_dir / "benchmark_gate.json"
    payload = {
        "schema_version": 1,
        "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "config": asdict(config),
        "measurement_config": measurement_config,
        "cpu_affinity": list(cpu_affinity) if cpu_affinity is not None else None,
        "summary": {
            "failures": sum(issue.status == "failure" for issue in issues),
            "warnings": sum(issue.status == "warning" for issue in issues),
            "meaningful_winners": sum(winner.status == "winner" for winner in winners),
            "ties": sum(winner.status == "tie" for winner in winners),
        },
        "issues": [asdict(issue) for issue in issues],
        "winners": [asdict(winner) for winner in winners],
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path


def save_baseline(
    source_dir: pathlib.Path,
    destination: pathlib.Path,
    csv_paths: dict[str, pathlib.Path],
    gate_config: GateConfig,
    cpu_affinity: tuple[int, ...] | None,
    measurement_config: dict[str, int],
) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for path in csv_paths.values():
        shutil.copy2(path, destination / path.name)
    for name in ("benchmark_report.md", "benchmark_gate.json"):
        source = source_dir / name
        if source.exists():
            shutil.copy2(source, destination / name)
    manifest = {
        "schema_version": 1,
        "created_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "host": platform.node(),
        "platform": platform.platform(),
        "processor": platform.processor(),
        "cpu_affinity": list(cpu_affinity) if cpu_affinity is not None else None,
        "gate_config": asdict(gate_config),
        "measurement_config": measurement_config,
        "modules": sorted(csv_paths),
    }
    (destination / "baseline_manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def validate_baseline_compatibility(
    baseline_dir: pathlib.Path, measurement_config: dict[str, int], cpu_affinity: tuple[int, ...] | None
) -> None:
    manifest_path = baseline_dir / "baseline_manifest.json"
    if not manifest_path.exists():
        raise ValueError(f"baseline manifest is missing: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    baseline_config = manifest.get("measurement_config")
    if not isinstance(baseline_config, dict):
        raise ValueError(f"baseline measurement configuration is missing: {manifest_path}")
    mismatches = [key for key, value in measurement_config.items() if baseline_config.get(key) != value]
    baseline_affinity = manifest.get("cpu_affinity")
    current_affinity = list(cpu_affinity) if cpu_affinity is not None else None
    if baseline_affinity != current_affinity:
        mismatches.append("cpu_affinity")
    if mismatches:
        raise ValueError("baseline is incompatible with this measurement: " + ", ".join(sorted(mismatches)))
