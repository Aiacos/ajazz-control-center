# Project Research Summary

**Project:** AJAZZ Control Center
**Milestone:** v1.1 — Device lifecycle hardening + scaffolding-to-functional
**Domain:** Cross-platform Qt 6 / QML 6 + hidapi HID device control-center (brownfield, alpha; subsequent to v1.0 retro-fit catalogue)
**Researched:** 2026-05-13
**Confidence:** HIGH (stack + architecture verified against working tree; features MEDIUM-HIGH; pitfalls HIGH)

______________________________________________________________________

## Executive Summary

v1.1 is a **brownfield hardening milestone** on top of a working 3-layer architecture (libajazz-core → device backends → Qt/QML app + Python plugin host). Five workstreams land in parallel: hot-plug hardening, time-sync five-layer scaffolding, scaffolded-device promotion, the Win32 OOP-host env-pollution fix (CR-01), and the `loadTrustRoots` parser hardening (WR-01). Four of the five are **additive** — extend existing seams cleanly without architectural change. Only WR-01 forces a real architectural decision (parser choice), and that decision **must be made before any phase starts**, not deferred into one.

The single most important finding across all four research files is a **load-bearing ordering constraint**: the registry today returns raw `IDevice*` from `unique_ptr<IDevice>` slots. The moment v1.1 introduces its first consumer that holds an `IDevice*` across an event-loop turn (the time-sync 300 ms debounced auto-sync), use-after-free under disconnect-during-use becomes essentially guaranteed. **Hot-plug hardening must land the shared-ownership change before time-sync wires the auto-sync hook**, or the milestone ships a crash. Three other pitfalls reinforce this same ordering: `dynamic_cast<IClockCapable*>` null-handling (Pitfall 2), the QML_SINGLETON dual-instance trap re-emerging on the new `TimeSyncService` (Pitfall 4), and toast-flood under USB-hub churn (Pitfall 3) which couples hot-plug coalescing to time-sync UI behaviour.

The risk profile is well-understood and bounded: every Win32 surface needs a Windows CI job (the canonical v1.0 lesson — CR-01 itself was the example), the `IDevice::capabilities()` method referenced in the time-sync design doc **does not exist** in the working tree (use the existing `hasX` static flag + `dynamic_cast` pattern instead — see Architecture Anti-pattern 1), and `nlohmann::json` vs custom 5-state scanner for WR-01 is a real architectural tradeoff (STACK + ARCHITECTURE recommend nlohmann; PITFALLS recommends in-tree scanner to minimise trust surface — flagged below as a Phase-0 decision).

______________________________________________________________________

## Key Findings

### Recommended Stack

**No core stack changes.** v1.0's stack (C++20, Qt 6.7+, QML 6, hidapi 0.14.0, Python 3.11+, Catch2 v3.7.1, CMake + Ninja) is unchanged. Four orthogonal **delta** additions, scoped narrowly:

**New additions:**

- **`nlohmann::json` 3.12.0** (single-header, MIT, FetchContent) — JSON parser for `loadTrustRoots`. **PRIVATE link to `ajazz_plugins` only**; never appears in `ajazz_core` or in any header. Preserves the COD-031 Qt-free / dep-light spirit because the IPC parser in `wire_protocol.hpp` stays untouched — `loadTrustRoots` is a host-only path. **Note: PITFALLS disagrees with this choice — see "Material Divergence" below.**
- **In-tree hand-rolled HID mock** — no third-party mocking library. `DeviceRegistry` gets a constructor-injectable `std::function<std::set<HidKey>()>` enumerator (defaults to real `hid_enumerate`). `HotplugMonitor` gets a test-only `injectEvent` shim behind `#ifdef AJAZZ_TESTING`. Mirrors the established `FakeAsyncExecutor` precedent at `tests/unit/test_action_engine.cpp:119`.
- **Win32 env block** — pure `processthreadsapi.h` API, no library. `GetEnvironmentStringsW` snapshot + per-spawn UTF-16 buffer + `CREATE_UNICODE_ENVIRONMENT` flag on `CreateProcessW`. ~40 LoC self-contained.
- **`HotplugMonitor::injectEvent`** — test-only synthetic event dispatch behind `#ifdef AJAZZ_TESTING`. Option C from ARCHITECTURE Q2.

