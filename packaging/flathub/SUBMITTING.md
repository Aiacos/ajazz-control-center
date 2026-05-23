# Submitting AJAZZ Control Center to Flathub

This document is the step-by-step runbook for getting
**`io.github.Aiacos.AjazzControlCenter`** onto [Flathub](https://flathub.org),
and for shipping each subsequent release. It tracks the current (2025–2026)
Flathub onboarding process.

The Flathub-ready manifest lives next to this file:
[`io.github.Aiacos.AjazzControlCenter.yml`](./io.github.Aiacos.AjazzControlCenter.yml).
It is the manifest you submit — it builds **from source** with pinned/verifiable
git sources, unlike `packaging/flatpak/*.yml` which builds from a local
`type: dir` for our own release CI.

> Sources (read these first, they are authoritative):
> - Submission process: <https://docs.flathub.org/docs/for-app-authors/submission>
> - Requirements: <https://docs.flathub.org/docs/for-app-authors/requirements>
> - MetaInfo guidelines: <https://docs.flathub.org/docs/for-app-authors/metainfo-guidelines>
> - Quality guidelines: <https://docs.flathub.org/docs/for-app-authors/metainfo-guidelines/quality-guidelines>
> - Runtimes: <https://docs.flathub.org/docs/for-app-authors/runtimes>
> - Linter / build checks: <https://docs.flathub.org/docs/for-app-authors/linter>
> - Maintenance / updating: <https://docs.flathub.org/docs/for-app-authors/maintenance>

---

## 0. Prerequisites

- A GitHub account with **2FA enabled** (Flathub grants you write access to your
  app's repo on merge and requires 2FA; you must accept the invite within a week).
- `flatpak` and the Flatpak Builder installed from Flathub:
  ```sh
  flatpak install flathub org.flatpak.Builder
  ```
- The KDE SDK/Platform for the runtime version pinned in the manifest:
  ```sh
  flatpak install flathub org.kde.Sdk//6.8 org.kde.Platform//6.8
  ```
- Some familiarity with Git and Flatpak (Flathub assumes this).

---

## 1. Pre-submission requirements checklist

Flathub enforces these. Tick every box before opening a PR.

### Application ID
- [x] Reverse-DNS, 3–5 components, chars `[A-Za-z0-9_]` (dash only in last
  component). `io.github.Aiacos.AjazzControlCenter` is valid.
- [x] Code-hosting prefix `io.github.` is correct because the project is hosted at
  `github.com/Aiacos`. The casing of the user segment (`Aiacos`) must match the
  GitHub account.

### Runtime (EOL policy)
- [x] Must use a **non-EOL runtime available at submission time**. Flathub rejects
  apps on EOL runtimes/extensions. `org.kde.Platform` **6.6 and earlier are EOL**;
  6.7 is aging out. The manifest targets **6.8** (current Qt 6.8 LTS-based KDE
  runtime). Re-check at submission time and bump to the newest non-EOL branch if
  KDE has published one. High-risk EOL deps (OpenSSL 1.x, Python 2, Qt5 WebKit)
  with network access are outright prohibited.

### Build from source / no binaries
- [x] All sources are publicly accessible and built from source. The app module
  uses `type: git` pinned to **tag `v0.1.0` + its commit**; hidapi/pybind11/
  nlohmann-json are likewise pinned git tags + commits. No `type: dir`, no
  prebuilt binaries. (Non-redistributable blobs would need `type: extra-data` —
  we have none.)

### Permissions (keep minimal; prefer portals)
- [x] Static permissions are minimal: `ipc`, `wayland` + `fallback-x11`, `dri`,
  `--device=usb` (fine-grained HID/USB via the device portal — preferred by the
  linter over `--device=all`), and two scoped `xdg-config`/`xdg-data` subdirs.
  No broad `--filesystem=host`, no `--talk-name` bus access.

### Stable release only
- [x] Flathub forbids beta/nightly builds. Submit a tagged stable release
  (`v0.1.0`). **Note:** the app is described as "Initial alpha scaffolding" in the
  metainfo; if it is genuinely pre-alpha, consider whether it is ready for a
  general-audience store before submitting.

### Metainfo (AppStream) — REQUIRED, must pass validation
The metainfo currently lives at
`resources/linux/io.github.Aiacos.AjazzControlCenter.appdata.xml` and is installed
to `/app/share/metainfo/`. The following **must** be true for the Flathub build to
pass (`appstreamcli compose` + linter run on every build):

- [x] `<id>` equals the app-id.
- [x] `<metadata_license>` (CC0-1.0) and `<project_license>` (SPDX
  `GPL-3.0-or-later`) present.
- [x] `<name>`, `<summary>` present. Summary should be short (a few words, no
  trailing period, not a duplicate of the name).
- [x] `<description>` has at least one non-empty `<p>`/`<ul>`/`<ol>`.
- [x] `<launchable type="desktop-id">` — the manifest's post-install repoints this
  to `io.github.Aiacos.AjazzControlCenter.desktop`.
- [x] `<url type="homepage">` present (bugtracker/help also present — good).
- [x] `<content_rating type="oars-1.1"/>` present.
- [x] `<releases>` present (required to pass validation).
- [ ] **`<screenshots>` — MISSING and REQUIRED.** "All graphical applications must
  have one or more screenshots." Each `<screenshot>` needs an `<image>` and a
  `<caption>`. Images **must be hosted at a stable URL from a git tag/commit, not
  a branch** (e.g. raw.githubusercontent.com pointing at the `v0.1.0` tag, or a
  release asset). Add a `<screenshots>` block before submitting — the build will
  be rejected without it.
- [ ] **Developer tag should use the modern form.** Current file uses the
  deprecated `<developer_name>`. Add the new
  `<developer id="io.github.Aiacos"><name>Lorenzo Argentieri</name></developer>`
  form (keep `<developer_name>` for older AppStream if desired, but the new tag is
  what current validation expects).
- [ ] Recommended (not blocking, but improves the listing): `<url type="vcs-browser">`,
  `<url type="donation">`, `<url type="contact">`, `<url type="contribute">`, and
  light/dark `<branding><color>` entries. See the MetaInfo guidelines.
- [x] **Community/proprietary disclaimer:** Flathub requires apps that are not the
  official vendor offering to state they are *not officially supported*. This is a
  third-party AJAZZ controller — add a sentence to the `<description>` such as
  "This is an unofficial, community-developed application and is not affiliated
  with or endorsed by AJAZZ." (treat as a blocker for review).

### Desktop file + icon
- [x] `.desktop` is renamed to the app-id by post-install and `Icon=` repointed to
  the app-id.
- [x] Icon installed as the app-id (PNG hicolor sizes; min 256×256 PNG, SVG
  preferred — note the manifest deliberately removes the SVG due to an
  appstreamcli rasterise failure, leaving PNGs incl. 256px which satisfies the
  minimum).

### License redistribution
- [x] GPL-3.0-or-later permits redistribution. Optionally install the license to
  `$FLATPAK_DEST/share/licenses/$FLATPAK_ID/`.

---

## 2. Build and test locally (do this BEFORE opening a PR)

From a checkout of the Flathub repo (or this `packaging/flathub/` directory),
build and install into the user installation:

```sh
flatpak-builder --user --install --force-clean build-dir \
  io.github.Aiacos.AjazzControlCenter.yml
```

(Equivalently `flatpak run org.flatpak.Builder --user --install --force-clean
build-dir <manifest>`.) Then run it:

```sh
flatpak run io.github.Aiacos.AjazzControlCenter
```

Plug in an AJAZZ device and confirm it is detected (HID/USB access via the
`--device=usb` portal grant).

---

## 3. Run the Flathub linter (must pass — the build-bot runs it too)

Both the **manifest** check and the **repo** check are run on every Flathub build
and can be run locally. Build with `--repo=repo` so the repo check has an OSTree
repo to inspect:

```sh
# Build producing an OSTree repo named `repo`
flatpak run org.flatpak.Builder --force-clean --repo=repo \
  build-dir io.github.Aiacos.AjazzControlCenter.yml

# Lint the manifest
flatpak run --command=flatpak-builder-lint org.flatpak.Builder \
  manifest io.github.Aiacos.AjazzControlCenter.yml

# Lint the resulting repo
flatpak run --command=flatpak-builder-lint org.flatpak.Builder \
  repo repo
```

Fix every error (and ideally every warning) before submitting. Common ones for
this app: missing screenshots, `--device=all` (use `--device=usb`), and AppStream
validation failures.

---

## 4. Open the submission PR

The submission is a PR against the **`new-pr`** branch of
`github.com/flathub/flathub` — **never** against `master`.

1. Fork `github.com/flathub/flathub`. When forking, **uncheck "Copy the master
   branch only"** so you get the `new-pr` branch.

2. Clone your fork's `new-pr` branch and create a submission branch:
   ```sh
   git clone --branch=new-pr git@github.com:YOUR_USERNAME/flathub.git
   cd flathub
   git checkout -b ajazz-control-center new-pr
   ```

3. Add the submission files at the repo root. At minimum the manifest. If you keep
   the metainfo/screenshots in-tree for the submission, add them too; otherwise the
   build pulls them from the pinned git source.
   ```sh
   cp /path/to/packaging/flathub/io.github.Aiacos.AjazzControlCenter.yml .
   git add io.github.Aiacos.AjazzControlCenter.yml
   git commit -m "Add io.github.Aiacos.AjazzControlCenter"
   git push -u origin ajazz-control-center
   ```

4. Open a PR on GitHub:
   - **Base branch:** `new-pr` (NOT `master`).
   - **Title:** `Add io.github.Aiacos.AjazzControlCenter`.

---

## 5. Review and build-bot flow

- A reviewer examines the submission and requests changes via PR comments.
  (Flathub policy: submissions must be human-reviewed; AI-generated submissions
  without human review are not accepted.)
- Once comments are resolved, a **test build** is triggered by commenting on the
  PR:
  ```
  bot, build
  ```
  The build-bot builds the manifest and runs the manifest + repo lint checks. Read
  the bot's reply / build log and fix any failures, then comment `bot, build`
  again.
- On approval, maintainers **merge** the app into a new repository under the
  `flathub` GitHub org (`github.com/flathub/io.github.Aiacos.AjazzControlCenter`)
  and grant you **write access**. Accept the invite within one week (2FA required).
- Publishing typically takes ~1–2 hours after merge; the app appears on
  flathub.org within a few hours after that.

---

## 6. Per-release update checklist

After the app is on Flathub, updates are committed to the app's own repo
(`github.com/flathub/io.github.Aiacos.AjazzControlCenter`), normally via a PR to
its `master` (the build-bot builds the PR; merge to publish).

For each new tagged release `vX.Y.Z`:

1. **Tag and push the release** in `Aiacos/ajazz-control-center`; note the tag's
   commit SHA:
   ```sh
   git rev-list -n 1 vX.Y.Z
   ```
2. **Update the app module source** in
   `io.github.Aiacos.AjazzControlCenter.yml`:
   - `tag: vX.Y.Z`
   - `commit: <the SHA from step 1>`
   (Both must be updated — Flathub requires the commit to be pinned, not just the
   tag.)
3. **Bump the metainfo `<releases>`** with a new `<release version="X.Y.Z"
   date="YYYY-MM-DD">` and release notes. AppStream validation expects the latest
   release entry to match the version being shipped.
4. **Refresh screenshots** if the UI changed (re-point image URLs at the new tag,
   not a branch).
5. **Check the runtime is still non-EOL.** If `org.kde.Platform` `runtime-version`
   has gone EOL, bump it (and `org.kde.Sdk`) to the current branch and re-test.
   Flathub emails maintainers and the build will warn when the runtime is EOL.
6. **Re-pin dependency modules** (hidapi/pybind11/nlohmann-json) only if you bump
   their versions; keep tag+commit in sync with the root `CMakeLists.txt`
   FetchContent tags.
7. **Rebuild and lint locally** (sections 2 and 3). All checks green.
8. **Commit and PR** to the app repo's `master`; comment `bot, build` if a fresh
   build is needed; merge to publish.

> Tip: enable Flathub's external-data / update bots later if you switch the app
> source to a release tarball with a checksum — they can auto-open update PRs.
> With a `type: git` tag+commit source you update manually as above.
