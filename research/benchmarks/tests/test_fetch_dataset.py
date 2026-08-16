"""Platform-neutral tests for the research dataset downloader."""

from __future__ import annotations

import contextlib
import functools
import hashlib
import importlib.util
import io
import json
import sys
import tempfile
import threading
import unittest
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest import mock
from urllib.request import ProxyHandler, Request, build_opener


FETCHER_PATH = Path(__file__).resolve().parents[1] / "tools" / "fetch_dataset.py"
FETCHER_SPEC = importlib.util.spec_from_file_location("ksj_fetch_dataset", FETCHER_PATH)
if FETCHER_SPEC is None or FETCHER_SPEC.loader is None:
    raise RuntimeError(f"could not load dataset fetcher: {FETCHER_PATH}")
fetch_dataset = importlib.util.module_from_spec(FETCHER_SPEC)
sys.modules[FETCHER_SPEC.name] = fetch_dataset
sys.dont_write_bytecode = True
FETCHER_SPEC.loader.exec_module(fetch_dataset)


class SilentRequestHandler(SimpleHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:
        del format, args


class SecureUrlResponse:
    """Expose the validated HTTPS source URL while using a local HTTP fixture."""

    def __init__(self, response: object, source_url: str) -> None:
        self.response = response
        self.source_url = source_url

    def __enter__(self) -> "SecureUrlResponse":
        self.response.__enter__()
        return self

    def __exit__(self, *args: object) -> object:
        return self.response.__exit__(*args)

    def geturl(self) -> str:
        return self.source_url

    def read(self, size: int = -1) -> bytes:
        return self.response.read(size)


class FetchDatasetTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name) / "datasets"
        self.source_root = Path(self.temporary_directory.name) / "source"
        self.source_root.mkdir()
        self.payload = b"KSpaceJet research fixture\x00" + b"x" * (
            fetch_dataset.CHUNK_BYTES + 257
        )
        (self.source_root / "fixture.bin").write_bytes(self.payload)

        handler = functools.partial(SilentRequestHandler, directory=str(self.source_root))
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.server_thread.start()
        self.server_url = f"http://127.0.0.1:{self.server.server_port}"
        self.local_opener = build_opener(ProxyHandler({}))
        self.requests: list[str] = []
        self.dataset_id = "local-fetch-fixture"
        self._write_manifest(storage_root="local-fixture")

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server_thread.join(timeout=5)
        self.server.server_close()
        self.temporary_directory.cleanup()

    def _write_manifest(self, *, storage_root: str) -> None:
        manifest_root = self.root / "manifests"
        manifest_root.mkdir(parents=True, exist_ok=True)
        md5 = fetch_dataset.new_md5()
        md5.update(self.payload)
        manifest = {
            "dataset_id": self.dataset_id,
            "title": "Local downloader fixture",
            "storage_root": storage_root,
            "source": {"accept_source_terms_required": True},
            "files": [
                {
                    "role": "raw_input",
                    "relative_path": "sample/fixture.bin",
                    "url": "https://datasets.example.invalid/fixture.bin",
                    "bytes": len(self.payload),
                    "md5": md5.hexdigest(),
                    "sha256": hashlib.sha256(self.payload).hexdigest(),
                }
            ],
        }
        (manifest_root / f"{self.dataset_id}.json").write_text(
            json.dumps(manifest),
            encoding="utf-8",
        )

    def _local_urlopen(self, request: Request, timeout: int) -> SecureUrlResponse:
        prefix = "https://datasets.example.invalid"
        self.assertTrue(request.full_url.startswith(prefix))
        self.requests.append(request.full_url)
        local_request = Request(
            f"{self.server_url}{request.full_url[len(prefix):]}",
            headers=dict(request.header_items()),
        )
        return SecureUrlResponse(
            self.local_opener.open(local_request, timeout=timeout),
            request.full_url,
        )

    def _run_cli(self, *arguments: str, use_local_server: bool = False) -> tuple[int, str, str]:
        stdout = io.StringIO()
        stderr = io.StringIO()
        patches = [
            mock.patch.object(sys, "argv", ["fetch_dataset.py", *arguments]),
            contextlib.redirect_stdout(stdout),
            contextlib.redirect_stderr(stderr),
        ]
        if use_local_server:
            patches.append(mock.patch.object(fetch_dataset.HTTPS_OPENER, "open", side_effect=self._local_urlopen))

        with contextlib.ExitStack() as stack:
            for patch in patches:
                stack.enter_context(patch)
            status = fetch_dataset.main()
        return status, stdout.getvalue(), stderr.getvalue()

    def test_fetch_then_verify_uses_the_same_cross_platform_layout(self) -> None:
        status, stdout, stderr = self._run_cli(
            "fetch",
            "--id",
            self.dataset_id,
            "--accept-source-terms",
            "--root",
            str(self.root),
            use_local_server=True,
        )
        self.assertEqual(0, status, stderr)
        self.assertIn("Downloading raw_input", stdout)
        self.assertIn("Verified raw_input", stdout)
        self.assertEqual(1, len(self.requests))
        target = self.root / "raw" / "local-fixture" / "sample" / "fixture.bin"
        self.assertEqual(self.payload, target.read_bytes())
        with mock.patch.object(
            fetch_dataset.HTTPS_OPENER,
            "open",
            side_effect=AssertionError("verify must not access the network"),
        ):
            status, _, stderr = self._run_cli(
                "verify",
                "--id",
                self.dataset_id,
                "--root",
                str(self.root),
            )
        self.assertEqual(0, status, stderr)
        self.assertEqual([], list((self.root / "raw" / ".staging").iterdir()))

    def test_fetch_requires_explicit_source_terms_acknowledgement(self) -> None:
        status, _, stderr = self._run_cli(
            "fetch",
            "--id",
            self.dataset_id,
            "--root",
            str(self.root),
        )
        self.assertEqual(2, status)
        self.assertIn("accept-source-terms", stderr)
        self.assertEqual([], self.requests)
        self.assertFalse((self.root / "raw").exists())

    def test_verify_rejects_a_corrupted_download(self) -> None:
        self.assertEqual(
            0,
            self._run_cli(
                "fetch",
                "--id",
                self.dataset_id,
                "--accept-source-terms",
                "--root",
                str(self.root),
                use_local_server=True,
            )[0],
        )
        target = self.root / "raw" / "local-fixture" / "sample" / "fixture.bin"
        target.write_bytes(b"corrupted")
        status, _, stderr = self._run_cli(
            "verify",
            "--id",
            self.dataset_id,
            "--root",
            str(self.root),
        )
        self.assertEqual(2, status)
        self.assertIn("verification failed", stderr)
        self.assertEqual(1, len(self.requests))

    def test_bad_remote_content_never_publishes_a_partial_target(self) -> None:
        (self.source_root / "fixture.bin").write_bytes(b"z" * len(self.payload))
        status, _, stderr = self._run_cli(
            "fetch",
            "--id",
            self.dataset_id,
            "--accept-source-terms",
            "--root",
            str(self.root),
            use_local_server=True,
        )
        self.assertEqual(2, status)
        self.assertIn("frozen byte count or digests", stderr)
        target = self.root / "raw" / "local-fixture" / "sample" / "fixture.bin"
        self.assertFalse(target.exists())
        self.assertEqual([], list((self.root / "raw" / ".staging").iterdir()))

    def test_oversized_remote_content_is_rejected_before_publication(self) -> None:
        (self.source_root / "fixture.bin").write_bytes(self.payload + b"extra")
        status, _, stderr = self._run_cli(
            "fetch",
            "--id",
            self.dataset_id,
            "--accept-source-terms",
            "--root",
            str(self.root),
            use_local_server=True,
        )
        self.assertEqual(2, status)
        self.assertIn("exceeded the frozen byte count", stderr)
        target = self.root / "raw" / "local-fixture" / "sample" / "fixture.bin"
        self.assertFalse(target.exists())
        self.assertEqual([], list((self.root / "raw" / ".staging").iterdir()))

    def test_manifest_cannot_escape_the_raw_data_directory(self) -> None:
        self._write_manifest(storage_root="..\\outside")
        status, _, stderr = self._run_cli(
            "verify",
            "--id",
            self.dataset_id,
            "--root",
            str(self.root),
        )
        self.assertEqual(2, status)
        self.assertIn("safe, relative POSIX path", stderr)

    def test_https_redirect_handler_rejects_a_downgrade(self) -> None:
        handler = fetch_dataset.HttpsOnlyRedirectHandler()
        with self.assertRaisesRegex(fetch_dataset.DatasetError, "redirect away from HTTPS"):
            handler.redirect_request(
                Request("https://datasets.example.invalid/fixture.bin"),
                None,
                302,
                "Found",
                {},
                "http://datasets.example.invalid/fixture.bin",
            )

    def test_https_redirect_handler_keeps_an_https_target(self) -> None:
        request = fetch_dataset.HttpsOnlyRedirectHandler().redirect_request(
            Request("https://datasets.example.invalid/original.bin"),
            None,
            302,
            "Found",
            {},
            "/redirected.bin",
        )
        self.assertIsNotNone(request)
        self.assertEqual("https://datasets.example.invalid/redirected.bin", request.full_url)


if __name__ == "__main__":
    unittest.main()
