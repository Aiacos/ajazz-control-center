---
quick_id: 260513-u0b
slug: streamdock-plugins-diagnostic
mode: diagnostic
status: complete
date: 2026-05-13
---

# Diagnostic: Stream Dock plugin loading + online fetch + logs + UI layout

**Request (IT):** _Ricontrolla attentamente i plugin per AJAZZ Stream dock. il loro caricamento ed il fetch online, controlla i log, e il layout nella UI sembra ancora non essere corretto._

**Approach:** Static code analysis across the plugin-catalogue pipeline (no runtime instrumentation). Walked the fetcher + model + QML page top-to-bottom, focused on the four axes the user named.

**Codebase surfaces inspected:**

- `src/app/src/streamdock_catalog_fetcher.{hpp,cpp}` (587 LoC) — upstream HTTP pagination + cache + bundled fallback
- `src/app/src/opendeck_catalog_fetcher.{hpp,cpp}` (379 LoC) — sister fetcher for the OpenDeck mirror
- `src/app/src/plugin_catalog_model.{hpp,cpp}` — `QAbstractListModel` exposed as the `PluginCatalog` QML singleton
- `src/app/qml/PluginStore.qml` (909 LoC) — tabs + grid + side-sheet
- `src/app/qml/LoadedPluginsPage.qml` (196 LoC) — live inventory page
- `src/app/src/application.{hpp,cpp}` — wiring at startup

______________________________________________________________________

## Findings (severity-ordered)

### CRITICAL-1 · Zero logging in the plugin-catalogue pipeline

**Symptom mapped:** "controlla i log" yields nothing.

**Evidence:** Zero `qInfo` / `qWarning` / `qCritical` / `qDebug` calls in `streamdock_catalog_fetcher.cpp`, `opendeck_catalog_fetcher.cpp`, or `plugin_catalog_model.cpp`. No `qInstallMessageHandler` or `QLoggingCategory` configured anywhere in the codebase. Confirmed via:

```bash
grep -rn 'qDebug\|qInfo\|qWarning\|qCritical' src/app/src/streamdock_catalog_fetcher.cpp \
  src/app/src/opendeck_catalog_fetcher.cpp src/app/src/plugin_catalog_model.cpp
# → empty
grep -rn 'qInstallMessageHandler\|QLoggingCategory' src/
# → empty
```

**Worst offender** — `streamdock_catalog_fetcher.cpp:537-558` silently swallows the two failure modes the user is most likely to hit:

```cpp
void StreamdockCatalogFetcher::onPageFinished(QNetworkReply* reply) {
    reply->deleteLater();
    QUrl const origin = effectiveCatalogUrl();

    if (reply->error() != QNetworkReply::NoError) {
        // Live fetch failed. Keep the previously emitted cached / fallback
        // snapshot — we don't want to flap the UI back to "loading".
        if (m_state == State::Loading) {
            m_state = m_accumulated.empty()
                          ? (loadFromCache().rows.empty() ? State::Offline : State::Cached)
                          : State::Cached;
            emit stateChanged(m_state);
        }
        return;  // ← no log of URL, HTTP status, error message
    }

    // ... JSON parse / `code != 200` envelope check ...
    if (err.error != QJsonParseError::NoError || !doc.isObject() ||
        doc.object().value(QStringLiteral("code")).toInt() != 200) {
        if (m_state == State::Loading) {
            m_state = m_accumulated.empty() ? State::Offline : State::Cached;
            emit stateChanged(m_state);
        }
        return;  // ← no log of the parse error or the upstream code
    }
```

**Impact:** When "fetch online" doesn't work, the UI shows a state pill change (online → cached/offline) but the user has no way to find out **why**: DNS error? TLS handshake? 401? 5xx? Malformed JSON? Upstream `code != 200` envelope? All silent.

**Fix shape (~30 LoC):**

