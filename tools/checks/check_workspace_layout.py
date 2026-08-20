#!/usr/bin/env python3
"""Validate KSpaceJet's paired source-and-data repository workspace.

KSpaceJet deliberately contains code, contracts, and small fixtures only.  Raw
ISMRMRD payloads live in the sibling ``KSpaceJet-ismrmrd-data`` repository.
This standard-library-only checker makes that local development contract
explicit without fetching data or running the data repository's verifier.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


DATA_REPOSITORY_NAME = "KSpaceJet-ismrmrd-data"
DATA_REPOSITORY_PATH = "isqiwen/KSpaceJet-ismrmrd-data"
REQUIRED_DATA_PATHS = (
    Path("catalog.yaml"),
    Path("datasets"),
    Path("tools/verify-data.sh"),
)
RAW_MRI_SUFFIXES = frozenset({".mrd", ".h5", ".hdf5", ".ismrmrd"})
EXCLUDED_DIRECTORY_NAMES = frozenset({".git", "out", "build", ".venv", ".kspacejet"})
EXPECTED_ORIGIN_PATTERN = re.compile(
    rf"(?:git@github[.]com:|ssh://git@github[.]com/|https://github[.]com/)"
    rf"{re.escape(DATA_REPOSITORY_PATH)}(?:[.]git)?/?",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class Diagnostic:
    """One actionable workspace-contract failure."""

    path: Path
    message: str

    def render(self, project_root: Path) -> str:
        try:
            display = self.path.relative_to(project_root).as_posix()
        except ValueError:
            display = self.path.as_posix()
        return f"{display}: error: {self.message}"


@dataclass(frozen=True)
class CheckResult:
    """The complete result returned by the checker and asserted by self-tests."""

    project_root: Path
    data_repository: Path
    diagnostics: tuple[Diagnostic, ...]


def run_git(directory: Path, *arguments: str) -> tuple[int | None, bytes, str]:
    """Run Git without a shell and retain binary stdout for ``ls-files -z``."""

    try:
        completed = subprocess.run(
            ["git", "-C", str(directory), *arguments],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as error:
        return None, b"", str(error)
    return (
        completed.returncode,
        completed.stdout,
        completed.stderr.decode("utf-8", errors="replace").strip(),
    )


def expected_origin(origin: str) -> bool:
    """Return whether *origin* names the canonical data repository on GitHub."""

    return EXPECTED_ORIGIN_PATTERN.fullmatch(origin.strip()) is not None


def validate_data_repository(
    project_root: Path, data_repository: Path, diagnostics: list[Diagnostic]
) -> None:
    """Check sibling placement, Git identity, and the data repository contract."""

    if data_repository.is_symlink():
        diagnostics.append(
            Diagnostic(
                data_repository,
                "required sibling data repository must be a real directory, not a symlink",
            )
        )
        return

    if not data_repository.is_dir():
        diagnostics.append(
            Diagnostic(
                data_repository,
                "missing required sibling data repository; expected "
                f"{project_root.parent / DATA_REPOSITORY_NAME}",
            )
        )
        return

    returncode, stdout, stderr = run_git(data_repository, "rev-parse", "--is-inside-work-tree")
    if returncode is None:
        diagnostics.append(Diagnostic(data_repository, f"cannot run Git: {stderr}"))
    elif returncode != 0 or stdout.strip() != b"true":
        detail = f": {stderr}" if stderr else ""
        diagnostics.append(Diagnostic(data_repository, f"not a Git worktree{detail}"))
    else:
        returncode, stdout, stderr = run_git(data_repository, "remote", "get-url", "origin")
        if returncode is None:
            diagnostics.append(Diagnostic(data_repository, f"cannot run Git: {stderr}"))
        elif returncode != 0:
            detail = f": {stderr}" if stderr else ""
            diagnostics.append(
                Diagnostic(
                    data_repository,
                    "missing Git remote 'origin' for the canonical data repository" + detail,
                )
            )
        else:
            origin = stdout.decode("utf-8", errors="replace").strip()
            if not expected_origin(origin):
                diagnostics.append(
                    Diagnostic(
                        data_repository,
                        "origin must be an SSH or HTTPS URL for "
                        f"github.com/{DATA_REPOSITORY_PATH}; found {origin!r}",
                    )
                )

    for required_path in REQUIRED_DATA_PATHS:
        path = data_repository / required_path
        if required_path == Path("datasets"):
            present = path.is_dir()
        else:
            present = path.is_file()
        if not present:
            diagnostics.append(
                Diagnostic(path, "missing required KSpaceJet-ismrmrd-data repository path")
            )


def validate_project_payloads(project_root: Path, diagnostics: list[Diagnostic]) -> None:
    """Forbid raw MRI payloads from KSpaceJet's index and working tree."""

    tracked_payloads: set[Path] = set()
    returncode, stdout, stderr = run_git(project_root, "ls-files", "-z")
    if returncode is None:
        diagnostics.append(Diagnostic(project_root, f"cannot run Git: {stderr}"))
    elif returncode != 0:
        detail = f": {stderr}" if stderr else ""
        diagnostics.append(
            Diagnostic(project_root, f"cannot list tracked files with 'git ls-files -z'{detail}")
        )
    else:
        for encoded_path in stdout.split(b"\0"):
            if not encoded_path:
                continue
            relative_path = Path(encoded_path.decode("utf-8", errors="surrogateescape"))
            if relative_path.suffix.casefold() not in RAW_MRI_SUFFIXES:
                continue
            payload_path = project_root / relative_path
            tracked_payloads.add(payload_path)
            diagnostics.append(
                Diagnostic(
                    payload_path,
                    "tracked raw MRI payload is forbidden in KSpaceJet; move it to the sibling "
                    f"{DATA_REPOSITORY_NAME} repository",
                )
            )

    for directory, directory_names, file_names in os.walk(project_root, topdown=True):
        directory_names[:] = [
            name for name in directory_names if name not in EXCLUDED_DIRECTORY_NAMES
        ]
        for file_name in file_names:
            payload_path = Path(directory) / file_name
            if payload_path.suffix.casefold() not in RAW_MRI_SUFFIXES:
                continue
            if payload_path in tracked_payloads:
                continue
            diagnostics.append(
                Diagnostic(
                    payload_path,
                    "raw MRI payload file is forbidden in KSpaceJet; move it to the sibling "
                    f"{DATA_REPOSITORY_NAME} repository",
                )
            )


