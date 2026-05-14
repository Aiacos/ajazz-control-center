---
decision: ARCH-01
title: WR-01 trust-roots parser choice
status: Locked
locked: 2026-05-14
phase: 3
---

# ARCH-01: WR-01 trust-roots parser choice

**Status:** Locked 2026-05-14

## Decision

`nlohmann::json` 3.12.0, single-header, vendored via `FetchContent`, **PRIVATE-linked to `ajazz_plugins` only** (never appears in `ajazz_core` or any installed public header).

## Threat-model framing

`trust_roots.json` parsing sits **inside the sandbox boundary** — the host parses the trust list to decide which plugin manifests to load, before any plugin code runs. A mature, well-fuzzed JSON parser is the right primitive; the alternative (custom 5-state scanner) would reinvent JSON parsing inside the security-critical path and inherit its own bug-velocity (the WR-01 partial-fix history already shows this risk on the mini-grep parser).

## What this commits Phase 7 to

- `vcpkg.json` + `CMakeLists.txt` + CI pin manifests + COD-031 charter update **in the same commit** as the parser swap.
- PRIVATE linkage check (`target_link_libraries(ajazz_plugins PRIVATE nlohmann_json::nlohmann_json)`) verified — nlohmann must not leak into `ajazz_core` or any installed public header.
- `loadTrustRoots` rewritten to use `nlohmann::json::parse` with explicit byte-cap (1 MB) and entry-count cap (1024 entries) — see TRUST-02.
- The mini-grep parser is **fully removed** from BOTH `manifest_signer.cpp` AND `manifest_signer_win32.cpp` in the same commit (drift between the two TUs is what re-introduces WR-01 — see Phase 7 SC1).

## Alternatives rejected

- **In-tree 5-state scanner** (PITFALLS recommendation) — would minimise dep surface but adds a hand-written parser to the security-critical host path. Threat-model framing of "trust_roots is inside the sandbox" makes the JSON parser an appropriate primitive rather than an attack surface widener.
- **simdjson** — use-case mismatch (SIMD-optimised throughput parser for hot-path bulk JSON; trust-roots is tiny + cold).
- **RapidJSON** — upstream-maintenance status (sporadic releases, unclear bus factor).
- **RapidYAML** — wrong format (trust-roots is JSON, not YAML).

See `STACK.md §3` for the full per-candidate write-up.

## References

- `.planning/research/SUMMARY.md` — material divergence on parser choice (STACK + ARCH vs PITFALLS).
- `.planning/research/STACK.md §3` — `nlohmann::json` 3.12.0 versioning + CMake integration sketches.
- `.planning/REQUIREMENTS.md` ARCH-01 row + Phase 7 TRUST-01..04.
