#!/usr/bin/env python3
"""Keep the Windows Viewer executable native-GUI and maximized by default."""

from __future__ import annotations

import pathlib
import re
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    project_root = pathlib.Path(__file__).resolve().parents[2]
    viewer_cmake = (project_root / "apps" / "kspacejet-viewer" / "CMakeLists.txt").read_text(encoding="utf-8")
    viewer_main = (project_root / "apps" / "kspacejet-viewer" / "main.cpp").read_text(encoding="utf-8")

    require(
        re.search(
            r"if\s*\(\s*WIN32\s*\)\s*"
            r"set_target_properties\s*\(\s*ksj_viewer\s+PROPERTIES\s+WIN32_EXECUTABLE\s+TRUE\s*\)\s*"
            r"endif\s*\(\s*\)",
            viewer_cmake,
            flags=re.DOTALL,
        )
        is not None,
        "ksj_viewer is not explicitly built as a Windows GUI-subsystem executable",
    )
    require(
        "CMAKE_WIN32_EXECUTABLE" not in viewer_cmake,
        "the Viewer GUI-subsystem setting must not become a directory-wide default",
    )
    require(
        re.search(
            r"ksj::viewer::ViewerWindow\s+window\s*;\s*window\.showMaximized\s*\(\s*\)\s*;",
            viewer_main,
            flags=re.DOTALL,
        )
        is not None,
        "Viewer startup must maximize the main workbench while retaining native window controls",
    )

    print("viewer Windows GUI-subsystem and maximized-startup contract passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"viewer Windows GUI-subsystem contract test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
