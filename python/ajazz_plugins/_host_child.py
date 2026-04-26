# SPDX-License-Identifier: GPL-3.0-or-later
"""Out-of-process plugin host — child-side wire protocol (audit finding A4 — slice 1).

Runs as a subprocess of the AJAZZ Control Center main app (spawned by
``OutOfProcessPluginHost`` in ``src/plugins/src/out_of_process_plugin_host.cpp``).
Reads line-delimited JSON ops from stdin, writes line-delimited JSON
events to stdout. Slice 1 implements just enough to prove the
isolation claim: spawn -> ready handshake -> list_plugins (returns
``[]``) -> shutdown, plus a ``_crash_for_test`` op that deliberately
SIGSEGVs the child so the host's crash-survival test has a target.

The slice 2 follow-up expands this to mirror the in-process
``PluginHost.loadAll`` / ``dispatch`` flow:

  * ``add_search_path``: append a directory to the plugin search list.
  * ``load_all``: walk the search paths, import each ``plugin.py``,
    instantiate ``Plugin``, capture metadata, send a ``loaded`` event
    per plugin and a final ``load_complete`` event.
  * ``dispatch``: route an action to the registered Plugin instance,
    catching exceptions so a buggy handler raising in Python returns
    ``dispatch_error`` to the host instead of taking the child down.

For now, list_plugins returns an empty array so the host's
``listPlugins()`` foundation contract holds without any plugin-loading
logic in place yet.

Wire protocol:

  inbound  (host -> child, one JSON object per line):
    {"op": "list_plugins"}            -> {"event":"plugins","plugins":[]}
    {"op": "shutdown"}                -> {"event":"shutdown_ack"}, then exit 0
    {"op": "_crash_for_test"}         -> SIGSEGV (test-only crash trigger)

  outbound (child -> host, one JSON object per line):
    {"event": "ready", "pid": <int>, "python": "<version>"}        (handshake)
    {"event": "plugins", "plugins": []}                            (list response)
    {"event": "shutdown_ack"}                                      (clean shutdown)
    {"event": "error", "op": "<name>", "message": "<text>"}        (op failure)

Lines that are not valid JSON or that lack an ``op`` key are answered
with an ``error`` event but the child stays alive — only ``shutdown``
and ``_crash_for_test`` end the process.

The child does NOT import ``ajazz_plugins.Plugin`` or any user plugin
in slice 1. That keeps the foundation deterministic and the safety
proof clean: a crash in the child while it has zero plugins loaded is
unambiguously a test signal, not a stray Python import side-effect.
"""

from __future__ import annotations

import ctypes
import json
import os
import signal
import sys


def _emit(event: dict[str, object]) -> None:
    """Write a single JSON object as one line on stdout, then flush."""
    sys.stdout.write(json.dumps(event, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def _ready() -> None:
    """First message the host expects to see — the ready handshake.

    Includes pid + python version so the host can log a useful diagnostic
    line when it recovers from a child crash.
    """
    _emit(
        {
            "event": "ready",
            "pid": os.getpid(),
            "python": sys.version.split()[0],
        }
    )


def _list_plugins() -> None:
    """Slice 1: always reports an empty inventory.

    Slice 2 fills this from the loaded ``Plugin`` instances kept in a
    module-level dict, mirroring ``PluginHost::plugins()``.
    """
    _emit({"event": "plugins", "plugins": []})


def _shutdown() -> None:
    """Acknowledge the shutdown request and exit cleanly.

    The host is waiting for the ``shutdown_ack`` line + EOF on our
    stdout pipe. ``sys.exit(0)`` triggers normal interpreter teardown,
    which closes stdout and gives the host the EOF signal.
    """
    _emit({"event": "shutdown_ack"})
    sys.exit(0)


def _crash_for_test() -> None:
    """Test-only: trigger a deterministic SIGSEGV and exit.

    Wired via ``os.kill(getpid(), SIGSEGV)`` rather than dereferencing a
    null C pointer — same effect (signal delivery, no clean unwind),
    but doesn't depend on ctypes / undefined behaviour and is easier to
    reason about in the test fixture.
    """
    _emit({"event": "crashing"})
    sys.stdout.flush()
    os.kill(os.getpid(), signal.SIGSEGV)
    # Belt and braces: if the platform somehow ignored the signal,
    # fall back to a real null deref via ctypes so the test still
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
        op = msg.get("op") if isinstance(msg, dict) else None
        if op == "list_plugins":
            _list_plugins()
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
    raise SystemExit(_main())
