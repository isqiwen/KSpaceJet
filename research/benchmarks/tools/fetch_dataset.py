#!/usr/bin/env python3
"""Fetch and verify manifest-pinned research MRI datasets.

The tool intentionally lives outside product targets. It only downloads data
to a Git-ignored research directory and uses Python's standard library.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Optional
from urllib.error import HTTPError, URLError
from urllib.parse import urljoin, urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener


CHUNK_BYTES = 1024 * 1024
DATASET_ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
HEX_RE = re.compile(r"^[0-9a-f]+$")


class DatasetError(RuntimeError):
    """A malformed manifest, unsafe path, or failed dataset verification."""


class HttpsOnlyRedirectHandler(HTTPRedirectHandler):
    """Reject every redirect whose resolved target is not HTTPS."""

    def redirect_request(
        self,
        request: Request,
        file_pointer: Any,
        code: int,
        message: str,
        headers: Any,
        new_url: str,
    ) -> Optional[Request]:
        resolved_url = urljoin(request.full_url, new_url)
        if urlsplit(resolved_url).scheme.lower() != "https":
            raise DatasetError(f"refusing a download redirect away from HTTPS: {resolved_url}")
        return super().redirect_request(
            request,
            file_pointer,
            code,
            message,
            headers,
            resolved_url,
        )


HTTPS_OPENER = build_opener(HttpsOnlyRedirectHandler())


def default_dataset_root() -> Path:
    return Path(__file__).resolve().parents[1] / "datasets"


def require_string(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value:
        raise DatasetError(f"{name} must be a non-empty string")
    return value


def require_hex(value: Any, length: int, name: str) -> str:
    value = require_string(value, name).lower()
    if len(value) != length or not HEX_RE.fullmatch(value):
        raise DatasetError(f"{name} must be a {length}-character hexadecimal digest")
    return value


def new_md5() -> Any:
    """Create an MD5 checker without treating it as a security primitive."""

    try:
        return hashlib.md5(usedforsecurity=False)
    except TypeError:
        # Python 3.9+ provides usedforsecurity; retain an actionable fallback
        # for interpreters built against older hashlib APIs.
        try:
            return hashlib.md5()
        except ValueError as error:
            raise DatasetError("this Python runtime cannot calculate the manifest MD5") from error
    except ValueError as error:
        raise DatasetError("this Python runtime cannot calculate the manifest MD5") from error


def safe_relative_path(value: Any, name: str) -> PurePosixPath:
    raw_path = require_string(value, name)
    path = PurePosixPath(raw_path)
    if (
        "\\" in raw_path
        or ":" in raw_path
        or path.is_absolute()
        or any(part in {"", ".", ".."} for part in path.parts)
    ):
        raise DatasetError(f"{name} must be a safe, relative POSIX path")
    return path


def checked_target(root: Path, storage_root: PurePosixPath, relative_path: PurePosixPath) -> Path:
    raw_root = (root / "raw").resolve()
    target = raw_root.joinpath(*storage_root.parts, *relative_path.parts).resolve()
    try:
        target.relative_to(raw_root)
    except ValueError as error:
        raise DatasetError(f"refusing path outside the raw-data root: {target}") from error
    return target


def load_manifest(root: Path, dataset_id: str) -> tuple[dict[str, Any], Path, PurePosixPath]:
    if not DATASET_ID_RE.fullmatch(dataset_id):
        raise DatasetError(f"invalid dataset id: {dataset_id}")

    manifest_path = root / "manifests" / f"{dataset_id}.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise DatasetError(f"manifest not found: {manifest_path}") from error
    except json.JSONDecodeError as error:
        raise DatasetError(f"invalid JSON in {manifest_path}: {error}") from error

    if not isinstance(manifest, dict):
        raise DatasetError(f"manifest root must be an object: {manifest_path}")
    if manifest.get("dataset_id") != dataset_id:
        raise DatasetError(f"dataset_id does not match manifest filename: {manifest_path}")
    if manifest.get("schema_version") != 1:
        raise DatasetError(f"unsupported schema_version in {manifest_path}")
    if not isinstance(manifest.get("files"), list) or not manifest["files"]:
        raise DatasetError(f"manifest has no files: {manifest_path}")

    storage_root = safe_relative_path(manifest.get("storage_root"), "storage_root")
    return manifest, manifest_path, storage_root


def validate_entry(entry: Any) -> dict[str, Any]:
    if not isinstance(entry, dict):
        raise DatasetError("each manifest file entry must be an object")

    path = safe_relative_path(entry.get("relative_path"), "relative_path")
    url = require_string(entry.get("url"), "url")
    if not url.lower().startswith("https://"):
        raise DatasetError(f"dataset URLs must use HTTPS: {url}")

    bytes_expected = entry.get("bytes")
    if not isinstance(bytes_expected, int) or bytes_expected < 0:
        raise DatasetError("bytes must be a non-negative integer")

    return {
        "role": require_string(entry.get("role"), "role"),
        "relative_path": path,
        "url": url,
        "bytes": bytes_expected,
        "md5": require_hex(entry.get("md5"), 32, "md5"),
        "sha256": require_hex(entry.get("sha256"), 64, "sha256"),
    }


def digest_file(path: Path) -> tuple[int, str, str]:
    sha256 = hashlib.sha256()
    md5 = new_md5()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK_BYTES), b""):
            size += len(block)
            sha256.update(block)
            md5.update(block)
    return size, md5.hexdigest(), sha256.hexdigest()


def verify_file(path: Path, entry: dict[str, Any]) -> None:
    if not path.is_file():
        raise DatasetError(f"missing {entry['role']}: {path}")

    size, md5, sha256 = digest_file(path)
    mismatches = []
    if size != entry["bytes"]:
        mismatches.append(f"bytes expected={entry['bytes']} actual={size}")
    if md5 != entry["md5"]:
        mismatches.append(f"md5 expected={entry['md5']} actual={md5}")
    if sha256 != entry["sha256"]:
        mismatches.append(f"sha256 expected={entry['sha256']} actual={sha256}")
    if mismatches:
        raise DatasetError(f"verification failed for {path}: {'; '.join(mismatches)}")


def download_file(root: Path, path: Path, entry: dict[str, Any]) -> None:
    # Resolve beneath raw/ so temporary and final files always share the same
    # volume, including when a user maps raw/ to a data drive with a junction
    # or symlink. Atomic publication then remains portable.
    staging_root = (root / "raw").resolve() / ".staging"
    temporary_path: Optional[Path] = None
    sha256 = hashlib.sha256()
    md5 = new_md5()
    size = 0

    try:
        staging_root.mkdir(parents=True, exist_ok=True)
        path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary_name = tempfile.mkstemp(
            dir=staging_root,
            prefix=f"{path.name}.",
            suffix=".part",
        )
        temporary_path = Path(temporary_name)
        request = Request(entry["url"], headers={"User-Agent": "KSpaceJet-dataset-fetcher/1"})
        with os.fdopen(descriptor, "wb") as output, HTTPS_OPENER.open(request, timeout=60) as response:
            if not response.geturl().lower().startswith("https://"):
                raise DatasetError(
                    f"refusing a download redirect away from HTTPS: {response.geturl()}"
                )
            for block in iter(lambda: response.read(CHUNK_BYTES), b""):
                size += len(block)
                if size > entry["bytes"]:
                    raise DatasetError(
                        f"downloaded {entry['role']} exceeded the frozen byte count"
                    )
                md5.update(block)
                sha256.update(block)
                output.write(block)

        if size != entry["bytes"] or md5.hexdigest() != entry["md5"] or sha256.hexdigest() != entry["sha256"]:
            raise DatasetError(
                f"downloaded {entry['role']} did not match the frozen byte count or digests"
            )
        if path.exists():
            raise DatasetError(f"refusing to overwrite an existing data file: {path}")
        temporary_path.replace(path)
    except (HTTPError, URLError, OSError, ValueError) as error:
        raise DatasetError(f"download failed for {entry['url']}: {error}") from error
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def list_datasets(root: Path) -> int:
    manifest_root = root / "manifests"
    manifests = sorted(manifest_root.glob("*.json"))
    if not manifests:
        raise DatasetError(f"no manifests found in {manifest_root}")

    for manifest_path in manifests:
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            print(f"{manifest['dataset_id']}\t{manifest['title']}")
        except (KeyError, json.JSONDecodeError) as error:
            raise DatasetError(f"invalid manifest while listing {manifest_path}: {error}") from error
    return 0


def fetch_or_verify(root: Path, dataset_id: str, *, fetch: bool, accept_source_terms: bool) -> int:
    manifest, _, storage_root = load_manifest(root, dataset_id)
    source = manifest.get("source")
    if not isinstance(source, dict):
        raise DatasetError("manifest source must be an object")
    if fetch and source.get("accept_source_terms_required") and not accept_source_terms:
        raise DatasetError(
            "source terms must be acknowledged with --accept-source-terms; "
            "this confirms local-only use and does not grant redistribution rights"
        )

    for raw_entry in manifest["files"]:
        entry = validate_entry(raw_entry)
        target = checked_target(root, storage_root, entry["relative_path"])
        if fetch and not target.exists():
            print(f"Downloading {entry['role']}: {entry['url']}")
            download_file(root, target, entry)
        verify_file(target, entry)
        print(f"Verified {entry['role']}: {target}")
    return 0


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    def add_root(option_parser: argparse.ArgumentParser) -> None:
        option_parser.add_argument(
            "--root",
            type=Path,
            default=default_dataset_root(),
            help="dataset root containing manifests/ and ignored raw/ storage",
        )

    list_parser = subparsers.add_parser("list", help="list versioned dataset manifests")
    add_root(list_parser)

    fetch_parser = subparsers.add_parser("fetch", help="fetch then verify a manifest-pinned dataset")
    fetch_parser.add_argument("--id", required=True, help="dataset manifest id")
    fetch_parser.add_argument(
        "--accept-source-terms",
        action="store_true",
        help="acknowledge local-only source terms when the manifest requires it",
    )
    add_root(fetch_parser)

    verify_parser = subparsers.add_parser("verify", help="verify an already downloaded dataset")
    verify_parser.add_argument("--id", required=True, help="dataset manifest id")
    add_root(verify_parser)

    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    root = args.root.resolve()
    try:
        if args.command == "list":
            return list_datasets(root)
        if args.command == "fetch":
            return fetch_or_verify(
                root,
                args.id,
                fetch=True,
                accept_source_terms=args.accept_source_terms,
            )
        return fetch_or_verify(root, args.id, fetch=False, accept_source_terms=False)
    except DatasetError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
