---
phase: 13-catalogue-v1-1-ui-verifies-back-fill
plan: 01
subsystem: catalogue
tags: [devices.yaml, autogen, hid, sonix, microdia, dongle, arch-06, devices-08, devices-09]

# Dependency graph
requires:
  - phase: 09-research-captures-hygiene
    provides: ARCH-06 default verdict + topology evidence (live lsusb 2026-05-15) for 0c45:7016
provides:
  - Reconciled 0c45:7016 catalogue row aligned to the ARCH-06 separate-dongle default verdict
  - NEW docs/protocols/keyboard/microdia_dongle.md identification stub
  - Regenerated README + wiki AUTOGEN blocks with corrected protocol_doc link
affects:
  - 09-research-captures-hygiene (Phase 9.x finalization gate — unplug test — promotes ARCH-06 to Locked, which lands here too)
  - 12 (AK980 PRO capabilities — confirms separate-row stays separate)
  - 13-02 (VERIFY-01..04 operator checklist consumes the corrected sidebar / tooltip output)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Catalogue rows for transports carry capabilities:[] + protocol_doc + topology notes; capability surface lives on the paired peripheral codename
    - ARCH-06 honesty-surface pattern — DEFAULT VERDICT label appears verbatim in both the catalogue row notes and the protocol-doc status banner (mirrors ARCH-04/05 D-05 contract)
    - Codename stability over churn — existing ASCII codename retained, canonical ADR alias recorded inside the notes + used as the stub filename

key-files:
  created:
    - docs/protocols/keyboard/microdia_dongle.md
  modified:
    - docs/_data/devices.yaml
    - README.md
    - docs/wiki/Supported-Devices.md

key-decisions:
  - Reconciled the existing 0c45:7016 row in place instead of adding a duplicate (DEVICES-08 explicitly asks for ONE row at probed tier; the row added 2026-05-15 had internal contradictions but the slot was correct).
  - Kept the existing codename ak980pro_dongle_24g (already ASCII-only per Pitfall 32, already referenced in prose; renaming to ARCH-06's canonical microdia_dongle_7016 would churn AUTOGEN sort order with no functional gain). The canonical alias is recorded inside the notes field AND used as the stub doc filename, so any future reader keying off the ADR finds the doc.
  - Removed the composite-HID framing wholesale (the 'same physical keyboard enumerates twice' sentence and the unsupported 'High-Speed 480Mbps' figure) — these directly contradicted ARCH-06's separate-device verdict and ARCH-06's own evidence table (ak980pro link speed is documented as port-dependent, NOT High-Speed).
  - Did NOT promote ARCH-06 to 'Locked' anywhere — D-05 honesty contract. Both the row notes and the stub status banner cite the DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION) status verbatim and name the 2-minute physical unplug test as the finalization gate.
  - HID descriptor bytes left as a PENDING placeholder in the stub — capturing them is the Pitfall 17-safe path (descriptor parsing only) and belongs to the Phase 9.x evtest / hidraw session, not this autonomous plan.

patterns-established:
  - 'Catalogue dongle row idiom: family=dongle, capabilities=[], protocol_doc points at a topology+identification stub (NOT the AK820/AK980 proprietary.md when the dongle is a separate device). Future SONiX OEM receiver SKUs can be added as sibling rows pointing at the same identification methodology.'

requirements-completed:
  - DEVICES-08
  - DEVICES-09

# Metrics
duration: 6min
completed: 2026-05-28
---

# Phase 13 Plan 01: Catalogue Back-Fill (DEVICES-08 + DEVICES-09) Summary

**Reconciled the 0c45:7016 catalogue row to the ARCH-06 separate-dongle default verdict, removed the contradictory composite-HID framing inherited from the 2026-05-15 row, and shipped a new microdia_dongle.md identification stub carrying the four ARCH-06 topology facts + descriptor-capture methodology + DEFAULT-VERDICT honesty banner.**

## Performance

- **Duration:** 6 min
- **Started:** 2026-05-28T08:17:22Z
- **Completed:** 2026-05-28T08:23:24Z
- **Tasks:** 2 (both `type=auto`, both committed atomically)
- **Files modified:** 3
- **Files created:** 1

## Accomplishments

