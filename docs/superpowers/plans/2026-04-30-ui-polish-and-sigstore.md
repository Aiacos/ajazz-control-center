# UI polish + Sigstore — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship three independent atomic commits to `main`: M3 surface-tint palette wired into `Card` + drawers; an animated `Ripple` state-layer integrated into `PrimaryButton`/`SecondaryButton`; Sigstore build-provenance attestation in the release CI.

**Architecture:** Three independently revertable commits. Items 1 and 2 are pure QML changes; item 3 is a CI YAML + wiki doc change. No new C++ source files. No new dependencies. The two QML items extend existing token systems already in `Theme.qml` (M3 motion + elevation). The CI item adds 3 lines to one job and one section to one wiki page.

**Tech Stack:** Qt 6.7 / QML, CMake (qt_add_qml_module), GitHub Actions, Sigstore via `actions/attest-build-provenance`, mdformat, clang-format, qmllint-qt6.

**Spec:** `docs/superpowers/specs/2026-04-30-ui-polish-and-sigstore-design.md`

______________________________________________________________________

## File structure

| File                                         | Change                                                                                      | Owner task |
| -------------------------------------------- | ------------------------------------------------------------------------------------------- | ---------- |
| `src/app/qml/Theme.qml`                      | Modify — add 5 surface-container tokens + `surfaceAt(level)` function                       | Task 1     |
| `src/app/qml/components/Card.qml`            | Modify — `color` becomes elevation-aware                                                    | Task 1     |
| `src/app/qml/Main.qml`                       | Modify — two Drawer backgrounds switch from `bgBase` to `surfaceContainer`                  | Task 1     |
| `src/app/qml/components/Ripple.qml`          | Create — new reusable state-layer component                                                 | Task 2     |
| `src/app/qml/components/PrimaryButton.qml`   | Modify — embed `Ripple` + `Connections` on `pressedChanged`                                 | Task 2     |
| `src/app/qml/components/SecondaryButton.qml` | Modify — embed `Ripple` (different color/opacity) + `Connections`                           | Task 2     |
| `src/app/CMakeLists.txt`                     | Modify — register `qml/components/Ripple.qml` in the `QML_FILES` list                       | Task 2     |
| `.github/workflows/release.yml`              | Modify — `publish` job: explicit `permissions:` block + new `Attest release artifacts` step | Task 3     |
| `docs/wiki/Release-Process.md`               | Modify — add "Verifying release artifacts" section                                          | Task 3     |

Three commits, no overlap between tasks. Each task owns a contiguous, revertable slice.

______________________________________________________________________

## Common pre-flight (run once before starting)

- [ ] **Verify clean tree and sync with `origin/main`**

```bash
git status        # expect "nothing to commit, working tree clean"
git fetch origin
git log --oneline HEAD..origin/main   # expect empty (already up-to-date)
```

If commits arrived on `origin/main`, fast-forward: `git pull --rebase origin main`.

- [ ] **Verify the design spec is committed and pushed**

```bash
git log --oneline -1 -- docs/superpowers/specs/2026-04-30-ui-polish-and-sigstore-design.md
```

Expected: a `docs(design):` commit exists for the spec file.

- [ ] **Confirm working build before any change**

```bash
make build
make test
```

Expected: build succeeds, all tests pass. If they fail, do not proceed — fix the regression first or rebase to a known-good commit.

______________________________________________________________________

## Task 1 — M3 surface-tint palette + Card/drawer wiring

**Goal:** Land 5 new Theme tokens, an `surfaceAt(level)` resolver, an elevation-aware `Card.qml` color binding, and switch the two right-edge drawers to `surfaceContainer`.

**Files:**

- Modify: `src/app/qml/Theme.qml` (add tokens + function after the existing `tile/tileHover/borderSubtle` block)
- Modify: `src/app/qml/components/Card.qml` (single `color` line)
- Modify: `src/app/qml/Main.qml` (two Drawer `background` blocks)

### Task 1.1 — Add surface-tint tokens to Theme.qml

- [ ] **Step 1: Open `src/app/qml/Theme.qml` and locate the `borderSubtle` block (lines 84-86)**

Find this exact block in the file:

```qml
    /// Subtle border for tiles, separators, divider lines.
    readonly property color borderSubtle:
        Qt.tint(bgSidebar, Qt.rgba(fgPrimary.r, fgPrimary.g, fgPrimary.b, 0.13))
```

The new tokens go **immediately after** this block, before the `// ---- Spacing / radius scale` separator (around line 88).

