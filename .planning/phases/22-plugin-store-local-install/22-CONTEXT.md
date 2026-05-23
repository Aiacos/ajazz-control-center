# Phase 22: Plugin Store / Local Install - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions + akp_plugin_sdk.md §6 + existing-code survey

<domain>
## Phase Boundary

Phase 22 lets the user install plugins from a **local** package through a **host-owned** catalog,
behind a **signature-verification gate**, with **no automatic phone-home**. Most pieces exist —
the store UI (`PluginStore.qml` with install/enable/disable/uninstall + All/Installed/Streamdock/
Community tabs), the catalog models, the zip-slip-guarded extractor, AND the **Ed25519
manifest verifier** (`manifest_signer.hpp`, SEC-003 follow-up #51). This phase is **integration +
the honesty gate**.

**Delivers (PLUGIN-14):**

- Install from a **local `.sdPlugin`/`.zip`** into `installedPlugins/` via the host-owned catalog
  (reuse the extractor + `plugin_catalog_model` + `loaded_plugins_model`).
- A **signature/manifest-verification gate** (reuse the Ed25519 `manifest_signer`) — an unsigned
  or tamper-signed manifest is rejected/quarantined (not loaded), per the SEC-003 #51 contract.
- **No phone-home:** the existing `streamdock_catalog_fetcher` online fetch is **opt-in /
  off-by-default** — no automatic launch-time fetch to the Mirabox "Space"/Aliyun endpoints
  (anti-feature). Local install is the default path.

**Out of scope:** auxiliary surfaces (Phase 23); hardware verify (Phase 25). The catalog UI and
the Phase-18 `PluginManager` (discovery/spawn) already exist — Phase 22 feeds installs into them.
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Reuse + integrate (do NOT rebuild)

- Install/extract: reuse `sdplugin_extractor` (`extractSdPluginArchive`, zip-slip-guarded).
- Signature gate: reuse the **Ed25519 `manifest_signer`** (`ManifestSignerConfig`,
  `verifyManifestSignature`) — the same SEC-003 #51 verifier the Python host uses. An unsigned or
  tampered manifest is **rejected/quarantined**, not loaded.
- Catalog/UI/state: reuse `PluginStore.qml`, `plugin_catalog_model`, `plugin_catalog_proxy_model`,
  `loaded_plugins_model`; an installed local package appears as an "Installed" catalog row.
- Discovery/lifecycle: the Phase-18 `PluginManager` consumes the installed package
  (`depends_on: [18]`).

### No phone-home (PLUGIN-14 anti-feature)

- The online `streamdock_catalog_fetcher` must NOT auto-fetch on launch. Make the online source
  **opt-in** (a user-enabled setting / explicit "refresh online catalog" action); the default is
  **local install only** (offline snapshot is fine — it ships no user data outbound). No outbound
  request without explicit user action. (The fetcher already supports an offline snapshot +
  configurable/empty URL — wire the default to local/offline.)

### Trust model

- The signature gate is the trust boundary the vendor lacked (vendor extracts + trusts ZIPs with
  no verification — §6). A local install of an UNSIGNED package may be allowed only with an
  explicit user confirmation (or refused outright — planner picks the stricter UX that still lets
  a developer sideload their own plugin); a TAMPERED signature is always refused.

### Claude's Discretion (planner/executor)

- "Install from file…" UX entry point in `PluginStore.qml` + the controller method.
- Unsigned-package policy: hard-refuse vs explicit-confirm-with-warning (favor the safer default
  that still permits developer sideload).
- Where the verify runs (at install time, at load time, or both) — at least once before trust.
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

- `docs/protocols/streamdeck/akp_plugin_sdk.md` §6 (plugin store / Mirabox Space; the no-signature gap we close).
- `src/plugins/include/ajazz/plugins/manifest_signer.hpp` — Ed25519 `ManifestSignerConfig` + `verifyManifestSignature` (SEC-003 #51); `Ed25519PublicKey` manifest field.
- `src/app/src/sdplugin_extractor.{hpp,cpp}` — install/extract (zip-slip-guarded).
- `src/app/src/plugin_catalog_model.{hpp,cpp}` + `plugin_catalog_proxy_model.*` + `loaded_plugins_model.*` + `src/app/qml/PluginStore.qml` — catalog/UI/state.
- `src/app/src/streamdock_catalog_fetcher.{hpp,cpp}` — the online fetcher (make opt-in/off-by-default; it already has an offline snapshot + `ACC_STREAMDOCK_CATALOG_URL`).
- `.planning/phases/18-...spawn/18-*` — the manifest parser + `PluginManager` discovery the install feeds.
- v1.0 SEC-003 + the Phase-7 trust-roots parser (the Ed25519 trust story) — `.planning/RETROSPECTIVE.md`.
- CLAUDE.md — COD-031 (`manifest_signer` lives in `ajazz_plugins`); never skip pre-commit; ASCII test names. Anti-feature inventory (no phone-home, no unsigned trust) in `.planning/REQUIREMENTS.md` Out-of-Scope.
  \</canonical_refs>

<specifics>
## Specific Ideas

- Verification is hardware-free + network-free: a **signed fixture `.sdPlugin`** installs →
  extracted (zip-slip-guarded) → signature verified → registered as installed; an **unsigned**
  package → refused or quarantined per the chosen policy (not loaded); a **tamper-signed** package
  → always refused; **no outbound network request occurs on launch or local install** (assert the
  fetcher is not auto-triggered — e.g. a no-network test harness / a spy on the fetcher). Reuse the
  existing `manifest_signer` tests + the extractor tests. ASCII test names; `ctest --preset linux-release`.

</specifics>

<deferred>
## Deferred Ideas

- Auxiliary surfaces → Phase 23. Family coverage → Phase 24. Hardware verify → Phase 25.
- An online plugin marketplace with curation/payments (the vendor's `productType`/`price` fields) — out of scope; host-owned local catalog only.

</deferred>

______________________________________________________________________

*Phase: 22-plugin-store-local-install*
*Context gathered: 2026-05-23 (v1.3 replan locked decisions; akp_plugin_sdk.md §6 is the spec; signer + store UI pre-built)*
