# Phase 4: Hot-plug Hardening - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-14
**Phase:** 04-hot-plug-hardening
**Areas discussed:** Toast policy, DeviceModel update strategy, Debounce window value, DeviceRegistry::open() caching policy
**Mode:** `/gsd-autonomous --only 4 --interactive` (default discuss-phase mode, single-question turns)

______________________________________________________________________

## Toast policy

### Q1 — Should hot-plug arrive/depart events surface as toasts at all?

| Option                      | Description                                                                                                                                                                                          | Selected |
| --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| Silent — badge only         | No toast on arrive or depart. The sidebar badge state-change is the only feedback. Matches OSS convergent UX. Reserves toast surface for Phase 5 user-initiated 'Sync now'. Cheapest, calmest.       | ✓        |
| Single arrive toast only    | One toast on first-time arrive of a device this session. No depart, no re-arrive. Acknowledges the 'wow it sees my device' moment without flooding.                                                  |          |
| 1-cap queue, replace-oldest | Allow at most 1 toast on screen; new toasts replace the oldest non-error. Show on every coalesced arrive/depart. More feedback, but consumes the toast surface Phase 5 wants for sync confirmations. |          |

**User's choice:** Silent — badge only.

### Q2 — How should hot-plug *errors* surface (factory returns nullptr, hid_open fails, etc.)?

| Option                          | Description                                                                                                                                                 | Selected |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| Log only — no UI surface        | WARN to log, no toast, no badge change. The device just doesn't appear / stays offline. User sees absence; logs explain why. Matches the 'silent' contract. | ✓        |
| Sidebar badge variant ('error') | Add a third badge state: connected / offline / error (with tooltip explaining the failure). User can see *something is wrong* without a modal.              |          |
| Toast on hard errors only       | WARN+toast on hard failures. Reserves toast for actionable errors. Inconsistent with the 'silent' contract for arrivals though.                             |          |

**User's choice:** Log only — no UI surface.
**Notes:** Errors during enumeration are not user-actionable from inside the app; deferring to whatever phase introduces device pairing/setup.

______________________________________________________________________

## DeviceModel update strategy

### Q1 — How should DeviceModel propagate hot-plug changes to QML?

| Option                                                                 | Description                                                                                                                                                                                                                           | Selected |
| ---------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| Fine-grained dataChanged on ConnectedRole                              | Diff old vs new m_connected set; for each VID/PID whose state flipped, emit dataChanged(idx, idx, {ConnectedRole}). No reset, no selection loss, no scroll jump. ~30 LoC delta in DeviceModel::refresh(). Closes Pitfall 15 properly. | ✓        |
| Keep beginResetModel + QML codename-restore                            | Status quo. QML side tracks selected codename, listens for ListView.modelReset, finds the new index by codename, re-applies selection. Cheaper to implement but spreads the invariant across two layers.                              |          |
| Hybrid: dataChanged for connected-state, reset only on registry growth | Best-of-both: reset only when registerDevice() actually adds a new backend; dataChanged for live presence flips. Adds a versioning counter to DeviceRegistry.                                                                         |          |

**User's choice:** Fine-grained dataChanged on ConnectedRole.
**Notes:** The registered-backend list is static (only grows at `registerDevice()` time, never during hot-plug), so fine-grained updates are essentially free. This re-frames Pitfall 15 from "future-work" to "Phase 4 small-diff cleanup."

### Q2 — Should DeviceModel rows represent backends (codename) or physical units (codename + serial)?

| Option                                                          | Description                                                                                                                                                                              | Selected |
| --------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| One row per backend, ConnectedRole = any-unit-connected         | Status quo. Two AKP153s collapse to one sidebar row — the badge says 'connected' if at least one unit is plugged in. Per-unit state lives in TimeSyncService / future ProfileController. | ✓        |
| One row per physical unit, keyed (codename, serial)             | True multi-device sidebar. Two AKP153s = two rows with distinguishing labels. Aligns with Pitfall 2's identity warning. Bigger lift — likely a v1.2 feature.                             |          |
| One row per backend now, design unit-aware data model behind it | Sidebar stays one-row-per-codename in Phase 4, but DeviceModel internally tracks per-unit connected set. Pays a bit of internal complexity now to avoid a v1.2 rewrite.                  |          |