- [ ] **Step 2: Insert the 5 surface tokens + `surfaceAt(level)` resolver**

Insert this block after the `borderSubtle` definition:

```qml

    // ---- M3 surface tint scale --------------------------------------------
    // Material 3 elevation surfaces — `bgBase` tinted with `accent` at the
    // M3-spec opacity ladder (5 / 8 / 11 / 14 / 16 %). Unlike `tile` (which
    // uses `fgPrimary` for polarity-flip neutral elevation), these are
    // *brand-flavored* surfaces — a warmth shift toward the accent color
    // that reads as the same family in both dark and light themes because
    // the low alpha keeps `bgBase` dominant.
    //
    // Wire from any surface that takes an `elevation: int` property:
    //
    //     Rectangle { color: Theme.surfaceAt(elevation) }
    //
    // Or pick a named token directly when the elevation is fixed:
    //
    //     Rectangle { color: Theme.surfaceContainer }   // level 3

    /// Surface Container Lowest — level 1.
    readonly property color surfaceContainerLowest:
        Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.05))
    /// Surface Container Low — level 2.
    readonly property color surfaceContainerLow:
        Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.08))
    /// Surface Container — level 3 (default container surface).
    readonly property color surfaceContainer:
        Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.11))
    /// Surface Container High — level 4.
    readonly property color surfaceContainerHigh:
        Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.14))
    /// Surface Container Highest — level 5.
    readonly property color surfaceContainerHighest:
        Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.16))

    /// Resolve an elevation level to its surface-tint token. Levels 0 and
    /// negative resolve to `bgBase` (no tint); levels above 5 clamp to
    /// `surfaceContainerHighest`.
    function surfaceAt(level) {
        if (level <= 0) return bgBase
        if (level === 1) return surfaceContainerLowest
        if (level === 2) return surfaceContainerLow
        if (level === 3) return surfaceContainer
        if (level === 4) return surfaceContainerHigh
        return surfaceContainerHighest
    }
```

- [ ] **Step 3: Save, run a build to verify Theme.qml is syntactically valid**

```bash
make build
```

Expected: build succeeds with no new qmllint warnings. If a `Qt.tint` call fails to type-check, the build fails — fix the syntax before continuing.

### Task 1.2 — Make `Card.qml` color elevation-aware

- [ ] **Step 1: Open `src/app/qml/components/Card.qml` and locate the `color: Theme.tile` line (~line 27)**

Find this exact line:

```qml
    color: Theme.tile
```

- [ ] **Step 2: Replace with the elevation-aware binding**

Change the line to:

```qml
    color: root.elevation === 0 ? Theme.tile : Theme.surfaceAt(root.elevation)
```

Why `root.elevation === 0 ? Theme.tile : ...`: the existing visual contract for the dozens of `Card` consumers that don't set `elevation` (and rely on `Theme.tile` matching the sidebar polarity) must remain byte-identical. The new surface tokens kick in only when `elevation > 0`.

- [ ] **Step 3: Build to verify**

```bash
make build
```

Expected: build succeeds. No new warnings. The `Card` ABI is unchanged — `elevation: int` already exists.

### Task 1.3 — Wire drawers to `surfaceContainer`

- [ ] **Step 1: Open `src/app/qml/Main.qml` and find the Plugin Store drawer background**

```bash
grep -n "color: Theme.bgBase" src/app/qml/Main.qml
```

Expected: two matches, one for each drawer's `background: Rectangle` block (around lines 148-152 and 176-180 per the spec; line numbers may have drifted by upstream edits — use grep output as ground truth).

- [ ] **Step 2: Edit both `color: Theme.bgBase` lines to `color: Theme.surfaceContainer`**

Use `Edit` with `replace_all: false` twice (once per match), or `replace_all: true` if both occurrences are identical and only inside Drawer backgrounds — verify with `grep` that no other `color: Theme.bgBase` line exists in the file before using `replace_all`.

- [ ] **Step 3: Verify both lines were updated**

```bash
grep -n "color: Theme\." src/app/qml/Main.qml | grep -E "bgBase|surfaceContainer"
```

Expected: two lines containing `Theme.surfaceContainer`, zero containing `Theme.bgBase`.

### Task 1.4 — Build, lint, smoke test

- [ ] **Step 1: Full build**

```bash
make build
```

Expected: green. Look for any `qmllint` warning in output — must be zero.

- [ ] **Step 2: Run tests**

```bash
make test
```

Expected: green. No QML rendering tests exist; this just confirms no C++ regression slipped in.

- [ ] **Step 3: Manual smoke test — both themes**

```bash
make run
```

