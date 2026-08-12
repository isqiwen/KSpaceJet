#!/usr/bin/env python3
"""Stable command-line entry point for the KSpaceJet numerics benchmark suite."""

from __future__ import annotations

import pathlib
import sys


if __package__ in {None, ""}:
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from ksj_numerics_benchmark.cli import main


if __name__ == "__main__":
    sys.exit(main())
