# Phase 24: Family Coverage AKP03/153/815 - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions + existing-code survey

<domain>
## Phase Boundary

Phase 24 generalises the AKP05E vertical slice (Phases 14-19) across the rest of the Stream Dock
family — **AKP03** (6 LCD keys + 3 side buttons + **3 encoders**), **AKP153** (15 keys), **AKP815**
(15 keys) — via the capability-generic control service. It is **generalisation, not new protocol
work**: the per-family backends (`akp03.cpp`, `akp153.cpp`, `akp815.cpp`) and the
`image_pipeline` per-family transforms (AKP153 Rot90+mirror, AKP815 Rot180, AKP05 Rot0) already
exist.

**Delivers (DEVICES-10):**

- On **AKP03**, assigning a key image shows it on the physical key and pressing the key fires its
  bound action via the **same capability-generic control service** (its 3 encoders route too).
- On **AKP153** and **AKP815**, the same assign-image-and-press flow works with the device's
  per-family **init sequence** and **image format/rotation** honored.
- Per-family differences (key count, grid, encoder count, image resolution/rotation/format, init)
  are **table-driven from the device descriptor**, not hardcoded per call site.

**Out of scope:** hardware verification (Phase 25). AKP153/AKP815 may not be physically connected —
the `MockTransport` byte tests are the gate; real-hardware confirmation covers whatever is plugged in.
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Generalise, do NOT rebuild

- The Phase-14 control service + Phase-15 input routing are **capability-generic** (they
  `dynamic_cast` to `IDisplayCapable`/the input contract, not to `Akp05Device`). Phase 24 confirms
  - fills the per-family gaps so the SAME service drives AKP03/153/815 — no per-device branching at
    the call site.
- Per-family geometry/format/init comes from the **device descriptor** (`register.cpp` rows +
  `image_pipeline` `ImageTransform`): AKP03 6 keys/3 encoders, AKP153/AKP815 15 keys, AKP153
  Rot90+mirror, AKP815 Rot180. The control service reads the descriptor; it does not hardcode 10
  keys / 2x5 / Rot0.
- Reuse the per-family backends' existing wire methods (`setKeyImage`/`setBrightness`/`clearKey`/
  input parse) — do NOT alter wire code (RE source of truth).

### Honesty

- A family member that is NOT physically connected is gated behind the descriptor + `MockTransport`
  byte tests; do not claim live `functional` for a device nobody plugged in (the live promotion is
  whatever Phase 25 / the connected hardware confirms).

### Claude's Discretion (planner/executor)

- Whether any control-service code still implicitly assumes AKP05 geometry (10 keys / encoders /
  touch) and must be made descriptor-driven; the fix is parameterisation, not duplication.
- AKP03 encoder routing reuse (3 encoders) through the Phase-15 path.
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

- `src/devices/streamdeck/src/akp03.cpp` + `akp03_protocol.hpp` (6 keys + 3 side + 3 encoders), `akp153.cpp` + `akp153_protocol.hpp` (15 keys), `akp815.cpp` + `akp815_protocol.hpp` (15 keys).
- `src/devices/streamdeck/src/image_pipeline.hpp` — per-family `ImageTransform` (AKP153 Rot90+mirror, AKP815 Rot180, AKP05 Rot0).
- `src/devices/streamdeck/src/register.cpp` — per-family descriptors (keyCount/gridColumns/encoderCount/hasTouchStrip).
- `src/core/include/ajazz/core/capabilities.hpp` — `IDisplayCapable`/`DisplayInfo` (the capability-generic contract).
- `.planning/phases/14-...control-service/14-02-PLAN.md` + `15-...input-routing/15-01-PLAN.md` + `19-...bridge/19-*` — the AKP05E template to generalise (the control service + input + bridge).
- CLAUDE.md — cross-platform build strictness; RE-source-of-truth; COD-031; ASCII test names; PacketSize family note (Phase-10 DISPLAY-01: 512→1024 unblocked the family).
  \</canonical_refs>

<specifics>
## Specific Ideas

- Verification is hardware-free: per-family `MockTransport` byte tests via `makeAkp03WithTransport`
  / `makeAkp153WithTransport` / `makeAkp815WithTransport` (or the registry) — assert the
  control-service assign-image emits the right per-family header + rotation/format, and that a fed
  input event fires the bound action, for AKP03 (incl. its 3 encoders), AKP153, AKP815. The
  descriptor drives geometry (no hardcoded 10/2x5/Rot0). Real-hardware confirmation covers whatever
  family member is connected; the rest is descriptor + MockTransport gated. ASCII names.

</specifics>

<deferred>
## Deferred Ideas

- Live hardware confirmation across the family → Phase 25 / whatever is connected.
- AKP815 dedicated factory (currently reuses AKP153) — `TODO.md` carry-over, not required for DEVICES-10.

</deferred>

______________________________________________________________________

*Phase: 24-family-coverage-akp03-153-815*
*Context gathered: 2026-05-23 (v1.3 replan; generalisation of the 14-19 template; family backends pre-built)*
