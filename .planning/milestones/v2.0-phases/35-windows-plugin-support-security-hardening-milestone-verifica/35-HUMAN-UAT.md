---
status: partial
phase: 35-windows-plugin-support-security-hardening-milestone-verifica
source: [35-03-PLAN.md, 35-VALIDATION.md]
started: 2026-06-08T21:55:00Z
updated: 2026-06-08T21:55:00Z
---

## Current Test

\[awaiting human testing in a windowed GUI session — the chip-render walks
require opening the Loaded-plugins modal Drawer (harness `qml.invoke clicked`
gap) AND a populated `LoadedPluginsModel`, neither reproducible headlessly on
this machine. Loopback + consent/tamper invariants WERE confirmed headlessly +
test-locked this phase — see SUMMARY §"Headless live-gate results".\]

## What WAS confirmed this phase (not deferred)

- **PLGSEC-03 loopback (live + runtime).** Isolated offscreen instance launched
  with its own `XDG_RUNTIME_DIR` (user GUI untouched). `ajazz-debug ping` → pong;
  `ss -ltnp` showed the SdPluginServer bound `127.0.0.1:45429` (loopback only),
  and code asserts `listen(QHostAddress::LocalHost, ...)`
  (`sd_plugin_server.cpp:93`). Tests `SdPluginServer binds loopback only` +
  `bind loopback only on random port` green.
- **PLGSEC-01/02 (test-locked).** `tampered plugin refused even with consent`,
  `allowUnsignedPlugins=true tampered STILL refused (CR-01)`, and
  `per-plugin allow survives launch-sweep with global toggle OFF` all green.
- **WINPLG-03 derive (unit-locked).** `test_loaded_plugins_model.cpp` pins
  `winClass 1→"native"`, `2→"unsupported"` (wine false this phase), `0→""`, and
  asserts the `platformStatus` role is in `roleNames()`.
- **VERIF-01 source coverage.** All three chips carry an `objectName` + a
  headless-readable label property (`grep -c objectName LoadedPluginsPage.qml`
  ≥ 3, was 0). Full suite green: 802/802 (785 unit/integration + 17 qml).

## Deferred live tests (need a windowed GUI session)

### 1. WINPLG-03 status-chip render + `qml.get` (VERIF-01)

expected: In a windowed session, click the "Loaded" header button to open the
Loaded-plugins drawer with at least one classified plugin present. The
`platformStatusChip` shows "Runs natively" for a WS-only-IPC win `.sdPlugin`
(os=[windows], CodePath `.js`) and "Unsupported on this OS" for a vendor-DLL win
`.sdPlugin` (CodePath `.exe`/`.dll` or an MZ-magic file; Wine absent on this
host). `scripts/ajazz-debug qml.get` on `platformStatusChip.statusLabel`,
`unsignedConsentChip.consentLabel`, and `trustChip.trustLabel` returns the
locked copy; a screenshot shows the chips rendered (not blank).
result: [pending — BLOCKED headless: see blockers below]

### 2. PLGSEC-02 consent restart round-trip (live UI)

expected: Install an unsigned `.sdPlugin` and grant consent; `plugin.list` shows
it loaded. Kill + relaunch the app; `plugin.list` shows it loaded again WITHOUT
a re-prompt (consent persisted in QSettings `plugins/allowed/<uuid>`, survives
the launch-sweep). NOTE: the persistence is already test-locked
(`per-plugin allow survives launch-sweep`, green); this UAT item is the live UI
confirmation of the no-re-prompt behavior.
result: [pending — live UI]

## Blockers (honest, recorded — not fabricated)

1. **[RESOLVED — code-review fix CR-01]** `LoadedPluginsModel` is now wired to
   the merged `.sdPlugin` + Python inventory in the shipping build. After the
   `.sdPlugin` discover+spawn loop and `m_pluginHost2` construction
   (`application.cpp` `startBackgroundServices`), the model is re-pointed at the
   unified host via `LoadedPluginsModel::setPluginHost2(m_pluginHost2.get())` +
   `setPlugins(m_pluginHost2->plugins())` — `setPlugins()` does a full reset
   (REPLACE, not append), so the Python-only list is swapped for the merged list
   with no duplication, and the Python entries remain present. The
   `platformStatusChip` now instantiates for win-only `.sdPlugin` rows (the only
   ones carrying a meaningful `winClass`). The production population path is
   unit-covered by `LoadedPluginsModel refresh from unified host surfaces win platformStatus (WR-02)` in `test_loaded_plugins_model.cpp`. The deferred LIVE
   render walk (item 1 above) is now unblocked at the wiring level and only
   awaits a windowed GUI session.
1. **The Loaded-plugins drawer cannot be opened headlessly.** It is a modal
   `Drawer`/`Popup` opened via the `navLoaded` ToolButton's `onClicked` →
   `loadedPluginsRequested()`; the documented CLAUDE.md harness gap is that
   `qml.invoke clicked` / `qml.invoke open` do not fire for ToolButton/Popup. A
   real windowed click is required to realize the delegate chips.

## WR-02 live-walk requirement (code-review follow-up)

The unit test now exercises the real production population path (an
`IPluginHost2`-shaped merged vector fed through `setPluginHost2`+`refresh`,
asserting a win-only row surfaces a non-empty `platformStatus` and the Python
row stays present). Per CLAUDE.md "Debug-channel verification — MANDATORY", the
DEFINITIVE end-to-end confirmation is still a live walk in a windowed session:
install a real win-only `.sdPlugin` (os=[windows], CodePath `.js`), open the
Loaded-plugins drawer, and `scripts/ajazz-debug qml.get` on
`platformStatusChip.statusLabel` — expect the non-empty "native" copy (not blank).
This is the same deferred VERIF-01 walk in item 1 above; the CR-01 wiring fix is
the prerequisite that makes it observable, and it is now in place.

1. **The Loaded-plugins drawer cannot be opened headlessly.** It is a modal
   `Drawer`/`Popup` opened via the `navLoaded` ToolButton's `onClicked` →
   `loadedPluginsRequested()`; the documented CLAUDE.md harness gap is that
   `qml.invoke clicked` / `qml.invoke open` do not fire for ToolButton/Popup. A
   real windowed click is required to realize the delegate chips.
