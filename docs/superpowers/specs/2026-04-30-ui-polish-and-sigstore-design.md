# UI polish + Sigstore release attestation — design spec

| Field            | Value                                            |
| ---------------- | ------------------------------------------------ |
| Date             | 2026-04-30                                       |
| Branch           | `main` (direct-commit per project workflow)      |
| Owner            | `uni.lorenzo.a@gmail.com`                        |
| Scope            | One session, 3 atomic items (+ optional stretch) |
| Estimated effort | ~5–8 hours, 3 commits                            |

## 1. Context

This session ships three small, self-contained items selected from the
**UI polish (incremental)** and **Iceboxed** sections of `TODO.md`. The
underlying themes are *Material 3 expressive depth* and *supply-chain
hardening of release artifacts*.

Reverse-engineering work is happening in parallel on a separate Windows
machine and is **not** a dependency of any item in this spec. None of the
three items below require external user actions (no certificate purchase,
no account creation, no forum review threads). Each item ships as a single
commit directly to `main` per the project's parallel direct-commit
workflow.

The packaging triple (AppImage recipe, Snap recipe, Snap publishing docs)
was deliberately deferred to a follow-up session — Snap in particular has
several gotchas (nested QtWebEngine sandbox, `$SNAP_USER_DATA` rebind,
`raw-usb` manual-connect, out-of-band udev rules) that benefit from
focused attention. See research notes synthesised in the brainstorming
session preceding this spec.

## 2. Item 1 — Surface-tinted primary palette

### 2.1 Scope

Add the Material 3 *surface tint* token family to `src/app/qml/Theme.qml`
and wire it into `components/Card.qml` plus the two right-edge drawers
(Plugin Store and Settings).

### 2.2 Design

#### Theme.qml additions

Five new `readonly property color` tokens plus a resolver function:

```qml
readonly property color surfaceContainerLowest:
    Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.05))
readonly property color surfaceContainerLow:
    Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.08))
readonly property color surfaceContainer:
    Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.11))
readonly property color surfaceContainerHigh:
    Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.14))
readonly property color surfaceContainerHighest:
    Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.16))

function surfaceAt(level) {
    if (level <= 0) return bgBase
    if (level === 1) return surfaceContainerLowest
    if (level === 2) return surfaceContainerLow
    if (level === 3) return surfaceContainer
    if (level === 4) return surfaceContainerHigh
    return surfaceContainerHighest
}
```

The opacity ladder (`0.05`, `0.08`, `0.11`, `0.14`, `0.16`) matches the
Material 3 reference. Tint is applied with `accent` (brand primary), not
`fgPrimary` — so the tint is a *warmth shift toward the brand*, not a
luminance flip. This works in both polarities because the base color
(`bgBase`) dominates the low-opacity tint regardless of theme.

#### Card.qml integration

Existing surface color (`color: Theme.tile`) becomes elevation-aware:

```qml
color: root.elevation === 0 ? Theme.tile : Theme.surfaceAt(root.elevation)
```

This preserves the visual contract for every existing `Card` consumer
(default `elevation: 0` still resolves to `Theme.tile`), while elevation
1–3 now picks up the M3 surface-tint ladder.

#### Drawer integration

`PluginStore.qml` and `SettingsPage.qml` are mounted inside right-edge
modal `Drawer { ... }` instances in `Main.qml`. The two drawer
backgrounds (`Main.qml:148-152` and `Main.qml:176-180`) currently use
`color: Theme.bgBase`. Swap to `color: Theme.surfaceContainer` (level 3)
to give the drawer a perceptible "lift" over the main content.

Concrete diff (both drawers):

```diff
 background: Rectangle {
-    color: Theme.bgBase
+    color: Theme.surfaceContainer
     border.color: Theme.borderSubtle
     border.width: 1
 }
```

### 2.3 Trade-offs

- **Polarity-agnostic vs polarity-aware tint base**: chose
  polarity-agnostic (`accent` at low alpha over `bgBase`). The
  `tile`/`tileHover`/`borderSubtle` family already does
  polarity-aware `Qt.tint` with `fgPrimary` — those are *neutral
  elevation indicators*. Surface tint is *brand-flavored elevation*; it
  needs to feel like the same color family in both light and dark.
- **Opt-in via `elevation` property vs blanket replacement**: chose
  opt-in. Replacing `Theme.tile` everywhere would break consumers that
  rely on the sidebar-derived neutrality (notably `DeviceRow` and the
  AppHeader search field).
- **Five tokens vs three tokens**: M3 spec defines five (Lowest, Low,
  default, High, Highest). We expose all five even though our current
  call-sites only use levels 1 and 3. Consistency with the M3 reference
  pays back when future call-sites need finer granularity, and the cost
  is five lines.

### 2.4 Acceptance criteria