**Deferred (do NOT add in v1.1):** Trompeloeil / FakeIt, simdjson / RapidJSON, Boost.Process, `QJsonDocument` in `ajazz_plugins` (crosses COD-031), generic mocking frameworks, HTTP clients, vendor-driver bundling.

See [STACK.md](STACK.md) for full version verification and CMake integration sketches.

### Expected Features

Three feature categories with explicit table-stakes / differentiators / anti-features. Compact view for requirements scoping:

| Category                     | Table Stakes (must-have)                                                                                                                                                            | Differentiators (P2)                                                                                                                                                                                                         | Anti-Features (explicitly NOT building)                                                                                                                                                                                |
| ---------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Hot-plug UX**              | Silent auto-reconnect; offline badge on sidebar row; non-blocking toast; stable lexicographic sort by `(deviceClass, codename)`; focus retention on disconnect; debounced reconnect | "Last seen" tooltip; reconnect-counter health badge; multi-device test harness as reusable fixture; per-device auto-rebind opt-out toggle                                                                                    | Modal "Device disconnected, retry?" dialog; auto-retry loop hammering `hid_open`; prompt-user-which-profile on reconnect; sidebar reorder by recency; removal sound; full QML scene reload                             |
| **Time-sync UX**             | Per-device "Sync now" button gated on `hasClock`; capability-gated UI hiding; honest `NotImplemented` reporting (exclamation glyph + tooltip, NOT a success toast)                  | Global "auto-sync on connect" toggle (`QSettings`-persisted, 300 ms debounce); UTC at the interface boundary                                                                                                                 | Interval-based re-sync timer; device→host time read-back; per-device timezone offset; world-clock / NTP client; sync-history log surface; render-time-on-keyface clock widget; "Time synced" toast on `NotImplemented` |
| **Scaffolded-device wiring** | Catalogued/recognised/maturity-honest device list; per-backend honest capability advertisement; stubs return `NotImplemented` not crash/lie                                         | Documented maturation tiers (Scaffolded → Probed → Partial → Functional → Verified) in `docs/_data/devices.yaml`; per-family inline "what works / what doesn't" tooltip; reverse-eng artefacts committed; runtime probe hook | Big-bang "promote all 7" in this milestone; vendor-app HID replay (legally murky); marketing "supported" without qualifiers; speculative descriptor entries; telemetry; vendor-driver bundling/wine'd installer        |

**P1 must-haves for v1.1 launch:** silent auto-reconnect + offline badge + stable sort + focus retention; multi-device baseline test harness; disconnect/reconnect non-blocking toast; time-sync five-layer scaffolding; maturity-tier YAML field + README regeneration; CR-01 Win32 env fix; WR-01 parser hardening + architectural decision.

**P2 (v1.1.x patch series):** promote 1-2 scaffolded stream-dock siblings Tier 0 → Tier 2; debounced reconnect with counter badge; per-device auto-rebind toggle.

**P3 (v1.2+):** real `IClockCapable::setTime` wire formats (blocked on firmware); `IDevice::probe()` hook; Tier 2 → Tier 4 promotion of any device; Clock Widget (image-upload render); device→host time read-back.

See [FEATURES.md](FEATURES.md) for full prioritization matrix and competitor analysis.

### Architecture Approach

The existing 3-layer architecture is sound; v1.1 work is overwhelmingly additive. Most important correction:

**`IDevice::capabilities()` does not exist in the working tree.** The time-sync design doc and plan both reference it; the actual runtime dispatch is `dynamic_cast<I*Capable*>(IDevice*)` paired with **static** `DeviceDescriptor.hasRgb` / `hasTouchStrip` flags for UI gating. Time-sync should mirror that exact pattern: add `DeviceDescriptor.hasClock`, never invent `capabilities()`.

**Major components and their v1.1 changes:**

