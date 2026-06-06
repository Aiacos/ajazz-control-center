---
phase: 27-plugin-install-trust-persistence-hardening
reviewed: 2026-05-31T16:08:18Z
depth: standard
files_reviewed: 17
files_reviewed_list:
  - src/plugins/include/ajazz/plugins/manifest_signer.hpp
  - src/plugins/src/manifest_signer.cpp
  - src/plugins/src/manifest_signer_win32.cpp
  - src/app/src/plugin_verify_gate.hpp
  - src/app/src/plugin_verify_gate.cpp
  - src/app/src/plugin_catalog_model.hpp
  - src/app/src/plugin_catalog_model.cpp
  - src/app/src/plugin_manager.hpp
  - src/app/src/plugin_manager.cpp
  - src/app/src/application.cpp
  - src/app/qml/LoadedPluginsPage.qml
  - tests/unit/test_manifest_signer.cpp
  - tests/unit/test_plugin_verify_gate.cpp
  - tests/unit/test_plugin_install_from_file.cpp
  - tests/unit/test_plugin_lifecycle.cpp
  - tests/unit/test_plugin_concurrency.cpp
  - tests/unit/CMakeLists.txt
findings:
  critical: 0
  warning: 4
  info: 3
  total: 7
status: issues_found
---

# Phase 27: Code Review Report

**Reviewed:** 2026-05-31T16:08:18Z
**Depth:** standard
**Files Reviewed:** 17
**Status:** issues_found

## Summary

Phase 27 hardens plugin install/trust/persistence. I focused adversarially on the CR-01
security gate (tampered vs unsigned) across every promotion path. **The CR-01 invariant
holds**: I traced `installFromFile` (rename + cross-fs copy re-verify), the network
`install()` path, the constructor launch-sweep, `allowPlugin()`, the `allowUnsignedPlugins`
setting, the `AJAZZ_ALLOW_UNTRUSTED_PLUGINS` env var, and `consentToUnsigned()` — in all of
them a `VerifyVerdict::Refused` (signature-present-but-invalid / tampered) package is
unconditionally quarantined and never promoted, even with `userConfirmedUnsigned==true`.
SignatureState{None,Valid,Invalid} is correctly classified by signature-block *presence*
before any subprocess on the POSIX backend, and the QML "Allow" action is correctly ABSENT
(not merely disabled) on tampered rows. No Critical findings.

The defects below are correctness/robustness issues, the most consequential being that the
**per-plugin "Allow this plugin" trust UX is functionally dead** — it writes a settings key
nothing reads, never promotes the plugin, and the launch-sweep then deletes that very plugin
on next restart. There is also a **cross-platform fail-closed asymmetry**: the Win32 verifier
backend lacks the CWE-426 PATH-hijack guard the POSIX backend enforces.

## Warnings

### WR-01: Per-plugin "Allow this plugin" consent is a no-op and is undone on next launch

**File:** `src/app/src/plugin_catalog_model.cpp:531-571` (`allowPlugin`), interacting with `:156-176` (launch-sweep) and `:524-529` (`consentToUnsigned`)
**Issue:** `allowPlugin()` is the per-plugin trust affordance wired to the QML "Allow" button
(`LoadedPluginsPage.qml:249-253`). Its header contract (`plugin_catalog_model.hpp:433-453`)
says it records consent "so subsequent launches do not require re-consent. Then re-runs the
install/promote path for the plugin so it becomes immediately runnable." Neither happens:

1. It writes `plugins/allowed/<uuid>=true` (`:561`) — but **nothing ever reads that key**
   (`grep -rn "plugins/allowed" src/ tests/` → only the write site). It is a dead write.
1. It does NOT promote/re-install anything; it only emits `installedCountChanged()` (`:569`).
1. The launch-sweep (`:156-176`) and `installFromFile` (`:821`) gate unsigned-keep on
   `consentToUnsigned()` (`:524-529`), which consults ONLY the global
   `m_allowUnsignedPlugins` setting and the env var — never the per-plugin `plugins/allowed`
   key. So an unsigned plugin the user explicitly "Allowed" via the per-plugin button is
   `removeRecursively()`-deleted on the next launch (`:170`) unless the *global* toggle is on.

