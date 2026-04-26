# Plugin system

AJAZZ Control Center spawns plugins in an isolated child process running the system `python3` interpreter. The host (C++) and the child (Python) talk over a pair of pipes using a line-delimited JSON wire protocol. Plugins are ordinary Python packages discovered in a per-user directory the host scans at startup.

## Why Python?

- Stream Deck-style "click → run macro" UX really wants scripting parity with the official SDK.
- Bundling Lua / JS / WASM would mean shipping a second runtime; CPython is already required for tooling and maps 1-to-1 to user expectations.

## Why out-of-process?

The original design embedded CPython via pybind11; audit finding A4 retired that backend in favour of a subprocess host (`OutOfProcessPluginHost`):

- A segfault inside any C extension a plugin imports (numpy, opencv, mido, …) used to take the whole host process down. Crash isolation is now real — the kernel kills only the child and the next op on the dead host returns a clean exception instead of a SIGSEGV.
- The child runs under an OS-level sandbox (`bwrap` on Linux today; `sandbox-exec` on macOS and AppContainer on Windows follow). Granting `obs-websocket`/`spotify`/`discord-rpc` keeps network reachable; granting `notifications`/`media-control`/`system-power` bind-mounts the user session DBus socket. By default a plugin runs in a fresh PID/IPC/UTS namespace with no network, no `~/.ssh/`, no shared memory.
- The host binary no longer carries the ≈30 MB embedded interpreter — plugin support is now a pure subprocess concern, and the `ajazz_plugins` library has zero Python compile-time deps.

## Lifecycle

```
QGuiApplication startup
   │
   ▼
OutOfProcessPluginHost ctor
   │  fork() + execvp() bwrap → python3 _host_child.py
   ▼
{"event":"ready", "pid":..., "python":"3.x.y"}        ← handshake on stdout pipe
   │
   ▼
host.addSearchPath(...) for each user plugin dir       ← see "Discovery" below
host.loadAll()                                          ← child imports every plugin.py
   │
   ▼
host.dispatch(plugin_id, action_id, settings_json)     ← per-binding key press
   ⋮
   ▼
~OutOfProcessPluginHost
   │  send {"op":"shutdown"}, wait, SIGKILL fallback
```

If a plugin's import raises (syntax error, missing dependency), the child reports a `plugin_error` event and the host logs it but continues — partial coverage is preferable to a single bad plugin breaking everything. If a handler raises during `dispatch`, the child catches the exception and emits `dispatch_error`; the host returns `false` to the caller. A child crash (SIGSEGV in a C extension, etc.) is observed by the host as EOF on the read pipe — `isAlive()` flips to false and the next op throws `std::runtime_error`. A single bad plugin can never take the AJAZZ Control Center UI down.

## Discovery

Plugin directories are scanned at startup, in this order:

| OS      | Path                                                         |
| ------- | ------------------------------------------------------------ |
| Linux   | `~/.config/ajazz-control-center/plugins`                     |
| Windows | `%APPDATA%\ajazz-control-center\plugins`                     |
| macOS   | `~/Library/Application Support/ajazz-control-center/plugins` |

A plugin is any subdirectory containing a `plugin.py` (or a top-level `plugin.py` whose parent has `__init__.py`). Layout:

```
~/.config/ajazz-control-center/plugins/
  obs-scene-switcher/
    plugin.py
    requirements.txt          # optional, host pip-installs into a private venv
    icons/
      mute.png
```

Bundled examples ship under `python/ajazz_plugins/examples/` and are copied verbatim into a user's plugin folder on first run.

## The `ajazz` runtime module

Exposed surface (subject to expansion):

```python
import ajazz

@ajazz.action("obs.toggle_mute", icon="mute.png", label="Mute mic")
def toggle_mute(ctx: ajazz.ActionContext) -> None:
    """Run on every key press bound to this action id."""
    ...

@ajazz.on_key_press(device="akp03")
def any_key(device: ajazz.Device, key_index: int) -> None:
    ...

@ajazz.on_encoder_turn(device="akp05", encoder=0)
def volume(device: ajazz.Device, delta: int) -> None:
    ...
```

The `ajazz.Device` proxy mirrors `ajazz::core::IDevice` plus its capability mix-ins, but with idiomatic Python names (`device.set_key_image(...)`, `device.set_brightness(...)`).

## Action dispatch

When the user binds a key to action id `"com.example.obs.toggle_mute"` in the UI:

1. `ProfileEngine` records `{ device: akp03, key: 2 → action: "com.example.obs.toggle_mute" }` in the profile JSON.
1. On a matching `KeyPressed` event, the host calls `IPluginHost::dispatch(plugin_id, action_id, settings_json)`.
1. The host serialises a `{"op":"dispatch", ...}` line over the IPC pipe; the child receives it, looks up the handler in its registry, and calls it.
1. The child catches any handler exception, emits `dispatch_error` on the IPC pipe, and the host logs it. The user sees an in-app notification, not a crash.

Handler latency is bounded by the worst-case path documented in [THREADING.md](THREADING.md#timing-sensitive-paths) plus one IPC round-trip (sub-millisecond on a warm child). If your action does network I/O, spawn a `threading.Thread` from the handler — the host does *not* offload automatically, by design (it would break perceived ordering).

## Sandbox / security

The OOP host wraps the child in a per-OS sandbox (`bwrap` on Linux today; `sandbox-exec` on macOS and AppContainer on Windows in slices 3c / 3d). Default profile is most-restrictive: fresh PID/IPC/UTS/cgroup namespaces, network unreachable, host filesystem read-only mounted, writable tmpfs at `/tmp`, `--die-with-parent` so a host crash reaps the child.

Each plugin declares the coarse capabilities it needs through its Python class:

```python
class MyPlugin(Plugin):
    permissions = ["obs-websocket", "notifications"]
```

Permission strings come from the `Ajazz.Permissions` enum in [`docs/schemas/plugin_manifest.schema.json`](../schemas/plugin_manifest.schema.json) and are mapped to sandbox relaxations:

- `obs-websocket`, `spotify`, `discord-rpc` → drop `--unshare-net`.
- `notifications`, `media-control`, `system-power` → bind-mount the user session DBus socket.

The granted set passed to `LinuxBwrapSandbox` is the union of every loaded plugin's declared permissions. The user reviews this list at install time.

Plugin signing (Sigstore / cosign) and a "third-party plugin" warning chip in the UI are tracked under [issue #6 (sandbox + permissions)](https://github.com/Aiacos/ajazz-control-center/issues/6).

## Example: hello-world plugin

```python
# ~/.config/ajazz-control-center/plugins/hello/plugin.py
"""Minimal AJAZZ plugin — pops a notification on key press."""

from typing import ClassVar

from ajazz_plugins import ActionContext, Plugin, action


class Hello(Plugin):
    id = "com.example.hello"
    name = "Hello World"
    version = "1.0.0"
    authors = "Your Name"
    permissions: ClassVar[list[str]] = ["notifications"]

    @action(id="say-hi", label="Say hi")
    def say_hi(self, ctx: ActionContext) -> None:
        ctx.notify("Hello from Python!")


Plugin = Hello  # required by the plugin loader
```

After dropping the file in `~/.config/ajazz-control-center/plugins/hello/`, the action `Say hi` becomes available in the **Keys** tab of the UI for any compatible device.
