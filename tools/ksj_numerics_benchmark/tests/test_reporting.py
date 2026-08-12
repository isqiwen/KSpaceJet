from __future__ import annotations

import json
import pathlib
import tempfile
import unittest

from helpers import TOOLS_DIRECTORY
from ksj_numerics_benchmark.reporting import validate_baseline_compatibility


assert TOOLS_DIRECTORY.is_dir()


class ReportingTests(unittest.TestCase):
    def test_baseline_requires_matching_measurement_configuration(self) -> None:
        measurement_config = {
            "minimum_iterations": 5,
            "trials": 5,
            "minimum_sample_time_us": 1000,
            "process_repetitions": 3,
            "backend_threads": 1,
        }
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "baseline_manifest.json").write_text(
                json.dumps({"measurement_config": measurement_config, "cpu_affinity": [0]}), encoding="utf-8"
            )
            validate_baseline_compatibility(root, measurement_config, (0,))

            incompatible = dict(measurement_config)
            incompatible["backend_threads"] = 2
            with self.assertRaisesRegex(ValueError, "backend_threads"):
                validate_baseline_compatibility(root, incompatible, (0,))
