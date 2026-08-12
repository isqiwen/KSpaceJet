from __future__ import annotations

import pathlib
import sys


TOOLS_DIRECTORY = pathlib.Path(__file__).parents[2]
if str(TOOLS_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIRECTORY))

from ksj_numerics_benchmark.models import GateConfig


def gate_config(mode: str = "policy") -> GateConfig:
    return GateConfig(
        mode=mode,
        min_speedup_percent=5.0,
        max_policy_gap_percent=5.0,
        max_regression_percent=10.0,
        float_abs_tolerance=1.0e-4,
        float_rel_tolerance=1.0e-5,
        double_abs_tolerance=1.0e-10,
        double_rel_tolerance=1.0e-10,
    )


def row(
    backend: str,
    role: str,
    median: float,
    ci_low: float,
    ci_high: float,
    checksum: float = 10.0,
) -> dict[str, str]:
    return {
        "case": "operation",
        "comparison_group": "operation/output",
        "backend": backend,
        "role": role,
        "selected_backend": "",
        "timing_scope": "output_reuse",
        "type": "float",
        "size": "1024",
        "iterations": "50",
        "trials": "5",
        "mean_ns": str(median),
        "median_ns": str(median),
        "stddev_ns": "1",
        "ci95_low_ns": str(ci_low),
        "ci95_high_ns": str(ci_high),
        "min_ns": str(ci_low),
        "max_ns": str(ci_high),
        "checksum": str(checksum),
        "abs_tolerance": "-1",
        "rel_tolerance": "-1",
    }