1. **`src/core/`** (libajazz-core) — additive: new `IClockCapable` mix-in next to `IFirmwareCapable`; new `DeviceDescriptor.hasClock` field; `HotplugMonitor` gets test-only `injectEvent` shim; `DeviceRegistry` gets constructor-injectable enumerator AND **must migrate from `unique_ptr<IDevice>` to `std::shared_ptr<IDevice>` slot ownership** to fix UAF risk.
1. **`src/devices/`** — additive: 4 backends add `IClockCapable` mix-in with `Result::NotImplemented` stub; selected `register.cpp` rows set `.hasClock = true`. Pick 1-2 scaffolded stream-dock siblings of AKP153/AKP03/AKP05 for Tier 0 → Tier 2 promotion.
1. **`src/app/`** — additive: new `TimeSyncService` QML_SINGLETON (must follow `static_assert(!is_default_constructible_v<T>)` pattern); new `DeviceModel` roles (`HasClockRole`, `ConnectionStateRole`, `LastSeenRole`, `MaturityRole`) in single PR; `Application::onHotplug` adds coalescing debounce + time-sync fan-out branch.
1. **`src/plugins/`** — CR-01: single-file replacement of three `_putenv_s` calls. WR-01: parser replacement in lockstep across `manifest_signer.cpp` + `manifest_signer_win32.cpp` (verbatim mirror — drift re-introduces WR-01).

See [ARCHITECTURE.md](ARCHITECTURE.md) for full anchor-file map and data-flow diagrams.

### Critical Pitfalls

Top 5 of 16 pitfalls. Full taxonomy + per-phase mapping in [PITFALLS.md](PITFALLS.md).

1. **`IDevice` use-after-free during disconnect-while-in-use** (Pitfall 1, Phase 4) — `unique_ptr<IDevice>` slots can `reset()` between `dynamic_cast` and `setTime` once 300 ms debounced auto-sync lands. **Migrate to `shared_ptr<IDevice>` registry slots before time-sync wires the auto-sync hook.** Load-bearing ordering constraint.
1. **`dynamic_cast<IClockCapable*>` returns `nullptr` for device that just disappeared** (Pitfall 2, Phase 5) — `hasClock` is static, not a connectedness check. Every `dynamic_cast<I*Capable*>` site needs null-check within 3 lines (grep-able). VID/PID equality is NOT unique-identity after reconnect — use `HotplugEvent::serial`.
1. **QML_SINGLETON dual-instantiation strikes `TimeSyncService`** (Pitfall 4, Phase 5) — Qt 6 SFINAE picks default-construction over `create(QQmlEngine*, QJSEngine*)`. v1.0 sweep (`e221b21`) added `static_assert(!std::is_default_constructible_v<T>)` to 9 services. **`TimeSyncService` must be added; the static_assert IS the build-break check.**
1. **Win32 env-block UTF-16 missing second NUL terminator** (Pitfall 5, Phase 6) — `CreateProcessW` reads past buffer end looking for `\0\0`. Also: don't filter `=`-prefixed drive-letter entries; sort case-insensitively; delete three `_putenv_s` calls atomically in same commit (Pitfall 6 — "belt and braces" defeats the fix).
1. **Toast-flood / stuck-toast during USB-hub shuffle** (Pitfall 3, Phases 4+5) — udev delivers per-USB-interface arrivals. Coalesce hot-plug events for `(vid, pid, serial)` tuples with 250-500 ms trailing-edge debounce **before** any consumer sees them; auto-sync failures log WARN + per-row glyph only.

**Cross-cutting:** Three v1.1 features re-create the v1.0 "silently passes CI on Linux while broken on Windows" risk (CR-01 env block; hot-plug Win32 `WM_DEVICECHANGE` thread affinity; HID Report ID byte 0 platform differences). Every phase touching `*_win32.cpp` needs a Windows CI job — non-negotiable.

______________________________________________________________________

## Material Divergence Between Research Files (Phase-0 Decision)

**WR-01 parser choice — STACK + ARCHITECTURE vs PITFALLS:**

