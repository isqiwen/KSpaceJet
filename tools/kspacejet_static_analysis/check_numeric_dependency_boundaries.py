#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import re
import sys
from collections.abc import Iterable


DEFAULT_EXCLUDED_DIRS = {
    ".git",
    ".idea",
    ".vs",
    ".vscode",
    "__pycache__",
    "build",
    "cmake-build-debug",
    "cmake-build-release",
    "out",
    "third_party",
}

SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".c++",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".ipp",
    ".inl",
    ".cmake",
}

SOURCE_NAMES = {
    "CMakeLists.txt",
}


@dataclass(frozen=True)
class Rule:
    name: str
    description: str
    pattern: re.Pattern[str]
    allowed_prefixes: tuple[str, ...] = ()


@dataclass(frozen=True)
class Violation:
    rule_name: str
    description: str
    path: Path
    line: int
    text: str


RULES = (
    Rule(
        name="old-kspacejet-math-include",
        description="Old kspacejet-math public headers must be replaced by numerics/core/runtime APIs.",
        pattern=re.compile(
            r'#\s*include\s*[<"](?:'
            r'kspacejet-math/include/|'
            r'kspacejet/mri/math/|'
            r'common/|'
            r'fourier/|'
            r'image/|'
            r'linear_algebra/|'
            r'noise/'
            r')'
        ),
    ),
    Rule(
        name="kspacejet-math-target",
        description="Business targets must not link or include the legacy KSpaceJet::math target.",
        pattern=re.compile(r"\b(?:KSpaceJet::math|ksj_math)\b"),
    ),
    Rule(
        name="armadillo",
        description="Armadillo use is being migrated out; row/column-major behavior must move behind numerics APIs.",
        pattern=re.compile(
            r'#\s*include\s*[<"](?:arma\.h|armadillo)|'
            r"\barma::|"
            r"\bArmadillo::Armadillo\b|"
            r"\bfind_package\s*\(\s*Armadillo\b|"
            r"\bARMA_[A-Z0-9_]+\b"
        ),
    ),
    Rule(
        name="mkl-outside-numerics",
        description="MKL calls should be hidden behind libs/numerics backends.",
        pattern=re.compile(
            r'#\s*include\s*[<"]mkl[^>"]*|'
            r"\b(?:Dfti|MKL_|LAPACKE_|cblas_|mkl_)[A-Za-z0-9_]*\b|"
            r"\bIntel::mkl(?:_[A-Za-z0-9_]+)?\b"
        ),
        allowed_prefixes=("libs/numerics/",),
    ),
    Rule(
        name="ipp-outside-numerics",
        description="IPP calls should be hidden behind libs/numerics backends.",
        pattern=re.compile(
            r'#\s*include\s*[<"]ipp[^>"]*|'
            r"\b(?:Ipp[A-Za-z0-9_]*|ipp[iscm][A-Za-z0-9_]*|Intel::ipp[A-Za-z0-9_]*)\b"
        ),
        allowed_prefixes=("libs/numerics/",),
    ),
    Rule(
        name="opencv-outside-numerics",
        description="OpenCV calls should be hidden behind libs/numerics image backends.",
        pattern=re.compile(
            r'#\s*include\s*[<"]opencv[^>"]*|'
            r"\bcv::|"
            r"\bOpenCV::[A-Za-z0-9_]+\b|"
            r"\bfind_package\s*\(\s*OpenCV\b"
        ),
        allowed_prefixes=("libs/numerics/",),
    ),
    Rule(
        name="itk-outside-numerics",
        description="ITK calls should be hidden behind libs/numerics image backends.",
        pattern=re.compile(
            r'#\s*include\s*[<"]itk[^>"]*|'
            r"\bitk::|"
            r"\bITK::[A-Za-z0-9_]+\b|"
            r"\bfind_package\s*\(\s*ITK\b"
        ),
        allowed_prefixes=("libs/numerics/",),
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Report direct use of legacy kspacejet-math and backend numeric libraries outside libs/numerics."
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path.cwd(),
        help="Repository root. Defaults to the current working directory.",
    )
    parser.add_argument(
        "--path-prefix",
        action="append",
        default=[],
        help="Only scan this project-relative path prefix. Can be repeated.",
    )
    parser.add_argument(
        "--report",
        type=Path,
        default=None,
        help="Optional Markdown report path.",
    )
    parser.add_argument(
        "--show-limit",
        type=int,
        default=30,
        help="Number of findings to print per rule on stdout. Use 0 to print all.",
    )
    parser.add_argument(
        "--fail-on-violation",
        action="store_true",
        help="Return a non-zero exit code when any violation is found.",
    )
    return parser.parse_args()


def normalize_prefix(prefix: str) -> str:
    return prefix.strip().strip("/").replace("\\", "/")


def relative_path(path: Path, root: Path) -> Path:
    try:
        return path.relative_to(root)
    except ValueError:
        return path


def relative_text(path: Path, root: Path) -> str:
    return relative_path(path, root).as_posix()


def is_scanned_file(path: Path) -> bool:
    return path.name in SOURCE_NAMES or path.suffix in SOURCE_SUFFIXES


def is_allowed(rule: Rule, rel_path: str) -> bool:
    return bool(rule.allowed_prefixes) and any(rel_path.startswith(prefix) for prefix in rule.allowed_prefixes)


def is_selected(rel_path: str, prefixes: list[str]) -> bool:
    if not prefixes:
        return True
    return any(rel_path == prefix or rel_path.startswith(prefix.rstrip("/") + "/") for prefix in prefixes)


def iter_source_files(root: Path, prefixes: list[str]) -> Iterable[Path]:
    for directory_text, dir_names, file_names in os.walk(root):
        dir_names[:] = [
            name
            for name in dir_names
            if name not in DEFAULT_EXCLUDED_DIRS and not name.startswith("cmake-build-")
        ]

        directory = Path(directory_text)
        for file_name in file_names:
            path = directory / file_name
            if not is_scanned_file(path):
                continue

            rel_path = relative_text(path, root)
            if is_selected(rel_path, prefixes):
                yield path


def scan_file(path: Path, root: Path) -> list[Violation]:
    rel_path = relative_text(path, root)
    violations: list[Violation] = []

    try:
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    except OSError as error:
        print(f"warning: cannot read {rel_path}: {error}", file=sys.stderr)
        return violations

    for line_number, line in enumerate(lines, start=1):
        for rule in RULES:
            if is_allowed(rule, rel_path):
                continue
            if rule.pattern.search(line):
                violations.append(
                    Violation(
                        rule_name=rule.name,
                        description=rule.description,
                        path=relative_path(path, root),
                        line=line_number,
                        text=line.strip(),
                    )
                )

    return violations


def group_violations(violations: list[Violation]) -> dict[str, list[Violation]]:
    grouped: dict[str, list[Violation]] = {rule.name: [] for rule in RULES}
    for violation in violations:
        grouped[violation.rule_name].append(violation)
    return grouped


def render_markdown(violations: list[Violation]) -> str:
    grouped = group_violations(violations)
    lines = [
        "# Numeric Dependency Boundary Report",
        "",
        "This report lists direct dependencies that should be migrated behind `libs/numerics` or removed.",
        "",
        f"Total findings: {len(violations)}",
        "",
    ]

    for rule in RULES:
        findings = grouped[rule.name]
        lines.extend(
            [
                f"## {rule.name}",
                "",
                rule.description,
                "",
                f"Findings: {len(findings)}",
                "",
            ]
        )
        for finding in findings:
            lines.append(f"- `{finding.path.as_posix()}:{finding.line}`: `{finding.text}`")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def print_summary(violations: list[Violation], show_limit: int) -> None:
    grouped = group_violations(violations)
    print("Numeric dependency boundary scan")
    print(f"Total findings: {len(violations)}")

    for rule in RULES:
        findings = grouped[rule.name]
        print(f"\n{rule.name}: {len(findings)}")
        if not findings:
            continue
        print(f"  {rule.description}")

        visible_findings = findings if show_limit == 0 else findings[:show_limit]
        for finding in visible_findings:
            print(f"  {finding.path.as_posix()}:{finding.line}: {finding.text}")

        hidden_count = len(findings) - len(visible_findings)
        if hidden_count > 0:
            print(f"  ... {hidden_count} more finding(s)")


def main() -> int:
    args = parse_args()
    project_root = args.project_root.resolve(strict=False)
    prefixes = [normalize_prefix(prefix) for prefix in args.path_prefix if normalize_prefix(prefix)]

    violations: list[Violation] = []
    for path in iter_source_files(project_root, prefixes):
        violations.extend(scan_file(path, project_root))

    violations.sort(key=lambda item: (item.rule_name, item.path.as_posix(), item.line))
    print_summary(violations, args.show_limit)

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(render_markdown(violations), encoding="utf-8")
        print(f"\nReport written to {args.report}")

    if args.fail_on_violation and violations:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
