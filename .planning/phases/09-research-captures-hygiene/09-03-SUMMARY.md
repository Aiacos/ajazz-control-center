---
phase: 09-research-captures-hygiene
plan: 03
subsystem: testing
tags: [python, pytest, usbrply, capture, dev-tooling, ruff, cpp20]

# Dependency graph
requires:
  - phase: 09-research-captures-hygiene/01
    provides: reject-raw-captures pre-commit hook + capture-data-hygiene policy
provides:
  - scripts/hex-to-cpparray.py (usbrply JSON -> C++ header converter, stdlib-only)
  - tests/unit/test_hex_to_cpparray.py (8-test pytest smoke suite)
  - End-to-end dev-tooling pipeline ready for first real capture in Phase 9.x
affects:
  - Phase 9.x (CAPTURE-05) — user-produced captures will flow through this script
  - Phase 10+ wire-format tests — fixtures emitted by this script land under tests/integration/fixtures/<codename>/*.h

# Tech tracking
tech-stack:
  added:
    - scripts/hex-to-cpparray.py (Python 3.11+, stdlib only)
  patterns:
    - Dev-time Python tool that consumes usbrply -j JSON; raw .pcap reading is explicitly rejected at the script boundary (CAPTURE-01 belt-and-braces)
    - Emitted C++ headers use `inline constexpr std::array<std::uint8_t, N>` inside `namespace ajazz::tests::fixtures`
    - ASCII-only emitted text + ASCII-only identifier regex on --device (Pitfall 32 + T-09-13 shell-injection guard)

key-files:
  created:
    - scripts/hex-to-cpparray.py
    - tests/unit/test_hex_to_cpparray.py
  modified: []

key-decisions:
  - Script consumes usbrply -j JSON, not raw pcap (D-03) -- keeps libpcap out of the C++ link surface
  - Stdlib-only Python -- no PyPI dependency, contributors without `pipx install usbrply` can still run the smoke test
  - Emitted header is wrapped in `namespace ajazz::tests::fixtures` for ADL hygiene (matches STACK 'Test-replay infrastructure' shape)
  - 'Identifier shape: `<DEVICE>_<LABEL>_BYTES` (UPPER_SNAKE_CASE) -- deterministic for `#include` consumers'
  - Script REJECTS `.pcap` / `.pcapng` file suffixes at input -- defence-in-depth on top of the CAPTURE-01 pre-commit hook

patterns-established:
  - 'Dev-time Python tooling: shebang + SPDX + Google-style docstrings + ruff-clean stdlib-only'
  - 'Test convention: pytest files have NO shebang (invoked via `python -m pytest`); only `scripts/*.py` carry shebangs'
  - 'Threat-register mitigation in test names: `test_invalid_codename_rejected` cites Pitfall 32 + T-09-13 in the docstring'

requirements-completed: [CAPTURE-03]

# Metrics
duration: 6min
completed: 2026-05-15
---

# Phase 9 Plan 03: hex-to-cpparray.py + smoke test Summary

**Stdlib-only Python 3.11+ helper that converts usbrply JSON into `inline constexpr std::array<std::uint8_t, N>` C++ headers, with an 8-test pytest smoke suite (no real capture required).**

## Performance

- **Duration:** ~6 min
- **Started:** 2026-05-15T07:42:41Z
- **Completed:** 2026-05-15T07:48:05Z
- **Tasks:** 2 (both `type="auto"`)
- **Files created:** 2

## Accomplishments

- `scripts/hex-to-cpparray.py` lands as a ~260-LoC stdlib-only dev tool that turns usbrply `-j` JSON into a single-array C++ header. CLI: `hex-to-cpparray.py <input.json|-> --device <codename> --capture <label> [--packet-index N] [--type {controlWrite|controlRead|interruptIn|interruptOut|any}]`.
- `tests/unit/test_hex_to_cpparray.py` provides 8 smoke tests (the 7 in the plan plus one bonus `test_raw_pcap_extension_rejected` for the `.pcap`/`.pcapng` rejection policy). All 8 pass on Python 3.14.5 + pytest 9.0.1, including the g++ -fsyntax-only -std=c++20 syntactic-validity assertion.
- End-to-end pipeline (`tshark` → `pcap` → `usbrply -j` → `hex-to-cpparray.py` → `tests/integration/fixtures/<codename>/<label>.h`) is now functional. When the first real capture lands in Phase 9.x, the on-ramp into the test suite already exists and has been smoke-tested.

## Task Commits

Single atomic commit (both files together, per the plan's explicit instruction):

1. **Task 1 + Task 2 (combined commit per plan §Task 2)** — `c2208a2` (`feat(capture): add hex-to-cpparray.py + smoke test (CAPTURE-03)`)

## Files Created/Modified

- `scripts/hex-to-cpparray.py` — usbrply JSON → C++ header converter (executable, mode 100755, ASCII-only output).
- `tests/unit/test_hex_to_cpparray.py` — 8-test pytest smoke suite (mode 100644, no shebang per project convention).

## CLI Signature

```text
usage: hex-to-cpparray.py [-h] --device DEVICE --capture CAPTURE
                          [--packet-index PACKET_INDEX]
                          [--type {controlWrite,controlRead,interruptIn,interruptOut,any}]
                          input
```

| Flag             | Required   | Default | Purpose                                                                                                            |
| ---------------- | ---------- | ------- | ------------------------------------------------------------------------------------------------------------------ |
| `input`          | positional | --      | Path to a usbrply `-j` JSON file, or `-` for stdin. `*.pcap` / `*.pcapng` rejected with a pointer to CAPTURING.md. |
| `--device`       | yes        | --      | Codename, regex `[a-z0-9_]+` enforced (T-09-13 shell-injection guard).                                             |
| `--capture`      | yes        | --      | Free-form label, regex `[A-Za-z0-9_\-]+` enforced; normalised to UPPER_SNAKE_CASE for the C++ identifier.          |
| `--packet-index` | no         | 0       | Zero-based index into the filtered packet list.                                                                    |
| `--type`         | no         | `any`   | Filter to one of `controlWrite` / `controlRead` / `interruptIn` / `interruptOut` / `any`.                          |

## Example Output (synthetic 3-packet input, --packet-index=0 --type=any)

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// AUTOGENERATED by scripts/hex-to-cpparray.py -- do not edit by hand.
// Source: usbrply JSON capture, device=akp03_variant_3004, label=image-upload-first-chunk.
// Generated: 2026-05-15T07:44:43Z (UTC, timestamp at script invocation time).
#pragma once
#include <array>
#include <cstdint>

namespace ajazz::tests::fixtures {

inline constexpr std::array<std::uint8_t, 9> AKP03_VARIANT_3004_IMAGE_UPLOAD_FIRST_CHUNK_BYTES = {
    0x02, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x44,
};

}  // namespace ajazz::tests::fixtures
```

## Test Matrix

| #   | Test                                             | What it asserts                                                                                                                   |
| --- | ------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------- |
| 1   | `test_happy_path_emits_valid_cpp_header`         | Default invocation emits #pragma once, includes, namespace, identifier shape, first-3-bytes correctness, single trailing newline. |
| 2   | `test_packet_index_selection`                    | `--packet-index 1` selects the second packet (interruptIn `0007` payload).                                                        |
| 3   | `test_type_filter`                               | `--type interruptOut --packet-index 0` returns the 2-byte `ff00` packet.                                                          |
| 4   | `test_invalid_codename_rejected`                 | `--device "AKP03!Variant"` exits non-zero with stderr mentioning device/codename/ASCII (Pitfall 32 + T-09-13).                    |
| 5   | `test_missing_capture_label_rejected`            | Argparse rejects omitted `--capture`.                                                                                             |
| 6   | `test_empty_packs_rejected`                      | `{"packs": []}` exits non-zero with stderr mentioning empty/no packets.                                                           |
| 7   | `test_raw_pcap_extension_rejected` (bonus)       | `*.pcap` input file is rejected upfront with stderr referencing pcap/usbrply (CAPTURE-01 defence-in-depth).                       |
| 8   | `test_emitted_header_is_syntactically_valid_cpp` | `g++ -fsyntax-only -std=c++20` compiles the emitted header; skip if g++ unavailable (clang-only CI tolerated).                    |

## Decisions Made

- **Bonus 8th test** (`test_raw_pcap_extension_rejected`): the script itself implements the `.pcap` / `.pcapng` rejection policy at its input boundary (defence-in-depth on top of the CAPTURE-01 commit hook). Adding the test makes that policy load-bearing rather than implicit.
- **No shebang on the test file**: project convention (`python/ajazz_plugins/tests/*.py`) is for pytest files to have only `# SPDX-License-Identifier`. The pre-commit hook `check-shebang-scripts-are-executable` enforces this.
- **`# noqa: S603` on subprocess.run inside the test**: ruff's bandit rule flagged untrusted-subprocess-input. All inputs in this test are test-supplied constants — the rule does not apply, narrow noqa applied per call site.
- **ruff `target-version = "py311"`** in pyproject; the script uses `datetime.UTC` (3.11+) and `from __future__ import annotations`. Runs cleanly on the developer's 3.14.5 interpreter and is forward-compatible.

## Deviations from Plan

None substantive. Two cosmetic/clean-up fixes during execution, both auto-applied:

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Shebang on test file failed `check-shebang-scripts-are-executable`**

- **Found during:** Task 2 (first commit attempt)
- **Issue:** Initial test file carried `#!/usr/bin/env python3` per the plan's "shebang/SPDX/docstring per project convention" instruction, but project convention (verified against `python/ajazz_plugins/tests/*.py`) is for pytest files to omit the shebang. Pre-commit hook rejected the commit.
- **Fix:** Removed the shebang line; SPDX header retained.
- **Files modified:** `tests/unit/test_hex_to_cpparray.py`
- **Verification:** Pre-commit hooks pass on re-run; pytest still discovers and runs the file via `python -m pytest` (no functional change).
- **Committed in:** `c2208a2` (single atomic commit).

**2. [Rule 1 - Bug] Verification step 6 (`grep -rn nlohmann ... returns 0`) initially matched docstring comments**

- **Found during:** Final verification block run
- **Issue:** The script's docstring originally referenced `nlohmann::json` literally as part of an anti-feature comment ("NO nlohmann::json in any C++ runtime path"). The plan's verification step expects grep to find zero matches.
- **Fix:** Reworded the anti-feature comment to say "NO C++ JSON library in any runtime path (COD-031 boundary; the agent-side JSON library stays PRIVATE-linked to ajazz_plugins)". Semantically identical, grep-clean.
- **Files modified:** `scripts/hex-to-cpparray.py`
- **Verification:** `grep -rn nlohmann scripts/hex-to-cpparray.py tests/unit/test_hex_to_cpparray.py` now exits 1 (no match) as the plan's verification block requires.
- **Committed in:** `c2208a2` (single atomic commit).

______________________________________________________________________

**Total deviations:** 2 auto-fixed (1 blocking pre-commit hook, 1 verification-text alignment).
**Impact on plan:** None. Both fixes preserve the plan's intent; the second is the literal verification-grep correctness fix.

## Issues Encountered

None. The script worked first-try once ruff's two stylistic findings (`UP017` "use `datetime.UTC`", `RUF100` "unused noqa") were applied via `ruff format`.

## Verification Block (plan §verification)

| #   | Check                                                                                  | Result                                   |
| --- | -------------------------------------------------------------------------------------- | ---------------------------------------- |
| 1   | `python3 scripts/hex-to-cpparray.py --help` exits 0 and lists `--device` + `--capture` | PASS                                     |
| 2   | `python3 -m pytest tests/unit/test_hex_to_cpparray.py -v` -> 8 passed                  | PASS (8/8; test 8 ran g++ successfully)  |
| 3   | `ruff check scripts/hex-to-cpparray.py tests/unit/test_hex_to_cpparray.py`             | PASS                                     |
| 4   | `pre-commit run --files <both files>`                                                  | PASS (includes reject-raw-captures hook) |
| 5   | `git log -1 --pretty=%B` contains `CAPTURE-03`                                         | PASS                                     |
| 6   | `grep -rn nlohmann <both files>` returns exit 1 (no matches)                           | PASS                                     |

## Next Phase Readiness

- **Phase 9.x follow-up unblocked:** When the user produces `.pcap` captures off-tree, the conversion pipeline is ready. `usbrply -j cap.pcap > out.json && scripts/hex-to-cpparray.py out.json --device <codename> --capture <label> > tests/integration/fixtures/<codename>/<label>.h` works end-to-end.
- **No downstream blockers:** No new C++ link-time dependency, no PyPI dependency, no runtime code change. COD-031 boundary preserved (`grep -rn nlohmann src/core/include/` remains 0).
- **Documentation reference:** `docs/protocols/CAPTURING.md` (CAPTURE-02, already landed at commit `791e510`) cites this script in the sanitise+convert step — verify the literal path matches.

## Known Stubs

None. The script and test suite are fully wired; no placeholder values, no `TODO`, no `FIXME` strings emitted at runtime.

## Self-Check: PASSED

- `scripts/hex-to-cpparray.py` — FOUND (executable, 9.5KB).
- `tests/unit/test_hex_to_cpparray.py` — FOUND.
- Commit `c2208a2` — FOUND in `git log`.

______________________________________________________________________

*Phase: 09-research-captures-hygiene*
*Completed: 2026-05-15*