In the running app:

1. Open Plugin Store drawer (right-edge button) — confirm the drawer reads as a "warmer" surface than the main content. Close drawer.
1. Open Settings drawer (right-edge button) — confirm same warmth shift. Close drawer.
1. Switch theme to Light (Settings → Appearance) — confirm both drawers still show a perceptible tint, not jarring.
1. Switch back to Dark — confirm the warmth is preserved.

If the tint reads too aggressive in light mode (washed-out or pink-tinted), the opacity ladder can be tuned by halving the values (`0.025/0.04/0.055/0.07/0.08`) — but commit only if visual is acceptable as-is.

### Task 1.5 — Commit + push

- [ ] **Step 1: Stage the three files**

```bash
git add src/app/qml/Theme.qml \
        src/app/qml/components/Card.qml \
        src/app/qml/Main.qml
git status
```

Expected: 3 files modified, nothing else staged.

- [ ] **Step 2: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(ui): M3 surface-tint palette + Card/drawer wiring

Adds the M3 surfaceContainer{Lowest,Low,default,High,Highest} ladder to
Theme.qml — bgBase tinted with accent at the M3-spec opacity ladder
(5/8/11/14/16%). Unlike `tile` (which uses fgPrimary for polarity-flip
neutral elevation), these are *brand-flavored* surfaces — a warmth
shift toward the accent that reads as the same family in both dark
and light themes because the low alpha keeps bgBase dominant.

A `surfaceAt(level)` resolver picks the right token from an integer
elevation level, so Card.qml's existing `elevation` property now drives
both the shadow (via elevationOf) and the surface color.

Card.qml's `color` binding is gated on `elevation === 0` so the dozens
of pre-existing `Card` consumers that rely on `Theme.tile` matching the
sidebar polarity stay byte-identical. Levels 1-3 opt into the new
surface-tint ladder.

Main.qml's two right-edge drawers (Plugin Store + Settings) switch
their background from `Theme.bgBase` to `Theme.surfaceContainer`
(level 3), giving them perceptible "lift" over the main content.
EOF
)"
```

If pre-commit hooks fail (mdformat, clang-format, etc.), inspect the failure, fix it (re-stage), and retry the commit. Do **not** use `--no-verify`.

- [ ] **Step 3: Fetch + push**

```bash
git fetch origin
git log --oneline HEAD..origin/main   # expect empty
git push origin main
```

If `origin/main` advanced, `git pull --rebase origin main` then re-push.

______________________________________________________________________

## Task 2 — Ripple component + button integration

**Goal:** Create a reusable `components/Ripple.qml`, register it in the QML module, integrate into `PrimaryButton.qml` and `SecondaryButton.qml`.

**Files:**

- Create: `src/app/qml/components/Ripple.qml`
- Modify: `src/app/CMakeLists.txt` (register Ripple.qml in `QML_FILES`)
- Modify: `src/app/qml/components/PrimaryButton.qml`
- Modify: `src/app/qml/components/SecondaryButton.qml`

### Task 2.1 — Create Ripple.qml

- [ ] **Step 1: Create `src/app/qml/components/Ripple.qml`**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ripple.qml — Material 3 animated state-layer for press feedback.
//
// Reusable component. Anchor to the button's background `Rectangle` and
// trigger via `ripple.trigger(x, y)` from a `Connections` block on the
// button's `pressedChanged` signal. The expanding circle is clipped to
// the parent's rounded shape via a `MultiEffect` mask (same pattern used
// by Card.qml's elevation shadows — single visual-effects pipeline for
// clipped effects keeps the codebase coherent).
//
// API:
//   property color rippleColor    — fill color of the expanding circle
//   property real  rippleOpacity  — peak alpha during fade-in (0..1)
//   property int   duration       — total animation time (ms)
//   property real  cornerRadius   — corner radius of the parent surface
//   function trigger(x, y)        — restart animation from (x, y)
//
// Asymmetric opacity envelope (30% fade-in / 70% fade-out) is M3-spec
// — symmetric curves feel mechanical; asymmetric feels human.

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import AjazzControlCenter

Item {
    id: root

    /// Fill color of the expanding circle. Pick the on-container
    /// foreground color of the button it overlays.
    property color rippleColor: "white"
    /// Peak alpha during fade-in. M3 pressed state-layer: 0.12 (on
    /// neutral surfaces) — 0.16 (on saturated container colors).
    property real rippleOpacity: 0.16
    /// Total animation duration; bind to `Theme.durationMedium` (≈ 280 ms).
    property int duration: 280
    /// Corner radius of the parent surface — needed for the mask.
    property real cornerRadius: 0

    /// Restart the ripple animation from (x, y) in this Item's local
    /// coordinates. Center-origin callers pass (width/2, height/2).
    function trigger(x, y) {
        rippleCircle.originX = x
        rippleCircle.originY = y
        rippleAnim.restart()
    }

    layer.enabled: true
    layer.effect: MultiEffect {
        maskEnabled: true
        maskSource: Rectangle {
            width: root.width
            height: root.height
            radius: root.cornerRadius
        }
    }

    Rectangle {
        id: rippleCircle
        property real originX: 0
        property real originY: 0
        x: rippleCircle.originX - width / 2
        y: rippleCircle.originY - height / 2
        width: 0
        height: 0
        radius: width / 2
        color: root.rippleColor
        opacity: 0
    }

    ParallelAnimation {
        id: rippleAnim
        readonly property real maxDim: Math.max(root.width, root.height) * 2.5

        NumberAnimation {
            target: rippleCircle
            property: "width"
            from: 0
            to: rippleAnim.maxDim
            duration: root.duration
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: rippleCircle
            property: "height"
            from: 0
            to: rippleAnim.maxDim
            duration: root.duration
            easing.type: Easing.OutCubic
        }
        SequentialAnimation {
            NumberAnimation {
                target: rippleCircle
                property: "opacity"
                from: 0
                to: root.rippleOpacity
                duration: root.duration * 0.3
            }
            NumberAnimation {
                target: rippleCircle
                property: "opacity"
                from: root.rippleOpacity
                to: 0
                duration: root.duration * 0.7
                easing.type: Easing.InCubic
            }
        }
    }
}
```

