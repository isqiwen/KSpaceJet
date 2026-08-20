#!/usr/bin/env python3
"""Check repository-local Markdown links without making network requests.

The checker deliberately covers the navigation contract that can be verified
offline: inline Markdown links to repository files and Markdown headings.
External URLs are left untouched because their availability is not a local
build property.  It uses only the Python standard library so hooks and CI do
not need an additional Markdown parser dependency.
"""

from __future__ import annotations

import argparse
from collections import Counter
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import dataclass
import html
from io import StringIO
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unicodedata
import unittest
from urllib.parse import unquote


MARKDOWN_SUFFIXES = frozenset({".md", ".markdown", ".mdown", ".mkdn"})
EXCLUDED_DIRECTORY_NAMES = frozenset({".git", ".kspacejet", ".venv", "out"})
EXCLUDED_PREFIX = ("third_party", "intel", "payload")
SCHEME_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
ATX_HEADING_PATTERN = re.compile(r"^ {0,3}#{1,6}(?:[ \t]+(.*)|[ \t]*$)")
FENCE_PATTERN = re.compile(r"^ {0,3}(`{3,}|~{3,})")


@dataclass(frozen=True)
class LinkReference:
    """One parsed inline link destination."""

    source: Path
    line: int
    destination: str


@dataclass(frozen=True)
class Diagnostic:
    """One local documentation integrity error."""

    source: Path
    line: int
    message: str


@dataclass(frozen=True)
class CheckResult:
    """The compact result used by both the CLI and its self-test."""

    markdown_files: int
    local_references: int
    diagnostics: tuple[Diagnostic, ...]


@dataclass(frozen=True)
class MarkdownDocument:
    """Parsed local information needed to validate a Markdown file."""

    path: Path
    lines: tuple[str, ...]
    scan_lines: tuple[str | None, ...]
    anchors: frozenset[str]


def is_within(path: Path, root: Path) -> bool:
    """Return whether *path* stays within the resolved project root."""

    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def display_path(path: Path, root: Path) -> str:
    """Render a stable project-relative path for diagnostics."""

    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def is_excluded(relative_path: Path) -> bool:
    """Keep generated, local-environment, and vendored payload docs out of scope."""

    parts = relative_path.parts
    if any(part in EXCLUDED_DIRECTORY_NAMES for part in parts):
        return True
    return parts[: len(EXCLUDED_PREFIX)] == EXCLUDED_PREFIX


