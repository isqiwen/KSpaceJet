#!/usr/bin/env python3
"""Validate and render the derived dashboard for the canonical execution plan.

The task ledger in section 12 of
``docs/architecture/KSpaceJet_project_plan_and_acceptance.md`` is the only
state authority.  This tool deliberately does not invent another task store:
it parses that section, validates its small state machine, and maintains the
marker-delimited dashboard elsewhere in the same document.

Only the Python standard library is used so the checker can run in local hooks
and CI before project-specific environments are available.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from datetime import date
import difflib
from pathlib import Path
import re
import sys
import tempfile
import unittest


PLAN_RELATIVE_PATH = Path("docs/architecture/KSpaceJet_project_plan_and_acceptance.md")
DASHBOARD_BEGIN = "<!-- KSJ-PLAN-DASHBOARD:BEGIN -->"
DASHBOARD_END = "<!-- KSJ-PLAN-DASHBOARD:END -->"
PLAN_ENTRYPOINTS = (
    (Path("README.md"), (PLAN_RELATIVE_PATH.as_posix(),)),
    (
        Path("docs/README.md"),
        (Path("architecture/KSpaceJet_project_plan_and_acceptance.md").as_posix(),),
    ),
    (Path("AGENTS.md"), (PLAN_RELATIVE_PATH.as_posix(),)),
)

ALLOWED_STATUSES = (
    "PLANNED",
    "READY",
    "IN_PROGRESS",
    "VERIFYING",
    "ACCEPTED",
    "BLOCKED",
    "NOT_APPLICABLE",
    "SUPERSEDED",
    "REOPENED",
)
ALLOWED_STATUS_SET = frozenset(ALLOWED_STATUSES)
ACTIVE_STATUSES = frozenset({"IN_PROGRESS", "VERIFYING"})
NOT_APPLICABLE_STATUSES = frozenset({"NOT_APPLICABLE", "SUPERSEDED"})
DISPLAY_STATUS_ORDER = (
    "ACCEPTED",
    "IN_PROGRESS",
    "VERIFYING",
    "READY",
    "BLOCKED",
    "REOPENED",
    "PLANNED",
    "NOT_APPLICABLE",
    "SUPERSEDED",
)
PHASES = tuple(f"P{number}" for number in range(8))
PHASE_NAMES = {
    "P0": "规范、基线和工程治理",
    "P1": "可信离线 reference 基线",
    "P2": "图、artifact、compiler、verifier 和 CLI 计划工具",
    "P3": "有界 generic CPU runtime",
    "P4": "Provider 产品化",
    "P5": "可选进程内 ISMRMRD feed 与宿主 API",
    "P6": "并行、NUMA、GPU 与性能",
    "P7": "Qualification、CI、安装、供应链和发布",
}

SECTION_HEADING_PATTERN = re.compile(r"^##[ \t]+12\.(?:[ \t]+.*)?$")
CATALOG_SECTION_HEADING_PATTERN = re.compile(r"^##[ \t]+10\.(?:[ \t]+.*)?$")
NEXT_SECTION_PATTERN = re.compile(r"^##(?:[ \t]|$)")
TASK_ROW_PATTERN = re.compile(
    r"^[ \t]*\|[ \t]*(?P<identifier>P(?P<phase>[0-9]+)-(?P<number>[0-9]{3}))"
    r"[ \t]*\|[ \t]*(?P<status>[^|]*)[ \t]*\|"
)
DEPENDENCY_ID_PATTERN = re.compile(r"P[0-7]-[0-9]{3}")
DEPENDENCY_RANGE_PATTERN = re.compile(
    r"P(?P<phase>[0-7])-(?P<first>[0-9]{3})[ \t]*至[ \t]*"
    r"P(?P=phase)-(?P<last>[0-9]{3})"
)
EVIDENCE_HEADING_PATTERN = re.compile(
    r"^###[ \t]+(?P<section>13[.][0-9]+)[ \t]+"
    r"(?P<identifier>P[0-7]-[0-9]{3})[ \t]+"
    r"(?P<status>ACCEPTED|BLOCKED|REOPENED)[ \t]+证据[ \t]*$"
)
LEDGER_DATE_PATTERN = re.compile(
    r"^\*\*更新时间\*\*[：:][ \t]*(?P<value>[0-9]{4}-[0-9]{2}-[0-9]{2})\b"
)


@dataclass(frozen=True)
class Task:
    """One task row declared in the canonical ledger."""

    identifier: str
    phase: str
    status: str
    line: int
    next_action: str = "—"
    dependencies: tuple[str, ...] = ()


@dataclass(frozen=True)
class EvidenceRecord:
    """A task evidence heading that can be linked from the derived dashboard."""

    identifier: str
    status: str
    section: str
    heading: str
    line: int


@dataclass(frozen=True)
class ParsedLedger:
    """The validated information from section 12 that drives the dashboard."""

    path: Path
    lines: tuple[str, ...]
    section_start: int
    section_end: int
    ledger_date: str
    tasks: tuple[Task, ...]
    evidence_records: tuple[EvidenceRecord, ...]


@dataclass(frozen=True)
class Diagnostic:
    """A human-actionable validation error with an optional one-based line."""

    path: Path
    message: str
    line: int | None = None

    def render(self, project_root: Path) -> str:
        try:
            display = self.path.relative_to(project_root).as_posix()
        except ValueError:
            display = self.path.as_posix()
        location = f"{display}:{self.line}" if self.line is not None else display
        return f"{location}: error: {self.message}"


def validate_plan_entrypoints(project_root: Path) -> tuple[Diagnostic, ...]:
    """Ensure the repository's three front doors keep the canonical-plan route."""

    diagnostics: list[Diagnostic] = []
    for relative_path, accepted_targets in PLAN_ENTRYPOINTS:
        path = project_root / relative_path
        try:
            content = path.read_text(encoding="utf-8")
        except OSError as error:
            diagnostics.append(Diagnostic(path, f"cannot read canonical-plan entrypoint: {error}"))
            continue
        if not any(target in content for target in accepted_targets):
            expected = " or ".join(f"'{target}'" for target in accepted_targets)
            diagnostics.append(
                Diagnostic(
                    path,
                    f"missing canonical-plan reference; expected {expected}",
                )
            )
    return tuple(diagnostics)


