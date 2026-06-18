# Project memory — AJAZZ Control Center

> Load-bearing project conventions and gotchas for new contributors (human
> or AI). Update via PR; **don't** check in personal notes here — those
> belong in your local Claude Code auto-memory.

## What this project is

Modern, open, cross-platform control center for AJAZZ devices (Stream
Decks, keyboards, mice) with Qt 6 / QML UI and a Python out-of-process
plugin system. C++20, Qt 6.7+, CMake, Ninja. Linux primary; Windows and
macOS supported.

For full context and current product requirements: `specs/001-control-center-baseline/spec.md`
(the Spec Kit baseline, which superseded the former `.planning/` GSD artifacts on 2026-06-18).
Project principles live in `.specify/memory/constitution.md`.

## Workflow conventions

- **GitFlow-lite.** Two long-lived branches: `develop` (integration, the
  default branch) and `main` (release-only). Do work on short-lived topic
  branches (`feat/…`, `fix/…`, `chore/…`) cut from `develop`; open a PR
  **into `develop`**. `main` only ever receives a PR **from `develop`** (the
  release promotion); release tags `v*` are cut from `main`. Never push
  directly to `develop` or `main`, and never force-push either.
  - *Why this changed:* the project ran direct-to-`main` through v1.1; the
    move to PR-gated `develop` lets the Lint workflow auto-fix formatting on
    the topic branch before it ever reaches an integration branch, so CI on
    `develop`/`main` stays green. See `.github/workflows/lint.yml`.
- **Atomic commits.** Each commit is one independently-revertable change.
  Don't bundle unrelated fixes.
- **Conventional Commits.** `feat:`, `fix:`, `chore:`, `docs:`, `test:`,
  `refactor:`, with optional scope: `fix(plugins): ...`. Enforced by pre-commit.
- **Never skip pre-commit hooks.** `--no-verify` is only acceptable when the
  hook itself is broken (e.g. stash/restore failure under concurrent agents)
  AND the hook content is verified to pass independently. Document the bypass
  in the commit message body.

## Hard rules

- **No system-level mutations from project tooling.** Code-only fixes inside
  the project repo. Don't write to `/etc/`, `~/.config/niri/`,
  `~/.config/noctalia/`, `/usr/share/`, etc. Even read-only inspection of
  user dotfiles should be sparing and explicitly justified.
- **No `nlohmann::json` in `ajazz_core` or any installed public header.**
  PRIVATE-linked to `ajazz_plugins` only. Verified by grep at audit time
  (`grep -rn nlohmann src/core/include/` must return 0). This is the
  **COD-031 boundary** — crossing it is a release-blocker.
- **Schema doc is the source of truth for JSON wire keys.** When the
  C++ field name and the JSON key differ (e.g. `Profile::deviceCodename` ⇄
  `"device"`), the schema documentation wins. Never align a writer to a C++
  field name without first checking the schema.
- **Cap concurrent execute agents at 2** in autonomous runs. Three or more
  concurrent agents have caused atomic-commit splits and forced `--no-verify`
  workarounds (v1.1 retrospective lesson).
- **Always cross-check the reverse engineering before touching protocol /
  wire-format / device code.** Before changing any opcode, packet layout,
  HID interface/usage-page, report ID, or capability wiring, re-read the
  relevant RE docs under `docs/protocols/**` (the Ghidra DLL passes +
  `[mirajazz]` / `[opendeck-*]` / `[ajazz-sdk]` corpora) — ideally via a
  dedicated sub-agent sweep so family-wide implications aren't missed. The RE
  is the source of truth for wire formats, but it has **gaps and provisional
  values**: treat any value the docs flag as "provisional"/"unconfirmed" as a
  hypothesis to verify against the physical device, not a fact. Real examples
  (2026-05-20): the `0x0300:0x3004` SKU was mis-filed as a 6-key AKP03 until a
  live `CRT VER` handshake proved it an AKP05E; `proprietary.md` listed the
  AK980 control interface as usage page `0xFF00` when the real device uses
  `0xFF13` (pinned by a hardware probe). When the RE and the hardware
  disagree, the hardware wins — and update the RE doc.

