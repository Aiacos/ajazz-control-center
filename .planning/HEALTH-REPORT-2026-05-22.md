# Software Engineering Health Report — AJAZZ Control Center

> Generated 2026-05-22. Three parallel read-only analysis agents (architecture/quality/debt,
> security, testing/build/CI/deps) + direct metrics, all verified against HEAD (`3e2fe27`).

## Executive summary

**Overall condition: HEALTHY — strong, mature engineering for a ~1-month-old project.**
584 commits since 2026-04-24, 3 milestones (v1.0 + v1.1 sealed, v1.2 in flight),
~49.6k LOC (src+tests+qml), ~31k LOC production C++ across 133 files. The project
punches well above its age on build/CI/dependency discipline and architectural
cleanliness. The genuine weaknesses are concentrated and well-understood: one
HIGH security gap (sandbox filesystem confidentiality), one MEDIUM (a PATH-hijack
in the *signature-verification* path — the same bug class just fixed elsewhere),
a QML test blind spot, and some doc drift.

| Dimension               | Rating                           | Headline                                                                                                          |
| ----------------------- | -------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| Architecture & layering | **Excellent**                    | Strict downward deps, Qt-free core boundary, COD-031 verified clean, well-formed capability/DI/flyweight patterns |
| Code quality            | **Good → Excellent**             | Strong RAII/const/`[[nodiscard]]`, disciplined UAF-avoidance, deliberate heterogeneous error model                |
| Technical debt          | **Good**                         | ~2 real inline markers, 0 FIXME/HACK; debt externalized & tracked; remainder is hardware-gated provisional RE     |
| Testing                 | **Good (Excellent on protocol)** | 395 cases / 16,690 assertions, clean MockTransport DI — but **0 QML tests** + 2 single-case service tests         |
| Build system            | **Excellent**                    | Full preset matrix, 3-compiler `-Werror`/`/WX`, ASan/UBSan/TSan, fuzzing, coverage                                |
| CI/CD                   | **Excellent**                    | 3-OS matrix, fail-fast gates, full packaging, codesign/notarize, SLSA L3 provenance, SHA-pinned actions           |
| Dependencies            | **Excellent**                    | All pinned (Qt/hidapi-SHA/Catch2/json), Dependabot, dependency-review + CodeQL + gitleaks                         |
| Security                | **Good**                         | Mature defense-in-depth & threat-ID traceability; 1 HIGH + 1 MEDIUM residual (see below)                          |

______________________________________________________________________

## 1. Architecture & layering — Excellent

- Dependency graph flows strictly downward (`core → devices → app/plugins`), verified by grep: **0** upward includes, **0** cross-backend (streamdeck↔keyboard↔mouse) includes.
- **Core is Qt-free at its public boundary** (`grep QObject src/core/include/` → 0) — better isolation than the docs claim.
- **COD-031 boundary INTACT**: `grep -rn nlohmann src/core/include/` → **0** (only a comment in `profile.cpp:7`); `nlohmann_json` PRIVATE-linked to `ajazz_plugins`.
- Well-formed abstractions: `IDevice` (non-copyable, explicit zombie contract), `ITransport` DI seam (COD-026, `MockTransport`/`ThrowingTransport`), `DeviceRegistry` flyweight (`weak_ptr` cache, documented lock order, injectable enumerator), 21 capability interfaces queried via `dynamic_cast` with disciplined `shared_ptr`-pin-before-cast (UAF avoidance).
- Out-of-process plugin host cleanly platform-split with manifest-signature gating.
- **Nits:** the `[[deprecated]] DeviceRegistry::instance()` shim now has 0 call sites (deletable dead weight); `DeviceDescriptor` carries UI-sizing hints in a *core* struct that duplicate runtime capability data (latent drift surface).

## 2. Code quality — Good → Excellent

