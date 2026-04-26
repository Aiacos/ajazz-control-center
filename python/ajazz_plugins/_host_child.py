# SPDX-License-Identifier: GPL-3.0-or-later
"""Out-of-process plugin host — child-side wire protocol (audit finding A4).

Runs as a subprocess of the AJAZZ Control Center main app (spawned by
``OutOfProcessPluginHost`` in ``src/plugins/src/out_of_process_plugin_host.cpp``).
Reads line-delimited JSON ops from stdin, writes line-delimited JSON
events on the dedicated IPC channel (a saved reference to stdout
captured at startup before plugin code can rebind it).

Slice 2 wire protocol (this version):

  inbound  (host -> child, one JSON object per line):
    {"op": "add_search_path", "path": "..."}    -> {"event":"path_added"}
    {"op": "load_all"}                           -> per-plugin events + load_complete
    {"op": "list_plugins"}                       -> per-plugin events + plugins_complete
    {"op": "dispatch", "plugin_id": "...",
                       "action_id": "...",
                       "settings_json": "..."}   -> dispatched | dispatch_error
    {"op": "shutdown"}                           -> {"event":"shutdown_ack"}, then exit 0
    {"op": "_crash_for_test"}                    -> SIGSEGV (test-only crash trigger)

  outbound (child -> host, one JSON object per line):
    {"event": "ready", "pid": <int>, "python": "<version>"}     (handshake on startup)
    {"event": "path_added"}                                     (ack of add_search_path)
    {"event": "plugin_loaded", "id": "...", "name": "...",
                                "version": "...", "authors": "...",
                                "permissions": ["..."]}
    {"event": "plugin_error", "id": "...", "message": "..."}
    {"event": "load_complete", "count": <int>}
    {"event": "plugin", "id": "...", "name": "...",
                         "version": "...", "authors": "...",
                         "permissions": ["..."]}
    {"event": "plugins_complete", "count": <int>}
    {"event": "dispatched", "plugin_id": "...", "action_id": "..."}
    {"event": "dispatch_error", "plugin_id": "...", "action_id": "...",
                                 "message": "..."}
    {"event": "shutdown_ack"}
    {"event": "error", "op": "<name>", "message": "<text>"}

Stdout / stderr handling: at startup we save the original stdout into
``_ipc`` for the wire protocol, then redirect ``sys.stdout`` and
``sys.stderr`` to a single shared sink so anything plugin code prints
goes to the host process's stderr (the host inherits stderr so a
human dev can see the prints; the IPC channel stays clean).

Each invariant the C++ side relies on:

  * The first line on stdout is always the ``ready`` event.
  * Every op except ``shutdown`` and ``_crash_for_test`` produces at
    least one event line that the host can wait on (no silent ops).
  * Multi-event responses (``load_all`` / ``list_plugins``) end with
    a ``*_complete`` terminator so the host knows when to stop reading.
"""

from __future__ import annotations

import contextlib
import ctypes
import importlib
import importlib.util
import io
import json
import os
import signal
import sys
import traceback
from pathlib import Path
from typing import Any

# ---------------------------------------------------------------------------
# IPC channel: capture stdout BEFORE any plugin code runs, then redirect the
# user-facing stdout/stderr to the host's stderr. Plugins that print() will
# land on stderr (which the host's parent inherits, so a developer can see
# the output during local runs) without polluting the wire protocol.
# ---------------------------------------------------------------------------

_ipc: io.TextIOWrapper = sys.stdout
sys.stdout = sys.stderr  # plugin-side print() goes to host's stderr now


def _emit(event: dict[str, object]) -> None:
    """Write a single JSON object as one line on the IPC channel + flush."""
    _ipc.write(json.dumps(event, separators=(",", ":")) + "\n")
    _ipc.flush()


# ---------------------------------------------------------------------------
# Plugin loading state.
# ---------------------------------------------------------------------------

_search_paths: list[Path] = []
_plugins: dict[str, Any] = {}  # plugin_id -> Plugin instance
_added_to_sys_path: set[str] = set()


