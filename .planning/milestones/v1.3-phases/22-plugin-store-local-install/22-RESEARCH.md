# Phase 22: Plugin Store / Local Install - Research

**Researched:** 2026-05-23
**Domain:** Qt 6 / QML / C++20 — local `.sdPlugin` install via a host-owned catalog, Ed25519 signature gate, no phone-home
**Confidence:** HIGH (every claim below is grounded in a file:line read this session)

\<user_constraints>

## User Constraints (from CONTEXT.md)

### Locked Decisions

**Reuse + integrate (do NOT rebuild):**

- Install/extract: reuse `sdplugin_extractor` (`extractSdPluginArchive`, zip-slip-guarded).
- Signature gate: reuse the **Ed25519 `manifest_signer`** (`ManifestSignerConfig`, `verifyManifest`) — the same SEC-003 #51 verifier the Python host uses. An unsigned or tampered manifest is **rejected/quarantined**, not loaded.
- Catalog/UI/state: reuse `PluginStore.qml`, `plugin_catalog_model`, `plugin_catalog_proxy_model`, `loaded_plugins_model`; an installed local package appears as an "Installed" catalog row.
- Discovery/lifecycle: the Phase-18 `PluginManager` consumes the installed package (`depends_on: [18]`).

**No phone-home (PLUGIN-14 anti-feature):**

- The online `streamdock_catalog_fetcher` must NOT auto-fetch on launch. Make the online source **opt-in** (a user-enabled setting / explicit "refresh online catalog" action); the default is **local install only** (offline snapshot is fine — it ships no user data outbound). No outbound request without explicit user action.

**Trust model:**

- The signature gate is the trust boundary the vendor lacked (vendor extracts + trusts ZIPs with no verification — §6). A local install of an UNSIGNED package may be allowed only with an explicit user confirmation (or refused outright — planner picks the stricter UX that still lets a developer sideload their own plugin); a TAMPERED signature is always refused.

### Claude's Discretion

- "Install from file…" UX entry point in `PluginStore.qml` + the controller method.
- Unsigned-package policy: hard-refuse vs explicit-confirm-with-warning (favor the safer default that still permits developer sideload).
- Where the verify runs (at install time, at load time, or both) — at least once before trust.

### Deferred Ideas (OUT OF SCOPE)

- Auxiliary surfaces → Phase 23. Family coverage → Phase 24. Hardware verify → Phase 25.
- An online plugin marketplace with curation/payments (`productType`/`price` fields) — host-owned local catalog only.
  \</user_constraints>

\<phase_requirements>

## Phase Requirements

