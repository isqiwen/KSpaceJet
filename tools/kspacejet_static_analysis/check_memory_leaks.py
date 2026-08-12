#!/usr/bin/env python3
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import concurrent.futures
from dataclasses import dataclass
import datetime as dt
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


CHECKERS = (
    "cplusplus.NewDelete",
    "cplusplus.NewDeleteLeaks",
    "unix.Malloc",
    "unix.MallocSizeof",
    "unix.MismatchedDeallocator",
)

DISABLED_CHECKERS = (
    "core",
    "deadcode",
    "unix.Stream",
    "unix.StdCLibraryFunctions",
)

MEMORY_CHECKER_MARKERS = tuple(f"[{checker}]" for checker in CHECKERS)

ANALYZER_ERROR_MARKERS = (
    " error:",
    "fatal error:",
    "too many errors emitted",
)

WARNING_RE = re.compile(
    r"^(?P<file>.*?):(?P<line>\d+):(?P<column>\d+): warning: (?P<message>.*?) \[(?P<checker>[^\]]+)\]$"
)

ERROR_RE = re.compile(r"^(?P<file>.*?):(?P<line>\d+):(?P<column>\d+): (?P<kind>fatal error|error): (?P<message>.*)$")

VARIABLE_RE = re.compile(r"pointed to by '([^']+)'")

OWNERSHIP_TRANSFER_MARKERS = (
    "I_StartThread",
    "StartThread",
    "CBAT_NetPost",
    "NetPost",
    "RunNet",
    "callback",
    "Callback",
)

HIGH_CONFIDENCE_CHECKERS = {
    "cplusplus.NewDelete",
    "unix.Malloc",
    "unix.MallocSizeof",
    "unix.MismatchedDeallocator",
}

CONFIDENCE_ORDER = {
    "High confidence": 0,
    "Likely leak": 1,
    "Needs review": 2,
}

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".c++", ".C"}

OPTIONS_WITH_VALUE = {
    "-MF",
    "-MT",
    "-MQ",
    "-o",
}

OPTIONS_WITH_JOINED_VALUE = (
    "-MF",
    "-MT",
    "-MQ",
    "-o",
)

UNSUPPORTED_CLANG_OPTIONS = {
    "-fdiagnostics-urls=always",
    "-fdiagnostics-urls=never",
    "-Wduplicated-cond",
    "-Wduplicated-branches",
    "-Wlogical-op",
    "-Wuseless-cast",
}


@dataclass(frozen=True)
class AnalyzerWarning:
    file_name: str
    line: int
    column: int
    checker: str
    message: str
    variable: str
    confidence: str
    rationale: str
    primary_source: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run Linux-only Clang static analyzer checks for memory leak risks."
    )
    parser.add_argument("--compile-commands", required=True, type=Path)
    parser.add_argument("--project-root", required=True, type=Path)
    parser.add_argument("--clangxx", default="", help="clang++ executable to use")
    parser.add_argument("--jobs", type=int, default=max(1, min(os.cpu_count() or 1, 8)))
    parser.add_argument(
        "--path-prefix",
        action="append",
        default=[],
        help="Only analyze source files under this project-relative path. Can be repeated.",
    )
    parser.add_argument(
        "--fail-on-analyzer-error",
        action="store_true",
        help="Return a non-zero exit code when Clang cannot analyze a translation unit.",
    )
    parser.add_argument(
        "--report",
        type=Path,
        default=None,
        help="Markdown report path. Defaults to <build>/static_analysis/ksj_static_memory_leak_report.md.",
    )
    return parser.parse_args()


def resolve_clangxx(value: str) -> str:
    if value:
        resolved = shutil.which(value) if not Path(value).is_absolute() else value
        if resolved:
            return str(resolved)
        raise SystemExit(f"clang++ was not found: {value}")

    resolved = shutil.which("clang++")
    if resolved:
        return resolved
    raise SystemExit("clang++ was not found. Install clang to run ksj_static_memory_leak_check.")


def normalize_path(path: Path) -> Path:
    return path.resolve(strict=False)


def should_analyze(file_path: Path, project_root: Path, prefixes: list[str]) -> bool:
    if file_path.suffix not in SOURCE_SUFFIXES:
        return False

    if file_path.name.startswith("cmake_pch"):
        return False

    try:
        relative = file_path.relative_to(project_root)
    except ValueError:
        return False

    if relative.parts and relative.parts[0] == "out":
        return False

    if not prefixes:
        return True

    relative_text = relative.as_posix()
    return any(relative_text == prefix or relative_text.startswith(prefix.rstrip("/") + "/") for prefix in prefixes)


