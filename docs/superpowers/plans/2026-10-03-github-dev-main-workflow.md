# GitHub dev/main workflow

## Goal

Use `dev` as the integration and default branch, and accept changes into
`main` only through a pull request with successful CI checks. The normal
release path is `dev` to `main`, while reviewed in-repository topic branches
may target `main` for an urgent fix. Keep release workflows and tags on `main`.

## Work

1. Align CI, lint, security, and dependency-review triggers with `dev` and
   `main`.
1. Update the release guide with the branch flow and GitHub Actions log
   commands.
1. Set the GitHub repository default branch to `dev`, protect `dev` and
   `main`, and configure required status checks.
1. Put the local checkout on `dev` tracking `origin/dev` and verify settings,
   YAML, and working-tree state.

## Artifact policy

Keep CI build products ephemeral on runners. Keep release/nightly packages in
GitHub Releases; keep only the coverage report as a short-lived Actions
artifact. Retain existing per-runner build caches. Local `build/dev` and
`build/release` remain useful; `build/dev.stale` is a removable stale cache,
but will be left untouched.

## Constraints

Do not commit or push. Existing GitHub Actions failures are recorded as
follow-up work, not silently marked as passing.