## Qt 6 / QML gotchas (verified, recurring)

- **`QML_SINGLETON` requires `qmlRegisterSingletonInstance`**, NOT the bare
  `QML_SINGLETON` macro. The macro path silently creates a second instance
  per QML import (v1.0 light-theme bug). Co-locate
  `static_assert(!std::is_default_constructible_v<T>)` with the
  `QML_SINGLETON` declaration to make this a build break, not a comment.
- **`WebEngineView` has no `page` Q_PROPERTY.** Use `Qt6::WebChannelQuick`
  (not `WebChannel`). `qt_add_qml_module IMPORTS` is the right wire.
- **`MultiEffect.maskSource` needs a proper Item type.** A raw `Rectangle`
  causes silent SIGABRT on real GPU. Wrap it.
- **Material attached properties don't cross Popup scope.** Set them
  inside the popup contentItem, not on the popup root.

## Cross-platform build strictness

Each platform's compiler catches things the others don't. Land all three.

- **Linux GCC + Linux Clang**: most permissive, smallest set of catches.
- **Apple Clang on macOS (`-Werror`)**: catches `-Wunused-const-variable`
  on `inline constexpr` at file scope. GCC/Linux-Clang don't.
- **MSVC on windows-2022 (`/W4 /WX`)**: deprecation warnings (C4996) are
  hard errors. Prefer the `_s` variants (`_wdupenv_s` not `_wgetenv`,
  `sprintf_s` not `sprintf`).
- **Test names must be ASCII-only.** ctest passes filter args through the
  Win32 CMD codepage; em-dash `—` and right-arrow `→` get mangled to `?`
  and Catch2's filter no longer matches. Use `-` and `->`.
- **Moved-from `std::wstring` size is implementation-defined.** libstdc++
  and libc++ tend to leave SSO contents intact; MSVC clears heap-allocated
  strings on move. Save `entry.size() + 1` BEFORE any `std::move(entry)`
  call when using the value to advance a cursor.

## CMake / CTest

- **Working preset**: `ctest --preset linux-release`. The suite contains
  roughly 408 ctest cases (≈399 Catch2 `TEST_CASE`s in `tests/unit/` plus
  `tests/integration/` and the new offscreen QML smoke target `tests/qml/`)
  as of 2026-05-22 (was 178 at v1.1 close, ~286 at 2026-05-18; grew through
  Phase 9 captures, vendor-RE work, AK980 clock-sync, OOP plugin host,
  SdPluginServer MVP, the bulk audit follow-up, the health-report fix loop,
  and the QML harness). Run the preset and trust the live count; do not
  hand-edit this figure on every push.
- **ctest filter flag is `--tests-regex` / `-R`, NOT `--test-regex`.**
  The latter is a typo that produces "Unknown argument" from CMake.
- **Win32EnvBlock sort order**: env blocks passed to `CreateProcessW` must
  be sorted **by name** (key only), not by full `KEY=VALUE` entry. Use
  `std::wstring_view` to extract the key cheaply for the comparator.

## Linux device access

- udev rules at `resources/linux/70-ajazz.rules` cover VID prefixes
  `0300` (Stream Dock family), `3151` (SONiX-VID AJAZZ keyboards/mice),
  `0c45` (Microdia-VID AK980 PRO), `248a` + `249a` + `3554` (AJ-series mice).
- **Backend**: `hidapi_hidraw` only — kernel-native `/dev/hidraw*`. The
  libusb hidapi backend is NOT used and is explicitly disabled in the
  Flatpak manifest. Don't add libusb-only code paths without a deliberate
  cross-team decision.
- **Rule filename MUST sort before `73-seat-late.rules`.** That stock rule
  (`/usr/lib/udev/rules.d/73-seat-late.rules:16`,
  `TAG=="uaccess", … RUN{builtin}+="uaccess"`) is what actually applies the
  per-user ACL, and it requires the `uaccess` tag to already be set when it
  runs. Our rule is therefore numbered **`70-`** (was `99-ajazz.rules`, which
  ran AFTER 73 → the builtin fired before our tag existed → tag shows in
  `udevadm info` but no ACL → `/dev/hidraw*` stays root-only → `hid_open`
  EACCES; devices enumerate but cannot be opened). Matches hidapi's documented
  rule (`udev/69-hid.rules`: "must have priority before 73-seat-late.rules").