def _instance_permissions(instance: object) -> list[str]:
    """Read the Plugin instance's `permissions` attribute, defensively.

    Plugin authors are expected to set ``permissions: list[str] = [...]``
    on their subclass (per ``Ajazz.Permissions`` in the manifest schema).
    Anything that is not a list-of-strings collapses to ``[]`` so a
    malformed plugin cannot crash the inventory event encoder.
    """
    raw = getattr(instance, "permissions", None)
    if not isinstance(raw, list):
        return []
    return [item for item in raw if isinstance(item, str)]


def _ready() -> None:
    """First line the host expects — handshake with pid + python version."""
    _emit(
        {
            "event": "ready",
            "pid": os.getpid(),
            "python": sys.version.split()[0],
        }
    )


_MAX_PLUGIN_ID_LEN = 64


def _is_safe_plugin_id(value: str) -> bool:
    """Mirror C++ in-process host's `isSafePluginId`.

    Allowed chars: lowercase ASCII letters, digits, dot, underscore,
    hyphen. Length 1-64.
    """
    if not value or len(value) > _MAX_PLUGIN_ID_LEN:
        return False
    return all(c.isascii() and (c.islower() or c.isdigit() or c in "._-") for c in value)


def _add_search_path(msg: dict[str, Any]) -> None:
    """Add a directory the next `load_all` call should scan.

    The path is resolved (so symlinks / `..` segments are flattened)
    before we add it; duplicates are silently skipped. `sys.path` is
    NOT modified here — only at `load_all` time, so callers can stage
    multiple roots before any imports happen.
    """
    raw = msg.get("path")
    if not isinstance(raw, str) or not raw:
        _emit({"event": "error", "op": "add_search_path", "message": "missing path"})
        return
    resolved = Path(raw).resolve()
    if resolved not in _search_paths:
        _search_paths.append(resolved)
    _emit({"event": "path_added"})


def _load_one_plugin(plugin_dir: Path) -> tuple[bool, str, str]:
    """Try to import a single plugin directory.

    Returns:
        ``(ok, id, message)``. On success ``id`` is the plugin's id and
        ``message`` is empty; on failure ``id`` is the directory name
        and ``message`` carries a short description of what went wrong.
    """
    pkg_name = plugin_dir.name
    if not _is_safe_plugin_id(pkg_name):
        return False, pkg_name, "unsafe directory name"

    parent = str(plugin_dir.parent)
    if parent not in _added_to_sys_path:
        sys.path.insert(0, parent)
        _added_to_sys_path.add(parent)
    # Plugin packages are by convention `<pkg>/plugin.py` without an
    # `__init__.py` — Python treats `<pkg>/` as an implicit namespace
    # package and `import <pkg>` returns an empty namespace, so we
    # have to import `<pkg>.plugin` directly to reach the `Plugin`
    # class. Always reload from disk so a developer iterating on a
    # plugin picks up edits without restarting the whole host.
    submodule = f"{pkg_name}.plugin"
    try:
        if submodule in sys.modules:
            mod = importlib.reload(sys.modules[submodule])
        else:
            mod = importlib.import_module(submodule)
    except Exception as exc:
        return False, pkg_name, f"import: {exc}"
    cls = getattr(mod, "Plugin", None)
    if cls is None:
        return False, pkg_name, "module has no `Plugin` attribute"
    try:
        instance = cls()
    except Exception as exc:
        return False, pkg_name, f"Plugin(): {exc}"
    plugin_id = getattr(instance, "id", "")
    if not isinstance(plugin_id, str) or not _is_safe_plugin_id(plugin_id):
        return False, pkg_name, "plugin reports unsafe id"
    _plugins[plugin_id] = instance
    return True, plugin_id, ""


def _load_all() -> None:
    """Walk every registered search path and import each plugin found.

    Discovers every ``<dir>/plugin.py`` under each registered search
    path and imports it. Per-plugin failures emit ``plugin_error``
    and continue.
    """
    loaded_now = 0
    for root in _search_paths:
        if not root.is_dir():
            _emit(
                {
                    "event": "plugin_error",
                    "id": str(root),
                    "message": "search path is not a directory",
                }
            )
            continue
        for entry in sorted(root.iterdir()):
            if not entry.is_dir() or not (entry / "plugin.py").is_file():
                continue
            ok, pid, message = _load_one_plugin(entry)
            if ok:
                instance = _plugins[pid]
                _emit(
                    {
                        "event": "plugin_loaded",
                        "id": pid,
                        "name": str(getattr(instance, "name", pid)),
                        "version": str(getattr(instance, "version", "")),
                        "authors": str(getattr(instance, "authors", "")),
                        "permissions": _instance_permissions(instance),
                    }
                )
                loaded_now += 1
            else:
                _emit({"event": "plugin_error", "id": pid, "message": message})
    _emit({"event": "load_complete", "count": loaded_now})


