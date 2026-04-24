# Plugin Development Guide

AJAZZ Control Center runs user plugins inside an embedded Python 3 interpreter. Plugins are regular Python packages with one required entry point: a subclass of `ajazz_plugins.Plugin` named `Plugin` (or re-exported as `Plugin = YourClass`) in `plugin.py`.

## Plugin anatomy

```
~/.config/ajazz-control-center/plugins/
└── hello/
    ├── plugin.py            # required
    ├── manifest.yml         # optional — icon, category, homepage
    └── <any other modules>  # free-form
```

### Minimal example

```python
from ajazz_plugins import ActionContext, Plugin, action


class Hello(Plugin):
    id       = "com.example.hello"
    name     = "Hello World"
    version  = "1.0.0"
    authors  = "Your Name"

    @action(id="say-hi", label="Say hi")
    def say_hi(self, ctx: ActionContext) -> None:
        ctx.notify("Hello from Python!")


Plugin = Hello
```

## Naming

Plugin ids must be reverse-DNS strings (`com.example.hello`). Action ids are short identifiers local to the plugin (`say-hi`). The profile engine refers to actions through their fully-qualified id: `com.example.hello.say-hi`.

## The `ActionContext`

The object passed to every handler exposes:

- `ctx.device_codename` — e.g. `"akp153"`, when the action is bound to a specific device.
- `ctx.key_index` — physical key index that triggered the action.
- `ctx.settings` — a `dict[str, Any]` round-tripped from the profile JSON.
- `ctx.notify(msg)` — desktop notification (in the stub, prints to stderr).

Future revisions will add `ctx.device.*` accessors that wrap the C++ capability interfaces (`set_key_image`, `set_rgb`, `get_battery`, …).

## Actions without a context

Handlers may declare zero parameters. The dispatcher detects the signature and will call them without a `ctx`:

```python
@action(id="toggle", label="Toggle")
def toggle(self) -> None:
    ...
```

## Lifecycle hooks (roadmap)

```python
class MyPlugin(Plugin):
    def on_load(self) -> None: ...
    def on_unload(self) -> None: ...
    def on_device_connected(self, codename: str) -> None: ...
    def on_device_disconnected(self, codename: str) -> None: ...
```

## Packaging and distribution

- A `manifest.yml` at the plugin root may declare `icon`, `category` and `homepage`.
- Plugins may bundle pure-Python dependencies under `<plugin>/vendor/` — the loader appends that directory to `sys.path` automatically.
- Binary wheels / native extensions are **not** supported; keep the code portable.

## Testing your plugin outside the app

Plugins only need the `ajazz_plugins` SDK, which is a regular installable package:

```bash
pip install -e /path/to/ajazz-control-center
pytest my_plugin/
```

## Security

The plugin host runs Python with the same privileges as the application. Do not execute untrusted third-party code. The host is expected to grow a sandbox mode (subprocess + seccomp on Linux) before the first stable release.