- Add a `QLoggingCategory` (e.g. `Q_LOGGING_CATEGORY(lcPluginCatalog, "ajazz.plugins.catalog")`) at the top of each fetcher TU
- Log at `qInfo` for: refresh start, page-by-page progress, total rows, cache write
- Log at `qWarning` for: timeout, HTTP error, JSON parse error, envelope `code != 200`
- Optionally wire `qInstallMessageHandler` so messages also tee to `$XDG_CACHE_HOME/ajazz-control-center/log/app.log`

______________________________________________________________________

### CRITICAL-2 · No manual refresh affordance in the Plugin Store UI

**Symptom mapped:** "il fetch online" — user can't retry without restarting the app.

**Evidence:** `PluginCatalogModel::reload()` is `Q_INVOKABLE` (callable from QML) and triggers `m_streamdockFetcher->refresh()` + `m_opendeckFetcher->refresh()`. It is called **only once**, from the `PluginCatalogModel` constructor (`plugin_catalog_model.cpp:131`). Confirmed:

```bash
grep -rn 'PluginCatalog\.reload\|->reload()' src/  # → 1 hit, the constructor itself
```

The two QML banners (Streamdock at line 199, OpenDeck at line 329) surface the fetch state via a status pill but have **no Retry button**. So a user who:

- Starts the app while offline → lands in `offline` / bundled-fallback state
- Reconnects to the network 30 seconds later
- … has no way to re-fetch except quitting and relaunching the app.

**Impact:** Compounds CRITICAL-1 — the user can't even retry-on-demand, much less observe what's failing.

**Fix shape (~15 LoC QML + 0 LoC C++):** Add a `ToolButton` next to each banner's status pill calling `PluginCatalog.reload()`. The plumbing already exists.

______________________________________________________________________

### HIGH-1 · GridView delegate `visibleCount` drifts on recycle (likely "layout sembra non corretto")

**Symptom mapped:** "il layout nella UI sembra ancora non essere corretto."

**Evidence:** `PluginStore.qml:538-546`:

```qml
onVisibleChanged: {
    if (visible) grid.visibleCount += 1;
    else grid.visibleCount = Math.max(0, grid.visibleCount - 1);
}
Component.onCompleted: if (visible) grid.visibleCount += 1
Component.onDestruction: if (visible) grid.visibleCount = Math.max(0, grid.visibleCount - 1)
```

**The bug:** GridView **recycles** delegates — it doesn't destroy + recreate them when items scroll off-screen or model roles change. So:

- A delegate created off-screen with `visible: false` runs `Component.onCompleted` with no increment.
- It then scrolls into view → roles update → `matches` flips true → `visible` flips true → `onVisibleChanged` runs `+= 1`. Correct.
- But if a recycle event arrives **without `visible` changing** (e.g., a `matches`-true row is replaced by another `matches`-true row), the counter doesn't move. Correct.
- However, when the user toggles tabs (`activeTab` change), every delegate re-evaluates `matches`. Some flip true→false, fire `onVisibleChanged` (–1). Some flip false→true, fire `onVisibleChanged` (+1). **But recycled delegates that were destroyed-then-recreated for new model positions might have stale counter increments from the original instance.**

The fundamental issue: `visibleCount` is being maintained via DOM-event-style increment/decrement on a recyclable view that has no guarantee of `onCompleted` ⇄ `onDestruction` parity.

**Where this surfaces visually:** The `EmptyState` fallback at line 457 reads:

```qml
visible: grid.visibleCount === 0
```

So the empty-state placeholder may appear when there ARE matching rows (false positive) or stay hidden when there are zero matches (false negative). Either way: layout doesn't match the data, which is exactly the "non essere corretto" feel.

**Fix shape (~5 LoC):** Replace the counter with a direct count from the model:

```qml
property int visibleCount: {
    var c = 0;
    if (!PluginCatalog) return 0;
    for (var i = 0; i < PluginCatalog.count; ++i) {
        var e = PluginCatalog.data(PluginCatalog.index(i, 0), /* MatchesRole */ ...);
        // ... or compute the filter in C++ via a QSortFilterProxyModel
    }
    return c;
}
```

