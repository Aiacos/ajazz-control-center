# SPDX-License-Identifier: GPL-3.0-or-later
"""Pure-Python half of the AJAZZ Control Center plugin SDK.

The host process loads this package into its embedded interpreter and exposes
it as ``ajazz``. The ``Plugin`` base class and ``action`` decorator defined
here are the *only* public surface plugins should rely on.
"""

from __future__ import annotations

import inspect
import json
from collections.abc import Callable
from dataclasses import dataclass, field
from typing import Any, ClassVar

__all__ = ["ActionContext", "Plugin", "action"]


@dataclass
class ActionContext:
    """Runtime context passed to every ``@action`` handler.

    The host constructs one ActionContext per dispatch call and passes it as
    the sole argument to the handler (if the handler accepts a parameter).

    Attributes:
        device_codename: Codename of the device that triggered the action
            (e.g. ``"ak820pro"``).  Empty when dispatched programmatically.
        key_index: Zero-based key index on the device.  Zero when the action
            is not tied to a specific key.
        settings: Per-binding settings dictionary deserialised from the JSON
            stored in the profile.  Handlers may read and mutate this dict;
            mutations are not persisted automatically.
    """

    device_codename: str = ""
    key_index: int = 0
    settings: dict[str, Any] = field(default_factory=dict)

    def notify(self, message: str) -> None:
        """Display a desktop notification.

        The production host shows an OS-level notification; this stub prints
        to stdout so plugins can be developed and tested without the host.

        Args:
            message: The notification text to display.
        """
        print(f"[ajazz] {message}")


def action(*, id: str, label: str = "") -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """Decorator that marks a Plugin method as an exposed action.

    The decorator attaches a ``__ajazz_action__`` metadata dict to the
    decorated function so that ``Plugin.actions()`` can discover it via
    ``inspect.getmembers``.

    Args:
        id: Unique action identifier within the plugin (e.g. ``"say-hi"``).
            Must not contain dots; dots are used as the plugin/action separator
            in the C++ dispatch path.
        label: Human-readable label shown in the UI action picker.

    Returns:
        A decorator that annotates the target callable and returns it unchanged.

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
    """Base class for AJAZZ Control Center Python plugins.

    Subclass Plugin, set the class-level attributes, and decorate handler
    methods with ``@action``.  The host calls ``dispatch()`` on each key
    press that matches a bound action.

    Attributes:
        id: Reverse-domain plugin identifier (e.g. ``"com.example.obs"``).
            Must be globally unique.
        name: Human-readable plugin name shown in the UI.
        version: Semantic version string (e.g. ``"1.0.0"``).
        authors: Author list (free-form string).
        permissions: Coarse permissions the plugin requests; surfaced to
            the user at install time and (in a follow-up slice) enforced
            by the per-OS sandbox. Each entry must come from the
            ``Ajazz.Permissions`` enum in
            ``docs/schemas/plugin_manifest.schema.json`` (e.g.
            ``"notifications"``, ``"shell-exec"``, ``"clipboard-read"``).
            Default empty: a plugin that declares nothing gets the
            most-restrictive sandbox profile.
    """

    id: str = ""
    name: str = ""
    version: str = "0.0.0"
    authors: str = ""
    # ClassVar makes the read-only-metadata intent explicit and stops
    # `self.permissions.append(...)` from silently mutating the
    # class-level list shared across instances. Plugin authors should
    # rebind the attribute (`permissions = ["foo", "bar"]`) at the
    # subclass level rather than mutating it.
    permissions: ClassVar[list[str]] = []

    def actions(self) -> dict[str, Callable[..., Any]]:
        """Discover all ``@action``-decorated methods bound to this instance.

        Returns:
            A dict mapping action id strings to bound method callables.
        """
        out: dict[str, Callable[..., Any]] = {}
        for _, method in inspect.getmembers(self, inspect.ismethod):
            meta = getattr(method, "__ajazz_action__", None)
            if meta is None:
                continue
            out[meta["id"]] = method
        return out

    def dispatch(self, action_id: str, settings_json: str) -> None:
        """Route an action invocation to the appropriate handler.

        Called by the C++ host (via pybind11) when the user triggers a bound
        key action.  Deserialises the settings JSON and invokes the matching
        ``@action`` handler.  If the handler accepts zero positional parameters
        it is called without an ActionContext.

        Args:
            action_id: The action id (without the plugin prefix).
            settings_json: JSON string of per-binding settings, or empty string.

        Raises:
            KeyError: If no ``@action`` with the given id exists on this plugin.
        """
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
