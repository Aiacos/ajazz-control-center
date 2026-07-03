# AKP05E input cross-check — StreamController

**Spike branch. Not for merge into `develop`.** Third of three per-stack spike
branches probing the same question (see also `python-elgato-streamdeck` and
`boatswain`).

## Context: StreamController was already evaluated and rejected

See `docs/architecture/STREAMCONTROLLER-EVALUATION.md` (Status: evaluation
only, no fork). It was rejected as an app foundation because it cannot run
Elgato `.sdPlugin` plugins, is a standalone GTK4 app (not embeddable), is
GPL-3.0, and is decks-only. **This branch is not re-opening that decision** —
it uses StreamController purely as an independent device stack to cross-check
the AKP05E input finding.

## Hypothesis under test

The `0300:3004` demo unit never emits input reports (proven against five
methods in `docs/protocols/streamdeck/akp05_input_corrections.md` §7.1). Does
StreamController's stack read anything the others miss?

## Why this test is one notch different from the upstream-lib test

Per the evaluation doc, StreamController's device backend is its **own fork**
of python-elgato-streamdeck (`StreamController/streamcontroller-python-elgato-streamdeck`,
Python + **hidapi**) in which non-Elgato clones are registered as StreamDeck
subclasses. Upstream python-elgato-streamdeck (the sibling branch) only knows
Elgato VID `0x0fd9`; the **fork may already carry an AKP05E/Mirabox entry**.
So the first question is enumeration, not raw reads:

```bash
python3 -m venv /tmp/sc-venv && . /tmp/sc-venv/bin/activate
pip install streamcontroller-python-elgato-streamdeck hidapi
python3 check_enumeration.py
```

- **Fork lists 0300:3004** → it has an AKP05E device class; drive its input
  path (its `read()` loop) and watch for frames.
- **Fork does not list it** → no AKP05E entry; the input test collapses into
  the upstream `python-elgato-streamdeck` branch result.

Since the transport is hidapi — the same family the upstream-lib branch drives
— a zero-input result here corroborates rather than adds a fully independent
transport (that role is Boatswain's libusb path).

## The device seam worth noting

The evaluation doc records that StreamController isolates all device I/O behind
a single `BetterDeck` adapter, with a `Subclasses/FakeDeck.py` mock and a
network `RemoteDeck` — the same out-of-process device shape as our
`SidecarStreamDockDevice`. A `MirajazzDeck` subclass (~2–3 files) would let
StreamController drive AKP05E hardware through our sidecar contract if that
ever becomes desirable.

## Manual test procedure (full app)

Per project rules we do **not** system-install StreamController here.

1. `git clone https://github.com/StreamController/StreamController`
1. Follow its README to run from source (it pulls the fork above as a dep).
1. Plug in the AKP05E. If it does not appear, the fork lacks the device class
   (see enumeration check) — add a subclass or stop here.
1. If it appears, press keys / turn dials / touch the strip and watch the
   StreamController log for input events.

## Reading the result

- **No input** → agrees with the proof chain; expected for the demo firmware.
- **Any input** → capture it; reconcile the report layout with
  `parseInputReport` in `akp05_input_corrections.md`.
