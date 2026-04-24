# Plugin Development

AJAZZ Control Center embeds CPython via
[pybind11](https://pybind11.readthedocs.io/). Plugins are regular Python
packages that declare **actions** — named callables that appear in the
profile editor's action palette.

The full SDK reference lives at
[`docs/guides/PLUGIN_DEVELOPMENT.md`](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/guides/PLUGIN_DEVELOPMENT.md).

## Plugin layout

```
my_plugin/
├── pyproject.toml
└── my_plugin/
    ├── __init__.py
    └── plugin.py
```

`pyproject.toml` must set `[project.entry-points."ajazz.plugins"]` with
a name → dotted path mapping, for example:

```toml
[project]
name = "ajazz-plugin-obs"
version = "0.1.0"
dependencies = ["obs-websocket-py>=1.0"]

[project.entry-points."ajazz.plugins"]
obs = "ajazz_plugin_obs.plugin:OBSPlugin"
```

## Hello world

```python
# my_plugin/plugin.py
from ajazz_plugins import Plugin, action, ActionContext

class HelloPlugin(Plugin):
    name = "hello"
    version = "0.1.0"

    @action("hello.say_hi", title="Say Hi", icon="hand-wave")
    def say_hi(self, ctx: ActionContext) -> None:
        ctx.log.info("Hi from %s", ctx.device.name)
        ctx.device.set_key_image(ctx.key_index, "/path/to/wave.png")
```

Install into the bundled Python environment and restart the app:

```bash
acc --install-plugin ./my_plugin
```

## The `ActionContext` object

| Attribute       | Description                                                          |
| --------------- | -------------------------------------------------------------------- |
| `ctx.device`    | The device the action is firing on (stream deck / keyboard / mouse). |
| `ctx.key_index` | For stream decks, which key was pressed.                             |
| `ctx.event`     | `press`, `release`, `rotate_cw`, `rotate_ccw`, `dial_press`.         |
| `ctx.value`     | For encoders, the integer delta.                                     |
| `ctx.log`       | Python `logging.Logger` routed to the app logger.                    |
| `ctx.settings`  | Per-action JSON-serializable settings.                               |

## Capability-aware actions

Plugins can gate actions on device capabilities:

```python
from ajazz_plugins import Plugin, action, requires

class RgbWavePlugin(Plugin):
    name = "rgb-wave"

    @action("rgb.wave", title="RGB Wave")
    @requires("rgb")               # will only appear on IRgbCapable devices
    def wave(self, ctx):
        ctx.device.rgb.set_effect("wave", speed=5, color="#ff00ff")
```

Available capability strings: `display`, `rgb`, `encoder`, `key_remap`,
`mouse`, `firmware`.

## Events

Plugins can subscribe to the core event bus:

```python
from ajazz_plugins import Plugin, on_event

class Logger(Plugin):
    name = "logger"

    @on_event("device.connected")
    def device_connected(self, device):
        print(f"connected: {device.name}")
```

Standard topics: `device.connected`, `device.disconnected`,
`profile.activated`, `key.pressed`, `key.released`, `dial.rotated`.

## Testing plugins

Unit test without the app by instantiating the fake context:

```python
from ajazz_plugins.testing import FakeContext
from my_plugin.plugin import HelloPlugin

def test_hello():
    ctx = FakeContext.for_streamdeck("AKP153")
    HelloPlugin().say_hi(ctx)
    assert ctx.log.records[-1].levelname == "INFO"
```

See
[`python/ajazz_plugins/tests/test_plugin_api.py`](https://github.com/Aiacos/ajazz-control-center/blob/main/python/ajazz_plugins/tests/test_plugin_api.py)
for more examples.

## Publishing

- Publish to PyPI under the `ajazz-plugin-*` naming convention.
- Optionally open a PR to add your plugin to the
  [awesome-ajazz-plugins](https://github.com/Aiacos/ajazz-control-center/wiki/Awesome-Plugins)
  page.
