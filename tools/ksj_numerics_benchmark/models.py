from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class BenchmarkSpec:
    name: str
    executable: str
    output: str


@dataclass(frozen=True)
class GateConfig:
    mode: str
    min_speedup_percent: float
    max_policy_gap_percent: float
    max_regression_percent: float
    float_abs_tolerance: float
    float_rel_tolerance: float
    double_abs_tolerance: float
    double_rel_tolerance: float


@dataclass(frozen=True)
class GateIssue:
    module: str
    category: str
    status: str
    key: str
    message: str


@dataclass(frozen=True)
class Winner:
    module: str
    key: str
    backend: str
    median_ns: float
    speedup_percent: float
    confidence_separated: bool
    status: str


BENCHMARKS = {
    "array": BenchmarkSpec("array", "ksj_array_backend_benchmark", "array.csv"),
    "linalg": BenchmarkSpec("linalg", "ksj_linalg_backend_benchmark", "linalg.csv"),
    "fft": BenchmarkSpec("fft", "ksj_fft_backend_benchmark", "fft.csv"),
    "signal": BenchmarkSpec("signal", "ksj_signal_backend_benchmark", "signal.csv"),
    "image": BenchmarkSpec("image", "ksj_image_backend_benchmark", "image.csv"),
    "stats": BenchmarkSpec("stats", "ksj_stats_backend_benchmark", "stats.csv"),
    "optimization": BenchmarkSpec("optimization", "ksj_optimization_backend_benchmark", "optimization.csv"),
    "sparse": BenchmarkSpec("sparse", "ksj_sparse_backend_benchmark", "sparse.csv"),
    "special": BenchmarkSpec("special", "ksj_special_backend_benchmark", "special.csv"),
    "nufft": BenchmarkSpec("nufft", "ksj_nufft_backend_benchmark", "nufft.csv"),
}

DEFAULT_SIZES = "16,32,64,128,256,512,1024,2048"
LINALG_DEFAULT_SIZES = "16,32,64,128,256,512,1024"
ARRAY_DEFAULT_SIZES = "256,1024,4096,16384,65536,262144,1048576"
NUFFT_DEFAULT_SIZES = "4,8,16,32,64"