Net effect: the per-plugin trust UX advertised in D-27-2 ("a per-plugin 'Allow this plugin'
action") and the success criteria silently does nothing useful and actively loses the plugin
on restart. Fails safe (toward deletion, not toward running attack code), so not Critical, but
it is a broken user-facing security feature.
**Fix:** Make `consentToUnsigned()` (or the launch-sweep / install gate) also honour the
per-plugin key, and have `allowPlugin()` actually drive promotion. Minimum viable:

```cpp
// In the launch-sweep Unsigned branch and installFromFile, consult per-plugin consent:
bool perPluginAllowed(QString const& pluginDirName) {
    QSettings s;
    QString const uuid = pluginDirName.endsWith(QStringLiteral(".sdPlugin"))
        ? pluginDirName.chopped(9) : pluginDirName;
    return s.value(QStringLiteral("plugins/allowed/") + uuid, false).toBool();
}
// launch-sweep: keep when consentToUnsigned() || perPluginAllowed(entry)
```

And in `allowPlugin()`, after recording consent, re-run discovery/promotion (e.g. emit a
`rediscover`-triggering signal) rather than only `installedCountChanged()`. Add a test:
allow an unsigned plugin per-plugin with the global toggle OFF, restart (fresh model), assert
the plugin dir survives.

### WR-02: Win32 verifier backend omits the POSIX CWE-426 fail-closed interpreter guard

**File:** `src/plugins/src/manifest_signer_win32.cpp:165-175` vs `src/plugins/src/manifest_signer.cpp:183-192`
**Issue:** The POSIX backend resolves the interpreter via `resolveTrustedExecutable()`, which
only accepts a vetted absolute path (`/usr/bin`, `/usr/local/bin`, `/bin`) and **never** uses
`$PATH`; if it cannot resolve, it returns empty and the verifier fails closed to
`SignatureState::Invalid` (`manifest_signer.cpp:187-192`). The Win32 backend instead passes
`win32::resolveRealPython(config.pythonExecutable)` straight into argv (`:169`) with no
empty/unresolvable check, then `runChild()` calls `_wspawnvp(_P_WAIT, …)` (`:106`). `_wspawnvp`
is the `p`-variant that performs a `$PATH` search. When `resolveRealPython` cannot resolve a
concrete file it returns the bare name `"python3"` **unchanged** (see
`win32_python_resolve.hpp:98-99,127`), so `_wspawnvp` falls back to `$PATH`. A writable dir
prepended to `%PATH%` (installer, `.lnk` wrapper, CI shim) dropping a fake `python3.exe` that
`exit(0)`s would forge a `SignatureState::Valid` verdict on Windows — exactly the PATH-hijack
the POSIX side closes. The header comment in `win32_python_resolve.hpp:88-90` claims it "fails
closed (CWE-426)", but returning the bare name to a PATH-resolving spawn is not fail-closed.
The two backends therefore do NOT agree on the security contract (review focus area #2).
**Fix:** In the Win32 `verifyManifest`, mirror the POSIX guard: resolve to a concrete absolute
path and fail closed when it is still a bare name. E.g.

```cpp
std::string const pythonExe = win32::resolveRealPython(config.pythonExecutable);
// Reject anything that did not resolve to a concrete file (no PATH fallback via _wspawnvp).
if (pythonExe.empty() ||
    ::GetFileAttributesA(pythonExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    result.signatureState = SignatureState::Invalid; // present-but-unverifiable
    return result;
}
```

and/or switch `runChild` from `_wspawnvp` to `_wspawnv` (no `P_` PATH search) now that argv[0]
is absolute. Add a Win32 analogue of the POSIX `unresolvable interpreter fails closed (CWE-426)`
test (`test_manifest_signer.cpp:274-309`), which currently has no Windows counterpart.

### WR-03: `installFromFile` Refused branch leaks the staging-parent directory

**File:** `src/app/src/plugin_catalog_model.cpp:804-819`
**Issue:** The Refused (tampered) branch removes the per-plugin staging subdir
(`QDir(QDir(stagingParent).filePath(archiveName)).removeRecursively();` at `:813`) but, unlike
the Unsigned (`:828-829`) and SelfSigned (`:844-845`) branches, never calls
`QDir(stagingParent).rmdir(".")`. So the `.plugin_staging` parent dir is left behind after a
tampered install. Across repeated hostile-install attempts this accumulates an empty (or
partially-populated, if a sibling archive coexists) staging dir in
`AppDataLocation/../.plugin_staging`. Minor, but it is an inconsistency in the security path
and means an attacker can leave a marker dir adjacent to the plugins tree. The successful-path
cleanup at `:938` (`removeRecursively`) only runs on success, not on the Refused early-return.
**Fix:** Add the same parent cleanup the sibling branches use:

```cpp
QDir(QDir(stagingParent).filePath(archiveName)).removeRecursively();
QDir(stagingParent).rmdir(QStringLiteral("."));  // <-- add, mirroring :829 / :845
```

### WR-04: `installFinished(ok==true)` → `rediscover()` connection has no path-scoping and re-scans on every successful install

**File:** `src/app/src/application.cpp:813-820` + `src/app/src/plugin_manager.cpp:567-603`
**Issue:** Functionally the lifetime is sound — both `m_pluginCatalog` and `m_pluginManager`
are `Application`-owned `unique_ptr`s that outlive the connection, the receiver is
`m_pluginManager.get()` so Qt auto-disconnects if it dies first, and `rediscover()` is
idempotent (diffs against `m_live`). No dangling. However: `installFinished` also fires
`success==true` for the network `install()` "already installed" idempotent case
(`plugin_catalog_model.cpp:976-978`) and for `openUpstream`-only fallbacks
(`:992-998`, where `opened==true` but nothing was installed locally). Each of those triggers a
full `discover()` (which re-runs `extractStandalonePluginArchives` + a directory walk +
`parsePluginManifest` on every `.sdPlugin`). It is harmless (idempotent) but means an
"open browser page" click or a redundant install re-walks the whole plugins dir. More
importantly, `rediscover()` does not consult the verify gate — it relies on the install paths
and the constructor launch-sweep having already quarantined Refused/unconsented-Unsigned dirs.
That holds today because every promotion path verifies, but the coupling is implicit and
undocumented at the `rediscover()` call site.
**Fix:** Either gate the connection on a stronger signal than bare `installFinished(ok)` (e.g.
only when an actual promote happened), or add an assertion/comment at
`plugin_manager.cpp:567` documenting that `rediscover()` trusts the on-disk dirs to be
pre-verified and must NOT be called on an unverified plugins dir. Low risk; flagged so the
implicit invariant is made explicit before someone wires `rediscover()` to a new caller.

## Info

### IN-01: `SignatureState` enum has no fixed underlying type (clang-tidy performance-enum-size)

**File:** `src/plugins/include/ajazz/plugins/manifest_signer.hpp:65-69`
**Issue:** As the review brief noted, clang-tidy `performance-enum-size` flags `enum class SignatureState` (3 values) for defaulting to `int`. Cosmetic; the struct is small and
short-lived. Confirming the flag is accurate.
**Fix:** `enum class SignatureState : std::uint8_t { None, Valid, Invalid };` (add
`#include <cstdint>`). Same applies to `VerifyVerdict` in `plugin_verify_gate.hpp:41` if the
linter is run over `src/app/`.

### IN-02: `entryFor()` omits `downloadUrl` key that `data()`/`roleNames()` expose

**File:** `src/app/src/plugin_catalog_model.cpp:1258-1283`
**Issue:** `entryFor()` returns a flat map for the details pane but does not include the
`downloadUrl` field, while `roleNames()`/`data()` (`:285-286, :310`) do expose `DownloadUrlRole`.
A QML consumer reading `entryFor(uuid).downloadUrl` gets an empty/undefined value rather than
the row's URL. Not a bug today (no caller observed reading it from `entryFor`), but an
inconsistency between the two row-access surfaces.
**Fix:** Add `{"downloadUrl", src.downloadUrl}` to the returned map for parity with
`roleNames()`.

### IN-03: Cross-fs copy-fallback recurses only one directory level

**File:** `src/app/src/plugin_catalog_model.cpp:876-906`
**Issue:** The CR-02 copy-fallback (used when `QDir::rename` fails across filesystems) hand-rolls
a copy that recurses exactly one level (`:887-896` comment: "Recurse one level … deep trees are
unusual"). A `.sdPlugin` with nested resource subdirs (`Code/lib/foo/bar.js`,
`assets/img/icons/…`) would be copied incompletely, then re-verified at `:919-934` — verification
passes (manifest.json is top-level) but the plugin is promoted missing files and fails to run.
Real Stream Deck plugins routinely ship `Code/` plus nested asset trees, so "deep trees are
unusual" is optimistic. The plugin won't be *insecure* (it re-verifies), just broken. Only
triggers on a genuine cross-filesystem staging→install rename failure (uncommon since staging is
a sibling of the install dir on the same volume), hence Info not Warning.
**Fix:** Replace the bespoke one-level copy with a proper recursive copy (e.g. a small
`copyDirRecursive` helper or `std::filesystem::copy(..., recursive)`), or — since staging and
install are siblings under the same `AppDataLocation` — assert same-filesystem and treat a
rename failure as a hard error rather than attempting a partial copy.

______________________________________________________________________

_Reviewed: 2026-05-31T16:08:18Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
