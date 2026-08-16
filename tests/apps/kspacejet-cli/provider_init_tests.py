#!/usr/bin/env python3
"""End-to-end checks for `ksj provider init`.

The command must create a complete Provider skeleton once, retain the typed-port
placeholders that require author input, and never overwrite an existing target.
"""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def parse_report(result: subprocess.CompletedProcess[str], stream: str) -> dict[str, object]:
    payload = result.stdout if stream == "stdout" else result.stderr
    try:
        return json.loads(payload)
    except json.JSONDecodeError as error:
        raise AssertionError(f"expected a JSON report on {stream}, got: {payload!r}") from error


def test_provider_init(executable: pathlib.Path) -> None:
    with tempfile.TemporaryDirectory(prefix="ksj-provider-init-") as temporary_directory:
        output_parent = pathlib.Path(temporary_directory)
        help_result = run([str(executable), "provider", "init", "--help", "--format", "json"])
        require(help_result.returncode == 0, f"provider init help failed: {help_result.stderr}")
        help_report = parse_report(help_result, "stdout")
        require(help_report.get("schema") == "ksj.program-help", "nested help did not honor --format json")
        require(help_report.get("command") == "provider init", "nested help named the wrong command")

        command = [
            str(executable),
            "provider",
            "init",
            "example-filter",
            "image_filter",
            "--output",
            str(output_parent),
            "--format",
            "json",
        ]

        created = run(command)
        require(created.returncode == 0, f"provider init failed: {created.stderr}")
        report = parse_report(created, "stdout")
        require(report.get("schema") == "kspacejet.provider-init-report", "success report has the wrong schema")
        require(report.get("created") is True, "success report does not say created=true")

        provider_directory = output_parent / "kspacejet-example-filter"
        expected_files = {
            "CMakeLists.txt",
            "README.md",
            "contracts/image_filter.json",
            "src/provider_entry.cpp",
            "src/provider_api.hpp",
            "src/provider_api.cpp",
            "src/provider_state.hpp",
            "src/operators/image_filter.hpp",
            "src/operators/image_filter.cpp",
        }
        actual_files = {
            path.relative_to(provider_directory).as_posix()
            for path in provider_directory.rglob("*")
            if path.is_file()
        }
        require(expected_files.issubset(actual_files), f"scaffold is missing files: {expected_files - actual_files}")
        require(not any(path.endswith(".in") for path in actual_files), "scaffold retained a template .in suffix")

        contract = (provider_directory / "contracts/image_filter.json").read_text(encoding="utf-8")
        require('"operator_id": "image_filter"' in contract, "contract did not receive the Operator id")
        for placeholder in ("@INPUT_PORT@", "@INPUT_TYPE_REF@", "@OUTPUT_PORT@", "@OUTPUT_TYPE_REF@"):
            require(placeholder in contract, f"contract unexpectedly replaced {placeholder}")

        cmake = (provider_directory / "CMakeLists.txt").read_text(encoding="utf-8")
        require("ksj_example_filter_provider" in cmake, "CMake target did not receive the Provider target name")
        require("ksj-example-filter" in cmake, "CMake output name did not receive the Provider slug")
        require("KSpaceJet::core" in cmake, "generated Provider does not link the core logging module")

        provider_api = (provider_directory / "src/provider_api.hpp").read_text(encoding="utf-8")
        require("namespace ksj::example_filter::api" in provider_api, "Provider API namespace was not materialized")
        require("provider_template" not in provider_api, "generated source retained the template namespace")
        operator_header = (provider_directory / "src/operators/image_filter.hpp").read_text(encoding="utf-8")
        require(
            "namespace ksj::example_filter::operators" in operator_header,
            "Operator declaration did not receive the Provider namespace",
        )
        require("@PROVIDER_TARGET@" not in operator_header, "Provider target placeholder was not substituted")

        sentinel = provider_directory / "do-not-overwrite.txt"
        sentinel.write_text("preserve me", encoding="utf-8")
        collision = run(command)
        require(collision.returncode == 2, f"existing destination did not return usage failure: {collision.stderr}")
        collision_report = parse_report(collision, "stdout")
        require(collision_report.get("created") is False, "collision report does not say created=false")
        require(sentinel.read_text(encoding="utf-8") == "preserve me", "existing Provider was modified")

        traversal = run(
            [
                str(executable),
                "provider",
                "init",
                "../escape",
                "image_filter",
                "--output",
                str(output_parent),
                "--format",
                "json",
            ]
        )
        require(traversal.returncode == 2, f"traversal name was accepted: {traversal.stderr}")
        traversal_report = parse_report(traversal, "stdout")
        require(traversal_report.get("created") is False, "traversal report does not say created=false")
        require(not (output_parent / "escape").exists(), "traversal created an unexpected directory")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: provider_init_tests.py <ksj-executable>", file=sys.stderr)
        return 2

    executable = pathlib.Path(sys.argv[1])
    require(executable.is_file(), f"ksj executable does not exist: {executable}")
    test_provider_init(executable)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"provider init test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
