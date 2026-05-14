---
phase: 8
phase_slug: scaffolded-device-wiring
gathered: 2026-05-14
status: Ready for planning
mode: autonomous-interactive
---

# Phase 8: Scaffolded-Device Wiring — Context

**Gathered:** 2026-05-14 via `/gsd-autonomous --interactive`
**Source for recommendations:** Stream Dock catalog work in `62da68c` (AKP815 backend, register.cpp catalog reconciliation, devices.yaml maturity scaffolding, README/wiki regeneration) + REQUIREMENTS DEVICES-01..04 + ROADMAP Phase 8 success criteria.

<domain>
## Phase Boundary

Users can see honest maturity tiers per device in the sidebar and README, and 1-2 stream-dock-family siblings move from Tier 0 (Scaffolded) to Tier 2 (Partial) with documented protocol artefacts. Most pre-work landed in `62da68c`:

- AKP815 backend (`src/devices/streamdeck/src/akp815.cpp` + `akp815_protocol.hpp`) wired and registered.
- `register.cpp` catalog expanded to 17 streamdeck rows (canonical PIDs from `[ajazz-sdk]`, Mirabox rebadges, AKP815, AKP153R/E_v2, Mirabox N4).
- `devices.yaml` includes maturity scaffolding (currently using `status: functional|scaffolded`).
- `streamdock_catalog_fetcher.cpp:117` N4→akp815 bug fixed (now N4→akp05).
- README + wiki + Supported-Devices regenerated via AUTOGEN hooks.
- `docs/protocols/streamdeck/akp815.md` + `_research-sources.md` written.