- `Theme.qml` exposes the five `surfaceContainer*` tokens and a
  `surfaceAt(level)` function.
- `Card.qml` resolves `color` via `Theme.surfaceAt(root.elevation)`
  when `elevation > 0`; default-zero behavior is byte-identical to
  the pre-change rendering.
- The Plugin Store drawer and Settings drawer use
  `Theme.surfaceContainer` for their background.
- `qmllint-qt6` reports zero warnings.
- `cmake --build` is green.
- `make test` is green (no test changes expected; tests exercise C++,
  not QML rendering).
- Visual smoke test: launch the app in dark theme and switch to light;
  open both drawers; confirm the tint reads as a "warmer" surface than
  `bgBase` in both modes, and is not jarring.

### 2.5 Effort

~2–3 hours. **One commit** (`Theme.qml` + 3 call-sites in one push,
because the visual story is incoherent if Theme tokens land alone).

## 3. Item 2 — Ripple effects on Primary/Secondary buttons

### 3.1 Scope

Create a reusable `src/app/qml/components/Ripple.qml` component and
integrate it into `PrimaryButton.qml` and `SecondaryButton.qml`.

### 3.2 Design

#### Ripple.qml component

```qml
// Item-based, no shaders. Clipped to a rounded shape via MultiEffect mask.
Item {
    id: root
    property color rippleColor: "white"
    property real  rippleOpacity: 0.16          // M3 pressed state layer
    property int   duration: Theme.durationMedium  // 280 ms
    property real  cornerRadius: Theme.radiusMd

    function trigger(x, y) {
        rippleCircle.x = x
        rippleCircle.y = y
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
        width: 0; height: 0
        radius: width / 2
        color: root.rippleColor
        opacity: 0
        transformOrigin: Item.Center
        // x/y set by trigger(); circle expands from press point
    }

    ParallelAnimation {
        id: rippleAnim
        readonly property real maxDim: Math.max(root.width, root.height) * 2.5

        NumberAnimation {
            target: rippleCircle; property: "width"
            from: 0; to: rippleAnim.maxDim
            duration: root.duration; easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: rippleCircle; property: "height"
            from: 0; to: rippleAnim.maxDim
            duration: root.duration; easing.type: Easing.OutCubic
        }
        SequentialAnimation {
            NumberAnimation {
                target: rippleCircle; property: "opacity"
                from: 0; to: root.rippleOpacity
                duration: root.duration * 0.3
            }
            NumberAnimation {
                target: rippleCircle; property: "opacity"
                from: root.rippleOpacity; to: 0
                duration: root.duration * 0.7
                easing.type: Easing.InCubic
            }
        }
    }
}
```

Note: `rippleCircle.x` and `rippleCircle.y` need to be offset by half the
animated width/height so the circle expands *from* the press point, not
the top-left of it. The actual implementation will bind
`x: pressPoint.x - width / 2` against a stored `pressPoint` Q_PROPERTY,
or use a simpler "always center-origin" variant — see trade-off 3.3.

#### Origin point — chosen variant

**Center-origin** (M3-light): trigger ripple from the geometric center
of the button on every `pressedChanged → true`. Simpler, robust, works
identically with mouse and keyboard activation. The visual difference
versus press-point origin is < 100ms perceptual on small buttons (≤ 120px
wide).

```qml
// PrimaryButton.qml integration
Connections {
    target: root
    function onPressedChanged() {
        if (root.pressed)
            ripple.trigger(ripple.width / 2, ripple.height / 2)
    }
}
```

If a future iteration wants press-point fidelity, replacing the
`Connections` block with a `TapHandler { onPressedChanged: ... }` is a
single-component-internal change with no API breakage.

#### PrimaryButton.qml integration

```qml
background: Rectangle {
    radius: Theme.radiusMd
    color: root.down ? Qt.darker(Theme.accent, 1.2)
                     : (root.hovered ? Qt.lighter(Theme.accent, 1.1)
                                     : Theme.accent)
    border.width: root.activeFocus ? Theme.focusRingWidth : 0
    border.color: Theme.fgPrimary

    Ripple {
        id: ripple
        anchors.fill: parent
        cornerRadius: parent.radius
        rippleColor: "#0e1011"   // matches contentItem text color
        rippleOpacity: 0.16       // M3 on-primary @ pressed
    }
}
```

#### SecondaryButton.qml integration

Identical structure; only the ripple parameters differ:

```qml
Ripple {
    id: ripple
    anchors.fill: parent
    cornerRadius: parent.radius
    rippleColor: Theme.fgPrimary
    rippleOpacity: 0.12          // M3 on-surface @ pressed
}
```

### 3.3 Trade-offs

- **Center-origin vs press-point origin**: chose center. Simpler, and
  the visual cost is small for buttons ≤ 120px wide. Promotion to
  press-point is purely internal to `Ripple.qml` if needed later.
