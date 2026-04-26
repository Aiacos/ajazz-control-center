<!--
  docs/research/ — vendor reverse-engineering & parity track.

  This directory holds the public-facing research outputs of the
  reverse-engineering campaign described in TODO.md →
  "Reverse-engineering & vendor parity". The end goal is feature
  parity with the AJAZZ first-party desktop apps (Stream Dock,
  keyboard / mouse drivers) so users can ship a single open client
  on Linux / Windows / macOS instead of the proprietary stack, and
  so we can cross-pollinate techniques the vendor has already
  battle-tested (HID reconnect debounce, firmware update retry,
  per-device USB chunk sizes, …).

  Hard rules — apply to every file under this directory:

    1. CLEAN-ROOM. No vendor binary, decompiled source, copyrighted
       art or string-table dump lands in version control. Notes are
       written from observed behavior (USB / WebSocket / IPC
       captures, on-disk artefact inspection of CONFIG-only files),
       not from re-typed vendor source. The single engineer who
       reads vendor sources for a given module writes the spec but
       does NOT contribute to the matching implementation; a second
       "clean" engineer implements from the spec. This split is
       enforced socially, not by tooling — the commit logs of any
       implementation file should never share an author with the
       file that documents the protocol it follows.

    2. NO REDISTRIBUTION. We never check vendor installers into
       this repo, link to a private mirror, or publish a torrent.
       The inventory only points at vendor-published URLs (or third-
       party CDN mirrors that the vendor itself uses for delivery —
       e.g. shopify CDN for keyboard / mouse drivers) and records
       integrity metadata so a downstream engineer can verify the
       artefact they are looking at matches the one we documented.

    3. ABSOLUTE DATES. Every observation that depends on time
       (release date, capture date, version-was-current-on-X) must
       be written as ISO-8601 (`2026-04-26`). Relative dates
       ("yesterday", "last week") rot.

  File index:

    * `vendor-software-inventory.md` — TASK 1: where the official
      AJAZZ software lives, what version, what hash, what OS. Fills
      in over time as new releases ship.
    * `vendor-protocol-notes.md` — TASK 2: clean-room protocol &
      feature inventory. Per-module sections (Stream Dock, keyboard,
      mouse, dial widgets, firmware update) with what we observed,
      what we inferred, and what we did NOT verify. Capture metadata
      (Wireshark / usbmon session ID, environment, device firmware)
      is required for every entry.
    * `vendor-feature-matrix.md` — TASK 3: gap analysis. One row per
      user-facing feature; columns: vendor app status, our status
      (✅ done / 🟡 partial / ❌ missing), implementing module link
      or TODO link, notes.
    * `vendor-techniques.md` — TASK 5: cross-pollination notes.
      Where the vendor solves a hard problem better than we do (e.g.
      reconnect timing, firmware update retries), describe the
      *technique* (never the code) so we can port the idea into our
      stack.

  See TODO.md → "Reverse-engineering & vendor parity" for the full
  task breakdown and effort estimates.
-->

# Vendor reverse-engineering & parity research

This directory holds the public artefacts of the AJAZZ vendor
parity track. **Read the comment block at the top of this file
before contributing** — there are hard clean-room and no-
redistribution rules that everyone touching this directory must
follow.

## Files

| File                                                           | Purpose                                                                                    | Source TODO |
| -------------------------------------------------------------- | ------------------------------------------------------------------------------------------ | ----------- |
| [`vendor-software-inventory.md`](vendor-software-inventory.md) | URL + hash + OS + version of every official AJAZZ desktop app we know about.               | RE task 1   |
| [`vendor-protocol-notes.md`](vendor-protocol-notes.md)         | Clean-room observations of the vendor wire protocols.                                      | RE task 2   |
| [`vendor-feature-matrix.md`](vendor-feature-matrix.md)         | Gap analysis: vendor features vs. our coverage.                                            | RE task 3   |
| [`vendor-techniques.md`](vendor-techniques.md)                 | Stability techniques the vendor uses that we should consider porting (idea-only, no code). | RE task 5   |

## Status

| Track                     | Status                                                                                                         | Last updated |
| ------------------------- | -------------------------------------------------------------------------------------------------------------- | ------------ |
| Inventory (URLs + hashes) | 🟡 partial — Stream Dock, AK keyboards, AJ mice covered; Mirabox-branded Stream Dock and Maude not yet probed. | 2026-04-26   |
| Protocol notes            | ⚪ scaffold only — no captures performed yet.                                                                  | 2026-04-26   |
| Feature matrix            | ⚪ scaffold only — populated as captures land.                                                                 | 2026-04-26   |
| Techniques                | ⚪ scaffold only — populated as recon surfaces patterns.                                                       | 2026-04-26   |
