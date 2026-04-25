# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the Python half of the AJAZZ plugin SDK.

Verifies:
- ``@action`` decorator registration and discovery via ``Plugin.actions()``.
- ``Plugin.dispatch()`` routing to the correct handler with and without an
  ActionContext argument.
- Graceful handling of invalid JSON in the settings payload.
- Correct exception type when dispatching an unknown action id.
"""

from __future__ import annotations

from ajazz_plugins import ActionContext, Plugin, action


class MyPlugin(Plugin):
    """Minimal in-test plugin fixture used by all test functions."""

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
    """Plugin.actions() must return every @action-decorated method by id."""
    plugin = MyPlugin()
    assert set(plugin.actions().keys()) == {"hello", "no-ctx"}


def test_dispatch_routes_to_method() -> None:
    """dispatch() deserialises settings JSON and passes an ActionContext to the handler."""
    plugin = MyPlugin()
    plugin.dispatch("hello", '{"foo": "bar"}')
    assert plugin.calls == [("hello", {"foo": "bar"})]


def test_dispatch_handles_no_ctx() -> None:
    """dispatch() calls a zero-parameter handler without an ActionContext."""
    plugin = MyPlugin()
    plugin.dispatch("no-ctx", "")
    assert plugin.calls == [("no-ctx", {})]


def test_dispatch_handles_invalid_json() -> None:
    """dispatch() falls back to an empty settings dict when JSON is malformed."""
    plugin = MyPlugin()
    plugin.dispatch("hello", "not-json")
    assert plugin.calls == [("hello", {})]


def test_dispatch_raises_on_unknown_action() -> None:
    """dispatch() raises KeyError when the action id is not registered on the plugin."""
    plugin = MyPlugin()
    try:
        plugin.dispatch("missing", "{}")
    except KeyError:
        pass
    else:
        raise AssertionError("expected KeyError")
