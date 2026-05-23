# Submitting AJAZZ Control Center to Homebrew (macOS Cask)

This directory contains a Homebrew **Cask** for the macOS Universal `.dmg`
published on GitHub Releases:

- Cask file: [`Casks/ajazz-control-center.rb`](Casks/ajazz-control-center.rb)

There are two ways to ship it. Read both — the **personal tap** (Path A) is the
realistic first step today; the **official `homebrew-cask`** (Path B) has
notability and Gatekeeper expectations our currently-unsigned DMG likely does
not meet.

> Reference docs (read these for current rules):
> - Cask Cookbook — <https://docs.brew.sh/Cask-Cookbook>
> - Acceptable Casks — <https://docs.brew.sh/Acceptable-Casks>
> - Adding Software to Homebrew — <https://docs.brew.sh/Adding-Software-to-Homebrew>
> - How to Create and Maintain a Tap — <https://docs.brew.sh/How-to-Create-and-Maintain-a-Tap>
> - Taps (Third-Party Repositories) — <https://docs.brew.sh/Taps>

---

## Path A — Personal tap (`Aiacos/homebrew-tap`) — recommended first

A personal tap is a GitHub repo named `homebrew-<something>`. Users install
from it without it ever touching the official Homebrew repos. This is the
fastest, fully-under-your-control distribution path and has **no notability or
notarization gate**.

### One-time setup

```sh
# Creates a local tap skeleton with a Casks/ dir and CI workflow templates.
# The repo on GitHub MUST be named "homebrew-tap" so the short
# `brew tap aiacos/tap` form resolves to github.com/Aiacos/homebrew-tap.
brew tap-new Aiacos/tap
```

`brew tap-new` scaffolds the tap under
`$(brew --repository)/Library/Taps/aiacos/homebrew-tap` with a `Casks/`
directory and a ready-made GitHub Actions workflow (`tests.yml`) that runs
`brew test-bot` on PRs.

### Add the cask

```sh
TAP_DIR="$(brew --repository)/Library/Taps/aiacos/homebrew-tap"

# Copy this repo's cask into the tap.
cp packaging/homebrew/Casks/ajazz-control-center.rb "$TAP_DIR/Casks/"

cd "$TAP_DIR"
git add Casks/ajazz-control-center.rb
git commit -m "ajazz-control-center 0.1.0 (new cask)"

# Create the GitHub repo named exactly "homebrew-tap" and push.
gh repo create Aiacos/homebrew-tap --public --source=. --remote=origin --push
# (or: git remote add origin git@github.com:Aiacos/homebrew-tap.git && git push -u origin HEAD)
```

### Validate locally before pushing

```sh
brew style packaging/homebrew/Casks/ajazz-control-center.rb
brew audit --cask --online packaging/homebrew/Casks/ajazz-control-center.rb

# Full install/uninstall round-trip from the live tap:
brew tap aiacos/tap
brew install --cask aiacos/tap/ajazz-control-center
brew uninstall --cask ajazz-control-center
brew untap aiacos/tap   # optional cleanup
```

### What users run

```sh
brew tap aiacos/tap
brew install --cask aiacos/tap/ajazz-control-center
# After the tap is added, the short form also works:
# brew install --cask ajazz-control-center
```

Upgrades come automatically via `brew upgrade` once you push a new version to
the tap (see the per-release checklist at the bottom).

---

## Path B — Official `Homebrew/homebrew-cask`

This puts the cask in `brew install --cask ajazz-control-center` for everyone,
with no tap step. Per **Acceptable Casks**, there are gates we must clear first.

### Acceptability gates (read carefully)

1. **Notability.** Self-submitted GitHub apps (i.e. submitted by the repo owner)
   need roughly **90 forks, 90 watchers, and 225 stars**; third-party
   submissions need **30 forks, 30 watchers, 75 stars**. These are guidelines,
   not hard guarantees — maintainers have discretion. As of v0.1.0 this project
   is very unlikely to clear the self-submission bar yet.

2. **Gatekeeper / signing.** Acceptable Casks **rejects apps that fail with
   Gatekeeper enabled** on supported macOS versions. Our DMG is currently
   **unsigned and un-notarized**; on Apple Silicon an unsigned/un-notarized app
   often will not launch for a normal user, which is grounds for rejection.
   Homebrew does not *mandate* notarization in the abstract, but the app must
   actually run under Gatekeeper. **Sign + notarize the build before attempting
   Path B.**