def find_section(lines: tuple[str, ...], path: Path) -> tuple[int, int] | Diagnostic:
    """Return the zero-based bounds of the level-two section numbered 12."""

    starts = [index for index, line in enumerate(lines) if SECTION_HEADING_PATTERN.match(line)]
    if not starts:
        return Diagnostic(path, "missing level-two heading '## 12. ...'")
    if len(starts) > 1:
        return Diagnostic(
            path,
            "multiple level-two section-12 headings; exactly one canonical ledger is required",
            starts[1] + 1,
        )

    start = starts[0]
    for index in range(start + 1, len(lines)):
        if NEXT_SECTION_PATTERN.match(lines[index]):
            return start, index
    return start, len(lines)


def parse_catalog_task_ids(
    lines: tuple[str, ...], path: Path
) -> tuple[frozenset[str], tuple[Diagnostic, ...]]:
    """Read the static section-10 work-item directory for ledger completeness."""

    starts = [index for index, line in enumerate(lines) if CATALOG_SECTION_HEADING_PATTERN.match(line)]
    if not starts:
        return frozenset(), (Diagnostic(path, "missing level-two heading '## 10. ...'"),)
    if len(starts) > 1:
        return frozenset(), (
            Diagnostic(
                path,
                "multiple level-two section-10 headings; exactly one work-item directory is required",
                starts[1] + 1,
            ),
        )

    start = starts[0]
    end = next(
        (
            index
            for index in range(start + 1, len(lines))
            if NEXT_SECTION_PATTERN.match(lines[index])
        ),
        len(lines),
    )
    identifiers: set[str] = set()
    diagnostics: list[Diagnostic] = []
    for index in range(start + 1, end):
        match = TASK_ROW_PATTERN.match(lines[index])
        if match is None:
            continue
        identifier = match.group("identifier")
        if identifier in identifiers:
            diagnostics.append(
                Diagnostic(
                    path,
                    f"duplicate work item {identifier} in section 10 directory",
                    index + 1,
                )
            )
            continue
        identifiers.add(identifier)

    if not identifiers:
        diagnostics.append(
            Diagnostic(
                path,
                "section 10 contains no task rows matching '| P<phase>-<3 digits> | ... |'",
                start + 1,
            )
        )
    return frozenset(identifiers), tuple(diagnostics)


def dependency_ids(value: str) -> tuple[str, ...]:
    """Extract explicit task dependencies, expanding same-phase Chinese ranges."""

    identifiers = list(DEPENDENCY_ID_PATTERN.findall(value))
    for match in DEPENDENCY_RANGE_PATTERN.finditer(value):
        first = int(match.group("first"))
        last = int(match.group("last"))
        if first > last:
            continue
        identifiers.extend(
            f"P{match.group('phase')}-{number:03d}" for number in range(first, last + 1)
        )
    return tuple(dict.fromkeys(identifiers))


