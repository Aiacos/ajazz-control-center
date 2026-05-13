---
quick_id: 260513-v6b
slug: plugin-store-phase-b
date: 2026-05-13
status: complete
parent_diagnostic: 260513-u0b
sibling: 260513-uy6 (Phase A — logs + Retry)
---

# Quick Task 260513-v6b: Plugin Store Phase B — Summary

**Goal:** Replace QML-side filtering (`tile.matches` + `visible:0; width:0; height:0` + manual `visibleCount`) with a C++ `QSortFilterProxyModel` so the GridView only sees matching rows and the layout reflows correctly.
**Parent diagnostic:** [260513-u0b FINDINGS](../260513-u0b-streamdock-plugins-diagnostic/260513-u0b-FINDINGS.md) — resolves HIGH-1, HIGH-2, LOW-2.

## What landed

### `src/app/src/plugin_catalog_proxy_model.{hpp,cpp}` (NEW, ~120 LoC)

- `PluginCatalogProxyModel : QSortFilterProxyModel` declared with `QML_NAMED_ELEMENT(PluginCatalogProxy)` — instantiable inline in QML, scoped to the page (not a singleton).
- **Properties:**
  - `Q_PROPERTY(int activeTab READ activeTab WRITE setActiveTab NOTIFY activeTabChanged)` — mirrors `PluginStore.qml`'s tab index (0=All, 1=Installed, 2=Streamdock, 3=OpenDeck, 4=Community).
  - `Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)` — search string. `setQuery` lowercases internally so `filterAcceptsRow` does a plain `contains` rather than per-row case-folding.
  - `Q_PROPERTY(int count READ count NOTIFY countChanged)` — visible-row count after filtering; single source of truth for the EmptyState.
- **`filterAcceptsRow`** mirrors the old QML `rowMatches()` behaviour exactly: tab filter via `SourceRole`/`InstalledRole`, then case-insensitive substring across `NameRole`, `DescriptionRole`, and each string in `TagsRole`.
- **`countChanged` wiring** connects to `rowsInserted`, `rowsRemoved`, `modelReset`, **and `layoutChanged`** — the last is what `QSortFilterProxyModel::invalidate()` actually emits when a filter property changes (verified empirically via unit test 87, which failed without the `layoutChanged` connection).
- **Re-evaluation:** Setters call `invalidate()` rather than the deprecated `invalidateFilter()` / `invalidateRowsFilter()` or the protected `begin/endFilterChange()` pair (the latter is a no-op when called without internal state mutation in Qt 6.7).

### `src/app/CMakeLists.txt`

- Added `src/plugin_catalog_proxy_model.cpp` to the `ajazz-control-center` source list.

### `src/app/qml/PluginStore.qml` (net delete ~35 LoC, structural simplification)

- New `PluginCatalogProxy { id: catalogProxy; sourceModel: PluginCatalog; activeTab: root.activeTab; query: root.query }` at page scope.
- GridView's `model: PluginCatalog` → `model: catalogProxy`.
- EmptyState's `visible: grid.visibleCount === 0` → `visible: catalogProxy.count === 0`.
- **Deleted from delegate:**
  - `readonly property bool matches: root.rowMatches(...)` (4 lines)
  - `visible: matches; width: matches ? ... : 0; height: matches ? ... : 0` → replaced with unconditional sizing.
  - `onVisibleChanged { ... }`, `Component.onCompleted: if (visible) ...`, `Component.onDestruction: if (visible) ...` (6 lines)
- **Deleted from GridView:** `property int visibleCount: 0`.
- **Deleted from page scope:** `function rowMatches(...)` (~22 lines) — now in C++.

### `tests/unit/test_plugin_catalog_proxy_model.cpp` (NEW, 10 test cases) + `tests/unit/CMakeLists.txt`

- Hand-rolled `MockCatalogSource : QAbstractListModel` with controlled fixture (2 streamdock + 2 opendeck + 2 community + 1 local; some marked installed).
- Covered: default identity passthrough; each tab filter in isolation; query against name / description / tags; tab + query intersection; `countChanged` fires on filter change; `setQuery` idempotency (no spurious signal on equal value); proxy with no source attached.

## What this resolves

| Diagnostic finding                                                                       | Resolution                                                                                                                                                                                  |
| ---------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **HIGH-1** — `visibleCount` drift on delegate recycle                                    | Counter eliminated; EmptyState predicate is now `catalogProxy.count === 0`, sourced from `QSortFilterProxyModel::rowCount()` after `invalidate()`. Single source of truth, no recycle math. |
| **HIGH-2** — `visible:0; width:0; height:0` filter pattern leaves phantom GridView cells | GridView only sees matching rows now (proxy hides non-matching rows from the model entirely), so cellWidth/cellHeight math is honest — no sparse layout.                                    |
| **LOW-2** — `tile.matches` recomputes on every `dataChanged` for all rows                | Filter logic in C++ via `filterAcceptsRow`; Qt only re-evaluates affected rows on `dataChanged`, not all 100+.                                                                              |

## What this does NOT touch

- **MEDIUM-1** — Persisted `activeTab` integer can point at the wrong tab after tab-order refactor. Separate concern (data migration); diagnostic left it as P3.
- **LOW-1** — No per-page retry-with-backoff on transient HTTP errors. Phase A added per-fetch retry button; per-page resilience is a deeper change.

## Verification

| Check                                                                                       | Result             |
| ------------------------------------------------------------------------------------------- | ------------------ |
| `grep -c 'tile\.matches\|grid\.visibleCount\|onCompleted: if (visible)'` in PluginStore.qml | **0** ✓            |
| `grep -c 'model: catalogProxy'` in PluginStore.qml                                          | **1** ✓            |
| `grep -c 'PluginCatalogProxy {'` in PluginStore.qml                                         | **1** ✓            |
| `cmake --build build/linux-debug --target ajazz-control-center`                             | **OK** ✓           |
| `cmake --build build/linux-debug --target ajazz_unit_tests`                                 | **OK** ✓           |
| `ctest -I 64,89` (Streamdock 8 + OpenDeck 8 + Proxy 10 = 26)                                | **26/26 pass** ✓   |
| `ctest` full suite                                                                          | **150/150 pass** ✓ |

## Build notes worth remembering

1. **Qt 6.7 deprecates `invalidateFilter()` AND `invalidateRowsFilter()`** with the migration message pointing at `begin/endFilterChange()`. But: the protected `begin/endFilterChange()` pair appears to be a no-op when called in sequence without internal state mutation between them — at least in this Qt build's implementation. The public `invalidate()` is the working escape hatch.
1. **`invalidate()` emits `layoutChanged`, not `rowsInserted`/`rowsRemoved`.** If you wire a `count` Q_PROPERTY to "row-count-changing signals," `layoutChanged` is mandatory or the property goes stale after filter updates. Caught immediately by unit test 87.
1. **`QML_NAMED_ELEMENT` on a `QSortFilterProxyModel` subclass works cleanly** without any `qmlRegisterType` call — Qt's `qt_add_qml_module` picks it up via the AUTOMOC scan. The QML side just declares `PluginCatalogProxy { ... }`.

## Routing

Outside v1.1 milestone scope (28 REQs, 6 phases for hot-plug + time-sync + carry-overs). Tracked as a quick task. The full plugin-store fix is now: **diagnostic (260513-u0b)** → **Phase A logs+Retry (260513-uy6)** → **Phase B proxy refactor (this)**. All three landed in one session.
