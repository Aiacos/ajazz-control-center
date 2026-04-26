<!--
  vendor-techniques.md — RE task 5 deliverable (scaffold).

  Where the AJAZZ vendor app already solves a hard problem better
  than we do (HID reconnect, firmware update retry/rollback, USB
  chunk sizing, profile sync conflict resolution, telemetry
  beaconing, crash-safe persistence), record the *technique* — the
  observable behavior + the abstract algorithm — so we can port the
  IDEA into our stack. Never the code.

  Read `docs/research/README.md` for the rules. The clean-room split
  applies here too: an entry written by an engineer who has read
  vendor source for the same module disqualifies that engineer from
  implementing the matching ported version.

  Until captures (RE task 2) land, this file is methodology + a
  short list of techniques worth investigating first. Resist the
  temptation to fill in details from public reverse-engineering
  blogs — those introduce non-clean-room contamination and can
  carry copyrighted snippets.
-->

# Vendor stability & infrastructure techniques

When the AJAZZ first-party desktop app does something hard *better*
than we do, this file describes the **technique** so we can adopt
the idea (never the code).

> **Read [`docs/research/README.md`](README.md) first.** Clean-room
> rules apply: an engineer who reads vendor source for a given
> module cannot also implement the ported version. Each entry
> should describe behavior observed externally (USB / WS / IPC
> traces, on-disk artefact inspection) — not re-typed source.

> **Status — 2026-04-26**: scaffold only. The "Candidate techniques
> to investigate first" list below is sized by *expected
> stability impact*, not by recon completeness. Sections will be
> filled as captures land per RE task 2.

## Entry shape

Each technique gets its own section laid out as:

1. **Problem** — what hard problem does the technique address? Why
   does the naive implementation fail?
1. **Observed behavior** — what the vendor app does, citing the
   capture id (or another externally-verifiable artefact).
1. **Abstract algorithm** — described in prose + pseudocode, in
   our own words. Not a translation of vendor code.
1. **Cost / complexity** — what does adopting this cost us? What
   constraints does it impose on the surrounding architecture?
1. **Action** — link to a TODO entry or an in-flight PR. Without an
   action this is a blog post, not engineering.

## Candidate techniques to investigate first

These are ranked by *expected stability impact on our codebase*,
not by how impressive they are. They are intentionally short
placeholders today; each should grow into a full entry above as
recon (RE task 2) provides observed-behavior evidence.

### 1. HID reconnect debounce timing

> **Problem we have today**: `DeviceModel` re-enumerates on every
> hidapi hotplug event. A device that briefly disappears (USB-C
> reseat, USB hub power glitch, kernel autosuspend) triggers a full
> re-init that races against the in-flight Stream Dock image
> uploads. A single bad reconnect at the wrong moment leaks the
> previous JPEG batch into the next session.

The vendor app survives this without flicker — debounce window,
state-machine for "transitioning" vs. "online", or a per-device
in-flight-cancel?

- Observation pending — capture a USB unplug+replug under the
  vendor app and time the gap until the first re-init command.
- Action: file as a follow-up under `Architecture refactors → A2`
  once we know the timing window the vendor uses.

### 2. Firmware update retry / rollback

> **Problem we have today**: we do not ship firmware update at all.
> When we do, getting it wrong bricks the device — the vendor app
> handles half-applied flashes with a recovery flow worth copying.

- Observation pending — capture a firmware update on AKP815 (the
  device with the most public version churn) and document: stage
  count, bootloader handoff sequence, the canary command that
  proves the new image booted cleanly, and the rollback that fires
  when the canary times out.
- Action: pin a recon ticket before any of our DFU work starts.

### 3. Per-device USB transfer chunk sizing

> **Problem we have today**: `streamdeck/akp03` and `akp153`
> backends use a fixed image chunk size (~1 KiB header + payload)
> tuned by trial. AKP05 — bigger screens, more pixel data — would
> benefit from a measured per-device size.

The vendor app adapts chunk size per device (likely from the HID
descriptor's `wMaxPacketSize` or a vendor-specific descriptor
extension). Capturing one image-upload per device family will tell
us the algorithm.

- Action: track under `src/devices/streamdeck/<dev>` per-device
  optimisation backlog once measured.

### 4. Profile sync conflict resolution

> **Problem we have today**: `.ajazzprofile` import / export is a
> blob-replace; if a user edits a profile in two places the second
> import wins outright. Vendor app supports cloud sync across
> machines — they must resolve conflicts somehow (last-writer-wins
> with timestamp? CRDT-style merge? user-prompt?).

This is a UX-shape question more than a wire-protocol one — capture
"sync after diverge" via the vendor app and observe the dialog.

- Action: feeds into `Plugin SDK + Store → Catalog backend` and
  potentially a future `acc profile sync` CLI.

### 5. Telemetry beaconing

> **Problem we have today**: telemetry is iceboxed (`TODO.md` →
> Iceboxed). Before we open it, study the vendor app's beacon
> rate, granularity (per-action? per-launch? per-connect?) and
> opt-out path so we can pick a defensible default.

- Action: this is a privacy-first design choice; recon informs the
  *minimum* we'd need to ship if we ever did, never the maximum.

### 6. Crash-safe settings persistence

> **Problem we have today**: `profile_io.cpp` already uses
> `O_EXCL|O_NOFOLLOW` + parent-dir fsync on Linux/macOS and
> `ReplaceFileW` on Windows (SEC-S4 / SEC-S6 hardening). Settings
> persistence in `pi_bridge.cpp` uses `QSaveFile`. We are likely
> already on par here — this entry is to *confirm* the vendor app
> is no better than us, so we can stop worrying about it. Confirm
> by inducing a crash mid-write under the vendor app and checking
> the on-disk artefact survives.

- Action: low priority; close as "no parity gap" once captured.

## How to add a section

1. Run a capture per the methodology in
   [`vendor-protocol-notes.md`](vendor-protocol-notes.md).
1. Translate observations into the entry shape above. Keep
   pseudocode short; the goal is for an implementer who has NOT
   seen the capture to be able to design from scratch.
1. Open a PR. Disclose in the description if you also intend to
   implement the matching ported version — the clean-room policy
   forbids that combination, so your PR should split the work
   between two contributors.