### Task 2.2 — Register Ripple.qml in the QML module

- [ ] **Step 1: Open `src/app/CMakeLists.txt` and locate the `QML_FILES` list**

```bash
grep -n "qml/components/PrimaryButton.qml" src/app/CMakeLists.txt
```

Expected: one match (line 214 per latest inspection — may have drifted).

- [ ] **Step 2: Insert `qml/components/Ripple.qml` between `PrimaryButton.qml` and `SecondaryButton.qml`**

Find this block (around lines 214-216):

```cmake
              qml/components/PrimaryButton.qml
              qml/components/SecondaryButton.qml
              qml/components/Toast.qml
```

Replace with:

```cmake
              qml/components/PrimaryButton.qml
              qml/components/Ripple.qml
              qml/components/SecondaryButton.qml
              qml/components/Toast.qml
```

Order is alphabetical (`P` < `R` < `S` < `T`) — matches the existing convention.

- [ ] **Step 3: Reconfigure CMake so the new file is picked up**

```bash
make configure
```

Expected: `Configuring done`, no errors. If CMake complains that `Ripple.qml` is missing, the file path in step 2 is wrong — fix it.

### Task 2.3 — Integrate Ripple into PrimaryButton.qml

- [ ] **Step 1: Open `src/app/qml/components/PrimaryButton.qml`**

The current file has a `background: Rectangle { ... }` block (lines 20-26). The `Ripple` instance lives **inside** that Rectangle so it inherits the rounded shape.

- [ ] **Step 2: Replace the `background` block with the Ripple-enhanced version**

Find this exact block:

```qml
    background: Rectangle {
        radius: Theme.radiusMd
        color: root.down ? Qt.darker(Theme.accent, 1.2)
                          : (root.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
        border.width: root.activeFocus ? Theme.focusRingWidth : 0
        border.color: Theme.fgPrimary
    }
```

Replace with:

```qml
    background: Rectangle {
        id: bg
        radius: Theme.radiusMd
        color: root.down ? Qt.darker(Theme.accent, 1.2)
                          : (root.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
        border.width: root.activeFocus ? Theme.focusRingWidth : 0
        border.color: Theme.fgPrimary

        Ripple {
            id: ripple
            anchors.fill: parent
            cornerRadius: bg.radius
            rippleColor: "#0e1011"   // matches contentItem text color
            rippleOpacity: 0.16       // M3 on-primary @ pressed
            duration: Theme.durationMedium
        }
    }

    Connections {
        target: root
        function onPressedChanged() {
            if (root.pressed)
                ripple.trigger(ripple.width / 2, ripple.height / 2)
        }
    }
```

Why `id: bg` is added: the `Ripple`'s `cornerRadius` binding needs to reference the parent Rectangle's `radius`. Without an `id`, `parent.radius` works but is less robust to refactoring.

