#!/usr/bin/env python3
"""Validate that every non-hidden configurePreset has matching build/test presets.

Catches the class of bug where ``ctest --preset <name>`` fails with
``No such test preset`` because the matrix in ``CMakePresets.json`` is
incomplete (a configurePreset / buildPreset was added without the
corresponding testPreset, or vice versa).

Run from the repo root or via the pre-commit hook of the same name.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

PRESETS = Path(__file__).resolve().parent.parent / "CMakePresets.json"


def main() -> int:
    try:
        data = json.loads(PRESETS.read_text(encoding="utf-8"))
    except FileNotFoundError:
        print(f"validate_presets: {PRESETS} not found", file=sys.stderr)
        return 2
    except json.JSONDecodeError as exc:
        print(f"validate_presets: {PRESETS}: invalid JSON ({exc})", file=sys.stderr)
        return 2

    configure = {p["name"] for p in data.get("configurePresets", []) if not p.get("hidden")}
    build = {p["name"] for p in data.get("buildPresets", [])}
    test = {p["name"] for p in data.get("testPresets", [])}

    missing_build = sorted(configure - build)
    missing_test = sorted(configure - test)
    if not (missing_build or missing_test):
        return 0

    print("CMakePresets.json: preset matrix is incomplete.", file=sys.stderr)
    if missing_build:
        print(
            f"  configurePresets without matching buildPresets: {', '.join(missing_build)}",
            file=sys.stderr,
        )
    if missing_test:
        print(
            f"  configurePresets without matching testPresets:  {', '.join(missing_test)}",
            file=sys.stderr,
        )
    print(
        "Every non-hidden configurePreset should have a buildPreset and testPreset "
        "of the same name so 'cmake --build --preset <name>' and "
        "'ctest --preset <name>' both work.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
