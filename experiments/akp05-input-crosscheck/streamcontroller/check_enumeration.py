#!/usr/bin/env python3
"""StreamController fork enumeration check for the AKP05E (0300:3004).

StreamController's device backend is its **own fork** of python-elgato-streamdeck
(``StreamController/streamcontroller-python-elgato-streamdeck``, Python + hidapi)
in which non-Elgato clones are added as StreamDeck subclasses -- unlike the
upstream library (tested on the sibling ``python-elgato-streamdeck`` branch),
which only knows Elgato VID 0x0fd9.

So the meaningful question here is one layer up from raw reads: **does the
fork's extended device registry recognise 0300:3004 at all?** If it does,
``DeviceManager().enumerate()`` returns a deck object and the input path is
worth driving; if it does not, the fork has no AKP05E entry and the input
test reduces to the upstream-lib case.

Both the fork and upstream install under the same ``StreamDeck`` package name,
so run this in its OWN venv:
    python3 -m venv /tmp/sc-venv && . /tmp/sc-venv/bin/activate
    pip install streamcontroller-python-elgato-streamdeck hidapi

Run:
    python3 check_enumeration.py

Per project rules we do NOT system-install StreamController (a GTK4 app); this
checks only the device-enumeration layer, which is where AKP05E support would
first appear. The README documents the full manual test procedure.
"""

from __future__ import annotations

import sys

try:
    from StreamDeck.DeviceManager import DeviceManager
except ImportError:
    DeviceManager = None

VID = 0x0300
PID = 0x3004


def _call(obj: object, name: str) -> object:
    """Call obj.name() if it exists and is callable, else return None."""
    attr = getattr(obj, name, None)
    return attr() if callable(attr) else None


def main() -> int:
    """Enumerate via the StreamController fork; report whether the AKP05E appears."""
    if DeviceManager is None:
        sys.exit(
            "StreamDeck package not importable. Install the fork in a venv: "
            "pip install streamcontroller-python-elgato-streamdeck hidapi"
        )

    decks = DeviceManager().enumerate()
    print(f"Fork enumerated {len(decks)} deck(s).")
    hit = False
    for deck in decks:
        vid = _call(deck, "vendor_id")
        pid = _call(deck, "product_id")
        name = _call(deck, "deck_type") or type(deck).__name__
        if vid == VID and pid == PID:
            hit = True
            marker = "  <-- AKP05E (0300:3004)"
        else:
            marker = ""
        if isinstance(vid, int) and isinstance(pid, int):
            ids = f"vid={vid:#06x} pid={pid:#06x}"
        else:
            ids = "ids=?"
        print(f"  {name}: {ids}{marker}")

    if hit:
        print("\nRESULT: the fork RECOGNISES the AKP05E. Drive its input path next.")
        return 0
    print(
        "\nRESULT: the fork does NOT list 0300:3004. Its device registry has no "
        "AKP05E entry -- add a StreamDeck subclass for it, or the test reduces to "
        "the upstream python-elgato-streamdeck case."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
