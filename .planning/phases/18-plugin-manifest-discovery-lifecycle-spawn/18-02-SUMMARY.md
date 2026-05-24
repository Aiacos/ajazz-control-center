---
phase: 18-plugin-manifest-discovery-lifecycle-spawn
plan: '02'
subsystem: node-runner
tags: [node-runner, node-detection, plugin-spawn, argv-builder, plugin-08]
dependency_graph:
  requires: [18-01]
  provides: [buildNodeArgv, NodeProbe, resolveNode20Plus, makeDefaultNodeProbe]
  affects: [18-04-plugin-manager]
tech_stack:
  added: []
  patterns:
    - pure-function-builder (buildNodeArgv; returns QStringList with no side effects)
    - injectable-probe-pattern (NodeProbe std::function members; fake-able in tests)
    - QVersionNumber-version-gate (strips leading v, trims whitespace, majorVersion < 20 rejects)
    - QStandardPaths-findExecutable (cross-platform node PATH resolution; no manual PATH walk)
key_files:
  created:
    - src/app/src/node_runner.hpp
    - src/app/src/node_runner.cpp
    - tests/unit/test_node_runner.cpp
  modified:
    - src/app/CMakeLists.txt
    - tests/unit/CMakeLists.txt
decisions:
  - 'argv contract: codePath is argv[0], node binary is NOT in the list (Pitfall 3 from 18-RESEARCH.md); QProcess::start(nodeExe, buildNodeArgv(...)) is the caller contract'
  - 'NodeProbe defaults: struct holds uninitialised std::function members; callers either assign explicitly (tests) or call makeDefaultNodeProbe() (production) — no Qt runtime deps in header context'
  - "version parsing: strip leading 'v' with mid(1) then trimmed(); QVersionNumber::fromString handles 2.10 > 2.9 correctly unlike string compare"
  - 'anti-feature enforced: no bundled node20 anywhere; system node is a runtime probe; absence = plugin disabled, not a build error'
metrics:
  duration_minutes: 4
  completed_date: '2026-05-24'
  tasks_completed: 2
  files_created: 3
  files_modified: 2
  tests_added: 6
  test_suite_total: 467
---

# Phase 18 Plan 02: NodeRunner Argv Builder + Injectable Node Detection Summary

**One-liner:** Pure `buildNodeArgv()` emitting the exact akp_plugin_sdk.md §3 nine-token spawn CLI, plus injectable `resolveNode20Plus()` that version-gates system `node` >= 20 without ever bundling the runtime.

## What Was Built

Two free functions and a supporting probe struct in `src/app/src/node_runner.{hpp,cpp}`:

- `buildNodeArgv(codePath, port, pluginUuid, infoJson)` — returns a nine-element `QStringList` per akp_plugin_sdk.md §3:
  `{ codePath, "-port", portStr, "-pluginUUID", uuid, "-registerEvent", "registerPlugin", "-info", infoJson }`.
  `codePath` is `argv[0]`; the node binary is passed separately as the `program` argument to `QProcess::start`.
- `struct NodeProbe` — two `std::function` members (`findNode`, `queryVersion`), both unset by default so tests inject fakes and production calls `makeDefaultNodeProbe()`.
- `resolveNode20Plus(probe)` — calls `probe.findNode()` (empty = `std::nullopt`), then `probe.queryVersion(exe)`, strips the leading `'v'` and trims whitespace, parses with `QVersionNumber::fromString`, rejects `majorVersion() < 20`.
- `makeDefaultNodeProbe()` — wires `QStandardPaths::findExecutable("node")` + a 5-second `QProcess --version` query for production use. The manager (18-04) passes this probe to `resolveNode20Plus()`.

A six-case `NodeRunnerTest` suite covers:

1. `buildsCorrectArgv` — asserts all 9 tokens by index (codePath first, no node binary)
1. `detects node 20 plus via injected probe` — fake returning `v26.0.0\n` -> returns path
1. `rejects absent node` — fake returning `""` from `findNode` -> `std::nullopt`
1. `rejects node below 20` — fake returning `v18.20.0` -> `std::nullopt`
1. `version strip tolerates leading v and trailing newline` — `v20.0.0\n` is accepted (boundary)
1. `rejects node 19 boundary` — `v19.9.9` is rejected

No live node process is launched by any test case.

## Deviations from Plan

None - plan executed exactly as written.

## Key Decisions Made

1. **`argv[0]` = codePath, not node binary (Pitfall 3 enforced):** `buildNodeArgv` returns only the script arguments; the caller uses `QProcess::start(nodeExe, buildNodeArgv(...))`. This is asserted by index in `buildsCorrectArgv`.

1. **`NodeProbe` defaults are uninitialised:** The struct does not set default functors in the header — this avoids pulling `QStandardPaths` into every TU that includes `node_runner.hpp`. `makeDefaultNodeProbe()` is the production factory; tests assign lambdas directly. This is cleaner than the research example which inlined a default lambda in the struct definition.

1. **Version parsing strip:** `mid(1)` drops the leading `'v'`; `trimmed()` handles trailing `'\n'`. `QVersionNumber::fromString` then gives correct numeric comparison (e.g. `v20.0.0` accepted, `v19.9.9` rejected).

1. **Anti-feature respected:** No bundled node. The implementation uses `QStandardPaths::findExecutable("node")` exactly as mandated by CONTEXT.md.

## TDD Gate Compliance

- RED gate: commit `84661a8` `test(18-02)` — test file + CMakeLists wiring (tests would have failed without the implementation, confirmed by CMake configure error on missing `node_runner.cpp`).
- GREEN gate: commit `ead73fe` `feat(18-02)` — implementation files; all 6 NodeRunnerTest cases pass.
- REFACTOR: not required; implementation is minimal and already readable.

## Test Results

| Test                                                                  | Status |
| --------------------------------------------------------------------- | ------ |
| NodeRunnerTest buildsCorrectArgv                                      | PASS   |
| NodeRunnerTest detects node 20 plus via injected probe                | PASS   |
| NodeRunnerTest rejects absent node                                    | PASS   |
| NodeRunnerTest rejects node below 20                                  | PASS   |
| NodeRunnerTest version strip tolerates leading v and trailing newline | PASS   |
| NodeRunnerTest rejects node 19 boundary                               | PASS   |
| Full suite (467 tests)                                                | PASS   |

## COD-031 Verification

`grep -rn nlohmann src/app/src/node_runner.cpp src/app/src/node_runner.hpp` — only doc comment in .cpp ("No nlohmann::json (COD-031 boundary)"); zero functional includes or usage.
`grep -c 'registerPlugin' src/app/src/node_runner.cpp` = 2 (one in the comment, one as the string literal in `buildNodeArgv`).

## Known Stubs

None — `buildNodeArgv` and `resolveNode20Plus` are complete implementations. `makeDefaultNodeProbe` is fully wired. The plan's scope is entirely fulfilled.

## Threat Flags

T-18-ARGV (mitigate): `buildNodeArgv` returns a `QStringList`; the manager (18-04) passes it to `QProcess::start(program, args)` — never concatenated into a shell string. Hostile `codePath` or `pluginUuid` values cannot inject shell metacharacters because there is no shell invocation.

T-18-NODEVER (mitigate): `resolveNode20Plus` parses and rejects any version with `majorVersion < 20`; absent node returns `std::nullopt`; no bundled runtime.

## Self-Check: PASSED

Files exist:

- src/app/src/node_runner.hpp: EXISTS
- src/app/src/node_runner.cpp: EXISTS
- tests/unit/test_node_runner.cpp: EXISTS

Commits exist:

- 84661a8: test(18-02): add failing NodeRunnerTest suite for PLUGIN-08 argv + detection
- ead73fe: feat(18-02): implement buildNodeArgv + injectable resolveNode20Plus (PLUGIN-08)