def validate_dependencies(tasks: list[Task], diagnostics: list[Diagnostic], path: Path) -> None:
    """Check explicit dependencies and prevent impossible READY/active task states."""

    by_identifier = {task.identifier: task for task in tasks}
    ready = [task for task in tasks if task.status == "READY"]
    if len(ready) > 1:
        rendered = ", ".join(f"{task.identifier} (line {task.line})" for task in ready)
        diagnostics.append(
            Diagnostic(
                path,
                "multiple READY work items; the canonical recovery protocol requires one: "
                f"{rendered}",
                ready[1].line,
            )
        )

    for task in tasks:
        missing = [identifier for identifier in task.dependencies if identifier not in by_identifier]
        if missing:
            diagnostics.append(
                Diagnostic(
                    path,
                    f"{task.identifier} names dependencies absent from the section-12 ledger: "
                    f"{', '.join(missing)}",
                    task.line,
                )
            )
        if task.status not in ACTIVE_STATUSES and task.status != "READY":
            continue
        unsatisfied = [
            identifier
            for identifier in task.dependencies
            if identifier in by_identifier
            and by_identifier[identifier].status not in {"ACCEPTED", "NOT_APPLICABLE"}
        ]
        if unsatisfied:
            diagnostics.append(
                Diagnostic(
                    path,
                    f"{task.identifier} is {task.status} but explicit dependencies are not "
                    f"ACCEPTED/NOT_APPLICABLE: {', '.join(unsatisfied)}",
                    task.line,
                )
            )


def parse_ledger(path: Path) -> tuple[ParsedLedger | None, tuple[Diagnostic, ...]]:
    """Parse section 12 and report malformed task state without editing it."""

    try:
        lines = tuple(path.read_text(encoding="utf-8").splitlines())
    except OSError as error:
        return None, (Diagnostic(path, f"cannot read canonical plan: {error}"),)

    bounds = find_section(lines, path)
    if isinstance(bounds, Diagnostic):
        return None, (bounds,)
    section_start, section_end = bounds

    diagnostics: list[Diagnostic] = []
    catalog_ids, catalog_diagnostics = parse_catalog_task_ids(lines, path)
    diagnostics.extend(catalog_diagnostics)
    ledger_date: str | None = None
    tasks: list[Task] = []
    first_seen: dict[str, Task] = {}

    for index in range(section_start + 1, section_end):
        line = lines[index]
        date_match = LEDGER_DATE_PATTERN.match(line)
        if date_match is not None:
            if ledger_date is not None:
                diagnostics.append(
                    Diagnostic(
                        path,
                        "multiple '**更新时间**' dates in section 12; keep one ledger date",
                        index + 1,
                    )
                )
            else:
                candidate = date_match.group("value")
                try:
                    date.fromisoformat(candidate)
                except ValueError:
                    diagnostics.append(
                        Diagnostic(path, f"invalid ledger date '{candidate}'", index + 1)
                    )
                else:
                    ledger_date = candidate

        task_match = TASK_ROW_PATTERN.match(line)
        if task_match is None:
            continue

        identifier = task_match.group("identifier")
        phase_number = int(task_match.group("phase"))
        phase = f"P{phase_number}"
        status = task_match.group("status").strip()
        cells = [cell.strip() for cell in line.split("|")]
        next_action = cells[5] if len(cells) > 5 and cells[5] else "—"
        dependencies = dependency_ids(cells[3]) if len(cells) > 3 else ()
        task = Task(
            identifier=identifier,
            phase=phase,
            status=status,
            line=index + 1,
            next_action=next_action,
            dependencies=dependencies,
        )
        tasks.append(task)

        if phase not in PHASES:
            diagnostics.append(
                Diagnostic(
                    path,
                    f"{identifier} is outside supported phases P0 through P7",
                    task.line,
                )
            )
        if status not in ALLOWED_STATUS_SET:
            allowed = ", ".join(ALLOWED_STATUSES)
            diagnostics.append(
                Diagnostic(
                    path,
                    f"{identifier} has unsupported status '{status}'; expected one of: {allowed}",
                    task.line,
                )
            )
        prior = first_seen.get(identifier)
        if prior is not None:
            diagnostics.append(
                Diagnostic(
                    path,
                    f"duplicate work item {identifier}; first declared at line {prior.line}",
                    task.line,
                )
            )
        else:
            first_seen[identifier] = task

    if ledger_date is None:
        diagnostics.append(
            Diagnostic(
                path,
                "missing '**更新时间**：YYYY-MM-DD' metadata in section 12",
                section_start + 1,
            )
        )
    if not tasks:
        diagnostics.append(
            Diagnostic(
                path,
                "section 12 contains no task rows matching '| P<phase>-<3 digits> | STATUS |'",
                section_start + 1,
            )
        )

    active = [task for task in tasks if task.status in ACTIVE_STATUSES]
    if len(active) > 1:
        rendered = ", ".join(
            f"{task.identifier} ({task.status}, line {task.line})" for task in active
        )
        diagnostics.append(
            Diagnostic(
                path,
                "multiple active work items; at most one of IN_PROGRESS/VERIFYING is allowed: "
                f"{rendered}",
                active[1].line,
            )
        )

    ledger_ids = {task.identifier for task in tasks}
    missing_from_ledger = sorted(catalog_ids - ledger_ids)
    undeclared_in_catalog = sorted(ledger_ids - catalog_ids)
    if missing_from_ledger:
        diagnostics.append(
            Diagnostic(
                path,
                "section-10 work items missing from the section-12 ledger: "
                f"{', '.join(missing_from_ledger)}",
                section_start + 1,
            )
        )
    if undeclared_in_catalog:
        diagnostics.append(
            Diagnostic(
                path,
                "section-12 work items absent from the section-10 directory: "
                f"{', '.join(undeclared_in_catalog)}",
                section_start + 1,
            )
        )
    validate_dependencies(tasks, diagnostics, path)

    evidence_records: list[EvidenceRecord] = []
    for index, line in enumerate(lines):
        evidence_match = EVIDENCE_HEADING_PATTERN.match(line)
        if evidence_match is None:
            continue
        evidence_records.append(
            EvidenceRecord(
                identifier=evidence_match.group("identifier"),
                status=evidence_match.group("status"),
                section=evidence_match.group("section"),
                heading=line[4:].strip(),
                line=index + 1,
            )
        )

    if ledger_date is None:
        return None, tuple(diagnostics)
    return (
        ParsedLedger(
            path=path,
            lines=lines,
            section_start=section_start,
            section_end=section_end,
            ledger_date=ledger_date,
            tasks=tuple(tasks),
            evidence_records=tuple(evidence_records),
        ),
        tuple(diagnostics),
    )