def check_workspace(project_root: Path) -> CheckResult:
    """Validate the paired repository workspace rooted at *project_root*."""

    resolved_project_root = project_root.expanduser().resolve()
    data_repository = resolved_project_root.parent / DATA_REPOSITORY_NAME
    diagnostics: list[Diagnostic] = []

    if not resolved_project_root.is_dir():
        diagnostics.append(Diagnostic(resolved_project_root, "project root is not a directory"))
        return CheckResult(resolved_project_root, data_repository, tuple(diagnostics))

    validate_data_repository(resolved_project_root, data_repository, diagnostics)
    validate_project_payloads(resolved_project_root, diagnostics)
    return CheckResult(resolved_project_root, data_repository, tuple(diagnostics))


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    """Parse the small CLI surface used by hooks and standalone development checks."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path("."),
        help="KSpaceJet repository root (default: current directory)",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run hermetic checker tests instead of validating a workspace",
    )
    return parser.parse_args(argv)


class WorkspaceLayoutTests(unittest.TestCase):
    """Hermetic fixtures for the repository-layout and raw-payload contract."""

    @classmethod
    def setUpClass(cls) -> None:
        if shutil.which("git") is None:
            raise unittest.SkipTest("Git is required for workspace layout tests")

    def create_workspace(self, origin: str = "https://github.com/isqiwen/KSpaceJet-ismrmrd-data.git") -> tuple[Path, Path]:
        temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(temporary_directory.cleanup)
        workspace = Path(temporary_directory.name)
        project_root = workspace / "KSpaceJet"
        data_repository = workspace / DATA_REPOSITORY_NAME
        project_root.mkdir()
        data_repository.mkdir()
        (data_repository / "datasets").mkdir()
        (data_repository / "tools").mkdir()
        (data_repository / "catalog.yaml").write_text("datasets: []\n", encoding="utf-8")
        (data_repository / "tools" / "verify-data.sh").write_text(
            "#!/usr/bin/env bash\n", encoding="utf-8"
        )
        self.initialize_git(project_root)
        self.initialize_git(data_repository)
        self.run_checked_git(data_repository, "remote", "add", "origin", origin)
        return project_root, data_repository

    def initialize_git(self, repository: Path) -> None:
        completed = subprocess.run(
            ["git", "init", "--quiet", str(repository)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.assertEqual(
            completed.returncode,
            0,
            completed.stderr.decode("utf-8", errors="replace"),
        )

    def run_checked_git(self, repository: Path, *arguments: str) -> None:
        returncode, _, stderr = run_git(repository, *arguments)
        self.assertEqual(returncode, 0, stderr)

    def test_accepts_complete_sibling_workspace(self) -> None:
        project_root, _ = self.create_workspace()

        result = check_workspace(project_root)

        self.assertEqual(result.diagnostics, ())

    def test_rejects_tracked_raw_mri_payload(self) -> None:
        project_root, _ = self.create_workspace()
        raw_payload = project_root / "fixtures" / "scan.mrd"
        raw_payload.parent.mkdir()
        raw_payload.write_bytes(b"not a real MRI acquisition")
        self.run_checked_git(project_root, "add", raw_payload.relative_to(project_root).as_posix())

        result = check_workspace(project_root)

        self.assertTrue(
            any(
                diagnostic.path == raw_payload
                and "tracked raw MRI payload is forbidden" in diagnostic.message
                for diagnostic in result.diagnostics
            ),
            result.diagnostics,
        )

    def test_rejects_untracked_raw_mri_payload(self) -> None:
        project_root, _ = self.create_workspace()
        raw_payload = project_root / "scratch" / "scan.h5"
        raw_payload.parent.mkdir()
        raw_payload.write_bytes(b"not a real MRI acquisition")

        result = check_workspace(project_root)

        self.assertTrue(
            any(
                diagnostic.path == raw_payload
                and "raw MRI payload file is forbidden" in diagnostic.message
                for diagnostic in result.diagnostics
            ),
            result.diagnostics,
        )

    def test_rejects_noncanonical_data_origin(self) -> None:
        project_root, _ = self.create_workspace("git@github.com:example/not-the-data-repository.git")

        result = check_workspace(project_root)

        self.assertTrue(
            any("origin must be an SSH or HTTPS URL" in diagnostic.message for diagnostic in result.diagnostics),
            result.diagnostics,
        )

    def test_rejects_symlinked_data_repository(self) -> None:
        project_root, data_repository = self.create_workspace()
        actual_data_repository = data_repository.parent / "actual-data-repository"
        data_repository.rename(actual_data_repository)
        try:
            data_repository.symlink_to(actual_data_repository, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"cannot create directory symlink on this platform: {error}")

        result = check_workspace(project_root)

        self.assertTrue(
            any("must be a real directory, not a symlink" in diagnostic.message for diagnostic in result.diagnostics),
            result.diagnostics,
        )

    def test_rejects_missing_data_contract_path(self) -> None:
        project_root, data_repository = self.create_workspace()
        (data_repository / "catalog.yaml").unlink()

        result = check_workspace(project_root)

        self.assertTrue(
            any(diagnostic.path == data_repository / "catalog.yaml" for diagnostic in result.diagnostics),
            result.diagnostics,
        )


def main(argv: list[str] | None = None) -> int:
    """Run the workspace checker or its hermetic self-test suite."""

    arguments = parse_arguments(argv)
    if arguments.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(WorkspaceLayoutTests)
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        return 0 if result.wasSuccessful() else 1

    result = check_workspace(arguments.project_root)
    if result.diagnostics:
        for diagnostic in result.diagnostics:
            print(diagnostic.render(result.project_root), file=sys.stderr)
        return 1

    print(
        "workspace layout check passed: KSpaceJet and "
        f"{DATA_REPOSITORY_NAME} are sibling repositories"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