def command_tokens(entry: dict[str, object]) -> list[str]:
    if "arguments" in entry:
        return [str(arg) for arg in entry["arguments"]]  # type: ignore[index]
    return shlex.split(str(entry["command"]))


def strip_compile_only_flags(tokens: list[str]) -> list[str]:
    stripped: list[str] = []
    skip_next = False
    for token in tokens[1:]:
        if skip_next:
            skip_next = False
            continue

        if token in {"-c", "-MD", "-MMD", "-MP"}:
            continue

        if token in OPTIONS_WITH_VALUE:
            skip_next = True
            continue

        if any(token.startswith(option) and token != option for option in OPTIONS_WITH_JOINED_VALUE):
            continue

        if token in UNSUPPORTED_CLANG_OPTIONS or token.startswith("-Werror"):
            continue

        stripped.append(token)
    return stripped


def write_openmp_shim(include_dir: Path) -> None:
    include_dir.mkdir(parents=True, exist_ok=True)
    (include_dir / "omp.h").write_text(
        """#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef int omp_lock_t;
typedef int omp_nest_lock_t;
int omp_get_num_threads(void);
int omp_get_thread_num(void);
int omp_get_max_threads(void);
int omp_get_num_procs(void);
int omp_in_parallel(void);
void omp_set_dynamic(int);
int omp_get_dynamic(void);
void omp_set_nested(int);
int omp_get_nested(void);
void omp_set_num_threads(int);
double omp_get_wtime(void);
double omp_get_wtick(void);
void omp_init_lock(omp_lock_t*);
void omp_destroy_lock(omp_lock_t*);
void omp_set_lock(omp_lock_t*);
void omp_unset_lock(omp_lock_t*);
int omp_test_lock(omp_lock_t*);
#ifdef __cplusplus
}
#endif
""",
        encoding="utf-8",
    )


def analyzer_command(clangxx: str, entry: dict[str, object], openmp_include_dir: Path) -> list[str]:
    return [
        clangxx,
        "--analyze",
        "-Qunused-arguments",
        "-Wno-c++11-narrowing",
        "-Wno-narrowing",
        "-Wno-error",
        "-Wno-register",
        "-Wno-unknown-warning-option",
        f"-I{openmp_include_dir}",
        "-Xanalyzer",
        "-analyzer-output=text",
        "-Xanalyzer",
        f"-analyzer-checker={','.join(CHECKERS)}",
        "-Xanalyzer",
        f"-analyzer-disable-checker={','.join(DISABLED_CHECKERS)}",
        *strip_compile_only_flags(command_tokens(entry)),
    ]


