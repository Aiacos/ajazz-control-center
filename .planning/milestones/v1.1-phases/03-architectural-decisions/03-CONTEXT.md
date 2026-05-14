---
phase: 3
phase_slug: architectural-decisions
gathered: 2026-05-14
status: Ready for planning
mode: autonomous-interactive
---

# Phase 3: Architectural Decisions — Context

**Gathered:** 2026-05-14 via `/gsd-autonomous --from 3 --to 4 --interactive`
**Source for recommendations:** `.planning/research/SUMMARY.md` (synthesis of 4-dimension research) + `.planning/research/STACK.md` / `ARCHITECTURE.md` / `PITFALLS.md` from milestone bootstrap.

<domain>
## Phase Boundary

Three written architectural decisions are recorded that unblock downstream phases — WR-01 parser choice (ARCH-01), HotplugMonitor mock seam (ARCH-02), and DeviceRegistry ownership migration scope (ARCH-03). No code lands in this phase — only decision artefacts under `.planning/phases/03-architectural-decisions/`.

Maps to requirements: ARCH-01, ARCH-02, ARCH-03 (full text in `.planning/REQUIREMENTS.md`).

</domain>

<decisions>
## Implementation Decisions (locked)

### ARCH-01 — WR-01 trust-roots parser choice

**Decision:** `nlohmann::json` 3.12.0, single-header, vendored via `FetchContent`, **PRIVATE-linked to `ajazz_plugins` only** (never appears in `ajazz_core` or any header).

**Threat-model framing chosen (locked):** `trust_roots.json` parsing sits **inside the sandbox boundary** — the host parses the trust list to decide which plugin manifests to load, before any plugin code runs. A mature, well-fuzzed JSON parser is the right primitive; the alternative (custom 5-state scanner) would reinvent JSON parsing inside the security-critical path and inherit its own bug-velocity (the WR-01 partial-fix history already shows this risk on the mini-grep parser).

**What this commits Phase 7 to:**

- `vcpkg.json` + `CMakeLists.txt` + CI pin manifests + COD-031 charter update **in the same commit** as the parser swap.
- PRIVATE linkage check (`target_link_libraries(ajazz_plugins PRIVATE nlohmann_json::nlohmann_json)`) verified — nlohmann must not leak into `ajazz_core` or any installed public header.
- `loadTrustRoots` rewritten to use `nlohmann::json::parse` with explicit byte-cap (1 MB) and entry-count cap (1024 entries) — see TRUST-02.
- The mini-grep parser is **fully removed** from BOTH `manifest_signer.cpp` AND `manifest_signer_win32.cpp` in the same commit (drift between the two TUs is what re-introduces WR-01 — see Phase 7 SC1).

**Alternatives rejected:**

- In-tree 5-state scanner (PITFALLS recommendation) — would minimise dep surface but adds a hand-written parser to the security-critical host path. Threat-model framing of "trust_roots is inside the sandbox" makes the JSON parser an appropriate primitive rather than an attack surface widener.
- simdjson, RapidJSON, RapidYAML — each rejected for use-case mismatch / upstream-maintenance status / wrong format (see STACK.md §3).

### ARCH-02 — HotplugMonitor mock-seam approach

**Decision:** Test-only `HotplugMonitor::injectEvent(HotplugEvent const&)` shim guarded by `#ifdef AJAZZ_TESTING`.

**Rationale (locked):** This is the cheapest seam that meets Phase 4's needs (multi-device integration tests + Windows CI smoke for `WM_DEVICECHANGE`). Promoting `HotplugMonitor::runImpl()` to `protected virtual` would add a virtual call per real hot-plug for one test-only override; extracting a full `IHotplugSource` interface would over-design for a single use case. The `injectEvent` shim costs ~10 LoC, is invisible to production builds, and mirrors the existing `FakeAsyncExecutor` precedent at `tests/unit/test_action_engine.cpp:119`.

**What this commits Phase 4 to:**

- `HotplugMonitor::injectEvent(HotplugEvent const&)` declared + implemented behind `#ifdef AJAZZ_TESTING` in `src/core/include/ajazz/core/hotplug_monitor.hpp` + `src/core/src/hotplug_monitor.cpp`.
- The shim synthesises a `HotplugEvent` into the same delivery pipeline as a real udev / `WM_DEVICECHANGE` / IOKit event — emits via the existing internal signal so all subscribers see it indistinguishably.
- The constructor-injectable `HidEnumerator = std::function<std::set<HidKey>()>` on `DeviceRegistry` (Phase 4 separate work) is the other half: it lets `MockHidEnumerator` drive the synthetic devices the injected events refer to.

