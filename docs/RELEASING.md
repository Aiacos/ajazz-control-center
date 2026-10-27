# Development workflow, CI/CD, and releasing

This guide is the single source of truth for **how code flows from a branch to
a published release**: the branching model, what CI/CD does at each step, and
the exact steps to cut a release.

For the day-to-day contribution mechanics (commit style, local pre-commit,
tests) see [`CONTRIBUTING.md`](../CONTRIBUTING.md).

______________________________________________________________________

## 1. Branching model — GitFlow-lite

Two long-lived branches:

| Branch | Role                                                                                                                  |
| ------ | --------------------------------------------------------------------------------------------------------------------- |
| `dev`  | **Integration / default branch.** All work merges here first.                                                         |
| `main` | **Release branch.** Receives reviewed PRs from `dev` or an in-repository topic branch, then carries the release tags. |

Flow:

```
feat/… ┐
fix/…  ├──PR──▶ dev ──promotion PR──▶ main ──tag vX.Y.Z──▶ Release
chore/…┘
```

- Cut short-lived topic branches **off `dev`** (`feat/…`, `fix/…`,
  `docs/…`, `chore/…`) and open a PR **into `dev`**.
- Pull requests into `main` must come from this repository and pass the full
  required check set. The normal release path remains `dev → main`; a fix may
  target `main` directly from a topic branch when needed.
- Both branches are protected: no direct pushes, no force-push, PR required.
  Never push directly to `dev` or `main`.

______________________________________________________________________

## 2. CI/CD pipeline

All workflows live in [`.github/workflows/`](../.github/workflows/).

### Per-PR / per-push

| Workflow              | Trigger                           | What it does                                                                                                                                      |
| --------------------- | --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| **CI** (`ci.yml`)     | push/PR to `main`, `dev`          | Build + test matrix (ubuntu / windows-2022 / macos-14); `Code coverage` (push only, ≥40% floor); `Sanitizers` (ASan+UBSan, TSan).                 |
| **Lint** (`lint.yml`) | push/PR to `main`, `dev`          | Runs every pre-commit hook; **auto-fixes** formatting and pushes a `style:` commit back to the branch. Plus `clang-tidy` and a docs/markdown job. |
| **CodeQL**            | push/PR to `main`, `dev` + weekly | Static security analysis (C++ + Python).                                                                                                          |
| **Secret scan**       | push/PR to `main`, `dev` + weekly | gitleaks.                                                                                                                                         |
| **Dependency Review** | PR to `main`, `dev`               | Flags vulnerable / incompatibly-licensed dependency changes.                                                                                      |

**Required status checks** on both `dev` and `main`: `pre-commit (all hooks)`,
`clang-tidy (static analysis)`, `docs (markdown + links)`,
`Build ubuntu-24.04 · Release`, `Build windows-2022 · Release`, and
`Build macos-14 · Release`. The CI `Promotion source` check also requires PRs
into `main` to come from this repository. Sanitizers and
coverage are informational and are not required for merge. Fix any failing
required check before merging.

Use GitHub Actions logs from the repository checkout:

```bash
gh run list --repo Userbash/AJAZZ-Control-Center --branch dev
gh run view <run-id> --repo Userbash/AJAZZ-Control-Center --log-failed
gh run watch <run-id> --repo Userbash/AJAZZ-Control-Center
gh run download <run-id> --repo Userbash/AJAZZ-Control-Center
```

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

### Step 1 — Prepare the version bump (on a topic branch off `dev`)

```bash
git checkout dev && git pull --ff-only
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
   `dev`**, let CI go green, merge.

### Step 2 — Promote `dev` → `main`

```bash
gh pr create --base main --head dev \
  --title "Promote dev → main (vX.Y.Z)"
```

Wait for the required checks to pass, then merge (a **merge commit**, not
squash — preserve the history). Do **not** delete `dev`.

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
`dev`** so the fix isn't lost on the next promotion.
