# Phase 35: Windows Plugin Support + Security Hardening + Milestone Verification - Pattern Map

**Mapped:** 2026-06-08
**Files analyzed:** 14 (6 net-new code, 4 verify-only code, 4 deliverable docs/tests)
**Analogs found:** 14 / 14 (every analog grep-verified against the live tree this session)

> **RESEARCH re-verification note:** Every file:line cited in 35-RESEARCH.md was
> re-grepped this session and **confirmed**, with only trivial drift (the
> consent `setValue` is at `plugin_catalog_model.cpp:1072`, research said
> 1070-1076 — same FIX-CONSENT block). The launch-sweep `perPluginAllowed`
> read is at `:173` (matches). `VerifyVerdict` enum is at
> `plugin_verify_gate.hpp:41-46` (matches). OS-gate hook is at
> `plugin_manager.cpp:278` (matches). CI gate is at `ci.yml:67-83` (matches).
> **One material correction to A1 below** (the chip surface) based on new
> grep evidence the research under-weighted.

______________________________________________________________________

## File Classification

| New/Modified File                                                           | Role                    | Data Flow        | Closest Analog                                                                                                              | Match Quality                         |
| --------------------------------------------------------------------------- | ----------------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| `classifyWindowsPlugin()` helper (in `plugin_manifest.{hpp,cpp}`)           | utility (free function) | transform        | `manifestRunnableHere()` `plugin_manifest.cpp:316-362`                                                                      | exact (same file, same free-fn idiom) |
| `supportsCurrentPlatform()` helper                                          | utility (free function) | transform        | OS gate in `manifestRunnableHere()` + LOCKED linux-accept `plugin_manifest.cpp:319-350`                                     | exact                                 |
| classification model role (`WinClassRole`/`platformStatus`)                 | model                   | request-response | `LoadedPluginsModel::Roles` `loaded_plugins_model.hpp:83-91` + `TrustLevelRole` derive `loaded_plugins_model.cpp:86-87,131` | exact                                 |
| status chip + amber "Unsigned — requires consent" chip (QML)                | component               | request-response | `trustChip` `LoadedPluginsPage.qml:168-221` + `waylandCapabilityWarningChip` `SettingsPage.qml:208-245`                     | exact (two analogs)                   |
| `test_win_plugin_classification.cpp` (or extend `test_plugin_manifest.cpp`) | test                    | request-response | `test_plugin_manifest.cpp` + `test_plugin_verify_gate.cpp`                                                                  | exact                                 |
| WINPLG-01 ADR doc                                                           | config (doc)            | —                | `30-ADR-plugin-host-unification.md`                                                                                         | exact                                 |
| VERIF milestone-audit doc                                                   | config (doc)            | —                | `docs/plugin-event-parity.md` (Phase-34 audit style)                                                                        | role-match                            |
| **VERIFY-ONLY:** `VerifyVerdict` + tampered-refused-with-consent            | model + utility         | transform        | already at `plugin_verify_gate.hpp:41-46` / `plugin_catalog_model.cpp:907-927`                                              | self (don't reimplement)              |
| **VERIFY-ONLY:** consent persistence (QSettings)                            | utility                 | file-I/O         | already at `plugin_catalog_model.cpp:1063-1076` + sweep `:173`                                                              | self                                  |
| **VERIFY-ONLY:** SdPluginServer LocalHost bind                              | service                 | event-driven     | already at `sd_plugin_server.cpp:93,148`                                                                                    | self                                  |
| **VERIFY-ONLY:** VERIF-02 grep gates / loopback CI gate                     | config (CI)             | batch            | already at `ci.yml:67-83`                                                                                                   | self                                  |

______________________________________________________________________

## A1 OPEN QUESTION — RESOLVED with code-grounded recommendation

**Question:** Which UI surface carries win-only `.sdPlugin` plugins and must
host the WINPLG classification chip?

**RESEARCH's recommendation (A1):** `PluginCatalogModel` / `PluginStore.qml`.

**CORRECTION — the planner should use `LoadedPluginsModel` / `LoadedPluginsPage.qml`, NOT the catalog.** Code evidence:

1. **`PluginCatalogModel` is browse/install only and has NO trust/verdict role.**
   Its `Roles` enum (`plugin_catalog_model.hpp:146-163`) is `Uuid/Name/Version/ Author/Description/IconUrl/Category/Tags/Devices/Compatibility/SizeBytes/ Verified/Installed/Enabled/Source/StreamdockProductId/DownloadUrl` — there is
   **no per-row trust level** (`grep trustLevel src/app/src/plugin_catalog_model.hpp`
   returns only doc-comment hits at `:425,:456`). The catalog renders a *download
   tile* (`pluginTile` `PluginStore.qml:709-719`); it never shows a per-plugin
   security verdict, so it is the wrong place for a per-plugin classification chip.

1. **`LoadedPluginsModel` is the model that already carries per-plugin verdict** —
   it has `TrustLevelRole` (`loaded_plugins_model.hpp:91`) derived via
   `trustLevelOf()` (`loaded_plugins_model.cpp:131`), and its QML
   (`LoadedPluginsPage.qml:168-221`) already renders the per-plugin `trustChip`.
   The classification chip is the natural sibling here.

1. **DECISIVE: post-Phase-30, `.sdPlugin` plugins DO flow into `LoadedPluginsModel`.**
   `LoadedPluginsModel` is fed by an `IPluginHost*` (`loaded_plugins_model.cpp:113-125`,
   `setPluginHost`→`setPlugins(m_host->plugins())`). The host is now the
   `UnifiedPluginHost`, whose `plugins()` (`unified_plugin_host.cpp:93-117`)
   **merges the `.sdPlugin` inventory FIRST, then the Python inventory**:

   ```cpp
   // unified_plugin_host.cpp:96-102
   if (m_sdPluginHost) {
       auto sdPlugins = m_sdPluginHost->plugins();   // .sdPlugin first
       result.insert(result.end(), move(sdPlugins.begin()), move(sdPlugins.end()));
   }
   ```

   So the LoadedPluginsPage list contains BOTH `.sdPlugin` and Python plugins
   today. The Phase-27-era comment at `LoadedPluginsPage.qml:222-227` ("this row
   is a Python OOP-host plugin … the per-plugin Allow was a no-op for .sdPlugin")
   is **stale** with respect to the *list contents* (it is correct only about the
   removed Allow *action*). The Phase-27 no-op trap was that the **action** was
   wired to the Python host; the **list display** now aggregates both.

**Recommendation for the planner:** Add the WINPLG classification (`WinPluginClass`

- a derived `platformStatus` string) to **`LoadedPluginsModel`** as a new role
  (mirror `TrustLevelRole`/`trustLevelOf`), and render the status chip in
  **`LoadedPluginsPage.qml`** as a sibling of `trustChip`. To carry the
  classification through, either extend `plugins::PluginInfo`
  (`i_plugin_host.hpp:46-70`) with the class, or compute it in `LoadedPluginsModel`
  from the staged manifest+bundle at `setPlugins` time. **Live-drive the REAL
  surface** (install a WS-only win `.sdPlugin`, `scripts/ajazz-debug plugin.list`
- `qml.get` the chip) before declaring done — this is exactly the Phase-27
  live-verification lesson.

**Residual risk (flag in plan):** confirm at execute-time that the bundle dir /
manifest is reachable from `LoadedPluginsModel`'s data source so the PE-scan can
run; if not, classify at install/scan time in `PluginCatalogModel::installFromFile`
and stash the verdict where the loaded-model can read it (e.g. a small
`QHash<uuid,WinPluginClass>` cache, or `PluginInfo`). Either way the **chip
renders on LoadedPluginsPage**.

______________________________________________________________________

## Pattern Assignments

### `classifyWindowsPlugin()` — `plugin_manifest.{hpp,cpp}` (utility, transform) — NET-NEW

**Analog:** `manifestRunnableHere()` at `plugin_manifest.cpp:316-362` (free
function, `PluginManifest const&` + extra args, `[[nodiscard]] bool`, never
throws — mirror this exactly).

**Declaration idiom to copy** (`plugin_manifest.hpp:187,197`):

```cpp
[[nodiscard]] bool manifestRunnableHere(PluginManifest const& manifest,
                                        QString const& platform,
                                        QString const& appVer);
[[nodiscard]] QString currentPlatformString();
```

Add alongside:

```cpp
enum class WinPluginClass { WsOnlyIpc, VendorDll, NotWindowsOnly };
[[nodiscard]] WinPluginClass classifyWindowsPlugin(PluginManifest const& m,
                                                   QString const& bundleDir);
```

**Manifest fields already parsed** (`plugin_manifest.hpp:91-97`) — DO NOT add a parser:

```cpp
std::vector<PluginOsRequirement> os;  ///< OS array
QString codePath;     ///< CodePath
QString codePathWin;  ///< CodePathWin (Windows override)
QString codePathMac;  ///< CodePathMac (macOS override)
```

**Heuristic (from CONTEXT, locked):** CodePath/CodePathWin suffix `.exe`/`.dll`
OR a PE-magic file (`MZ` 0x4D 0x5A at offset 0; optional `PE\0\0` at e_lfanew)
anywhere in the bundle → `VendorDll`; else (suffix `.js`/`.html`/`.cjs` and no
PE) → `WsOnlyIpc`; non-windows-only → `NotWindowsOnly`. **Bounded reads** — reuse
the bounded-read discipline already used in `installFromFile`
(`plugin_catalog_model.cpp` `kMaxPluginDownloadBytes`, ~:843). Pure function, no
device I/O, no PE *loader* (locked anti-decision).

______________________________________________________________________

### `supportsCurrentPlatform()` — `plugin_manifest.cpp` (utility, transform) — NET-NEW

**Analog + exact insertion point:** the OS gate inside `manifestRunnableHere()`,
`plugin_manifest.cpp:319-350` (the LOCKED Linux-accept exception lives here):

```cpp
// plugin_manifest.cpp:326-350 — the OS gate + LOCKED linux-accept policy
if (!manifest.os.empty()) {
    bool osMatch = false;
    bool hasLinuxEntry = false;
    for (auto const& osReq : manifest.os) {
        if (osReq.platform == platform) { osMatch = true; break; }
        if (osReq.platform == QStringLiteral("linux")) hasLinuxEntry = true;
    }
    if (!osMatch) {
        // LOCKED ACCEPT — 18-RESEARCH.md Assumption A1 / Open Question 1.
        if (platform == QStringLiteral("linux") && !hasLinuxEntry) {
            // accept so far (fall through to MinimumVersion)
        } else {
            return false; // strict reject for non-Linux or explicit linux entry
        }
    }
}
```

**Wired at** `plugin_manager.cpp:278-279` (the spawn-eligibility gate):

```cpp
if (!manifestRunnableHere(*opt, currentPlatformString(),
                          QString::fromLatin1(kEmulatedSdVersion))) { /* skip */ }
```

**New work:** `supportsCurrentPlatform()` = `WsOnlyIpc → true regardless of declared OS`;
`VendorDll → (currentPlatform=="windows") || wineDetected`; `wineDetected` is
DEFERRED → **always false this phase** → chip "Requires Wine"/"Unsupported on this OS".
Hook it so a WS-only-IPC win plugin passes the gate at `plugin_manager.cpp:278`
even when `manifestRunnableHere` would otherwise reject the declared OS. Do NOT
weaken the existing strict reject for vendor-DLL.

______________________________________________________________________

### classification model role — `LoadedPluginsModel` (model, request-response) — NET-NEW

**Analog:** `TrustLevelRole` end-to-end in `LoadedPluginsModel`.

**Role enum idiom** (`loaded_plugins_model.hpp:83-91`):

```cpp
enum Roles {
    IdRole = Qt::UserRole + 1,
    NameRole, VersionRole, AuthorsRole, PermissionsRole,
    SignedRole, PublisherRole,
    TrustLevelRole,   ///< Derived enum string — the chip's data source
    // … add e.g. PlatformStatusRole / WinClassRole here
};
```

**data() + roleNames() + derive idiom** (`loaded_plugins_model.cpp:86-87,102,131`):

```cpp
case TrustLevelRole: return trustLevelOf(info);          // :86-87
{TrustLevelRole, "trustLevel"},                          // :102 (roleNames)
QString LoadedPluginsModel::trustLevelOf(PluginInfo const& info) { … }  // :131
```

Mirror exactly: add `PlatformStatusRole` → `"platformStatus"` in `roleNames()`,
a `case` in `data()`, and a static derive (`platformStatusOf(...)`) returning
`"native"`/`"wine"`/`"unsupported"` (or the chip-label strings directly).

______________________________________________________________________

### status chip + amber consent chip — `LoadedPluginsPage.qml` (component, request-response) — NET-NEW

**Primary analog:** `trustChip` `LoadedPluginsPage.qml:168-221` (same delegate,
add a sibling Rectangle). **objectName analog:** `waylandCapabilityWarningChip`
`SettingsPage.qml:208-245` (the Phase-34 amber chip — copy its `objectName` +
addressable-property idiom).

**trustChip idiom to mirror** (`LoadedPluginsPage.qml:168-188`):

```qml
Rectangle {
    id: trustChip
    visible: row.trustLevel !== "trusted"
    Layout.preferredHeight: 24
    Layout.preferredWidth: chipText.implicitWidth + Theme.spacingMd * 2
    radius: 12
    color: (row.trustLevel === "unsigned" || row.trustLevel === "tampered")
        ? Theme.chipBgError : Theme.chipBgWarning
    border.color: (row.trustLevel === "unsigned" || row.trustLevel === "tampered")
        ? Theme.chipBorderError : Theme.chipBorderWarning
    border.width: 1
    // … Text{chipText} + ToolTip + MouseArea{hoverEnabled}
}
```

**Phase-34 amber-chip + objectName + addressable-property idiom to ALSO copy**
(`SettingsPage.qml:208-238`):

```qml
Rectangle {
    id: waylandCapabilityWarningChip
    objectName: "waylandCapabilityWarningChip"         // <-- MANDATORY (VERIF-01)
    visible: !ProfileController.foregroundCapabilityAvailable
    property string warningText: ProfileController.foregroundCapabilityWarning  // headless-readable
    color: Theme.chipBgWarning; border.color: Theme.chipBorderWarning; border.width: 1
    radius: 12
    Text { id: capabilityChipText; text: qsTr("Limited on this desktop"); color: Theme.chipFgWarning
           font.pixelSize: Theme.fontXs; font.weight: Font.DemiBold }
    ToolTip.text: waylandCapabilityWarningChip.warningText
}
```

**GAP to fix (VERIF-01):** `grep -c objectName LoadedPluginsPage.qml` = **0** — the
existing `trustChip` has NO objectName. The NEW chip(s) MUST set `objectName`
(e.g. `"platformStatusChip"`, `"unsignedConsentChip"`) AND expose the label as a
readable property (mirror `warningText`) so `scripts/ajazz-debug qml.get` returns
the label headlessly. Consider adding `objectName` to the existing `trustChip`
while here. Chip labels (locked): "Runs natively" / "Requires Wine" /
"Unsupported on this OS"; amber "Unsigned — requires consent".
**Harness gap (CLAUDE.md):** `qml.invoke toggle` fires `onClicked` but NOT
`Switch.toggled` — use plain display chips (no Switch); any action needs a
dedicated `Q_INVOKABLE`.

______________________________________________________________________

### `test_win_plugin_classification.cpp` (test) — NET-NEW

**Analogs:** `test_plugin_manifest.cpp` (manifest-fixture style) +
`test_plugin_verify_gate.cpp` (verdict-mapping style).

**TEST_CASE naming idiom (ASCII-only, tagged — `[plugin-manifest]`):**

```cpp
// test_plugin_manifest.cpp:68 / test_plugin_verify_gate.cpp:179
TEST_CASE("PluginManifestTest rejects_macOnly_onLinux", "[plugin-manifest]") { … }
TEST_CASE("PluginVerifyGate verdictToTrustLevel: Refused maps to tampered",
          "[plugin-verify-gate]") { … }
```

**Cases (from Validation Architecture):** CodePath `.html`/`.js` + no PE →
`WsOnlyIpc`; CodePath `.exe`/`.dll` OR PE-magic present → `VendorDll`;
`supportsCurrentPlatform()` true for WsOnlyIpc on linux; OS=["windows"] WS-only
manifest accepted on linux via the gate. Filter: `ctest --preset linux-release -R "plugin-manifest|win-plugin-classification"` (use `-R`, NOT `--test-regex`).

______________________________________________________________________

### WINPLG-01 ADR doc — NET-NEW deliverable

**Analog:** `.planning/phases/30-plugin-host-modular-foundation/30-ADR-plugin-host-unification.md`.
**Structure to copy:** `# ADR: <title>` → `**Status:** Accepted (date)` → `## 1. Status` → `## 2. Context` (state-before) → decision → consequences. Document: the
heuristic (CodePath suffix + PE-magic), the WS-only-IPC-runs-native decision, and
the **explicit Wine-launch DEFERRAL** with the no-hardware rationale + the
never-bundle-Wine anti-decision. Place at
`.planning/phases/35-…/35-ADR-windows-plugin-classification.md`.

______________________________________________________________________

### VERIF milestone-audit doc — NET-NEW deliverable

**Analog:** `docs/plugin-event-parity.md` (Phase-34 audit-doc style, 30 KB,
table-driven with honest "done / partial / deferred" status). Enumerate the
grep-gate results verbatim (see Shared Patterns / VERIF-02 below), the
objectName-coverage scan over new v2.0 QML, and the **honest** per-phase live-
evidence vs deferred-HUMAN-UAT reconciliation. **NEVER fabricate screenshots** —
Phases 32/33/34 each have a deferred `*-HUMAN-UAT.md` walk; state that plainly.

______________________________________________________________________

## VERIFY-ONLY (frame as verify + test-lock + live-drive — do NOT reimplement)

### VerifyVerdict + tampered-refused-even-with-consent (PLGSEC-01)

**Already implemented — confirmed this session.** `enum class VerifyVerdict { Trusted, SelfSigned, Unsigned, Refused }` at `plugin_verify_gate.hpp:41-46`;
`Refused == tampered` mapped by `verdictToTrustLevel` (`:89`). The CR-01
invariant is live at `plugin_catalog_model.cpp:907-927`:

```cpp
// plugin_catalog_model.cpp:907 — Refused quarantined even with consent
if (vout.verdict == VerifyVerdict::Refused) { /* remove staging, abort install */ }
```

**Existing tests (verify still green, do NOT regress):**

- `test_plugin_install_from_file.cpp:581` "PluginInstallFromFile tampered plugin refused even with consent"
- `:854` "PluginCatalog allowUnsignedPlugins=true tampered STILL refused (CR-01)"
- `:1004` "PluginCatalog per-plugin allow does not promote a tampered plugin (CR-01)"
- `test_plugin_verify_gate.cpp:193,265,305` (verdict mapping + tampered→Refused + unsigned→Unsigned)

**Plan framing:** verify the 4-way enum is unchanged; do NOT add/rename a verdict.

### Consent persistence — QSettings `plugins/allowed/<uuid>` (PLGSEC-02)

**Already implemented.** Write at `plugin_catalog_model.cpp:1063-1076`:

```cpp
// FIX-CONSENT (:1070-1072)
if (userConfirmedUnsigned && vout.verdict != VerifyVerdict::Trusted) {
    QSettings settings;  // resolves to AJAZZ_VENDOR_NAME / AJAZZ_PRODUCT_NAME scope
    settings.setValue(QStringLiteral("plugins/allowed/") + candidateUuid, true);
}
```

Read in launch-sweep at `:173` via `perPluginAllowed()` (`:61-66`); org/app scope
set in `main.cpp:69-71` (`setOrganizationName(AJAZZ_VENDOR_NAME)` /
`setOrganizationDomain` / `setApplicationName(AJAZZ_PRODUCT_NAME)`) → bare
`QSettings` is registry-safe. **Plan framing:** verify the restart round-trip live
(`grant → relaunch → plugin.list shows loaded, no re-prompt`); **A3:** grep
`tests/unit` for `plugins/allowed`/`perPluginAllowed` and add a two-instance
persistence test ONLY if absent.

### SdPluginServer LocalHost bind (PLGSEC-03)

**Already enforced.** `sd_plugin_server.cpp:93` `m_server->listen(QHostAddress::LocalHost, port)`;
`:148` `return QHostAddress(QHostAddress::LocalHost)`. The only `QHostAddress::Any`
occurrence is the explanatory comment at `:90`. **Plan framing:** verify via
`ajazz-debug state`/`ping`; confirm a code-level assert in `test_sd_plugin_server.cpp`.

### VERIF-02 grep gates + loopback CI gate

**Already present + comment-aware.** `ci.yml:67-83`:

```yaml
- name: Enforce loopback-only + SIGPIPE invariants (Phase 30)
  if: runner.os == 'Linux'
  run: |
    if grep -rn 'QHostAddress::Any' src/ | grep -vP ':[0-9]+:\s*(//|[*])'; then exit 1; fi
    if ! grep -n 'SIGPIPE' src/app/src/main.cpp | grep -q 'SIG_IGN'; then exit 1; fi
```

Pair with the `hid_open` gate idiom at `ci.yml:55-64`. **Plan framing:** the gate
exists — confirm/extend; add the milestone greps as scripted checks (see below).

______________________________________________________________________

## Shared Patterns

### Free-function helper idiom (manifest tier)

**Source:** `plugin_manifest.{hpp,cpp}` — `[[nodiscard]]`, `PluginManifest const&`
arg, never throws, namespaced `ajazz::app`. **Apply to:** `classifyWindowsPlugin`,
`supportsCurrentPlatform`.

### Model-role → derived-string → QML chip

**Source:** `LoadedPluginsModel` `TrustLevelRole`/`trustLevelOf` →
`LoadedPluginsPage.qml` `trustChip`. **Apply to:** the new platform/classification
chip end-to-end.

### objectName + readable-property on every display chip (VERIF-01)

**Source:** `SettingsPage.qml:209-218` (`objectName` + `property string warningText`).
**Apply to:** every new chip/control — headless `qml.get` must return the label.

### Comment-aware CI grep gate

**Source:** `ci.yml:67-83` + `:55-64`. **Apply to:** the VERIF-02 milestone gates,
run verbatim as scripted checks (current results = comment-only matches → PASS):

```bash
grep -rn mirajazz src/core/ src/app/src/plugin_manager.cpp src/app/src/plugin_device_bridge.cpp   # expect 0
grep -rn '#include.*nlohmann' src/core/include/                                                     # expect 0 (word-in-comment is fine)
grep -rn 'makeAkp05\|makeAkp03\|makeAkp153' src/                                                    # expect 0 (prefer symbols over akp05.cpp filename)
grep -rn 'QHostAddress::Any' src/ | grep -vP ':[0-9]+:\s*(//|[*])'                                  # expect 0
```

### ASCII-only tagged TEST_CASE + `-R` filter

**Source:** `test_plugin_manifest.cpp:68`, `test_plugin_verify_gate.cpp:179`.
**Apply to:** all new tests. Filter with `-R` (NOT `--test-regex`); ASCII names only.

______________________________________________________________________

## No Analog Found

None. Every required pattern has a concrete in-tree analog (the phase is ~70%
verification of already-shipped code + ~30% net-new that mirrors existing idioms).

______________________________________________________________________

## Metadata

**Analog search scope:** `src/app/src/`, `src/app/qml/`,
`src/plugins/include/ajazz/plugins/`, `tests/unit/`, `.github/workflows/`,
`docs/`, `.planning/phases/30-*`.
**Files scanned:** ~22 (greps) + 6 targeted reads.
**Pattern extraction date:** 2026-06-08