Phase 8 closes the remaining gaps: the 5-tier `maturity` vocabulary (currently 2 tiers), the `MaturityRole` exposure in DeviceModel + QML sidebar, README per-family "what works/what doesn't" tooltips, and one additional Tier 0→Tier 2 promotion (AKP815 already counts as #1).

Maps to requirements: DEVICES-01 .. DEVICES-04 (full text in `.planning/REQUIREMENTS.md`).

</domain>

<decisions>
## Implementation Decisions (locked)

### D-01 — Rename `status` → `maturity`, expand to 5-tier vocabulary

`docs/_data/devices.yaml` field rename + value expansion:

- **Field rename:** every `status:` field → `maturity:`.
- **Value vocabulary (5 tiers, lowercase per existing convention):**
  - `scaffolded` — descriptor + factory exist, no real device-specific code path; backend may compile but does not exercise the device.
  - `probed` — device enumerates correctly, capabilities respond, basic descriptor populated; no protocol writes confirmed.
  - `partial` — some features work end-to-end (e.g., key reads, basic display), but advertised capability set is incomplete or untested.
  - `functional` — all advertised capabilities work in practice; tested manually by maintainer or in CI.
  - `verified` — `functional` + automated CI exercise on real hardware OR sustained user-confirmed reliability across multiple maintainer reports.
- **Migration of existing values (1:1):** every `status: functional` → `maturity: functional`; every `status: scaffolded` → `maturity: scaffolded`. No semantic change for existing devices; just rename.
- **AUTOGEN regenerator update:** `regenerate-docs` pre-commit hook reads from `maturity` instead of `status`. Update the regenerator script (likely `tools/regenerate-docs.py` or similar — planner finds the exact path) to read `maturity` and (optionally) emit a tier badge in the per-device table cells.

**Rationale:** Single source of truth. ROADMAP DEVICES-01 specifies the 5-tier vocabulary verbatim — keeping `status` (a misleading name once values include capability tiers like "probed") would force constant translation between the field name and what reviewers see in the README. Rename is a one-time cost; clarity is permanent.

### D-02 — DEVICES-04 promotion: AKP815 counts as #1; planner picks #2

The AKP815 work landed in `62da68c` already satisfies the "Tier 0 (Scaffolded) → Tier 2 (Partial) with documented protocol artefact" template DEVICES-04 specifies:

- Backend: `src/devices/streamdeck/src/akp815.cpp` (265 lines) + `akp815_protocol.hpp` (47 lines).
- Protocol artefact: `docs/protocols/streamdeck/akp815.md` (162 lines, with byte-0 Report ID convention per Pitfall 9).
- Register entry: `register.cpp` adds the canonical Mirabox V1 PID with `akp815_descriptor` factory.
- devices.yaml entry: present with maturity tier.
- Tests: backend reuses AKP153 test surface; no AKP815-specific captures yet.

**Counts as promotion #1.** Maturity tier in devices.yaml: `scaffolded` (no live captures yet) — actually that's still Tier 0. Hmm, semantic question: does "implementation exists" = Tier 2 (`partial`), or do we need real-device confirmation = Tier 2? Per the D-01 vocabulary above, AKP815 today is **`probed`** (descriptor exists, factory wired, backend compiles, but no real-device capture has confirmed protocol writes work). To reach `partial` ("some features work end-to-end"), Phase 8 execute needs to either (a) confirm via a real-device capture that AKP815's per-key image upload works, or (b) lower the bar and accept that "implementation present, code path exercised by unit tests" qualifies. **Planner picks the bar at execute time** based on whether a real AKP815 unit is available for testing.

**#2 promotion:** picked at planning time. Candidates per ROADMAP (siblings of AKP153/AKP03/AKP05, AKB980 PRO explicitly excluded — vendor driver requires wine):

- **AKP153R** (`0x0300:0x1020`) — registered but no capture; currently `scaffolded`. Planner can promote to `probed` if at least the registry + factory wiring is verified to compile-and-link. Promote to `partial` if a contributor with the device sends a brief capture.
- **Mirabox N3** (one of `0x6602:0x1002`, `0x6603:0x1002`, `0x6603:0x1003`) — Mirabox-branded sibling. Same backend as AKP03; protocol identical. Likely `probed` since enumeration is identical; the documented protocol artefact would be a one-page note in `docs/protocols/streamdeck/akp03.md` cross-reference.
- **AKP153E (V2 firmware variant, `0x0300:0x1010`)** — different PID from canonical AKP153E (`0x0300:0x1002`); same wire format. Promotes to `probed` trivially since AKP153 backend already handles it.

**Default suggestion for #2:** **Mirabox N3 (one rev)** since it adds value beyond AJAZZ-branded SKUs (broadens the product the project supports), and the protocol artefact is a small annotated-cross-reference rather than a full new protocol doc.

### D-03 — `MaturityRole` exposure in DeviceModel + sidebar

`DeviceModel` gets a new role:

```cpp
MaturityRole,   ///< String value of devices.yaml `maturity` for this device.
```

- `data()` returns the maturity string for the row's codename, looked up at startup time from a static map (devices.yaml is build-time-baked into the binary, OR loaded once at app init from the embedded resource path — planner picks based on existing pattern).
- `roleNames()` exposes `"maturity"` to QML.
- QML sidebar displays the maturity tier as a **tooltip** on the device row (NOT a visible badge per the existing v1.0 styling vocabulary — adds nothing visible by default, but right-click / long-hover surfaces the tier). Optional small color-coded badge in the row's right-side stack if the existing `ConnectedRole` badge can accommodate it (e.g. green dot for functional+, amber for partial, gray for scaffolded). Planner picks based on existing badge precedent.

**Rationale:** Per ROADMAP, "consistent with the v1.0 styling vocabulary" — tooltips already exist in the codebase, badges are bounded by existing badge slots. Don't introduce a new visual element if a tooltip serves.

### D-04 — README per-family "what works/what doesn't" tooltips (DEVICES-03)

The README's existing per-device table (regenerated by AUTOGEN from devices.yaml) gets enriched: each row's `Notes` cell carries a "works: X, Y / partial: Z / pending: W" prose summary populated from a new devices.yaml field per-device. Field name suggestion: `feature_summary` with sub-keys `works:`, `partial:`, `pending:`.

**Implementation:**

- devices.yaml gets an optional `feature_summary` block per device with three sub-lists.
- AUTOGEN regenerator concatenates them into a Notes-cell prose: e.g., "✓ key reads, key images. ⚠ encoder rotation (untested). ✗ firmware update."
- Devices without `feature_summary` keep the existing free-text `notes:` field as fallback.

**Rationale:** Markdown tables can't host rich tooltip metadata reliably across renderers (GitHub, GitLab, in-IDE preview all differ). Inline prose summaries are the lowest-common-denominator that still conveys "what works / what doesn't" in a scannable way. Optional field means low migration cost — only the 1-2 promoted devices (per D-02) need it filled in this milestone; others get the prose later.

### Claude's Discretion

- **Maturity → badge color mapping** — if the planner adopts a colored badge, suggested mapping: `scaffolded` = gray, `probed` = blue, `partial` = amber, `functional` = green, `verified` = green-with-checkmark. Planner picks based on existing palette tokens.
- **devices.yaml schema validation** — if there's an existing JSON Schema or yamllint rule for devices.yaml, update it to enforce the 5-tier vocabulary as an enum.
- **Migration sequence for the rename** — single commit covering: devices.yaml field rename + regenerator script update + `MaturityRole` addition to DeviceModel + sidebar tooltip wiring. Bigger atomic commit, but the rename touches every device row so partial commits would leave the build broken.
- **`maturity` field placement in YAML** — adjacent to `status` placeholder line during transition, then `status` is deleted in the same commit. After the commit, only `maturity` exists.
- **Wiki regeneration** — `regenerate-docs` pre-commit hook handles README + wiki/Home + wiki/Supported-Devices automatically. Phase 8 doesn't need to touch wiki source manually.

</decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase 8 pre-work (already landed in 62da68c)

- `src/devices/streamdeck/src/akp815.cpp` + `akp815_protocol.hpp` — AKP815 backend.
- `src/devices/streamdeck/src/register.cpp` — 17 streamdeck rows including AKP815 + canonical PIDs + rebadges.
- `src/app/src/streamdock_catalog_fetcher.cpp` — N4→akp05 mapping fix.
- `tests/unit/test_streamdock_catalog_fetcher.cpp` — N4 mapping test.
- `docs/_data/devices.yaml` — current schema with `status` field (Phase 8 renames to `maturity`).
- `docs/protocols/streamdeck/akp815.md` — AKP815 protocol artefact for DEVICES-04 #1.
- `docs/protocols/streamdeck/_research-sources.md` — citation index.
- `docs/research/vendor-protocol-notes.md` Finding 16 — catalog reconciliation that drives the 17-row register.cpp.

### Requirements & roadmap

- `.planning/REQUIREMENTS.md` — DEVICES-01..04 verbatim. Out-of-Scope (AKB980 PRO promotion deferred — Delphi installer + wine).
- `.planning/ROADMAP.md` Phase 8 success criteria — four contractual SC1..SC4.

### Phase 4 dependencies (must land first)

- `.planning/phases/04-hot-plug-hardening/04-CONTEXT.md` D-04 — row identity is codename. DEVICES-02 `MaturityRole` follows the same per-codename keying.

### Phase 5 cross-reference

- `.planning/phases/05-time-sync-scaffolding/05-CONTEXT.md` D-03 — symmetric `IClockCapable` coverage across all streamdeck variants. The maturity tier is independent (a `scaffolded`-tier device can still advertise `Capability::Clock` and stub `setTime()` per Phase 5 D-03).

### Pitfalls research (relevant)

- `.planning/research/PITFALLS.md` Pitfall 9 — HID Report ID byte 0 confusion. Each promoted device's protocol .md must document its byte-0 convention (AKP815 doc already does this).
- `.planning/research/PITFALLS.md` Pitfall 10 — Endianness on packed protocol structs. `cppcoreguidelines-pro-type-reinterpret-cast` clang-tidy rule must pass on any new device backend code.

### Existing code (touched by this phase)

- `docs/_data/devices.yaml` — field rename + 5-tier vocabulary + optional `feature_summary` blocks for promoted devices.
- `src/app/src/device_model.hpp` + `src/app/src/device_model.cpp` — add `MaturityRole`, `roleNames` entry, `data()` branch.
- `src/app/qml/components/DeviceRow.qml` (or equivalent) — wire `maturity` role to a tooltip and (optional) colored badge.
- `tools/regenerate-docs.py` (or whatever the AUTOGEN regenerator script is) — read `maturity` field, emit prose for `feature_summary`.
- `README.md` + `docs/wiki/Home.md` + `docs/wiki/Supported-Devices.md` — auto-regenerated by the hook (no manual edits).

### Test infrastructure

- `tests/unit/test_device_model.cpp` (likely existing) — extend with `MaturityRole` assertions.
- AKP815 / promoted-device-#2 unit tests — depend on what kind of test surface the planner picks (compile-only smoke vs real-protocol-bytes unit).

\</canonical_refs>

\<code_context>

## Existing Code Insights

### Reusable Assets

- **`DeviceModel` role enum + `roleNames` pattern** — adding `MaturityRole` follows the same template as `ConnectedRole`, `HasRgbRole`, etc. Phase 5's `HasClockRole` will use the same pattern.
- **devices.yaml pre-commit regenerator** — already updates README + wiki/Home + wiki/Supported-Devices. Phase 8 just changes the source field name + adds optional feature_summary handling.
- **Protocol .md doc convention** — `docs/protocols/streamdeck/<codename>.md` template already established (akp153.md, akp03.md, akp05.md, akp815.md). Promoted-device-#2 follows the same template.
- **`_research-sources.md` citation index** — already exists. New entries cited per its convention.

### Established Patterns

- **devices.yaml field naming** — snake_case. `feature_summary` follows.
- **5-tier vocabulary lowercase per convention** — devices.yaml uses lowercase enum values (`functional`, `scaffolded`). New tiers (`probed`, `partial`, `verified`) follow.
- **Backwards-compat field deprecation** — usually handled via single atomic commit covering YAML + regenerator + tests; no transitional aliases (the codebase doesn't have a separate "deprecated field" pattern in YAML).

### Integration Points

- **devices.yaml → AUTOGEN → README/wiki** — single source of truth flow. Phase 8's rename ripples through automatically once the regenerator updates.
- **devices.yaml → DeviceModel** — currently DeviceModel reads from `core::DeviceRegistry` (in-memory descriptor list, not YAML directly). Phase 8 needs to bake the maturity field into the C++-side descriptor too, OR populate at app startup from an embedded YAML resource. Planner picks based on whether the existing pipeline already does either (most likely an embedded resource is the convention).
- **Phase 5 cross-cutting** — Phase 5 also adds a new `HasClockRole` to DeviceModel. Phase 8 and Phase 5 should land their DeviceModel changes in non-conflicting ways (separate commits, different role enum slots).

\</code_context>

<specifics>
## Specific Ideas / Anchor Artefacts

- **Per-decision artefact files under `.planning/phases/08-scaffolded-device-wiring/`:**
  - `08-PLAN.md` — gsd-planner output. Likely 3-4 atomic tasks (rename + regenerator + DeviceModel role + #2 promotion).
  - `08-PROMOTION-NOTES.md` — narrative of which #2 device was picked and why; protocol artefact for that device if it's a small cross-reference rather than a full new doc.
  - `08-SUMMARY.md` — gsd-executor output.
- **Atomic commit boundaries:**
  - Commit 1: devices.yaml `status` → `maturity` rename + regenerator update + AUTOGEN re-run (touches README/wiki/Supported-Devices).
  - Commit 2: DeviceModel `MaturityRole` + sidebar tooltip wiring.
  - Commit 3: Promoted-device-#2 backend wiring + protocol .md (size depends on candidate; could be a small cross-reference or a fresh full doc).
  - Commit 4: `feature_summary` optional field rollout for the 2 promoted devices (AKP815 + #2).

</specifics>

<deferred>
## Deferred Ideas

- **AKB980 PRO promotion** — vendor driver is Delphi installer requiring `wine`/`innoextract`; explicitly out of scope per REQUIREMENTS.
- **Mass promotion of remaining 5-6 scaffolded devices** — REQUIREMENTS Out-of-Scope says "too high variance for v1.1; defer 5-6 promotions to v1.1.x or v1.2".
- **`feature_summary` field rollout for ALL devices** — Phase 8 only fills it in for the 2 promoted devices. Remaining devices get it lazily as their tier advances. Track as a v1.1.x quality-of-life follow-up.
- **Real-hardware CI for promoted devices** — `verified` tier requires automated CI on real hardware; out of reach for v1.1 without dedicated hardware runners. Plausible candidates for v1.2 contributors with multiple devices.
- **`AjazzControlCenter` repo as the source of truth for the YAML** — a future v1.2 could publish `devices.yaml` as a separate vendored package consumable by other catalog projects (e.g. an `opendeck` plugin would read our list). Out of scope for v1.1.
- **Per-family product-page links in README** — currently the README points to `docs/protocols/streamdeck/*.md`; could also link to vendor product pages (Mirabox, AJAZZ Brand). Bandwidth-cheap once `_research-sources.md` lists them, but pollution-prone (vendor URL drift). Defer.

</deferred>

______________________________________________________________________

*Phase: 08-scaffolded-device-wiring*
*Context gathered: 2026-05-14*