def locate_dashboard(lines: tuple[str, ...], path: Path) -> tuple[int, int] | tuple[Diagnostic, ...]:
    """Locate the single dashboard region and diagnose malformed markers."""

    begin_lines = [index for index, line in enumerate(lines) if line.strip() == DASHBOARD_BEGIN]
    end_lines = [index for index, line in enumerate(lines) if line.strip() == DASHBOARD_END]
    diagnostics: list[Diagnostic] = []

    if not begin_lines:
        diagnostics.append(Diagnostic(path, f"missing dashboard begin marker '{DASHBOARD_BEGIN}'"))
    elif len(begin_lines) > 1:
        diagnostics.append(
            Diagnostic(path, "multiple dashboard begin markers; exactly one is required", begin_lines[1] + 1)
        )

    if not end_lines:
        diagnostics.append(Diagnostic(path, f"missing dashboard end marker '{DASHBOARD_END}'"))
    elif len(end_lines) > 1:
        diagnostics.append(
            Diagnostic(path, "multiple dashboard end markers; exactly one is required", end_lines[1] + 1)
        )

    if diagnostics:
        return tuple(diagnostics)

    begin, end = begin_lines[0], end_lines[0]
    if begin >= end:
        return (
            Diagnostic(
                path,
                "dashboard end marker must follow its begin marker",
                end + 1,
            ),
        )
    return begin, end


def format_percentage(numerator: int, denominator: int) -> str:
    """Format a stable, compact percentage without floating-point artifacts."""

    if denominator == 0:
        return "n/a"
    # Round half up to one decimal using integers, avoiding binary-float drift.
    tenths = (numerator * 2000 + denominator) // (2 * denominator)
    whole, fraction = divmod(tenths, 10)
    return f"{whole}%" if fraction == 0 else f"{whole}.{fraction}%"


def execution_state(tasks: tuple[Task, ...]) -> str:
    """Derive the one-line execution state without interpreting dependencies."""

    active = [task for task in tasks if task.status in ACTIVE_STATUSES]
    if active:
        return active[0].status
    if any(task.status == "READY" for task in tasks):
        return "READY"
    if any(task.status == "BLOCKED" for task in tasks):
        return "BLOCKED"
    if any(task.status in {"PLANNED", "REOPENED"} for task in tasks):
        return "PLANNED"
    return "ACCEPTED"