def run_one(clangxx: str, entry: dict[str, object], openmp_include_dir: Path) -> tuple[int, str, str]:
    file_name = str(entry["file"])
    command = analyzer_command(clangxx, entry, openmp_include_dir)
    completed = subprocess.run(
        command,
        cwd=str(entry.get("directory", ".")),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    return completed.returncode, file_name, completed.stdout + completed.stderr


def has_memory_finding(output: str) -> bool:
    return any(marker in output for marker in MEMORY_CHECKER_MARKERS)


def has_analyzer_error(returncode: int, output: str) -> bool:
    if returncode != 0:
        return True
    return any(marker in output for marker in ANALYZER_ERROR_MARKERS)


def summarize_output(output: str, max_lines: int = 50) -> str:
    lines = output.rstrip().splitlines()
    if len(lines) <= max_lines:
        return "\n".join(lines)
    clipped = "\n".join(lines[:max_lines])
    return f"{clipped}\n... clipped {len(lines) - max_lines} additional diagnostic lines ..."


def default_report_path(compile_commands_path: Path) -> Path:
    return compile_commands_path.parent / "static_analysis" / "ksj_static_memory_leak_report.md"


def display_path(file_name: str, project_root: Path) -> str:
    path = normalize_path(Path(file_name))
    try:
        return path.relative_to(project_root).as_posix()
    except ValueError:
        return str(path)


def markdown_cell(value: object) -> str:
    text = str(value)
    return text.replace("|", "\\|").replace("\n", " ")


def first_source_line(block: list[str]) -> str:
    for line in block[1:]:
        if " | " in line and "note:" not in line and "warning:" not in line:
            return line.split(" | ", 1)[1].strip()
    return ""


def classify_warning(checker: str, block_text: str) -> tuple[str, str]:
    if checker in HIGH_CONFIDENCE_CHECKERS:
        return "High confidence", "Checker reports a concrete allocator/deallocator misuse or malloc ownership risk."

    if any(marker in block_text for marker in OWNERSHIP_TRANSFER_MARKERS):
        return "Needs review", "Trace contains thread/API handoff markers, so ownership transfer must be checked manually."

    return "Likely leak", "Clang reports a leaked allocation and no obvious ownership handoff marker was found in the trace."


def warning_variable(message: str) -> str:
    match = VARIABLE_RE.search(message)
    return match.group(1) if match else ""


def parse_memory_warnings(memory_findings: list[tuple[str, str]]) -> list[AnalyzerWarning]:
    warnings: list[AnalyzerWarning] = []
    for fallback_file_name, output in memory_findings:
        lines = output.splitlines()
        index = 0
        while index < len(lines):
            match = WARNING_RE.match(lines[index])
            if not match:
                index += 1
                continue

            block = [lines[index]]
            next_index = index + 1
            while next_index < len(lines) and not WARNING_RE.match(lines[next_index]):
                block.append(lines[next_index])
                next_index += 1

            checker = match.group("checker")
            block_text = "\n".join(block)
            confidence, rationale = classify_warning(checker, block_text)
            warnings.append(
                AnalyzerWarning(
                    file_name=match.group("file") or fallback_file_name,
                    line=int(match.group("line")),
                    column=int(match.group("column")),
                    checker=checker,
                    message=match.group("message"),
                    variable=warning_variable(match.group("message")),
                    confidence=confidence,
                    rationale=rationale,
                    primary_source=first_source_line(block),
                )
            )
            index = next_index
    return warnings


def first_analyzer_error(output: str) -> str:
    for line in output.splitlines():
        match = ERROR_RE.match(line)
        if match:
            return f"{match.group('line')}: {match.group('message')}"
    for marker in ANALYZER_ERROR_MARKERS:
        for line in output.splitlines():
            if marker in line:
                return line.strip()
    return "Analyzer did not provide a parseable first error."


def confidence_sort_key(value: str) -> int:
    return CONFIDENCE_ORDER.get(value, len(CONFIDENCE_ORDER))


def strongest_confidence(warnings: list[AnalyzerWarning]) -> str:
    return min((warning.confidence for warning in warnings), key=confidence_sort_key, default="")


def markdown_table(headers: list[str], rows: list[list[object]]) -> list[str]:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    lines.extend("| " + " | ".join(markdown_cell(cell) for cell in row) + " |" for row in rows)
    return lines


def markdown_finding_summary(
    warnings: list[AnalyzerWarning],
    analyzer_errors: list[tuple[str, str]],
    project_root: Path,
) -> list[str]:
    lines = ["## Finding Summary", ""]
    if not warnings:
        lines.extend(["No memory warnings were reported.", ""])
    else:
        confidence_counts = Counter(warning.confidence for warning in warnings)
        confidence_meaning = {
            "High confidence": "Concrete allocator/deallocator mismatch or malloc ownership risk.",
            "Likely leak": "Leak reported without an obvious ownership handoff marker in the trace.",
            "Needs review": "Possible ownership transfer through a thread/API/callback path.",
        }
        lines.extend(["### Warnings By Confidence", ""])
        lines.extend(
            markdown_table(
                ["Confidence", "Warnings", "Meaning"],
                [
                    [confidence, confidence_counts[confidence], confidence_meaning[confidence]]
                    for confidence in sorted(confidence_counts, key=confidence_sort_key)
                ],
            )
        )
        lines.append("")

        checker_counts = Counter(warning.checker for warning in warnings)
        checker_confidence: dict[str, set[str]] = defaultdict(set)
        for warning in warnings:
            checker_confidence[warning.checker].add(warning.confidence)
        lines.extend(["### Warnings By Checker", ""])
        lines.extend(
            markdown_table(
                ["Checker", "Warnings", "Confidence"],
                [
                    [
                        checker,
                        count,
                        ", ".join(sorted(checker_confidence[checker], key=confidence_sort_key)),
                    ]
                    for checker, count in sorted(checker_counts.items())
                ],
            )
        )
        lines.append("")

        by_file: dict[str, list[AnalyzerWarning]] = defaultdict(list)
        for warning in warnings:
            by_file[display_path(warning.file_name, project_root)].append(warning)
        lines.extend(["### Files With Memory Warnings", ""])
        lines.extend(
            markdown_table(
                ["File", "Warnings", "Confidence", "Checkers", "Lines"],
                [
                    [
                        f"`{file_name}`",
                        len(file_warnings),
                        strongest_confidence(file_warnings),
                        ", ".join(sorted({warning.checker for warning in file_warnings})),
                        ", ".join(str(warning.line) for warning in sorted(file_warnings, key=lambda item: item.line)),
                    ]
                    for file_name, file_warnings in sorted(by_file.items())
                ],
            )
        )
        lines.append("")

        lines.extend(["### Warning Index", ""])
        lines.extend(
            markdown_table(
                ["File", "Line", "Checker", "Confidence", "Variable", "Message"],
                [
                    [
                        f"`{display_path(warning.file_name, project_root)}`",
                        warning.line,
                        warning.checker,
                        warning.confidence,
                        warning.variable or "-",
                        warning.message,
                    ]
                    for warning in sorted(
                        warnings,
                        key=lambda item: (display_path(item.file_name, project_root), item.line, item.column),
                    )
                ],
            )
        )
        lines.append("")

    lines.extend(["### Analyzer Coverage Gaps", ""])
    if not analyzer_errors:
        lines.extend(["None.", ""])
        return lines

    lines.extend(
        [
            "These translation units could not be fully analyzed. They are coverage gaps, not memory findings.",
            "",
        ]
    )
    lines.extend(
        markdown_table(
            ["File", "First analyzer error"],
            [
                [f"`{display_path(file_name, project_root)}`", first_analyzer_error(output)]
                for file_name, output in sorted(analyzer_errors)
            ],
        )
    )
    lines.append("")
    return lines


def markdown_diagnostic_section(
    title: str,
    items: list[tuple[str, str]],
    project_root: Path,
    max_lines: int,
) -> list[str]:
    lines = [f"## {title}", ""]
    if not items:
        lines.extend(["None.", ""])
        return lines

    for index, (file_name, output) in enumerate(sorted(items), start=1):
        lines.extend(
            [
                f"### {index}. `{display_path(file_name, project_root)}`",
                "",
                "```text",
                summarize_output(output, max_lines=max_lines),
                "```",
                "",
            ]
        )
    return lines


def write_markdown_report(
    report_path: Path,
    *,
    project_root: Path,
    compile_commands_path: Path,
    clangxx: str,
    jobs: int,
    selected_count: int,
    skipped_count: int,
    memory_findings: list[tuple[str, str]],
    analyzer_errors: list[tuple[str, str]],
    ignored_diagnostics: list[tuple[str, str]],
    exit_code: int,
) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    status = "FAILED" if exit_code != 0 else "PASSED"
    generated_at = dt.datetime.now(dt.timezone.utc).astimezone().isoformat(timespec="seconds")
    warnings = parse_memory_warnings(memory_findings)
    lines = [
        "# KSpaceJet Static Memory Leak Analysis",
        "",
        f"- Generated: `{generated_at}`",
        f"- Status: `{status}`",
        f"- Project root: `{project_root}`",
        f"- Compile commands: `{compile_commands_path}`",
        f"- Clang: `{clangxx}`",
        f"- Jobs: `{jobs}`",
        f"- Translation units analyzed: `{selected_count}`",
        f"- Generated/non-source entries skipped: `{skipped_count}`",
        f"- Memory finding files: `{len(memory_findings)}`",
        f"- Memory warnings: `{len(warnings)}`",
        f"- Analyzer-incompatible translation units: `{len(analyzer_errors)}`",
        f"- Ignored non-memory diagnostics: `{len(ignored_diagnostics)}`",
        f"- Enabled checkers: `{', '.join(CHECKERS)}`",
        f"- Disabled checkers: `{', '.join(DISABLED_CHECKERS)}`",
        "",
    ]
    lines.extend(markdown_finding_summary(warnings, analyzer_errors, project_root))
    lines.extend(markdown_diagnostic_section("Memory Findings", memory_findings, project_root, max_lines=200))
    lines.extend(
        markdown_diagnostic_section(
            "Analyzer-Incompatible Translation Units",
            analyzer_errors,
            project_root,
            max_lines=120,
        )
    )
    lines.extend(
        markdown_diagnostic_section(
            "Ignored Non-Memory Diagnostics",
            ignored_diagnostics,
            project_root,
            max_lines=80,
        )
    )
    report_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    args = parse_args()
    if platform.system() != "Linux":
        print("ksj_static_memory_leak_check is Linux-only; skipping on this platform.")
        return 0

    compile_commands_path = normalize_path(args.compile_commands)
    if not compile_commands_path.exists():
        print(f"compile_commands.json was not found: {compile_commands_path}", file=sys.stderr)
        return 2

    project_root = normalize_path(args.project_root)
    clangxx = resolve_clangxx(args.clangxx)
    report_path = normalize_path(args.report) if args.report is not None else default_report_path(compile_commands_path)

    entries = json.loads(compile_commands_path.read_text(encoding="utf-8"))
    selected = [
        entry
        for entry in entries
        if should_analyze(normalize_path(Path(str(entry["file"]))), project_root, args.path_prefix)
    ]
    if not selected:
        print("No compile commands matched the requested source filters.")
        write_markdown_report(
            report_path,
            project_root=project_root,
            compile_commands_path=compile_commands_path,
            clangxx=clangxx,
            jobs=args.jobs,
            selected_count=0,
            skipped_count=len(entries),
            memory_findings=[],
            analyzer_errors=[],
            ignored_diagnostics=[],
            exit_code=0,
        )
        print(f"Markdown report written to: {report_path}")
        return 0

    skipped_count = len(entries) - len(selected)
    print(
        f"Running Clang static analyzer memory checks on {len(selected)} translation units "
        f"with {args.jobs} jobs. Skipped {skipped_count} generated/non-source entries."
    )

    memory_findings: list[tuple[str, str]] = []
    analyzer_errors: list[tuple[str, str]] = []
    ignored_diagnostics: list[tuple[str, str]] = []
    with tempfile.TemporaryDirectory(prefix="kspacejet-static-analysis-") as temporary_dir:
        openmp_include_dir = Path(temporary_dir) / "include"
        write_openmp_shim(openmp_include_dir)

        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
            futures = [executor.submit(run_one, clangxx, entry, openmp_include_dir) for entry in selected]
            for future in concurrent.futures.as_completed(futures):
                returncode, file_name, output = future.result()
                if has_memory_finding(output):
                    memory_findings.append((file_name, output))
                elif has_analyzer_error(returncode, output):
                    analyzer_errors.append((file_name, output))
                elif output.strip():
                    ignored_diagnostics.append((file_name, output))

    exit_code = 1 if memory_findings or (args.fail_on_analyzer_error and analyzer_errors) else 0

    write_markdown_report(
        report_path,
        project_root=project_root,
        compile_commands_path=compile_commands_path,
        clangxx=clangxx,
        jobs=args.jobs,
        selected_count=len(selected),
        skipped_count=skipped_count,
        memory_findings=memory_findings,
        analyzer_errors=analyzer_errors,
        ignored_diagnostics=ignored_diagnostics,
        exit_code=exit_code,
    )
    print(f"Markdown report written to: {report_path}")

    if memory_findings:
        print("\nClang static analyzer reported memory issues:\n", file=sys.stderr)
        for file_name, output in sorted(memory_findings):
            print(f"==== {file_name} ====", file=sys.stderr)
            print(summarize_output(output), file=sys.stderr)
            print(file=sys.stderr)
        return exit_code

    if analyzer_errors:
        print(
            "\nSkipped analyzer-incompatible translation units "
            f"({len(analyzer_errors)}). These are compile/analyzer setup errors, not memory findings."
        )
        for file_name, output in sorted(analyzer_errors):
            print(f"==== {file_name} ====")
            print(summarize_output(output))
            print()

        if args.fail_on_analyzer_error:
            return exit_code

    if ignored_diagnostics:
        print(
            "\nIgnored non-memory analyzer diagnostics "
            f"({len(ignored_diagnostics)}). Re-run with a narrower path-prefix if these need inspection."
        )

    print("No memory leak findings reported by Clang static analyzer.")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