- Consistent RAII/smart-pointers, strong const-correctness, `[[nodiscard]]`/`noexcept` throughout headers.
- **Deliberate heterogeneous error model:** `std::optional<T>` for fallible reads (never throws on malformed USB), `TimeSyncResult` enum (no-lying-success contract), `void`+internal-try/catch+log for fire-and-forget setters, `throw` confined to lifecycle/transport edges (verified: none in the hot input/poll path).
- No god-objects; `application.cpp` (409 LOC) is a thin orchestrator. Largest units are cohesive protocol files (`proprietary_keyboard.cpp` 1,183; `aj_series.cpp` 963; `akp05.cpp` 934).
- Duplication candidate: `opendeck_catalog_fetcher.cpp` / `streamdock_catalog_fetcher.cpp` are parallel HTTP state machines (shared base would help; streamdock also missing a watchdog, QC-27).
- **Doc caveat:** ARCH.md references a `Result::DeviceGone`/`Result::Ok` enum that **does not exist** — the real model is `TimeSyncResult` + `optional` + `void`+log. Misleading to a maintainer.

## 3. Technical debt — Good

- **Genuinely low marker count:** ~2 real inline gaps (`via_keyboard.cpp:185` per-LED RGB FEAT-01; `pi_bridge.cpp:392` pi-prompt follow-up), **0 FIXME, 0 HACK**. Debt externalized to `TODO.md`/CONCERNS.md by convention.
- The real remaining debt is the **§5 hardware wall**: 7 DEFERRED wire-format items (AK980 envelope framing, `0x0A` RGB off-by-two, touch-strip X-clamp@640, packet-size 512-vs-1024, placeholder VID:PID) — provisional RE values that *cannot* be resolved without physical-device captures. Honestly flagged in-code, no live callers.
- **Risk pattern (QC-23):** tests pin provisional layouts as ground truth, so a future hardware correction will read as a regression. → Recommend annotating those `REQUIRE`s as `PROVISIONAL`.
- Honest stubs (no fake success): `MacroRecorder` no-op, Autostart Linux-only, Stream Dock firmware-update unimplemented.

## 4. Testing — Good (Excellent on protocol, one structural gap)

