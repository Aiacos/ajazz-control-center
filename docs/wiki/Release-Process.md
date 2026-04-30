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

> **Building installable artifacts locally?** See
> [Building from Source → Packaging](Building.md#packaging) for the
> Fedora `.rpm` / Debian `.deb` / Flatpak / Windows `.msi` / macOS
> universal `.dmg` recipes that mirror what this CI workflow runs.

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

Stable releases for macOS and Windows ship **signed when the publisher
certificates are configured as repository secrets**, and unsigned
otherwise. The signing steps in `.github/workflows/release.yml` are
gated on `if: env.<SECRET> != ''`, so a fork without the secrets still
builds an installable artifact (with the standard Gatekeeper /
SmartScreen warning on first run).

Tracked under SEC-020 (issue
[#11](https://github.com/Aiacos/ajazz-control-center/issues/11)).

### Required secrets

#### macOS — Developer ID Application + notarization

| Secret                          | Source                                              |
| ------------------------------- | --------------------------------------------------- |
| `APPLE_CERT_BASE64`             | `base64 -i DeveloperID.p12` of the exported cert    |
| `APPLE_CERT_PASSWORD`           | password set on the `.p12` export                   |
| `APPLE_SIGNING_IDENTITY`        | exact `Developer ID Application: Name (TEAMID)`     |
| `APPLE_ID`                      | Apple ID email used for notarization                |
| `APPLE_TEAM_ID`                 | 10-character Team ID from developer.apple.com       |
| `APPLE_APP_SPECIFIC_PASSWORD`   | app-specific password from appleid.apple.com        |

Pipeline: import cert into a temporary keychain → build → `codesign`
the `.app` with `--options runtime` (hardened runtime is required by
notarization) → build the DMG → `codesign` the DMG → submit to
`notarytool --wait` → `stapler staple` the ticket so the DMG installs
offline. Keychain is destroyed in a `always()` cleanup step.

#### Windows — Authenticode signature

| Secret                | Source                                   |
| --------------------- | ---------------------------------------- |
| `WIN_CERT_BASE64`     | `base64 cert.pfx` of the exported PFX    |
| `WIN_CERT_PASSWORD`   | password set on the `.pfx` export        |

Pipeline: write the `.pfx` to a runner-temp file → sign the `.exe`
before MSI packaging (extracted binary stays signed) → build the MSI →
sign the MSI → verify both signatures → delete the `.pfx`. Use a
SmartScreen-friendly EV cert if you can; OV certs work but trigger a
"Unknown Publisher" warning until reputation builds up.

#### Where to get certs

* Apple: <https://developer.apple.com/programs/> ($99/year),
  download "Developer ID Application" cert, export as `.p12`.
* Windows: any CA listed at
  <https://learn.microsoft.com/en-us/windows/security/threat-protection/windows-defender-application-control/> —
  DigiCert, Sectigo, GlobalSign all sell EV Authenticode certs
  (~$300-500/year); standard OV ~$100-200/year.

### Verifying signatures locally

```bash
# macOS — verify the DMG is notarized + signed
spctl --assess --type install <file>.dmg
codesign --verify --deep --strict --verbose=2 /Applications/AjazzControlCenter.app

# Windows — PowerShell
Get-AuthenticodeSignature .\AjazzControlCenter-*.msi | Format-List
```

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

## Hotfix process

For an urgent fix on top of an already-released `vX.Y.Z`:

1. Create a branch `release/vX.Y` from the tag.
2. Cherry-pick the fix.
3. Tag `vX.Y.(Z+1)` on that branch and push.

The same `release.yml` will pick it up.