| File                | Recommendation                                                                            | Argument                                                                                                                                                      |
| ------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **STACK.md**        | `nlohmann::json` 3.12.0 PRIVATE-linked to `ajazz_plugins`                                 | Mature, MIT, FetchContent like hidapi, no Qt entanglement. WR-01 burned twice via mini-grep parser — don't reinvent JSON.                                     |
| **ARCHITECTURE.md** | Same — Option A (`nlohmann::json` single-header)                                          | Eliminates parser bug class; gives a real JSON parser ready for next use. ~150ms compile time per TU.                                                         |
| **PITFALLS.md**     | **Custom 5-state scanner** (~80-100 LoC in `wire_protocol.hpp` next to `findStringField`) | Adding a JSON parser **widens the trusted parsing surface inside the plugin sandbox boundary** — the opposite of the WR-01 goal. Fully fuzzable. No new deps. |

This is exactly the kind of architectural-decision-before-implementation call FEATURES.md also flagged. **Decision must be made in a Phase 3 (architectural decisions) before any WR-01 code lands.** Both options are defensible; the call hinges on threat-model framing (is `trust_roots.json` parsing INSIDE or OUTSIDE the security boundary?). Both options are reversible later, but mid-implementation change cost > 1-day decision-doc phase up front.

______________________________________________________________________

## Implications for Roadmap

v1.0 ended at Phase 2 — v1.1 phases start at **Phase 3**. Six suggested phases.

### Phase 3: Architectural Decisions (gating)

**Rationale:** Two architectural decisions gate the milestone; cheap 1-day decision-doc phase.

**Delivers:** Phase-plan docs recording (a) WR-01 parser choice with written rationale, (b) `HotplugMonitor` mock-seam choice (STACK + ARCHITECTURE lean Option C, test-only `injectEvent` shim), (c) confirmation of `unique_ptr<IDevice>` → `shared_ptr<IDevice>` registry migration for disconnect-during-use safety.

**Avoids:** Pitfall 1 (locked in by ownership decision); Pitfall 16 (dep-creep on WR-01).

### Phase 4: Hot-plug Hardening (foundational)

**Rationale:** Fixes the load-bearing ownership constraint that Phase 5 depends on. Lands the mock seam + coalescing debounce time-sync auto-sync will consume. Multi-device baseline test harness is an explicit PROJECT.md goal.

**Delivers:** `shared_ptr<IDevice>` registry migration (closes Pitfall 1); `DeviceRegistry::HidEnumerator` injection + `MockHidEnumerator` test double; `HotplugMonitor::injectEvent` shim; coalescing debounce in `Application::onHotplug` for `(vid, pid, serial)` (closes Pitfalls 3, 15); new `DeviceModel` roles (`ConnectionStateRole`, `LastSeenRole`); stable lexicographic sort; multi-device integration test; QML sidebar offline-badge + focus-retention + non-blocking toast; Windows CI smoke run for `WM_DEVICECHANGE` path.

**Avoids:** Pitfalls 1, 2 (precondition), 3, 11, 15.

### Phase 5: Time-Sync Five-Layer Scaffolding

**Rationale:** Plan already written (`docs/superpowers/plans/2026-05-13-time-sync.md`, 8 atomic tasks). **First active user of `dynamic_cast<I*Capable*>` dispatch.** Depends on Phase 4 for UAF safety + clean coalesced event stream.

**Delivers:** `IClockCapable` mix-in; `DeviceDescriptor.hasClock` (NOT a new `IDevice::capabilities()` method — avoid Architecture Anti-pattern 1); 4 backend stubs returning `Result::NotImplemented` (gated with `std::once_flag` per Pitfall 14); `TimeSyncService` QML_SINGLETON with `static_assert(!is_default_constructible_v<TimeSyncService>)` (closes Pitfall 4); `DeviceModel::HasClockRole`; Settings page global auto-sync toggle with "**to device**" labels (Pitfall 12); persisted-setting load-time validation (Pitfall 13); 300 ms debounced auto-sync with capability + connectedness re-validated at firing time (Pitfall 2); honest `NotImplemented` UI (exclamation glyph + tooltip, never success toast); `static_assert` bit-numbering lock.

**Avoids:** Pitfalls 2, 4, 12, 13, 14.

### Phase 6: CR-01 Win32 OOP Env Pollution Fix

**Rationale:** Single-file, ~20-line change. Independent of every other phase. Gated only by Windows CI exercising OOP host child spawn.

