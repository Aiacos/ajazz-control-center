<!--
SYNC IMPACT REPORT
==================
Version change: (template / unversioned) → 1.0.0
Bump rationale: MAJOR — initial ratification; first concrete constitution replacing the
  placeholder template. Establishes nine governing principles plus constraint and workflow
  sections.

Modified principles: none (initial adoption)
Added principles:
  I.    Code Quality & Architectural Boundaries
  II.   Testing Standards (NON-NEGOTIABLE)
  III.  User Experience Consistency
  IV.   Performance Discipline
  V.    Auto Debug & Live Validation (NON-NEGOTIABLE)
  VI.   Research-Backed Implementation
  VII.  Documentation Currency
  VIII. Repository Hygiene
  IX.   Consistent, Useful CI/CD
Added sections:
  - Platform & Technology Constraints
  - Development Workflow & Quality Gates

Removed sections: none

Templates requiring updates:
  ✅ .specify/templates/plan-template.md — Constitution Check gate is reference-based
     ("[Gates determined based on constitution file]"); no hard-coded principle names. No edit needed.
  ✅ .specify/templates/spec-template.md — no constitution references. No edit needed.
  ✅ .specify/templates/tasks-template.md — phase categories are story-driven and principle-agnostic.
     No edit needed.
  ✅ CLAUDE.md — existing project conventions are consistent with and feed these principles.

Follow-up TODOs: none. Ratification date set to first adoption (2026-06-18).
-->

# AJAZZ Control Center Constitution

AJAZZ Control Center is an open, cross-platform control center for AJAZZ devices (Stream
Decks, keyboards, mice) built on Qt 6 / QML and C++20, with a Python out-of-process plugin
host and a Rust `mirajazz` sidecar for Stream Dock families. Linux is the primary target;
Windows and macOS are first-class. This constitution defines the non-negotiable engineering
principles for the project. It supersedes ad-hoc convention where the two conflict.

## Core Principles

### I. Code Quality & Architectural Boundaries

Code MUST be formatted and linted by the project toolchain before it lands: clang-format,
clang-tidy (pre-push), ruff/ruff-format for Python, shfmt for shell. Pre-commit hooks MUST
NOT be skipped; `--no-verify` is permitted ONLY when the hook itself is broken AND the hook's
intent is independently verified to pass, with the bypass documented in the commit body.

Architectural boundaries are load-bearing and MUST hold:

- **COD-031**: `nlohmann::json` MUST NOT appear in `ajazz_core` or any installed public
  header (`grep -rn nlohmann src/core/include/` MUST return 0). It is PRIVATE-linked to
  `ajazz_plugins` only. Crossing this boundary is a release blocker.
- The Stream Dock backend is the out-of-process `mirajazz` sidecar; the removed in-tree C++
  AKP03/05/153 wire backends MUST NOT be reintroduced. The `mirajazz` crate is a pristine
  dependency and MUST NOT be modified in-tree. (Carve-out: AKP815 keeps its custom C++
  backend.)
- Qt singletons MUST use `qmlRegisterSingletonInstance`, never the bare `QML_SINGLETON`
  macro path, with a co-located `static_assert(!std::is_default_constructible_v<T>)` to make
  misuse a build break.

Commits MUST be atomic (one independently-revertable change) and follow Conventional Commits
(`feat:`, `fix:`, `chore:`, `docs:`, `test:`, `refactor:`, optional scope).

**Rationale**: These boundaries are the ones that have actually broken releases or shipped
silent bugs in this project; encoding them turns recurring traps into hard gates.

### II. Testing Standards (NON-NEGOTIABLE)

Every behavioral change MUST be covered by automated tests at the appropriate layer: Catch2
unit tests in `tests/unit/`, integration tests in `tests/integration/`, and the offscreen
QML smoke target in `tests/qml/`. Wire-byte coverage for the sidecar lives in its cargo
tests; app-layer tests use the in-process `FakeStreamDockDevice` fixture.

The working gate is `ctest --preset linux-release`; it MUST be green before any change is
considered complete. Trust the live ctest count — do not hand-edit reported figures. The
ctest filter flag is `--tests-regex` / `-R`. Test names MUST be ASCII-only (CTest mangles
non-ASCII through the Win32 CMD codepage).

Cross-platform strictness is mandatory: a change MUST compile clean under Linux GCC, Linux
Clang, Apple Clang (`-Werror`), and MSVC (`/W4 /WX`). Each compiler catches what the others
miss; landing all three is required, not optional.