- **State layer vs animated ripple**: kept the existing static
  `Qt.lighter` / `Qt.darker` hover/down state and *added* the animated
  ripple on top. M3 spec treats them as separate concerns; merging them
  would lose the immediate-feedback "pressed" tint.
- **`MultiEffect maskSource` vs `OpacityMask` from Qt5Compat**: chose
  `MultiEffect` to stay consistent with the elevation-shadow pattern
  already established in `Card.qml`. Single visual-effects pipeline,
  fewer imports, fewer warnings to chase.
- **Asymmetric fade-in/fade-out (30/70)**: M3-spec asymmetry. Symmetric
  curves feel mechanical; asymmetric feels human.

### 3.4 Acceptance criteria

- New file `src/app/qml/components/Ripple.qml` exists and is registered
  in `qt_add_qml_module`.
- Clicking either button produces a visible expanding circle that fades
  out within ~280ms, clipped to the button's rounded rectangle.
- Keyboard activation (Space/Enter) triggers the ripple identically.
- The pre-existing hover and down state styling continues to work
  (the ripple is additive, not replacing them).
- `qmllint-qt6` reports zero warnings.
- `cmake --build` is green.
- `make test` is green.
- Visual smoke test in both light and dark theme.

### 3.5 Effort

~2–3 hours. **One commit** (component + both integrations in one push
for visual coherence).

## 4. Item 3 — Sigstore release attestation

### 4.1 Scope

Add SLSA Level 3 build-provenance attestation to the GitHub Actions
release workflow, signed via Sigstore using GitHub OIDC. Document the
verification flow in the wiki.

### 4.2 Design

#### release.yml — publish job changes

The `publish` job gains an explicit job-scope `permissions` block (which
overrides the workflow-level `contents: write`) and one new step.

```yaml
  publish:
    name: Publish GitHub release
    needs: [ linux-packages, linux-flatpak, windows-msi, macos-dmg ]
    runs-on: ubuntu-24.04
    timeout-minutes: 15
    permissions:
      contents: write           # already implicit; explicit at job scope
      id-token: write           # NEW: Sigstore OIDC
      attestations: write       # NEW: upload attestation to GitHub
    steps:
      - uses: actions/download-artifact@<sha>
        with: { path: dist }
      - name: List artifacts
        run: find dist -type f
      - name: Generate SHA256 checksums
        # ...existing block unchanged...

      # NEW
      - name: Attest release artifacts
        uses: actions/attest-build-provenance@<sha>  # pin at impl time
        with:
          subject-path: |
            dist/linux-packages/*
            dist/windows-packages/*
            dist/macos-packages/*
            dist/*.flatpak

      - name: Create release
        uses: softprops/action-gh-release@<sha>
        # ...existing block unchanged...
```

#### Permission scope

The new permissions are job-scope, not workflow-scope. The other four
build jobs continue to inherit only `contents: write` from the workflow
default. This minimises blast radius: only the publish job ever holds an
OIDC token.

#### Failure semantics

The attestation step has no `continue-on-error`. If Sigstore or Rekor are
temporarily unavailable, the job fails, the release does not publish, and
the workflow can be re-run via `workflow_dispatch`. This is preferable to
shipping unsigned artifacts.

#### docs/wiki/Release-Process.md additions

A new "Verifying release artifacts" section documenting the
`gh attestation verify` flow:

```md
## Verifying release artifacts

Every release artifact is signed with [Sigstore](https://www.sigstore.dev/)
build provenance via GitHub Actions OIDC. Verify with the GitHub CLI:

    gh attestation verify <artifact> \
        --owner Aiacos --repo Aiacos/ajazz-control-center

This confirms the artifact was built by our release workflow at the
tagged commit, with no manual intervention. Failed verification means
the artifact was not produced by our CI — do not install it.
```

The `README.md` is intentionally not modified — `gh attestation` is a
maintainer/security-researcher tool, not an end-user one. The existing
SHA256SUMS in the GitHub release tag is still the user-facing integrity
check.

### 4.3 Trade-offs

- **Job-scope vs workflow-scope permissions**: chose job-scope.
  Principle of least privilege. The four build jobs do not need OIDC.
- **Per-artifact attestation vs single-artifact attestation**: the
  action creates one bundle per `subject-path` entry. We use globs that
  cover all release artifacts in one step. The result is one bundle per
  file, all attached to the workflow run.
- **`actions/attest-build-provenance` (SLSA L3) vs `actions/attest`
  (custom predicate)**: chose build-provenance. Standard predicate,
  out-of-the-box `gh attestation verify` support, no custom verifier
  needed.
- **Failure-fast vs best-effort**: chose failure-fast. The whole point
  of this work is to mitigate audit finding `SEC-014`; "best-effort
  signing" would reintroduce the same gap.

### 4.4 Acceptance criteria

