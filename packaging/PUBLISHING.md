# Publishing & release sync

`.github/workflows/release.yml` builds and publishes the installers on every
tagged release (`.deb` `.rpm` `.flatpak` `.dmg` `.msi` `.zip` + `SHA256SUMS`).
Each package manager is then a separate, mostly-moderated submission.

## Channel index

| Channel                  | Manifest                   | Submission guide                                   |
| ------------------------ | -------------------------- | -------------------------------------------------- |
| winget                   | `packaging/winget/<ver>/`  | this file (below)                                  |
| Chocolatey               | `packaging/chocolatey/`    | this file (below)                                  |
| Flathub                  | `packaging/flathub/`       | [`flathub/SUBMITTING.md`](flathub/SUBMITTING.md)   |
| Fedora (Copr / official) | `packaging/fedora/`        | [`fedora/SUBMITTING.md`](fedora/SUBMITTING.md)     |
| Ubuntu (PPA)             | `packaging/ubuntu/debian/` | [`ubuntu/SUBMITTING.md`](ubuntu/SUBMITTING.md)     |
| Arch (AUR)               | `packaging/aur/`           | [`aur/SUBMITTING.md`](aur/SUBMITTING.md)           |
| Snap                     | `packaging/snap/`          | [`snap/SUBMITTING.md`](snap/SUBMITTING.md)         |
| Homebrew (macOS)         | `packaging/homebrew/`      | [`homebrew/SUBMITTING.md`](homebrew/SUBMITTING.md) |

## Automated sync on release

`.github/workflows/sync-packages.yml` runs on `release: published` (or manual
dispatch with a tag). It runs `scripts/sync-packaging.py`, which rewrites the
version + download URL + SHA256 (+ MSI ProductCode) across every manifest above
from the release's own `SHA256SUMS` and source tarball — one source of truth, no
hand-editing — then commits the bumped manifests to `main`.

Re-submission to each store is then auto-run **only for channels whose secret is
configured** (otherwise skipped, so the workflow is safe with no secrets):

| Channel      | Repo secret                                  | Mechanism                                                            |
| ------------ | -------------------------------------------- | -------------------------------------------------------------------- |
| winget       | `WINGET_TOKEN` (GitHub PAT, public_repo)     | `wingetcreate update --submit` job (wired)                           |
| Chocolatey   | `CHOCO_API_KEY`                              | `choco push` (add a job; see Chocolatey below)                       |
| AUR          | `AUR_SSH_KEY` (deploy key on the AUR repo)   | `git push ssh://aur@aur.archlinux.org/...`                           |
| Snap         | `SNAPCRAFT_TOKEN` (`snapcraft export-login`) | `snapcraft upload --release=stable`                                  |
| Homebrew tap | `HOMEBREW_TAP_TOKEN`                         | push the bumped cask to `Aiacos/homebrew-tap`                        |
| Flathub      | —                                            | the Flathub bot proposes update PRs after the first merge            |
| Fedora Copr  | Copr SCM webhook                             | Copr auto-rebuilds from the tag                                      |
| Ubuntu PPA   | GPG key                                      | `debuild -S` + `dput` (sign locally; PPA recipe can also auto-build) |

Local manual run: `python scripts/sync-packaging.py --tag vX.Y.Z` then review
`git diff packaging/`.

______________________________________________________________________

## Windows: winget & Chocolatey

Getting the MSI into **winget** and **Chocolatey** is a moderated submission per
package manager — both go through human/automated review before users can install.

The per-release facts you need (from the MSI built by CI):

| Field          | v0.1.0 value                                                                                                   |
| -------------- | -------------------------------------------------------------------------------------------------------------- |
| Installer URL  | `https://github.com/Aiacos/ajazz-control-center/releases/download/v0.1.0/ajazz-control-center-0.1.0-win64.msi` |
| SHA256         | `3A91E4A5C438BB2C705941888431F8DB8D76D4C11D6986F1D5AD3737289424BA` (matches the release `SHA256SUMS`)          |
| ProductCode    | `{95C04736-6ED7-49B6-879E-534784599357}`                                                                       |
| ProductVersion | `0.1.0`                                                                                                        |