| ID                     | Description                                                                                                                                                                                                                                                                                                  | Research Support                                                                                                                                                                                                                                                                            |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PLUGIN-14              | Plugins install from a local `.sdPlugin`/`.zip` into `installedPlugins/` via a **host-owned** catalog with **no phone-home**; a **signature/manifest-verification gate** (reuse `ManifestSignerConfig`) must pass before a plugin is trusted (sdk §6 — vendor's no-signature behaviour is the gap we close). | Three concrete gaps identified (local-file install path MISSING; verify gate NOT wired into install; auto-fetch on launch PRESENT). Reuse surfaces confirmed: `extractSdPluginArchive`, `verifyManifest`, `PluginCatalogModel`, `LoadedPluginsModel`. See Standard Stack + Common Pitfalls. |
| \</phase_requirements> |                                                                                                                                                                                                                                                                                                              |                                                                                                                                                                                                                                                                                             |

## Summary

Phase 22 is **integration + an honesty gate**, not greenfield. Three of the four building blocks already exist and are tested: the zip-slip-guarded extractor (`extractSdPluginArchive`), the Ed25519 verifier (`verifyManifest` in `ajazz_plugins`), and the store UI + catalog model (`PluginStore.qml` + `PluginCatalogModel`). The work is wiring them together and closing two honesty gaps.

The investigation found three concrete deltas the planner must address:

1. **No local-file install path exists.** `PluginCatalogModel::install(uuid)` is *catalog-row-driven*: it requires a `CatalogEntry` with an HTTPS `downloadUrl`, issues a network GET, then extracts. There is no "install from a file on disk" entry point — neither a controller method nor a QML `FileDialog`. `PluginStore.qml` only has per-tile Install/Uninstall buttons bound to catalog rows. **This is the primary new code in Phase 22.** `[VERIFIED: src/app/src/plugin_catalog_model.cpp:429-461, src/app/qml/PluginStore.qml:703-717]`

1. **The signature gate is NOT wired into the install path at all.** `install()` does: HTTPS GET → `validateDownloadedArchive` (size cap + ZIP magic only) → write to disk → `extractSdPluginArchive` → mark installed. **No call to `verifyManifest` anywhere.** The Ed25519 verifier exists and is unit-tested, but it is currently only ever invoked by the Python OOP plugin host's discovery path, never by the app's `.sdPlugin` install path. `LoadedPluginsModel` *surfaces* a trust level (`trusted`/`self-signed`/`unsigned`) but only reflects what the host already computed — it does not gate install. **Wiring `verifyManifest` into the install flow is the trust-boundary work.** `[VERIFIED: src/app/src/plugin_catalog_model.cpp:415-603 — no `verifyManifest`/`ManifestSigner` reference; src/app/src/loaded_plugins_model.hpp:45-92]`

1. **Phone-home fires on launch today.** `PluginCatalogModel`'s constructor calls `reload()` (line 157), which calls `m_streamdockFetcher->refresh()` (line 269) and `m_opendeckFetcher->refresh()` (line 272). `refresh()` performs a live HTTP POST to `https://space.key123.vip/...` **unless** the URL override or `ACC_STREAMDOCK_CATALOG_URL` env var equals the literal string `"disabled"`. The default URL is non-empty, so **the default behaviour is an outbound request on every app launch** — exactly the anti-feature. The minimal fix is to flip the *default* to offline (no live fetch unless an explicit opt-in setting is set), while leaving the offline snapshot/cached/bundled-fallback path intact. `[VERIFIED: src/app/src/plugin_catalog_model.cpp:99-158,245-273; src/app/src/streamdock_catalog_fetcher.cpp:49,142-167,502-540]`

**Primary recommendation:** Add a `installFromFile(QString localPath)` Q_INVOKABLE on `PluginCatalogModel` (or a small new install controller) that: validates the archive (existing `validateDownloadedArchive`) → extracts to a staging dir → runs `verifyManifest` on the extracted `manifest.json` → on `valid==true` promotes into `installedPlugins/` and marks installed; on `valid==false` (unsigned/tampered) refuses or quarantines per the chosen UX. Add a "Install from file…" `FileDialog` in `PluginStore.qml`. Separately, gate `streamdock_catalog_fetcher`'s live fetch behind an opt-in setting so the default launch makes zero outbound requests.

## Architectural Responsibility Map

| Capability                              | Primary Tier                                              | Secondary Tier          | Rationale                                                                                          |
| --------------------------------------- | --------------------------------------------------------- | ----------------------- | -------------------------------------------------------------------------------------------------- |
| Pick a local `.sdPlugin` file           | QML (FileDialog)                                          | App C++ controller      | UI affordance; QML `FileDialog` returns a `file://` URL the controller consumes                    |
| Validate archive (size + ZIP magic)     | App C++ (`PluginCatalogModel::validateDownloadedArchive`) | —                       | Already exists, static + pure, reuse verbatim                                                      |
| Extract `.sdPlugin` (zip-slip guarded)  | App C++ (`sdplugin_extractor`)                            | —                       | Existing tested extractor; zip-slip guard is the security boundary                                 |
| Verify Ed25519 manifest signature       | `ajazz_plugins` lib (`verifyManifest`)                    | App C++ (caller)        | COD-031: signer lives in `ajazz_plugins`; app links it (PRIVATE) — see Pitfall 1 on the build flag |
| Decide trusted / self-signed / unsigned | App C++ (`LoadedPluginsModel::trustLevelOf`)              | —                       | Existing collapse rule; reuse the same enum for the install gate verdict                           |
| Catalog row state (installed/enabled)   | App C++ (`PluginCatalogModel`)                            | QML (`PluginStore.qml`) | Existing model + UI; an installed local pkg becomes an "Installed" row                             |
| Discover + spawn installed plugin       | App C++ Phase-18 `PluginManager`                          | —                       | Out of scope here; Phase 22 only lands the files into `installedPlugins/`                          |
| Online catalog fetch (opt-in)           | App C++ (`streamdock_catalog_fetcher`)                    | QML toggle              | Must become opt-in/off-by-default; offline snapshot stays                                          |

## Standard Stack

### Core (all already present — reuse, do not add deps)

| Component                                                                                                            | Where                                                      | Purpose                                                       | Why Standard                                                                                    |
| -------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------- | ------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `extractSdPluginArchive(archivePath, destDir, targetSubdir)`                                                         | `src/app/src/sdplugin_extractor.hpp:39`                    | Atomic zip-slip-guarded extract of a `.sdPlugin`/zip          | Existing, tested (`tests/unit/test_sdplugin_extractor.cpp`); Qt6 `QZipReader` under the hood    |
| `verifyManifest(manifestPath, ManifestSignerConfig)` → `ManifestVerifyResult{valid, publisherKeyB64, publisherName}` | `src/plugins/include/ajazz/plugins/manifest_signer.hpp:98` | Ed25519 signature verify (fail-closed)                        | SEC-003 #51; exit-code-0-from-Python-verifier contract; CWE-426-hardened interpreter resolution |
| `PluginCatalogModel::validateDownloadedArchive(QByteArray)` → error string                                           | `src/app/src/plugin_catalog_model.hpp:260`                 | Size cap + ZIP-magic pre-flight                               | Static + pure, unit-testable, already used by the network install path                          |
| `PluginCatalogModel` (singleton)                                                                                     | `src/app/src/plugin_catalog_model.{hpp,cpp}`               | Catalog rows + install/enabled state side-map                 | Existing QML singleton; an installed local pkg becomes an "Installed" row                       |
| `LoadedPluginsModel::trustLevelOf(PluginInfo)` → `"trusted"`/`"self-signed"`/`"unsigned"`                            | `src/app/src/loaded_plugins_model.hpp:152`                 | Collapse `(signed_, publisher)` → trust enum                  | Reuse the exact same enum strings for the install-gate verdict so UI is consistent              |
| `StreamdockCatalogFetcher` / `OpenDeckCatalogFetcher`                                                                | `src/app/src/streamdock_catalog_fetcher.{hpp,cpp}`         | Online mirror with cached + bundled-fallback layers           | Already supports a `"disabled"` override; offline snapshot path is what we keep                 |
| `PluginStore.qml`                                                                                                    | `src/app/qml/PluginStore.qml`                              | Store grid + All/Installed/Streamdock/OpenDeck/Community tabs | Existing UI; add a "Install from file…" affordance                                              |

### `ManifestSignerConfig` fields (the verify contract)

```cpp
// src/plugins/include/ajazz/plugins/manifest_signer.hpp:69-83
struct ManifestSignerConfig {
    std::string pythonExecutable{"python3"};         // resolved against vetted dirs, NOT $PATH (CWE-426)
    std::filesystem::path verifierScript;            // scripts/sign-plugin-manifest.py
    std::filesystem::path trustedPublishersFile;     // resources/trusted_publishers.json (must be 0600/user-owned)
};
```

The app already has the build-time paths wired for the python-host build:
`AJAZZ_PLUGIN_VERIFIER_SCRIPT` and `AJAZZ_PLUGIN_TRUST_ROOTS` compile definitions
(`src/app/CMakeLists.txt:448-449`). The planner should reuse these to construct the config —
do NOT re-derive paths. `[VERIFIED: src/app/CMakeLists.txt:437-449]`

### The verify CLI contract

`verifyManifest` spawns `<python> scripts/sign-plugin-manifest.py verify --manifest <path>`;
**exit 0 = valid signature, exit 1 = bad/unsigned**. Canonicalisation: parse JSON → remove
`Ajazz.Signing.Ed25519Signature` → re-emit with `ensure_ascii=False, sort_keys=True, separators=(",",":")` → Ed25519-verify against the embedded `Ed25519PublicKey`. The signature
covers the embedded public key, so a publisher cannot post-hoc swap keys.
`[VERIFIED: scripts/sign-plugin-manifest.py:1-190]`

`ManifestVerifyResult` semantics (`manifest_signer.hpp:57-61`):

- `valid==false` → unsigned OR tampered → **REFUSE** (this is the always-refuse case for tampered).
- `valid==true && publisherName empty` → self-signed (key not in trust roots) → **the developer-sideload case** (allow with explicit confirm).
- `valid==true && publisherName set` → trusted (key in `trusted_publishers.json`) → allow.

### No new packages

This phase adds **zero** external dependencies. Everything is in-tree Qt6 + the existing
`ajazz_plugins` static lib. (The signer deliberately subprocess-calls the Python verifier
rather than linking libsodium — see `manifest_signer.hpp:19-31`.)

**Installation:** none — `## Package Legitimacy Audit` is N/A (no external packages installed).

## Architecture Patterns

### System Architecture Diagram

```
  User clicks "Install from file…"           User opens Plugin Store
            │                                          │
            ▼                                          ▼
   QML FileDialog (file:// URL)            PluginCatalogModel ctor → reload()
            │                                          │
            ▼                                          ▼  (TODAY: auto-fetch — MUST become opt-in)
   PluginCatalogModel::installFromFile()    StreamdockCatalogFetcher::refresh()
            │  (NEW Q_INVOKABLE)                       │
            ▼                                  ┌───────┴────────┐
   validateDownloadedArchive() ── reject ──►   cached/bundled    live HTTP POST
            │  (size + ZIP magic)              (offline, OK)     (DEFAULT-OFF after fix)
            ▼
   extractSdPluginArchive() → staging dir ── fail ──► refuse
            │  (zip-slip guarded)
            ▼
   verifyManifest(staging/manifest.json, ManifestSignerConfig)
            │
   ┌────────┼─────────────────────┐
   ▼        ▼                      ▼
 valid    valid+self-signed     valid==false
 +trusted  (confirm prompt)     (unsigned OR tampered)
   │        │                      │
   ▼        ▼                      ▼
 promote to installedPlugins/    REFUSE / quarantine
   │                              (never promoted, never loaded)
   ▼
 mark installed → dataChanged → "Installed" row
   │
   ▼
 Phase-18 PluginManager discovers on next scan (out of scope here)
```

### Recommended new code shape

```
src/app/src/
├── plugin_catalog_model.{hpp,cpp}   # ADD: installFromFile(QString) Q_INVOKABLE + verify gate
│                                     #      reuse validateDownloadedArchive + extractSdPluginArchive
└── (verify config helper)           # construct ManifestSignerConfig from AJAZZ_PLUGIN_* defs

src/app/qml/
└── PluginStore.qml                  # ADD: "Install from file…" button + FileDialog
                                     # ADD: opt-in toggle for online catalog refresh
```

### Pattern 1: Install controller method (verify-before-trust)

**What:** A new `Q_INVOKABLE bool installFromFile(QString const& localPath)` that extracts to a
staging dir, verifies, and only promotes on a passing verdict. Keeps the verify *before* the
package ever lands in `installedPlugins/` (so an unsigned/tampered package is never discoverable).

**When to use:** This is the PLUGIN-14 entry point. Mirror the existing async `installFinished(uuid, success, error)` signal so the QML tile binding is consistent with the network install path.

**Example (shape — verify against the existing `install()` for the disk/dataChanged plumbing):**

```cpp
// Source: pattern derived from src/app/src/plugin_catalog_model.cpp:429-603 (existing install path)
bool PluginCatalogModel::installFromFile(QString const& localPath) {
    QByteArray const body = readAllCapped(localPath);              // reuse size cap
    if (auto err = validateDownloadedArchive(body); !err.isEmpty())// reuse
        { emit installFinished(localPath, false, err); return false; }
    // extract to a *staging* dir (NOT installedPlugins/ yet)
    if (!extractSdPluginArchive(localPath, stagingDir, subdir))
        { emit installFinished(localPath, false, tr("extract failed")); return false; }
    auto const r = verifyManifest(stagingDir/subdir/"manifest.json", makeSignerConfig());
    if (!r.valid) {                       // unsigned OR tampered
        // tampered: always refuse. unsigned: refuse or confirm-then-allow per chosen UX.
        quarantineOrRefuse(stagingDir);
        emit installFinished(localPath, false, tr("signature verification failed"));
        return false;
    }
    promoteToInstalledPlugins(stagingDir);  // atomic rename into installedPlugins/
    // mark installed, emit dataChanged + installedCountChanged + installFinished(ok)
}
```

### Pattern 2: Opt-in online fetch (kill the launch phone-home)

**What:** The minimal change is to make `streamdock_catalog_fetcher`'s **live** fetch off by
default. Two viable shapes (planner's discretion):

- **(a) Settings-gated:** read a `QSettings` "online catalog enabled" flag in `reload()`; only call `refresh()` (or only let `refresh()` do the live step) when it's true. The Streamdock/OpenDeck tabs then show the cached/bundled snapshot until the user opts in.
- **(b) Invert the default sentinel:** treat the *absence* of an explicit opt-in as `"disabled"` (the fetcher already short-circuits the live POST when the override == `"disabled"`, keeping cached/fallback). The existing `"refresh online catalog"` button (`PluginStore.qml:315,449` already call `reload()`) becomes the explicit user action.

**When to use:** Required by the anti-feature decision. Favor (a) — an explicit setting is clearer than a sentinel string and is easy to test with a `QSettings` override.

**Critical:** the offline path (`loadFromCache()` → `loadBundledFallback()`) makes **zero** outbound requests and must keep working — it's how the Store tabs stay populated offline. Only the `fetchPage()` HTTP POST is the phone-home.

### Anti-Patterns to Avoid

- **Verifying after promotion.** Do NOT extract straight into `installedPlugins/` and verify afterwards — Phase-18 discovery could pick it up between the two steps. Verify in a staging dir, promote only on pass.
- **Aligning the verify to the catalog `verified` bool.** `CatalogEntry::verified` (line 66) is a *catalogue Sigstore-bundle hint* for online rows, NOT the manifest signature verdict. Local install trust comes from `verifyManifest`, not from `verified`.
- **Treating `validateDownloadedArchive` as the security gate.** It only checks size + ZIP magic (`plugin_catalog_model.cpp:415-427`). It is a pre-flight, not the trust boundary. The trust boundary is `verifyManifest` + the zip-slip guard.
- **Re-deriving the verifier/trust-roots paths.** Use the `AJAZZ_PLUGIN_VERIFIER_SCRIPT` / `AJAZZ_PLUGIN_TRUST_ROOTS` compile defs already in `src/app/CMakeLists.txt`.

## Don't Hand-Roll

| Problem                              | Don't Build                | Use Instead                                                       | Why                                                                                                                            |
| ------------------------------------ | -------------------------- | ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| Ed25519 signature verify             | A C++ libsodium binding    | `verifyManifest` (subprocess to Python)                           | SEC-003 #51 already did this; canonicalisation must match the signer byte-for-byte; CWE-426 interpreter hardening already done |
| Zip extraction with traversal safety | A manual `QZipReader` loop | `extractSdPluginArchive`                                          | Zip-slip guard + atomic tmp-dir+rename already tested (incl. the Qt 6.11 `x/../../../e` passthrough — see project memory)      |
| Archive pre-flight (size/magic)      | A new validator            | `validateDownloadedArchive`                                       | Static, pure, tested; reuse for the local-file path too                                                                        |
| Trust-level UI string                | A new enum                 | `LoadedPluginsModel::trustLevelOf` semantics                      | Same `trusted`/`self-signed`/`unsigned` vocabulary keeps UX consistent                                                         |
| Offline catalog fallback             | A new local-only model     | `StreamdockCatalogFetcher::loadBundledFallback` / `loadFromCache` | Already there; the no-network path is exactly the "default offline" behaviour we want                                          |

**Key insight:** Phase 22 is ~80% wiring. The only genuinely new logic is (1) the local-file
entry point + staging→verify→promote sequencing, and (2) flipping the online-fetch default.
Everything that touches crypto, zip safety, or trust vocabulary already exists and is tested.

## Common Pitfalls

### Pitfall 1: The signer is link-gated behind `AJAZZ_BUILD_PYTHON_HOST`

**What goes wrong:** The app links `ajazz::plugins` (and gets `verifyManifest`) **only inside
`if(AJAZZ_BUILD_PYTHON_HOST)`** (`src/app/CMakeLists.txt:437-438`). The option defaults `ON`
(`CMakeLists.txt:34`), but some CI matrices build with `-DAJAZZ_BUILD_PYTHON_HOST=OFF`
(`CMakeLists.txt:31`). In that configuration the app cannot call `verifyManifest`, and the
`AJAZZ_PLUGIN_VERIFIER_SCRIPT`/`AJAZZ_PLUGIN_TRUST_ROOTS` defs are absent.

**Why it happens:** The verifier was built for the OOP python host and inherited its build flag.

**How to avoid:** The planner must decide the `OFF` behaviour explicitly: either (a) **fail the
local install closed** ("signature verification unavailable in this build") so the trust gate is
never silently bypassed, or (b) move `manifest_signer` link + the verifier defs out of the
`AJAZZ_BUILD_PYTHON_HOST` guard so the signer is always available (the signer subprocess-calls
python at runtime regardless — it needs python3 present, not the python *host*). Option (b) is
cleaner but is a CMake change that must land on all three platforms. **Fail-closed is mandatory:
never let a `-OFF` build install unsigned packages silently.** `[VERIFIED: CMakeLists.txt:29-34, src/app/CMakeLists.txt:437-449]`

**Warning signs:** A green local install on a `-DAJAZZ_BUILD_PYTHON_HOST=OFF` build with no verify call in the logs.

### Pitfall 2: Auto-fetch hides in the constructor, not a startup hook

**What goes wrong:** Removing a `Component.onCompleted: reload()` from QML would NOT stop the
phone-home, because `reload()` (→ `refresh()` → live POST) is called from the
**`PluginCatalogModel` constructor** (`plugin_catalog_model.cpp:157`). The singleton is
instantiated as soon as QML imports it.

**How to avoid:** Gate the *live* step inside `refresh()`/`reload()` on the opt-in flag, OR
don't call `refresh()` from the constructor at all. Keep the synchronous cached/fallback
`emitSnapshot` so the tabs still populate offline.

**Warning signs:** A packet-capture / network spy shows a POST to `space.key123.vip` within the
first second of launch with no user interaction. The default endpoint is
`https://space.key123.vip/interface/user/productInfo/list`
`[VERIFIED: src/app/src/streamdock_catalog_fetcher.cpp:49]`.

### Pitfall 3: `install()` marks installed BEFORE any verify — and there's a second sweep

**What goes wrong:** The existing network `install()` sets `installed=true` immediately after a
successful extract (`plugin_catalog_model.cpp:586-594`) with no verify. Separately, the
constructor runs `extractStandalonePluginArchives(userPluginsDir())`
(`plugin_catalog_model.cpp:110`) which sweeps any leftover `.sdPlugin` *files* into extracted
dirs on **every launch** — also with no verify. If Phase 22 only adds verify to the new
local-file path, the network path and the launch sweep remain unverified back doors.

**How to avoid:** The planner must decide the scope of the gate. PLUGIN-14 says "a signature
gate must pass before a plugin is **trusted**." At minimum the new local-file path verifies; the
planner should also state explicitly whether the existing network `install()` and the launch
sweep get the same gate (recommended: yes, or document why not). The Phase-18 `PluginManager`
discovery is the other place trust is computed — coordinate so verify happens at least once
before load.

**Warning signs:** A plugin in `installedPlugins/` that was never run through `verifyManifest`.

### Pitfall 4: `trusted_publishers.json` ships a placeholder key

**What goes wrong:** `resources/trusted_publishers.json` currently contains
`REPLACE_WITH_AIACOS_KEY_44_CHARS_BASE64_PLACEHOLDER==` — a placeholder, not a real key. So
**every** validly-signed plugin currently resolves as `self-signed` (valid==true,
publisherName empty), never `trusted`. `[VERIFIED: resources/trusted_publishers.json]`

**How to avoid:** This is fine for Phase 22's *gate* (self-signed still passes `valid==true`), but
the planner/UX must not promise a "trusted publisher" badge that can never appear with the
placeholder. The unsigned-vs-self-signed-vs-trusted UX should degrade gracefully:
self-signed + explicit-confirm is the realistic developer-sideload path today.

### Pitfall 5: ASCII-only test names + `-R` filter (project hard rules)

**What goes wrong:** Catch2 `TEST_CASE` names with `—`/`→` get mangled through the Win32 CMD
codepage; the ctest filter is `--tests-regex`/`-R` (not `--test-regex`). `[CITED: CLAUDE.md]`

**How to avoid:** ASCII-only tags/names (`[plugin-store]`, `signed -> installs`); run
`ctest --preset linux-release`.

## Code Examples

### Building signed / unsigned / tampered `.sdPlugin` fixtures (reuse the proven recipe)

The `manifest_signer` test already demonstrates the full sign/verify/tamper recipe by shelling
out to the Python CLI; the extractor test demonstrates building `.sdPlugin` zips with
`QZipWriter` (and hostile zips with Python for zip-slip cases).

```cpp
// Source: tests/unit/test_manifest_signer.cpp:194-260 (signed-passes / tampered-fails / unsigned-fails)
// 1. keygen:   python3 sign-plugin-manifest.py keygen --out-dir <keys>
// 2. sign:     python3 sign-plugin-manifest.py sign --manifest <m> --priv-key <keys/priv.pem>
// 3. verify:   verifyManifest(<m>, makeConfig())  → ManifestVerifyResult{valid=true,...}
// tampered:    flip a byte in the manifest AFTER signing → verifyManifest → valid==false
// unsigned:    a manifest with no Ajazz.Signing block → verifyManifest → valid==false (fail-closed)
```

```cpp
// Source: tests/unit/test_sdplugin_extractor.cpp:42-57 (build a synthetic .sdPlugin zip)
QZipWriter zip(archivePath);                       // Qt6 private header: Qt6::GuiPrivate / QtZlib
REQUIRE(zip.status() == QZipWriter::NoError);
zip.addFile("com.example.sdPlugin/manifest.json", manifestBytes);
zip.close();                                       // close() BEFORE QZipReader on Win32 (lock)
```

**Fixture matrix for Phase 22 tests (hardware-free + network-free):**

- `signed-trusted` → (only achievable if trust roots carry the test key) installs as trusted.
- `signed-selfsigned` → installs after explicit confirm (or refused, per chosen policy).
- `tampered` → always refused, never lands in `installedPlugins/`.
- `unsigned` → refused/quarantined per policy.
- `no-network-on-launch` → spy/override asserts no live POST (see Validation Architecture).

### Asserting no network on launch

The fetcher already exposes the seam: `setCatalogUrlOverride("disabled")` (or
`ACC_STREAMDOCK_CATALOG_URL=disabled`) short-circuits the live POST while keeping
cached/fallback. A test can assert that, with the opt-in flag OFF (the new default), the live
branch is never entered — e.g. by pointing the override at `"disabled"` and asserting
`state()` stays `Cached`/`Offline` and never transitions to `Loading`.
`[VERIFIED: src/app/src/streamdock_catalog_fetcher.cpp:108-110,502-540]`

## Runtime State Inventory

> Phase 22 is mostly new code + a default flip, but it touches on-disk install layout and a settings flag, so the inventory is relevant.

| Category            | Items Found                                                                                                                                                                                                                                          | Action Required                                                                                                                                                                    |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Stored data         | Downloaded `.sdPlugin` archives + extracted dirs under `<QStandardPaths::AppDataLocation>/plugins/` (`userPluginsDir()`, `plugin_catalog_model.cpp:404-411`). Per-plugin settings under `<AppDataLocation>/plugins/<uuid>/` (Phase-18/20 territory). | Local install writes here; the planner must reconcile this single `plugins/` dir against Phase-18's `defaultPlugins/`+`installedPlugins/` split (Phase-18 RESEARCH open question). |
| Live service config | Online catalog cache at `<QStandardPaths::CacheLocation>/streamdock-catalog.json` (`streamdock_catalog_fetcher.cpp:152-158`). Not in git; regenerated.                                                                                               | No migration — the offline default reads it if present, else the bundled fallback.                                                                                                 |
| OS-registered state | None — verified: no Task Scheduler / launchd / systemd registration in this phase.                                                                                                                                                                   | None.                                                                                                                                                                              |
| Secrets/env vars    | `ACC_STREAMDOCK_CATALOG_URL` (catalog URL override; `"disabled"` sentinel). A NEW opt-in setting (likely `QSettings`) will be introduced for online-fetch enablement.                                                                                | Document the new setting key; default = online OFF.                                                                                                                                |
| Build artifacts     | `AJAZZ_PLUGIN_VERIFIER_SCRIPT` / `AJAZZ_PLUGIN_TRUST_ROOTS` compile defs only emitted under `AJAZZ_BUILD_PYTHON_HOST` (`src/app/CMakeLists.txt:443-449`).                                                                                            | See Pitfall 1 — the verify path's availability depends on this flag.                                                                                                               |

## Environment Availability

| Dependency                                      | Required By                           | Available                                               | Version                         | Fallback                                                                                |
| ----------------------------------------------- | ------------------------------------- | ------------------------------------------------------- | ------------------------------- | --------------------------------------------------------------------------------------- |
| Qt6 (Core/Gui/Quick/QML)                        | All UI + models                       | ✓ (project baseline)                                    | 6.7+                            | —                                                                                       |
| Qt6 private headers (`QZipWriter`/`QZipReader`) | extractor + fixture zips              | ✓ on this box (`qt6-qtbase-private-devel` per STATE.md) | 6.7+                            | — (operator installs system pkg; project hard rule = no system mutations from tooling)  |
| `python3` on a vetted path                      | `verifyManifest` runtime (subprocess) | ✓ (OOP host already requires it)                        | 3.x w/ `cryptography` (Ed25519) | Fail-closed: verify returns `valid=false` if python/script/crypto missing               |
| `ajazz_plugins` link in app                     | `verifyManifest`                      | ✓ **only if** `AJAZZ_BUILD_PYTHON_HOST=ON` (default ON) | —                               | **Pitfall 1** — `-OFF` builds must fail the install closed or the link must be un-gated |

**Missing dependencies with no fallback:** none on the default (`ON`) build.
**Missing dependencies with fallback:** `python3`/`cryptography` absent → `verifyManifest`
fails closed (treats every manifest as unsigned) — this is by design and is the safe failure.

## Validation Architecture

> `workflow.nyquist_validation: true` (`.planning/config.json`) — section included.

### Test Framework

| Property           | Value                                                                            |
| ------------------ | -------------------------------------------------------------------------------- |
| Framework          | Catch2 (`tests/unit/`) + integration + offscreen QML smoke (`tests/qml/`)        |
| Config file        | CTest presets (`CMakePresets.json`); `tests/unit/CMakeLists.txt` registers cases |
| Quick run command  | `ctest --preset linux-release -R "plugin-store\|manifest-signer"`                |
| Full suite command | `ctest --preset linux-release`                                                   |

### Phase Requirements → Test Map

| Req ID    | Behavior                                                        | Test Type       | Automated Command                                                    | File Exists?                                             |
| --------- | --------------------------------------------------------------- | --------------- | -------------------------------------------------------------------- | -------------------------------------------------------- |
| PLUGIN-14 | A signed local `.sdPlugin` installs into `installedPlugins/`    | unit            | `ctest --preset linux-release -R "plugin-store"`                     | ❌ Wave 0 (new `installFromFile` + test)                 |
| PLUGIN-14 | A tampered `.sdPlugin` is always refused (never promoted)       | unit            | `ctest --preset linux-release -R "plugin-store"`                     | ❌ Wave 0                                                |
| PLUGIN-14 | An unsigned `.sdPlugin` is refused/quarantined per policy       | unit            | `ctest --preset linux-release -R "plugin-store"`                     | ❌ Wave 0                                                |
| PLUGIN-14 | No live HTTP POST on launch / on local install (opt-in OFF)     | unit            | `ctest --preset linux-release -R "streamdock-catalog\|plugin-store"` | ⚠️ extend existing `test_streamdock_catalog_fetcher.cpp` |
| PLUGIN-14 | `verifyManifest` reused unchanged (byte-equal canonicalisation) | unit (existing) | `ctest --preset linux-release -R "manifest-signer"`                  | ✅ `test_manifest_signer.cpp`                            |
| PLUGIN-14 | Extract is zip-slip-guarded for local files too                 | unit (existing) | `ctest --preset linux-release -R "plugin-store.*issue-62"`           | ✅ `test_sdplugin_extractor.cpp`                         |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R "plugin-store"` (targeted).
- **Per wave merge:** `ctest --preset linux-release` (full suite, ~408 cases per CLAUDE.md).
- **Phase gate:** Full suite green before `/gsd:verify-work`. All hardware-free + network-free.

### Wave 0 Gaps

- [ ] `tests/unit/test_plugin_catalog_install_from_file.cpp` (or extend an existing catalog test) — covers PLUGIN-14 signed/unsigned/tampered + staging→verify→promote sequencing.
- [ ] Extend `tests/unit/test_streamdock_catalog_fetcher.cpp` — assert no live fetch when the opt-in flag is OFF (the new default).
- [ ] Signed/unsigned/tampered fixture builders — reuse the `test_manifest_signer.cpp` keygen/sign recipe + `test_sdplugin_extractor.cpp` `QZipWriter` recipe (no new infra).
- [ ] (If chosen) a tiny `QSettings` override harness for the opt-in flag.

*Existing infra covers the crypto + extract halves; the gap is the install-orchestration test + the no-network-on-launch assertion.*

## Security Domain

> `security_enforcement` absent in config → treated as ENABLED.

### Applicable ASVS Categories

| ASVS Category              | Applies | Standard Control                                                                                       |
| -------------------------- | ------- | ------------------------------------------------------------------------------------------------------ |
| V1 Architecture            | yes     | Verify-before-trust: extract to staging, verify, promote only on pass (trust boundary is explicit)     |
| V5 Validation/Sanitization | yes     | `validateDownloadedArchive` (size+magic) + zip-slip guard in `extractSdPluginArchive` + Ed25519 verify |
| V6 Cryptography            | yes     | **Never hand-roll** — reuse `verifyManifest` (Ed25519, canonicalisation pinned to the signer)          |
| V10 Malicious Code         | yes     | Signature gate refuses tampered manifests; unsigned refused/quarantined per policy                     |
| V12 Files/Resources        | yes     | Path traversal (zip-slip) guarded; archive size cap (`kMaxPluginDownloadBytes`)                        |
| V14 Configuration          | yes     | No phone-home by default; outbound fetch is opt-in (privacy posture)                                   |

### Known Threat Patterns for local `.sdPlugin` install

| Pattern                                                 | STRIDE                 | Standard Mitigation                                                                                          |
| ------------------------------------------------------- | ---------------------- | ------------------------------------------------------------------------------------------------------------ |
| Tampered manifest (swap action behaviour after signing) | Tampering              | Ed25519 verify over canonical bytes incl. embedded key → `valid==false` → refuse                             |
| Unsigned malicious plugin                               | Spoofing/Elevation     | Refuse or explicit-confirm-with-warning (never silent trust); fail-closed when verifier unavailable          |
| Zip-slip path traversal (`../../etc/...`)               | Tampering              | `extractSdPluginArchive` zip-slip guard (incl. Qt 6.11 embedded-`..` passthrough handled per project memory) |
| Decompression bomb / oversized archive                  | DoS                    | `validateDownloadedArchive` size cap before disk write                                                       |
| Phone-home / data exfiltration on launch                | Information disclosure | Online fetch opt-in/off-by-default; offline cached/bundled snapshot only                                     |
| PATH-hijack of the verifier interpreter                 | Elevation              | Already mitigated: `resolveTrustedExecutable` resolves python against vetted dirs, not `$PATH` (CWE-426)     |

## Project Constraints (from CLAUDE.md)

- **COD-031:** `manifest_signer` lives in `ajazz_plugins`; no `nlohmann::json` in `ajazz_core` or any installed public header. The app may link `ajazz::plugins` PRIVATE (it already does — `src/app/CMakeLists.txt:438`). Do NOT pull signer internals into `ajazz_core`.
- **Never skip pre-commit hooks** (`--no-verify` only for a broken hook, documented).
- **Atomic commits**, Conventional Commits (`feat(plugins):`, `feat(app):`, `test:`).
- **ASCII-only test names**; ctest filter is `--tests-regex`/`-R`.
- **No system-level mutations from project tooling** — the `qt6-qtbase-private-devel` install is the operator's action.
- **Cap concurrent execute agents at 2.**
- **Schema doc is the source of truth for JSON wire keys** — `Ajazz.Signing.Ed25519PublicKey` / `Ed25519Signature` are the manifest keys (per the verifier script); don't rename to match a C++ field.
- This is **not** protocol/wire-format/device code, so the RE-cross-check hard rule does not gate this phase (no opcodes touched).

## Dependency Gate (depends_on: [18])

**STOP-GATE STATUS: Phase 18 is PLANNED but NOT EXECUTED.** `[VERIFIED: .planning/phases/18-.../ has 18-01..04-PLAN.md + CONTEXT/RESEARCH/VALIDATION, but NO `*SUMMARY*` file; STATE.md says "14-18 PLANNED ... NOT executed"]`

Phase 18 introduces (per `18-RESEARCH.md`):

- A NEW `PluginManifest` struct + `parsePluginManifest()` parser (`src/app/src/plugin_manifest.{hpp,cpp}`) — there is **no manifest.json reader in the codebase today**.
- A NEW `PluginManager` (`src/app/src/plugin_manager.{hpp,cpp}`) for discovery (`defaultPlugins/`+`installedPlugins/`) + spawn + crash lifecycle.
- A planner-decided canonical on-disk layout mapping vendor `defaultPlugins/`+`installedPlugins/` onto the existing single `<AppDataLocation>/plugins/` dir.

**Implication for Phase 22 planning:** Phase 22's install must land files where Phase-18's
`PluginManager` will discover them. The `installedPlugins/` directory name in PLUGIN-14 is
Phase-18's layout decision — the planner must either consume Phase-18's resolved layout or
explicitly pin it. **Do not begin Phase 22 execution until Phase 18 has executed and its SUMMARY
exists** (the `PluginManifest` parser + the on-disk layout are Phase-22 inputs). Planning Phase 22
ahead of Phase 18 execution is fine (this is plan-ahead), but the plan should reference Phase-18
symbols as "to be delivered by Phase 18" and the executor must STOP if Phase 18 is unexecuted.

## Assumptions Log

| #   | Claim                                                                                                                  | Section                         | Risk if Wrong                                                                                                            |
| --- | ---------------------------------------------------------------------------------------------------------------------- | ------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| A1  | The intended default-OFF mechanism is a `QSettings` opt-in flag (vs. inverting the `"disabled"` sentinel)              | Pattern 2                       | Low — both are viable; planner picks. Either yields zero launch traffic.                                                 |
| A2  | `installedPlugins/` is the Phase-18-resolved subdir under `<AppDataLocation>/plugins/`                                 | Runtime State / Dependency Gate | Medium — depends on Phase-18 execution; if layout differs, install target path changes (1-line fix once Phase 18 lands). |
| A3  | Self-signed (valid + no trust-roots match) is the realistic developer-sideload verdict given the placeholder trust key | Pitfall 4                       | Low — verified the placeholder; UX must not promise a "trusted" badge that can't appear yet.                             |
| A4  | `python3` + `cryptography` are present on target (OOP host already requires them)                                      | Environment                     | Low — fail-closed if absent (safe).                                                                                      |

## Open Questions

1. **Should the existing network `install()` and the launch sweep also gain the verify gate?**

   - What we know: only the NEW local-file path is mandated by the CONTEXT decision; the network path and `extractStandalonePluginArchives` currently have no verify.
   - What's unclear: whether PLUGIN-14's "before a plugin is trusted" intends to cover *all* install paths.
   - Recommendation: gate all paths (or document the deferral) so there's no unverified back door; coordinate with Phase-18 discovery so verify happens at least once before load.

1. **Where exactly does verify run — install-time, load-time, or both?** (Claude's discretion per CONTEXT.)

   - Recommendation: verify at **install-time in a staging dir** (refuse before promotion) as the primary gate; Phase-18 discovery may re-verify at load-time as defence-in-depth. At minimum once before trust.

1. **`AJAZZ_BUILD_PYTHON_HOST=OFF` behaviour** (Pitfall 1).

   - Recommendation: fail-closed, OR un-gate the `manifest_signer` link + verifier defs so the gate is always present. Planner must pick and pin.

## Sources

### Primary (HIGH confidence — all read this session)

- `src/plugins/include/ajazz/plugins/manifest_signer.hpp` — `verifyManifest`, `ManifestSignerConfig`, `ManifestVerifyResult`, `TrustedPublisher`.
- `src/plugins/src/manifest_signer.cpp` — POSIX verifier (CWE-426 interpreter resolution, fail-closed).
- `scripts/sign-plugin-manifest.py` — keygen/sign/verify CLI; canonicalisation rules; exit codes.
- `src/app/src/plugin_catalog_model.{hpp,cpp}` — `install()`, `validateDownloadedArchive`, `userPluginsDir()`, constructor `reload()`/`refresh()` auto-fetch.
- `src/app/src/streamdock_catalog_fetcher.{hpp,cpp}` — `refresh()`, `effectiveCatalogUrl()`, `"disabled"` sentinel, default URL `space.key123.vip`.
- `src/app/src/loaded_plugins_model.hpp` — trust-level enum (`trusted`/`self-signed`/`unsigned`).
- `src/app/src/sdplugin_extractor.hpp` — `extractSdPluginArchive`, `extractStandalonePluginArchives`.
- `src/app/qml/PluginStore.qml` — tabs + per-tile Install/Uninstall; no FileDialog today.
- `src/app/CMakeLists.txt:437-449` — `ajazz::plugins` link + verifier/trust-roots defs gated on `AJAZZ_BUILD_PYTHON_HOST`.
- `CMakeLists.txt:29-34` — `AJAZZ_BUILD_PYTHON_HOST` option (default ON; CI uses OFF in some matrices).
- `resources/trusted_publishers.json` — placeholder key.
- `tests/unit/test_manifest_signer.cpp` — signed/tampered/unsigned/fail-closed recipe.
- `tests/unit/test_sdplugin_extractor.cpp` — `QZipWriter` fixture + zip-slip recipe.
- `docs/protocols/streamdeck/akp_plugin_sdk.md` §6 — the no-signature gap.
- `.planning/phases/18-.../18-RESEARCH.md` — `PluginManifest`/`parsePluginManifest`/`PluginManager` (the depends_on:[18] inputs).
- `.planning/STATE.md` — Phase 14-18 PLANNED, NOT executed.

### Secondary / Tertiary

- None — no web sources were needed; all findings are in-repo.

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH — every reuse surface read at file:line; no external deps.
- Architecture: HIGH — the three gaps (no local path / no verify in install / launch phone-home) are verified by direct code inspection.
- Pitfalls: HIGH — build-flag gate, constructor auto-fetch, mark-before-verify, placeholder trust key all confirmed in source.
- Dependency gate: HIGH — Phase 18 unexecuted confirmed (no SUMMARY + STATE.md).

**Research date:** 2026-05-23
**Valid until:** 2026-06-22 (stable — in-repo code; re-check after Phase 18 executes, as the on-disk layout + `PluginManifest` parser are Phase-22 inputs).