Why center-origin (`ripple.width / 2`, `ripple.height / 2`): chosen trade-off per spec section 3.3. Press-point origin is a follow-up.

### Task 2.4 — Integrate Ripple into SecondaryButton.qml

- [ ] **Step 1: Open `src/app/qml/components/SecondaryButton.qml`**

- [ ] **Step 2: Replace the `background` block + add the `Connections` block**

Find this exact block:

```qml
    background: Rectangle {
        radius: Theme.radiusMd
        color: root.hovered ? Theme.bgRowHover : "transparent"
        border.width: root.activeFocus ? Theme.focusRingWidth : 1
        border.color: root.activeFocus ? Theme.accent : Theme.borderSubtle
    }
```

Replace with:

```qml
    background: Rectangle {
        id: bg
        radius: Theme.radiusMd
        color: root.hovered ? Theme.bgRowHover : "transparent"
        border.width: root.activeFocus ? Theme.focusRingWidth : 1
        border.color: root.activeFocus ? Theme.accent : Theme.borderSubtle

        Ripple {
            id: ripple
            anchors.fill: parent
            cornerRadius: bg.radius
            rippleColor: Theme.fgPrimary
            rippleOpacity: 0.12          // M3 on-surface @ pressed (lower than primary)
            duration: Theme.durationMedium
        }
    }

    Connections {
        target: root
        function onPressedChanged() {
            if (root.pressed)
                ripple.trigger(ripple.width / 2, ripple.height / 2)
        }
    }
```

### Task 2.5 — Build, lint, smoke test

- [ ] **Step 1: Full build**

```bash
make build
```

Expected: green. Watch for `qmllint` warnings in build output — must be zero. Common issues:

- "unqualified access" on `bg.radius` → `pragma ComponentBehavior: Bound` is missing from a file that needs it. Both buttons should already have implicit binding scope; if a warning appears, add the pragma to the offending file.

- "Cannot find Ripple" → the QML module didn't pick up the new file. Run `make configure` then `make build` again.

- [ ] **Step 2: Run tests**

```bash
make test
```

Expected: green.

- [ ] **Step 3: Manual smoke test — both themes, both input modes**

```bash
make run
```

In the running app:

1. Click any `PrimaryButton` (e.g. Apply in the sticky footer of the Profile editor). Confirm: an expanding ripple visible from button center, fades within ~280 ms, clipped to rounded corners (no rectangular overflow).
1. Click any `SecondaryButton` (e.g. Revert). Same expectation, slightly more subtle (lower opacity, neutral color).
1. Tab to a button using the keyboard → press `Space` or `Enter`. Same ripple should trigger from keyboard activation.
1. Switch theme to Light. Click both buttons again. Confirm ripples remain visible without being overwhelming.

If the ripple trails outside the button corners, `cornerRadius: bg.radius` is not propagating — verify the `MultiEffect maskSource` block in `Ripple.qml`.

### Task 2.6 — Commit + push

- [ ] **Step 1: Stage all 4 files**

```bash
git add src/app/qml/components/Ripple.qml \
        src/app/qml/components/PrimaryButton.qml \
        src/app/qml/components/SecondaryButton.qml \
        src/app/CMakeLists.txt
git status
```

Expected: 1 new file, 3 modified, nothing else.

- [ ] **Step 2: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(ui): M3 ripple state-layer for buttons

New reusable components/Ripple.qml — radial expanding circle clipped
to a rounded shape via MultiEffect mask (same pattern used by Card.qml
for elevation shadows; single visual-effects pipeline for clipped
effects). API: rippleColor, rippleOpacity, duration, cornerRadius;
trigger(x, y) restarts the animation from (x, y) in local coords.

Asymmetric opacity envelope (30% fade-in via OutCubic, 70% fade-out
via InCubic) — M3-spec asymmetric. Symmetric curves feel mechanical.

PrimaryButton + SecondaryButton both embed a Ripple inside their
background Rectangle and trigger it from a Connections block on
`pressedChanged`. Center-origin (ripple.width/2, ripple.height/2)
chosen over press-point — simpler, robust to keyboard activation
(Space/Enter trigger identical animation), visual cost < 100 ms
perceptual on buttons ≤ 120 px wide. Press-point promotion is purely
internal to Ripple.qml if a future iteration needs it.

The static hover/down state styling (Qt.lighter / Qt.darker on the
accent fill) stays untouched — the ripple is additive over the static
state layer, per M3 spec.