- **395 TEST_CASEs / 16,690 assertions** (live), 402 ctest entries, +16 Python tests. ~437 source `TEST_CASE`s (CLAUDE.md's "~286" is stale).
- Clean unit/integration split; `MockTransport` header-only DI records every write for exact byte-equality assertions; capture-replay integration tests + a libFuzzer harness for `loadTrustRoots`.
- **Strong:** device protocol/wire-format coverage (keyboard 37, mouse wire-format 23, akp03 21, akp05 20).
- **GAPS:**
  - **QML entirely untested** — 32 `.qml` files, 0 QML test harness. Every documented QML gotcha (SIGABRT on `MultiEffect.maskSource`, singleton double-instance, Popup Material scope) is regression-unprotected. **Largest hole.**
  - `test_battery_service.cpp` (1 case) and `test_settings_service.cpp` (1 case) are thin — notable given battery's documented decode fragility.
  - Coverage preset + CI job exist but **no enforced floor / no Codecov trend** (artifact-only upload).

## 5. Build / CI/CD / Dependencies — Excellent

- **Build:** full {linux,windows,macos}×{debug,release} + dev/release/coverage/fuzz presets, guarded by a preset-validator pre-commit hook. Very aggressive warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wconversion …`; MSVC `/W4 /permissive-`) with `-Werror`/`/WX`. ASan/UBSan/TSan (mutually-exclusive-guarded), libFuzzer, gcov coverage.
- **CI/CD:** 3-OS matrix, fail-fast gates (`hid_open` invariant grep, Windows hot-plug smoke gate with correct `--tests-regex`), full packaging (.deb/.rpm/.flatpak/.dmg/.msi), conditional codesign/notarize, **SLSA L3 build provenance** + Sigstore, all third-party actions **SHA-pinned**. CodeQL + dependency-review + gitleaks workflows. Rich pre-commit incl. 3 project-specific guards (raw-capture rejection, ASCII test-name guard, preset validator).
- **Dependencies:** Qt 6.8.3 pinned, hidapi pinned by git SHA, Catch2 3.7.1, nlohmann_json PRIVATE (COD-031). Dependabot configured (actions + pip, conventional-commit-safe prefix). FetchContent SHAs are inherently manual (not a defect).

## 6. Security — Good (mature posture; 1 HIGH + 1 MEDIUM residual)

**Done well (preserve):** complete zip-slip guard (embedded-`..` + drive-prefix, `sdplugin_extractor.cpp:61-71`), `readUInt` sign rejection, HTTPS-only + size-capped + magic-checked download pipeline, WebEngine property-inspector hardening with per-plugin profile isolation + URL interceptor, **no** TLS-verification disabling anywhere, no `system()`/`popen()`/`ShellExecute`, span-based HID transport (memory-safe), Ed25519 manifest signing that fails closed, exemplary threat-ID traceability (WR-/SEC-/COD-).

**Top risks (ranked):**

1. **HIGH — Sandboxed plugin can read the entire host filesystem.** `linux_bwrap_sandbox.cpp:163` (`--ro-bind / /`) + `macos_sandbox_exec_sandbox.cpp:113` (`(allow file-read*)`). **CWE-200**; self-documented as provisional. A plugin can read `~/.ssh`, browser credential stores, `~/.config` secrets. Blast radius widens with a network permission (Linux drops `--unshare-net` → child sees host net namespace verbatim → exfiltration). *Fix:* scope `--ro-bind` to `/usr`, `/lib*`, `/etc/ld.so.*` + the plugin's own dir.
1. **MEDIUM — `python3` resolved via `$PATH` in the signature-verifier and host spawn.** `manifest_signer.cpp:78` + default `pythonExecutable{"python3"}` (and the OOP host child spawn). **CWE-426** PATH-hijack can substitute a fake `python3` that always exits 0, **defeating Ed25519 manifest verification**. *Ironic:* this is the **same bug class just fixed in `notification_service` (P09 WR-02)** — the fix was not applied consistently to the security-critical path. *Fix:* resolve python from a vetted absolute path (mirror the `execv`-allowlist pattern). **High value, low effort.**
1. **LOW** — child inherits full parent environment on the spawn (sandbox re-exec mitigates; tighten env block).
1. **INFO** — macOS profile allows broad `process-exec*` / `file-write*` under temp dirs; narrow alongside #1.

______________________________________________________________________

## Cross-cutting findings & prioritized recommendations

| #   | Priority   | Finding                                                                                               | Action                                                                                   |
| --- | ---------- | ----------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| 1   | **HIGH**   | Sandbox reads whole host FS (CWE-200)                                                                 | Scope bwrap `--ro-bind` / macOS `file-read*` to minimal subpaths                         |
| 2   | **MEDIUM** | `python3` PATH-hijack defeats sig verification (CWE-426) — same class as the just-fixed WR-02         | Resolve python by absolute path; apply the `execv`-allowlist pattern                     |
| 3   | **MEDIUM** | QML entirely untested (32 files, 0 harness)                                                           | Add `QtQuickTest`/`QQmlApplicationEngine` smoke loads — closes the biggest coverage hole |
| 4   | **LOW**    | `CONCERNS.md` lists fixed items as OPEN (QC-01/10/18/19/20 fixed this session)                        | Refresh CONCERNS.md; real open count is materially lower                                 |
| 5   | **LOW**    | Provisional-RE test `REQUIRE`s pinned as ground truth (QC-23)                                         | Annotate as `PROVISIONAL` so hardware corrections don't read as regressions              |
| 6   | **LOW**    | ARCH.md `Result::*` enum is fictional; `instance()` shim dead; `DeviceDescriptor` UI-hint duplication | Doc reconcile + delete shim                                                              |
| 7   | **LOW**    | No enforced coverage floor; CLAUDE.md "~286 tests" stale                                              | Add coverage gate / Codecov; bump the doc figure                                         |

**Bottom line:** the project is in good shape and clearly built with discipline — the
hard parts (layering, cross-platform strictness, supply chain, RE traceability) are
done unusually well. The two security items (#1 sandbox FS scope, #2 python PATH) are
the only findings that rise to "should fix before shipping plugins broadly," and #2 is
a quick, high-value follow-on to work already done this session.