**Delivers:** Per-spawn UTF-16 env block from `GetEnvironmentStringsW` + 3 Python overrides + `\0\0` block terminator + case-insensitive sort + preserved `=`-prefixed drive-letter entries; `CREATE_UNICODE_ENVIRONMENT` flag; **atomic deletion** of all three `_putenv_s` calls in same commit (Pitfall 6); Windows CI job with Python entrypoint printing `os.environ['PYTHONPATH']`; parent-pollution unit test.

**Avoids:** Pitfalls 5, 6; cross-cutting "Linux CI silently green" risk.

### Phase 7: WR-01 `loadTrustRoots` Parser Hardening

**Rationale:** Implementation of Phase 3 decision. Two TUs change in lockstep.

**Delivers:** Replacement parser per Phase 3; hard byte-cap (1 MB fail-closed, Pitfall 7); entry-count cap (1024 entries, Pitfall 7); public API header doc-comment naming 0600 permissions assumption (Pitfall 8); fuzz corpus run \<1s on 100 KB inputs; all existing `test_manifest_signer.cpp` cases stay green + new BOM / escape / nested / NUL cases; if `nlohmann::json` chosen: `vcpkg.json` + CMakeLists + CI pin manifests + COD-031 charter update in same commit.

**Avoids:** Pitfalls 7, 8, 16.

### Phase 8: Scaffolded-Device Wiring (opportunistic)

**Rationale:** Independent feature track; each device is its own protocol-RE task. Recommendation: promote 1-2 scaffolded stream-dock-family devices Tier 0 → Tier 2 (Partial), NOT all 7, NOT Tier 4. AKB980 PRO is NOT a v1.1 candidate.

**Delivers:** `maturity` field in `docs/_data/devices.yaml`; `MaturityRole`; README + per-family `docs/protocols/<family>/<device>.md`; 1-2 backend promotions; capability-honesty audit; per-device protocol .md documents byte-0 Report ID convention (Pitfall 9); multi-byte protocol fields written byte-wise + clang-tidy `cppcoreguidelines-pro-type-reinterpret-cast` passes (Pitfall 10).

**Avoids:** Pitfalls 9, 10; cross-cutting Windows-blind risk.

### Phase Ordering Rationale

- **Phase 3 first** because both architectural decisions gate downstream phases.
- **Phase 4 (hot-plug) before Phase 5 (time-sync)** because the `unique_ptr` → `shared_ptr` registry migration is the precondition for safe `dynamic_cast<IClockCapable*>` use across event-loop turns.
- **Phase 5 (time-sync) before Phase 8 (device wiring)** because time-sync's 4 backend stubs land in every existing functional backend; Phase 8 should sit on a stable `IClockCapable` interface.
- **Phases 6 (CR-01) and 7 (WR-01) parallel-independent** of each other and of 4/5/8.
- **Phase 8 last and optional** — highest variance, no other phase depends on its output.

Phase 4 + Phase 5 form the **critical path**. Phases 6, 7, 8 are parallelisable side branches.

### Research Flags

**Needs research during planning:**

- **Phase 8 (Scaffolded-device wiring):** Each promoted device is clean-room protocol-RE. Per-device candidate selection during phase planning.
- **Phase 6 (CR-01):** Windows CI runner availability + duplicate-key precedence question (first-wins vs last-wins on inherited `PYTHONPATH`) need Windows smoke test before implementation.

**Standard patterns (skip `/gsd-research-phase`):** Phases 3, 4, 5, 7.

______________________________________________________________________

## Confidence Assessment

