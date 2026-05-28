---
phase: 25-hardware-verification-real-plugin
plan: 02
status: partial-blocked
completed: 2026-05-28
autonomous: false
operator: uni.lorenzo.a@gmail.com
session: 2026-05-28 13:00-13:10 GMT+2 (autonomous-mode walkthrough)
deliverables:
  - .planning/phases/25-hardware-verification-real-plugin/25-UAT.md (operator results recorded)
  - src/app/qml/Inspector.qml (L1 fix — QML role-type)
  - src/app/src/stream_dock_control_service.cpp (L2 fix — C++ url normalisation)
commits:
  - 24651a3 fix(app): icon-source URL handling across QML/C++ boundary
gaps_opened:
  - GAP-25A: setActiveDevice wiring gap (L3 structural) → Phase 26
  - GAP-25B: missing device-shaped editor affordances → Phase 26
---

# Plan 25-02 — Summary (PARTIAL, blocked)

## What was delivered

### Operator UAT session (partial)

A 10-minute autonomous-mode operator session walked 25-UAT.md against
the physically connected AKP05E (`0300:3004`, fw `V3.AKP05E.01.007`)
with the user. Per-criterion results:

| Category      | Count | Tests                                  |
| ------------- | ----- | -------------------------------------- |
| PASS          | 3     | 7 (brightness), 8 (clear), 9 (honesty) |
| FAIL          | 1     | 1 (3-layer image-upload regression)    |
| NO_AFFORDANCE | 1     | 6 (no touch-strip drop target)         |
| BLOCKED       | 6     | 2, 3, 4, 5, 15, 16 (demo unit gap)     |
| NOT_WALKED    | 5     | 10, 11, 12 (driven by 4/5/6); 13, 14   |

Full per-criterion table at `25-UAT.md` Summary section.

### Two bug fixes (in tree, committed `24651a3`)

The walkthrough surfaced a real 3-layer regression in the image-upload
pipeline:

- **L1** — `Inspector.qml:102` emitted a `url` from `FileDialog`
  `selectedFile` to a ListModel role first seeded as String in
  `KeyDesigner.qml._ensureBindings()`. Qt rejected the assignment with
  "Can't assign to existing role 'iconSource' of different type
  [Url -> String]" and silent no-op. **Fixed** by `.toString()` at the
  emission site.

- **L2** — `stream_dock_control_service.cpp:292,389` passed the stored
  `imagePath` to `QImage(QString)` verbatim. `QImage(QString)` wants a
  filesystem path, not a `file://` URL. The `file://` form is now
  required at the QML side (so KeyCell `url` properties accept it as
  absolute, not qrc-relative). **Fixed** by file-scope helper
  `normaliseImagePath()` that strips the scheme via
  `QUrl(s).toLocalFile()` when present.

- **L3** — `StreamDockControlService::setActiveDevice()` is declared
  but never called from any QML file. `m_activeDevice` stays null,
  `repaintPage()` early-returns, and even after L1+L2 fixes the
  device-write never fires. **NOT FIXED** — structural gap routed to
  Phase 26.

### `hasClock=false` honesty contract held (Test 9 PASS)

The AKP05E sidebar row correctly hides the Sync button — DEVICES-11 /
ARCH-05 honoured by the UI (`SettingsRow.qml:196` gates on
`root.hasClock`). The Phase 25 Plan 01 acceptance criterion
("the Sync button no longer appears on the AKP05E row") is verified.

### Reachable wire-level controls confirmed (Tests 7, 8 PASS)

The fallback path `setBrightness(codename) / clearAll(codename)` works
because both methods use `m_lookup(codename)` directly rather than
relying on `m_activeDevice`. The CLE opcode (clear-all) and LIG
percentage byte (brightness slider) fire successfully.

## Demo-unit caveat held (no FAIL recorded for 2-5, 15-16)

Per CLAUDE.md AKP05E glossary §7.1 and Plan 25-01's documented
expectation, input-streaming on `0x0300:0x3004` is unreachable across
hidraw raw read, GET_REPORT polling, evdev, raw usbmon, AND the
reference library `4ndv/mirajazz` (its own `async_hid` backend with
exact DIS+LIG+CONNECT keep-alive). Five tools captured zero input on
press. Tests 2, 3, 4, 5, 15, 16 recorded `BLOCKED` not `FAIL` per the
methodology rule. Resume needs a retail AKP05E / Mirabox N4 OR the
Frida-on-Windows vendor-app capture path.

## Two new gaps documented

- **GAP-25A** — Image-upload pipeline L3 wiring gap:
  `setActiveDevice` is declared but never called from QML.
  `m_activeDevice` stays null. Phase 26 sidebar selection must wire
  `setActiveDevice` on selection-changed.

- **GAP-25B** — Missing device-shaped editor affordances: KeyDesigner
  is a generic NxN tile grid; needs Elgato/OpenDeck-pattern device
  geometry per SKU (key grid + encoder dials + touch strip).

## Phase 25 status after this plan

**Plan 25-02: partial-blocked.** The runbook was walked as far as the
existing UI allowed; the structural blocker (GAP-25A + GAP-25B) closes
only when Phase 26 lands a device-shaped editor that wires
`setActiveDevice`. Six demo-unit-blocked tests stay BLOCKED until a
unit that streams input is available.

**Phase 25 overall: partial.** Cannot be marked `[x]` in ROADMAP.
Status flipped to `[partial — UAT blocked on Phase 26 + demo unit gap]`. The hardware-verification + real-plugin deliverables resume
after Phase 26 lands.

## Suite-green precondition held

Build precondition: `ctest --preset linux-release -E qml` =
**645/645 passed**, 108s, verified 2026-05-28 12:30. The L1+L2 fix
landed without breaking the 26 StreamDockControlService tests
(re-run after edit: 26/26 passed, 4.58s).

## Deviations

- Plan 25-02 frontmatter `must_haves.truths` line 14 (
  "A human operator has walked 25-UAT.md against the physically
  connected AKP05E and recorded pass/fail for every VERIFY-05
  criterion") is **partially met**: 9 of 9 VERIFY-05 criteria
  recorded, but 1 FAIL + 1 NO_AFFORDANCE + 4 BLOCKED — not all PASS.
  This is the honest result; recording fabricated PASS would violate
  the D-02 honesty contract from `25-CONTEXT.md`.
- Plan 25-02 `truths` line 15 (VERIFY-06 real third-party `.sdPlugin`
  round-trip) is **NOT_WALKED** — gated on a sdPlugin sample on disk
  - the Phase 26 setActiveDevice wiring closing GAP-25A.
- Plan 25-02 `truths` line 16 (provisional §5 reconciliation) is
  **deferred** — Tests 10-12 BLOCKED behind Tests 4-6.

These deviations are documented honestly in `25-UAT.md` Summary + Gaps
sections and the GAP-25A/B descriptors above. Phase 26 closes them.
