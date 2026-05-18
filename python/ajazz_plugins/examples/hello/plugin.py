# SPDX-License-Identifier: GPL-3.0-or-later
"""Hello-world example plugin for the AJAZZ Control Center plugin SDK.

Copy this directory to
``~/.config/ajazz-control-center/plugins/hello/`` to enable the plugin.
The plugin exposes two actions:

- ``say-hi``  — pops a desktop notification.
- ``counter`` — increments a persistent per-binding counter and reports it.
"""

from typing import ClassVar

from ajazz_plugins import ActionContext, action
from ajazz_plugins import Plugin as _PluginBase


class Hello(_PluginBase):
    """Minimal example plugin demonstrating the AJAZZ plugin SDK surface."""

    id = "com.example.hello"
    name = "Hello World"
    version = "1.0.0"
    authors = "AJAZZ Control Center contributors"
    # Both actions call ctx.notify(), which would surface a desktop
    # notification under the production host — declare it so the user
    # sees it during install-time review.
    permissions: ClassVar[list[str]] = ["notifications"]

    @action(id="say-hi", label="Say hi")
    def say_hi(self, ctx: ActionContext) -> None:
        """Send a greeting notification.

        Args:
            ctx: Runtime context injected by the host.
        """
        ctx.notify("Hello from Python!")

    @action(id="counter", label="Increment counter")
    def counter(self, ctx: ActionContext) -> None:
        """Increment a per-binding press counter and report the new value.

        The count is stored inside ``ctx.settings`` under the ``state`` key.
        Note: mutations to ``ctx.settings`` are not persisted across sessions
        by this stub implementation.

        Args:
            ctx: Runtime context injected by the host.
        """
        state = ctx.settings.setdefault("state", {"count": 0})
        state["count"] += 1
        ctx.notify(f"count: {state['count']}")


# The loader expects a top-level `Plugin` attribute on the module bound
# to the plugin class. The base class is imported as `_PluginBase` so
# this top-level alias is the only public `Plugin` symbol in this
# module — mypy is happy and the loader finds it by name.
Plugin: type[_PluginBase] = Hello
