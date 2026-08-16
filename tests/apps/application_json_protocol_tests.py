#!/usr/bin/env python3
"""Verify that application JSON reports remain isolated from core diagnostics."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_json_failure(name: str, executable: pathlib.Path, arguments: list[str]) -> None:
    result = subprocess.run([str(executable), *arguments], check=False, capture_output=True, text=True)
    require(result.returncode != 0, f"{name} unexpectedly succeeded")
    try:
        report = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"{name} did not write one JSON error report to stdout: {result.stdout!r}; stderr={result.stderr!r}"
        ) from error
    require(isinstance(report, dict), f"{name} JSON report is not an object")
    require(result.stderr, f"{name} did not initialize the core diagnostic logger on stderr")


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: application_json_protocol_tests.py <ksj> <ksj-gateway> <ksj-recon> <ksj-research>", file=sys.stderr)
        return 2

    ksj, gateway, recon, research = (pathlib.Path(argument) for argument in sys.argv[1:])
    for executable in (ksj, gateway, recon, research):
        require(executable.is_file(), f"application executable does not exist: {executable}")

    run_json_failure(
        "ksj",
        ksj,
        ["provider", "init", "bad--slug", "image_filter", "--output", ".", "--format", "json"],
    )
    run_json_failure("ksj-gateway", gateway, ["--config", "gateway.json", "--format", "json"])
    run_json_failure("ksj-recon", recon, ["cartesian-rss", "--format", "json"])
    run_json_failure("ksj-research", research, ["run", "--format", "json"])
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"application JSON protocol test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
