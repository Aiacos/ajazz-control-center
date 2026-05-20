# Phase 11: AJAZZ 2.4G 8K Mouse Probe-and-Confirm - Context

**Gathered:** 2026-05-20
**Status:** Ready for planning
**Mode:** Smart-discuss (autonomous), grey areas accepted as ADR/ROADMAP-grounded defaults

<domain>
## Phase Boundary

Promote the `3151:5007` 8K mouse backend: a user can cycle 8 DPI stages (field-determined; `devices.yaml dpi_stages: 8`, corrected from the earlier 6 assumption) in
the firmware-captured cycle order, set per-stage DPI / colour / LOD, set
polling rate up to 8000 Hz (with an honest USB 2.0 cap warning), and set
per-zone RGB. The `devices.yaml` row reflects the captured AJ199 V1.0-vs-Max
envelope outcome. Maturity `scaffolded` → `partial` (→ `functional` only if
captures confirm the full envelope).

**Out of this phase:** the AJ-series wire-format reconciliation capture
itself (the P0 open item) — this phase plans against the documented envelope
and gates the byte-level encoder on capture confirmation.

</domain>

<decisions>
## Implementation Decisions

### Accepted grey-area defaults (2026-05-20)

- **Wire envelope is CAPTURE-GATED.** Plan against the documented AJ199
  envelope but mark the byte-level encoder with a `// CAPTURE-PENDING`
  switch-point; the V1.0 (OemDrv, 17-byte) vs Max (HIDUsb, 20-byte)
  reconciliation resolves only when the AJ-series capture lands. Do NOT
  speculatively pick an envelope.
- **Maturity target: `partial`** unless the capture confirms the full
  envelope, in which case `functional`.
- **DPI cycle order from firmware capture, NOT naive +1** (Pitfall 28).
- **RGB zone count + names derived from the capability descriptor, NOT
  hardcoded** (Pitfall 22).
- Documented opcodes to wire: `setStageDpi` (cmd `0x21`), LOD (cmd `0x23`),
  polling rate (cmd `0x22`), per-zone RGB (cmd `0x30`).
- 8000 Hz selection on a USB 2.0 host port surfaces an honest cap warning.

### Claude's Discretion

Error-message wording, test fixture shapes, and the UI scale-mapping detail
follow existing mouse-backend + `IDpiCapable`/`IPollingRateCapable`
conventions.

</decisions>

\<code_context>

## Existing Code Insights

- `src/devices/mouse/src/aj_series.cpp` — current envelope is 64-byte /
  ReportId `0x05` / sum-mod-256, which structurally resembles Witmod but
  matches neither OemDrv (AJ199 V1.0) nor HIDUsb (AJ199 Max). This is the
  P0 "AJ-series wire format reconciliation" divergence (likely-broken).
- `docs/protocols/mouse/aj_series.md` — current doc is stale on multiple
  axes; this phase extends it with the captured `3151:5007` envelope.
- `IDpiCapable`, `IPollingRateCapable`, `IMouseCapable` (RGB) — capability
  interfaces the backend implements.
- `docs/_data/devices.yaml` — `ajazz_24g_8k` row maturity + notes updated.

**Build precondition:** C++ build currently fails to configure (Qt6
CorePrivate / qzipreader_p.h — needs qt6-qtbase-private-devel). Execution
blocked until resolved.

\</code_context>

<specifics>
## Specific Ideas (ROADMAP success criteria)

1. `aj_series.md` extended with the captured AJ199 V1.0-vs-Max reconciliation for `3151:5007`.
1. 8 DPI stages (field-determined count) in firmware-captured cycle order; state persists across power-cycle.
1. Per-stage DPI (`0x21`), colour indicator, LOD (`0x23`) set independently.
1. Polling 1000/2000/4000/8000 Hz (`0x22`) with honest USB 2.0 cap warning.
1. Per-zone RGB (`0x30`); zone count/names from capability descriptor.
1. `devices.yaml` notes + maturity `scaffolded` → `partial`.

</specifics>

<deferred>
## Deferred Ideas

- **AJ-series wire-format reconciliation capture** (the P0 item) — finalizes
  the envelope choice; this phase's byte encoder gates on it.
- **Real-hardware verification** — needs a physical `3151:5007`.
- **Build fix** — Qt6 CorePrivate precedes any compile/test step.

</deferred>
