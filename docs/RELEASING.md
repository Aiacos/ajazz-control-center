# Development workflow, CI/CD, and releasing

This guide is the single source of truth for **how code flows from a branch to
a published release**: the branching model, what CI/CD does at each step, and
the exact steps to cut a release.

For the day-to-day contribution mechanics (commit style, local pre-commit,
tests) see [`CONTRIBUTING.md`](../CONTRIBUTING.md).

______________________________________________________________________

## 1. Branching model — GitFlow-lite

Two long-lived branches:

| Branch    | Role                                                                                            |
| --------- | ----------------------------------------------------------------------------------------------- |
| `develop` | **Integration / default branch.** All work merges here first.                                   |
| `main`    | **Release-only.** Receives a single promotion PR from `develop`, then carries the release tags. |

Flow:

```
feat/… ┐
fix/…  ├──PR──▶ develop ──promotion PR──▶ main ──tag vX.Y.Z──▶ Release
chore/…┘
```

- Cut short-lived topic branches **off `develop`** (`feat/…`, `fix/…`,
  `docs/…`, `chore/…`) and open a PR **into `develop`**.
- `main` **never** receives feature PRs directly — only the `develop → main`
  promotion PR (see §3).
- Both branches are protected: no direct pushes, no force-push, PR required.
  Never push directly to `develop` or `main`.

______________________________________________________________________

## 2. CI/CD pipeline

All workflows live in [`.github/workflows/`](../.github/workflows/).

### Per-PR / per-push

| Workflow              | Trigger                              | What it does                                                                                                                                      |
| --------------------- | ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| **CI** (`ci.yml`)     | push/PR to `main`, `develop`         | Build + test matrix (ubuntu / windows-2022 / macos-14); `Code coverage` (push only, ≥40% floor); `Sanitizers` (ASan+UBSan, TSan).                 |
| **Lint** (`lint.yml`) | push to `main`/`develop`, any PR     | Runs every pre-commit hook; **auto-fixes** formatting and pushes a `style:` commit back to the branch. Plus `clang-tidy` and a docs/markdown job. |
| **CodeQL**            | push/PR to `main`/`develop` + weekly | Static security analysis (C++ + Python).                                                                                                          |
| **Secret scan**       | push/PR to `main`/`develop` + weekly | gitleaks.                                                                                                                                         |
| **Dependency Review** | PR to `main`/`develop`               | Flags vulnerable / incompatibly-licensed dependency changes.                                                                                      |

**Required status checks** (must be green before a PR merges to `develop` or
`main`): `pre-commit (all hooks)`, `clang-tidy (static analysis)`,
`docs (markdown + links)`. The build matrix and sanitizers are **not** required
(matrix job names are version-interpolated and brittle); add them from
**Settings → Branches** if you want them enforced.

> **Auto-fix note:** the Lint workflow pushes auto-fix commits using
> `secrets.LINT_AUTOFIX_PAT` (a repo-admin PAT) so the fix commit re-triggers
> the checks. Without that secret it falls back to `GITHUB_TOKEN`, whose pushes
> do **not** re-trigger workflows — the PR then sits "waiting for status" on the
> fix commit until you re-push or close/reopen it. Running `make lint-all`
> locally before pushing avoids the whole dance.

### Release-time

| Workflow                                | Trigger                                | What it does                                                                                                                                                                                                                     |
| --------------------------------------- | -------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Nightly** (`nightly.yml`)             | push to `main`, daily cron, manual     | Builds rolling artifacts and force-moves the `nightly` pre-release. Not a release.                                                                                                                                               |
| **Release** (`release.yml`)             | push tag `v*`, or manual dispatch      | Builds `.deb` `.rpm` `.flatpak` `.dmg` `.msi` `.zip` + `SHA256SUMS`, signs/notarizes (if certs present), attaches SLSA provenance, and publishes the GitHub Release.                                                             |
| **Sync packages** (`sync-packages.yml`) | release `published`, or manual         | Rewrites every `packaging/` manifest (winget, Chocolatey, Homebrew, AUR, Flathub, Fedora, Ubuntu, Snap) to the new version/URL/checksum, commits them to `main`, and auto-submits the channels whose credentials are configured. |
| **Publish wiki** (`wiki.yml`)           | push to `main` touching `docs/wiki/**` | Mirrors `docs/wiki/` to the GitHub Wiki.                                                                                                                                                                                         |

______________________________________________________________________

## 3. Cutting a release

A release is **just a tag on `main`**. The pipeline does the rest. Pick the
new version `X.Y.Z` per [SemVer](https://semver.org/).

### Step 1 — Prepare the version bump (on a topic branch off `develop`)

```bash
git checkout develop && git pull
git checkout -b chore/release-X.Y.Z
```

1. Bump the project version in `CMakeLists.txt`:

   ```cmake
   project(
       AjazzControlCenter
       VERSION X.Y.Z   # ← match the tag you will push
       ...
   ```

   This must match the tag — the built installers take their version from
   here, while `sync-packaging.py` takes it from the tag; a mismatch produces
   inconsistent artifact names.

1. Move the `## [Unreleased]` block in `CHANGELOG.md` to a dated section:

   ```markdown
   ## [Unreleased]

   ## [X.Y.Z] - YYYY-MM-DD
   ```

1. Commit (`chore(release): bump to X.Y.Z`), push, open a PR **into
   `develop`**, let CI go green, merge.

### Step 2 — Promote `develop` → `main`

```bash
gh pr create --base main --head develop \
  --title "Promote develop → main (vX.Y.Z)"
```

Wait for the required checks to pass, then merge (a **merge commit**, not
squash — preserve the history). Do **not** delete `develop`.

### Step 3 — Tag `main` to trigger the release

```bash
git checkout main && git pull
git tag vX.Y.Z          # tag matches CMakeLists VERSION
git push origin vX.Y.Z  # ← this starts the Release workflow
```

The `Release` workflow now builds and publishes the GitHub Release with all
installers + `SHA256SUMS`. On `release: published`, `sync-packages` updates the
in-repo package manifests and submits the automated channels.

> Re-running without re-tagging: `gh workflow run release.yml -f tag=vX.Y.Z`
> (the `workflow_dispatch` input republishes the same tag).

### Step 4 — Verify

- The [Release](https://github.com/Aiacos/ajazz-control-center/releases) has
  all five installers + `SHA256SUMS`.
- `sync-packages` committed the manifest bump to `main` and (if configured)
  opened the winget PR. Remaining channels are moderated manual submissions —
  see [`packaging/PUBLISHING.md`](../packaging/PUBLISHING.md).

______________________________________________________________________

## 4. Hotfix

For an urgent fix on a published release: branch off `main`
(`fix/hotfix-X.Y.Z+1`), PR **into `main`**, tag, then **back-merge `main` into
`develop`** so the fix isn't lost on the next promotion.