Better: introduce a `QSortFilterProxyModel` in C++ (the comment at line 42-44 of PluginStore.qml acknowledges this is the intended fix). Then `visibleCount` becomes `proxy.rowCount()` — single source of truth.

______________________________________________________________________

### HIGH-2 · Filter pattern `visible: false; width:0; height:0` breaks GridView cell math

**Symptom mapped:** "il layout nella UI sembra ancora non essere corretto" — compounds HIGH-1.

**Evidence:** `PluginStore.qml:528-532`:

```qml
readonly property bool matches: root.rowMatches(
    tile.name, tile.description, tile.tags, tile.source, tile.installed)
visible: matches
width: matches ? grid.cellWidth - Theme.spacingMd : 0
height: matches ? grid.cellHeight - Theme.spacingMd : 0
```

GridView treats cellWidth/cellHeight as fixed grid coordinates and **flows delegates into those slots regardless of the delegate's actual rendered size**. So zero-sized non-matching tiles still **occupy a grid slot**, but render nothing. Visible tiles get pushed to the next slot. Effect: visible tiles are sparse, with phantom gaps where filtered-out rows live.

This is also why the explicit cell sizing (`cellWidth: 252; cellHeight: 212`) gives the impression of "tiles floating with weird gaps." The grid math is honoring the cell allocation, not the delegate's render.

**Fix shape:** Same as HIGH-1 — `QSortFilterProxyModel` so the GridView only sees matching rows, not the union with `visible: false` placeholders.

______________________________________________________________________

### MEDIUM-1 · Persisted `activeTab` can point at the wrong tab after refactor

**Symptom mapped:** "il layout sembra ancora non essere corretto" — possible secondary cause.

**Evidence:** `PluginStore.qml:78-81`:

```qml
Settings {
    category: "PluginStore"
    property alias activeTab: root.activeTab
}
```

Tab order today is `[All, Installed, AJAZZ Streamdock, OpenDeck, Community]` (indices 0-4). If OpenDeck was inserted at index 3 in a recent refactor (after a previous tab order without it), users who had selected "Community" (then index 3) would now land on OpenDeck on app re-launch — **without anyone knowing**, because the persisted integer is treated as opaque.

**Quick check (manual):**

```bash
# On Linux this typically lives at:
cat ~/.config/Aiacos/AjazzControlCenter.conf | grep -A2 PluginStore
```

If `activeTab=4` is observed but the user has never opened "Community," that confirms the drift.

**Fix shape:** Persist the tab **identifier** ("streamdock", "opendeck", …) instead of an integer index. ~10 LoC QML change.

______________________________________________________________________

### MEDIUM-2 · `m_accumulated` not reset on overlapping `refresh()` calls

**Symptom mapped:** Not the user's stated complaint, but a latent foot-gun once CRITICAL-2's "Retry" lands.

**Evidence:** `streamdock_catalog_fetcher.cpp:474-504`. `refresh()` calls `m_accumulated.clear()` at line 493 only if the live fetch path is taken. If two `refresh()` calls overlap (e.g., user spam-clicks a future "Retry" button while a fetch is in flight), the second call's `clear()` could fire after page N of the first fetch has already appended, **dropping the first fetch's accumulated rows mid-flight**. Both calls then fight for the same `m_accumulated` vector with no in-flight cancellation.

**Fix shape:** Either guard with `if (m_state == State::Loading) return;` at the top of `refresh()`, or `cancel()` the existing replies before clearing.

______________________________________________________________________

### LOW-1 · No retry on transient HTTP errors during pagination

**Symptom mapped:** Compounds "il fetch online" — partial-fetch resilience.

**Evidence:** `streamdock_catalog_fetcher.cpp:537-547`. One page-level error → entire pagination chain aborts. For a 5-page Streamdock catalogue, a single transient flake on page 3 throws away pages 1-2's data.