Ripple colors picked to match the on-container foreground:
- PrimaryButton: #0e1011 @ 0.16 (matches contentItem text on accent)
- SecondaryButton: Theme.fgPrimary @ 0.12 (lower for neutral surface)
EOF
)"
```

- [ ] **Step 3: Fetch + push**

```bash
git fetch origin
git log --oneline HEAD..origin/main   # expect empty
git push origin main
```

______________________________________________________________________

## Task 3 — Sigstore release attestation

**Goal:** Add SLSA Level 3 build-provenance attestation to the `release.yml` `publish` job + a "Verifying release artifacts" section in `docs/wiki/Release-Process.md`.

**Files:**

- Modify: `.github/workflows/release.yml` (publish job: explicit permissions block + new step)
- Modify: `docs/wiki/Release-Process.md` (new section)

### Task 3.1 — Resolve the `attest-build-provenance` SHA pin

- [ ] **Step 1: Fetch the latest stable release of `actions/attest-build-provenance`**

```bash
gh api repos/actions/attest-build-provenance/releases/latest \
    --jq '"\(.tag_name) \(.target_commitish)"'
```

Expected output: `vX.Y.Z <sha>` where `<sha>` is a 40-char commit hash. Record both. The `target_commitish` may be a branch name (e.g. `main`); if so, resolve the actual tag SHA:

```bash
TAG=$(gh api repos/actions/attest-build-provenance/releases/latest --jq '.tag_name')
gh api repos/actions/attest-build-provenance/git/refs/tags/$TAG --jq '.object.sha'
```

Use the result as the SHA pin in step 3.2. Note the major version (e.g. `v2`) for the trailing comment.

### Task 3.2 — Edit `.github/workflows/release.yml`

- [ ] **Step 1: Open `.github/workflows/release.yml` and locate the `publish` job (line 132)**

The job currently looks like this (lines 132-169):

```yaml
  publish:
    name: Publish GitHub release
    needs: [ linux-packages, linux-flatpak, windows-msi, macos-dmg ]
    runs-on: ubuntu-24.04
    timeout-minutes: 15
    steps:
      - uses: actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c  # v8.0.1
        with: { path: dist }
      - name: List artifacts
        run: find dist -type f
      - name: Generate SHA256 checksums
        ...
      - name: Create release
        uses: softprops/action-gh-release@3bb12739c298aeb8a4eeaf626c5b8d85266b0e65  # v2.6.2
        ...
```

- [ ] **Step 2: Insert an explicit `permissions:` block right after `timeout-minutes: 15`**

Find this exact line:

```yaml
    timeout-minutes: 15
    steps:
```

Insert the permissions block between them:

```yaml
    timeout-minutes: 15
    permissions:
      contents: write           # for softprops/action-gh-release (already implicit at workflow level)
      id-token: write           # for Sigstore OIDC
      attestations: write       # for actions/attest-build-provenance
    steps:
```

- [ ] **Step 3: Insert the `Attest release artifacts` step between `Generate SHA256 checksums` and `Create release`**

Find this exact closing of the SHA256SUMS step (the line right before `- name: Create release`):

```yaml
          echo "=== SHA256SUMS ==="
          cat SHA256SUMS
      - name: Create release