def next_task(tasks: tuple[Task, ...]) -> str | None:
    """Return the unambiguous next item without inventing a priority order."""

    active = [task for task in tasks if task.status in ACTIVE_STATUSES]
    if active:
        return active[0].identifier
    ready = [task.identifier for task in tasks if task.status == "READY"]
    return ready[0] if len(ready) == 1 else None


def status_distribution(tasks: list[Task]) -> str:
    """Return a fixed-order compact distribution, omitting only zero counts."""

    counts = Counter(task.status for task in tasks)
    values = [f"{status}: {counts[status]}" for status in DISPLAY_STATUS_ORDER if counts[status]]
    return " · ".join(values) if values else "—"


def compact_table_text(value: str, limit: int = 132) -> str:
    """Make a ledger cell safe and compact for a dashboard table."""

    compact = " ".join(value.replace("|", "/").split())
    if len(compact) <= limit:
        return compact
    return f"{compact[: limit - 1].rstrip()}…"


def markdown_heading_anchor(heading: str) -> str:
    """Produce the GitHub-style anchor used by the repository's Markdown links."""

    retained = "".join(
        character
        for character in heading.casefold()
        if character.isalnum() or character in {"-", "_", " "}
    )
    return re.sub(r"[\s-]+", "-", retained).strip("-")


def recent_accepted_evidence(ledger: ParsedLedger) -> list[EvidenceRecord]:
    """Return the three latest accepted evidence headings, newest first."""

    accepted = {task.identifier for task in ledger.tasks if task.status == "ACCEPTED"}
    records: list[EvidenceRecord] = []
    seen: set[str] = set()
    for record in reversed(ledger.evidence_records):
        if record.status != "ACCEPTED" or record.identifier not in accepted:
            continue
        if record.identifier in seen:
            continue
        records.append(record)
        seen.add(record.identifier)
        if len(records) == 3:
            break
    return records


def append_yaml_list(lines: list[str], key: str, values: list[str]) -> None:
    """Append an explicit YAML list, including the unambiguous empty-list form."""

    if not values:
        lines.append(f"{key}: []")
        return
    lines.append(f"{key}:")
    lines.extend(f"  - {value}" for value in values)


def render_dashboard(ledger: ParsedLedger) -> str:
    """Render the complete deterministic dashboard body (without markers)."""

    active = [task for task in ledger.tasks if task.status in ACTIVE_STATUSES]
    ready = [task.identifier for task in ledger.tasks if task.status == "READY"]
    blocked = [task.identifier for task in ledger.tasks if task.status == "BLOCKED"]
    accepted = sum(task.status == "ACCEPTED" for task in ledger.tasks)
    applicable = sum(task.status not in NOT_APPLICABLE_STATUSES for task in ledger.tasks)

    lines = [
        "#### 执行进度总览（自动生成）",
        "",
        "> 此区块是 [第 12 节唯一执行台账](#12-唯一执行台账) 的只读投影。"
        "修改任务状态、依赖或证据后，运行 "
        "`python3 tools/checks/check_execution_plan.py --write`；不要手工编辑本区块。",
        "> 覆盖度 = `ACCEPTED / 适用项`，不按工作量加权，也不表示阶段门禁已经通过。",
        "",
        "```yaml",
        f'source: "{PLAN_RELATIVE_PATH.as_posix()}#12-唯一执行台账"',
        f"ledger_date: {ledger.ledger_date}",
        f"execution_state: {execution_state(ledger.tasks)}",
        f"active_phase: {active[0].phase if active else 'null'}",
        f"active_work_item: {active[0].identifier if active else 'null'}",
        f"next_task: {next_task(ledger.tasks) or 'null'}",
    ]
    append_yaml_list(lines, "ready_items", ready)
    append_yaml_list(lines, "blocked_items", blocked)
    lines.extend(
        [
            f"accepted: {accepted}",
            f"applicable: {applicable}",
            f"coverage: {format_percentage(accepted, applicable)}",
            "```",
            "",
            "| 阶段 | 目标 | 已接受 | 适用项 | 覆盖度 | 状态分布 |",
            "| --- | --- | ---: | ---: | ---: | --- |",
        ]
    )

    for phase in PHASES:
        phase_tasks = [task for task in ledger.tasks if task.phase == phase]
        phase_accepted = sum(task.status == "ACCEPTED" for task in phase_tasks)
        phase_applicable = sum(
            task.status not in NOT_APPLICABLE_STATUSES for task in phase_tasks
        )
        lines.append(
            "| "
            f"{phase} | {PHASE_NAMES[phase]} | {phase_accepted} | {phase_applicable} | "
            f"{format_percentage(phase_accepted, phase_applicable)} | "
            f"{status_distribution(phase_tasks)} |"
        )

    evidence = recent_accepted_evidence(ledger)
    if evidence:
        lines.extend(
            [
                "",
                "#### 最近验收证据（自动生成）",
                "",
                "| 工作项 | 证据记录 |",
                "| --- | --- |",
            ]
        )
        for record in evidence:
            anchor = markdown_heading_anchor(record.heading)
            lines.append(
                f"| {record.identifier} | [{record.section} {record.identifier} "
                f"{record.status} 证据](#{anchor}) |"
            )

    if blocked:
        lines.extend(
            [
                "",
                "#### 当前阻塞项（自动生成）",
                "",
                "> 详细依赖、证据和限制以第 12 节为准；下表只显示其下一精确行动。",
                "",
                "| 工作项 | 解锁后动作 |",
                "| --- | --- |",
            ]
        )
        for task in (task for task in ledger.tasks if task.status == "BLOCKED"):
            lines.append(f"| {task.identifier} | {compact_table_text(task.next_action)} |")

    return "\n".join(lines)


