from __future__ import annotations

import unittest

from helpers import TOOLS_DIRECTORY
from ksj_numerics_benchmark.cli import parse_module_sizes


assert TOOLS_DIRECTORY.is_dir()


class CliTests(unittest.TestCase):
    def test_module_sizes_are_module_scoped_and_normalized(self) -> None:
        self.assertEqual(
            {
                "linalg": "16,32,64,128,256,512,1024",
                "array": "256,1024,4096",
            },
            parse_module_sizes(
                [
                    "linalg=16, 32,64,128,256,512,1024",
                    "array=256,1024,4096",
                ]
            ),
        )

    def test_module_sizes_reject_invalid_or_duplicated_entries(self) -> None:
        with self.assertRaisesRegex(ValueError, "expected MODULE=A,B,C"):
            parse_module_sizes(["16,32"])
        with self.assertRaisesRegex(ValueError, "unknown module"):
            parse_module_sizes(["unknown=16,32"])
        with self.assertRaisesRegex(ValueError, "positive"):
            parse_module_sizes(["linalg=16,0"])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            parse_module_sizes(["linalg=16,32", "linalg=64,128"])