- `release.yml` `publish` job declares an explicit `permissions:` block
  with `contents: write`, `id-token: write`, `attestations: write`.
- Step `Attest release artifacts` exists, pinned to a SHA, runs after
  artifact download and SHA256SUMS generation, before the release
  creation step.
- The other four jobs (`linux-packages`, `linux-flatpak`,
  `windows-msi`, `macos-dmg`) are byte-identical to the pre-change
  version.
- A test invocation (push a `v0.0.0-attest-test` tag, or trigger
  `workflow_dispatch`) shows attestations in the run's "Attestations"
  tab on github.com.
- `gh attestation verify <artifact> --owner Aiacos --repo Aiacos/ajazz-control-center` exits 0 for at least one
  downloaded artifact.
- `docs/wiki/Release-Process.md` has a "Verifying release artifacts"
  section.

### 4.5 Effort

~1–2 hours. **One commit** (workflow YAML + wiki page in one push).

## 5. Stretch — Type-ramp audit on one page (only if budget allows)

If the three primary items land cleanly with budget remaining, replace
ad-hoc `font.pixelSize: <int>` literals on `SettingsPage.qml` (smallest
existing page) with `Theme.typeBodyMedium`, `Theme.typeLabelLarge`, and
`Theme.typeTitleMedium` references. Single page, 5–10 lines changed,
~1 hour, demonstrates the pattern for a future page-by-page sweep.

This is **not committed work** — only ship if the three primary items
above land with > 1 hour remaining.

## 6. Out of scope

- AppImage packaging recipe — deferred to next session.
- Snap packaging recipe and publishing docs — deferred to next session.
- HID protocol fuzzer (libfuzzer + ASan) — needs CMake sanitizer
  variant, separate session.
- Any change to the four build jobs in `release.yml` — strictly
  publish-job-only changes.
- README updates for release verification — wiki is the right
  surface; README would mislead end users into thinking they need to
  run `gh attestation verify`.
- Press-point origin for Ripple — center-origin is M3-acceptable for
  buttons of this size; press-point promotion is internal to
  `Ripple.qml` and can ship later without API breakage.

## 7. Build sequence (commits in order)

1. `feat(ui): M3 surface-tint palette + Card/drawer wiring`
   - `Theme.qml` adds 5 tokens + `surfaceAt(level)`
   - `Card.qml` resolves `color` via `surfaceAt` when `elevation > 0`
   - `Main.qml:148` and `Main.qml:176` swap drawer background from
     `Theme.bgBase` to `Theme.surfaceContainer`
1. `feat(ui): M3 ripple state-layer for buttons`
   - New `components/Ripple.qml`
   - `PrimaryButton.qml` and `SecondaryButton.qml` integrate `Ripple`
   - `qt_add_qml_module` registers the new file
1. `chore(ci): Sigstore build-provenance attestation for release artifacts`
   - `release.yml` publish job: explicit permissions + new attest step
   - `docs/wiki/Release-Process.md` adds a "Verifying release artifacts"
     section

Each commit is independent and can be reverted in isolation.

## 8. Risks

| Risk                                                                          | Likelihood | Impact | Mitigation                                                                                                                                  |
| ----------------------------------------------------------------------------- | ---------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Surface tint with `accent` looks washed out in light theme                    | low        | low    | visual smoke test before commit; opacities can be tuned without breaking the API                                                            |
| Ripple `MultiEffect` mask interacts badly with `Button`'s internal MouseArea  | low        | medium | test mouse + keyboard activation paths before commit; fallback is to clip via `Item { clip: true }` (rectangular only — visible at corners) |
| `actions/attest-build-provenance` rejects the glob patterns in `subject-path` | low        | medium | test with a `workflow_dispatch` run on a `v0.0.0-attest-test` tag before merging to `main`                                                  |
| Wiki content doesn't render under `wiki-sync.yml` because of MD syntax        | very low   | low    | `mdformat` runs in pre-commit; the existing `Release-Process.md` is the same format                                                         |
| `make test` regression from QML changes                                       | very low   | low    | tests exercise C++ unit logic, not QML rendering                                                                                            |

## 9. References

- TODO.md — sections "UI polish (incremental)", "Iceboxed",
  "Security hardening" (audit finding `SEC-014`)
- Theme.qml — existing M3 typography, motion, elevation tokens
- Card.qml — existing `elevation` Q_PROPERTY + MultiEffect shadow pattern
- release.yml — existing 4-job + publish-job structure
- Material 3 surface tint reference:
  https://m3.material.io/styles/color/the-color-system/color-roles
- Material 3 motion reference:
  https://m3.material.io/styles/motion/easing-and-duration/tokens-specs
- Sigstore + GitHub OIDC keyless signing:
  https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations
- `actions/attest-build-provenance`:
  https://github.com/actions/attest-build-provenance
