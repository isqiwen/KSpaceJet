#!/usr/bin/env python3
"""Verify that application JSON reports remain isolated from core diagnostics."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_core_log(name: str, result: subprocess.CompletedProcess[str], level: str) -> None:
    require(
        f"[{level}]" in result.stderr,
        f"{name} did not record a core {level} diagnostic on stderr: {result.stderr!r}",
    )


def parse_json_stdout(name: str, result: subprocess.CompletedProcess[str]) -> dict[str, object]:
    try:
        report = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"{name} did not write one JSON report to stdout: {result.stdout!r}; stderr={result.stderr!r}"
        ) from error
    require(isinstance(report, dict), f"{name} JSON report is not an object")
    require(result.stderr, f"{name} did not initialize the core diagnostic logger on stderr")
    require_core_log(name, result, "INFO")
    return report


def run_json_failure(
    name: str,
    executable: pathlib.Path,
    arguments: list[str],
    expected_code: str | None = None,
) -> None:
    result = subprocess.run(
        [str(executable), *arguments], check=False, capture_output=True, text=True, encoding="utf-8"
    )
    require(result.returncode != 0, f"{name} unexpectedly succeeded")
    report = parse_json_stdout(name, result)
    require_core_log(name, result, "WARN")
    if expected_code is not None:
        require(report.get("schema") == "ksj.error", f"{name} did not write an error report")
        require(report.get("code") == expected_code, f"{name} wrote the wrong error code: {report!r}")


def run_scaffold_help(name: str, executable: pathlib.Path) -> None:
    json_result = subprocess.run(
        [str(executable), "--help", "--format", "json"], check=False, capture_output=True, text=True, encoding="utf-8"
    )
    require(json_result.returncode == 0, f"{name} JSON help failed: {json_result.stderr}")
    report = parse_json_stdout(name, json_result)
    require(report.get("schema") == "ksj.program-help", f"{name} did not write a help report")
    require(report.get("status") == "scaffold", f"{name} did not identify itself as a scaffold")
    require(report.get("availability") == "reserved", f"{name} did not identify its commands as reserved")
    require(report.get("operations") == "unimplemented", f"{name} did not identify operations as unimplemented")

    text_result = subprocess.run(
        [str(executable), "--help"], check=False, capture_output=True, text=True, encoding="utf-8"
    )
    require(text_result.returncode == 0, f"{name} text help failed: {text_result.stderr}")
    require(text_result.stderr, f"{name} did not initialize the core diagnostic logger on stderr")
    text_help = text_result.stdout.lower()
    for marker in ("scaffold", "reserved", "unimplemented"):
        require(marker in text_help, f"{name} text help does not identify itself as {marker}")

    help_text = json.dumps(report).lower() + text_help
    for prohibited in ("external-system", "authentication", "connector", "routing", "session", "mrd", "scanner", "runner"):
        require(prohibited not in help_text, f"{name} help still claims {prohibited!r} capability")


def run_recon_image_help(recon: pathlib.Path, command: str) -> None:
    result = subprocess.run(
        [str(recon), command, "--format", "json", "--help"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    require(result.returncode == 0, f"ksj-recon {command} JSON help failed: {result.stderr}")
    report = parse_json_stdout(f"ksj-recon {command} help", result)
    require(report.get("schema") == f"kspacejet.{command}-help", f"ksj-recon {command} help schema drifted")
    usage = report.get("usage")
    require(isinstance(usage, str) and "<image.mrd>" in usage, f"{command} help omits the MRD output")
    require("--metadata" not in usage, f"{command} help still exposes a JSON image sidecar")
    if command == "radial-rss":
        require("encoded-matrix-index" in usage, "radial help omits an explicit unit")


def run_recon_removed_metadata_failure(recon: pathlib.Path) -> None:
    result = subprocess.run(
        [str(recon), "cartesian-rss", "--format", "json", "--metadata", "legacy.json"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    require(result.returncode == 2, f"ksj-recon legacy metadata option returned {result.returncode}, expected 2")
    report = parse_json_stdout("ksj-recon legacy metadata option", result)
    require_core_log("ksj-recon legacy metadata option", result, "WARN")
    require(
        report.get("schema") == "kspacejet.cartesian-rss-result",
        f"ksj-recon legacy metadata option schema drifted: {report!r}",
    )
    require(report.get("ok") is False, f"ksj-recon legacy metadata option did not report failure: {report!r}")
    require(
        report.get("code") == "invalid_argument",
        f"ksj-recon legacy metadata option wrote the wrong code: {report!r}",
    )
    message = report.get("message")
    require(isinstance(message, str) and "metadata" in message, f"ksj-recon error omits the rejected option: {report!r}")


def run_viewer_protocol(viewer: pathlib.Path) -> None:
    # The real QApplication smoke belongs to the Windows install check, where
    # qwindows.dll is part of the product contract. This generic protocol
    # test intentionally remains display-server independent.
    json_result = subprocess.run(
        [str(viewer), "--help", "--format", "json"], check=False, capture_output=True, text=True, encoding="utf-8"
    )
    require(json_result.returncode == 0, f"ksj-viewer JSON help failed: {json_result.stderr}")
    report = parse_json_stdout("ksj-viewer help", json_result)
    require(report.get("schema") == "ksj.program-help", f"ksj-viewer help schema drifted: {report!r}")
    require(report.get("program") == "ksj-viewer", f"ksj-viewer help program drifted: {report!r}")
    require(report.get("status") == "inspection", f"ksj-viewer did not disclose its inspection status: {report!r}")
    require(report.get("availability") == "desktop", f"ksj-viewer availability drifted: {report!r}")
    require(
        report.get("operations") == "metadata,k-space,image,pipeline,visualization-derivative-export",
        f"ksj-viewer operation surface drifted: {report!r}",
    )

    invalid_result = subprocess.run(
        [str(viewer), "--ui-smoke", "--export-smoke", "--format", "json"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    require(
        invalid_result.returncode == 2,
        f"ksj-viewer mutually exclusive smoke flags returned {invalid_result.returncode}, expected 2",
    )
    invalid_report = parse_json_stdout("ksj-viewer mutually exclusive smoke flags", invalid_result)
    require(invalid_report.get("schema") == "ksj.error", f"ksj-viewer error schema drifted: {invalid_report!r}")
    require(
        invalid_report.get("code") == "invalid_argument",
        f"ksj-viewer error code drifted: {invalid_report!r}",
    )
    require_core_log("ksj-viewer mutually exclusive smoke flags", invalid_result, "WARN")


def run_pipeline_validation_protocol(ksj: pathlib.Path) -> None:
    project_root = pathlib.Path(__file__).resolve().parents[2]
    valid_pipeline = project_root / "tests" / "unit" / "libs" / "recon" / "fixtures" / "valid" / "pipeline-minimal.json"
    invalid_pipeline = (
        project_root / "tests" / "unit" / "libs" / "recon" / "fixtures" / "invalid" / "pipeline-input-path.json"
    )
    require(valid_pipeline.is_file(), f"pipeline validation fixture is missing: {valid_pipeline}")
    require(invalid_pipeline.is_file(), f"pipeline validation fixture is missing: {invalid_pipeline}")

    valid_result = subprocess.run(
        [str(ksj), "pipeline", "validate", str(valid_pipeline), "--format", "json"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    require(valid_result.returncode == 0, f"ksj pipeline validate rejected a valid pipeline: {valid_result.stderr}")
    valid_report = parse_json_stdout("ksj pipeline validate valid", valid_result)
    require(valid_report.get("schema") == "kspacejet.pipeline-validation-report", "pipeline validation schema drifted")
    require(valid_report.get("valid") is True, f"valid pipeline did not report success: {valid_report!r}")
    profile = valid_report.get("input_profile")
    require(
        profile == {"kind": "ismrmrd-hdf5", "container": {"mode": "auto"}},
        f"pipeline validation omitted the ISMRMRD input profile: {valid_report!r}",
    )
    counts = valid_report.get("counts")
    require(isinstance(counts, dict) and counts.get("parameters") == 0, f"pipeline parameter count drifted: {valid_report!r}")

    valid_text_result = subprocess.run(
        [str(ksj), "pipeline", "validate", str(valid_pipeline), "--format", "text"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    require(valid_text_result.returncode == 0, f"ksj text pipeline validation rejected a valid pipeline: {valid_text_result.stderr}")
    require(
        "input profile: ismrmrd-hdf5 (container: auto)" in valid_text_result.stdout,
        f"ksj text pipeline validation omitted the automatic container selector: {valid_text_result.stdout!r}",
    )

    explicit_document = json.loads(valid_pipeline.read_text(encoding="utf-8"))
    explicit_document["input_profile"]["container"] = {"mode": "explicit", "path": "/dataset_2"}
    legacy_document = json.loads(valid_pipeline.read_text(encoding="utf-8"))
    legacy_document["input_profile"] = {"kind": "ismrmrd-hdf5", "dataset_group": "dataset"}
    with tempfile.TemporaryDirectory() as temporary_directory:
        explicit_pipeline = pathlib.Path(temporary_directory) / "pipeline-explicit-container.json"
        explicit_pipeline.write_text(json.dumps(explicit_document), encoding="utf-8")
        explicit_result = subprocess.run(
            [str(ksj), "pipeline", "validate", str(explicit_pipeline), "--format", "json"],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        legacy_pipeline = pathlib.Path(temporary_directory) / "pipeline-legacy-dataset-group.json"
        legacy_pipeline.write_text(json.dumps(legacy_document), encoding="utf-8")
        legacy_result = subprocess.run(
            [str(ksj), "pipeline", "validate", str(legacy_pipeline), "--format", "json"],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
    require(
        explicit_result.returncode == 0,
        f"ksj pipeline validation rejected an explicit container selector: {explicit_result.stderr}",
    )
    explicit_report = parse_json_stdout("ksj pipeline validate explicit container", explicit_result)
    require(
        explicit_report.get("input_profile")
        == {"kind": "ismrmrd-hdf5", "container": {"mode": "explicit", "path": "/dataset_2"}},
        f"pipeline validation did not preserve the explicit container selector: {explicit_report!r}",
    )
    require(legacy_result.returncode == 3, f"legacy pipeline returned {legacy_result.returncode}, expected 3")
    legacy_report = parse_json_stdout("ksj pipeline validate legacy dataset group", legacy_result)
    require_core_log("ksj pipeline validate legacy dataset group", legacy_result, "WARN")
    require(legacy_report.get("valid") is False, f"legacy pipeline unexpectedly passed: {legacy_report!r}")
    legacy_diagnostics = legacy_report.get("diagnostics")
    require(
        isinstance(legacy_diagnostics, list)
        and legacy_diagnostics
        and "dataset_group" in str(legacy_diagnostics[0].get("message")),
        f"legacy field rejection lost its diagnostic: {legacy_report!r}",
    )

    invalid_result = subprocess.run(
        [str(ksj), "pipeline", "validate", str(invalid_pipeline), "--format", "json"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    require(invalid_result.returncode == 3, f"invalid pipeline returned {invalid_result.returncode}, expected 3")
    invalid_report = parse_json_stdout("ksj pipeline validate invalid", invalid_result)
    require_core_log("ksj pipeline validate invalid", invalid_result, "WARN")
    require(invalid_report.get("schema") == "kspacejet.pipeline-validation-report", "invalid pipeline schema drifted")
    require(invalid_report.get("valid") is False, f"invalid pipeline unexpectedly reported success: {invalid_report!r}")
    diagnostics = invalid_report.get("diagnostics")
    require(isinstance(diagnostics, list) and diagnostics, f"invalid pipeline omitted diagnostics: {invalid_report!r}")
    require(diagnostics[0].get("code") == "validation_error", f"invalid pipeline used the wrong diagnostic: {invalid_report!r}")


def main() -> int:
    if len(sys.argv) != 6:
        print(
            "usage: application_json_protocol_tests.py <ksj> <ksj-gateway> <ksj-recon> <ksj-research> <ksj-viewer>",
            file=sys.stderr,
        )
        return 2

    ksj, gateway, recon, research, viewer = (pathlib.Path(argument) for argument in sys.argv[1:])
    for executable in (ksj, gateway, recon, research, viewer):
        require(executable.is_file(), f"application executable does not exist: {executable}")

    run_json_failure(
        "ksj",
        ksj,
        ["provider", "init", "bad--slug", "image_filter", "--output", ".", "--format", "json"],
    )
    run_pipeline_validation_protocol(ksj)
    run_scaffold_help("ksj-gateway", gateway)
    run_json_failure(
        "ksj-gateway", gateway, ["--config", "reserved.json", "--format", "json"], "unimplemented"
    )
    run_json_failure("ksj-recon", recon, ["cartesian-rss", "--format", "json"])
    run_json_failure("ksj-recon", recon, ["radial-rss", "--format", "json"])
    for command in ("cartesian-rss", "noncartesian-rss", "radial-rss"):
        run_recon_image_help(recon, command)
    run_recon_removed_metadata_failure(recon)
    run_scaffold_help("ksj-research", research)
    run_json_failure("ksj-research", research, ["run", "--format", "json"], "unimplemented")
    run_viewer_protocol(viewer)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"application JSON protocol test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