**User's choice:** One row per backend, ConnectedRole = any-unit-connected.
**Notes:** Per-unit sidebar identity is deferred to v1.2 (see Deferred Ideas in CONTEXT.md). The event-coalescing layer in `Application::onHotplug` still tracks `(vid, pid, serial)` per HOTPLUG-05.

______________________________________________________________________

## Debounce window value

### Q1 — What trailing-edge debounce window should Application::onHotplug use?

| Option | Description                                                                                                                                                                             | Selected |
| ------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| 300 ms | Comfortable middle. Catches composite-USB pairs (~10ms apart) and most flaky-cable bounces. Adds 300ms to first-paint latency. Symmetrical with Phase 5's own 300ms time-sync debounce. | ✓        |
| 250 ms | Lower bound from ROADMAP. Snappiest UI response. May let through some hub-shuffle bursts on slow USB hubs (>250ms inter-event).                                                         |          |
| 500 ms | Upper bound from ROADMAP. Most conservative — catches even slow USB hub topology renegotiation. Half-second 'where is my device' delay is noticeable.                                   |          |

**User's choice:** 300 ms.
**Notes:** Stacks with Phase 5's 300 ms time-sync debounce → 600 ms total from plug-in to auto-sync fire.

______________________________________________________________________

## DeviceRegistry::open() caching policy

### Q1 — Should DeviceRegistry::open() cache instances so multiple callers share one backend?

| Option                         | Description                                                                                                                                                                                                                                                         | Selected |
| ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| Add weak_ptr cache in Phase 4  | DeviceRegistry holds map\<DeviceId, weak_ptr<IDevice>>. open() locks the weak_ptr; if expired, constructs new shared_ptr + re-stores. All consumers share one backend, one hid_open. ~25 LoC delta. Closes the Windows-exclusive-open issue before Phase 5 hits it. | ✓        |
| Defer caching to Phase 5       | Phase 4 only flips ownership type; open() stays fresh-per-call. Phase 5 adds the cache when it actually has a second consumer. Keeps Phase 4 surface smaller.                                                                                                       |          |
| Don't cache; document the rule | Keep fresh-per-call permanently. Document 'callers must coordinate'. Most fragile, most explicit.                                                                                                                                                                   |          |

**User's choice:** Add weak_ptr cache in Phase 4.
**Notes:** Lifecycle contract — passive eviction (weak_ptr expiry on last consumer drop), not proactive on Removed events. Cached `shared_ptr` whose USB device was removed must return `Result::DeviceGone` rather than UB. Captured in CONTEXT.md D-06.

______________________________________________________________________

## Claude's Discretion

The following were left for Claude / planner to decide:

- **File layout** under `.planning/phases/04-hot-plug-hardening/` follows Phase 3 convention.
- **HOTPLUG-07 retro placement** — phase-local at `04-HOTPLUG-RETRO.md`; promotion to `docs/architecture/HOTPLUG.md` deferred to gsd-extract-learnings.
- **Multi-device test scenario matrix** — planner derives from HOTPLUG-06 + Pitfall 1/3 (UAF stress, hub yank, round-robin replug, two same-VID-PID-different-serial units).
- **Windows CI smoke scope** — smoke build + non-USB-touching unit test only; real Windows USB runner deferred to Phase 6 / v1.2.
- **`hid_open` invariant CI grep** added per Pitfall 11.

## Deferred Ideas

- **Per-unit sidebar rows (codename + serial)** — v1.2 once ProfileController/KeyDesigner become unit-aware.
- **Real Windows CI runner with USB capacity** — v1.2 / Phase 6 prerequisite.
- **Proactive cache eviction on Removed events** — passive weak_ptr expiry first; revisit if zombie-`shared_ptr` window proves problematic.
- **"Last seen N minutes ago" tooltip on offline rows** — quality-of-life follow-up.
- **Strong `shared_ptr` cache (instead of `weak_ptr`)** — rejected for v1.1 unless Phase 5 measures `open()` latency as a problem.
