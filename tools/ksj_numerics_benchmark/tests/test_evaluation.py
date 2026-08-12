from __future__ import annotations

import unittest

from helpers import gate_config, row
from ksj_numerics_benchmark.evaluation import (
    evaluate_correctness,
    evaluate_policy_and_winners,
    evaluate_regressions,
)


class EvaluationTests(unittest.TestCase):
    def test_correctness_uses_absolute_and_relative_tolerance(self) -> None:
        rows = [
            row("eigen", "reference", 100.0, 99.0, 101.0),
            row("intel", "candidate", 90.0, 89.0, 91.0, checksum=10.00015),
        ]
        self.assertEqual([], evaluate_correctness("array", rows, gate_config()))

        rows[1]["checksum"] = "10.01"
        issues = evaluate_correctness("array", rows, gate_config())
        self.assertEqual(1, len(issues))
        self.assertEqual("failure", issues[0].status)

    def test_winner_requires_gain_and_separated_confidence_intervals(self) -> None:
        rows = [
            row("eigen", "reference", 100.0, 98.0, 102.0),
            row("intel", "candidate", 80.0, 78.0, 82.0),
            row("public_policy", "policy", 82.0, 80.0, 84.0),
        ]
        rows[2]["selected_backend"] = "intel"
        issues, winners = evaluate_policy_and_winners("array", rows, gate_config())
        self.assertEqual([], issues)
        self.assertEqual("winner", winners[0].status)
        self.assertEqual("intel", winners[0].backend)

        rows[1]["ci95_high_ns"] = "101"
        _, winners = evaluate_policy_and_winners("array", rows, gate_config())
        self.assertEqual("tie", winners[0].status)

    def test_policy_gap_is_warning_in_smoke_and_failure_in_policy_mode(self) -> None:
        rows = [
            row("intel", "candidate", 80.0, 78.0, 82.0),
            row("eigen", "candidate", 100.0, 98.0, 102.0),
            row("public_policy", "policy", 100.0, 98.0, 102.0),
        ]
        rows[2]["selected_backend"] = "eigen"
        smoke_issues, _ = evaluate_policy_and_winners("sparse", rows, gate_config("smoke"))
        policy_issues, _ = evaluate_policy_and_winners("sparse", rows, gate_config("policy"))
        self.assertEqual("warning", smoke_issues[0].status)
        self.assertEqual("failure", policy_issues[0].status)

    def test_selected_backend_checks_dispatch_choice_not_wrapper_timing(self) -> None:
        rows = [
            row("eigen", "reference", 100.0, 98.0, 102.0),
            row("intel", "candidate", 80.0, 78.0, 82.0),
            row("public_policy", "policy", 120.0, 118.0, 122.0),
        ]
        rows[2]["selected_backend"] = "intel"
        issues, _ = evaluate_policy_and_winners("array", rows, gate_config())
        self.assertEqual([], issues)

        rows[2]["selected_backend"] = "eigen"
        issues, _ = evaluate_policy_and_winners("array", rows, gate_config())
        self.assertEqual(1, len(issues))
        self.assertIn("selected backend eigen", issues[0].message)

    def test_policy_row_requires_selected_backend(self) -> None:
        rows = [
            row("eigen", "reference", 100.0, 98.0, 102.0),
            row("public_policy", "policy", 100.0, 98.0, 102.0),
        ]
        issues, _ = evaluate_policy_and_winners("array", rows, gate_config())
        self.assertEqual(1, len(issues))
        self.assertIn("does not declare selected_backend", issues[0].message)

    def test_oracle_excludes_inaccurate_fast_candidate_from_policy_ranking(self) -> None:
        rows = [
            row("high_precision", "oracle", 0.0, 0.0, 0.0, checksum=10.0),
            row("fast_inaccurate", "reference", 50.0, 49.0, 51.0, checksum=12.0),
            row("accurate", "candidate", 100.0, 98.0, 102.0, checksum=10.0),
            row("accurate_slow", "candidate", 130.0, 128.0, 132.0, checksum=10.0),
            row("public_policy", "policy", 120.0, 118.0, 122.0, checksum=10.0),
        ]
        rows[4]["selected_backend"] = "accurate"
        issues, winners = evaluate_policy_and_winners("array", rows, gate_config())
        self.assertEqual([], issues)
        self.assertEqual("accurate", winners[0].backend)

    def test_regression_requires_threshold_and_confidence_separation(self) -> None:
        baseline = [row("intel", "candidate", 80.0, 78.0, 82.0)]
        current = [row("intel", "candidate", 100.0, 98.0, 102.0)]
        issues = evaluate_regressions("sparse", current, baseline, gate_config())
        self.assertEqual(1, len(issues))
        self.assertEqual("failure", issues[0].status)

        current[0]["ci95_low_ns"] = "81"
        self.assertEqual([], evaluate_regressions("sparse", current, baseline, gate_config()))