def expected_dashboard_region(ledger: ParsedLedger) -> list[str]:
    """Build the exact interior lines controlled by the two dashboard markers."""

    return ["", *render_dashboard(ledger).splitlines(), ""]


def replace_dashboard(lines: tuple[str, ...], begin: int, end: int, ledger: ParsedLedger) -> str:
    """Return the document text with only the marker-controlled region replaced."""

    updated = [*lines[: begin + 1], *expected_dashboard_region(ledger), *lines[end:]]
    return "\n".join(updated) + "\n"


def dashboard_diff(actual: list[str], expected: list[str]) -> str:
    """Produce a short actionable diff for stale generated content."""

    return "\n".join(
        difflib.unified_diff(
            actual,
            expected,
            fromfile="current dashboard",
            tofile="generated dashboard",
            lineterm="",
        )
    )


def check_or_write(project_root: Path, write: bool) -> tuple[bool, str]:
    """Run one normal command invocation and return (success, human output)."""

    plan_path = project_root / PLAN_RELATIVE_PATH
    entrypoint_diagnostics = validate_plan_entrypoints(project_root)
    if entrypoint_diagnostics:
        return False, "\n".join(
            diagnostic.render(project_root) for diagnostic in entrypoint_diagnostics
        )
    ledger, diagnostics = parse_ledger(plan_path)
    if diagnostics:
        return False, "\n".join(diagnostic.render(project_root) for diagnostic in diagnostics)
    assert ledger is not None

    markers = locate_dashboard(ledger.lines, plan_path)
    if isinstance(markers, tuple) and markers and isinstance(markers[0], Diagnostic):
        return False, "\n".join(diagnostic.render(project_root) for diagnostic in markers)
    assert isinstance(markers, tuple)
    begin, end = markers

    expected = expected_dashboard_region(ledger)
    actual = list(ledger.lines[begin + 1 : end])
    if actual == expected:
        return True, (
            f"execution plan dashboard is current: {PLAN_RELATIVE_PATH.as_posix()} "
            f"({len(ledger.tasks)} work items)"
        )

    if not write:
        diff = dashboard_diff(actual, expected)
        instruction = (
            "dashboard is stale; run "
            "'python3 tools/checks/check_execution_plan.py --write' to regenerate it"
        )
        return False, f"{plan_path.relative_to(project_root).as_posix()}: error: {instruction}\n{diff}"

    try:
        plan_path.write_text(replace_dashboard(ledger.lines, begin, end, ledger), encoding="utf-8")
    except OSError as error:
        return False, f"{plan_path.relative_to(project_root).as_posix()}: error: cannot write dashboard: {error}"
    return True, f"updated execution plan dashboard: {PLAN_RELATIVE_PATH.as_posix()}"


def make_test_plan(
    rows: str,
    dashboard: str = "stale",
    evidence: str = "",
    catalog_rows: str | None = None,
) -> str:
    """Build a minimal standalone canonical-plan fixture for the self-test."""

    catalog = rows if catalog_rows is None else catalog_rows
    return "\n".join(
        [
            "# Test plan",
            "",
            DASHBOARD_BEGIN,
            dashboard,
            DASHBOARD_END,
            "",
            "## 10. Work directory",
            "",
            "| ID | Status | Notes |",
            "| --- | --- | --- |",
            catalog,
            "",
            "## 11. Control",
            "",
            "## 12. 唯一执行台账",
            "",
            "**更新时间**：2026-08-20",
            "",
            "| ID | Status | Notes |",
            "| --- | --- | --- |",
            rows,
            "",
            "## 13. Evidence",
            "",
            evidence,
        ]
    )