```

Insert between them (use the SHA recorded in Task 3.1; replace `<sha>` and `vX.Y.Z` with actual values):

```yaml
          echo "=== SHA256SUMS ==="
          cat SHA256SUMS
      - name: Attest release artifacts
        # SLSA Level 3 build-provenance via Sigstore + GitHub OIDC.
        # Mitigates SEC-014 (release artifacts published unsigned).
        # Verify with: gh attestation verify <file> --owner Aiacos
        uses: actions/attest-build-provenance@<sha>  # vX.Y.Z
        with:
          subject-path: |
            dist/linux-packages/*
            dist/windows-packages/*
            dist/macos-packages/*
            dist/*.flatpak
      - name: Create release
```

- [ ] **Step 4: Verify the YAML syntax**

```bash
yamllint .github/workflows/release.yml
```

Expected: clean (zero errors, zero warnings). The pre-commit `yamllint` hook runs the same check.

If `yamllint` complains about indentation, the inserted block doesn't match the file's indent level — re-indent (this file uses 2-space indent, with step content at 6 spaces).

### Task 3.3 — Update `docs/wiki/Release-Process.md`

- [ ] **Step 1: Locate the "Signing & notarization" section in `docs/wiki/Release-Process.md`**

```bash
grep -n "^## " docs/wiki/Release-Process.md
```

Expected: Lists "Signing & notarization" at line 81, "Hotfix process" at line 89.

The new section goes **between** them — after "Signing & notarization", before "Hotfix process".

- [ ] **Step 2: Read the current "Signing & notarization" → "Hotfix process" boundary**

```bash
sed -n '80,92p' docs/wiki/Release-Process.md
```

Identify the exact blank-line boundary. Insert the new `## Verifying release artifacts` section above the `## Hotfix process` heading.

- [ ] **Step 3: Insert the new section**

Add this content immediately before `## Hotfix process` (the outer fence below uses 4 backticks so the inner `bash` block stays correctly nested — paste only the content between the outer \`\`\`\`markdown and the closing 4-backtick line):

````markdown
## Verifying release artifacts

Every release artifact is signed with [Sigstore](https://www.sigstore.dev/)
build provenance via GitHub Actions OIDC. The attestation links the
artifact to the exact commit and workflow run that produced it.

Verify with the GitHub CLI:

```bash
gh attestation verify <artifact> \
    --owner Aiacos --repo Aiacos/ajazz-control-center
```

This confirms the artifact was built by our release workflow at the
tagged commit, with no manual intervention. Failed verification means
the artifact was not produced by our CI — do not install it.

The attestation is in addition to the `SHA256SUMS` file attached to
each GitHub release; SHA256SUMS provides integrity (the file you
downloaded matches), `gh attestation verify` provides provenance
(the file came from our CI).

````

After saving, re-read the wiki file and confirm the new `## Verifying release artifacts` heading is followed by exactly one fenced code block (`bash`) and that the section terminates cleanly before `## Hotfix process`. `mdformat` may rewrite the fence style — that's expected and harmless.

### Task 3.4 — Validate workflow + docs

- [ ] **Step 1: Run all linters via pre-commit**

```bash
pre-commit run --files .github/workflows/release.yml docs/wiki/Release-Process.md
```

Expected: all hooks pass. If `mdformat` modifies the wiki file, that's expected — re-read the file to verify the changes are cosmetic only and proceed.

- [ ] **Step 2: Optional — validate the workflow against GitHub's schema**

If `actionlint` is available locally:

```bash
actionlint .github/workflows/release.yml
```

Expected: zero issues. (The repo's CI runs actionlint via the `lint` workflow already, but a local pre-flight saves a CI round-trip.)

### Task 3.5 — Commit + push

- [ ] **Step 1: Stage both files**

```bash
git add .github/workflows/release.yml docs/wiki/Release-Process.md
git status
```

Expected: 2 files modified, nothing else.

- [ ] **Step 2: Commit**

```bash
git commit -m "$(cat <<'EOF'
chore(ci): Sigstore build-provenance attestation for release artifacts

Adds SLSA Level 3 build-provenance attestation to release.yml's publish
job via actions/attest-build-provenance. Keyless signing via GitHub
OIDC — no private keys, no certs to renew. Attestations are pushed to
the public Rekor transparency log and visible in the workflow run's
"Attestations" tab.

The publish job gains an explicit permissions: block with id-token:
write + attestations: write. These are job-scope, not workflow-scope —
the four build jobs continue to inherit only contents: write from the
workflow default. Principle of least privilege.

Failure-fast semantics: if Sigstore or Rekor are temporarily down, the
attestation step fails, the publish job fails, and the release is
*not* published. Re-run via workflow_dispatch when the infra is back.
This is preferable to shipping unsigned artifacts and reintroduces no
gap relative to the SEC-014 audit finding (release artifacts published
unsigned).

docs/wiki/Release-Process.md gains a "Verifying release artifacts"
section documenting `gh attestation verify`. README.md is intentionally
unchanged — gh attestation is a maintainer / security-researcher tool;
the user-facing integrity check remains SHA256SUMS in the release tag.
EOF
)"
```

- [ ] **Step 3: Fetch + push**

```bash
git fetch origin
git log --oneline HEAD..origin/main   # expect empty
git push origin main
```

### Task 3.6 — Post-merge validation (optional, can be run async)

The release workflow only fires on `tags: [ 'v*' ]` push or `workflow_dispatch`. Validation requires either:

- [ ] **Option A: trigger a `workflow_dispatch` run for an existing tag**

```bash
gh workflow run release.yml -f tag=v0.0.0-attest-validate
```

(Use a throwaway tag name; cancel/delete the resulting GitHub release after verification.)

Expected: workflow run completes, "Attestations" tab on the run page shows entries for each artifact subject.

- [ ] **Option B: cut a real release on the next available version bump**

The next time a `v*` tag is pushed (a real release), the attestation step runs automatically. Verify after the run finishes:

```bash
gh attestation verify <one-of-the-artifacts> \
    --owner Aiacos --repo Aiacos/ajazz-control-center
```

Expected: exit code 0, the verification table prints with one row per attestation matched.

If verification fails on the first real run, capture the error output and open a follow-up TODO entry — the attestation step itself succeeded (otherwise the release wouldn't exist), so any verification failure is downstream (Rekor index lag is the most likely culprit; usually self-resolves within minutes).

______________________________________________________________________

## Optional stretch — Type-ramp audit on SettingsPage

Only execute this if all three primary tasks land cleanly with > 1 hour of session budget remaining. Pure polish, low risk, isolates well as a 4th independent commit.

**Goal:** Replace ad-hoc `font.pixelSize: <int>` literals on `SettingsPage.qml` with `Theme.typeBodyMedium`/`typeLabelLarge`/`typeTitleMedium` references. Demonstrates the M3 type-ramp pattern for a future page-by-page sweep.

### Task 4.1 — Inventory the literals

- [ ] **Step 1: Grep `SettingsPage.qml` for `pixelSize`**

```bash
grep -nE "pixelSize|font\.bold" src/app/qml/SettingsPage.qml
```

Each match is a candidate for type-ramp tokenisation. Map each by role:

- Section heading text → `Theme.typeTitleMedium`
- Body / list-row label → `Theme.typeBodyMedium`
- Helper text under a control → `Theme.typeBodySmall`
- Switch-delegate primary label → `Theme.typeLabelLarge`

### Task 4.2 — Replace each literal

For each `font.pixelSize: <N>` occurrence:

- [ ] **Step 1: Read the surrounding context (3-5 lines around the match)**

Identify the role from the visual hierarchy.

- [ ] **Step 2: Replace `font.pixelSize: <N>` with the role-typed bag**

Example:

```qml
// before
Text {
    text: "General"
    font.pixelSize: 16
    font.bold: true
}

// after
Text {
    text: "General"
    font.pixelSize: Theme.typeTitleMedium.pixelSize
    font.weight: Theme.typeTitleMedium.weight
    font.letterSpacing: Theme.typeTitleMedium.letterSpacing
}
```

(The `font.bold: true` becomes `font.weight: Theme.typeTitleMedium.weight` — `Font.Medium` (500) replaces the bold (700) for tighter M3 hierarchy.)

### Task 4.3 — Build, lint, smoke

- [ ] **Step 1: `make build && make test`** — same expectations as Task 1.4.
- [ ] **Step 2: Open the Settings drawer in `make run` and confirm visual hierarchy reads correctly.** No element should look heavier than its M3 role.

### Task 4.4 — Commit + push

- [ ] **Single commit:**

```bash
git add src/app/qml/SettingsPage.qml
git commit -m "$(cat <<'EOF'
refactor(ui): adopt M3 type-ramp tokens on SettingsPage

Replaces ad-hoc font.pixelSize / font.bold literals on SettingsPage.qml
with Theme.typeTitleMedium / typeBodyMedium / typeLabelLarge bags. The
M3 ramp tokens were added in 31f74a4 ("feat(ui): Material 3 polish —
motion + elevation + Snackbar") but no page consumed them yet.

Demonstrates the migration pattern for the future page-by-page audit
sweep tracked in TODO.md UI-polish section. Settings is the smallest
existing page, so this is the smoke-test page for the pattern; expand
to PluginStore / DeviceList / KeyDesigner only when the visual works
on this surface.

font.bold → font.weight: Theme.typeTitleMedium.weight (500/Medium)
intentionally — M3 reserves bold for emphasis runs inside body text,
not for headings; Medium gives tighter hierarchy.
EOF
)"
git fetch origin && git push origin main
```

______________________________________________________________________

## Self-review

After the implementation lands (or before, as a sanity pass), confirm:

1. **Three commits exist on `main` with the conventional types `feat(ui)`, `feat(ui)`, `chore(ci)`** — `git log --oneline -3`.
1. **Each commit is independently revertable:** `git revert --no-commit HEAD~2..HEAD~1` (test-only — abort with `git reset --hard HEAD`) does not pull cross-commit content.
1. **`qmllint-qt6` reports zero warnings** — already enforced by build, but spot-check via `make build` output.
1. **No new test failures:** `make test` is green.
1. **Visual regression smoke** in dark + light theme of: drawers (Plugin Store + Settings), buttons (Primary in sticky footer + Secondary in any modal), keyboard-triggered ripple (Tab + Space).
1. **Sigstore attestation is reachable:** Either workflow_dispatch test or natural release tag — see Task 3.6.
