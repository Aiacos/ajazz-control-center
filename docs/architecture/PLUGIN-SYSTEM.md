# Plugin system

AJAZZ Control Center embeds a CPython 3.11+ interpreter and exposes its core APIs through a `pybind11`-built native module called `ajazz`. Plugins are ordinary Python packages that import that module, register actions and handlers, and live in a per-user directory the host scans at startup.

## Why Python?

- Stream Deck-style "click → run macro" UX really wants scripting parity with the official SDK.
- Bundling Lua / JS / WASM would mean shipping a second runtime; CPython is already required for tooling and maps 1-to-1 to user expectations.
- pybind11 lets us expose the same C++ types we use internally, with zero serialization overhead.

The trade-off — a heavier installer (≈30 MB embedded interpreter) — is acceptable for a desktop companion app. Embedded mode is configured at build time via `AJAZZ_BUILD_PYTHON_HOST=ON` and can be disabled for minimal builds.

## Lifecycle

```
QGuiApplication startup
   │
   ▼
PluginHost::Impl::initInterpreter()         ← Py_InitializeEx, pybind11 init
   │
   ▼
PluginHost::scanPluginDirs()                ← see "Discovery" below
   │
   ▼
for each plugin/ dir:
    importlib.import_module("plugin")        ← imports plugin.py
    plugin.register(host: ajazz.Host)        ← user-defined entry-point
   │
   ▼
QGuiApplication::exec()                     ← normal event loop
   ⋮
   ▼
PluginHost destructor                       ← Py_FinalizeEx
```

If `register()` raises, the host catches the exception, logs it, surfaces a non-blocking notification, and skips the offending plugin. The rest of the app keeps running.

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

When the user binds a key to action id `"obs.toggle_mute"` in the UI:

1. `ProfileEngine` records `{ device: akp03, key: 2 → action: "obs.toggle_mute" }` in the profile JSON.
1. On a matching `KeyPressed` event, `PluginHost::dispatch("obs.toggle_mute", ctx)` is called from the main thread.
1. The host acquires the GIL, looks up the handler, calls it.
1. Any exception is caught at the boundary; the user sees an in-app notification, not a crash.

Handler latency is bounded by the worst-case path documented in [THREADING.md](THREADING.md#timing-sensitive-paths). If your action does network I/O, spawn a `threading.Thread` from the handler — the host does *not* offload automatically, by design (it would break perceived ordering).

## Sandbox / security

Plugins run **in the host's address space with the host's permissions**. There is no sandbox.

This is a deliberate alpha-stage choice: we want users to be able to run shell commands, talk to OBS, hit a REST API, etc. without fighting capability gates. A future major version will likely add a per-plugin permission manifest (see [#permissions](https://github.com/Aiacos/ajazz-control-center/issues)).

Until then:

- Only install plugins you trust. Read the source.
- The host shows a "third-party plugin" warning chip in the UI for any plugin not signed by the project.

## Example: hello-world plugin

```python
# ~/.config/ajazz-control-center/plugins/hello/plugin.py
"""Minimal AJAZZ plugin — logs every key press to stdout."""

import ajazz

def register(host: ajazz.Host) -> None:
    """Plugin entry-point — host calls this once at startup."""
    @ajazz.action("hello.print", label="Say hello", icon="wave.png")
    def hello(ctx: ajazz.ActionContext) -> None:
        print(f"Hello from key {ctx.key_index} on {ctx.device.codename}!")
```

After dropping the file in the plugins folder, the action `Say hello` becomes available in the **Keys** tab of the UI for any compatible device.
