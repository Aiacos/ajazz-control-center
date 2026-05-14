---
artefact: HOTPLUG-RETRO
requirement: HOTPLUG-07
phase: 4
written: 2026-05-14
---

# Hot-Plug Debugging Retrospective — 2026-05-12 / 13

> Why this exists: HOTPLUG-07 requires that the institutional knowledge from
> the multi-day Stream Dock / DeviceList debugging session does not evaporate.
> This artefact narrates **what was broken, what changed, why 3 devices now
> work, and what we learned** with concrete commit references so a future
> reader can reconstruct the chain without re-reading 8 individual fix
> commits one by one.

## Symptom

The app started cleanly, the registry held descriptors for all 7+
supported device models, and the QML sidebar dutifully listed every
*registered* device. But three known-connected devices appeared as
**offline** in the UI even though they were physically plugged in and
visible to `lsusb` / `hidapi`.

The user-visible failure mode: every freshly-launched session needed a
manual unplug/replug cycle for the affected devices to flip from "offline"
to "connected" — even though the OS had had them attached the whole
time. On a particularly bad session, the sidebar's `EmptyState`
placeholder also flickered visible despite the model holding rows,
suggesting a deeper view/model coupling problem rather than just a
catalog gap.

This was the load-bearing UX bug for the AJAZZ Control Center v1.1
preview: a multi-device user couldn't trust the sidebar.

## Root Cause Chain

Multiple defects compounded into the single user-visible symptom. In
order of how they were peeled back:

1. **QML `connected` self-binding shadow** — commit `6b11f57`
   (`fix(qml): device list — self-binding shadow made all rows show offline`).
   The `DeviceRow` delegate had a `property bool connected: model.connected`,
   but the inner `Item` redeclared `connected` for its own UI state, which
   shadowed the model binding. Every row read the inner `false` default
   instead of the model role — so even genuinely-connected devices
   rendered with the offline badge. **No QML warning, no error**, just a
   silently wrong runtime value.