def _list_plugins() -> None:
    """Stream the current inventory back to the host, terminator last."""
    for pid, instance in _plugins.items():
        _emit(
            {
                "event": "plugin",
                "id": pid,
                "name": str(getattr(instance, "name", pid)),
                "version": str(getattr(instance, "version", "")),
                "authors": str(getattr(instance, "authors", "")),
                "permissions": _instance_permissions(instance),
            }
        )
    _emit({"event": "plugins_complete", "count": len(_plugins)})


def _dispatch(msg: dict[str, Any]) -> None:
    """Route a host dispatch op to the matching `Plugin.dispatch` call.

    Settings_json is passed through verbatim — the Plugin base class is
    responsible for decoding it (it stays a string at the wire level so
    the same wire protocol works for arbitrary settings shapes without
    the child enforcing a particular schema).
    """
    plugin_id = msg.get("plugin_id")
    action_id = msg.get("action_id")
    settings_json = msg.get("settings_json", "")
    if not isinstance(plugin_id, str) or not isinstance(action_id, str):
        _emit(
            {
                "event": "dispatch_error",
                "plugin_id": str(plugin_id),
                "action_id": str(action_id),
                "message": "missing plugin_id / action_id",
            }
        )
        return
    instance = _plugins.get(plugin_id)
    if instance is None:
        _emit(
            {
                "event": "dispatch_error",
                "plugin_id": plugin_id,
                "action_id": action_id,
                "message": "unknown plugin",
            }
        )
        return
    try:
        instance.dispatch(action_id, settings_json)
    except Exception as exc:
        _emit(
            {
                "event": "dispatch_error",
                "plugin_id": plugin_id,
                "action_id": action_id,
                "message": f"{type(exc).__name__}: {exc}",
            }
        )
        traceback.print_exc()  # to stderr — for dev visibility
        return
    _emit({"event": "dispatched", "plugin_id": plugin_id, "action_id": action_id})


def _shutdown() -> None:
    """Acknowledge the shutdown request and exit cleanly."""
    _emit({"event": "shutdown_ack"})
    sys.exit(0)


def _crash_for_test() -> None:
    """Test-only: trigger a deterministic SIGSEGV and exit.

    Uses ``os.kill(getpid(), SIGSEGV)`` so the host observes both signal
    delivery and a non-zero exit code, mirroring the failure mode of
    a misbehaving C extension inside the child.
    """
    _emit({"event": "crashing"})
    _ipc.flush()
    os.kill(os.getpid(), signal.SIGSEGV)
    # Belt and braces: if the platform somehow ignored the signal,
    # fall back to a real null deref via `ctypes` so the test still
    # observes a non-zero exit and a child death.
    ctypes.string_at(0)


def _main() -> int:
    _ready()
    for raw_line in sys.stdin:
        line = raw_line.rstrip("\r\n")
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError as exc:
            _emit({"event": "error", "op": "<parse>", "message": str(exc)})
            continue
        if not isinstance(msg, dict):
            _emit({"event": "error", "op": "<parse>", "message": "not a JSON object"})
            continue
        op = msg.get("op")
        if op == "add_search_path":
            _add_search_path(msg)
        elif op == "load_all":
            _load_all()
        elif op == "list_plugins":
            _list_plugins()
        elif op == "dispatch":
            _dispatch(msg)
        elif op == "shutdown":
            _shutdown()
            return 0  # unreachable; _shutdown calls sys.exit
        elif op == "_crash_for_test":
            _crash_for_test()
            return 0  # unreachable; _crash_for_test signals SIGSEGV
        else:
            _emit({"event": "error", "op": str(op), "message": "unknown op"})
    return 0


if __name__ == "__main__":
    # Wrap in contextlib.suppress so a host that closes stdin without
    # sending shutdown still terminates cleanly (the for-loop falls out
    # of `sys.stdin` on EOF and we exit 0).
    with contextlib.suppress(KeyboardInterrupt):
        raise SystemExit(_main())
