# Phase 1 — Deferred Review Fixes

Findings from `01-REVIEW.md` that were not auto-applied because the
suggested fix is non-trivial (new code paths, race-prone alternatives,
or a missing dependency). Each entry explains why automation was
declined and the recommended path forward for human attention.

## CR-01 — Win32 OOP host pollutes parent environment

**File:** `src/plugins/src/out_of_process_plugin_host_win32.cpp:463-467`

**Why deferred:** The straightforward in-place fix is a snapshot/restore
RAII around the three `_putenv_s` calls — _almost_ correct but **race-prone
when more than one `OutOfProcessPluginHost` is constructed concurrently**,
which is the failure mode the reviewer specifically called out (cross-instance
pollution). Two concurrent constructors would interleave their snapshot/set
sequences and either leak the inner instance's values to the outer one, or
deadlock around a coarse-grained mutex that the file does not currently
own. The race window is small but real, and "fail-open under contention" is
exactly the regression a security-coded review wants flagged, not papered
over.

The reviewer's preferred fix — building a per-spawn UTF-16 environment
block via `GetEnvironmentStringsW` / `FreeEnvironmentStringsW`, appending
the three Python overrides, and passing it to `CreateProcessW` with
`CREATE_UNICODE_ENVIRONMENT` — is the only solution that is both correct
and concurrency-safe. It is also ~40-60 lines of new Win32-specific code
that touches the spawn call site, the cleanup path, and the
`CreateProcessAsUserW` branch (which currently passes `lpEnvironment = nullptr` and would need the new block too). Untestable on the Linux dev
box.

**Recommended path forward (pick one):**

1. **Per-spawn env block (preferred).** Implement a small UTF-16 env
   builder that takes the parent snapshot, applies the three overrides
   (`PYTHONPATH`, `PYTHONDONTWRITEBYTECODE`, `PYTHONUNBUFFERED`), and
   passes the result to both `CreateProcessW` and `CreateProcessAsUserW`
   along with `CREATE_UNICODE_ENVIRONMENT`. Free the snapshot via
   `FreeEnvironmentStringsW` when done. This brings the Win32 spawn
   hygiene up to parity with the POSIX `setenv-in-child` model.

1. **Document the limitation.** If the per-spawn env block is too much
   for the next slice, add a doc comment on the constructor warning
   that the host mutates `PYTHONPATH` / `PYTHONDONTWRITEBYTECODE` /
   `PYTHONUNBUFFERED` in the parent process, instantiating more than
   one host is unsupported on Windows, and any sibling subprocess
   (notably the manifest verifier in `manifest_signer_win32.cpp`)
   inherits the polluted environment. Add an instance-count guard
   (`std::atomic<int>` ctor counter) that throws if a second host is
   constructed while a first is still alive, so the unsupported state
   fails loudly instead of silently mispairing.

The reviewer characterised this as "the biggest single defect" of the
Win32 backend; whichever path is chosen, it should land before any CI
job actually exercises this code on Windows.

## WR-01 — `loadTrustRoots` parser robustness (partial fix only)

**File:** `src/plugins/src/manifest_signer.cpp:118-143`,
`src/plugins/src/manifest_signer_win32.cpp:128-149`

**Status:** **Partial fix applied** in commit
`fix(plugins): trust-roots accept name-before-key JSON object order (REVIEW WR-01)`.

**What was applied:** The cheap window-widening fix the reviewer
suggested as the first option — walk backwards from `keyPos` to the
nearest `{` so the search window covers the whole entry body. Handles
the reverse-order case the original mini-grep miscounted. New Catch2
case `loadTrustRoots: name-before-key entry resolves to a row` pins
the contract.

**What was deferred:** The reviewer's preferred fix — bite the bullet
and write a real five-state JSON object scanner, or replace the
mini-grep entirely with `nlohmann::json` / `QJsonDocument`. Both
options are non-trivial:

- **`nlohmann::json`** — not currently a project dependency. Adding
  it would touch `vcpkg.json` (or the equivalent FetchContent block),
  the `ajazz_plugins/CMakeLists.txt` link list, and likely the
  CI pin manifests. Out of scope for an automated REVIEW fix.
- **`QJsonDocument`** — already used at the app layer, but
  `ajazz_plugins` is intentionally Qt-free per `COD-031` (the public
  headers cannot transitively pull in any Qt header so non-GUI consumers
  can link the library without Qt). Switching would either break
  COD-031 or require a parallel Qt-free verifier path that defeats the
  point of having one.
- **Five-state scanner** — ~80-100 lines of carefully-tested code that
  lives in `wire_protocol.hpp` (the dependency-free home of
  `findStringField`). Worth a dedicated PR with its own test sweep
  rather than shoving into a review-fix commit.

The window-widening fix the partial commit lands closes the specific
miscount the reviewer demonstrated. The remaining concern — that the
mini-grep approach is fragile enough that the next round of "field X
also matters" feedback will surface a similar bug — should drive a
deliberate decision about which of the three replacement paths to take.

______________________________________________________________________

_Generated: 2026-05-12 by gsd-code-fixer_
_Source review: `01-REVIEW.md`_