> The MSI is **unsigned** until a code-signing cert is wired into the release
> workflow (`WIN_CERT_BASE64` secret). Both stores accept unsigned installers,
> but SmartScreen warns on first run and winget/Choco moderation may comment.

______________________________________________________________________

## winget (microsoft/winget-pkgs)

Manifests live in [`packaging/winget/<version>/`](winget/) (3 YAML files:
version, defaultLocale, installer). Submitting = opening a PR that copies them
to `manifests/a/Aiacos/AjazzControlCenter/<version>/` in
[`microsoft/winget-pkgs`](https://github.com/microsoft/winget-pkgs).

Recommended path — `wingetcreate` (validates + forks + PRs for you):

```powershell
winget install Microsoft.WingetCreate
wingetcreate update Aiacos.AjazzControlCenter `
  --version 0.1.0 `
  --urls "https://github.com/Aiacos/ajazz-control-center/releases/download/v0.1.0/ajazz-control-center-0.1.0-win64.msi" `
  --submit --token <GITHUB_PAT>
```

Or manual: `winget validate packaging/winget/0.1.0`, then fork winget-pkgs, copy
the 3 files under `manifests/a/Aiacos/AjazzControlCenter/0.1.0/`, and open a PR.
An automated bot validates (installer reachable, SHA matches, schema) and a
moderator reviews; once merged, `winget install Aiacos.AjazzControlCenter` works.

______________________________________________________________________

## Chocolatey (community.chocolatey.org)

The package source lives in [`packaging/chocolatey/`](chocolatey/):
`ajazz-control-center.nuspec` + `tools/chocolateyinstall.ps1` +
`tools/chocolateyuninstall.ps1`. The install script downloads the GitHub-hosted
MSI and verifies its SHA256 (Chocolatey policy: community packages must download
from official, durable URLs, not bundle the binary).

One-time account setup: register at <https://community.chocolatey.org>, then copy
your key from <https://community.chocolatey.org/account> and save it:

```powershell
choco apikey add --source "https://push.chocolatey.org/" --key "<API_KEY>"
```

Build, test locally, and push:

```powershell
cd packaging/chocolatey
choco pack                              # -> ajazz-control-center.0.1.0.nupkg
# local install test (elevated):
choco install ajazz-control-center -s ".;https://community.chocolatey.org/api/v2/" -y
choco push ajazz-control-center.0.1.0.nupkg --source "https://push.chocolatey.org/"
```

After `choco push` the package enters **moderation**: an automated validator
checks the nuspec (requires `projectUrl`, `packageSourceUrl`, `tags`,
`releaseNotes`, `iconUrl`, `licenseUrl` — all present here) and a verifier VM
test-installs it; then a human moderator reviews. Respond to any comments on the
package page; once approved it is publicly installable with
`choco install ajazz-control-center`.

### Nuspec validation rules we satisfy (package-validator)

`projectUrl`, `packageSourceUrl`, `licenseUrl`, `iconUrl`, `tags`,
`releaseNotes`, `summary`, `description`, `bugTrackerUrl`, `docsUrl`,
`requireLicenseAcceptance=false`. See the
[Chocolatey moderation rules](https://docs.chocolatey.org/en-us/community-repository/moderation/).

______________________________________________________________________

## Per-release checklist

For each new tag, refresh the MSI URL + SHA256 + ProductCode (above) in:

- `packaging/winget/<version>/Aiacos.AjazzControlCenter.installer.yaml`
- `packaging/chocolatey/ajazz-control-center.nuspec` (`<version>`) and
  `tools/chocolateyinstall.ps1` (`url64bit`, `checksum64`)

then re-run the submission steps for that version.