def discover_markdown_files(project_root: Path) -> list[Path]:
    """Find tracked or project Markdown while remaining usable outside Git."""

    command = ["git", "ls-files", "--cached", "--others", "--exclude-standard"]
    try:
        completed = subprocess.run(
            command,
            cwd=project_root,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        completed = None

    if completed is not None and completed.returncode == 0:
        candidates = [Path(entry) for entry in completed.stdout.splitlines() if entry]
    else:
        candidates = [path.relative_to(project_root) for path in project_root.rglob("*.md")]

    markdown_files: list[Path] = []
    for relative_path in candidates:
        if relative_path.suffix.lower() not in MARKDOWN_SUFFIXES or is_excluded(relative_path):
            continue
        source = (project_root / relative_path).resolve()
        if source.is_file() and is_within(source, project_root):
            markdown_files.append(source)
    return sorted(set(markdown_files), key=lambda path: path.as_posix())


def mask_inline_code_and_math(line: str) -> str:
    """Blank inline code and ``\\(...\\)`` math without changing line numbers."""

    characters = list(line)
    index = 0
    while index < len(line):
        if line.startswith(r"\(", index):
            closing = line.find(r"\)", index + 2)
            end = len(line) if closing == -1 else closing + 2
            characters[index:end] = " " * (end - index)
            index = end
            continue

        if line[index] == "`":
            delimiter_end = index + 1
            while delimiter_end < len(line) and line[delimiter_end] == "`":
                delimiter_end += 1
            delimiter = line[index:delimiter_end]
            closing = line.find(delimiter, delimiter_end)
            if closing != -1:
                end = closing + len(delimiter)
                characters[index:end] = " " * (end - index)
                index = end
                continue
            index = delimiter_end
            continue

        index += 1
    return "".join(characters)


def markdown_scan_lines(lines: tuple[str, ...]) -> tuple[str | None, ...]:
    """Mask fenced code and display math, which are not rendered Markdown links."""

    scan_lines: list[str | None] = []
    fence: tuple[str, int] | None = None
    in_bracket_math = False
    in_dollar_math = False

    for line in lines:
        if fence is not None:
            scan_lines.append(None)
            fence_character, minimum_length = fence
            closing_pattern = rf"^ {{0,3}}{re.escape(fence_character)}{{{minimum_length},}}"
            if re.match(closing_pattern, line):
                fence = None
            continue

        opening_fence = FENCE_PATTERN.match(line)
        if opening_fence is not None:
            marker = opening_fence.group(1)
            scan_lines.append(None)
            fence = (marker[0], len(marker))
            continue

        if in_bracket_math:
            scan_lines.append(None)
            if r"\]" in line:
                in_bracket_math = False
            continue

        if r"\[" in line:
            scan_lines.append(None)
            opening_index = line.find(r"\[")
            if r"\]" not in line[opening_index + 2 :]:
                in_bracket_math = True
            continue

        if in_dollar_math:
            scan_lines.append(None)
            if "$$" in line:
                in_dollar_math = False
            continue

        if "$$" in line:
            scan_lines.append(None)
            if line.count("$$") % 2 == 1:
                in_dollar_math = True
            continue

        scan_lines.append(mask_inline_code_and_math(line))

    return tuple(scan_lines)


def unescape_markdown_text(value: str) -> str:
    """Remove the small Markdown escape subset relevant to paths and headings."""

    return re.sub(r"\\([!\"#$%&'()*+,\-./:;<=>?@[\\\]^_`{|}~])", r"\1", value)


def heading_slug(value: str) -> str:
    """Produce the GitHub-style fragment form used by this repository's headings."""

    text = html.unescape(value)
    text = re.sub(r"<[^>]*>", "", text)
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = unescape_markdown_text(text)
    text = text.replace("*", "").replace("_", "").replace("~", "").strip().lower()

    characters: list[str] = []
    for character in text:
        category = unicodedata.category(character)
        if (category.startswith("P") or category.startswith("S")) and character not in {"-", "_"}:
            continue
        characters.append(character)
    return re.sub(r"\s+", "-", "".join(characters))


def extract_anchors(lines: tuple[str, ...], scan_lines: tuple[str | None, ...]) -> frozenset[str]:
    """Extract ATX heading anchors, including GitHub's duplicate suffix rule."""

    counts: Counter[str] = Counter()
    anchors: set[str] = set()
    for line, scan_line in zip(lines, scan_lines, strict=True):
        if scan_line is None:
            continue
        match = ATX_HEADING_PATTERN.match(line)
        if match is None:
            continue
        heading = re.sub(r"[ \t]+#+[ \t]*$", "", match.group(1) or "")
        slug = heading_slug(heading)
        if not slug:
            continue
        suffix = counts[slug]
        counts[slug] += 1
        anchors.add(slug if suffix == 0 else f"{slug}-{suffix}")
    return frozenset(anchors)


def is_escaped(line: str, index: int) -> bool:
    """Return whether the character at *index* is protected by an odd slash run."""

    slash_count = 0
    cursor = index - 1
    while cursor >= 0 and line[cursor] == "\\":
        slash_count += 1
        cursor -= 1
    return slash_count % 2 == 1


def find_closing_bracket(line: str, opening_index: int) -> int | None:
    """Find the closing Markdown label bracket while allowing nested labels."""

    depth = 1
    for index in range(opening_index + 1, len(line)):
        if is_escaped(line, index):
            continue
        if line[index] == "[":
            depth += 1
        elif line[index] == "]":
            depth -= 1
            if depth == 0:
                return index
    return None


def find_closing_parenthesis(line: str, opening_index: int) -> int | None:
    """Find the matching inline-link delimiter while allowing URL parentheses."""

    depth = 1
    for index in range(opening_index + 1, len(line)):
        if is_escaped(line, index):
            continue
        if line[index] == "(":
            depth += 1
        elif line[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    return None


def parse_destination(content: str) -> str | None:
    """Extract an inline-link destination and ignore an optional title."""

    stripped = content.strip()
    if not stripped:
        return None
    if stripped.startswith("<"):
        closing = stripped.find(">", 1)
        return None if closing == -1 else stripped[1:closing]

    depth = 0
    for index, character in enumerate(stripped):
        if is_escaped(stripped, index):
            continue
        if character == "(":
            depth += 1
        elif character == ")" and depth > 0:
            depth -= 1
        elif character.isspace() and depth == 0:
            return stripped[:index]
    return stripped


def find_inline_links(document: MarkdownDocument) -> list[LinkReference]:
    """Find inline links and images in the rendered portions of a document."""

    references: list[LinkReference] = []
    for line_number, scan_line in enumerate(document.scan_lines, start=1):
        if scan_line is None:
            continue
        index = 0
        while index < len(scan_line):
            opening = scan_line.find("[", index)
            if opening == -1:
                break
            if is_escaped(scan_line, opening):
                index = opening + 1
                continue
            closing_label = find_closing_bracket(scan_line, opening)
            if closing_label is None or closing_label + 1 >= len(scan_line) or scan_line[closing_label + 1] != "(":
                index = opening + 1
                continue
            closing_destination = find_closing_parenthesis(scan_line, closing_label + 1)
            if closing_destination is None:
                index = closing_label + 1
                continue
            destination = parse_destination(scan_line[closing_label + 2 : closing_destination])
            if destination:
                references.append(LinkReference(document.path, line_number, destination))
            index = closing_destination + 1
    return references


def load_documents(project_root: Path) -> tuple[dict[Path, MarkdownDocument], list[Diagnostic]]:
    """Load Markdown once so anchor validation is deterministic and inexpensive."""

    documents: dict[Path, MarkdownDocument] = {}
    diagnostics: list[Diagnostic] = []
    for path in discover_markdown_files(project_root):
        try:
            lines = tuple(path.read_text(encoding="utf-8").splitlines())
        except OSError as error:
            diagnostics.append(Diagnostic(path, 1, f"cannot read Markdown file: {error}"))
            continue
        except UnicodeDecodeError as error:
            diagnostics.append(Diagnostic(path, 1, f"Markdown must be UTF-8: {error}"))
            continue
        scan_lines = markdown_scan_lines(lines)
        documents[path] = MarkdownDocument(path, lines, scan_lines, extract_anchors(lines, scan_lines))
    return documents, diagnostics


def is_external_destination(destination: str) -> bool:
    """Leave URLs and URI schemes to their owners; this check must stay offline."""

    return destination.startswith("//") or SCHEME_PATTERN.match(destination) is not None


def validate_reference(
    reference: LinkReference,
    documents: dict[Path, MarkdownDocument],
    project_root: Path,
) -> Diagnostic | None:
    """Validate one local destination and its optional Markdown fragment."""

    destination = unescape_markdown_text(reference.destination)
    if is_external_destination(destination):
        return None

    path_text, separator, fragment = destination.partition("#")
    path_text = unquote(path_text.split("?", 1)[0])
    fragment = unquote(fragment) if separator else ""
    if not path_text:
        target = reference.source
    elif path_text.startswith("/"):
        target = project_root / path_text.lstrip("/")
    else:
        target = reference.source.parent / path_text
    target = target.resolve()

    if not is_within(target, project_root):
        return Diagnostic(
            reference.source,
            reference.line,
            f"local link escapes project root: {reference.destination}",
        )
    if is_excluded(target.relative_to(project_root)):
        return None
    if not target.exists():
        return Diagnostic(
            reference.source,
            reference.line,
            f"missing local link target: {reference.destination} -> {display_path(target, project_root)}",
        )
    if not fragment or target.suffix.lower() not in MARKDOWN_SUFFIXES:
        return None

    target_document = documents.get(target)
    if target_document is None:
        return Diagnostic(
            reference.source,
            reference.line,
            f"cannot inspect Markdown anchor in local target: {reference.destination}",
        )
    anchor = heading_slug(fragment)
    if anchor not in target_document.anchors:
        return Diagnostic(
            reference.source,
            reference.line,
            f"missing Markdown anchor: {reference.destination}",
        )
    return None


def check_project(project_root: Path) -> CheckResult:
    """Run the offline check and return diagnostics without printing them."""

    root = project_root.resolve()
    documents, diagnostics = load_documents(root)
    references = [reference for document in documents.values() for reference in find_inline_links(document)]
    local_references = 0
    for reference in references:
        if is_external_destination(unescape_markdown_text(reference.destination)):
            continue
        local_references += 1
        diagnostic = validate_reference(reference, documents, root)
        if diagnostic is not None:
            diagnostics.append(diagnostic)

    diagnostics.sort(key=lambda item: (display_path(item.source, root), item.line, item.message))
    return CheckResult(len(documents), local_references, tuple(diagnostics))


def print_result(result: CheckResult, project_root: Path) -> None:
    """Print compact, stable output suitable for hooks and CI logs."""

    if not result.diagnostics:
        print(
            "[kspacejet-checks] Markdown link check passed: "
            f"{result.markdown_files} file(s), {result.local_references} local link(s)."
        )
        return

    for diagnostic in result.diagnostics:
        print(
            f"[kspacejet-checks] error: {display_path(diagnostic.source, project_root)}:"
            f"{diagnostic.line}: {diagnostic.message}",
            file=sys.stderr,
        )
    print(
        "[kspacejet-checks] Markdown link check failed: "
        f"{len(result.diagnostics)} error(s) across {result.markdown_files} file(s).",
        file=sys.stderr,
    )


def default_project_root() -> Path:
    """Return the repository root when the script is invoked without an option."""

    return Path(__file__).resolve().parents[2]


class MarkdownLinkCheckerSelfTest(unittest.TestCase):
    """Small hermetic regression coverage for the checker contract itself."""

    def write_file(self, root: Path, relative_path: str, content: str) -> None:
        path = root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def test_valid_link_anchor_and_ignored_regions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.write_file(
                root,
                "docs/README.md",
                "# Root\n"
                "[Guide](guide.md#中文标题)\n"
                "[Website](https://example.invalid)\n"
                "```text\n[not-a-link](missing.md)\n```\n"
                "\\[\n[w](not-a-link)\n\\]\n",
            )
            self.write_file(root, "docs/guide.md", "# 中文：标题\n")

            result = check_project(root)

            self.assertEqual(2, result.markdown_files)
            self.assertEqual(1, result.local_references)
            self.assertEqual((), result.diagnostics)

    def test_missing_relative_path_returns_nonzero(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.write_file(root, "docs/README.md", "# Root\n[Missing](missing.md)\n")

            output = StringIO()
            errors = StringIO()
            with redirect_stdout(output), redirect_stderr(errors):
                exit_code = main(["--project-root", str(root)])

            self.assertNotEqual(0, exit_code)
            self.assertIn("missing local link target", errors.getvalue())

    def test_missing_anchor_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.write_file(root, "docs/README.md", "# Root\n[Guide](guide.md#missing)\n")
            self.write_file(root, "docs/guide.md", "# Present\n")

            result = check_project(root)

            self.assertEqual(1, len(result.diagnostics))
            self.assertIn("missing Markdown anchor", result.diagnostics[0].message)

    def test_local_environment_and_vendored_paths_are_excluded(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.write_file(
                root,
                "docs/README.md",
                "# Root\n[Ignored vendor anchor](../third_party/intel/payload/README.md#missing)\n",
            )
            self.write_file(root, ".venv/README.md", "[Ignored](missing.md)\n")
            self.write_file(root, ".kspacejet/README.md", "[Ignored](missing.md)\n")
            self.write_file(root, "out/README.md", "[Ignored](missing.md)\n")
            self.write_file(
                root,
                "third_party/intel/payload/README.md",
                "[Ignored](missing.md)\n",
            )

            result = check_project(root)

            self.assertEqual(1, result.markdown_files)
            self.assertEqual((), result.diagnostics)


def parse_arguments(arguments: list[str] | None) -> argparse.Namespace:
    """Parse the small command-line surface used by hooks and direct runs."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--project-root",
        type=Path,
        default=default_project_root(),
        help="repository root to scan (default: repository containing this script)",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run hermetic checker regression tests instead of scanning a project",
    )
    return parser.parse_args(arguments)


def main(arguments: list[str] | None = None) -> int:
    """Run the requested check and return a conventional process status."""

    args = parse_arguments(arguments)
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(MarkdownLinkCheckerSelfTest)
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        return 0 if result.wasSuccessful() else 1

    project_root = args.project_root.resolve()
    if not project_root.is_dir():
        print(f"[kspacejet-checks] error: project root does not exist: {project_root}", file=sys.stderr)
        return 2
    result = check_project(project_root)
    print_result(result, project_root)
    return 1 if result.diagnostics else 0


if __name__ == "__main__":
    raise SystemExit(main())
