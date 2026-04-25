# Release Process

This page describes how releases are produced for AJAZZ Control Center.

## Release channels

| Channel        | Tag pattern   | Workflow      | Trigger              | Audience                           |
| -------------- | ------------- | ------------- | -------------------- | ---------------------------------- |
| **Stable**     | `vX.Y.Z`      | `release.yml` | Pushing a `v*` tag   | End users — recommended            |
| **Nightly**    | `nightly`     | `nightly.yml` | Every push to `main` | Early testers and contributors     |
| **Per-commit** | (artifact)    | `ci.yml`      | Every PR / push      | CI verification only — not a build |

Each channel produces installable packages for all supported platforms:

- Linux: `.deb` (Debian / Ubuntu), `.rpm` (Fedora / openSUSE), `.flatpak`
- Windows: `.msi` installer, portable `.zip`
- macOS: universal `.dmg` (arm64 + x86_64), `.tar.gz`

## Cutting a stable release

1. Update `CHANGELOG.md` with the new version's notable changes (the
   pre-commit hook `Regenerate README + wiki AUTOGEN blocks` will update
   the rendered tables for you).
2. Bump the project version in the top-level `CMakeLists.txt`
   (`project(... VERSION X.Y.Z ...)`).
3. Commit on `main`:

   ```bash
   git commit -am "chore(release): vX.Y.Z"
   git push origin main
   ```

4. Wait for CI to go green on all three OS runners.
5. Tag and push:

   ```bash
   git tag -a vX.Y.Z -m "AJAZZ Control Center vX.Y.Z"
   git push origin vX.Y.Z
   ```

6. The `Release` workflow runs automatically:
   - Builds Linux / Windows / macOS packages in parallel.
   - Builds the Flatpak bundle.
   - Creates a GitHub release with auto-generated release notes and all
     installers attached.

You can also trigger the workflow manually from the Actions tab via the
"Run workflow" button (`workflow_dispatch`).

## Nightly builds

Every push to `main` triggers `nightly.yml`, which:

1. Builds installable packages for Linux / Windows / macOS.
2. Force-moves the rolling tag `nightly` to the current commit.
3. (Re)publishes the GitHub pre-release named `nightly` with the freshly
   built installers.

Users who want to track `main` can always download from the
[`nightly` release page](https://github.com/Aiacos/ajazz-control-center/releases/tag/nightly).

A scheduled run also fires daily at 03:17 UTC so the page reflects the
latest build even on quiet days.

## Versioning

The project follows [Semantic Versioning 2.0.0](https://semver.org).

- **MAJOR** is bumped for breaking changes to the device API, plugin ABI,
  or profile schema.
- **MINOR** is bumped for new device support, new features, or new
  capability interfaces.
- **PATCH** is bumped for bug fixes and protocol corrections that do not
  break compatibility.

## Signing & notarization

Stable releases for macOS and Windows are currently **unsigned**. Users
will see a Gatekeeper / SmartScreen warning the first time they run the
app. Code signing is tracked in
[issue #signing](https://github.com/Aiacos/ajazz-control-center/issues)
and depends on the project obtaining publisher certificates.

## Hotfix process

For an urgent fix on top of an already-released `vX.Y.Z`:

1. Create a branch `release/vX.Y` from the tag.
2. Cherry-pick the fix.
3. Tag `vX.Y.(Z+1)` on that branch and push.

The same `release.yml` will pick it up.
