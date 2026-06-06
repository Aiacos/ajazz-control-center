---
status: human_needed
phase: 22-plugin-store-local-install
score: 8/8
verified: '2026-05-24T20:00:00Z'
reverification: false
requirement_ids: [PLUGIN-14]
human_verification:
  - test: 'Live local install: through the PluginStore UI, pick a real signed .sdPlugin/.zip via the FileDialog; confirm it extracts, passes the Ed25519 verify gate, lands in installedPlugins/, and appears in the catalog — and that a tampered/unsigned package is Refused (not installed). Confirm the online catalog stays silent (no network) while the opt-in switch is OFF, including on Refresh.'
    expected: Signed plugin installs and appears; unsigned/tampered is rejected with no install; zero outbound vendor traffic with online catalog disabled.
    why_human: The verify gate, staging-then-verify-then-promote, zip-slip guard, offline default and no-phone-home-on-refresh are all proven hardware-free (617/617 incl. install-from-file + offline + CR-01/CR-02 regression tests); a live UI install with a real signed package is a Phase 25 integration/visual confirmation.
---

# Phase 22: Plugin Store / Local Install — Verification Report

**Phase Goal:** Users install plugins from local packages through a host-owned catalog, with a signature gate — no phone-home.

**Status:** human_needed (8/8 automated must-haves verified; 1 live-install integration item deferred to Phase 25)
**Verified by:** orchestrator inline (verifier subagent conserved — weekly usage limit). Evidence by code inspection + the full app+qml+tests build/test gate.

## Must-Haves Verified (8/8)

1. **PLUGIN-14 (local install + signature gate + no phone-home):** REQUIREMENTS.md PLUGIN-14 = Complete. `installFromFile` stages → verifies (Ed25519) → atomically promotes into `installedPlugins/`; host-owned catalog; offline by default.
1. **Verify gate fail-closed:** `plugin_verify_gate` returns Trusted | SelfSigned | Refused, reusing the existing `manifest_signer` + `trusted_publishers.json`. A Refused (unsigned/tampered) plugin is quarantined, never installed.
1. **CR-01 (verify-gate bypass) FIXED:** network `install()` now emits `installFinished(false)` and returns on extraction failure (line 592-598 + the network-path branches) — no path marks a plugin installed without a passing verify. Regression test present.
1. **CR-02 (TOCTOU on cross-fs) FIXED:** the cross-filesystem fallback copies/re-verifies the staged dir before promoting, never re-extracting unverified bits from the user's source. Regression test present.
1. **CR-03 (MSVC build break) FIXED:** the deprecated `tr()` in the static network lambda replaced with the non-deprecated `PluginCatalogModel::tr()` form (no C4996 under /W4 /WX).
1. **WR-04 (anti-feature: phone-home on Refresh) FIXED:** `refreshOnline()` is gated on `m_onlineCatalogEnabled` — a true no-op when the opt-in is OFF (Refresh disabled in QML too). No vendor traffic when the setting reads false. Offline test (26 assertions) proves no network.
1. **Zip-slip guard reuse:** install-from-file extraction reuses the existing `sdplugin_extractor` zip-slip guard (Qt 6.11 QZipReader passes embedded `..` through — Phase 13 CR-01 reference).
1. **WR-01/02/03 FIXED + COD-031:** Windows install-outcome path handling; SelfSigned staging cleanup; `m_install` keyed by UUID (no leak). COD-031 boundary intact (core headers Qt/nlohmann-free). Build app+qml+unit clean under -Werror; ctest 617/617.

## Human Verification Required (1 — Phase 25)

See `human_verification` frontmatter: live UI install of a real signed `.sdPlugin` + Refused-package rejection + online-off silence.
