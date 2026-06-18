# Contract — Plugin Store install affordance (no browser)

**Scope**: the plugin store row's primary action and the `PluginCatalogModel::install` path.
Impl surfaces: `src/app/qml/PluginStore.qml` (row button), `src/app/src/plugin_catalog_model.{hpp,cpp}`.

## Per-row primary action — state machine

A catalog row presents exactly ONE primary action with these states. The action MUST NEVER
open an external browser as part of installing.

| State              | Button label                                                   | Enabled         | Trigger / transition                                                    |
| ------------------ | -------------------------------------------------------------- | --------------- | ----------------------------------------------------------------------- |
| **Installable**    | `Install`                                                      | yes             | entry has a resolvable `https` package URL (`installableInApp == true`) |
| **Installing**     | `Installing… N%`                                               | no              | `install(uuid)` accepted; inline progress from `installProgressChanged` |
| **Installed**      | `Installed` (+ uninstall affordance)                           | yes (uninstall) | `installFinished(uuid, true, "")`                                       |
| **NotInstallable** | `Not installable in-app` (label/tooltip = `unavailableReason`) | **no**          | `installableInApp == false`                                             |

- `install(uuid)` on an **Installable** row → downloads + extracts in-app; emits
  `installProgressChanged(uuid, pct)` and finally `installFinished(uuid, success, error)`.
- `install(uuid)` on a **NotInstallable** row → **no-op returning false**; MUST NOT call
  `openUpstream()` / `QDesktopServices::openUrl`.
- Failure: `installFinished(uuid, false, error)` → row returns to **Installable**, inline error
  shown; nothing partially-installed is left usable.
- After **Installed**, the plugin's actions MUST appear in the editor action list with no extra
  navigation (existing `installedCountChanged` → catalogue refresh path).

## Hard rules

- **No browser launch from the install action** (removes the `openUpstream`/"Open page ↗" path).
- **No side drawer** is required to perform an install.
- **Scope = UI + flow only**: the contract does NOT require resolving private/secret download
  paths; a source without a resolvable package stays **NotInstallable** with a reason.
- A separate, clearly-non-install "details" link MAY exist but is out of this contract's primary
  action; default is no browser affordance at all.

## Debug-channel verification

- `plugin.installFromCatalog`/`plugin.list` drive + observe install; the store row button is
  `objectName`-addressable; `screenshot` confirms `Install`/`Installing`/`Installed`/disabled
  states; assert **zero** `QDesktopServices::openUrl` on the install path.