- DEVICES-08: exactly one 0c45:7016 catalogue row, at `probed` / `family: dongle` / `capabilities: []`, with notes asserting the ARCH-06 separate-dongle facts (different USB bus branch `usb1/1-13/1-13.1/1-13.1.2` vs ak980pro's `usb1/1-10`; Full-Speed 12 Mbps; two boot-keyboard HID interfaces with 8-byte EPs; `iManufacturer="SONiX"` / `iProduct="USB DEVICE"` / `bcdDevice 1.03`) and citing the DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION) status verbatim + the 2-minute unplug-test gate.
- DEVICES-09: new `docs/protocols/keyboard/microdia_dongle.md` stub with provenance line, ARCH-06 status banner, identification table, the four topology facts (table + Pitfall-20-refutation prose), HID-descriptor placeholder section pointing at the Pitfall 17-safe `sudo cat /sys/class/hidraw/hidrawN/device/report_descriptor` recipe, identification methodology (recipe for the next SONiX receiver SKU), and pointer to ARCH-06.
- Removed the pre-existing internal contradictions in the row: the "same physical keyboard enumerates twice" sentence and the unsupported "High-Speed 480Mbps" figure are GONE; the row no longer asserts a composite-HID framing while citing ARCH-06's negation of it.
- Redirected `protocol_doc:` from the wrong `docs/protocols/keyboard/proprietary.md` (the AK820/AK980 RE doc) to the new `microdia_dongle.md` stub. Both the README "by-family" table and the wiki Supported-Devices table now link to the correct stub.
- AUTOGEN blocks in `README.md` + `docs/wiki/Supported-Devices.md` regenerated cleanly via `python3 scripts/generate-docs.py`; no hand-edits to the generated blocks (CLAUDE.md hard rule preserved).

## Task Commits

Each task was committed atomically on `worktree-agent-accf5a828a4c7e92f`:

1. **Task 1: Reconcile the 0c45:7016 catalogue row (DEVICES-08)** - `c4591e2` (docs)

   - Files: `docs/_data/devices.yaml`, `README.md`, `docs/wiki/Supported-Devices.md`
   - Rewrote `notes:` to drop the composite framing and the 480Mbps claim, added the DEFAULT VERDICT conditional + unplug-test gate, redirected `protocol_doc:` to `microdia_dongle.md`, refreshed the `name:` to the Microdia/SONiX framing, tightened the `feature_summary.pending:` wording, then regenerated README + wiki AUTOGEN blocks via `scripts/generate-docs.py`.

1. **Task 2: Create microdia_dongle.md stub (DEVICES-09)** - `fc16879` (docs)

   - Files: `docs/protocols/keyboard/microdia_dongle.md` (NEW)
   - mdformat reformatted on the first commit attempt (table padding + `1.` numbering normalisation); re-staged the cleaned file and the second commit attempt passed all hooks.

## Files Created/Modified

- `docs/_data/devices.yaml` - reconciled the `0c45:7016` row in place; codename retained, name + notes + protocol_doc + one `feature_summary.pending` bullet updated.
- `docs/protocols/keyboard/microdia_dongle.md` (NEW) - 165-line ASCII-only identification stub: provenance, ARCH-06 status banner, identification table, four-topology-facts table + Pitfall-20-refutation prose, HID descriptor placeholder section, identification methodology recipe, ARCH-06 pointer + cross-references.
- `README.md` - AUTOGEN regenerated; the `0c45:0x7016` row in the by-family table now links to `microdia_dongle.md` and carries the reconciled notes.
- `docs/wiki/Supported-Devices.md` - AUTOGEN regenerated; same row in the by-family + by-tier tables.

## Decisions Made

- **Reconcile-in-place vs add-second-row** — went with reconcile-in-place because DEVICES-08 explicitly asks for exactly ONE 0c45:7016 row, and adding a duplicate would have shipped a second contradiction (`grep -c 'pid: "0x7016"'` must be 1; verified post-commit). The pre-existing row added 2026-05-15 had the right slot/tier/family — only the prose was wrong.
- **Codename: keep `ak980pro_dongle_24g`** — already ASCII, already referenced; rename churn would re-sort the AUTOGEN tables for zero functional gain. The ARCH-06 canonical alias `microdia_dongle_7016` is recorded inside the notes AND used as the stub filename, so a reader landing from the ADR finds the doc by name.
- **DEFAULT VERDICT label discipline (D-05)** — visible in three load-bearing places: the row `notes`, the stub status banner, the stub identification table footer pointer. Mirrors the ARCH-04 / ARCH-05 honesty-surface pattern.
- **HID descriptor placeholder, not bytes** — capturing the descriptor (the Pitfall 17-safe path) belongs to the Phase 9.x evtest / hidraw session, not this autonomous plan. The stub gives the recipe; the bytes are explicitly marked PENDING.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] mdformat reformatted the new stub on first commit attempt; re-staged and re-committed.**

- **Found during:** Task 2 (commit)
- **Issue:** First `git commit` for Task 2 failed: the mdformat pre-commit hook reformatted `docs/protocols/keyboard/microdia_dongle.md` (table column padding normalised + ordered-list items normalised to `1.` repeated). The hook exits non-zero whenever it modifies files, which aborts the commit as designed. Standard pre-commit dance.
- **Fix:** `git add docs/protocols/keyboard/microdia_dongle.md` to stage the mdformat-cleaned file, then re-ran the commit. Second attempt passed mdformat clean and all other hooks. NOT a `--no-verify` bypass; the hook content was correct, I just needed to re-stage after the auto-reformat.
- **Files modified:** `docs/protocols/keyboard/microdia_dongle.md` (only formatting; content tokens preserved; ASCII purity preserved — re-verified post-mdformat).
- **Verification:** `git diff --stat HEAD~1 HEAD` showed exactly the file we expected; row-scope ASCII check showed 0 non-ASCII bytes; required tokens (0c45, ARCH-06, DEFAULT VERDICT, SONiX, USB DEVICE, Full-Speed, 1-13.1.2, Pitfall) all present.
- **Committed in:** `fc16879`

