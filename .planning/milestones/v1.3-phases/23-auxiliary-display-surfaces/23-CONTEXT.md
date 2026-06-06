# Phase 23: Auxiliary Display Surfaces - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions + akp05.md / akp05_vendor.md §4 + existing-code survey

<domain>
## Phase Boundary

Phase 23 lets the user assign images to the AKP05E's **auxiliary surfaces** beyond the 10 keys —
the per-encoder overlays, the main LCD strip, and the touch strip — plus the **`DRA`
rect-addressable** partial-zone upload. Like Phase 14, the device-backend wire methods already
exist (`akp05.cpp` `setMainImage` (MAI), `setEncoderImage` (ENC), the DRA builder; `IDisplayCapable`
declares `setMainImage`/`setEncoderImage`/`setTouchStripImage`); this phase is **app-layer wiring +
the provisional-framing reconciliation**. **HARDWARE-GATED** — the live framing confirmation is
Phase 25; Phase 23's gating proof is `MockTransport` byte tests.

**Delivers (DISPLAY-10):**

- Assigning an image to a **per-encoder overlay** shows it (rendered as one of the 4 touch-strip
  zones — **reconcile** the in-code per-encoder `ENC` LCD model (100×100) against akp05.md's "no
  separate encoder LCD; overlays are touch-strip zones").
- Assigning an image to the **main LCD strip** shows it (`MAI`, 800×100).
- Assigning an image to the **touch strip** shows it.
- The **`DRA` rect-addressable** opcode uploads a single zone without re-encoding the whole
  800×480 panel (vendor §10 P0 bandwidth win).
- Per-surface framing matches what the device accepts; where the provisional §5 RE contradicts the
  hardware, the RE doc is updated (the reconciliation lands in Phase 25; Phase 23 ships the wiring
  - flags the divergence).

**Out of scope:** family coverage (Phase 24); the live hardware confirmation (Phase 25).
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Reuse the backend wire methods (do NOT add new builders)

- Drive the existing `Akp05Device::setMainImage`/`setEncoderImage`/`setTouchStripImage` + the DRA
  builder (`buildSecondaryScreenRectImageHeader` / `CmdSecondaryScreen`) through the **Phase-14
  control service** (held-open handle) + the **Phase-16 editor** (assignment targets beyond keys).
  Encode via `image_pipeline` (encoder 100×100, main 800×100). The wire format is RE-sourced — do
  NOT alter it.

### Provisional-framing reconciliation (the load-bearing honesty deliverable)

- The code models per-encoder **`ENC` LCDs (100×100)**; akp05.md states the AKP05E has **no
  separate encoder LCDs** — the per-encoder graphics render as **4 zones of the 800×480 touch
  strip**. Phase 23 ships the wiring for BOTH the `ENC` path (as-coded) AND the touch-strip-zone
  path (via `DRA`), and **flags this divergence as PROVISIONAL** (§5). Do NOT delete the `ENC` path
  blind — the RE is the source of truth and the **hardware wins** in Phase 25, where the doc is
  reconciled. Treat the DRA rect layout (location/width/height/x/y) as a hypothesis to verify.

### Hardware gating

- HARDWARE-GATED: requires the physical AKP05E (`0300:3004`, fw `V3.AKP05E.01.007`) with a working
  `uaccess` ACL for the LIVE confirmation (Phase 25; replug/`setfacl` if root-only — systemd ≥258).
  Phase 23's automated gate is `MockTransport` byte-level assertions (ENC index byte, MAI
  whole-strip, DRA rect header) — no hardware needed to land the wiring + tests.

### Claude's Discretion (planner/executor)

- Editor UX for assigning to encoder/strip/touch surfaces (extend the Phase-16 editor with
  per-surface targets).
- Whether to expose `DRA` partial-zone upload now or wire it behind the per-encoder-overlay path.
- How `setTouchStripImage` (declared in `IDisplayCapable`) maps to the wire (confirm it's
  implemented in the backend vs needs wiring).
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

- `docs/protocols/streamdeck/akp05.md` (§Hardware/§Layout: "Encoder LCD overlays: None — rendered inside the touch strip"; the 4-zone touch model) + `akp05_vendor.md` §4 (ENC/MAI/DRA opcode table; DRA 32-byte rect header layout — PROVISIONAL).
- `src/devices/streamdeck/src/akp05.cpp` — `setMainImage` (:604), `setEncoderImage` (:635), the DRA path (:112); `akp05_protocol.hpp` — `CmdMainImage`/`CmdSecondaryScreen` (DRA), `buildEncoderImageHeader`/`buildMainImageHeader`/`buildSecondaryScreenRectImageHeader`, `EncoderScreenWidthPx/HeightPx`.
- `src/devices/streamdeck/src/image_pipeline.hpp` — encoder 100×100, main strip 800×100 dims (ARCH-04).
- `src/core/include/ajazz/core/capabilities.hpp` — `IDisplayCapable::setMainImage` (:182), `setEncoderImage` (:760), `setTouchStripImage` (:1566).
- `.planning/phases/14-...control-service/14-02-PLAN.md` — the control-service paint path to extend; `.planning/phases/16-...pages/16-*` — the editor assignment.
- CLAUDE.md — RE-is-source-of-truth (provisional §5 = hypotheses; hardware wins); COD-031; never skip pre-commit; ASCII test names.
  \</canonical_refs>

<specifics>
## Specific Ideas

- Verification is hardware-free for the wiring: `MockTransport` byte assertions — `setEncoderImage`
  emits the `ENC` header with the right encoder index; `setMainImage` emits `MAI` for the whole
  strip; the `DRA` path emits the rect header with the location/width/height/x/y fields per the
  provisional §5 layout; encode dims come from the descriptor (100×100 / 800×100), not hardcoded at
  the call site. The LIVE confirmation (the image actually appears on the encoder zone / strip, and
  the DRA rect lands where expected) is the **Phase-25** hardware witness; where the device
  contradicts the provisional framing, Phase 25 updates the RE doc + the code. ASCII test names.

</specifics>

<deferred>
## Deferred Ideas

- Family coverage (AKP03/153/815 aux surfaces) → Phase 24.
- LIVE hardware confirmation + provisional §5 reconciliation (ENC-vs-zone, DRA rect) → Phase 25 (VERIFY-05).
- Boot logo (`LOG`) / `M_V` secondary-screen logo → backlog (vendor §10 P3).

</deferred>

______________________________________________________________________

*Phase: 23-auxiliary-display-surfaces*
*Context gathered: 2026-05-23 (v1.3 replan; HARDWARE-GATED; backend aux wire methods pre-built)*
