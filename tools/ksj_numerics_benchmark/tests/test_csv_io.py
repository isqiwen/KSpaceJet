from __future__ import annotations

import csv
import pathlib
import tempfile
import unittest

from helpers import gate_config, row
from ksj_numerics_benchmark.csv_io import aggregate_csv_runs, observed_sizes, read_rows
from ksj_numerics_benchmark.evaluation import evaluate_policy_and_winners


class CsvIoTests(unittest.TestCase):
    def test_missing_required_metadata_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "old.csv"
            path.write_text(
                "case,backend,type,size,iterations,trials,mean_ns,min_ns,max_ns,checksum\n"
                "sum,eigen,float,16,3,5,10,9,11,42\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "missing required columns"):
                read_rows(path)

    def test_invalid_role_and_incomplete_policy_metadata_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "invalid.csv"
            rows = [row("eigen", "unsupported_role", 10.0, 9.0, 11.0)]
            with path.open("w", encoding="utf-8", newline="") as file:
                writer = csv.DictWriter(file, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            with self.assertRaisesRegex(ValueError, "invalid role"):
                read_rows(path)

            rows[0]["role"] = "policy"
            with path.open("w", encoding="utf-8", newline="") as file:
                writer = csv.DictWriter(file, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            with self.assertRaisesRegex(ValueError, "missing selected_backend"):
                read_rows(path)

    def test_observed_sizes_are_sorted_from_existing_csv(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "results.csv"
            rows = [
                row("eigen", "reference", 10.0, 9.0, 11.0),
                row("intel", "candidate", 8.0, 7.0, 9.0),
            ]
            rows[0]["size"] = "1024"
            rows[1]["size"] = "16"
            with path.open("w", encoding="utf-8", newline="") as file:
                writer = csv.DictWriter(file, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)

            self.assertEqual("16,1024", observed_sizes(path))

    def test_process_aggregation_uses_run_medians_and_exposes_instability(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            paths = []
            for run_index, candidate_median in enumerate((80.0, 120.0, 80.0), start=1):
                rows = [
                    row("candidate_a", "candidate", candidate_median, candidate_median - 1, candidate_median + 1),
                    row("candidate_b", "candidate", 100.0, 99.0, 101.0),
                    row("public_policy", "policy", 100.0, 99.0, 101.0),
                ]
                rows[2]["selected_backend"] = "candidate_b"
                path = root / f"run-{run_index}.csv"
                with path.open("w", encoding="utf-8", newline="") as file:
                    writer = csv.DictWriter(file, fieldnames=list(rows[0]))
                    writer.writeheader()
                    writer.writerows(rows)
                paths.append(path)

            output = root / "aggregate.csv"
            aggregate_csv_runs(paths, output)
            aggregated = read_rows(output)

        candidate_a = next(item for item in aggregated if item["backend"] == "candidate_a")
        self.assertEqual(80.0, float(candidate_a["median_ns"]))
        self.assertLess(float(candidate_a["ci95_low_ns"]), 100.0)
        self.assertGreater(float(candidate_a["ci95_high_ns"]), 100.0)
        self.assertEqual("3", candidate_a["process_repetitions"])
        issues, _ = evaluate_policy_and_winners("array", aggregated, gate_config())
        self.assertEqual([], issues)