### Plan-spec observation (not a deviation in behaviour, recorded for the planner)

The PLAN.md `<verify>` block for Task 1 contains `! grep -q '480' docs/_data/devices.yaml` as a gate. This gate is **structurally too coarse** to ever pass: `devices.yaml` contains the substring `480` in two unrelated rows describing the AKP153 / AKP05 LCD strips (`800x480` panel resolution). The gate's intent — verbatim from the threat register T-13a-02 and the Task 1 §3 bullet — is "the 'Full-Speed 12Mbps vs the AK980 PRO's High-Speed 480Mbps' clause is removed from the dongle row". I verified that intent at row scope (Python YAML load + substring check on `notes` + `feature_summary` for the `0x7016` row); both `enumerates twice` and `480` are absent from the row. Future plans should scope file-wide grep gates to the affected row (e.g. `yq -r '.devices[] | select(.pid == "0x7016") | .notes'`) when the gate is supposed to exercise a local contradiction.

______________________________________________________________________

**Total deviations:** 1 auto-fixed (1 blocking — pre-commit auto-reformat) + 1 planner-spec observation (no behaviour change).
**Impact on plan:** None on outcome. All success criteria met. The verify-gate coarseness was a planner artefact; the row-scoped check matches the threat-register intent.

## Issues Encountered

- None during planned work. The pre-commit mdformat dance is routine and expected on any new Markdown file.

## User Setup Required

None - this is a doc-only plan; no external service configuration, no build, no hardware required.

## Threat-Surface Scan

No new attack surface introduced. Scope was documentation only (catalogue YAML + a new Markdown stub + AUTOGEN regeneration). No code paths, no network endpoints, no schema changes at trust boundaries. The catalogue row is consumed only by `scripts/generate-docs.py` (existing) and the v1.1 sidebar's MaturityRole tooltip (existing); no new consumer added.

## Next Phase Readiness

**For Phase 13 Plan 02 (VERIFY-01..04 operator checklist):** The corrected sidebar / tooltip output is ready to be visually verified once the operator brings up the running app. VERIFY-04 in particular (MaturityRole tooltip matches the `devices.yaml` notes for all 5 tier values) will now read the reconciled notes for the dongle row when it is opened — previously it would have surfaced the contradictory composite framing.

**For Phase 9.x finalization:** ARCH-06 remains **DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)**. The 2-minute physical unplug test (unplug the `ak980pro` 2.4G receiver; confirm `0c45:7016` does NOT disappear simultaneously) is still gating the promotion to "Locked". When that test lands, three artefacts need synchronised updates:

1. `.planning/phases/09-research-captures-hygiene/ARCH-06.md` — status banner + frontmatter + title.
1. `docs/_data/devices.yaml` 0c45:7016 row notes — drop the conditional clause.
1. `docs/protocols/keyboard/microdia_dongle.md` status banner — drop the conditional banner; promote to a regular protocol stub.

This synchronisation is intentionally kept manual (single Phase 9.x follow-up commit) per the ARCH-04/05/06 honesty-surface pattern.

**For DEVICES-09 hardware completion:** When the operator runs the Phase 9.x evtest / hidraw session, the descriptor placeholder in `microdia_dongle.md` is the slot to fill with the decoded report descriptor (and, if/when identified, the paired downstream wireless input device).

## Self-Check: PASSED

Files exist (verified):

- `docs/_data/devices.yaml` (modified, row reconciled)
- `docs/protocols/keyboard/microdia_dongle.md` (created, 165 lines, ASCII-only)
- `README.md` (modified, AUTOGEN regenerated)
- `docs/wiki/Supported-Devices.md` (modified, AUTOGEN regenerated)

Commits exist (verified via `git log --oneline -3`):

- `c4591e2` — Task 1 (DEVICES-08 reconciliation)
- `fc16879` — Task 2 (DEVICES-09 stub)

Plan success criteria — row-scoped:

- DEVICES-08: exactly one 0c45:7016 row, `probed` / `dongle` / `[]` / ARCH-06 notes / DEFAULT VERDICT cited / `microdia_dongle.md` protocol_doc — YES.
- DEVICES-09: stub exists with provenance, ARCH-06 status banner, identification table, four topology facts, HID descriptor placeholder, identification methodology, ARCH-06 pointer — YES.
- ARCH-06 NOT promoted to "Locked" anywhere; DEFAULT VERDICT cited verbatim in both the row notes and the stub status banner — YES.
- README + wiki AUTOGEN regenerated from yaml; no hand-edits — YES.
- ASCII-only on stub doc (Pitfall 32) — YES.

______________________________________________________________________

*Phase: 13-catalogue-v1-1-ui-verifies-back-fill*
*Plan: 01*
*Completed: 2026-05-28*
