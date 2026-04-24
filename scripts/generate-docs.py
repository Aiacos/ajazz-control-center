#!/usr/bin/env python3
"""Regenerate every AUTOGEN section in README.md and docs/wiki/*.md.

The single source of truth is ``docs/_data/devices.yaml``. This script
reads it and rewrites the blocks delimited by ``<!-- BEGIN AUTOGEN: name -->``
and ``<!-- END AUTOGEN: name -->`` in the target files.

Usage:
    python scripts/generate-docs.py          # rewrite in place
    python scripts/generate-docs.py --check  # exit non-zero if out of date

Supported block names
---------------------
- ``devices-table``       — full device support matrix
- ``devices-by-family``   — grouped tables per family
- ``platform-matrix``     — OS support / install / notes
- ``stats``               — counts: devices, families, codelines
- ``legend``              — status legend
- ``toc-wiki``            — auto-generated wiki table of contents
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    import yaml
except ImportError:  # pragma: no cover
    sys.stderr.write("PyYAML is required: pip install pyyaml\n")
    sys.exit(2)

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "docs" / "_data" / "devices.yaml"
TARGETS = [
    ROOT / "README.md",
    ROOT / "docs" / "wiki" / "Supported-Devices.md",
    ROOT / "docs" / "wiki" / "Home.md",
]

BLOCK_RE = re.compile(
    r"(<!--\s*BEGIN AUTOGEN:\s*(?P<name>[a-z0-9_-]+)\s*-->)"
    r"(?P<body>.*?)"
    r"(<!--\s*END AUTOGEN:\s*(?P=name)\s*-->)",
    re.DOTALL,
)


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------
@dataclass
class Data:
    raw: dict[str, Any]

    @classmethod
    def load(cls, path: Path = DATA) -> Data:
        with path.open(encoding="utf-8") as fh:
            return cls(yaml.safe_load(fh))

    @property
    def devices(self) -> list[dict[str, Any]]:
        return self.raw.get("devices", [])

    @property
    def statuses(self) -> dict[str, dict[str, str]]:
        return self.raw.get("statuses", {})

    @property
    def capabilities(self) -> dict[str, str]:
        return self.raw.get("capabilities", {})

    @property
    def platforms(self) -> list[dict[str, str]]:
        return self.raw.get("platforms", [])


# ---------------------------------------------------------------------------
# Block renderers — each returns the inner markdown (without the HTML markers)
# ---------------------------------------------------------------------------
def _status_badge(status: str, statuses: dict[str, dict[str, str]]) -> str:
    s = statuses.get(status, {})
    return f"{s.get('emoji', '❔')} {s.get('label', status)}"


def render_devices_table(d: Data) -> str:
    hdr = "| Family | Device | VID:PID | Keys | Encoders | Status | Capabilities |\n"
    hdr += "|--------|--------|---------|:----:|:--------:|--------|--------------|\n"
    rows: list[str] = []
    for dev in d.devices:
        vid = dev.get("vid", "")
        pid = dev.get("pid", "")
        vp = f"`{vid}:{pid}`" if vid and pid else ""
        caps = ", ".join(dev.get("capabilities", []))
        rows.append(
            "| {family} | [{name}]({doc}) | {vp} | {keys} | {enc} | {stat} | {caps} |".format(
                family=dev.get("family", ""),
                name=dev.get("name", ""),
                doc=dev.get("protocol_doc", ""),
                vp=vp,
                keys=dev.get("keys", "—"),
                enc=dev.get("encoders", "—"),
                stat=_status_badge(dev.get("status", ""), d.statuses),
                caps=caps,
            )
        )
    return "\n" + hdr + "\n".join(rows) + "\n"


def render_devices_by_family(d: Data) -> str:
    out: list[str] = [""]
    families: dict[str, list[dict[str, Any]]] = {}
    for dev in d.devices:
        families.setdefault(dev["family"], []).append(dev)

    titles = {
        "streamdeck": "Stream decks",
        "keyboard": "Keyboards",
        "mouse": "Mice",
    }

    for fam_key in ["streamdeck", "keyboard", "mouse"]:
        if fam_key not in families:
            continue
        out.append(f"### {titles.get(fam_key, fam_key.title())}\n")
        out.append("| Device | USB | Status | Features | Notes |")
        out.append("|--------|-----|--------|----------|-------|")
        for dev in families[fam_key]:
            vp = f"`{dev.get('vid', '')}:{dev.get('pid', '')}`"
            caps = ", ".join(d.capabilities.get(c, c) for c in dev.get("capabilities", []))
            out.append(
                f"| [{dev['name']}]({dev.get('protocol_doc', '')}) | "
                f"{vp} | {_status_badge(dev['status'], d.statuses)} | "
                f"{caps} | {dev.get('notes', '')} |"
            )
        out.append("")
    return "\n".join(out) + "\n"


def render_platform_matrix(d: Data) -> str:
    hdr = "| Platform | Build | Install | Notes |\n"
    hdr += "|----------|-------|---------|-------|\n"
    rows = [f"| {p['name']} | {p['build']} | {p['install']} | {p['notes']} |" for p in d.platforms]
    return "\n" + hdr + "\n".join(rows) + "\n"


def render_legend(d: Data) -> str:
    parts = [f"{s['emoji']} **{s['label']}** — {s['description']}" for s in d.statuses.values()]
    return "\n" + " · ".join(parts) + "\n"


def render_stats(d: Data) -> str:
    by_family: dict[str, int] = {}
    by_status: dict[str, int] = {}
    for dev in d.devices:
        by_family[dev["family"]] = by_family.get(dev["family"], 0) + 1
        by_status[dev["status"]] = by_status.get(dev["status"], 0) + 1
    total = len(d.devices)
    fam_str = ", ".join(f"{n} {k}" for k, n in sorted(by_family.items()))
    stat_str = ", ".join(
        f"{n} {d.statuses[k]['label']}" for k, n in by_status.items() if k in d.statuses
    )
    return f"\n**{total} devices** across {fam_str} — {stat_str}.\n"


def render_toc_wiki(_: Data) -> str:
    """List every wiki page except the Home itself."""
    wiki_dir = ROOT / "docs" / "wiki"
    pages = sorted(p.name for p in wiki_dir.glob("*.md") if p.name != "Home.md")
    lines = [
        "",
        *(f"- [{p.replace('.md', '').replace('-', ' ')}]({p.replace('.md', '')})" for p in pages),
        "",
    ]
    return "\n".join(lines)


RENDERERS = {
    "devices-table": render_devices_table,
    "devices-by-family": render_devices_by_family,
    "platform-matrix": render_platform_matrix,
    "legend": render_legend,
    "stats": render_stats,
    "toc-wiki": render_toc_wiki,
}


# ---------------------------------------------------------------------------
# Core in-place rewrite
# ---------------------------------------------------------------------------
def rewrite_file(path: Path, data: Data) -> tuple[str, str]:
    """Return (original, updated) text for a single file."""
    if not path.exists():
        return "", ""
    original = path.read_text(encoding="utf-8")

    def _replace(match: re.Match[str]) -> str:
        name = match.group("name")
        renderer = RENDERERS.get(name)
        if renderer is None:
            sys.stderr.write(f"::warning file={path}:: unknown AUTOGEN block '{name}'\n")
            return match.group(0)
        body = renderer(data)
        return f"{match.group(1)}{body}{match.group(4)}"

    updated = BLOCK_RE.sub(_replace, original)
    return original, updated


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check",
        action="store_true",
        help="Do not write; exit 1 if any file would change.",
    )
    parser.add_argument(
        "--targets",
        nargs="+",
        type=Path,
        default=TARGETS,
        help="Files to rewrite (default: README + wiki pages).",
    )
    args = parser.parse_args(argv)

    data = Data.load()
    any_changed = False
    for path in args.targets:
        original, updated = rewrite_file(path, data)
        if original != updated:
            any_changed = True
            if args.check:
                sys.stderr.write(
                    f"::error file={path.relative_to(ROOT)}::"
                    "AUTOGEN block is out of date — run `make docs`.\n"
                )
            else:
                path.write_text(updated, encoding="utf-8")
                print(f"updated: {path.relative_to(ROOT)}")

    if args.check and any_changed:
        return 1
    if not any_changed:
        print("All AUTOGEN blocks already up to date.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