**Rationale**: A passing `ctest` is necessary but never sufficient on its own (see Principle
V) — but the inverse, shipping without it, is never acceptable.

### III. User Experience Consistency

The editor surface MUST stay Stream-Deck / OpenDeck-shaped: actions are bound by dragging
tools directly onto the device-canvas key or strip zone, not through a detached panel.
Bindings, profiles, and device geometry MUST behave consistently across all supported SKUs.
Visual treatment MUST respect Material theming rules — attached properties set inside the
popup `contentItem`, not on the popup root.

Device rendering MUST match physical reality: the AKP05E panel renders `Rot180` (mounted
inverted); render geometry and orientation MUST be verified against hardware, not assumed.

**Rationale**: Users transfer expectations from established Stream Deck tooling; consistency
across devices and with that mental model is the product's core value.

### IV. Performance Discipline

Hot paths MUST NOT do redundant work. Plugins MUST NOT be re-scanned from disk per keystroke;
device handles MUST be persistent for the session rather than opened/closed per interaction
(the per-interaction open/close churn wedged the AKP05E — one `CRT DIS` for the handle
lifetime is the contract). Device selectors and caches MUST refresh on hotplug events, not by
polling. Any change touching enumeration, rendering, or input dispatch MUST be evaluated for
per-event allocation and I/O cost before landing.

**Rationale**: This is a always-running tray-resident app driving real hardware at interactive
latency; wasted work in the hot path is felt directly as lag or device wedging.

### V. Auto Debug & Live Validation (NON-NEGOTIABLE)

Every UI or behavioral change MUST be verified live through the out-of-process debug-control
channel before being claimed done. The procedure: build → launch with `AJAZZ_DEBUG_CONTROL=1`
→ drive the relevant controls via `qml.invoke`/`qml.get`/`qml.set` → `screenshot` → read the
screenshot → confirm real behavior. `ctest` green plus code review have repeatedly passed
changes that were no-ops or mis-wired at runtime; only live verification caught them.

Every new interactive control (Button, Switch, Drawer, page, list delegate, input) MUST set
an `objectName` so it is addressable by the debug channel. New user-facing C++ surfaces SHOULD
expose a debug-control method or state when doing so enables autonomous verification. "Can I
drive this from `scripts/ajazz-debug`?" is a definition-of-done checklist item.

**Rationale**: This project's integration and wiring bugs live precisely in the gap that unit
tests and grep cannot see; the debug channel is the only tool that closes it.

### VI. Research-Backed Implementation

Before changing any protocol, wire-format, opcode, packet layout, HID interface/usage-page,
report ID, or capability wiring, the relevant reverse-engineering docs under
`docs/protocols/**` and the MEGAsync RE corpus MUST be re-read first — ideally via a dedicated
sub-agent sweep so family-wide implications are not missed. The RE is the source of truth for
wire formats, but it has gaps and provisional values: any value flagged "provisional" or
"unconfirmed" is a hypothesis to verify against hardware, not a fact. When RE and hardware
disagree, hardware wins — and the RE doc MUST be updated to match.

New or upgraded SDK/library dependencies MUST be researched against their official docs and
the findings recorded (capabilities, version constraints, pitfalls) before adoption.
Investigations MUST be grep-verified against git history and existing docs first; re-running
an investigation that already shipped is a violation of this principle.

**Rationale**: This codebase has had hardware-confirmed answers land ahead of bookkeeping;
groping without checking prior findings wastes time and ships regressions.

### VII. Documentation Currency

Technical and user documentation MUST be updated in the same change that alters the behavior
it describes. The JSON-wire schema documentation is the source of truth for wire keys: when a
C++ field name and a JSON key differ, the schema wins, and a writer MUST NOT be aligned to a
C++ field name without first checking the schema. `docs/protocols/**` MUST be corrected when
hardware contradicts it. User-facing docs (README, autogenerated device tables from
`docs/_data/devices.yaml`, quickstart) MUST reflect shipped capability, not planned capability.

**Rationale**: Stale protocol and schema docs are actively dangerous here — they get treated
as ground truth and propagate wrong wire formats into new code.

### VIII. Repository Hygiene

