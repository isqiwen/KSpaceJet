from __future__ import annotations

import os
import pathlib
import subprocess
import sys
from typing import Iterable

from .csv_io import aggregate_csv_runs
from .models import ARRAY_DEFAULT_SIZES, DEFAULT_SIZES, LINALG_DEFAULT_SIZES, NUFFT_DEFAULT_SIZES, BenchmarkSpec


BACKEND_THREAD_ENVIRONMENT_NAMES = (
    "OMP_NUM_THREADS",
    "MKL_NUM_THREADS",
    "OPENBLAS_NUM_THREADS",
    "BLIS_NUM_THREADS",
    "VECLIB_MAXIMUM_THREADS",
    "NUMEXPR_NUM_THREADS",
    "TBB_NUM_THREADS",
    "OPENCV_FOR_THREADS_NUM",
)

DEFAULT_WAIT_ENVIRONMENT = {
    "OMP_WAIT_POLICY": "PASSIVE",
    "KMP_BLOCKTIME": "0",
    "MKL_THREADING_LAYER": "SEQUENTIAL",
}


def parse_cpu_list(value: str) -> set[int]:
    cpus: set[int] = set()
    for part in value.split(","):
        token = part.strip()
        if not token:
            raise ValueError(f"invalid CPU list: {value}")
        if "-" in token:
            first_text, last_text = token.split("-", maxsplit=1)
            first = int(first_text)
            last = int(last_text)
            if first < 0 or last < first:
                raise ValueError(f"invalid CPU range: {token}")
            cpus.update(range(first, last + 1))
        else:
            cpu = int(token)
            if cpu < 0:
                raise ValueError(f"invalid CPU index: {token}")
            cpus.add(cpu)
    if not cpus:
        raise ValueError("CPU affinity must not be empty")
    return cpus


def configure_cpu_affinity(value: str | None) -> tuple[int, ...] | None:
    if value is not None:
        if not hasattr(os, "sched_setaffinity"):
            raise RuntimeError("--cpu-affinity is not supported on this platform")
        os.sched_setaffinity(0, parse_cpu_list(value))
    if hasattr(os, "sched_getaffinity"):
        return tuple(sorted(os.sched_getaffinity(0)))
    return None


def benchmark_environment(backend_threads: int) -> dict[str, str]:
    if backend_threads <= 0:
        raise ValueError("--backend-threads must be positive")
    environment = os.environ.copy()
    thread_count = str(backend_threads)
    environment.update({name: thread_count for name in BACKEND_THREAD_ENVIRONMENT_NAMES})
    environment["OMP_DYNAMIC"] = "FALSE"
    environment["MKL_DYNAMIC"] = "FALSE"
    for name, default in DEFAULT_WAIT_ENVIRONMENT.items():
        if not environment.get(name):
            environment[name] = default
    return environment


def selected_specs(names: Iterable[str] | None, available: dict[str, BenchmarkSpec]) -> list[BenchmarkSpec]:
    if not names:
        return list(available.values())
    return [available[name] for name in names]


def rotate_sizes(sizes: str, repetition: int) -> str:
    values = [value.strip() for value in sizes.split(",") if value.strip()]
    if not values:
        raise ValueError("benchmark size list must not be empty")
    offset = repetition % len(values)
    return ",".join(values[offset:] + values[:offset])


def default_sizes_for(spec: BenchmarkSpec) -> str:
    if spec.name == "array":
        return ARRAY_DEFAULT_SIZES
    if spec.name == "linalg":
        return LINALG_DEFAULT_SIZES
    if spec.name == "nufft":
        return NUFFT_DEFAULT_SIZES
    return DEFAULT_SIZES


def run_one(
    spec: BenchmarkSpec,
    bin_dir: pathlib.Path,
    out_dir: pathlib.Path,
    iterations: int,
    trials: int,
    min_sample_time_us: int,
    sizes: str,
    process_repetitions: int,
    backend_threads: int,
) -> pathlib.Path:
    executable = bin_dir / spec.executable
    if not executable.exists():
        raise FileNotFoundError(f"missing benchmark executable: {executable}")
    if not os.access(executable, os.X_OK):
        raise PermissionError(f"benchmark executable is not executable: {executable}")

    raw_dir = out_dir / "raw" / spec.name
    raw_dir.mkdir(parents=True, exist_ok=True)
    raw_paths: list[pathlib.Path] = []
    for repetition in range(process_repetitions):
        raw_output = raw_dir / f"run-{repetition + 1}.csv"
        command = [
            str(executable),
            "--iterations",
            str(iterations),
            "--trials",
            str(trials),
            "--min-sample-time-us",
            str(min_sample_time_us),
            "--sizes",
            rotate_sizes(sizes, repetition),
            "--csv",
        ]
        with raw_output.open("w", encoding="utf-8") as file:
            subprocess.run(
                command,
                check=True,
                stdout=file,
                stderr=sys.stderr,
                text=True,
                env=benchmark_environment(backend_threads),
            )
        raw_paths.append(raw_output)

    output = out_dir / spec.output
    aggregate_csv_runs(raw_paths, output)
    return output