| Area         | Confidence      | Notes                                                                                                                                                                                                                       |
| ------------ | --------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Stack        | **HIGH**        | nlohmann::json 3.12.0 verified against upstream releases + integration docs. In-tree mock verified by direct reading of `device_registry.cpp:71-85`. Win32 env-block recipe verified against Microsoft Learn + nullprogram. |
| Features     | **MEDIUM-HIGH** | Competitor analysis HIGH (5 OSS apps + Elgato + Loupedeck + Companion cross-referenced). AJAZZ vendor app feature-mapping LOW. All P1s map to PROJECT.md v1.1 goals.                                                        |
| Architecture | **HIGH**        | All claims verified against working tree at HEAD (`7201758`). Anchor-file map provides line-range citations. Material correction (`IDevice::capabilities()` doesn't exist) grep-verified across all of `src/`.              |
| Pitfalls     | **HIGH**        | 13 of 16 pitfalls repo-anchored to specific file:line. Win32 / hidapi behavioural claims verified against Microsoft Learn + libusb/hidapi authoritative sources.                                                            |

**Overall:** HIGH for proceeding to roadmap creation.

### Gaps to Address

- **WR-01 parser choice (nlohmann vs scanner) — Phase 3 decision.** Both defensible. Threat-model framing decides.
- **`HotplugMonitor` mock seam (A/B/C) — Phase 3 decision.** STACK + ARCHITECTURE both lean Option C for v1.1 with Option A flagged as long-term shape.
- **Win32 duplicate-env-key precedence — Phase 6 prerequisite.** MS docs vs nullprogram disagree. Resolve via CI smoke test before declaring CR-01 fixed.
- **Phase 8 device-candidate selection — defer to phase planning.** Recommendation: stream-dock family siblings of AKP153 first.
- **Windows CI capacity — Phases 4, 6, 8 all need Windows CI exercise.** Confirm Windows runner is in matrix and exercises OOP host child spawn (not just compile).
- **AJAZZ vendor app feature mapping — LOW confidence.** Not a blocker for v1.1; defer to v1.2 research.

______________________________________________________________________

## Sources

**Primary (HIGH):** [nlohmann/json releases](https://github.com/nlohmann/json/releases), [integration guide](https://json.nlohmann.me/integration/); [Microsoft Learn: CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw), [Environment Variables](https://learn.microsoft.com/en-us/windows/win32/procthread/environment-variables); [libusb/hidapi hidapi.h](https://github.com/libusb/hidapi/blob/master/hidapi/hidapi.h); [Singletons in QML — Qt 6](https://doc.qt.io/qt-6/qml-singleton.html); Elgato Stream Deck docs; streamdeck-linux-gui, StreamController, OpenDeck, Boatswain; Companion issues #1564/#2735/#2795; [Den Delimarsky reverse-eng](https://den.dev/blog/reverse-engineering-stream-deck/) + [Plus](https://den.dev/blog/reverse-engineer-stream-deck-plus/), [cliffrowley HID gist](https://gist.github.com/cliffrowley/d18a9c4569537b195f2b1eb6c68469e0); NN/g, LogRocket, Carbon, Microsoft toast UX.

**Secondary (MEDIUM):** [nullprogram Win32 env blocks](https://nullprogram.com/blog/2023/08/23/); [Loupedeck support](https://support.loupedeck.com/device-not-connecting); [rapidjson](https://github.com/Tencent/rapidjson/releases), [simdjson](https://github.com/simdjson/simdjson/releases).

**Internal:** `.planning/PROJECT.md`; `.planning/milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md`; `docs/superpowers/specs/2026-05-13-time-sync-design.md`; `docs/superpowers/plans/2026-05-13-time-sync.md`; commits `d5616ef`, `e221b21`, `d7f932f`; user-memory `reference_qt_qml_gotchas.md`, `feedback_no_system_mutations.md`, `project_wire_format_convention.md`.

**In-repo verification anchors:** `src/core/include/ajazz/core/capabilities.hpp:31-48`; `device.hpp:52-70, 112-170`; `hotplug_monitor.{hpp,cpp}`; `device_registry.cpp:71-85`; `hid_transport.cpp:70, 86, 109`; `application.{hpp,cpp}`; `branding_service.hpp:60-71, 166-171` (canonical static_assert pattern); `device_model.cpp:79-101`; `register.cpp` (all 3 device libs); `manifest_signer.cpp:102-153`; `manifest_signer_win32.cpp:115-158`; `out_of_process_plugin_host_win32.cpp:451-468, 540-560`; `wire_protocol.hpp:22-31, 182`; `test_manifest_signer.cpp:226-326`; `test_action_engine.cpp:119`.

______________________________________________________________________

*Research completed: 2026-05-13*
*Ready for roadmap: yes — v1.1 phase decomposition starts at Phase 3 (v1.0 ended at Phase 2)*