3. No SIP-disable requirement, no `allow_untrusted`, must be maintained, no
   open security issues.

Bottom line: do **not** open a `homebrew-cask` PR until (a) the app is signed +
notarized and (b) notability is plausible. Until then, Path A is the supported
distribution channel.

### When ready, the submission flow

```sh
# Fork & clone homebrew-cask (or let `brew` manage the tap clone):
brew tap homebrew/cask           # if not already present
cd "$(brew --repository homebrew/cask)"
git checkout -b ajazz-control-center

# Drop the cask in (homebrew-cask shards Casks/ by first letter):
cp /path/to/ajazz-control-center/packaging/homebrew/Casks/ajazz-control-center.rb Casks/a/

git add Casks/a/ajazz-control-center.rb
git commit -m "ajazz-control-center 0.1.0 (new cask)"
```

Required checks before opening the PR:

```sh
brew style --fix Casks/a/ajazz-control-center.rb
brew audit --new --cask Casks/a/ajazz-control-center.rb

# Install / uninstall test against the local file:
export HOMEBREW_NO_AUTO_UPDATE=1
export HOMEBREW_NO_INSTALL_FROM_API=1
brew install --cask Casks/a/ajazz-control-center.rb
brew uninstall --cask ajazz-control-center
```

Then push the branch to your fork and open a PR against
`Homebrew/homebrew-cask`. Use the commit/PR title format
`ajazz-control-center 0.1.0 (new cask)`. CI (`brew test-bot`) runs audit +
style + an install test on a fresh runner; fix anything it flags.

> Note on `livecheck`: the cask uses `strategy :github_latest`. Homebrew's
> autobump bot relies on a working `livecheck`. If you would rather bump
> manually, add `no_autobump! because: :requires_manual_review` (or another
> documented reason) per the Cask Cookbook.

---

## Per-release update checklist

Run this for every new tagged release (`vX.Y.Z`). It applies to **both** the
personal tap and (if accepted) the official cask.

1. Confirm the release assets exist on GitHub:
   - `ajazz-control-center-X.Y.Z-Darwin.dmg`
   - `SHA256SUMS`
2. Get the new checksum (do NOT trust an old value):
   ```sh
   curl -fsSL https://github.com/Aiacos/ajazz-control-center/releases/download/vX.Y.Z/SHA256SUMS \
     | grep 'Darwin.dmg'
   # or, by downloading the dmg:
   # shasum -a 256 ajazz-control-center-X.Y.Z-Darwin.dmg
   ```
3. Edit `Casks/ajazz-control-center.rb`:
   - bump `version "X.Y.Z"`
   - replace `sha256 "..."` with the new Darwin.dmg hash
   - (the `url` is version-templated via `#{version}`, so it needs no edit)
4. Re-validate:
   ```sh
   brew style  Casks/ajazz-control-center.rb
   brew audit --cask --online Casks/ajazz-control-center.rb
   ```
5. Commit with the conventional message
   `ajazz-control-center X.Y.Z (update)` and push to the tap (Path A) or open a
   bump PR / let `brew bump-cask-pr` handle it for the official cask (Path B):
   ```sh
   brew bump-cask-pr --version X.Y.Z ajazz-control-center
   ```
6. If the bundle ever gains an Apple Developer ID signature + notarization,
   drop the unsigned caveat from the cask and reconsider Path B.

---

## Notes / things to verify per release

- **`.app` bundle name** is `AJAZZ Control Center.app` — confirmed from
  `src/app/CMakeLists.txt` (`MACOSX_BUNDLE_BUNDLE_NAME "AJAZZ Control Center"`)
  and the DMG volume name `CPACK_DMG_VOLUME_NAME "AJAZZ Control Center"`. If the
  product name ever changes, update the `app` stanza and the `zap`/caveat paths.
- **Bundle identifier** is `io.github.Aiacos.AjazzControlCenter` — used for the
  `zap trash:` Preferences/Caches paths.
- The `zap` paths are a best-effort superset (Application Support under both the
  display name and the bundle id, Preferences, Caches, HTTPStorages, Saved
  Application State). Trim or extend them if the app's real on-disk footprint
  is confirmed to differ.
