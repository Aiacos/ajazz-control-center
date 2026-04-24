# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the Python half of the plugin SDK."""

from __future__ import annotations

from ajazz_plugins import ActionContext, Plugin, action


class MyPlugin(Plugin):
    id = "test.my-plugin"
    name = "Test plugin"

    def __init__(self) -> None:
        self.calls: list[tuple[str, dict]] = []

    @action(id="hello", label="Hello")
    def _hello(self, ctx: ActionContext) -> None:
        self.calls.append(("hello", dict(ctx.settings)))

    @action(id="no-ctx", label="No context")
    def _no_ctx(self) -> None:
        self.calls.append(("no-ctx", {}))


def test_actions_are_discovered() -> None:
    plugin = MyPlugin()
    assert set(plugin.actions().keys()) == {"hello", "no-ctx"}


def test_dispatch_routes_to_method() -> None:
    plugin = MyPlugin()
    plugin.dispatch("hello", '{"foo": "bar"}')
    assert plugin.calls == [("hello", {"foo": "bar"})]


def test_dispatch_handles_no_ctx() -> None:
    plugin = MyPlugin()
    plugin.dispatch("no-ctx", "")
    assert plugin.calls == [("no-ctx", {})]


def test_dispatch_handles_invalid_json() -> None:
    plugin = MyPlugin()
    plugin.dispatch("hello", "not-json")
    assert plugin.calls == [("hello", {})]


def test_dispatch_raises_on_unknown_action() -> None:
    plugin = MyPlugin()
    try:
        plugin.dispatch("missing", "{}")
    except KeyError:
        pass
    else:
        raise AssertionError("expected KeyError")