**Fix shape (out of scope for v1.1 likely):** Retry-with-backoff at the page level, or fall back to "show what we got so far" instead of nuking the partial accumulation.

______________________________________________________________________

### LOW-2 · `tile.matches` recomputes on every model `dataChanged`

**Symptom mapped:** Performance, not the user's stated complaint. Noted for completeness.

**Evidence:** `tile.matches` reads `tile.name / description / tags / source / installed`. Every install/uninstall fires `dataChanged` for one row's `InstalledRole`, but `matches` is a `readonly property` that re-evaluates whenever any of its dependencies change. For 100+ rows, that's 100+ filter recomputations per install. Not broken, but noisy in profiling.

______________________________________________________________________

## Map of findings → user's four axes

| User said                                     | Most relevant findings                                                                                         |
| --------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| "il loro caricamento" (loading)               | CRITICAL-1 (no logs to diagnose load issues), CRITICAL-2 (no manual reload)                                    |
| "il fetch online"                             | CRITICAL-1 (silent fetch failures), CRITICAL-2 (no retry), MEDIUM-2 (race on retry), LOW-1 (no per-page retry) |
| "controlla i log"                             | **CRITICAL-1 (no logs exist)** — this axis is purely a logging gap, not a code bug                             |
| "il layout sembra ancora non essere corretto" | **HIGH-1 (visibleCount drift)**, **HIGH-2 (zero-size filter pattern)**, MEDIUM-1 (stale activeTab)             |

______________________________________________________________________

## Recommended next step

Two phases of follow-up, in priority order:

**Phase A — quick wins (1 commit, ~50 LoC):**

1. Add `Q_LOGGING_CATEGORY` to both fetchers, log fetch start/end + every error path (CRITICAL-1)
1. Add a Retry `ToolButton` to both banners → `PluginCatalog.reload()` (CRITICAL-2)
1. Guard `refresh()` against re-entry while `m_state == State::Loading` (MEDIUM-2 prep)

Estimated effort: 1-2 hours. **Run the app after this and check what the logs actually say** — that alone may answer whether the "fetch online" actually works against real upstream.

**Phase B — layout fix (1 commit, ~150 LoC):**

1. Introduce a `PluginCatalogProxyModel : QSortFilterProxyModel` in C++ that takes the source `PluginCatalogModel` + a writable `tab` + `query` property
1. Move `rowMatches()` logic from QML JS into C++ `filterAcceptsRow()`
1. Rewire `PluginStore.qml` GridView's `model` to the proxy; delete `tile.matches`, `visibleCount` accounting, and the zero-size width/height pattern
1. EmptyState binds to `proxy.rowCount() === 0` — single source of truth

Estimated effort: half-day. Resolves HIGH-1 + HIGH-2 + LOW-2 together. Also aligns with the comment at `PluginStore.qml:42-44` that already acknowledges this is the intended direction.

**Phase C — persistence robustness (optional, post v1.1):**

- Migrate the persisted `activeTab` to a tab-name string instead of an integer (MEDIUM-1)
- Per-page retry-with-backoff in `onPageFinished` (LOW-1)

______________________________________________________________________

## Relationship to v1.1 milestone

This investigation surfaced 8 issues that are **not** currently in v1.1 REQUIREMENTS.md. Two routing options:

1. **Tack onto v1.1 as a new mini-phase** (e.g. "Phase 9: Plugin Store hardening") with REQ-IDs PLUGINS-01..05 covering Phase A + Phase B. Adds scope to a milestone that's already at 28 REQs.
1. **Track as a follow-up quick-task** that lands outside the milestone. Use `/gsd-quick --discuss` for Phase A (the logging + Retry button) and a separate `/gsd-quick` for Phase B (the proxy model refactor). Keeps v1.1 focused on the scoped work.

Recommendation: **Option 2**. v1.1 is already chunky (6 phases, 28 REQs, 4 architectural decision points). Plugin Store hardening is its own coherent thread that benefits from being independently shippable; folding it in would smear focus across phases.
