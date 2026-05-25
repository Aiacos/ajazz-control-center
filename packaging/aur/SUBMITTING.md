# Submitting AJAZZ Control Center to the AUR

This directory holds the Arch User Repository packaging for
`ajazz-control-center`. It is a **build-from-source** package: the AUR clones
the GitHub release tarball and compiles it on the user's machine via
`makepkg`.

- `PKGBUILD` — the build recipe.
- `.SRCINFO` — machine-readable metadata the AUR web backend reads. Must be
  regenerated whenever `PKGBUILD` metadata changes.

References (Arch Wiki, the source of truth):

- [Arch package guidelines](https://wiki.archlinux.org/title/Arch_package_guidelines)
- [PKGBUILD](https://wiki.archlinux.org/title/PKGBUILD)
- [CMake package guidelines](https://wiki.archlinux.org/title/CMake_package_guidelines)
- [AUR submission guidelines](https://wiki.archlinux.org/title/AUR_submission_guidelines)
- [Arch User Repository](https://wiki.archlinux.org/title/Arch_User_Repository)
- [VCS package guidelines](https://wiki.archlinux.org/title/VCS_package_guidelines)
  (only relevant if a future `-git` package is added)

> All commands below run on an Arch Linux (or derivative) host with
> `base-devel` installed. They are NOT run from this repo's CI and MUST NOT be
> committed into this repository — the AUR is a separate git remote.

______________________________________________________________________

## One-time account + SSH setup

1. Create an account at <https://aur.archlinux.org/register>.

1. Generate an SSH key pair dedicated to the AUR (skip if you reuse an
   existing key):

   ```sh
   ssh-keygen -f ~/.ssh/aur
   ```

1. Add the **public** key to your AUR profile: My Account → "SSH Public Key",
   paste the contents of `~/.ssh/aur.pub`, Save.

1. Point SSH at the right key for the AUR host. Add to `~/.ssh/config`:

   ```
   Host aur.archlinux.org
     IdentityFile ~/.ssh/aur
     User aur
   ```

1. Verify auth (expects an "interactive shell is disabled" greeting, which
   means the key works):

   ```sh
   ssh aur@aur.archlinux.org help
   ```

______________________________________________________________________

## First submission

The AUR git repo for a new package is named after the **pkgbase**
(`ajazz-control-center`). Cloning a not-yet-existing package gives an empty
repo you populate and push.

```sh
git clone ssh://aur@aur.archlinux.org/ajazz-control-center.git
cd ajazz-control-center
```

Copy in the packaging files from this repo (adjust the path to your checkout):

```sh
cp /path/to/ajazz-control-center/packaging/aur/PKGBUILD .
cp /path/to/ajazz-control-center/packaging/aur/.SRCINFO .
```

Regenerate `.SRCINFO` from the PKGBUILD so it is guaranteed in sync (the
committed copy is a convenience snapshot — always regenerate before pushing):

```sh
makepkg --printsrcinfo > .SRCINFO
```

Lint and build-test locally in a clean chroot. `namcap` flags missing/extra
dependencies and packaging mistakes; `makepkg -si` builds and installs:

```sh
namcap PKGBUILD
makepkg -si
namcap ajazz-control-center-*.pkg.tar.zst   # lint the built package too
```

> The clean-chroot build (`extra-x86_64-build` from `devtools`) is the most
> faithful test, because it exposes any missing `makedepends`:
>
> ```sh
> extra-x86_64-build
> ```

Once it builds and runs, commit and push to `master` (the AUR only accepts the
`master` branch):

```sh
git add PKGBUILD .SRCINFO
git commit -m "Initial import: ajazz-control-center 0.1.0-1"
git push origin master
```

The package appears at <https://aur.archlinux.org/packages/ajazz-control-center>
within a few seconds.

> **Do not commit** `pkg/`, `src/`, downloaded tarballs, or built
> `*.pkg.tar.zst` files. The AUR repo should contain only `PKGBUILD`,
> `.SRCINFO`, and any small helper files referenced from `source=()` (none
> here). Add a `.gitignore` in the AUR clone if needed:
>
> ```
> pkg/
> src/
> *.pkg.tar.zst
> *.tar.gz
> ```

______________________________________________________________________

## Per-release update checklist

When a new upstream tag (e.g. `v0.2.0`) ships:

1. **Bump `pkgver`** in `PKGBUILD` to the new version (e.g. `0.2.0`). Reset
   `pkgrel=1`. (Only bump `pkgrel` instead, leaving `pkgver`, when the *package*
   changes but upstream source does not.)

1. **Refresh `sha256sums`.** Recompute against the new tag tarball:

   ```sh
   curl -L https://github.com/Aiacos/ajazz-control-center/archive/refs/tags/v0.2.0.tar.gz \
       | sha256sum
   ```

   Or, with the `pacman-contrib` package, let it rewrite the array in place:

   ```sh
   updpkgsums
   ```

1. **Regenerate `.SRCINFO`** (mandatory — the AUR shows the version from
   `.SRCINFO`, not the PKGBUILD):

   ```sh
   makepkg --printsrcinfo > .SRCINFO
   ```

1. **Re-test:**

   ```sh
   namcap PKGBUILD
   makepkg -si        # or: extra-x86_64-build
   ```

1. **Commit + push:**

   ```sh
   git add PKGBUILD .SRCINFO
   git commit -m "Update to 0.2.0-1"
   git push origin master
   ```

If new build/runtime dependencies appear upstream (e.g. a new Qt module),
update `depends` / `makedepends` / `optdepends` in the same commit and let
`namcap` confirm the set.

______________________________________________________________________

## Why there is no `ajazz-control-center-bin` package

The task considered a binary repackaging variant. It is **deliberately
skipped**:

- The v0.1.0 release ships a `.deb` and `.rpm`, but both are linked against the
  Qt6 / glibc / hidapi sonames of their build distros (Ubuntu, Fedora). Arch
  ships different Qt6 point releases and its own hidapi, so a straight
  `bsdtar`-extract repackage would almost certainly fail to launch with
  unresolved-symbol / soname-mismatch errors — it would not be "cleanly
  doable".
- There is no upstream distro-agnostic prebuilt artifact (no AppImage, no
  static/generic tarball) that a `-bin` package could wrap reliably.

The from-source `PKGBUILD` compiles against the user's actual Arch libraries,
which is the correct and supported path. Revisit a `-bin` package only if
upstream starts publishing an AppImage or a generic relocatable tarball.
