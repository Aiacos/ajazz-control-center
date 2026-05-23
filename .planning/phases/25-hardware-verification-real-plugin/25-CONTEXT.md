# Phase 25: Hardware Verification + Real Plugin - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions

<domain>
## Phase Boundary

Phase 25 is the **milestone close-out + verification gate** (HARDWARE-GATED). It verifies the whole
v1.3 vertical slice on the physically connected **AKP05E** (`0300:3004`, fw `V3.AKP05E.01.007`),
reconciles every provisional §5 RE value the milestone touched, and proves the Elgato-compatible
SDK by running a **real third-party `.sdPlugin`** end-to-end. It is a verification phase, not new
feature work — the load-bearing deliverable is the RE-doc reconciliation that promotes the family
`scaffolded` → `functional`/`verified` honestly.

**Delivers (VERIFY-05, VERIFY-06):**

- **VERIFY-05** — on the connected AKP05E, a human operator confirms: an assigned image appears on
  the key; a key press fires its action; **each of the 4 encoders fires on rotate (CW/CCW) and on
  press**; a touch-zone tap fires the under-encoder action; swipe changes pages; the brightness
  slider changes panel brightness; "clear all" blanks the panel. Every provisional §5 wire item
  (DRA / encoder-overlay framing — ENC-LCD vs touch-strip-zone / touch zone+swipe map) is
  reconciled and the relevant `docs/protocols/streamdeck/**` RE doc is updated where the hardware
  contradicts a provisional value. `hasClock=false` confirmed (no Sync button on the AKP05E row).
- **VERIFY-06** — a real third-party Elgato/Mirabox `.sdPlugin` registers over the loopback
  WebSocket, paints a key via `setImage`, and a physical key press / encoder turn delivers
  `keyDown`/`dialRotate` to the plugin with its observable effect.

**Out of scope:** new features. Phase 25 closes the milestone.
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Verification, not features

- Phase 25 is a **UAT runbook + RE reconciliation**, not new code (beyond small RE-doc/code fixes
  if the hardware contradicts a provisional value — where it does, **the hardware wins** and the RE
  doc + the affected code are corrected, per CLAUDE.md).
- **HARDWARE-GATED:** requires the AKP05E physically connected with a working `uaccess` ACL. If
  `/dev/hidraw*` is root-only after a USB re-enumeration (systemd ≥258 regression), physically
  replug or `setfacl -m u:$(id -u):rw /dev/hidraw*` per CLAUDE.md. This phase **cannot be
  auto-executed** — it gates on the operator running the runbook against the device.
- Depends on the whole slice (14-23) being executed; it interleaves with the milestone close.

### Provisional §5 reconciliation (the load-bearing deliverable)

- Confirm-or-correct: the **DRA rect header** layout, the **encoder-overlay framing** (the in-code
  per-encoder `ENC` LCD model vs akp05.md's touch-strip-zone model — Phase 23 shipped both; Phase
  25 picks the real one on hardware), the **touch zone + swipe** coordinate map. Update
  `docs/protocols/streamdeck/akp05*.md` + the code wherever the device contradicts the provisional
  value.

### Honesty

- `hasClock=false` for `akp05e` confirmed on hardware (Stream Dock has no firmware RTC per ARCH-05);
  the Sync button must NOT appear on the AKP05E row.

### Claude's Discretion (planner/executor)

- The exact UAT runbook structure (a `25-UAT.md` checklist the operator walks) + which real
  `.sdPlugin` to use for VERIFY-06 (a simple Elgato/Mirabox HTML or node plugin).
- Whether any reconciliation finding spawns a small follow-up fix task vs an RE-doc-only update.
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

- `docs/protocols/streamdeck/akp05.md` + `akp05_vendor.md` §4 (the provisional §5 items to reconcile: DRA rect header, ENC-vs-zone, touch zone+swipe).
- The Phase 14-23 plans/SUMMARYs (the features to verify): control service (14), input (15), controls+pages (16), plugin protocol (17), spawn (18), bridge (19), PI (20), built-in actions (21), store (22), aux surfaces (23).
- `src/devices/streamdeck/src/register.cpp` (akp05e `hasClock` — confirm `false`); `.planning/phases/09-research-captures-hygiene/ARCH-05.md`.
- v1.1 `.planning/phases/13-*` UAT pattern + the Phase-10 `10-UAT.md` scaffold (the UAT runbook style); CLAUDE.md Linux device-access section (uaccess / setfacl / systemd ≥258).
- CLAUDE.md — RE is the source of truth, but the hardware wins on provisional values; D-02 honesty contract.
  \</canonical_refs>

<specifics>
## Specific Ideas

- The Phase-25 deliverable is a **`25-UAT.md`** runbook the operator executes against the connected
  AKP05E: per-criterion pass/fail for image-on-key, key press, the 4 encoders (CW/CCW/press),
  touch tap-zone, swipe-page, brightness slider, clear-all (VERIFY-05); plus the real-`.sdPlugin`
  round-trip (register → `setImage` paints a key → physical press delivers `keyDown` → observable
  effect) (VERIFY-06). Each provisional §5 item gets a confirm-or-correct row that, on mismatch,
  updates the RE doc + code (hardware wins). `hasClock=false` row. The automated build/tests from
  Phases 14-24 must be green first (the slice exists); Phase 25 adds the human + real-plugin witness.

</specifics>

<deferred>
## Deferred Ideas

- AKP153/AKP815 live verification (if not connected) → carry to a later capture/verify pass.
- Anything the milestone deferred (vmix/youtube/etc. built-ins; AK980 TFT; mouse) stays out.

</deferred>

______________________________________________________________________

*Phase: 25-hardware-verification-real-plugin*
*Context gathered: 2026-05-23 (v1.3 replan; HARDWARE-GATED milestone close-out)*
