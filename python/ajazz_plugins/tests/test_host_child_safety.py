# SPDX-License-Identifier: GPL-3.0-or-later
"""Safety tests for the out-of-process host child's plugin loader (SEC-S2).

Regression coverage for the stdlib-shadowing guard: a plugin directory
whose basename collides with a Python stdlib module name must be rejected
*before* its parent directory is prepended to ``sys.path``, so a plugin
shipped in e.g. a ``json/`` directory cannot hijack the stdlib module the
host child itself depends on.

The guard originally lived in the in-process ``plugin_host.cpp`` and was
lost when that file was retired (slice 3e); this re-ports it to the OOP
child and pins it down so it cannot silently regress again.
"""

from __future__ import annotations

import sys
from pathlib import Path

from ajazz_plugins import _host_child


def test_stdlib_names_are_flagged() -> None:
    """Common stdlib / builtin module names collide and must be rejected."""
    for name in ("json", "os", "sys", "logging", "socket"):
        assert _host_child._shadows_stdlib(name), name


def test_normal_plugin_names_are_allowed() -> None:
    """Realistic plugin directory names must not be treated as collisions."""
    for name in ("hello", "obs-control", "my.plugin", "spotify_now_playing"):
        assert not _host_child._shadows_stdlib(name), name


def test_stdlib_set_is_nonempty() -> None:
    """The collision set must never be empty (would disable the guard)."""
    assert _host_child._STDLIB_MODULE_NAMES


def test_load_one_plugin_rejects_stdlib_named_dir(tmp_path: Path) -> None:
    """A `json/` plugin dir is rejected and the real stdlib stays intact."""
    shadow = tmp_path / "json"
    shadow.mkdir()
    (shadow / "plugin.py").write_text("class Plugin: ...\n", encoding="utf-8")

    ok, plugin_id, message = _host_child._load_one_plugin(shadow)

    assert ok is False
    assert plugin_id == "json"
    assert "shadows" in message
    # The guard must return before mutating sys.path, so the parent dir
    # never gets a chance to shadow the real stdlib `json`.
    assert str(shadow.parent) not in sys.path