class ExecutionPlanCheckerTests(unittest.TestCase):
    """Small hermetic checks for parsing, state invariants, and regeneration."""

    def write_fixture(self, root: Path, content: str) -> None:
        plan = root / PLAN_RELATIVE_PATH
        plan.parent.mkdir(parents=True, exist_ok=True)
        plan.write_text(content, encoding="utf-8")
        (root / "README.md").write_text(PLAN_RELATIVE_PATH.as_posix(), encoding="utf-8")
        (root / "AGENTS.md").write_text(PLAN_RELATIVE_PATH.as_posix(), encoding="utf-8")
        (root / "docs/README.md").write_text(
            "architecture/KSpaceJet_project_plan_and_acceptance.md", encoding="utf-8"
        )

    def test_next_task_requires_one_unambiguous_ready_item(self) -> None:
        ready = Task(identifier="P1-001", phase="P1", status="READY", line=1)
        second_ready = Task(identifier="P1-002", phase="P1", status="READY", line=2)
        active = Task(identifier="P0-008", phase="P0", status="IN_PROGRESS", line=3)

        self.assertEqual(next_task((ready,)), "P1-001")
        self.assertIsNone(next_task((ready, second_ready)))
        self.assertEqual(next_task((ready, active)), "P0-008")

    def test_percentage_rounding_is_stable(self) -> None:
        self.assertEqual(format_percentage(1, 6), "16.7%")
        self.assertEqual(format_percentage(3, 8), "37.5%")
        self.assertEqual(format_percentage(0, 0), "n/a")

    def test_write_then_check_renders_progress_dashboard(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(
                root,
                make_test_plan(
                    "\n".join(
                        [
                            "| P0-001 | ACCEPTED | baseline |",
                            "| P0-002 | BLOCKED | host |",
                            "| P1-001 | READY | fixture |",
                            "| P2-001 | IN_PROGRESS | implementation |",
                            "| P3-001 | NOT_APPLICABLE | optional |",
                        ]
                    )
                ),
            )

            updated, update_output = check_or_write(root, write=True)
            self.assertTrue(updated, update_output)
            current, check_output = check_or_write(root, write=False)
            self.assertTrue(current, check_output)

            content = (root / PLAN_RELATIVE_PATH).read_text(encoding="utf-8")
            self.assertIn("execution_state: IN_PROGRESS", content)
            self.assertIn("active_phase: P2", content)
            self.assertIn("active_work_item: P2-001", content)
            self.assertIn("next_task: P2-001", content)
            self.assertIn("  - P1-001", content)
            self.assertIn("  - P0-002", content)
            self.assertIn("accepted: 1", content)
            self.assertIn("applicable: 4", content)
            self.assertIn("coverage: 25%", content)
            self.assertIn(
                "| P0 | 规范、基线和工程治理 | 1 | 2 | 50% | ACCEPTED: 1 · BLOCKED: 1 |",
                content,
            )
            self.assertIn(
                "| P3 | 有界 generic CPU runtime | 0 | 0 | n/a | NOT_APPLICABLE: 1 |",
                content,
            )

    def test_renders_recent_evidence_and_blocker_recovery_action(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(
                root,
                make_test_plan(
                    "\n".join(
                        [
                            "| P0-001 | ACCEPTED | — | baseline | no action | — |",
                            "| P0-002 | BLOCKED | — | host | use a verified host | — |",
                        ]
                    ),
                    evidence="\n".join(
                        [
                            "### 13.1 P0-001 ACCEPTED 证据",
                            "",
                            "Evidence body.",
                        ]
                    ),
                ),
            )

            updated, update_output = check_or_write(root, write=True)
            self.assertTrue(updated, update_output)
            content = (root / PLAN_RELATIVE_PATH).read_text(encoding="utf-8")
            self.assertIn("#### 最近验收证据（自动生成）", content)
            self.assertIn(
                "[13.1 P0-001 ACCEPTED 证据](#131-p0-001-accepted-证据)",
                content,
            )
            self.assertIn("#### 当前阻塞项（自动生成）", content)
            self.assertIn("| P0-002 | use a verified host |", content)

    def test_rejects_catalog_and_ledger_task_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(
                root,
                make_test_plan(
                    "| P0-001 | PLANNED | ledger |",
                    catalog_rows="| P0-002 | planned directory entry |",
                ),
            )
            success, output = check_or_write(root, write=False)
            self.assertFalse(success)
            self.assertIn("section-10 work items missing from the section-12 ledger: P0-002", output)
            self.assertIn("section-12 work items absent from the section-10 directory: P0-001", output)

    def test_rejects_missing_canonical_plan_entrypoint(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(root, make_test_plan("| P0-001 | PLANNED | row |"))
            (root / "README.md").write_text("no plan link", encoding="utf-8")
            success, output = check_or_write(root, write=False)
            self.assertFalse(success)
            self.assertIn("README.md: error: missing canonical-plan reference", output)

    def test_rejects_ready_task_with_unsatisfied_dependency(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(
                root,
                make_test_plan(
                    "\n".join(
                        [
                            "| P0-001 | PLANNED | none |",
                            "| P0-002 | READY | P0-001 | baseline | next | limit |",
                        ]
                    )
                ),
            )
            success, output = check_or_write(root, write=False)
            self.assertFalse(success)
            self.assertIn(
                "P0-002 is READY but explicit dependencies are not ACCEPTED/NOT_APPLICABLE: P0-001",
                output,
            )

    def test_rejects_multiple_ready_work_items(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(
                root,
                make_test_plan(
                    "\n".join(
                        [
                            "| P0-001 | READY | none |",
                            "| P0-002 | READY | none |",
                        ]
                    )
                ),
            )
            success, output = check_or_write(root, write=False)
            self.assertFalse(success)
            self.assertIn("multiple READY work items", output)

    def test_rejects_unsupported_status(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(root, make_test_plan("| P0-001 | DONE | invalid |"))
            success, output = check_or_write(root, write=False)
            self.assertFalse(success)
            self.assertIn("unsupported status 'DONE'", output)

    def test_rejects_duplicate_identifier(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(
                root,
                make_test_plan(
                    "\n".join(
                        [
                            "| P0-001 | PLANNED | first |",
                            "| P0-001 | READY | duplicate |",
                        ]
                    )
                ),
            )
            success, output = check_or_write(root, write=False)
            self.assertFalse(success)
            self.assertIn("duplicate work item P0-001", output)
            self.assertIn("first declared at line", output)

    def test_rejects_multiple_active_items(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(
                root,
                make_test_plan(
                    "\n".join(
                        [
                            "| P0-001 | IN_PROGRESS | first |",
                            "| P0-002 | VERIFYING | second |",
                        ]
                    )
                ),
            )
            success, output = check_or_write(root, write=False)
            self.assertFalse(success)
            self.assertIn("multiple active work items", output)
            self.assertIn("P0-001 (IN_PROGRESS", output)
            self.assertIn("P0-002 (VERIFYING", output)

    def test_rejects_missing_dashboard_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(root, make_test_plan("| P0-001 | READY | row |"))
            plan = root / PLAN_RELATIVE_PATH
            plan.write_text(
                plan.read_text(encoding="utf-8").replace(DASHBOARD_END, ""), encoding="utf-8"
            )
            success, output = check_or_write(root, write=False)
            self.assertFalse(success)
            self.assertIn("missing dashboard end marker", output)

    def test_detects_stale_dashboard_in_check_mode(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(root, make_test_plan("| P0-001 | READY | row |"))
            success, output = check_or_write(root, write=False)
            self.assertFalse(success)
            self.assertIn("dashboard is stale", output)
            self.assertIn("--write", output)


def run_self_test() -> bool:
    """Execute the script's hermetic standard-library test suite."""

    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ExecutionPlanCheckerTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return result.wasSuccessful()


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    """Parse the deliberately small CLI surface."""

    parser = argparse.ArgumentParser(
        description="Validate and regenerate the derived KSpaceJet execution-plan dashboard."
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="repository root containing docs/architecture (default: inferred from this script)",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--check",
        action="store_true",
        help="verify that the dashboard matches section 12 (default)",
    )
    mode.add_argument(
        "--write",
        action="store_true",
        help="replace only the marker-controlled dashboard region",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run the checker's hermetic standard-library test suite",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """Run the command-line checker."""

    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)
    if arguments.self_test:
        return 0 if run_self_test() else 1

    project_root = arguments.project_root.resolve()
    success, output = check_or_write(project_root, write=arguments.write)
    stream = sys.stdout if success else sys.stderr
    print(output, file=stream)
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
