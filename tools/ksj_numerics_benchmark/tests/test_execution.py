from __future__ import annotations

import unittest
from unittest import mock

from helpers import TOOLS_DIRECTORY
from ksj_numerics_benchmark.execution import (
    BACKEND_THREAD_ENVIRONMENT_NAMES,
    DEFAULT_WAIT_ENVIRONMENT,
    benchmark_environment,
    default_sizes_for,
    parse_cpu_list,
    rotate_sizes,
)
from ksj_numerics_benchmark.models import BENCHMARKS, LINALG_DEFAULT_SIZES, NUFFT_DEFAULT_SIZES


assert TOOLS_DIRECTORY.is_dir()


class ExecutionTests(unittest.TestCase):
    def test_cpu_affinity_parser_accepts_lists_and_ranges(self) -> None:
        self.assertEqual({0, 2, 3, 4}, parse_cpu_list("0,2-4"))

    def test_cpu_affinity_parser_rejects_invalid_ranges(self) -> None:
        with self.assertRaises(ValueError):
            parse_cpu_list("4-2")
        with self.assertRaises(ValueError):
            parse_cpu_list("")

    def test_backend_thread_environment_forces_single_threaded_backends(self) -> None:
        environment = benchmark_environment(1)
        for name in BACKEND_THREAD_ENVIRONMENT_NAMES:
            self.assertEqual("1", environment[name])
        self.assertEqual("FALSE", environment["OMP_DYNAMIC"])
        self.assertEqual("FALSE", environment["MKL_DYNAMIC"])

    def test_wait_defaults_follow_shell_defaulting_semantics(self) -> None:
        with mock.patch.dict("os.environ", {name: "" for name in DEFAULT_WAIT_ENVIRONMENT}):
            environment = benchmark_environment(1)
        self.assertEqual(DEFAULT_WAIT_ENVIRONMENT, {name: environment[name] for name in DEFAULT_WAIT_ENVIRONMENT})

        overrides = {
            "OMP_WAIT_POLICY": "ACTIVE",
            "KMP_BLOCKTIME": "25",
            "MKL_THREADING_LAYER": "INTEL",
        }
        with mock.patch.dict("os.environ", overrides):
            environment = benchmark_environment(1)
        self.assertEqual(overrides, {name: environment[name] for name in overrides})

    def test_size_rotation_changes_process_context_without_changing_coverage(self) -> None:
        self.assertEqual("16,32,64", rotate_sizes("16,32,64", 0))
        self.assertEqual("32,64,16", rotate_sizes("16,32,64", 1))
        self.assertEqual("64,16,32", rotate_sizes("16,32,64", 2))

    def test_linalg_default_size_does_not_exceed_matrix_order_1024(self) -> None:
        self.assertEqual("16,32,64,128,256,512,1024", LINALG_DEFAULT_SIZES)

    def test_nufft_default_sizes_use_grid_side_lengths(self) -> None:
        self.assertEqual("4,8,16,32,64", NUFFT_DEFAULT_SIZES)
        self.assertEqual(NUFFT_DEFAULT_SIZES, default_sizes_for(BENCHMARKS["nufft"]))
