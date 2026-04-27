# Plugin Development

AJAZZ Control Center runs plugins in a **child Python process** spawned
by the host on demand. The host talks to the child over line-delimited
JSON on a pair of pipes; a crash in plugin code (or a misbehaving C
extension a plugin imports) kills only the child, never the host. On
Linux the child is wrapped in `bwrap`; on macOS it's wrapped in
`/usr/bin/sandbox-exec`; on Windows it runs in a plain `_spawnvp`
subprocess today (AppContainer hardening is tracked in `TODO.md`).

> Note: pre-2026-04 the plugin runtime was an embedded interpreter
> linked into the host via pybind11. That backend was retired in
> [audit finding A4 slice 3e](https://github.com/Aiacos/ajazz-control-center/blob/main/TODO.md#a4--pluginhost-out-of-process)
> — references to a "bundled Python environment" or `acc --install-plugin`
> in older docs are out of date.

The full SDK reference lives at
[`docs/guides/PLUGIN_DEVELOPMENT.md`](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/guides/PLUGIN_DEVELOPMENT.md).
The architecture doc — wire protocol, sandbox model, permission system,
Sigstore signing — is at
[`docs/architecture/PLUGIN-SDK.md`](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/architecture/PLUGIN-SDK.md).

## Plugin layout

A plugin is a directory containing `plugin.py`. The host scans every
search path it has been configured with, imports each `<dir>/plugin.py`,
and instantiates the `Plugin` class.

```
hello/
└── plugin.py
```

The default search path is `~/.config/ajazz-control-center/plugins/`.
Drop your plugin directory in there and restart the app.

## Hello world

The canonical example shipped under
[`python/ajazz_plugins/examples/hello/plugin.py`](https://github.com/Aiacos/ajazz-control-center/blob/main/python/ajazz_plugins/examples/hello/plugin.py):

```python
# SPDX-License-Identifier: GPL-3.0-or-later
from typing import ClassVar

from ajazz_plugins import ActionContext, Plugin, action


class Hello(Plugin):
    """Minimal example plugin."""

    id = "com.example.hello"
    name = "Hello World"
    version = "1.0.0"
    authors = "AJAZZ Control Center contributors"
    permissions: ClassVar[list[str]] = ["notifications"]

    @action(id="say-hi", label="Say hi")
    def say_hi(self, ctx: ActionContext) -> None:
        ctx.notify("Hello from Python!")

    @action(id="counter", label="Increment counter")
    def counter(self, ctx: ActionContext) -> None:
        state = ctx.settings.setdefault("state", {"count": 0})
        state["count"] += 1
        ctx.notify(f"count: {state['count']}")


Plugin = Hello  # expected by the plugin loader
```

Required class attributes:

| Attribute     | Description                                              |
| ------------- | -------------------------------------------------------- |
| `id`          | Reverse-DNS unique identifier (e.g. `com.example.hello`) |
| `name`        | Human-readable name shown in the Plugin Store UI         |
| `version`     | SemVer string                                            |
| `authors`     | Free-form author string                                  |
| `permissions` | `ClassVar[list[str]]` — see *Permissions* below          |

The module-level `Plugin = Hello` line is a load-time contract: the
host's child process expects every `plugin.py` to expose a `Plugin`
attribute resolving to the class to instantiate.

## Permissions

Every action your plugin uses that touches the system beyond pure
computation must be declared in `permissions`. The current vocabulary
(matching the JSON schema at
[`docs/schemas/plugin_manifest.schema.json`](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/schemas/plugin_manifest.schema.json)):

| Permission       | Effect                                                |
| ---------------- | ----------------------------------------------------- |
| `notifications`  | Allow `ctx.notify(...)` desktop notifications         |
| `media-control`  | Allow MediaRemote / playerctl integration             |
| `system-power`   | Allow IOPower / logind sleep / shutdown calls         |
| `obs-websocket`  | Allow outbound network to OBS WebSocket               |
| `spotify`        | Allow outbound network to Spotify Web API             |
| `discord-rpc`    | Allow Discord Rich Presence socket                    |

The Linux `bwrap` sandbox unshares the network namespace by default;
declaring `obs-websocket` / `spotify` / `discord-rpc` lifts that
restriction. On macOS the `sandbox-exec` profile starts with
`(deny default)` and grants `(allow network*)` for the same set.
DBus-equivalent permissions (`notifications` / `media-control` /
`system-power`) bind-mount the user session bus on Linux and add
`(allow mach-lookup)` on macOS.

## The `ActionContext` object

Every `@action`-decorated method is called with a single
`ActionContext` argument:

```python
@dataclass
class ActionContext:
    device_codename: str = ""    # e.g. "akp153"
    key_index: int = 0           # zero-based, 0 when not key-bound
    settings: dict[str, Any] = ...  # per-binding settings dict

    def notify(self, message: str) -> None:
        """Display a desktop notification (production) /
        print to stderr (test stub)."""
```

Mutations to `ctx.settings` are visible inside the same dispatch but
**not** persisted across sessions in the current stub implementation —
persistence is on the roadmap as part of the Plugin lifecycle manager
milestone.

## Lifecycle

The host emits one `plugin_loaded` event per successful import and one
`plugin_error` per failure. Failures don't abort the load sweep — a
single broken plugin doesn't hide the rest. Every error is logged with
the offending file path and the exception traceback so the user can
fix the plugin without restarting the app.

## Testing plugins

Out-of-process testing is straightforward today: import your plugin's
`Plugin` class directly and call methods on it with a hand-built
`ActionContext`:

```python
from ajazz_plugins import ActionContext
from hello.plugin import Hello


def test_say_hi_does_not_raise():
    Hello().say_hi(ActionContext(device_codename="akp153"))


def test_counter_increments_settings():
    plugin = Hello()
    ctx = ActionContext(settings={})
    plugin.counter(ctx)
    plugin.counter(ctx)
    assert ctx.settings["state"]["count"] == 2
```

Wire-level integration tests (host ↔ child IPC, sandbox decoration,
crash isolation) live in
[`tests/unit/test_out_of_process_plugin_host.cpp`](https://github.com/Aiacos/ajazz-control-center/blob/main/tests/unit/test_out_of_process_plugin_host.cpp)
and run on every push across Linux + macOS + Windows CI.

## Publishing

The Plugin Store catalogue infrastructure is in flight (see
[`TODO.md` → Plugin SDK + Store](https://github.com/Aiacos/ajazz-control-center/blob/main/TODO.md)).
Until the catalogue backend lands, the recommended distribution path is:

1. Host the plugin directory on GitHub.
1. Document the install step in your project README:
   `git clone <repo> ~/.config/ajazz-control-center/plugins/<name>`.
1. Optionally open a PR adding your plugin to the
   [awesome-ajazz-plugins](https://github.com/Aiacos/ajazz-control-center/wiki/Awesome-Plugins)
   page.

Sigstore-signed bundles + a Plugin Store install button are coming
with the catalogue backend.
