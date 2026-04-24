# SPDX-License-Identifier: GPL-3.0-or-later
"""Minimal example plugin. Copy it to
``~/.config/ajazz-control-center/plugins/hello/`` to enable it."""

from ajazz_plugins import ActionContext, Plugin, action


class Hello(Plugin):
    id = "com.example.hello"
    name = "Hello World"
    version = "1.0.0"
    authors = "AJAZZ Control Center contributors"

    @action(id="say-hi", label="Say hi")
    def say_hi(self, ctx: ActionContext) -> None:
        ctx.notify("Hello from Python!")

    @action(id="counter", label="Increment counter")
    def counter(self, ctx: ActionContext) -> None:
        state = ctx.settings.setdefault("state", {"count": 0})
        state["count"] += 1
        ctx.notify(f"count: {state['count']}")


Plugin = Hello  # expected by the loader