- **systemd ≥258 `uaccess` regression (verified on systemd 259, 2026-05-21).**
  Even with correct ordering, the `uaccess` ACL is applied only on a *real
  physical replug / boot* — NOT on `udevadm trigger` (`--action=change` or
  `add`) and NOT after a synthetic hub re-enumeration (Debian #1112660,
  hidapi #411). So if Linux USB hubs emit a re-enumeration storm, a device can
  silently lose its ACL and `udevadm trigger` will NOT restore it. Recovery:
  physically replug, OR (transient dev-only) `sudo setfacl -m u:$(id -u):rw /dev/hidraw*` on the affected nodes (exactly what `uaccess` sets; survives
  until the next replug). The `70-` ordering fix is necessary but, on these
  systemd versions, not sufficient against synthetic re-enumeration. A `GROUP=`
  rule would survive it; the project deliberately chose `uaccess`-only (no
  `plugdev`) — revisit only via a cross-team decision.

## CI architecture

- **CI** (`.github/workflows/ci.yml`): per-PR/per-push matrix on ubuntu /
  windows-2022 / macos-14. Includes a "Verify Windows hot-plug smoke ran"
  gate that fails if zero hot-plug tests match the regex.
- **Nightly** (`.github/workflows/nightly.yml`): scheduled + manual.
  Builds macOS Universal DMG + Windows MSI/ZIP — STRICTER than CI because
  it builds both architectures of the Universal DMG.
- **Release** (`.github/workflows/release.yml`): triggered by `push: tags: ['v*']` or `workflow_dispatch`. Produces `.deb` + `.rpm` + `.flatpak` +
  `.dmg` + `.msi`. The `workflow_dispatch` trigger lets you re-test
  without retagging.
- **CodeQL** (`.github/workflows/codeql.yml`): the *analysis* job and the
  PR *gate* are separate checks — the gate fails on new high-severity
  alerts even when the 14-minute analyze leg is green. Result filtering
  lives in `.github/codeql/codeql-config.yml` (`paths-ignore: build` —
  FetchContent'd deps and moc autogen are compiled into the database, so
  they must be filtered at results level, not extraction).
  **`cpp/unused-static-function` findings are presumed extractor
  call-graph FPs here**: in 2026-06-10 CodeQL flagged the entire (live,
  test-covered) `profile.cpp` writer cluster + `settingsForContext` as
  unreachable; 12 alerts were dismissed after grep-verifying call sites.
  Never delete "dead code" off a CodeQL unreachable finding without
  grepping the callers first.
- **Secret scan** (`.github/workflows/gitleaks.yml`) checks out with
  `fetch-depth: 0`: rewording a flagged string in a later commit does NOT
  clear the finding on the original commit. Triaged false positives go in
  `.gitleaksignore` (fingerprint format `commit:file:rule:line`) — the
  only deterministic fix, honoured by both the action and the pre-commit
  hook.
- **macOS DMG flake**: `cpack -G DragNDrop` → `hdiutil` intermittently
  fails with "Resource busy" on hosted runners (XProtect scanning the
  fresh mount). Both nightly.yml and release.yml wrap it in a 3-attempt
  retry; if it fails after 3, it's a real error, not the flake.

## Pre-commit / local gates (shift-left)

- **`pre-commit install` installs all three hook types** (`pre-commit`,
  `commit-msg`, `pre-push` — `default_install_hook_types`). The pre-push
  stage runs clang-tidy on changed files via `scripts/run-clang-tidy.sh`,
  which strips GCC ≥ 15 C++20 module-scan flags (`-fmodules-ts`,
  `-fmodule-mapper=…`, `-fdeps-*`) from a temp copy of the compdb
  (clang-tidy hard-errors on them) and skips files with no compdb entry
  (headers, `*_win32.cpp` on Linux).
- **MSVC-fatal API hook** (`scripts/check_msvc_fatal_apis.py`): bans
  `std::getenv` / `sprintf` / `strcpy` / `strcat` / `_wgetenv` in shared
  C++ at commit time — the MSVC C4996→`/WX` class otherwise only fails
  the windows-2022 leg after a full build. Escape hatches: platform-named
  files (`*linux*`, `*posix*`, `*x11*`, `*wayland*`, `*macos*`,
  `*darwin*`, `.mm`), `NOLINT` lines, comment-only lines; the `_s`
  variants never match.
- **Custom local hooks are pure-Python**, not bash — `language: system`
  bash hooks silently no-op on Windows clones without Git-Bash on PATH.
- **Formatter hooks abort the commit when they rewrite files**
  (clang-format / ruff-format / shfmt): re-`git add` and re-run the
  identical commit. pre-commit also refuses to commit while
  `.pre-commit-config.yaml` itself has unstaged changes.

## Methodology — be methodical and precise, don't grope

**Hard prerequisite before any device experiment, "new finding" claim, or
refactor on this codebase.** This branch (`experiment/mirajazz`) has had heavy
ad-hoc RE + device work landing ahead of GSD bookkeeping; recent commits
very often already contain hardware-confirmed answers or pin known
PROVISIONAL values. Re-running an investigation that already shipped wastes
both time and tokens. Always:

1. **Check the glossary below first** — most recent AKP05 / streamdeck
   findings are catalogued there with their commit hashes and doc paths.
1. **`git log --all --oneline --grep=<topic>`** and
   `git log --all --oneline -- <file paths>`. Read the message bodies of
   recent commits that look related.
1. **Read the matching `docs/protocols/streamdeck/*.md` fully** — not just
   headers, not just grep hits. The big four for AKP05E:
   `akp05_vendor.md`, `akp05_init_sequence.md`,
   `akp05_input_corrections.md`, `akp_device_matrix.md`.
1. **Check the MEGAsync RE corpus** at
   `~/MEGAsync/ajazz-reverse-engineering/` (synthesis dossiers + Ghidra
   `ghidra_SDLibrary1_dll.json` + probe scripts).
1. **Only then propose an experiment** — and frame it as "verifying what
   doc X says under condition Y," not as discovery. When a finding feels
   "new," that's the cue to grep harder, not to commit it.

For v1.3 device phases specifically: **grep code + `devices.yaml` maturity
before "executing" the phase** — Phases 10/11/12 shipped ad-hoc; GSD
"planned / 0 summaries" ≠ unimplemented (see commit `2535fe1` reconciliation).

## Debug-channel verification — MANDATORY

The app ships an opt-in out-of-process debug-control channel
(`src/app/src/debug_control_*`; launch with `AJAZZ_DEBUG_CONTROL=1`; driven by
`scripts/ajazz-debug` over the Unix socket
`$XDG_RUNTIME_DIR/ajazz-control-center-debug.sock`). It exposes ~30 RPC methods:
`ping`, `state`, `qml.tree/get/set/invoke/click`, `screenshot`, `device.*`,
`input.*`, `plugin.list/sendEvent`, `profile.*`, `log.tail`, …

- **Verify EVERY change live through the debug channel before claiming "done".**
  `ctest` green is necessary but NOT sufficient — unit tests and grep/code-review
  miss integration and wiring bugs. The procedure for any UI/behavioral change:
  build → launch `AJAZZ_DEBUG_CONTROL=1` → drive the relevant controls via
  `qml.invoke`/`qml.get`/`qml.set` → `screenshot` → **read the screenshot** →
  confirm the real behavior. Real example (2026-05-31): the Phase-27 per-plugin
  "Allow" button passed 713/713 unit tests AND a code review, but driving the
  running app showed it was wired to the Python-host plugin list and was a no-op
  for `.sdPlugin` plugins — caught ONLY by live debug-channel verification.
- **Every new interactive control MUST be debug-addressable.** The channel
  addresses objects by `objectName` (`findByName`); an un-named control is
  invisible to `qml.get/set/invoke/click`. So every new Button / Switch / Drawer
  / page / list-delegate / input you add MUST set `objectName:`. New C++
  user-facing surfaces should expose a debug-control method/state when it enables
  autonomous verification. Treat "can I drive this from `scripts/ajazz-debug`?"
  as a definition-of-done checklist item for new code.
- **Known harness gap:** `qml.invoke toggle` / `qml.click` emit the control's
  method / `clicked()` but do NOT reproduce a `Switch`/`CheckBox`
  user-`toggled()` side effect (the `onToggled` handler never fires). To drive a
  setting end-to-end, expose a dedicated `Q_INVOKABLE`, or verify the C++ setter
  - the read-binding separately (set the backing store, relaunch, read the bound
    property).

## Stream Dock backend = mirajazz sidecar (architecture, 2026-06-01)

**The Stream Dock families (AKP03 / AKP05-N4 / AKP153) are driven by an
out-of-process Rust sidecar built on the [`mirajazz`](https://github.com/4ndv/mirajazz)
crate, NOT by in-tree C++ wire code.** The sidecar (`streamdock-host/`, JSON over
stdin/stdout) is proxied into the app by `SidecarStreamDockDevice`
(`src/app/src/sidecar_stream_dock_device.*`); the app registers every mirajazz
SKU via `streamDockSidecarDescriptors()` + `makeSidecarStreamDock` in bootstrap.
The persistent-handle sidecar renders the live AKP05E correctly with **no wedge**
(one `CRT DIS` for the handle lifetime vs the old per-interaction open/close
churn). The old C++ wire backends (`akp03/05/153.cpp` + their `*_protocol.hpp`,
`makeAkp03/05/153`) were **removed** in the `experiment/mirajazz` Slice D —
do NOT reintroduce them. Wire-byte coverage lives in the sidecar's cargo tests;
app-layer tests use the in-process `FakeStreamDockDevice` fixture.

**Carve-out:** **AKP815** is NOT a mirajazz device (800×480 strip) and keeps its
custom C++ backend (`akp815.cpp` + `akp815_protocol.hpp` + `akp815_wire.{hpp,cpp}`

- `akp_common_protocol.hpp` + `image_pipeline.*`). `streamdeck::registerAll()`
  registers ONLY the AKP815 now. Keyboards (AK980) and mice (AJ-series) are not
  stream controllers and stay custom, untouched. Do NOT modify the `mirajazz`
  crate itself — it is a pristine git dependency.

The glossary below records **hardware-confirmed findings about the AKP05E /
streamdeck wire protocol** — these remain valid (the protocol did not change,
only the implementation moved to the sidecar). References to the removed C++
symbols (`akp05.cpp`, `StreamDockControlService` open/close, `makeAkp05`) are
historical; the wire facts (image formats, `BAT` surfaces, input-unreachable
proof) still hold and the sidecar implements the same wire.

## AKP05E / streamdeck investigation glossary (2026-05-21 → 2026-05-28)

Quick-reference index. **Read before any AKP05 / streamdeck device experiment.**

### Live unit: `0x0300:0x3004` "HOTSPOTEKUSB HID DEMO" (white-label demo SKU)

| Capability                                  | Status                                                                                                                                                                | Where to read                                                             |
| ------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| `GET_FEATURE id 0x01` (firmware)            | ✓ returns `V3.AKP05E.01.007`                                                                                                                                          | commit `5ec18d9` (mirajazz way)                                           |
| Output writes (`LIG`/`CLE`/`CONNECT`/`BAT`) | ✓ drive the panel — but **never send `CRT DIS` at `open()`** (wedges the display via a `DIS,STP,DIS` open/close/reopen churn; recover by physical replug)             | `cc04a54` (report-id prepend) + `037bd8d`; DIS-wedge confirmed 2026-05-31 |
| `BAT` key-byte → physical surface           | ✓ **strip zones 1..4** (the 4 encoder zones), wire 5 = **no surface**, bottom keys 6..10, top keys 11..15. **ALL surfaces use `BAT`**; `ENC`/`MAI`/`DRA` render blank | `037bd8d` + `cb00677` (strip via BAT 1..4)                                |
| Image render on Linux                       | ✓ live — keys **and** the 4 strip zones, **`Rot180`** (panel mounted inverted; was upside-down at 0°), keys 85×85, zones ~128×128, `ULEND`@`5..9` works               | `cd48ea3` (key Rot180) + `cb00677` (strip); full model in `akp05.md`      |
| Input streaming (key/encoder/touch)         | ✗ **NOT reachable on this demo unit**                                                                                                                                 | `akp05_input_corrections.md §7.1` + commit `89c0db6`                      |
| `parseInputReport` structure                | ✓ aligned with vendor RE (encoder ±1, touch X single byte at `frame[10]`)                                                                                             | commit `7eb5501` + `akp05_input_corrections.md §3, §4`                    |

**Input-unreachable proof chain** (so nobody re-runs this): tested with
(a) raw hidraw read, (b) `GET_REPORT` polling, (c) evdev `event264`,
(d) raw `usbmon` filtered to the device, **and (e) the reference library
`4ndv/mirajazz`** (its own `async_hid` backend + exact `DIS`+`LIG`+`CONNECT`
keep-alive). All five captured **zero input on press**. Kernel arms EP
`0x82` correctly (usbmon: `S Ii:1:NNN:2 -115:1 512 <`); the device declines
to fill it. Most likely demo/engineering firmware with input path disabled
or stubbed. Remaining viable §7 paths: **Frida-on-Windows vendor app** (the
"decisive" method per
`~/MEGAsync/ajazz-reverse-engineering/dossier/methods-and-tooling.md §2`)
or a **retail AKP05E / Mirabox N4** unit.

### Authoritative references (read, don't re-derive)

**In-repo:**

- `docs/protocols/streamdeck/akp05_vendor.md` — `SDLibrary1.dll` Ghidra
  audit (§1.2 backends, §2 full opcode table, §14.1 `0x3004` = AKP05E
  correction).
- `docs/protocols/streamdeck/akp05_init_sequence.md` — vendor open
  handshake (§3.2 first commands sent: only `CRT VER`; **no input-enable
  command exists**).
- `docs/protocols/streamdeck/akp05_input_corrections.md` — encoder/touch
  structural corrections (§3 encoder, §4 touch, §7.1 unreachable proof).
- `docs/protocols/streamdeck/akp_device_matrix.md` — 96 SKUs per-device
  geometry + transport.

**MEGAsync corpus** (`~/MEGAsync/ajazz-reverse-engineering/`):

- `dossier/akp-streamdeck.md` — AKP05 family synthesis.
- `dossier/capture-evidence.md` — sanitised control-channel byte dumps
  (Frida hooks of the Windows vendor driver, NOT live USB pcaps).
- `dossier/methods-and-tooling.md` — Ghidra + Frida workflow; the
  "decisive method" is Frida-on-Windows-vendor-app.
- `raw-workdir/sd-app/ghidra_SDLibrary1_dll.json` — vendor SDK decompile
  (functions truncated ~10–12 KB but prologues complete).

**OSS reference libraries:**

- `4ndv/mirajazz` (Rust, GitHub) — authoritative AKP05 / N4 library; our
  firmware-read method (`GET_FEATURE id 0x01`) follows it. Its
  `initialize()` sends `CRT DIS` + `CRT LIG`; `keep_alive()` sends
  `CRT CONNECT`. Tested on this unit — also captures no input.
- `naerschhersch/opendeck-akp05` — opendeck plugin built on mirajazz.

### Working tools (in-tree + scratch)

- `scripts/akp05_input_probe.py` — dual-node hidraw input reader with
  mirajazz-exact `DIS`+`LIG` init + `CRT CONNECT` keep-alive +
  `GET_FEATURE id 0x01` firmware probe. The capture pattern is also documented
  in `akp05_input_corrections.md §7.1` for trivial reproduction.
- `scripts/akp05_color_probe.py` — known-good output round-trip: paints all
  15 BAT surfaces (wire 1..15). **Re-run this first when output looks broken**
  — if the probe also renders nothing, the device is wedged (replug), not a
  code bug.
- `scripts/akp05_strip_probe.py` — maps + sizes the 4 strip zones (BAT wire
  1..4); `--sizes` sweeps zone sizes. (How the ~128 px zone fit was found,
  2026-05-31.)
- `scripts/akp05_ulend_ab.py` — A/B the `ULEND` offset (5..9 vs 3..7) on one
  panel.
- Headless app render: launch with `AJAZZ_DEBUG_CONTROL=1`, then
  `scripts/ajazz-debug device.renderTest --params '{"codename":"akp05e","count":10,"main":true,"encoders":true}'`
  paints numbered keys + the 4 strip zones (the only headless way to render —
  QML KeyCells lack objectNames). Launch with plain `nohup … &`; kill by exact
  PID — a `pkill -f` whose pattern is in your own command line kills its shell.
- `build/linux-release/src/app/ajazz-control-center` — GUI app. Selecting
  the AKP05E in the sidebar holds a persistent open + sends `LIG`;
  brightness slider drives the panel live. Keys + the 4 strip zones render
  `Rot180` (orientation fixed 2026-05-31, `cd48ea3`+`cb00677`).

### v1.3 milestone tracking (don't blindly "execute" device phases)

Phase 10/11/12 device work **already shipped ad-hoc** in many commits
ahead of GSD bookkeeping. Retrospective reconciliation in commit `2535fe1`
(12 SUMMARY files marked `mode: retrospective-reconciliation`). Before any
`gsd-execute-phase` on a v1.3 device phase, grep
`src/devices/streamdeck/src/` + `docs/_data/devices.yaml` maturity. See
memory `project_phase_tracking_vs_code_divergence`.

### Latent items (open, low-priority)

- `tests/qml/ajazz_qml_tests` **link gap RESOLVED** — the target now compiles
  `sidecar_stream_dock_device.cpp` + the bridge slots and builds/runs clean on Linux
  and macOS (DeviceView geometry + drag-drop smoke gate). Two follow-ups remain:
  - **Windows-only hang (open).** The first drag-drop case (`DeviceViewDragDrop`,
    first to force the `ProfileController` QML singleton via `singletonInstance`)
    stalls the offscreen process on windows-2022 until the 45-min job timeout —
    most likely a crash parked behind Windows Error Reporting, not a clean failure.
    The tests are therefore **built but not registered/run on Windows**
    (`if(NOT WIN32)` around `catch_discover_tests` in `tests/qml/CMakeLists.txt`);
    Linux + macOS keep them as the smoke gate. Needs a Windows box (or WER-disabled
    CI) to root-cause. Pre-existing, unrelated to the sidecar migration.
  - The harness leaks `world()` and uses a custom `main()` that `std::_Exit`s past
    Qt's racy exit-time global-destructor teardown (was an intermittent ubuntu
    SegFault after "All tests passed"). Run ctest with `-LE qml` to skip the label;
    the coverage job already does (gcov × QML SIGSEGV).
- **Resolved by the sidecar:** the old `StreamDockControlService` per-interaction
  open/close churn on the AKP05E (which wedged the panel) no longer applies — the
  sidecar holds a persistent handle for the session (one `CRT DIS`), hardware-confirmed
  no-wedge 2026-06-01.

## Useful references

- `docs/superpowers/plans/` — pre-GSD ad-hoc plans, occasionally inherited
  into formal phases (e.g. v1.1 Phase 5 time-sync adopted
  `2026-05-13-time-sync.md`).
- `specs/` — Spec Kit feature specifications (current planning system). The
  product baseline is `specs/001-control-center-baseline/spec.md`.
- Milestone history (v1.0–v2.0 audits, ROADMAP, RETROSPECTIVE, ADRs) lived in
  the former `.planning/` GSD tree, removed 2026-06-18 in the migration to Spec
  Kit; recover from git history (`git log -- .planning/`) if a past decision
  rationale is needed.

______________________________________________________________________

*This file is project memory. Update via PR. Personal session notes
belong in your Claude Code auto-memory, not here.*

<!-- SPECKIT START -->

Active implementation plan: `specs/001-control-center-baseline/plan.md` — Stream Deck
device + plugin system (Elgato/OpenDeck parity on the mirajazz sidecar). See its
`research.md`, `data-model.md`, `contracts/`, and `quickstart.md` for the subsystem
decomposition, the Elgato/PI/sidecar contracts, and the debug-channel validation scenarios.
Keyboard/mouse are out of scope for that plan (already functional).

<!-- SPECKIT END -->