1. **Connected-only filter changed `DelegateModel` indices unexpectedly** —
   commits `c2fec55`, `6af22cf`, `42febbd` (the `align connected rows`
   sequence). The original `DeviceList` rendered every registered model
   row; commit `d377d80` ("show only currently-connected devices in the
   sidebar") added a `DelegateModel` filter so the sidebar would hide
   offline rows. But the filter changed which rows were visible without
   restructuring the layout, leaving collapsed-row gaps that misaligned
   the rest of the list. The fix sequence switched to a
   `Repeater` + `ColumnLayout` topology that uses `implicitHeight` so
   filtered-out rows truly disappear from layout, not just from paint.

1. **`EmptyState` visibility bound to a stale id** — commit `0eb886f`
   (`fix(device-list): EmptyState visibility — was bound to stale id`).
   After the topology change to `Repeater` + `ColumnLayout`, the
   `EmptyState` placeholder kept its old binding to a since-renamed id,
   so it intermittently appeared *over* populated lists or hid when it
   should have appeared.

1. **Catalog gaps masked the structural bugs** — commits `4818a6d`
   (`feat(devices): register AJAZZ 2.4G 8K mouse + Stream Dock 0x3004 variant`) and `62da68c` (`feat(streamdeck): add AKP815 backend + canonical PIDs + fix N4 catalog mapping`). Three of the affected
   devices weren't even reaching the model — their VID/PID pairs had no
   registered factory or descriptor at all. The user thought "the app is
   broken" while the actual semantic was "those devices are not in the
   catalog yet". This isn't strictly a hot-plug bug, but it sat
   underneath the other defects and made debugging significantly harder
   (the failure looked uniform; it was actually two distinct populations
   with different root causes).

1. **The deeper structural issue: refresh thrash** —
   `DeviceModel::refresh()` at the time used `beginResetModel` /
   `endResetModel` for every hot-plug event (`Pitfall 15` in
   `.planning/research/PITFALLS.md:370`). Even with all four bugs above
   fixed, a USB-hub shuffle still rebuilds the entire QML list, drops
   selection state, and loses scroll position. The 2026-05-12/13 fix
   sequence did **not** address this — Phase 4 owns it (Plan 04-04
   delivers the fine-grained `dataChanged` propagation; Plan 04-03
   delivers the 300ms debouncer that suppresses the burst-of-events
   shape that made the thrash visible in the first place).

## Fix Applied (2026-05-12 / 13)

Across the 8 commits in the table below, the symptom was reduced from
"3 devices invisible" to "all registered devices appear correctly, with
the right connected/offline state, on the first sidebar render":

| Commit    | Layer   | What it fixed                                                             |
| --------- | ------- | ------------------------------------------------------------------------- |
| `6b11f57` | QML     | Self-binding shadow on `DeviceRow.connected` property                     |
| `d377d80` | QML     | Add connected-only filter (intent: hide offline) — later partially undone |
| `c2fec55` | QML     | `implicitHeight` so collapsed rows don't misalign neighbours              |
| `6af22cf` | QML     | Align connected rows with the new DelegateModel filter                    |
| `42febbd` | QML     | Switch `DeviceList` to `Repeater` + `ColumnLayout` topology               |
| `0eb886f` | QML     | Rebind `EmptyState.visible` to the new id after topology change           |
| `4818a6d` | Catalog | Register AJAZZ 2.4G 8K mouse + Stream Dock `0x3004` variant               |
| `62da68c` | Catalog | Add AKP815 backend + canonical PIDs + fix N4 catalog mapping              |

Note the structural Pitfall 15 fix (fine-grained `dataChanged`) was
**deferred**: at the time the priority was to stop the bleed; the
refresh-thrash mitigation only became urgent once the surface bugs were
gone. **Phase 4 picks it up** — see Plan 04-04.

## Why 3 Devices Now Work

End state for a user 2026-05-14+ (post Phase 4 completion):

- Every registered backend produces a row in the sidebar.
- `model.connected` (the `ConnectedRole` exposed via `DeviceModel::roleNames`)
  flips correctly between `true` and `false` as `hidapi`'s
  `enumerateConnectedHidKeys()` reports devices appearing / disappearing.
- The 3 previously-invisible devices (the AKP815, the AJAZZ 2.4G 8K mouse,
  the Stream Dock 0x3004 variant) now have catalog entries and proper
  factory registrations, so they participate in `enumerate()` and `open()`
  flows like any other supported model.
- A user's sidebar selection survives a disconnect/reconnect cycle for the
  same codename (HOTPLUG-03 — Phase 4 makes this **structurally guaranteed**
  because Plan 04-04's fine-grained `dataChanged` doesn't move row indices,
  so QML's `currentIndex` remains bound to the same row).
- A USB-hub yank no longer floods the UI with toasts (HOTPLUG-05 — Plan 04-03's
  300ms trailing-edge debouncer collapses 4-8 raw events for the same
  `(vid, pid, serial)` to one downstream event).
- An in-flight call into a device that just got disconnected no longer
  crashes the app (HOTPLUG-01 — Plan 04-01's `shared_ptr<IDevice>`
  ownership migration plus the zombie contract on `IDevice`).

In short: the 2026-05-12/13 fix took the system from "works most of the
time once the user knows the unplug/replug workaround" to "works every
time on first launch". Phase 4 takes it from there to **structurally
guaranteed** (no UAF, coalesced events, fine-grained UI updates,
selection-stable).

## Lessons Learned

- **QML binding self-shadowing is silent**. There is no warning when an
  inner item redeclares an outer property of the same name; the inner
  value just wins. The defect is invisible to `qmllint` and to compile-
  time checks. The only defence is a UI smoke test that exercises the
  affected delegate with both `model.connected = true` and `false` and
  asserts the rendered offline-badge state matches. Phase 4 does not
  add this gate; if recurrences happen, Phase 4.1 should.

- **`DelegateModel` filters change layout indices, not just visibility**.
  When you add a filter to a `Repeater` / `DelegateModel`, surrounding
  layouts must explicitly use `implicitHeight` (or skip the row entirely)
  rather than render-with-zero-opacity. We re-learned this via commits
  `c2fec55` -> `42febbd`.

- **"Show only connected" felt like a UX win at the time** (commit
  `d377d80`) but **conflicts with the offline-badge UX target** that
  HOTPLUG-02 calls for. Phase 4's Plan 04-04 reverses the filter — rows
  for registered devices are always visible; their connected-state is
  surfaced via the badge. We will not re-add the filter unless a
  user-research signal demands it.

- **Catalog drift between `register.cpp` and the live HID enumeration is
  invisible until users plug in an unregistered device**. The catalog
  gap masked the structural bugs by making them look uniform across
  devices. Phase 8 (catalog corrections) owns the long-tail of rebadge
  cleanup so this category of "is this a bug or just an unregistered
  device?" question stops appearing in user reports.

- **Stop-the-bleed before structural fixes**. The 2026-05-12/13 sequence
  did *not* tackle Pitfall 15 even though it was visible in the same
  symptom space. That was the right call: the QML and catalog fixes
  were lower-risk, smaller-blast-radius, and unblocked user testing.
  The structural fix (Plan 04-04) needed a coalescing layer (Plan 04-03)
  *and* an ownership migration (Plan 04-01) underneath it to be safe.
  Trying to ship all four at once on 2026-05-12 would have stalled.

- **Pitfall 11 (`hid_open` invariant) was never violated during this
  episode** — `hid_transport.cpp` remained the sole call site. But the
  closeness of "we're touching the device-lifecycle code" made it a
  near-miss; this phase adds a CI grep guard so the next round of
  hot-plug fixes can't accidentally bypass the invariant under time
  pressure. See `.github/workflows/ci.yml` (`Enforce hid_open invariant`
  step, also added by this plan).

## Forward Pointers

- **Plan 04-04** (this phase) delivers the structural Pitfall 15 fix
  (fine-grained `dataChanged`, lex sort, offline badge) — closes
  HOTPLUG-02, HOTPLUG-03, HOTPLUG-04 and ratifies the UX target the
  2026-05-12/13 patches were aiming at.
- **Plan 04-03** (this phase) delivers the 300ms trailing-edge debouncer
  in `Application::onHotplug` so the burst-of-events shape that exposed
  Pitfall 15 is gone before any consumer sees it.
- **Plan 04-01** (this phase) delivers the `shared_ptr<IDevice>`
  ownership migration so disconnect-while-using is structurally safe
  (Pitfall 1 closed, HOTPLUG-01 satisfied).
- **Phase 8** (Catalog corrections) owns the residual long-tail of
  rebadges, the AKP153 PID corrections, and any further AKP815-class
  catalog gaps — see `.planning/phases/04-hot-plug-hardening/04-CONTEXT.md`
  Deferred Ideas.
- **If the architectural insight ("registry is the source of truth for
  what backends exist; hidapi is the presence signal") generalises
  beyond this phase**, promote this artefact to
  `docs/architecture/HOTPLUG.md` via `gsd-extract-learnings`. Until
  then, it lives phase-local per HOTPLUG-07 placement.