**Long-term shape (not in this phase):** If a future requirement surfaces multiple `HotplugMonitor` implementations (e.g. a Bluetooth-side enumerator), promote `injectEvent` from test-only to a public `IHotplugSource` boundary. ARCH-02 records this as a possible v1.2+ evolution, not a current commitment.

**Alternatives rejected:**

- `protected virtual runImpl()` — adds a virtual call per real event for a test-only override. Over-engineering.
- Full `IHotplugSource` interface extraction — substantially more code change for a single use case in v1.1.

### ARCH-03 — DeviceRegistry slot ownership migration

**Decision:** `DeviceRegistry` slot ownership migrates from `std::unique_ptr<IDevice>` to `std::shared_ptr<IDevice>` **in Phase 4**, before Phase 5 (time-sync) wires the 300 ms debounced auto-sync.

**Acknowledgement (locked):** No code consumer of v1.1 outside Phase 4 holds an `IDevice*` across an event-loop turn until the migration ships. This is the load-bearing ordering constraint flagged in research SUMMARY §1 (Executive Summary) and PITFALLS §1 (Pitfall 1: use-after-free during disconnect-while-in-use). The time-sync 300 ms debounce is the first consumer that would trigger the UAF if ownership stays unique_ptr.

**What this commits Phase 4 to:**

- All `DeviceRegistry::registerDevice(std::unique_ptr<IDevice>)` call sites migrate to `std::shared_ptr<IDevice>` — touches every device backend factory (`register.cpp` in `src/devices/streamdeck`, `src/devices/keyboard`, `src/devices/mouse`).
- All `IDevice*` raw-pointer escapes from the registry (e.g. `DeviceRegistry::deviceFor(codename)`) return `std::shared_ptr<IDevice>` (or `std::weak_ptr<IDevice>` where appropriate).
- Existing callers that store `IDevice*` for any duration beyond a single function call are audited and migrated to `shared_ptr` capture.
- Hot-plug device removal (`reset()`) decrements the registry's `shared_ptr`; consumers holding their own `shared_ptr` keep the device alive until their last reference releases — eliminating the UAF window.

**Risk window during migration:** The migration commit itself must be atomic — interleaved `unique_ptr` / `shared_ptr` in different TUs would break compilation. ARCH-03 commits to landing the migration as a **single coordinated commit** across `src/core/`, `src/devices/streamdeck/`, `src/devices/keyboard/`, `src/devices/mouse/`, and any `src/app/` consumer that holds a raw `IDevice*`.

### Claude's Discretion

For decisions not raised explicitly above (file layout under `.planning/phases/03-architectural-decisions/`, naming conventions for the per-decision artefact files, citation depth back to research SUMMARY.md), Claude can pick reasonable defaults that match the existing project conventions.

</decisions>

<specifics>
## Specific Ideas / Anchor Artefacts

- Each decision lands as its own per-decision markdown artefact under `.planning/phases/03-architectural-decisions/`: e.g. `ARCH-01-parser-choice.md`, `ARCH-02-mock-seam.md`, `ARCH-03-ownership-migration.md`.
- Each artefact follows a consistent structure: Decision (1-line), Threat-model framing / Rationale, What this commits downstream phases to, Alternatives rejected, References.
- The phase PLAN.md (written by gsd-planner) decomposes "write the 3 artefacts + update REQUIREMENTS.md to mark ARCH-01..03 as Locked" into 1-3 tasks.

</specifics>

\<canonical_refs>

## Canonical References

- `.planning/research/SUMMARY.md` — material divergence on ARCH-01 surfaced here (STACK + ARCH vs PITFALLS).
- `.planning/research/STACK.md` §3 — nlohmann::json 3.12.0 versioning + CMake integration sketches.
- `.planning/research/ARCHITECTURE.md` Q2 — HotplugMonitor mock-seam options (A/B/C).
- `.planning/research/PITFALLS.md` Pitfall 1 — UAF risk that drives ARCH-03.
- `.planning/REQUIREMENTS.md` — ARCH-01, ARCH-02, ARCH-03 verbatim.
- `.planning/ROADMAP.md` Phase 3 success criteria — the artefact contracts these decisions ratify.

\</canonical_refs>
