# SPDX-License-Identifier: GPL-3.0-or-later
"""Pure-Python half of the AJAZZ Control Center plugin SDK.

The host process loads this package into its embedded interpreter and exposes
it as ``ajazz``. The ``Plugin`` base class and ``action`` decorator defined
here are the *only* public surface plugins should rely on.
"""

from __future__ import annotations

import inspect
from dataclasses import dataclass, field
from typing import Any, Callable

__all__ = ["Plugin", "action", "ActionContext"]


@dataclass
class ActionContext:
    """Runtime context passed to every ``@action`` handler."""

    device_codename: str = ""
    key_index: int = 0
    settings: dict[str, Any] = field(default_factory=dict)

    def notify(self, message: str) -> None:
        """Display a desktop notification (no-op in the Python stub)."""
        print(f"[ajazz] {message}")


def action(*, id: str, label: str = "") -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """Mark a method as an exposed plugin action.

    Example::

        class Hello(Plugin):
            id = "com.example.hello"

            @action(id="say-hi", label="Say hi")
            def say_hi(self, ctx: ActionContext) -> None:
                ctx.notify("Hello!")
    """

    def decorator(func: Callable[..., Any]) -> Callable[..., Any]:
        func.__ajazz_action__ = {"id": id, "label": label}  # type: ignore[attr-defined]
        return func

    return decorator


class Plugin:
    """Base class for AJAZZ Control Center Python plugins."""

    id: str = ""
    name: str = ""
    version: str = "0.0.0"
    authors: str = ""

    def actions(self) -> dict[str, Callable[..., Any]]:
        out: dict[str, Callable[..., Any]] = {}
        for _, method in inspect.getmembers(self, inspect.ismethod):
            meta = getattr(method, "__ajazz_action__", None)
            if meta is None:
                continue
            out[meta["id"]] = method
        return out

    def dispatch(self, action_id: str, settings_json: str) -> None:
        """Entry point invoked by the C++ host."""
        import json

        handler = self.actions().get(action_id)
        if handler is None:
            raise KeyError(f"unknown action: {action_id}")

        try:
            settings = json.loads(settings_json) if settings_json else {}
        except json.JSONDecodeError:
            settings = {}

        ctx = ActionContext(settings=settings)
        sig = inspect.signature(handler)
        if len(sig.parameters) == 0:
            handler()
        else:
            handler(ctx)
