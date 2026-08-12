#!/usr/bin/env python3
"""Verify that the locally checked-out Intel payload is hydrated and intact.

The manifests are normal Git files while the payload files themselves are Git-LFS
objects.  Merely finding ``manifest-files.json`` therefore does not prove that
the required dynamic libraries have been downloaded.  The default quick check
hashes the IPP, MKL and OpenMP runtime sentinels needed by the local Conan
recipe; ``--full`` verifies every manifest entry.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path, PurePosixPath
from typing import Iterable


CHUNK_BYTES = 1024 * 1024
QUICK_SENTINELS = {
    "linux-x86_64": (
        "libippcore.so",
        "libmkl_rt.so",
        "libiomp5.so",
    ),
    "windows-x86_64": (
        "ippcore.dll",
        "libiomp5md.dll",
    ),
}


def default_platform() -> str:
    if sys.platform.startswith("linux") and os.uname().machine == "x86_64":
        return "linux-x86_64"
    if sys.platform == "win32" and os.environ.get("PROCESSOR_ARCHITECTURE", "").upper() == "AMD64":
        return "windows-x86_64"
    raise ValueError("cannot infer a supported Intel payload platform; pass --platform explicitly")


def checked_relative_path(value: object) -> PurePosixPath:
    if not isinstance(value, str):
        raise ValueError("manifest entry path must be a string")
    relative = PurePosixPath(value)
    if relative.is_absolute() or ".." in relative.parts or "\\" in value:
        raise ValueError(f"unsafe manifest relative path: {value!r}")
    return relative


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def select_quick_entries(platform: str, entries: list[dict[str, object]]) -> list[dict[str, object]]:
    by_name: dict[str, dict[str, object]] = {}
    for entry in entries:
        relative = checked_relative_path(entry.get("path"))
        by_name.setdefault(relative.name.lower(), entry)

    selected: list[dict[str, object]] = []
    for file_name in QUICK_SENTINELS[platform]:
        entry = by_name.get(file_name)
        if entry is None:
            raise ValueError(f"manifest lacks required quick-check entry: {file_name}")
        selected.append(entry)

    if platform == "windows-x86_64":
        mkl_entries = [
            entry
            for name, entry in by_name.items()
            if name.startswith("mkl_rt") and name.endswith(".dll")
        ]
        if not mkl_entries:
            raise ValueError("manifest lacks an MKL runtime DLL quick-check entry")
        selected.append(sorted(mkl_entries, key=lambda item: str(item["path"]))[0])

    return selected


def verify_entries(payload_root: Path, entries: Iterable[dict[str, object]]) -> list[str]:
    failures: list[str] = []
    for entry in entries:
        relative = checked_relative_path(entry.get("path"))
        expected = entry.get("sha256")
        if not isinstance(expected, str) or len(expected) != 64:
            failures.append(f"{relative}: invalid SHA-256 in manifest")
            continue

        target = payload_root.joinpath(*relative.parts)
        if not target.is_file():
            failures.append(f"{relative}: missing (Git-LFS payload is not hydrated)")
            continue

        actual = sha256_file(target)
        if actual.lower() != expected.lower():
            failures.append(f"{relative}: SHA-256 mismatch")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--platform", choices=("linux-x86_64", "windows-x86_64"))
    parser.add_argument("--full", action="store_true", help="verify every file listed in the manifest")
    parser.add_argument("--quiet", action="store_true", help="suppress success output")
    arguments = parser.parse_args()

    try:
        platform = arguments.platform or default_platform()
        payload_root = arguments.root.resolve() / "third_party" / "intel" / "payload" / platform
        manifest_path = payload_root / "manifest-files.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        entries = manifest.get("files")
        if not isinstance(entries, list) or not all(isinstance(entry, dict) for entry in entries):
            raise ValueError("manifest must contain a files array")
        selected = entries if arguments.full else select_quick_entries(platform, entries)
        failures = verify_entries(payload_root, selected)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        if not arguments.quiet:
            print(f"[kspacejet-devenv] Intel payload verification failed: {error}", file=sys.stderr)
        return 2

    if failures:
        if not arguments.quiet:
            for failure in failures:
                print(f"[kspacejet-devenv] Intel payload verification failed: {failure}", file=sys.stderr)
        return 2

    if not arguments.quiet:
        mode = "full" if arguments.full else "quick"
        print(f"[kspacejet-devenv] Intel payload {platform} {mode} verification passed ({len(selected)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