Project tooling MUST make code-only changes inside the repo. No system-level mutations: do not
write to `/etc/`, `~/.config/**`, `/usr/share/`, or user dotfiles; even read-only inspection
of dotfiles MUST be sparing and explicitly justified. Dead code MUST NOT be deleted on the
basis of a static-analysis "unreachable" finding without first grep-verifying call sites
(CodeQL `cpp/unused-static-function` findings here are presumed extractor call-graph false
positives). Generated artifacts, scratch files, and abandoned experiments MUST be cleaned up
rather than committed; triaged scanner false positives go in their dedicated ignore files
(`.gitleaksignore`, codeql config), never silenced with `--no-verify`.

**Rationale**: The repo is shared ground truth; uncontrolled mutations and speculative
deletions have each cost the project real debugging time.

### IX. Consistent, Useful CI/CD

The CI matrix (ubuntu / windows-2022 / macos-14), Nightly, Release, CodeQL, and secret-scan
workflows MUST stay green on integration branches. Work follows GitFlow-lite: short-lived
topic branches (`feat/…`, `fix/…`, `chore/…`) open PRs into the integration branch; the
release branch receives only the integration promotion; `v*` tags are cut from the release
branch. Never push directly to or force-push the long-lived branches.

CI MUST shift left: pre-commit, commit-msg, and pre-push hooks catch formatting, banned MSVC-
fatal APIs, and clang-tidy before CI does. CI checks MUST be meaningful — gates that assert a
specific behavior ran (e.g. the Windows hot-plug smoke gate) are preferred over green-by-
absence. A red pipeline is a stop-the-line event, not a mergeable state.

**Rationale**: A trustworthy, fast pipeline is what lets the team move quickly; flaky or
decorative CI erodes that trust and lets regressions through.

## Platform & Technology Constraints

- **Language/UI**: C++20, Qt 6.7+, QML. Build with CMake + Ninja via presets.
- **Plugin host**: Python, out-of-process. **Stream Dock transport**: Rust `mirajazz`
  sidecar (`streamdock-host/`, JSON over stdio), proxied by `SidecarStreamDockDevice`.
- **Linux device access**: `hidapi_hidraw` backend only — the libusb hidapi backend is
  disabled and MUST NOT gain code paths without a deliberate cross-team decision. udev rules
  MUST sort before `73-seat-late.rules` (the project uses `70-ajazz.rules`).
- **Cross-platform**: Linux primary; Windows and macOS supported and gated in CI. Platform-
  specific code belongs in platform-named files (`*_win32.cpp`, `*linux*`, `*macos*`, `.mm`).
- **Concurrency limit**: autonomous execution agents are capped at 2 concurrent; three or more
  have caused atomic-commit splits and forced `--no-verify` workarounds.

## Development Workflow & Quality Gates

A change is "done" only when ALL of the following hold:

1. `ctest --preset linux-release` is green (Principle II).
1. The change compiles clean on all three platform compiler configurations (Principle II).
1. Behavioral/UI changes are verified live through the debug-control channel, with a
   screenshot read and confirmed (Principle V).
1. New interactive controls are debug-addressable via `objectName` (Principle V).
1. Affected technical/user docs and wire schemas are updated in the same change (Principle VII).
1. Pre-commit / commit-msg / pre-push hooks pass without bypass (Principles I, IX).
1. Protocol/wire/device changes are cross-checked against the RE corpus, and any hardware
   disagreement is resolved in hardware's favor with the doc updated (Principles VI, VII).

Code review and `ctest` are necessary but not sufficient: every gate above that can be
verified at runtime MUST be verified at runtime.

## Governance

This constitution supersedes ad-hoc practice where they conflict. Project-level conventions
live in `CLAUDE.md`; where `CLAUDE.md` and this constitution diverge, this document governs
on matters of principle and `CLAUDE.md` governs on mechanical detail.

Amendments MUST be proposed via PR, state the rationale and the affected principle(s), and
include a version bump per the policy below. Dependent Spec Kit templates
(`.specify/templates/*.md`) and any runtime guidance docs MUST be checked for consistency in
the same PR, and a Sync Impact Report MUST be prepended to this file.

Versioning policy (semantic):

- **MAJOR**: backward-incompatible governance change — a principle removed or redefined.
- **MINOR**: a new principle or section added, or materially expanded guidance.
- **PATCH**: clarifications, wording, and non-semantic refinements.

Compliance is verified at PR review and at audit time. The quality gates in the section above
are the operational checklist; reviewers MUST confirm them. Complexity that violates a
principle MUST be justified in writing or removed.

**Version**: 1.0.0 | **Ratified**: 2026-06-18 | **Last Amended**: 2026-06-18
