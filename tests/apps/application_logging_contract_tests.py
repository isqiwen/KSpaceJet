#!/usr/bin/env python3
"""Keep every product application on the core diagnostic logging boundary."""

from __future__ import annotations

import pathlib
import re
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    project_root = pathlib.Path(__file__).resolve().parents[2]
    apps_root = project_root / "apps"
    app_cmake = (apps_root / "CMakeLists.txt").read_text(encoding="utf-8")
    require("KSpaceJet::core" in app_cmake, "ksj_add_application no longer links every application to KSpaceJet::core")

    entry_points = {
        "ksj": apps_root / "kspacejet-cli" / "main.cpp",
        "ksj-gateway": apps_root / "kspacejet-gateway" / "main.cpp",
        "ksj-recon": apps_root / "kspacejet-recon" / "main.cpp",
        "ksj-research": apps_root / "kspacejet-research" / "main.cpp",
        "ksj-viewer": apps_root / "kspacejet-viewer" / "main.cpp",
    }
    for program, entry_point in entry_points.items():
        source = entry_point.read_text(encoding="utf-8")
        require(
            '#include "kspacejet/logging/logging.hpp"' in source,
            f"{program} does not include the core logging API",
        )
        require(
            "ConfigureDefaultConsole" in source,
            f"{program} does not initialize the core fallback diagnostic logger",
        )
        require("KSJ_LOG_INFO" in source, f"{program} does not record process startup through the core logger")

    viewer_entry = entry_points["ksj-viewer"].read_text(encoding="utf-8")
    require(
        "qInstallMessageHandler(log_qt_message)" in viewer_entry,
        "ksj-viewer does not relay Qt framework diagnostics to the core logger",
    )
    viewer_window = (apps_root / "kspacejet-viewer" / "src" / "viewer_window.cpp").read_text(encoding="utf-8")
    require(
        '"kspacejet/logging/logging.hpp"' in viewer_window and "show_viewer_warning" in viewer_window,
        "ksj-viewer does not record its GUI warning paths through the core logger",
    )

    prohibited_direct_logging = {
        "spdlog": re.compile(r"(?:#\s*include\s*[<\"]spdlog/|\bspdlog::)"),
        "Qt logging": re.compile(r"\bq(?:Debug|Info|Warning|Critical|Fatal)\s*\("),
        "C stdio logging": re.compile(r"\b(?:printf|fprintf|perror|puts)\s*\("),
        "direct stderr": re.compile(r"\bstd::cerr\b"),
        "syslog": re.compile(r"\bsyslog\s*\("),
        "Qt message logger": re.compile(r"\bQMessageLogger\b"),
    }
    source_files = sorted(
        path
        for extension in ("*.c", "*.cc", "*.cpp", "*.cxx", "*.h", "*.hpp", "*.hxx")
        for path in apps_root.rglob(extension)
    )
    for source_file in source_files:
        source = source_file.read_text(encoding="utf-8")
        for description, pattern in prohibited_direct_logging.items():
            require(
                pattern.search(source) is None,
                f"{source_file.relative_to(project_root)} bypasses core logging through {description}",
            )

    print("application logging contract passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"application logging contract test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
