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


def parse_json_stdout(name: str, result: subprocess.CompletedProcess[str]) -> dict[str, object]:
    try:
        report = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"{name} did not write one JSON report to stdout: {result.stdout!r}; stderr={result.stderr!r}"
        ) from error
    require(isinstance(report, dict), f"{name} JSON report is not an object")
    require(result.stderr, f"{name} did not initialize the core diagnostic logger on stderr")
    return report


def run_json_failure(
    name: str,
    executable: pathlib.Path,
    arguments: list[str],
    expected_code: str | None = None,
) -> None:
    result = subprocess.run([str(executable), *arguments], check=False, capture_output=True, text=True)
    require(result.returncode != 0, f"{name} unexpectedly succeeded")
    report = parse_json_stdout(name, result)
    if expected_code is not None:
        require(report.get("schema") == "ksj.error", f"{name} did not write an error report")
        require(report.get("code") == expected_code, f"{name} wrote the wrong error code: {report!r}")


def run_scaffold_help(name: str, executable: pathlib.Path) -> None:
    json_result = subprocess.run(
        [str(executable), "--help", "--format", "json"], check=False, capture_output=True, text=True
    )
    require(json_result.returncode == 0, f"{name} JSON help failed: {json_result.stderr}")
    report = parse_json_stdout(name, json_result)
    require(report.get("schema") == "ksj.program-help", f"{name} did not write a help report")
    require(report.get("status") == "scaffold", f"{name} did not identify itself as a scaffold")
    require(report.get("availability") == "reserved", f"{name} did not identify its commands as reserved")
    require(report.get("operations") == "unimplemented", f"{name} did not identify operations as unimplemented")

    text_result = subprocess.run([str(executable), "--help"], check=False, capture_output=True, text=True)
    require(text_result.returncode == 0, f"{name} text help failed: {text_result.stderr}")
    require(text_result.stderr, f"{name} did not initialize the core diagnostic logger on stderr")
    text_help = text_result.stdout.lower()
    for marker in ("scaffold", "reserved", "unimplemented"):
        require(marker in text_help, f"{name} text help does not identify itself as {marker}")

    help_text = json.dumps(report).lower() + text_help
    for prohibited in ("external-system", "authentication", "connector", "routing", "session", "mrd", "scanner", "runner"):
        require(prohibited not in help_text, f"{name} help still claims {prohibited!r} capability")


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
    run_scaffold_help("ksj-gateway", gateway)
    run_json_failure(
        "ksj-gateway", gateway, ["--config", "reserved.json", "--format", "json"], "unimplemented"
    )
    run_json_failure("ksj-recon", recon, ["cartesian-rss", "--format", "json"])
    run_scaffold_help("ksj-research", research)
    run_json_failure("ksj-research", research, ["run", "--format", "json"], "unimplemented")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"application JSON protocol test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
