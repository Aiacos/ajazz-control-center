---
quick_id: 260513-uy6
slug: plugin-store-phase-a
date: 2026-05-13
status: complete
parent_diagnostic: 260513-u0b
---

# Quick Task 260513-uy6: Plugin Store Phase A — Summary

**Goal:** Make Plugin Store fetch diagnosable and recoverable without an app restart.
**Parent diagnostic:** [260513-u0b FINDINGS](../260513-u0b-streamdock-plugins-diagnostic/260513-u0b-FINDINGS.md) — resolves CRITICAL-1, CRITICAL-2, MEDIUM-2 (the three highest-priority issues).

## What landed

### `src/app/src/streamdock_catalog_fetcher.cpp` (+50 LoC)

- `#include <QLoggingCategory>` added; `Q_LOGGING_CATEGORY(lcStreamdock, "ajazz.plugins.streamdock")` declared in the anonymous namespace.
- **Re-entry guard** at top of `refresh()`: if `m_state == State::Loading`, log + early return.
- **15 `qC*` calls** across the fetch lifecycle:
  - `qCInfo` — refresh start (with URL), live-fetch-disabled short-circuit, page fetch start (with page N / total), total-pages report, pagination complete (with row count), cache write success.
  - `qCWarning` — invalid URL, network error (with QNetworkReply::error + errorString + URL + HTTP status), JSON parse error (with offset + first 200 bytes), upstream envelope rejection (with `code` + `msg` field), cache write failures (open + short-write).

### `src/app/src/opendeck_catalog_fetcher.cpp` (+30 LoC)

- Same `<QLoggingCategory>` + `Q_LOGGING_CATEGORY(lcOpenDeck, "ajazz.plugins.opendeck")` pattern.
- Same re-entry guard at top of `refresh()`.
- **10 `qC*` calls** — surface is smaller (one GET, no pagination): refresh start, live-fetch-disabled, fetch complete with row count, fetch failure, no-parseable-rows fallback, cache-path-empty, cache-open-failure, short-write, cache write success, plus the guard log.

### `src/app/qml/PluginStore.qml` (+24 LoC, 2 ToolButtons)

- Refresh `ToolButton` (`↻` glyph) added to the Streamdock banner's status row (line ~317).
- Mirror added to the OpenDeck banner (line ~447).
- Both buttons:
  - Disabled while the respective state is `"loading"` (cleaner UX than relying purely on the C++ re-entry guard).
  - `onClicked: if (PluginCatalog) PluginCatalog.reload()` — drives both fetchers via the existing `Q_INVOKABLE` slot.
  - Carry `ToolTip` ("Refresh catalogue") + `Accessible.role/name` ("Refresh Streamdock catalogue" / "Refresh OpenDeck catalogue").

## What this resolves

| Diagnostic finding                                     | Resolution                                                                                                                                                          |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **CRITICAL-1** — zero logging in plugin pipeline       | 25 new `qC*` calls cover every state transition and error path in both fetchers; user can now `QT_LOGGING_RULES="ajazz.plugins.*=true"` to see what fetch is doing. |
| **CRITICAL-2** — no manual refresh                     | Refresh buttons in both banners; `PluginCatalog.reload()` plumbing was already wired.                                                                               |
| **MEDIUM-2** — race on `m_accumulated` if Retry exists | Re-entry guard at top of both `refresh()` methods returns early when already loading.                                                                               |

## What this does NOT touch

Phase B (HIGH-1 + HIGH-2 — `QSortFilterProxyModel` refactor to fix GridView layout) is intentionally out of scope. Phase A unblocks observability and recovery; Phase B fixes the visible layout glitches. Recommendation in the parent diagnostic: handle Phase B as a separate quick task.

## Verification

| Check                                                           | Result                                                                    |
| --------------------------------------------------------------- | ------------------------------------------------------------------------- |
| Streamdock log call count (target ≥10)                          | **15** ✓                                                                  |
| OpenDeck log call count (target ≥6)                             | **10** ✓                                                                  |
| `PluginCatalog.reload()` call sites in QML (target 2)           | **2** ✓                                                                   |
| `cmake --build build/linux-debug --target ajazz-control-center` | **OK** ✓ — both fetchers + PluginStore_qml.cpp compile clean, app relinks |
| `ctest -I 64,79` (Streamdock + OpenDeck unit tests)             | **16/16 pass, 100%** ✓ — behaviour unchanged                              |

## How to verify the logging works at runtime

```bash
QT_LOGGING_RULES="ajazz.plugins.*=true" ./build/linux-debug/src/app/ajazz-control-center 2>&1 | grep "ajazz.plugins"
```

Expected lines on first launch (online):

```
ajazz.plugins.streamdock: starting live catalogue fetch: "https://space.key123.vip/..."
ajazz.plugins.streamdock: fetching page 1 (total unknown until page 1 returns)
ajazz.plugins.streamdock: upstream reports 3 page(s); will fetch 3 (kMaxPages= 20 )
ajazz.plugins.streamdock: fetching page 2 of 3
ajazz.plugins.streamdock: fetching page 3 of 3
ajazz.plugins.streamdock: pagination complete: 137 row(s) across 3 page(s) — writing cache
ajazz.plugins.streamdock: cache written: "/home/.../streamdock-catalog.json" ( 84321 bytes)
ajazz.plugins.opendeck: starting live catalogue fetch: "https://plugins.amankhanna.me/catalogue.json"
ajazz.plugins.opendeck: fetch complete: 325 row(s) — writing cache
ajazz.plugins.opendeck: cache written: "/home/.../opendeck-catalog.json" ( 158742 bytes)
```

When clicking Retry while a fetch is in flight, expect:

```
ajazz.plugins.streamdock: refresh() ignored — fetch already in flight
```

## Phase B handoff

The diagnostic's HIGH-1 / HIGH-2 fixes are queued for a separate `/gsd-quick` task. Recommended shape:

- Introduce `PluginCatalogProxyModel : QSortFilterProxyModel` in C++ with writable `tab` + `query` properties.
- Move `rowMatches()` from QML JS into C++ `filterAcceptsRow()`.
- Rewire `PluginStore.qml` GridView's `model: PluginCatalog` to `model: PluginCatalogProxy`.
- Delete `tile.matches`, `visibleCount` accounting, and the zero-size `width:0; height:0` filter pattern.
- `EmptyState.visible: proxy.rowCount() === 0` — single source of truth.

Estimated effort: half-day.

## Routing

This work is **outside the v1.1 milestone scope** (28 REQs, 6 phases for hot-plug + time-sync + carry-overs). Tracked here as quick task so the milestone stays focused.
